#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

#include "config.h"
#include "node_state.h"
#include "protocol.h"
#include "sensor.h"

namespace {

using namespace pifl;

WiFiUDP gUdp;
NodeState gNodeState;

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
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.begin(kWifiSsid, kWifiPassword);
  Serial.println("Attempting WiFi reconnect...");
}

bool sendPacket(const uint8_t* buffer, const size_t length) {
  if (length == 0 || WiFi.status() != WL_CONNECTED) {
    return false;
  }

  if (!gUdp.beginPacket(kBroadcastIp, kProtocolPort)) {
    return false;
  }

  const size_t written = gUdp.write(buffer, length);
  const bool ok = gUdp.endPacket();
  return written == length && ok;
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
  const size_t length =
      encodeModelPacket(gNodeState.localNodeId, gNodeState.model, weight, buffer, sizeof(buffer));
  sendPacket(buffer, length);
}

void broadcastAggregation() {
  AggregationPacket packet;
  if (!buildAggregatedModel(gNodeState, packet)) {
    return;
  }

  uint8_t buffer[kAggregationPacketSize];
  const size_t length =
      encodeAggregationPacket(gNodeState.localNodeId, packet.model, buffer, sizeof(buffer));
  if (sendPacket(buffer, length)) {
    applyGlobalModel(gNodeState, packet);
    Serial.println("Broadcast global aggregated model.");
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

void processInboundPackets(const uint32_t nowMs) {
  int packetSize = gUdp.parsePacket();
  while (packetSize > 0) {
    uint8_t buffer[kMaxPacketSize];
    const int bytesRead = gUdp.read(buffer, sizeof(buffer));

    if (bytesRead >= static_cast<int>(kPacketHeaderSize)) {
      PacketType packetType;
      uint8_t sourceNodeId = 0;
      if (getPacketType(buffer, static_cast<size_t>(bytesRead), packetType, sourceNodeId) &&
          sourceNodeId != gNodeState.localNodeId) {
        switch (packetType) {
          case PacketType::Temperature:
            handleTemperaturePacket(buffer, static_cast<size_t>(bytesRead), nowMs);
            break;
          case PacketType::ModelUpdate:
            handleModelPacket(buffer, static_cast<size_t>(bytesRead), nowMs);
            break;
          case PacketType::GlobalAggregation:
            handleAggregationPacket(buffer, static_cast<size_t>(bytesRead));
            break;
          case PacketType::Discovery:
            handleDiscoveryPacket(buffer, static_cast<size_t>(bytesRead), nowMs);
            break;
        }
      }
    }

    packetSize = gUdp.parsePacket();
  }
}

void performSampleStep(const uint32_t nowMs) {
  const float measuredTemperatureC = readTemperatureC(gNodeState.localNodeId, nowMs);
  const float previousPredictionC = gNodeState.pendingPredictionC;
  const bool hadPrediction = gNodeState.hasPendingPrediction;

  applyMeasurement(gNodeState, measuredTemperatureC);

  Serial.printf("node=%u peers=%u T=%.2fC lap=%.4f diff=%.4f corr=%.4f pred=%.2fC",
                gNodeState.localNodeId, static_cast<unsigned>(countOnlinePeers(gNodeState)),
                gNodeState.currentTempC, gNodeState.lastLaplacian, gNodeState.lastDiffusionTerm,
                gNodeState.lastCorrectionTerm, gNodeState.pendingPredictionC);
  if (hadPrediction) {
    const float error = measuredTemperatureC - previousPredictionC;
    Serial.printf(" error=%.4f sgd=%.4f", error, gNodeState.lastPredictionError);
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
    logModel("Model sync", gNodeState.model);
  }

  if (shouldBroadcastAggregation(gNodeState) &&
      nowMs - gNodeState.lastAggregationMs >= kAggregationPeriodMs) {
    broadcastAggregation();
    gNodeState.lastAggregationMs = nowMs;
  }
}

void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.begin(kWifiSsid, kWifiPassword);

  Serial.printf("Joining WiFi SSID '%s'\n", kWifiSsid);
  const uint32_t startMs = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startMs < 8000) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi connected, IP=");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi not connected yet. Firmware will keep retrying.");
  }

  gUdp.begin(kProtocolPort);
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
  broadcastDiscovery();
  gNodeState.lastDiscoveryMs = millis();
}

void loop() {
  const uint32_t nowMs = millis();

  ensureWiFiConnected(nowMs);
  pifl::expireStaleData(gNodeState, nowMs);
  processInboundPackets(nowMs);

  if (nowMs - gNodeState.lastSampleMs >= pifl::kSamplePeriodMs) {
    gNodeState.lastSampleMs = nowMs;
    performSampleStep(nowMs);
  }

  yield();
}
