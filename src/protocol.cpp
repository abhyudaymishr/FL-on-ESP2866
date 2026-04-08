#include "protocol.h"

#include <math.h>

namespace pifl {

namespace {

constexpr float kTemperatureScale = 100.0f;
constexpr float kParameterScale = 1024.0f;
constexpr float kWeightScale = 256.0f;

int16_t encodeFixed(const float value, const float scale) {
  const float scaled = value * scale;
  if (scaled > 32767.0f) {
    return 32767;
  }
  if (scaled < -32768.0f) {
    return -32768;
  }
  return static_cast<int16_t>(lrintf(scaled));
}

uint16_t encodeUnsignedFixed(const float value, const float scale) {
  const float scaled = value * scale;
  if (scaled <= 0.0f) {
    return 0;
  }
  if (scaled > 65535.0f) {
    return 65535;
  }
  return static_cast<uint16_t>(lrintf(scaled));
}

float decodeFixed(const int16_t value, const float scale) {
  return static_cast<float>(value) / scale;
}

float decodeUnsignedFixed(const uint16_t value, const float scale) {
  return static_cast<float>(value) / scale;
}

void writeInt16LE(uint8_t* buffer, const int16_t value) {
  const uint16_t encoded = static_cast<uint16_t>(value);
  buffer[0] = static_cast<uint8_t>(encoded & 0xFF);
  buffer[1] = static_cast<uint8_t>((encoded >> 8) & 0xFF);
}

void writeUInt16LE(uint8_t* buffer, const uint16_t value) {
  buffer[0] = static_cast<uint8_t>(value & 0xFF);
  buffer[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

int16_t readInt16LE(const uint8_t* buffer) {
  return static_cast<int16_t>(static_cast<uint16_t>(buffer[0]) |
                              (static_cast<uint16_t>(buffer[1]) << 8));
}

uint16_t readUInt16LE(const uint8_t* buffer) {
  return static_cast<uint16_t>(buffer[0]) | (static_cast<uint16_t>(buffer[1]) << 8);
}

}

bool getPacketType(const uint8_t* buffer, const size_t length, PacketType& type, uint8_t& nodeId) {
  if (length < kPacketHeaderSize) {
    return false;
  }
  nodeId = buffer[0];
  type = static_cast<PacketType>(buffer[1]);
  return true;
}

size_t encodeTemperaturePacket(const uint8_t nodeId, const float temperatureC, uint8_t* buffer,
                               const size_t capacity) {
  if (capacity < kTemperaturePacketSize) {
    return 0;
  }

  buffer[0] = nodeId;
  buffer[1] = static_cast<uint8_t>(PacketType::Temperature);
  writeInt16LE(&buffer[2], encodeFixed(temperatureC, kTemperatureScale));
  return kTemperaturePacketSize;
}

size_t encodeModelPacket(const uint8_t nodeId, const ModelParameters& model, const float weight,
                         uint8_t* buffer, const size_t capacity) {
  if (capacity < kModelPacketSize) {
    return 0;
  }

  buffer[0] = nodeId;
  buffer[1] = static_cast<uint8_t>(PacketType::ModelUpdate);
  writeInt16LE(&buffer[2], encodeFixed(model.w1, kParameterScale));
  writeInt16LE(&buffer[4], encodeFixed(model.w2, kParameterScale));
  writeInt16LE(&buffer[6], encodeFixed(model.b, kParameterScale));
  writeUInt16LE(&buffer[8], encodeUnsignedFixed(weight, kWeightScale));
  return kModelPacketSize;
}

size_t encodeAggregationPacket(const uint8_t nodeId, const ModelParameters& model, uint8_t* buffer,
                               const size_t capacity) {
  if (capacity < kAggregationPacketSize) {
    return 0;
  }

  buffer[0] = nodeId;
  buffer[1] = static_cast<uint8_t>(PacketType::GlobalAggregation);
  writeInt16LE(&buffer[2], encodeFixed(model.w1, kParameterScale));
  writeInt16LE(&buffer[4], encodeFixed(model.w2, kParameterScale));
  writeInt16LE(&buffer[6], encodeFixed(model.b, kParameterScale));
  return kAggregationPacketSize;
}

size_t encodeDiscoveryPacket(const uint8_t nodeId, const uint8_t roleFlags, uint8_t* buffer,
                             const size_t capacity) {
  if (capacity < kDiscoveryPacketSize) {
    return 0;
  }

  buffer[0] = nodeId;
  buffer[1] = static_cast<uint8_t>(PacketType::Discovery);
  buffer[2] = roleFlags;
  buffer[3] = kProtocolVersion;
  return kDiscoveryPacketSize;
}

bool decodeTemperaturePacket(const uint8_t* buffer, const size_t length, TemperaturePacket& packet) {
  if (length != kTemperaturePacketSize || buffer[1] != static_cast<uint8_t>(PacketType::Temperature)) {
    return false;
  }

  packet.nodeId = buffer[0];
  packet.temperatureC = decodeFixed(readInt16LE(&buffer[2]), kTemperatureScale);
  return true;
}

bool decodeModelPacket(const uint8_t* buffer, const size_t length, ModelPacket& packet) {
  if (length != kModelPacketSize || buffer[1] != static_cast<uint8_t>(PacketType::ModelUpdate)) {
    return false;
  }

  packet.nodeId = buffer[0];
  packet.model.w1 = decodeFixed(readInt16LE(&buffer[2]), kParameterScale);
  packet.model.w2 = decodeFixed(readInt16LE(&buffer[4]), kParameterScale);
  packet.model.b = decodeFixed(readInt16LE(&buffer[6]), kParameterScale);
  packet.weight = decodeUnsignedFixed(readUInt16LE(&buffer[8]), kWeightScale);
  return true;
}

bool decodeAggregationPacket(const uint8_t* buffer, const size_t length, AggregationPacket& packet) {
  if (length != kAggregationPacketSize ||
      buffer[1] != static_cast<uint8_t>(PacketType::GlobalAggregation)) {
    return false;
  }

  packet.nodeId = buffer[0];
  packet.model.w1 = decodeFixed(readInt16LE(&buffer[2]), kParameterScale);
  packet.model.w2 = decodeFixed(readInt16LE(&buffer[4]), kParameterScale);
  packet.model.b = decodeFixed(readInt16LE(&buffer[6]), kParameterScale);
  return true;
}

bool decodeDiscoveryPacket(const uint8_t* buffer, const size_t length, DiscoveryPacket& packet) {
  if (length != kDiscoveryPacketSize || buffer[1] != static_cast<uint8_t>(PacketType::Discovery)) {
    return false;
  }

  packet.nodeId = buffer[0];
  packet.roleFlags = buffer[2];
  packet.protocolVersion = buffer[3];
  return packet.protocolVersion == kProtocolVersion;
}

}
