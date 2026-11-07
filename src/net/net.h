// Network side of the panel: Wi-Fi, setup portal, companion discovery and polling.
// Runs in its own FreeRTOS task on core 0; the UI (core 1) reads immutable snapshots.
#pragma once

#include <Arduino.h>

#include <memory>

#include "../app/lyrics.h"

namespace net {

enum class Phase : uint8_t {
    Setup,            // no Wi-Fi configured: setup portal only
    WifiConnecting,
    WifiFailed,       // credentials stored but the network refuses / is absent
    Discovering,      // looking for a companion on the LAN (mDNS)
    CompanionError,   // companion found or configured but not answering correctly
    Idle,             // connected, nothing playing in the followed zone
    Playing,          // a track is loaded (playing or paused)
};

enum class LyricsStatus : uint8_t { None, Loading, Found, NotFound, Instrumental, Error };

struct Snapshot {
    Phase phase = Phase::WifiConnecting;
    String status;            // one human-readable line, with the reason on failure

    // Setup portal (soft AP), shown as QR codes when active.
    bool portalActive = false;
    String apSsid, apPass, apIp;

    String staIp;
    String companion;         // base URL in use, empty if none yet

    // Followed zone and track.
    String zoneId, zoneName;
    bool zonePinned = false;
    String state;             // "playing" | "paused" | "loading" | "stopped"
    String title, artist, album;
    uint32_t durationMs = 0;

    // Position model: positionMs was true at local time positionAtMs (millis()).
    int32_t positionMs = 0;
    uint32_t positionAtMs = 0;
    int32_t positionUncertaintyMs = -1;   // PositionClock window, -1 when paused

    // Lyrics of the current track. `lyricsSerial` changes whenever `lyrics` does.
    LyricsStatus lyricsStatus = LyricsStatus::None;
    uint32_t lyricsSerial = 0;
    int32_t offsetMs = 0;     // > 0 = lyrics shown later (lyric time = position - offset)
    std::shared_ptr<const lyrics::Lyrics> lyrics;
    String lyricsSource, lyricsError;

    bool playing() const { return state == "playing"; }
    // Position extrapolated to `now` (millis()), clamped to the track duration.
    int32_t positionAt(uint32_t now) const;
};

void begin();                // starts the network task
uint32_t version();          // bumps on every snapshot change (cheap to poll)
Snapshot snapshot();         // copy under the lock

void openPortal();           // start the setup portal even with Wi-Fi configured
void reconnect();            // re-read settings (Wi-Fi, companion, zone) and start over
void pause(bool on);         // hold all network calls (used while dumping a screenshot)

}  // namespace net
