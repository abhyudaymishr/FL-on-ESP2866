#pragma once

#include <Arduino.h>
#include "pifl_types.h"

namespace pifl {

enum class PacketType : uint8_t {
  Temperature = 0x01,
  ModelUpdate = 0x02,
  GlobalAggregation = 0x03,
  Discovery = 0x04,
};

constexpr uint8_t kRoleFlagAggregatorEligible = 0x01;
constexpr uint8_t kRoleFlagAutoNeighbor = 0x02;

struct TemperaturePacket {
  uint8_t nodeId;
  float temperatureC;
};

struct ModelPacket {
  uint8_t nodeId;
  ModelParameters model;
  float weight;
  float loss;
  uint16_t round;
};

struct AggregationPacket {
  uint8_t nodeId;
  ModelParameters model;
  float loss;
  uint16_t round;
};

struct DiscoveryPacket {
  uint8_t nodeId;
  uint8_t roleFlags;
  uint8_t protocolVersion;
};

constexpr size_t kPacketHeaderSize = 2;
constexpr size_t kTemperaturePacketSize = 6;
constexpr size_t kModelPacketSize = 24;
constexpr size_t kAggregationPacketSize = 20;
constexpr size_t kDiscoveryPacketSize = 4;
constexpr size_t kMaxPacketSize = kModelPacketSize;
constexpr uint8_t kProtocolVersion = 1;

bool getPacketType(const uint8_t* buffer, size_t length, PacketType& type, uint8_t& nodeId);

size_t encodeTemperaturePacket(uint8_t nodeId, float temperatureC, uint8_t* buffer, size_t capacity);
size_t encodeModelPacket(uint8_t nodeId, const ModelParameters& model, float weight, float loss,
                         uint16_t round, uint8_t* buffer, size_t capacity);
size_t encodeAggregationPacket(uint8_t nodeId, const ModelParameters& model, float loss,
                               uint16_t round, uint8_t* buffer, size_t capacity);
size_t encodeDiscoveryPacket(uint8_t nodeId, uint8_t roleFlags, uint8_t* buffer, size_t capacity);

bool decodeTemperaturePacket(const uint8_t* buffer, size_t length, TemperaturePacket& packet);
bool decodeModelPacket(const uint8_t* buffer, size_t length, ModelPacket& packet);
bool decodeAggregationPacket(const uint8_t* buffer, size_t length, AggregationPacket& packet);
bool decodeDiscoveryPacket(const uint8_t* buffer, size_t length, DiscoveryPacket& packet);

}
