#include "protocol.h"
#include "config.h"
#include <math.h>
#include <string.h>

namespace pifl {
namespace {
void write32(uint8_t* b, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) b[i] = static_cast<uint8_t>(value >> (8*i));
}
uint32_t read32(const uint8_t* b) {
  uint32_t value = 0;
  for (unsigned i = 0; i < 4; ++i) value |= static_cast<uint32_t>(b[i]) << (8*i);
  return value;
}
void write16(uint8_t* b, uint16_t value) { b[0] = value & 255; b[1] = value >> 8; }
uint16_t read16(const uint8_t* b) { return b[0] | (static_cast<uint16_t>(b[1]) << 8); }
void writeFloat(uint8_t* b, float value) {
  static_assert(sizeof(float) == 4, "Protocol requires IEEE-754 float32");
  uint32_t bits; memcpy(&bits, &value, 4); write32(b, bits);
}
float readFloat(const uint8_t* b) { uint32_t bits = read32(b); float v; memcpy(&v, &bits, 4); return v; }
void writeModel(uint8_t* b, const ModelParameters& m) {
  writeFloat(b, m.w1); writeFloat(b+4, m.w2); writeFloat(b+8, m.b);
}
ModelParameters readModel(const uint8_t* b) { return {readFloat(b), readFloat(b+4), readFloat(b+8)}; }
bool header(const uint8_t* b, size_t n, PacketType expected, size_t size) {
  return b && n == size && b[0] != 0 && b[1] == static_cast<uint8_t>(expected) && b[2] == kProtocolVersion;
}
void writeHeader(uint8_t* b, uint8_t id, PacketType type) {
  b[0] = id; b[1] = static_cast<uint8_t>(type); b[2] = kProtocolVersion;
}
}
bool isValidTemperature(float value) {
  return isfinite(value) && value >= kMinTemperatureC && value <= kMaxTemperatureC;
}
bool isValidModel(const ModelParameters& m) {
  return isfinite(m.w1) && isfinite(m.w2) && isfinite(m.b) &&
      fabsf(m.w1) <= kMaxAbsW1 && fabsf(m.w2) <= kMaxAbsW2 && fabsf(m.b) <= kMaxAbsB;
}
bool isNewerRound(uint16_t candidate, uint16_t previous) {
  const uint16_t delta = static_cast<uint16_t>(candidate - previous);
  return delta != 0 && delta < 0x8000u;
}
bool getPacketType(const uint8_t* b, size_t n, PacketType& type, uint8_t& id) {
  if (!b || n < kPacketHeaderSize || b[0] == 0 || b[2] != kProtocolVersion || b[1] < 1 || b[1] > 4) return false;
  id = b[0]; type = static_cast<PacketType>(b[1]); return true;
}
size_t encodeTemperaturePacket(uint8_t id, float t, uint8_t* b, size_t cap) {
  if (!b || !id || cap < kTemperaturePacketSize || !isValidTemperature(t)) return 0;
  writeHeader(b, id, PacketType::Temperature); writeFloat(b+3, t); return kTemperaturePacketSize;
}
size_t encodeModelPacket(const ModelPacket& p, uint8_t* b, size_t cap) {
  if (!b || !p.nodeId || cap < kModelPacketSize || !isValidModel(p.model) || !p.sampleCount ||
      !isfinite(p.meanLocalLoss) || p.meanLocalLoss < 0 || !p.senderBootId ||
      !((p.baseLeaderId == 0 && p.baseLeaderBootId == 0 && p.baseRound == 0) ||
        (p.baseLeaderId != 0 && p.baseLeaderBootId != 0))) return 0;
  writeHeader(b, p.nodeId, PacketType::ModelUpdate); writeModel(b+3, p.model);
  write32(b+15, p.sampleCount); write16(b+19, p.round); writeFloat(b+21, p.meanLocalLoss);
  write16(b+25, p.baseRound); b[27] = p.baseLeaderId; write32(b+28, p.baseLeaderBootId); write32(b+32, p.senderBootId);
  return kModelPacketSize;
}
size_t encodeAggregationPacket(const AggregationPacket& p, uint8_t* b, size_t cap) {
  if (!b || !p.nodeId || !p.leaderBootId || cap < kAggregationPacketSize || !isValidModel(p.model) ||
      !isfinite(p.meanLocalLoss) || p.meanLocalLoss < 0) return 0;
  writeHeader(b, p.nodeId, PacketType::GlobalAggregation); writeModel(b+3, p.model);
  write16(b+15, p.round); writeFloat(b+17, p.meanLocalLoss); write32(b+21, p.leaderBootId);
  return kAggregationPacketSize;
}
size_t encodeDiscoveryPacket(const DiscoveryPacket& p, uint8_t* b, size_t cap) {
  if (!b || !p.nodeId || !p.bootId || cap < kDiscoveryPacketSize || (p.roleFlags & ~kRoleFlagAggregatorEligible)) return 0;
  writeHeader(b, p.nodeId, PacketType::Discovery); b[3] = p.roleFlags; write32(b+4, p.bootId);
  return kDiscoveryPacketSize;
}
bool decodeTemperaturePacket(const uint8_t* b, size_t n, TemperaturePacket& p) {
  if (!header(b,n,PacketType::Temperature,kTemperaturePacketSize)) return false;
  p = {b[0],readFloat(b+3)}; return isValidTemperature(p.temperatureC);
}
bool decodeModelPacket(const uint8_t* b, size_t n, ModelPacket& p) {
  if (!header(b,n,PacketType::ModelUpdate,kModelPacketSize)) return false;
  p = {b[0],readModel(b+3),read32(b+15),readFloat(b+21),read16(b+19),read16(b+25),b[27],read32(b+28),read32(b+32)};
  return isValidModel(p.model) && p.senderBootId && p.sampleCount > 0 && isfinite(p.meanLocalLoss) && p.meanLocalLoss >= 0 &&
      ((p.baseLeaderId == 0 && p.baseLeaderBootId == 0 && p.baseRound == 0) ||
       (p.baseLeaderId != 0 && p.baseLeaderBootId != 0));
}
bool decodeAggregationPacket(const uint8_t* b, size_t n, AggregationPacket& p) {
  if (!header(b,n,PacketType::GlobalAggregation,kAggregationPacketSize)) return false;
  p = {b[0],readModel(b+3),readFloat(b+17),read16(b+15),read32(b+21)};
  return isValidModel(p.model) && p.leaderBootId && isfinite(p.meanLocalLoss) && p.meanLocalLoss >= 0;
}
bool decodeDiscoveryPacket(const uint8_t* b, size_t n, DiscoveryPacket& p) {
  if (!header(b,n,PacketType::Discovery,kDiscoveryPacketSize)) return false;
  p = {b[0],b[3],read32(b+4)}; return p.bootId && !(p.roleFlags & ~kRoleFlagAggregatorEligible);
}
}
