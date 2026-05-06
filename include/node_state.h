#pragma once

#include <Arduino.h>
#include "config.h"
#include "protocol.h"

namespace pifl {

struct NodeState {
  uint8_t localNodeId;
  float currentTempC;
  float pendingPredictionC;
  float lastPredictionInputC;
  float lastLaplacian;
  float lastDiffusionTerm;
  float lastCorrectionTerm;
  float lastPredictionError;
  float lastLocalLoss;
  float lastGlobalLoss;
  float alpha;
  ModelParameters model;
  ModelParameters m;
  ModelParameters v;
  uint32_t sgdStepCount;
  NeighborObservation neighbors[NeighborCount];
  LivePeerState livePeers[kMaxRealtimePeers];
  PeerModelState peerModels[kMaxFederatedPeers];
  bool hasPendingPrediction;
  uint16_t localModelRound;
  uint16_t lastGlobalRound;
  uint32_t lastDiscoveryMs;
  uint32_t lastSampleMs;
  uint32_t lastModelSyncMs;
  uint32_t lastAggregationMs;
  uint32_t lastWifiRetryMs;
};

void initializeNodeState(NodeState& state, uint8_t localNodeId);

void expireStaleData(NodeState& state, uint32_t nowMs);
void registerLivePeer(NodeState& state, const DiscoveryPacket& packet, uint32_t nowMs);
void registerNeighborTemperature(NodeState& state, uint8_t sourceNodeId, float temperatureC, uint32_t nowMs);
void registerPeerModel(NodeState& state, const ModelPacket& packet, uint32_t nowMs);
void applyGlobalModel(NodeState& state, const AggregationPacket& packet);

float evaluateCorrection(const ModelParameters& model, float temperatureC);
float computeLaplacian(const NodeState& state);
float computeAggregationWeight(float laplacian);
float predictNextTemperature(const NodeState& state, float dtSeconds);
size_t countOnlinePeers(const NodeState& state);
bool shouldBroadcastAggregation(const NodeState& state);

void applyMeasurement(NodeState& state, float measuredTemperatureC, float dtSeconds);
bool buildAggregatedModel(const NodeState& state, AggregationPacket& packet);

}
