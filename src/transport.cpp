#include "transport.h"
#include "config.h"
#include "protocol.h"
#include "packet_envelope.h"

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>

namespace pifl {

namespace {

void mqttCallbackWrapper(char* topic, byte* payload, unsigned int length);

class UdpTransport : public Transport {
private:
  WiFiUDP udp;

public:
  void begin() override {
    udp.begin(kProtocolPort);
  }

  void loop(uint32_t nowMs) override {
    int packetSize = udp.parsePacket();
    for (unsigned handled = 0; packetSize > 0 && handled < 8; ++handled) {
      if (packetSize > static_cast<int>(kMaxPacketSize + kPacketChecksumSize)) {
        while (udp.available()) udp.read();
        packetSize = udp.parsePacket();
        continue;
      }
      uint8_t rxBuffer[kMaxPacketSize + 4];
      const int bytesRead = udp.read(rxBuffer, sizeof(rxBuffer));

      if (bytesRead == packetSize && bytesRead > 4 && packetHandler) {
        uint8_t unwrapped[kMaxPacketSize];
        size_t unwrappedLength = 0;
        if (unwrapPacket(rxBuffer, static_cast<size_t>(bytesRead), unwrapped, sizeof(unwrapped), unwrappedLength, kNetworkSecret)) {
          packetHandler(unwrapped, unwrappedLength, nowMs);
        } else {
          Serial.println("Warning: Invalid UDP packet checksum rejected.");
        }
      }

      packetSize = udp.parsePacket();
    }
  }

  bool sendPacket(const uint8_t* buffer, size_t length) override {
    if (length == 0 || WiFi.status() != WL_CONNECTED) {
      return false;
    }

    uint8_t txBuffer[kMaxPacketSize + 4];
    const size_t txLength = wrapPacket(buffer, length, txBuffer, sizeof(txBuffer), kNetworkSecret);

    if (txLength == 0 || !udp.beginPacket(kBroadcastIp, kProtocolPort)) {
      return false;
    }

    const size_t written = udp.write(txBuffer, txLength);
    const bool ok = udp.endPacket();
    return written == txLength && ok;
  }
};

class MqttTransport : public Transport {
private:
  WiFiClient wifiClient;
  PubSubClient mqttClient;
  uint32_t lastReconnectAttempt = 0;

  void reconnectMqtt(uint32_t nowMs) {
    if (nowMs - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = nowMs;
      Serial.println("Attempting MQTT connection...");
      char clientId[32];
      sprintf(clientId, "PIFL-Node-%06X", ESP.getChipId());

      if (mqttClient.connect(clientId)) {
        Serial.println("MQTT connected!");
        mqttClient.subscribe(kMqttTopic);
      }
    }
  }

public:
  MqttTransport() : mqttClient(wifiClient) {}

  void begin() override {
    mqttClient.setServer(kMqttBroker, kMqttPort);
    mqttClient.setSocketTimeout(1);
    mqttClient.setCallback(mqttCallbackWrapper);
  }

  void loop(uint32_t nowMs) override {
    if (!mqttClient.connected() && WiFi.status() == WL_CONNECTED) {
      reconnectMqtt(nowMs);
    }
    mqttClient.loop();
  }

  bool sendPacket(const uint8_t* buffer, size_t length) override {
    if (length == 0 || !mqttClient.connected()) {
      return false;
    }

    uint8_t txBuffer[kMaxPacketSize + 4];
    const size_t txLength = wrapPacket(buffer, length, txBuffer, sizeof(txBuffer), kNetworkSecret);

    return txLength != 0 && mqttClient.publish(kMqttTopic, txBuffer, txLength);
  }

  void handleIncoming(const uint8_t* payload, unsigned int length) {
    if (length > 4 && length <= kMaxPacketSize + kPacketChecksumSize && packetHandler) {
      uint8_t unwrapped[kMaxPacketSize];
      size_t unwrappedLength = 0;
      if (unwrapPacket(payload, length, unwrapped, sizeof(unwrapped), unwrappedLength, kNetworkSecret)) {
        packetHandler(unwrapped, unwrappedLength, millis());
      } else {
        Serial.println("Warning: Invalid MQTT packet checksum rejected.");
      }
    }
  }
};

MqttTransport* gMqttTransportInstance = nullptr;

void mqttCallbackWrapper(char* topic, byte* payload, unsigned int length) {
  if (gMqttTransportInstance) {
    gMqttTransportInstance->handleIncoming(payload, length);
  }
}

} // namespace

Transport* createTransport() {
  if (kTransportMode == TransportMode::MQTT) {
    gMqttTransportInstance = new MqttTransport();
    return gMqttTransportInstance;
  } else {
    return new UdpTransport();
  }
}

} // namespace pifl
