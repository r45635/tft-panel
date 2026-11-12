// Firmware updates from the GitHub releases of r45635/tft-panel, run by the net task.
//
// Check: GET the latest release over HTTPS (built-in CA bundle) at boot and every 24 h;
// a newer tag vX.Y.Z than FW_VERSION becomes `Available`. Install (asked by the UI after the
// user confirmed on screen): stream tft-panel-<tag>.bin into the other app slot while hashing
// it, compare with tft-panel-<tag>.sha256, then reboot into it.
// Rollback: a freshly installed image stays "pending verify" until markHealthy() (Wi-Fi up
// and the companion answered); if that does not happen within 3 minutes it restarts and
// the bootloader goes back to the previous version.
#pragma once

#include <Arduino.h>

#include <functional>

namespace ota {

enum class State : uint8_t { Idle, Checking, UpToDate, Available, Downloading, Installed, Failed };

struct Status {
    State state = State::Idle;
    String latest;        // newest release tag seen ("v0.2.0")
    int percent = 0;      // while Downloading
    String error;         // when Failed
};

void begin();              // at boot: arms the rollback deadline if this image is unverified
uint32_t version();        // bumps on every status change (cheap to poll)
Status status();           // copy under the lock

// Net task only.
void tick(bool wifiUp);    // periodic check, pending install
void markHealthy();        // the new image works: cancel the rollback

// Any task: requests, executed by the net task.
void requestCheck();
void requestInstall();

const char *stateName(State s);

// Writes a whole image received in memory to the next OTA slot and marks it to boot (pending
// verify). For boards that must not write flash while the network is busy (OTA_DOWNLOAD_FIRST).
bool writeImage(const uint8_t *data, size_t len, String &error, const std::function<void(int)> &progress = nullptr);

}  // namespace ota
