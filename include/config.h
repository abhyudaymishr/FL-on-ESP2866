#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include "pifl_types.h"

namespace pifl {

enum class SensorMode : uint8_t {
  Synthetic = 0,
  AnalogLinear = 1,
};

constexpr uint8_t kNodeId = 0;
constexpr uint8_t kAggregatorNodeId = 0;
constexpr bool kAggregationEnabled = true;

constexpr uint8_t kNeighborIds[NeighborCount] = {
    0,
    0,
    0,
    0,
};

constexpr size_t kMaxFederatedPeers = 8;
constexpr size_t kMaxRealtimePeers = 12;

constexpr const char* kWifiSsid = "YOUR_WIFI_SSID";
constexpr const char* kWifiPassword = "YOUR_WIFI_PASSWORD";
constexpr uint16_t kProtocolPort = 4210;
const IPAddress kBroadcastIp(255, 255, 255, 255);

constexpr SensorMode kSensorMode = SensorMode::Synthetic;
constexpr uint8_t kAnalogPin = A0;
constexpr float kAnalogSlope = 0.0977f;
constexpr float kAnalogOffset = 0.0f;

constexpr float kInitialTemperatureC = 24.0f;
constexpr float kSyntheticAmplitudeC = 1.5f;
constexpr float kSyntheticWavePeriodSeconds = 60.0f;
constexpr float kSyntheticNodePhaseRadians = 0.45f;

constexpr float kDtSeconds = 1.0f;
constexpr float kGridSpacing = 1.0f;
constexpr float kAlpha = 0.15f;
constexpr float kLearningRate = 0.00005f;
constexpr float kGlobalBlend = 1.0f;

constexpr uint32_t kSamplePeriodMs = 1000;
constexpr uint32_t kDiscoveryPeriodMs = 2000;
constexpr uint32_t kModelSyncPeriodMs = 5000;
constexpr uint32_t kAggregationPeriodMs = 7000;
constexpr uint32_t kNeighborTimeoutMs = 6000;
constexpr uint32_t kRealtimePeerTimeoutMs = 6000;
constexpr uint32_t kPeerModelTimeoutMs = 15000;
constexpr uint32_t kWifiRetryPeriodMs = 5000;

constexpr float kMinimumAggregationWeight = 0.05f;
constexpr bool kAutoAssignNeighbors = true;

constexpr bool kClampModelParameters = true;
constexpr float kMaxAbsW1 = 2.0f;
constexpr float kMaxAbsW2 = 0.5f;
constexpr float kMaxAbsB = 10.0f;

inline float clampAbs(const float value, const float bound) {
  if (value > bound) {
    return bound;
  }
  if (value < -bound) {
    return -bound;
  }
  return value;
}

inline bool isAggregationNode(const uint8_t nodeId) {
  return kAggregationEnabled && kAggregatorNodeId != 0 && nodeId == kAggregatorNodeId;
}

}
