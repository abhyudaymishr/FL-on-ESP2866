#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
extern "C" {
#include <osapi.h>
#include <user_interface.h>
}

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
  const uint16_t nextRound = static_cast<uint16_t>(gNodeState.localModelRound + 1);
  const ModelPacket packet = buildLocalModelPacket(gNodeState, nextRound);
  const size_t length = encodeModelPacket(packet, buffer, sizeof(buffer));
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
  const size_t length = encodeAggregationPacket(packet, buffer, sizeof(buffer));
  if (sendPacket(buffer, length)) {
    applyGlobalModel(gNodeState, packet);
    Serial.printf("Broadcast aggregate. round=%u contributor_mean_loss=%.4f\n", packet.round,
                  packet.meanLocalLoss);
    logModel("Global model", packet.model);
  }
}

void broadcastDiscovery() {
  uint8_t buffer[kDiscoveryPacketSize];
  const size_t length =
      encodeDiscoveryPacket({gNodeState.localNodeId, localRoleFlags(), gNodeState.bootId}, buffer, sizeof(buffer));
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

  if (!applyGlobalModel(gNodeState, packet)) return;
  Serial.println("Applied global model update from aggregator.");
  logModel("Local model", gNodeState.model);
}

void handleDiscoveryPacket(const uint8_t* buffer, const size_t length, const uint32_t nowMs) {
  DiscoveryPacket packet;
  if (!decodeDiscoveryPacket(buffer, length, packet)) {
    return;
  }
  registerLivePeer(gNodeState, packet, nowMs);
}

void handleIncomingPacket(const uint8_t* buffer, size_t length, uint32_t nowMs) {
  if (length >= kPacketHeaderSize) {
    PacketType packetType;
    uint8_t sourceNodeId = 0;
    if (getPacketType(buffer, length, packetType, sourceNodeId) &&
        (sourceNodeId != gNodeState.localNodeId || packetType == PacketType::Discovery)) {
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

void performSampleStep(const uint32_t nowMs) {
  const float measuredTemperatureC = gSensor->readTemperatureC(gNodeState.localNodeId, nowMs);
  const uint32_t measuredMs = millis();
  const float elapsedSeconds = gNodeState.lastMeasurementMs == 0 ? kSamplePeriodMs / 1000.0f :
      (measuredMs - gNodeState.lastMeasurementMs) / 1000.0f;
  gNodeState.lastMeasurementMs = measuredMs;
  if (!applyMeasurement(gNodeState, measuredTemperatureC, elapsedSeconds)) {
    Serial.println("Skipped invalid sensor reading or conflicting node identity.");
    return;
  }
  Serial.printf("node=%u peers=%u T=%.2fC lap=%.4f pred=%.2fC error=%.4f loss=%.4f\n",
      gNodeState.localNodeId, static_cast<unsigned>(countOnlinePeers(gNodeState)),
      gNodeState.currentTempC, gNodeState.lastLaplacian, gNodeState.pendingPredictionC,
      gNodeState.lastPredictionError, gNodeState.lastLocalLoss);

  broadcastTemperature();

  if (nowMs - gNodeState.lastDiscoveryMs >= kDiscoveryPeriodMs) {
    broadcastDiscovery();
    gNodeState.lastDiscoveryMs = nowMs;
  }

  if (nowMs - gNodeState.lastModelSyncMs >= kModelSyncPeriodMs) {
    broadcastModel();
    gNodeState.lastModelSyncMs = nowMs;
    Serial.printf("FL sync=%u samples=%lu local_loss=%.4f\n", gNodeState.localModelRound,
                  static_cast<unsigned long>(gNodeState.localSampleCount), gNodeState.lastLocalLoss);
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

  pifl::initializeNodeState(gNodeState, resolveLocalNodeId(), os_random());
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
    gNodeState.lastSampleMs = nowMs;
    performSampleStep(nowMs);
  }

  yield();
}
