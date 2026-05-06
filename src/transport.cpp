#include "transport.h"
#include "config.h"
#include "protocol.h"

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>

namespace pifl {

namespace {

uint32_t computeSignature(const uint8_t* buffer, size_t length, const char* secret) {
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < length; ++i) {
    hash ^= buffer[i];
    hash *= 16777619u;
  }
  for (size_t i = 0; secret[i] != '\0'; ++i) {
    hash ^= secret[i];
    hash *= 16777619u;
  }
  return hash;
}

void writeUInt32LE(uint8_t* buffer, uint32_t value) {
  buffer[0] = static_cast<uint8_t>(value & 0xFF);
  buffer[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
  buffer[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
  buffer[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

uint32_t readUInt32LE(const uint8_t* buffer) {
  return static_cast<uint32_t>(buffer[0]) |
         (static_cast<uint32_t>(buffer[1]) << 8) |
         (static_cast<uint32_t>(buffer[2]) << 16) |
         (static_cast<uint32_t>(buffer[3]) << 24);
}

size_t signAndWrap(const uint8_t* input, size_t inputLength, uint8_t* output) {
  for (size_t i = 0; i < inputLength; ++i) {
    output[i] = input[i];
  }
  const uint32_t sig = computeSignature(input, inputLength, kNetworkSecret);
  writeUInt32LE(&output[inputLength], sig);
  return inputLength + 4;
}

bool verifyAndUnwrap(const uint8_t* input, size_t inputLength, uint8_t* output, size_t& outputLength) {
  if (inputLength < 4) {
    return false;
  }
  const size_t payloadLength = inputLength - 4;
  const uint32_t expectedSig = computeSignature(input, payloadLength, kNetworkSecret);
  const uint32_t actualSig = readUInt32LE(&input[payloadLength]);
  if (expectedSig != actualSig) {
    return false;
  }
  for (size_t i = 0; i < payloadLength; ++i) {
    output[i] = input[i];
  }
  outputLength = payloadLength;
  return true;
}

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
    while (packetSize > 0) {
      uint8_t rxBuffer[kMaxPacketSize + 4];
      const int bytesRead = udp.read(rxBuffer, sizeof(rxBuffer));

      if (bytesRead > 4 && packetHandler) {
        uint8_t unwrapped[kMaxPacketSize];
        size_t unwrappedLength = 0;
        if (verifyAndUnwrap(rxBuffer, static_cast<size_t>(bytesRead), unwrapped, unwrappedLength)) {
          packetHandler(unwrapped, unwrappedLength, nowMs);
        } else {
          Serial.println("Warning: Invalid UDP packet signature rejected.");
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
    const size_t txLength = signAndWrap(buffer, length, txBuffer);

    if (!udp.beginPacket(kBroadcastIp, kProtocolPort)) {
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
    const size_t txLength = signAndWrap(buffer, length, txBuffer);

    return mqttClient.publish(kMqttTopic, txBuffer, txLength);
  }

  void handleIncoming(const uint8_t* payload, unsigned int length) {
    if (length > 4 && packetHandler) {
      uint8_t unwrapped[kMaxPacketSize];
      size_t unwrappedLength = 0;
      if (verifyAndUnwrap(payload, length, unwrapped, unwrappedLength)) {
        packetHandler(unwrapped, unwrappedLength, millis());
      } else {
        Serial.println("Warning: Invalid MQTT packet signature rejected.");
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
