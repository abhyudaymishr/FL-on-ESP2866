#include "node_state.h"
#include "node_model.h"
#include <math.h>

namespace pifl {
namespace {
LivePeerState* touchPeer(NodeState& s, uint8_t id, uint32_t now) {
  if (!id || id == s.localNodeId) return nullptr;
  LivePeerState* empty = nullptr;
  for (auto& peer : s.livePeers) {
    if (peer.valid && peer.nodeId == id) { peer.lastSeenMs = now; return &peer; }
    if (!peer.valid && !empty) empty = &peer;
  }
  if (!empty) return nullptr;
  *empty = {};
  empty->nodeId=id; empty->valid=true; empty->lastSeenMs=now;
  return empty;
}
FiniteDifferenceStencil buildStencil(const NodeState& s) {
  FiniteDifferenceStencil stencil{s.currentTempC,0,0,0,0,0};
  float values[NeighborCount];
  for (unsigned i=0; i<NeighborCount; ++i) {
    const auto& n=s.neighbors[i];
    if (n.nodeId && n.valid) values[i]=n.temperatureC;
    else if (kBoundaryCondition == BoundaryCondition::AmbientDirichlet) values[i]=kAmbientTemperatureC;
    else { values[i]=s.currentTempC; stencil.zeroFluxMask |= 1u << i; }
  }
  stencil.northTempC=values[0]; stencil.southTempC=values[1];
  stencil.eastTempC=values[2]; stencil.westTempC=values[3];
  return stencil;
}
bool matchesBase(const NodeState& s, uint8_t leader, uint32_t boot, uint16_t round) {
  return leader == s.globalLeaderId && boot == s.globalLeaderBootId && round == s.lastGlobalRound;
}
uint32_t leaderBootId(const NodeState& s, uint8_t id) {
  if (id == s.localNodeId) return s.bootId;
  for (const auto& peer : s.livePeers) if (peer.valid && peer.nodeId == id) return peer.bootId;
  return 0;
}
void clampModel(ModelParameters& m) {
  if (kClampModelParameters) clampPhase2Model(m,kMaxAbsW1,kMaxAbsW2,kMaxAbsB);
}
}
void initializeNodeState(NodeState& s, uint8_t id, uint32_t bootId) {
  s = {};
  s.localNodeId=id; s.bootId=bootId ? bootId : 1;
  s.currentTempC=kInitialTemperatureC; s.pendingPredictionC=kInitialTemperatureC; s.alpha=kAlpha;
  for (unsigned i=0; i<NeighborCount; ++i) s.neighbors[i].nodeId=kNeighborIds[i];
}
void expireStaleData(NodeState& s, uint32_t now) {
  for (auto& n : s.neighbors) if (n.valid && now-n.lastSeenMs > kNeighborTimeoutMs) n.valid=false;
  for (auto& p : s.livePeers) {
    if (p.hasTemperature && now-p.temperatureLastSeenMs > kNeighborTimeoutMs) p.hasTemperature=false;
    if (p.valid && now-p.lastSeenMs > kRealtimePeerTimeoutMs) p.valid=false;
  }
  for (auto& p : s.peerModels) if (p.valid && now-p.lastSeenMs > kPeerModelTimeoutMs) p.valid=false;
}
void registerLivePeer(NodeState& s, const DiscoveryPacket& p, uint32_t now) {
  if (!p.nodeId || !p.bootId || (p.roleFlags & ~kRoleFlagAggregatorEligible)) return;
  if (p.nodeId == s.localNodeId) {
    if (p.bootId != s.bootId) s.identityConflict=true;
    return;
  }
  auto* peer=touchPeer(s,p.nodeId,now);
  if (!peer) return;
  if (peer->bootId && peer->bootId != p.bootId) {
    peer->hasTemperature=false;
    for (auto& model : s.peerModels) if (model.nodeId == p.nodeId) model.valid=false;
    for (auto& n : s.neighbors) if (n.nodeId == p.nodeId) n.valid=false;
  }
  peer->bootId=p.bootId; peer->roleFlags=p.roleFlags;
}
void registerNeighborTemperature(NodeState& s, uint8_t id, float temperature, uint32_t now) {
  if (!id || id == s.localNodeId || !isValidTemperature(temperature)) return;
  auto* p=touchPeer(s,id,now);
  if (p) { p->temperatureC=temperature; p->temperatureLastSeenMs=now; p->hasTemperature=true; }
  for (auto& n : s.neighbors) if (n.nodeId == id) {
    n.temperatureC=temperature; n.lastSeenMs=now; n.valid=true;
  }
}
bool registerPeerModel(NodeState& s, const ModelPacket& p, uint32_t now) {
  if (!p.nodeId || p.nodeId == s.localNodeId || !isValidModel(p.model) || !p.sampleCount ||
      !isfinite(p.meanLocalLoss) || p.meanLocalLoss < 0 ||
      !matchesBase(s,p.baseLeaderId,p.baseLeaderBootId,p.baseRound)) return false;
  bool knownBoot=false;
  for (const auto& live : s.livePeers) if (live.valid && live.nodeId == p.nodeId &&
      live.bootId == p.senderBootId && p.senderBootId) knownBoot=true;
  if (!knownBoot) return false;
  PeerModelState* slot=nullptr;
  for (auto& peer : s.peerModels) {
    if (peer.valid && peer.nodeId == p.nodeId) {
      if (!isNewerRound(p.round,peer.round) || p.sampleCount < peer.sampleCount) return false;
      slot=&peer; break;
    }
    if (!peer.valid && !slot) slot=&peer;
  }
  if (!slot) return false;
  *slot={p.nodeId,p.model,p.sampleCount,p.meanLocalLoss,p.round,p.baseRound,p.baseLeaderId,
         p.baseLeaderBootId,now,true};
  touchPeer(s,p.nodeId,now);
  return true;
}
uint8_t electedAggregatorId(const NodeState& s) {
  if (!kAggregationEnabled || s.identityConflict) return 0;
  if (kAggregatorNodeId) return kAggregatorNodeId;
  uint8_t lowest=s.localNodeId;
  for (const auto& p : s.livePeers) if (p.valid && p.bootId &&
      (p.roleFlags & kRoleFlagAggregatorEligible) && p.nodeId < lowest) lowest=p.nodeId;
  return lowest;
}
bool shouldBroadcastAggregation(const NodeState& s) { return electedAggregatorId(s) == s.localNodeId; }
bool applyGlobalModel(NodeState& s, const AggregationPacket& p) {
  if (!p.nodeId || p.nodeId != electedAggregatorId(s) || !p.leaderBootId ||
      p.leaderBootId != leaderBootId(s,p.nodeId) || !isValidModel(p.model) ||
      !isfinite(p.meanLocalLoss) || p.meanLocalLoss < 0) return false;
  const bool sameSession=s.hasGlobalModel && s.globalLeaderId == p.nodeId &&
                         s.globalLeaderBootId == p.leaderBootId;
  if (sameSession && !isNewerRound(p.round,s.lastGlobalRound)) return false;
  s.model={(1-kGlobalBlend)*s.model.w1+kGlobalBlend*p.model.w1,
           (1-kGlobalBlend)*s.model.w2+kGlobalBlend*p.model.w2,
           (1-kGlobalBlend)*s.model.b+kGlobalBlend*p.model.b};
  clampModel(s.model);
  s.globalLeaderId=p.nodeId; s.globalLeaderBootId=p.leaderBootId;
  s.lastGlobalRound=p.round; s.meanContributorLoss=p.meanLocalLoss; s.hasGlobalModel=true;
  // Explicit optimizer policy: a new global base starts a fresh local Adam run.
  s.m={0,0,0}; s.v={0,0,0}; s.adamStepCount=0;
  s.localSampleCount=0; s.localLossSum=0;
  for (auto& peer : s.peerModels) peer.valid=false;
  return true;
}
float computeLaplacian(const NodeState& s) { return computeFiniteDifferenceLaplacian(buildStencil(s),kGridSpacing); }
bool applyMeasurement(NodeState& s, float measured, float elapsedSeconds) {
  if (s.identityConflict || !isValidTemperature(measured) || !isfinite(elapsedSeconds) || elapsedSeconds <= 0) {
    s.hasPendingPrediction=false;
    return false;
  }
  if (s.hasPendingPrediction) {
    // Re-evaluate the cached input at current parameters and the measured elapsed
    // interval. This avoids applying an old model's gradient after aggregation.
    const auto prediction=predictPhase2NodeStep(s.pendingStencil,s.alpha,s.model,kGridSpacing,elapsedSeconds);
    if (prediction.valid) {
      const auto grads=computePhase2Gradients(measured,prediction);
      if (applyPhase2AdamUpdate(s.model,s.m,s.v,s.adamStepCount+1,grads,kLearningRate)) {
        ++s.adamStepCount;
        clampModel(s.model);
        s.lastPredictionError=grads.error; s.lastLocalLoss=grads.error*grads.error;
        if (s.localSampleCount < 1000000u) { ++s.localSampleCount; s.localLossSum += s.lastLocalLoss; }
      }
    }
  }
  s.currentTempC=measured;
  s.pendingStencil=buildStencil(s);
  const auto next=predictPhase2NodeStep(s.pendingStencil,s.alpha,s.model,kGridSpacing,kSamplePeriodMs/1000.0f);
  s.lastLaplacian=computeLaplacian(s);
  s.lastDiffusionTerm=next.diffusionTerm; s.lastCorrectionTerm=next.correctionTerm;
  s.pendingPredictionC=next.valid ? next.predictedTempC : measured;
  s.hasPendingPrediction=next.valid;
  return true;
}
ModelPacket buildLocalModelPacket(const NodeState& s, uint16_t round) {
  return {s.localNodeId,s.model,s.localSampleCount,
          s.localSampleCount ? s.localLossSum/s.localSampleCount : 0,round,s.lastGlobalRound,
          s.globalLeaderId,s.globalLeaderBootId,s.bootId};
}
bool buildAggregatedModel(const NodeState& s, AggregationPacket& p) {
  if (!shouldBroadcastAggregation(s) || !isValidModel(s.model)) return false;
  // Accumulate in double so counts cannot overflow and large counts lose less precision.
  double total=s.localSampleCount;
  double w1=s.model.w1*total, w2=s.model.w2*total, b=s.model.b*total, loss=s.localLossSum;
  for (const auto& peer : s.peerModels) {
    if (!peer.valid || !matchesBase(s,peer.baseLeaderId,peer.baseLeaderBootId,peer.baseRound)) continue;
    const double weight=peer.sampleCount;
    w1+=peer.model.w1*weight; w2+=peer.model.w2*weight; b+=peer.model.b*weight;
    loss+=peer.meanLocalLoss*weight; total+=weight;
  }
  if (total <= 0) return false;
  const uint16_t round=(s.globalLeaderId == s.localNodeId && s.globalLeaderBootId == s.bootId) ?
      static_cast<uint16_t>(s.lastGlobalRound+1) : 1;
  p={s.localNodeId,{static_cast<float>(w1/total),static_cast<float>(w2/total),static_cast<float>(b/total)},
      static_cast<float>(loss/total),round,s.bootId};
  return isValidModel(p.model) && isfinite(p.meanLocalLoss);
}
size_t countOnlinePeers(const NodeState& s) {
  size_t count=0; for (const auto& p : s.livePeers) if (p.valid) ++count; return count;
}
}
