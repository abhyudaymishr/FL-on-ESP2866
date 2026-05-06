#pragma once

#include <Arduino.h>

namespace pifl {

typedef void (*PacketHandler)(const uint8_t* buffer, size_t length, uint32_t nowMs);

class Transport {
public:
  virtual ~Transport() = default;
  virtual void begin() = 0;
  virtual void loop(uint32_t nowMs) = 0;
  virtual bool sendPacket(const uint8_t* buffer, size_t length) = 0;
  void setPacketHandler(PacketHandler handler) { packetHandler = handler; }

protected:
  PacketHandler packetHandler = nullptr;
};

Transport* createTransport();

}
