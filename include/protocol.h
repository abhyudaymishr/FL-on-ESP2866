#pragma once

#include <cstddef>
#include <cstdint>
#include "pifl_types.h"

namespace pifl {
enum class PacketType : uint8_t { Temperature = 1, ModelUpdate = 2, GlobalAggregation = 3, Discovery = 4 };
constexpr uint8_t kRoleFlagAggregatorEligible = 0x01;
constexpr uint8_t kProtocolVersion = 2;
constexpr size_t kPacketHeaderSize = 3;
constexpr size_t kTemperaturePacketSize = 7;
constexpr size_t kModelPacketSize = 36;
constexpr size_t kAggregationPacketSize = 25;
constexpr size_t kDiscoveryPacketSize = 8;
constexpr size_t kMaxPacketSize = kModelPacketSize;

struct TemperaturePacket { uint8_t nodeId; float temperatureC; };
struct ModelPacket {
  uint8_t nodeId;
  ModelParameters model;
  uint32_t sampleCount;
  float meanLocalLoss;
  uint16_t round;
  uint16_t baseRound;
  uint8_t baseLeaderId;
  uint32_t baseLeaderBootId;
  uint32_t senderBootId;
};
struct AggregationPacket {
  uint8_t nodeId;
  ModelParameters model;
  float meanLocalLoss; // Contributor training loss, not validation of this model.
  uint16_t round;
  uint32_t leaderBootId;
};
struct DiscoveryPacket { uint8_t nodeId; uint8_t roleFlags; uint32_t bootId; };

bool isValidTemperature(float value);
bool isValidModel(const ModelParameters& model);
bool isNewerRound(uint16_t candidate, uint16_t previous);
bool getPacketType(const uint8_t* buffer, size_t length, PacketType& type, uint8_t& nodeId);
size_t encodeTemperaturePacket(uint8_t nodeId, float temperatureC, uint8_t* buffer, size_t capacity);
size_t encodeModelPacket(const ModelPacket& packet, uint8_t* buffer, size_t capacity);
size_t encodeAggregationPacket(const AggregationPacket& packet, uint8_t* buffer, size_t capacity);
size_t encodeDiscoveryPacket(const DiscoveryPacket& packet, uint8_t* buffer, size_t capacity);
bool decodeTemperaturePacket(const uint8_t* buffer, size_t length, TemperaturePacket& packet);
bool decodeModelPacket(const uint8_t* buffer, size_t length, ModelPacket& packet);
bool decodeAggregationPacket(const uint8_t* buffer, size_t length, AggregationPacket& packet);
bool decodeDiscoveryPacket(const uint8_t* buffer, size_t length, DiscoveryPacket& packet);
}
