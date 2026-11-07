"""Version 2 wire codec. FNV tags provide development integrity, not security."""
from __future__ import annotations

import math
import os
import struct
from dataclasses import dataclass

TEMPERATURE_PACKET, MODEL_PACKET, AGGREGATION_PACKET, DISCOVERY_PACKET = 1, 2, 3, 4
PROTOCOL_VERSION = 2
ROLE_FLAG_AGGREGATOR_ELIGIBLE = 1
NETWORK_SECRET = os.environ.get('PIFL_NETWORK_SECRET', 'pifl-development-only')
PACKET_SIZES = {1: 7, 2: 36, 3: 25, 4: 8}
MAX_PACKET_SIZE = max(PACKET_SIZES.values())

@dataclass
class ModelParameters:
    w1: float = 0.0
    w2: float = 0.0
    b: float = 0.0


def checksum(payload: bytes, key: str = NETWORK_SECRET) -> int:
    value = 2166136261
    for byte in payload + key.encode('utf-8'):
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value


def wrap_packet(payload: bytes, key: str = NETWORK_SECRET) -> bytes:
    if not 3 <= len(payload) <= MAX_PACKET_SIZE:
        raise ValueError('Invalid payload length')
    return payload + struct.pack('<I', checksum(payload, key))


def newer_round(candidate: int, previous: int) -> bool:
    return 0 < ((candidate - previous) & 0xffff) < 0x8000


def decode_packet(wire: bytes, key: str = NETWORK_SECRET) -> dict | None:
    if not 7 <= len(wire) <= MAX_PACKET_SIZE + 4:
        return None
    payload, tag = wire[:-4], wire[-4:]
    if struct.unpack('<I', tag)[0] != checksum(payload, key):
        return None
    node_id, kind, version = payload[:3]
    if not node_id or version != PROTOCOL_VERSION or len(payload) != PACKET_SIZES.get(kind):
        return None
    result = {'node_id': node_id}
    if kind == TEMPERATURE_PACKET:
        temperature = struct.unpack_from('<f', payload, 3)[0]
        if not math.isfinite(temperature) or not -55 <= temperature <= 125:
            return None
        return dict(result, kind='temperature', temperature_c=temperature)
    if kind == DISCOVERY_PACKET:
        roles, boot_id = struct.unpack_from('<BI', payload, 3)
        if roles & ~1 or not boot_id:
            return None
        return dict(result, kind='discovery', role_flags=roles, boot_id=boot_id)
    model = ModelParameters(*struct.unpack_from('<fff', payload, 3))
    if not all(math.isfinite(v) and abs(v) <= limit for v, limit in
               zip((model.w1, model.w2, model.b), (2, .5, 10))):
        return None
    if kind == MODEL_PACKET:
        count, round_num, loss, base_round, leader_id, boot_id, sender_boot_id = struct.unpack_from('<IHfHBII', payload, 15)
        if not count or not sender_boot_id or not math.isfinite(loss) or loss < 0:
            return None
        if not ((leader_id == boot_id == base_round == 0) or (leader_id != 0 and boot_id != 0)):
            return None
        return dict(result, kind='model', model=model, sample_count=count, round=round_num,
                    mean_local_loss=loss, base_round=base_round, base_leader_id=leader_id,
                    base_leader_boot_id=boot_id, sender_boot_id=sender_boot_id)
    round_num, loss, boot_id = struct.unpack_from('<HfI', payload, 15)
    if not boot_id or not math.isfinite(loss) or loss < 0:
        return None
    return dict(result, kind='aggregation', model=model, round=round_num,
                mean_contributor_loss=loss, leader_boot_id=boot_id)
