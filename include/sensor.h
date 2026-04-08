#pragma once

#include <Arduino.h>

namespace pifl {

float readTemperatureC(uint8_t nodeId, uint32_t nowMs);

}
