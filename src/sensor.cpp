#include "sensor.h"

#include <math.h>

#include "config.h"

namespace pifl {

float readTemperatureC(const uint8_t nodeId, const uint32_t nowMs) {
  if (kSensorMode == SensorMode::AnalogLinear) {
    const int raw = analogRead(kAnalogPin);
    return static_cast<float>(raw) * kAnalogSlope + kAnalogOffset;
  }

  const float seconds = static_cast<float>(nowMs) * 0.001f;
  const float angularPhase =
      (TWO_PI * seconds / kSyntheticWavePeriodSeconds) + nodeId * kSyntheticNodePhaseRadians;
  return kInitialTemperatureC + kSyntheticAmplitudeC * sinf(angularPhase);
}

}
