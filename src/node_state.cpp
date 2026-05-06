#include "node_state.h"

#include <math.h>

#include "discretization.h"
#include "node_model.h"

namespace pifl {

namespace {

float effectiveNeighborTemperature(const NodeState& state, const NeighborObservation& neighbor) {
  return neighbor.valid ? neighbor.temperatureC : state.currentTempC;
}

size_t findPeerSlot(const NodeState& state, const uint8_t nodeId) {
  size_t emptySlot = kMaxFederatedPeers;

  for (size_t index = 0; index < kMaxFederatedPeers; ++index) {
    if (state.peerModels[index].valid && state.peerModels[index].nodeId == nodeId) {
      return index;
    }
    if (!state.peerModels[index].valid && emptySlot == kMaxFederatedPeers) {
      emptySlot = index;
    }
  }

  return emptySlot;
}

size_t findLivePeerSlot(const NodeState& state, const uint8_t nodeId) {
  size_t emptySlot = kMaxRealtimePeers;

  for (size_t index = 0; index < kMaxRealtimePeers; ++index) {
    if (state.livePeers[index].valid && state.livePeers[index].nodeId == nodeId) {
      return index;
    }
    if (!state.livePeers[index].valid && emptySlot == kMaxRealtimePeers) {
      emptySlot = index;
    }
  }

  return emptySlot;
}

void clampModel(ModelParameters& model) {
  if (!kClampModelParameters) {
    return;
  }

  clampPhase2Model(model, kMaxAbsW1, kMaxAbsW2, kMaxAbsB);
}

const LivePeerState* getLivePeerByNodeId(const NodeState& state, const uint8_t nodeId) {
  for (size_t index = 0; index < kMaxRealtimePeers; ++index) {
    if (state.livePeers[index].valid && state.livePeers[index].nodeId == nodeId) {
      return &state.livePeers[index];
    }
  }

  return nullptr;
}

float dynamicPeerTemperature(const NodeState& state, const size_t rank) {
  uint8_t usedIds[NeighborCount] = {0, 0, 0, 0};

  for (size_t selection = 0; selection <= rank; ++selection) {
    const LivePeerState* bestPeer = nullptr;

    for (size_t index = 0; index < kMaxRealtimePeers; ++index) {
      const LivePeerState& candidate = state.livePeers[index];
      if (!candidate.valid || !candidate.hasTemperature || candidate.nodeId == state.localNodeId) {
        continue;
      }

      bool alreadyUsed = false;
      for (size_t usedIndex = 0; usedIndex < selection; ++usedIndex) {
        if (usedIds[usedIndex] == candidate.nodeId) {
          alreadyUsed = true;
          break;
        }
      }
      if (alreadyUsed) {
        continue;
      }

      if (bestPeer == nullptr || candidate.nodeId < bestPeer->nodeId) {
        bestPeer = &candidate;
      }
    }

    if (bestPeer == nullptr) {
      return state.currentTempC;
    }

    usedIds[selection] = bestPeer->nodeId;
    if (selection == rank) {
      return bestPeer->temperatureC;
    }
  }

  return state.currentTempC;
}

float resolvedNeighborTemperature(const NodeState& state, const NeighborIndex index) {
  const uint8_t configuredNodeId = state.neighbors[index].nodeId;

  if (configuredNodeId != 0) {
    const LivePeerState* livePeer = getLivePeerByNodeId(state, configuredNodeId);
    if (livePeer != nullptr && livePeer->hasTemperature) {
      return livePeer->temperatureC;
    }
    return effectiveNeighborTemperature(state, state.neighbors[index]);
  }

  if (kAutoAssignNeighbors) {
    return dynamicPeerTemperature(state, static_cast<size_t>(index));
  }

  if (kBoundaryCondition == BoundaryCondition::AmbientDirichlet) {
    return kAmbientTemperatureC;
  }

  return state.currentTempC;
}

void touchLivePeer(NodeState& state, const uint8_t nodeId, const uint32_t nowMs) {
  if (nodeId == 0 || nodeId == state.localNodeId) {
    return;
  }

  const size_t slot = findLivePeerSlot(state, nodeId);
  if (slot >= kMaxRealtimePeers) {
    return;
  }

  state.livePeers[slot].nodeId = nodeId;
  state.livePeers[slot].lastSeenMs = nowMs;
  state.livePeers[slot].valid = true;
}

FiniteDifferenceStencil buildStencil(const NodeState& state) {
  return {
      state.currentTempC,
      resolvedNeighborTemperature(state, NeighborNorth),
      resolvedNeighborTemperature(state, NeighborSouth),
      resolvedNeighborTemperature(state, NeighborEast),
      resolvedNeighborTemperature(state, NeighborWest),
  };
}

}

void initializeNodeState(NodeState& state, const uint8_t localNodeId) {
  state.localNodeId = localNodeId;
  state.currentTempC = kInitialTemperatureC;
  state.pendingPredictionC = kInitialTemperatureC;
  state.lastPredictionInputC = kInitialTemperatureC;
  state.lastLaplacian = 0.0f;
  state.lastDiffusionTerm = 0.0f;
  state.lastCorrectionTerm = 0.0f;
  state.lastPredictionError = 0.0f;
  state.lastLocalLoss = 0.0f;
  state.lastGlobalLoss = 0.0f;
  state.alpha = kAlpha;
  state.model = {0.0f, 0.0f, 0.0f};
  state.m = {0.0f, 0.0f, 0.0f};
  state.v = {0.0f, 0.0f, 0.0f};
  state.sgdStepCount = 0;
  state.hasPendingPrediction = false;
  state.localModelRound = 0;
  state.lastGlobalRound = 0;
  state.lastDiscoveryMs = 0;
  state.lastSampleMs = 0;
  state.lastModelSyncMs = 0;
  state.lastAggregationMs = 0;
  state.lastWifiRetryMs = 0;

  for (size_t index = 0; index < NeighborCount; ++index) {
    state.neighbors[index].nodeId = kNeighborIds[index];
    state.neighbors[index].temperatureC = kInitialTemperatureC;
    state.neighbors[index].lastSeenMs = 0;
    state.neighbors[index].valid = false;
  }

  for (size_t index = 0; index < kMaxRealtimePeers; ++index) {
    state.livePeers[index].nodeId = 0;
    state.livePeers[index].roleFlags = 0;
    state.livePeers[index].temperatureC = kInitialTemperatureC;
    state.livePeers[index].lastSeenMs = 0;
    state.livePeers[index].hasTemperature = false;
    state.livePeers[index].valid = false;
  }

  for (size_t index = 0; index < kMaxFederatedPeers; ++index) {
    state.peerModels[index].nodeId = 0;
    state.peerModels[index].model = {0.0f, 0.0f, 0.0f};
    state.peerModels[index].weight = 0.0f;
    state.peerModels[index].loss = 0.0f;
    state.peerModels[index].round = 0;
    state.peerModels[index].lastSeenMs = 0;
    state.peerModels[index].valid = false;
  }
}

void expireStaleData(NodeState& state, const uint32_t nowMs) {
  for (size_t index = 0; index < NeighborCount; ++index) {
    if (state.neighbors[index].valid &&
        nowMs - state.neighbors[index].lastSeenMs > kNeighborTimeoutMs) {
      state.neighbors[index].valid = false;
    }
  }

  for (size_t index = 0; index < kMaxRealtimePeers; ++index) {
    if (state.livePeers[index].valid && nowMs - state.livePeers[index].lastSeenMs > kRealtimePeerTimeoutMs) {
      state.livePeers[index].valid = false;
      state.livePeers[index].hasTemperature = false;
    }
  }

  for (size_t index = 0; index < kMaxFederatedPeers; ++index) {
    if (state.peerModels[index].valid &&
        nowMs - state.peerModels[index].lastSeenMs > kPeerModelTimeoutMs) {
      state.peerModels[index].valid = false;
    }
  }
}

void registerLivePeer(NodeState& state, const DiscoveryPacket& packet, const uint32_t nowMs) {
  touchLivePeer(state, packet.nodeId, nowMs);

  const size_t slot = findLivePeerSlot(state, packet.nodeId);
  if (slot >= kMaxRealtimePeers) {
    return;
  }

  state.livePeers[slot].roleFlags = packet.roleFlags;
}

void registerNeighborTemperature(NodeState& state, const uint8_t sourceNodeId, const float temperatureC,
                                 const uint32_t nowMs) {
  touchLivePeer(state, sourceNodeId, nowMs);

  const size_t liveSlot = findLivePeerSlot(state, sourceNodeId);
  if (liveSlot < kMaxRealtimePeers) {
    state.livePeers[liveSlot].temperatureC = temperatureC;
    state.livePeers[liveSlot].hasTemperature = true;
  }

  for (size_t index = 0; index < NeighborCount; ++index) {
    if (state.neighbors[index].nodeId == 0) {
      continue;
    }
    if (state.neighbors[index].nodeId != sourceNodeId) {
      continue;
    }

    state.neighbors[index].temperatureC = temperatureC;
    state.neighbors[index].lastSeenMs = nowMs;
    state.neighbors[index].valid = true;
    return;
  }
}

void registerPeerModel(NodeState& state, const ModelPacket& packet, const uint32_t nowMs) {
  touchLivePeer(state, packet.nodeId, nowMs);
  const size_t slot = findPeerSlot(state, packet.nodeId);
  if (slot >= kMaxFederatedPeers) {
    return;
  }

  state.peerModels[slot].nodeId = packet.nodeId;
  state.peerModels[slot].model = packet.model;
  state.peerModels[slot].weight = packet.weight;
  state.peerModels[slot].loss = packet.loss;
  state.peerModels[slot].round = packet.round;
  state.peerModels[slot].lastSeenMs = nowMs;
  state.peerModels[slot].valid = true;
}

void applyGlobalModel(NodeState& state, const AggregationPacket& packet) {
  state.model.w1 = (1.0f - kGlobalBlend) * state.model.w1 + kGlobalBlend * packet.model.w1;
  state.model.w2 = (1.0f - kGlobalBlend) * state.model.w2 + kGlobalBlend * packet.model.w2;
  state.model.b = (1.0f - kGlobalBlend) * state.model.b + kGlobalBlend * packet.model.b;
  state.lastGlobalRound = packet.round;
  state.lastGlobalLoss = packet.loss;
  clampModel(state.model);
}

float evaluateCorrection(const ModelParameters& model, const float temperatureC) {
  return evaluateQuadraticCorrection(model, temperatureC);
}

float computeLaplacian(const NodeState& state) {
  return computeFiniteDifferenceLaplacian(buildStencil(state), kGridSpacing);
}

float computeAggregationWeight(const float laplacian) {
  const float magnitude = fabsf(laplacian);
  return magnitude > kMinimumAggregationWeight ? magnitude : kMinimumAggregationWeight;
}

float predictNextTemperature(const NodeState& state, float dtSeconds) {
  return predictPhase2NodeStep(buildStencil(state), state.alpha, state.model, kGridSpacing,
                               dtSeconds)
      .predictedTempC;
}

void applyMeasurement(NodeState& state, const float measuredTemperatureC, const float dtSeconds) {
  if (state.hasPendingPrediction) {
    const Phase2TrainingUpdate grads =
        computePhase2Gradients(measuredTemperatureC, state.pendingPredictionC,
                               state.lastPredictionInputC);
    state.lastPredictionError = grads.error;
    state.lastLocalLoss = grads.error * grads.error;
    
    state.sgdStepCount++;
    applyPhase2AdamUpdate(state.model, state.m, state.v, state.sgdStepCount, grads, kLearningRate);
    clampPhase2Model(state.model, 32.0f, 32.0f, 32.0f);
  } else {
    state.lastPredictionError = 0.0f;
    state.lastLocalLoss = 0.0f;
  }

  state.currentTempC = measuredTemperatureC;
  state.lastPredictionInputC = state.currentTempC;

  const Phase2Prediction prediction =
      predictPhase2NodeStep(buildStencil(state), state.alpha, state.model, kGridSpacing,
                            dtSeconds);
  state.lastLaplacian = prediction.laplacian;
  state.lastDiffusionTerm = prediction.diffusionTerm;
  state.lastCorrectionTerm = prediction.correctionTerm;
  state.pendingPredictionC = prediction.predictedTempC;
  state.hasPendingPrediction = true;
}

bool buildAggregatedModel(const NodeState& state, AggregationPacket& packet) {
  const float localWeight = computeAggregationWeight(state.lastLaplacian);
  float weightedW1 = state.model.w1 * localWeight;
  float weightedW2 = state.model.w2 * localWeight;
  float weightedB = state.model.b * localWeight;
  float weightedLoss = state.lastLocalLoss * localWeight;
  float totalWeight = localWeight;

  for (size_t index = 0; index < kMaxFederatedPeers; ++index) {
    if (!state.peerModels[index].valid) {
      continue;
    }

    const float peerWeight =
        state.peerModels[index].weight > kMinimumAggregationWeight ? state.peerModels[index].weight
                                                                   : kMinimumAggregationWeight;
    weightedW1 += state.peerModels[index].model.w1 * peerWeight;
    weightedW2 += state.peerModels[index].model.w2 * peerWeight;
    weightedB += state.peerModels[index].model.b * peerWeight;
    weightedLoss += state.peerModels[index].loss * peerWeight;
    totalWeight += peerWeight;
  }

  if (totalWeight <= 0.0f) {
    return false;
  }

  packet.nodeId = state.localNodeId;
  packet.model.w1 = weightedW1 / totalWeight;
  packet.model.w2 = weightedW2 / totalWeight;
  packet.model.b = weightedB / totalWeight;
  packet.round = state.lastGlobalRound + 1;
  packet.loss = weightedLoss / totalWeight;
  return true;
}

size_t countOnlinePeers(const NodeState& state) {
  size_t count = 0;
  for (size_t index = 0; index < kMaxRealtimePeers; ++index) {
    if (state.livePeers[index].valid) {
      ++count;
    }
  }
  return count;
}

bool shouldBroadcastAggregation(const NodeState& state) {
  if (!kAggregationEnabled) {
    return false;
  }

  if (kAggregatorNodeId != 0) {
    return state.localNodeId == kAggregatorNodeId;
  }

  uint8_t lowestNodeId = state.localNodeId;
  for (size_t index = 0; index < kMaxRealtimePeers; ++index) {
    if (!state.livePeers[index].valid) {
      continue;
    }
    if (state.livePeers[index].nodeId < lowestNodeId) {
      lowestNodeId = state.livePeers[index].nodeId;
    }
  }

  return state.localNodeId == lowestNodeId;
}

}
