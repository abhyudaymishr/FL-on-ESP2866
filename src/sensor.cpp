#include "sensor.h"

#include <math.h>

#include "config.h"
#include <OneWire.h>
#include <DallasTemperature.h>

namespace pifl {

class SyntheticSensor : public TemperatureSensor {
public:
  float readTemperatureC(uint8_t nodeId, uint32_t nowMs) override {
    const float seconds = static_cast<float>(nowMs) * 0.001f;
    const float angularPhase =
        (TWO_PI * seconds / kSyntheticWavePeriodSeconds) + nodeId * kSyntheticNodePhaseRadians;
    return kInitialTemperatureC + kSyntheticAmplitudeC * sinf(angularPhase);
  }
};

class AnalogSensor : public TemperatureSensor {
public:
  float readTemperatureC(uint8_t /* nodeId */, uint32_t /* nowMs */) override {
    const int raw = analogRead(kAnalogPin);
    return static_cast<float>(raw) * kAnalogSlope + kAnalogOffset;
  }
};

class DS18B20Sensor : public TemperatureSensor {
private:
  OneWire oneWire;
  DallasTemperature sensors;
public:
  DS18B20Sensor() : oneWire(kDs18b20Pin), sensors(&oneWire) {}

  void setup() override {
    sensors.begin();
  }

  float readTemperatureC(uint8_t /* nodeId */, uint32_t /* nowMs */) override {
    sensors.requestTemperatures();
    float temp = sensors.getTempCByIndex(0);
    if (temp == DEVICE_DISCONNECTED_C) {
      return NAN;
    }
    return temp;
  }
};

TemperatureSensor* createSensor() {
  switch (kSensorMode) {
    case SensorMode::AnalogLinear:
      return new AnalogSensor();
    case SensorMode::DS18B20:
      return new DS18B20Sensor();
    case SensorMode::Synthetic:
    default:
      return new SyntheticSensor();
  }
}

}
