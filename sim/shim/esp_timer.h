// Host stand-in: microseconds on the simulator's clock (see millis()).
#pragma once

#include <Arduino.h>

inline int64_t esp_timer_get_time() { return (int64_t)millis() * 1000; }
