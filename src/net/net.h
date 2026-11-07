// Network side of the panel: Wi-Fi, setup portal, companion discovery and polling.
// Runs in its own FreeRTOS task on core 0; the UI (core 1) reads immutable snapshots.
#pragma once

#include <Arduino.h>

#include <memory>
#include <vector>

#include "../app/cover.h"
#include "../app/lyrics.h"

namespace net {

// Cover thumbnail size (px, square box) — also the size fetched from the companion.
constexpr int COVER_THUMB_PX = 56;
// Big cover on the cover view (px, square box), fetched once per track; thumbnail and
// backdrop are derived from it.
constexpr int COVER_BIG_PX = 384;

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

struct ZoneInfo {
    String id, name, state, title, artist;
};

struct OutputInfo {
    String id, name;
    String volumeType;        // "number" | "db" | "incremental" | "" (no volume control)
    float value = 0, min = 0, max = 0;
    bool muted = false;
};

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
    bool canPlay = false, canPause = false, canNext = false, canPrevious = false;
    std::vector<OutputInfo> outputs;                    // of the followed zone
    std::shared_ptr<const std::vector<ZoneInfo>> zones;  // every zone, refreshed every 5 s
    uint32_t zonesSerial = 0;

    // Track cover: thumbnail and backdrop (null = none / plain background).
    // `coverSerial` changes with either.
    std::shared_ptr<const Picture> bigCover, thumb, backdrop;
    uint32_t coverSerial = 0;

    // Last failed user command (control, volume), shown for a few seconds.
    String commandError;
    uint32_t commandErrorAt = 0;

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

// User commands, queued to the net task (never blocks the UI). Always sent with the
// followed zone_id; never POST /api/roon/active-zone (global to the companion).
void control(const char *action);   // "playpause" | "next" | "previous" | "play" | "pause"
void volumeStep(int direction);      // +1 / -1 on every output of the zone
void selectZone(const String &zoneId);   // pin a zone; empty = follow the playing zone

}  // namespace net
