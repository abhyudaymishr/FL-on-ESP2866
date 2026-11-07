// Version 2 codec. The FNV envelope is an integrity checksum, not authentication.
const VERSION = 2;
const SIZES = {1: 7, 2: 36, 3: 25, 4: 8};
const DEFAULT_KEY = process.env.PIFL_NETWORK_SECRET || 'pifl-development-only';
function checksum(payload, key = DEFAULT_KEY) {
  let h = 2166136261;
  for (const byte of Buffer.concat([payload, Buffer.from(key, 'utf8')])) {
    h = Math.imul(h ^ byte, 16777619) >>> 0;
  }
  return h;
}
function newerRound(candidate, previous) {
  const delta = (candidate - previous) & 0xffff;
  return delta > 0 && delta < 0x8000;
}
function parsePacket(wire, key = DEFAULT_KEY) {
  if (!Buffer.isBuffer(wire) || wire.length < 7 || wire.length > 40) return null;
  const payload = wire.subarray(0, -4);
  if (wire.readUInt32LE(wire.length - 4) !== checksum(payload, key)) return null;
  const [nodeId, typeId, version] = payload;
  if (!nodeId || version !== VERSION || payload.length !== SIZES[typeId]) return null;
  const packet = {nodeId, typeId, version, timestamp: Date.now()};
  if (typeId === 1) {
    const temperatureC = payload.readFloatLE(3);
    return Number.isFinite(temperatureC) && temperatureC >= -55 && temperatureC <= 125 ?
      {...packet, type: 'Temperature', temperatureC} : null;
  }
  if (typeId === 4) {
    const roleFlags = payload[3], bootId = payload.readUInt32LE(4);
    return bootId && !(roleFlags & ~1) ? {...packet, type: 'Discovery', roleFlags, bootId} : null;
  }
  const model = {w1: payload.readFloatLE(3), w2: payload.readFloatLE(7), b: payload.readFloatLE(11)};
  if (!Object.values(model).every((v, i) => Number.isFinite(v) && Math.abs(v) <= [2, .5, 10][i])) return null;
  if (typeId === 2) {
    const sampleCount = payload.readUInt32LE(15), round = payload.readUInt16LE(19);
    const loss = payload.readFloatLE(21), baseRound = payload.readUInt16LE(25);
    const baseLeaderId = payload[27], baseLeaderBootId = payload.readUInt32LE(28);
    const senderBootId = payload.readUInt32LE(32);
    const validBase = (!baseLeaderId && !baseLeaderBootId && !baseRound) || (baseLeaderId && baseLeaderBootId);
    return sampleCount && senderBootId && validBase && Number.isFinite(loss) && loss >= 0 ?
      {...packet, type: 'ModelUpdate', model, sampleCount, round, loss, baseRound, baseLeaderId, baseLeaderBootId, senderBootId} : null;
  }
  const round = payload.readUInt16LE(15), meanContributorLoss = payload.readFloatLE(17);
  const leaderBootId = payload.readUInt32LE(21);
  return leaderBootId && Number.isFinite(meanContributorLoss) && meanContributorLoss >= 0 ?
    {...packet, type: 'GlobalAggregation', model, round, meanContributorLoss, leaderBootId} : null;
}
module.exports = {parsePacket, checksum, newerRound};
