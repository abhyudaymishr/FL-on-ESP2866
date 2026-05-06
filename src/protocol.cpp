#include "protocol.h"
#include <string.h> // for memcpy

namespace pifl {

namespace {

void writeFloat32LE(uint8_t* buffer, const float value) {
  // ESP8266 is little-endian, so we can just memcpy the float directly.
  memcpy(buffer, &value, sizeof(float));
}

float readFloat32LE(const uint8_t* buffer) {
  float value;
  memcpy(&value, buffer, sizeof(float));
  return value;
}

void writeUInt16LE(uint8_t* buffer, const uint16_t value) {
  buffer[0] = static_cast<uint8_t>(value & 0xFF);
  buffer[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
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
  writeFloat32LE(&buffer[2], temperatureC);
  return kTemperaturePacketSize;
}

size_t encodeModelPacket(const uint8_t nodeId, const ModelParameters& model, const float weight,
                         const float loss, const uint16_t round, uint8_t* buffer,
                         const size_t capacity) {
  if (capacity < kModelPacketSize) {
    return 0;
  }

  buffer[0] = nodeId;
  buffer[1] = static_cast<uint8_t>(PacketType::ModelUpdate);
  writeFloat32LE(&buffer[2], model.w1);
  writeFloat32LE(&buffer[6], model.w2);
  writeFloat32LE(&buffer[10], model.b);
  writeFloat32LE(&buffer[14], weight);
  writeUInt16LE(&buffer[18], round);
  writeFloat32LE(&buffer[20], loss);
  return kModelPacketSize;
}

size_t encodeAggregationPacket(const uint8_t nodeId, const ModelParameters& model, const float loss,
                               const uint16_t round, uint8_t* buffer, const size_t capacity) {
  if (capacity < kAggregationPacketSize) {
    return 0;
  }

  buffer[0] = nodeId;
  buffer[1] = static_cast<uint8_t>(PacketType::GlobalAggregation);
  writeFloat32LE(&buffer[2], model.w1);
  writeFloat32LE(&buffer[6], model.w2);
  writeFloat32LE(&buffer[10], model.b);
  writeUInt16LE(&buffer[14], round);
  writeFloat32LE(&buffer[16], loss);
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
  packet.temperatureC = readFloat32LE(&buffer[2]);
  return true;
}

bool decodeModelPacket(const uint8_t* buffer, const size_t length, ModelPacket& packet) {
  // Allow backwards compat or variable length check if needed, but we rewrote to native
  if (length != kModelPacketSize || buffer[1] != static_cast<uint8_t>(PacketType::ModelUpdate)) {
    return false;
  }

  packet.nodeId = buffer[0];
  packet.model.w1 = readFloat32LE(&buffer[2]);
  packet.model.w2 = readFloat32LE(&buffer[6]);
  packet.model.b = readFloat32LE(&buffer[10]);
  packet.weight = readFloat32LE(&buffer[14]);
  packet.round = readUInt16LE(&buffer[18]);
  packet.loss = readFloat32LE(&buffer[20]);
  return true;
}

bool decodeAggregationPacket(const uint8_t* buffer, const size_t length, AggregationPacket& packet) {
  if (length != kAggregationPacketSize || buffer[1] != static_cast<uint8_t>(PacketType::GlobalAggregation)) {
    return false;
  }

  packet.nodeId = buffer[0];
  packet.model.w1 = readFloat32LE(&buffer[2]);
  packet.model.w2 = readFloat32LE(&buffer[6]);
  packet.model.b = readFloat32LE(&buffer[10]);
  packet.round = readUInt16LE(&buffer[14]);
  packet.loss = readFloat32LE(&buffer[16]);
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
