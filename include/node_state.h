#pragma once
#include <Arduino.h>
#include "config.h"
#include "discretization.h"
#include "protocol.h"
namespace pifl {
struct NodeState {
  uint8_t localNodeId;
  uint32_t bootId;
  bool identityConflict;
  float currentTempC;
  float pendingPredictionC;
  FiniteDifferenceStencil pendingStencil;
  float lastLaplacian;
  float lastDiffusionTerm;
  float lastCorrectionTerm;
  float lastPredictionError;
  float lastLocalLoss;
  float meanContributorLoss;
  float localLossSum;
  float alpha;
  ModelParameters model;
  ModelParameters m;
  ModelParameters v;
  uint32_t adamStepCount;
  uint32_t localSampleCount;
  NeighborObservation neighbors[NeighborCount];
  LivePeerState livePeers[kMaxRealtimePeers];
  PeerModelState peerModels[kMaxFederatedPeers];
  bool hasPendingPrediction;
  bool hasGlobalModel;
  uint8_t globalLeaderId;
  uint32_t globalLeaderBootId;
  uint16_t localModelRound;
  uint16_t lastGlobalRound;
  uint32_t lastDiscoveryMs;
  uint32_t lastSampleMs;
  uint32_t lastMeasurementMs;
  uint32_t lastModelSyncMs;
  uint32_t lastAggregationMs;
  uint32_t lastWifiRetryMs;
};
void initializeNodeState(NodeState& state, uint8_t nodeId, uint32_t bootId);
void expireStaleData(NodeState& state, uint32_t nowMs);
void registerLivePeer(NodeState& state, const DiscoveryPacket& packet, uint32_t nowMs);
void registerNeighborTemperature(NodeState& state, uint8_t sourceNodeId, float temperatureC, uint32_t nowMs);
bool registerPeerModel(NodeState& state, const ModelPacket& packet, uint32_t nowMs);
bool applyGlobalModel(NodeState& state, const AggregationPacket& packet);
float computeLaplacian(const NodeState& state);
size_t countOnlinePeers(const NodeState& state);
uint8_t electedAggregatorId(const NodeState& state);
bool shouldBroadcastAggregation(const NodeState& state);
bool applyMeasurement(NodeState& state, float temperatureC, float elapsedSeconds);
bool buildAggregatedModel(const NodeState& state, AggregationPacket& packet);
ModelPacket buildLocalModelPacket(const NodeState& state, uint16_t round);
}
