#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>

#include "config.h"
#include "node_state.h"
#include "protocol.h"
#include "sensor.h"
#include "transport.h"

namespace {

using namespace pifl;

NodeState gNodeState;
TemperatureSensor* gSensor = nullptr;
Transport* gTransport = nullptr;

uint8_t resolveLocalNodeId() {
  if (kNodeId != 0) {
    return kNodeId;
  }

  const uint32_t chipId = ESP.getChipId();
  const uint8_t derivedNodeId =
      static_cast<uint8_t>((chipId ^ (chipId >> 8) ^ (chipId >> 16)) & 0xFF);
  return derivedNodeId == 0 ? 1 : derivedNodeId;
}

uint8_t localRoleFlags() {
  uint8_t flags = 0;
  if (kAggregationEnabled) {
    flags |= kRoleFlagAggregatorEligible;
  }
  if (kAutoAssignNeighbors) {
    flags |= kRoleFlagAutoNeighbor;
  }
  return flags;
}

void logModel(const char* prefix, const ModelParameters& model) {
  Serial.printf("%s w1=%.5f w2=%.5f b=%.5f\n", prefix, model.w1, model.w2, model.b);
}

void ensureWiFiConnected(const uint32_t nowMs) {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  if (nowMs - gNodeState.lastWifiRetryMs < kWifiRetryPeriodMs) {
    return;
  }

  gNodeState.lastWifiRetryMs = nowMs;
  Serial.println("WiFi dropped, attempting reconnect...");
  WiFi.reconnect();
}

bool sendPacket(const uint8_t* buffer, const size_t length) {
  if (gTransport) {
    return gTransport->sendPacket(buffer, length);
  }
  return false;
}

void broadcastTemperature() {
  uint8_t buffer[kTemperaturePacketSize];
  const size_t length =
      encodeTemperaturePacket(gNodeState.localNodeId, gNodeState.currentTempC, buffer, sizeof(buffer));
  sendPacket(buffer, length);
}

void broadcastModel() {
  uint8_t buffer[kModelPacketSize];
  const float weight = computeAggregationWeight(gNodeState.lastLaplacian);
  const uint16_t nextRound = static_cast<uint16_t>(gNodeState.localModelRound + 1);
  const size_t length = encodeModelPacket(gNodeState.localNodeId, gNodeState.model, weight,
                                          gNodeState.lastLocalLoss, nextRound, buffer,
                                          sizeof(buffer));
  if (sendPacket(buffer, length)) {
    gNodeState.localModelRound = nextRound;
  }
}

void broadcastAggregation() {
  AggregationPacket packet;
  if (!buildAggregatedModel(gNodeState, packet)) {
    return;
  }

  uint8_t buffer[kAggregationPacketSize];
  const size_t length = encodeAggregationPacket(gNodeState.localNodeId, packet.model, packet.loss,
                                                packet.round, buffer, sizeof(buffer));
  if (sendPacket(buffer, length)) {
    applyGlobalModel(gNodeState, packet);
    Serial.printf("Broadcast global aggregated model. round=%u loss=%.4f\n", packet.round,
                  packet.loss);
    logModel("Global model", packet.model);
  }
}

void broadcastDiscovery() {
  uint8_t buffer[kDiscoveryPacketSize];
  const size_t length =
      encodeDiscoveryPacket(gNodeState.localNodeId, localRoleFlags(), buffer, sizeof(buffer));
  sendPacket(buffer, length);
}

void handleTemperaturePacket(const uint8_t* buffer, const size_t length, const uint32_t nowMs) {
  TemperaturePacket packet;
  if (!decodeTemperaturePacket(buffer, length, packet)) {
    return;
  }
  if (packet.nodeId == gNodeState.localNodeId) {
    return;
  }

  registerNeighborTemperature(gNodeState, packet.nodeId, packet.temperatureC, nowMs);
}

void handleModelPacket(const uint8_t* buffer, const size_t length, const uint32_t nowMs) {
  ModelPacket packet;
  if (!decodeModelPacket(buffer, length, packet)) {
    return;
  }
  if (packet.nodeId == gNodeState.localNodeId) {
    return;
  }

  registerPeerModel(gNodeState, packet, nowMs);
}

void handleAggregationPacket(const uint8_t* buffer, const size_t length) {
  AggregationPacket packet;
  if (!decodeAggregationPacket(buffer, length, packet)) {
    return;
  }
  if (packet.nodeId == gNodeState.localNodeId) {
    return;
  }

  applyGlobalModel(gNodeState, packet);
  Serial.println("Applied global model update from aggregator.");
  logModel("Local model", gNodeState.model);
}

void handleDiscoveryPacket(const uint8_t* buffer, const size_t length, const uint32_t nowMs) {
  DiscoveryPacket packet;
  if (!decodeDiscoveryPacket(buffer, length, packet)) {
    return;
  }
  if (packet.nodeId == gNodeState.localNodeId) {
    return;
  }

  registerLivePeer(gNodeState, packet, nowMs);
}

void handleIncomingPacket(const uint8_t* buffer, size_t length, uint32_t nowMs) {
  if (length >= kPacketHeaderSize) {
    PacketType packetType;
    uint8_t sourceNodeId = 0;
    if (getPacketType(buffer, length, packetType, sourceNodeId) &&
        sourceNodeId != gNodeState.localNodeId) {
      switch (packetType) {
        case PacketType::Temperature:
          handleTemperaturePacket(buffer, length, nowMs);
          break;
        case PacketType::ModelUpdate:
          handleModelPacket(buffer, length, nowMs);
          break;
        case PacketType::GlobalAggregation:
          handleAggregationPacket(buffer, length);
          break;
        case PacketType::Discovery:
          handleDiscoveryPacket(buffer, length, nowMs);
          break;
      }
    }
  }
}

void performSampleStep(const uint32_t nowMs, const float dtSeconds) {
  const float measuredTemperatureC = gSensor->readTemperatureC(gNodeState.localNodeId, nowMs);
  const float previousPredictionC = gNodeState.pendingPredictionC;
  const bool hadPrediction = gNodeState.hasPendingPrediction;

  applyMeasurement(gNodeState, measuredTemperatureC, dtSeconds);

  Serial.printf("node=%u peers=%u T=%.2fC lap=%.4f diff=%.4f corr=%.4f pred=%.2fC",
                gNodeState.localNodeId, static_cast<unsigned>(countOnlinePeers(gNodeState)),
                gNodeState.currentTempC, gNodeState.lastLaplacian, gNodeState.lastDiffusionTerm,
                gNodeState.lastCorrectionTerm, gNodeState.pendingPredictionC);
  if (hadPrediction) {
    const float error = measuredTemperatureC - previousPredictionC;
    Serial.printf(" error=%.4f sgd=%.4f loss=%.4f", error, gNodeState.lastPredictionError,
                  gNodeState.lastLocalLoss);
  }
  Serial.println();

  broadcastTemperature();

  if (nowMs - gNodeState.lastDiscoveryMs >= kDiscoveryPeriodMs) {
    broadcastDiscovery();
    gNodeState.lastDiscoveryMs = nowMs;
  }

  if (nowMs - gNodeState.lastModelSyncMs >= kModelSyncPeriodMs) {
    broadcastModel();
    gNodeState.lastModelSyncMs = nowMs;
    Serial.printf("FL local round=%u loss=%.4f weight=%.4f\n", gNodeState.localModelRound,
                  gNodeState.lastLocalLoss, computeAggregationWeight(gNodeState.lastLaplacian));
    logModel("Model sync", gNodeState.model);
  }

  if (shouldBroadcastAggregation(gNodeState) &&
      nowMs - gNodeState.lastAggregationMs >= kAggregationPeriodMs) {
    broadcastAggregation();
    gNodeState.lastAggregationMs = nowMs;
  }
}

void setupWiFi() {
  WiFiManager wifiManager;
  wifiManager.autoConnect("FL-Node-AP");

  Serial.println();
  Serial.print("WiFi connected, IP=");
  Serial.println(WiFi.localIP());
}

}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("Physics-informed federated thermal node booting...");

  pifl::initializeNodeState(gNodeState, resolveLocalNodeId());
  Serial.printf("Local node id=%u\n", gNodeState.localNodeId);
  setupWiFi();

  gSensor = pifl::createSensor();
  gSensor->setup();

  gTransport = pifl::createTransport();
  gTransport->setPacketHandler(handleIncomingPacket);
  gTransport->begin();

  broadcastDiscovery();
  gNodeState.lastDiscoveryMs = millis();
}

void loop() {
  const uint32_t nowMs = millis();

  ensureWiFiConnected(nowMs);
  pifl::expireStaleData(gNodeState, nowMs);
  if (gTransport) {
    gTransport->loop(nowMs);
  }

  if (nowMs - gNodeState.lastSampleMs >= pifl::kSamplePeriodMs) {
    float dtSeconds = (gNodeState.lastSampleMs == 0) ? 
        (static_cast<float>(pifl::kSamplePeriodMs) / 1000.0f) : 
        (static_cast<float>(nowMs - gNodeState.lastSampleMs) / 1000.0f);
    if (dtSeconds < 0.001f) {
      dtSeconds = 0.001f;
    }
    gNodeState.lastSampleMs = nowMs;
    performSampleStep(nowMs, dtSeconds);
  }

  yield();
}
