#pragma once

#include <Arduino.h>

namespace pifl {

class TemperatureSensor {
public:
  virtual ~TemperatureSensor() = default;
  virtual void setup() {}
  virtual float readTemperatureC(uint8_t nodeId, uint32_t nowMs) = 0;
};

TemperatureSensor* createSensor();

}
