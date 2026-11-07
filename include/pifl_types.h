#pragma once
#include <cstdint>
namespace pifl {
enum NeighborIndex : uint8_t { NeighborNorth=0, NeighborSouth=1, NeighborEast=2, NeighborWest=3, NeighborCount=4 };
struct ModelParameters { float w1; float w2; float b; };
struct NeighborObservation { uint8_t nodeId; float temperatureC; uint32_t lastSeenMs; bool valid; };
struct PeerModelState {
  uint8_t nodeId;
  ModelParameters model;
  uint32_t sampleCount;
  float meanLocalLoss;
  uint16_t round;
  uint16_t baseRound;
  uint8_t baseLeaderId;
  uint32_t baseLeaderBootId;
  uint32_t lastSeenMs;
  bool valid;
};
struct LivePeerState {
  uint8_t nodeId;
  uint8_t roleFlags;
  uint32_t bootId;
  float temperatureC;
  uint32_t lastSeenMs;
  uint32_t temperatureLastSeenMs;
  bool hasTemperature;
  bool valid;
};
}
