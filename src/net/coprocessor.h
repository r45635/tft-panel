// ESP32-P4 boards: Wi-Fi is an ESP32-C6 running its own firmware (esp_hosted). A factory C6
// firmware older than the esp_hosted host side built into this firmware may carry the local
// network but not UDP replies or Internet traffic (Waveshare 7B: no DNS, no GitHub, no OTA).
// This firmware embeds the matching C6 image and installs it from flash, no network needed.
#pragma once

#include <Arduino.h>

#include <functional>

namespace coprocessor {

// Call from the net task before Wi-Fi is used. When the C6 runs an older firmware than the
// embedded one, installs it (about a minute, `status` gets progress messages) and returns true:
// the caller restarts. At most two attempts per version, so a C6 that refuses it cannot keep
// the panel from starting. Always false on boards without a coprocessor.
bool updateIfNeeded(const std::function<void(const String &)> &status);

}  // namespace coprocessor
