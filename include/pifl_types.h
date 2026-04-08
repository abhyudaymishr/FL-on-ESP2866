#pragma once

#include <cstdint>

namespace pifl {

enum NeighborIndex : uint8_t {
  NeighborNorth = 0,
  NeighborSouth = 1,
  NeighborEast = 2,
  NeighborWest = 3,
  NeighborCount = 4,
};

struct ModelParameters {
  float w1;
  float w2;
  float b;
};

struct NeighborObservation {
  uint8_t nodeId;
  float temperatureC;
  uint32_t lastSeenMs;
  bool valid;
};

struct PeerModelState {
  uint8_t nodeId;
  ModelParameters model;
  float weight;
  uint32_t lastSeenMs;
  bool valid;
};

struct LivePeerState {
  uint8_t nodeId;
  uint8_t roleFlags;
  float temperatureC;
  uint32_t lastSeenMs;
  bool hasTemperature;
  bool valid;
};

}
