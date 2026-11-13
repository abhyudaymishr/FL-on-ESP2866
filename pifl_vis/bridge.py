"""Bounded UDP-to-HTTPS telemetry bridge; no cloud credentials on ESP8266s."""
from __future__ import annotations

import argparse
import json
import logging
import os
import re
import signal
import socket
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid

from .protocol import NETWORK_SECRET, decode_packet, newer_round
from .server import DashboardState, load_layout, process_datagram
from pathlib import Path

LOG = logging.getLogger("pifl.bridge")
MAX_NODES = 12


class Gateway:
    def __init__(self, gateway_id, port=4210, layout=None, key=NETWORK_SECRET, aggregator_id=0):
        if not re.fullmatch(r"[a-z0-9][a-z0-9-]{0,31}", gateway_id):
            raise ValueError("Invalid gateway ID")
        self.gateway_id = gateway_id
        self.session_id = str(uuid.uuid4())
        self.key = key
        self.state = DashboardState(port, 0, layout or {}, aggregator_id)
        self.rejected = 0
        self.last_capture = 0

    def receive(self, wire, source):
        decoded = decode_packet(wire, self.key)
        if decoded is None:
            self.rejected += 1
            return False
        node_id = decoded["node_id"]
        with self.state.lock:
            # Never evict a live node to admit an arbitrary packet.
            if node_id not in self.state.nodes and len(self.state.nodes) >= MAX_NODES:
                self.rejected += 1
                return False
            if decoded["kind"] == "model":
                node = self.state.nodes.get(node_id)
                if node is None or node.boot_id != decoded["sender_boot_id"]:
                    self.rejected += 1
                    return False
                if node.model_seen and not newer_round(decoded["round"], node.round):
                    self.rejected += 1
                    return False
        accepted = process_datagram(self.state, wire, source, self.key)
        if not accepted:
            self.rejected += 1
        return accepted

    def snapshot(self, epoch_ms=None, monotonic=None):
        wall = int(time.time() * 1000) if epoch_ms is None else epoch_ms
        mono = time.monotonic() if monotonic is None else monotonic
        # Do not create duplicate capture IDs on rapid retries or clock granularity.
        captured = max(wall, self.last_capture + 1)
        self.last_capture = captured
        def stamp(seen):
            return max(0, captured - int(max(0, mono - seen) * 1000)) if seen else None
        with self.state.lock:
            nodes = [{"id": n.node_id, "label": n.label[:48], "bootId": n.boot_id,
                      "round": n.round, "sampleCount": n.sample_count, "roleFlags": n.role_flags,
                      "seenAt": stamp(n.last_seen), "temperatureAt": stamp(n.temperature_seen),
                      "modelAt": stamp(n.model_seen), "temperature": n.temperature_c, "loss": n.loss,
                      "model": {"w1": n.model.w1, "w2": n.model.w2, "b": n.model.b}}
                     for n in sorted(self.state.nodes.values(), key=lambda n: n.node_id)]
            aggregate = None
            leader = self.state.nodes.get(self.state.global_sender)
            if self.state.global_model and leader and leader.boot_id == self.state.global_boot_id:
                m = self.state.global_model
                aggregate = {"leaderId": self.state.global_sender, "bootId": self.state.global_boot_id,
                             "round": self.state.global_round, "seenAt": stamp(self.state.global_seen),
                             "loss": self.state.mean_contributor_loss,
                             "model": {"w1": m.w1, "w2": m.w2, "b": m.b}}
            return {"schemaVersion": 1, "gatewayId": self.gateway_id, "sessionId": self.session_id,
                    "capturedAt": captured, "nodes": nodes, "aggregate": aggregate,
                    "counters": {**self.state.packet_counters, "rejected": self.rejected}}


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        # Never forward the gateway bearer token to another location.
        return None


def send_snapshot(endpoint, token, snapshot, opener=None):
    url = urllib.parse.urlsplit(endpoint)
    if url.scheme != "https" or not url.netloc or url.username or url.password or url.query or url.fragment:
        raise ValueError("Bridge endpoint must be a plain HTTPS origin")
    data = json.dumps(snapshot, allow_nan=False, separators=(",", ":")).encode()
    if len(data) > 32768:
        raise ValueError("Snapshot exceeds 32 KiB")
    request = urllib.request.Request(endpoint.rstrip("/") + "/api/ingest", data=data,
        headers={"Authorization": f"Bearer {token}", "Content-Type": "application/json"}, method="POST")
    client = opener or urllib.request.build_opener(NoRedirect())
    with client.open(request, timeout=10) as response:
        result = json.loads(response.read(4096))
    if not isinstance(result.get("accepted"), bool):
        raise ValueError("Unexpected ingestion response")
    return result["accepted"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gateway-id", default=os.environ.get("PIFL_GATEWAY_ID", "lab-01"))
    parser.add_argument("--endpoint", default=os.environ.get("PIFL_CLOUD_URL"))
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--udp-port", type=int, default=4210)
    parser.add_argument("--interval", type=int, default=30)
    parser.add_argument("--aggregator-id", type=int, default=0)
    parser.add_argument("--layout", type=Path)
    args = parser.parse_args()
    token = os.environ.get("PIFL_GATEWAY_TOKEN", "")
    if len(token) < 32 or not args.endpoint:
        parser.error("Set PIFL_GATEWAY_TOKEN (32+ characters) and PIFL_CLOUD_URL")
    if args.interval < 30:
        parser.error("The free-tier bridge interval must be at least 30 seconds")
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    gateway = Gateway(args.gateway_id, args.udp_port, load_layout(args.layout), aggregator_id=args.aggregator_id)
    stop = threading.Event()
    for sig in (signal.SIGINT, signal.SIGTERM):
        signal.signal(sig, lambda *_: stop.set())
    def upload():
        delay = args.interval
        while not stop.is_set():
            # Latest state wins. No stale backlog is replayed after outages.
            try:
                accepted = send_snapshot(args.endpoint, token, gateway.snapshot())
                LOG.info("snapshot %s", "accepted" if accepted else "already superseded")
                delay = args.interval
            except (urllib.error.URLError, OSError, ValueError):
                delay = min(delay * 2, 300)
                LOG.warning("Upload failed; retrying latest state in %ss", delay)
            stop.wait(delay)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind((args.bind, args.udp_port))
        sock.settimeout(.5)
        uploader = threading.Thread(target=upload, daemon=True)
        uploader.start()
        LOG.info("Listening on UDP %s; gateway %s", args.udp_port, args.gateway_id)
        try:
            while not stop.is_set():
                try:
                    wire, address = sock.recvfrom(2048)
                    gateway.receive(wire, f"{address[0]}:{address[1]}")
                except socket.timeout:
                    pass
        finally:
            stop.set()
            uploader.join(timeout=11)


if __name__ == "__main__":
    main()
