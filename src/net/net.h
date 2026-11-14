// Network side of the panel: Wi-Fi, setup portal, companion discovery and polling.
// Runs in its own FreeRTOS task on core 0; the UI (core 1) reads immutable snapshots.
#pragma once

#include <Arduino.h>

#include <memory>
#include <vector>

#include "../app/cover.h"
#include "../app/lyrics.h"
#include "../layout.h"
#include "psram_text.h"
#include "weather.h"

namespace net {

// Cover thumbnail size (px, square box) — also the size fetched from the companion.
constexpr int COVER_THUMB_PX = layout::S(56);
// Big cover on the cover view (px, square box), fetched once per track; thumbnail and
// backdrop are derived from it.
constexpr int COVER_BIG_PX = layout::S(384);

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
    float value = 0, min = 0, max = 0, step = 1;
    bool muted = false;
};

// A Wi-Fi network heard by the panel (2.4 GHz), strongest first, one entry per name.
struct WifiNet {
    String ssid;
    int8_t rssi = 0;
    bool open = false;   // no password
};

// The music library (boards with HAS_LIBRARY): one browse level as the panel shows it.
// Library text and lists live in PSRAM (psram_text.h): pages can hold hundreds of items.
struct LibItem {
    PStr title, subtitle;
    PStr itemKey;
    PStr hint;            // "list" (opens), "action_list" (playable: actions), "action", "header"
    PStr imageKey;
    bool prompt = false;  // asks for text (Roon's own search entry): the panel uses its Search tab
    std::shared_ptr<const Picture> thumb;   // filled in as thumbnails arrive
};
using LibItems = std::vector<LibItem, PsramAllocator<LibItem>>;

struct LibraryPage {
    uint32_t id = 0;              // changes when the items change, not when a thumbnail arrives
    String hierarchy;             // "search" | "browse"
    String query;                 // the search that opened it
    std::vector<String> path;     // level titles from the top
    int total = 0;                // items in this level (more than `items` until all are loaded)
    LibItems items;
    String actionsFor;            // title of the item whose actions are open, empty if none
    LibItems actions;
    bool loading = false;
    String message;               // result of an action, or an error
    bool error = false;
    uint32_t messageAt = 0;       // millis()
};

// Find = Search then open the one exact match (index 0 artist, 1 album; text "<name>\n<artist>").
enum class LibOp : uint8_t { Open, Close, Search, Browse, Find, Drill, Back, More, Actions, Action, CloseActions };

struct Snapshot {
    Phase phase = Phase::WifiConnecting;
    String status;            // one human-readable line, with the reason on failure

    // Setup portal (soft AP), shown as QR codes when active.
    bool portalActive = false;
    String apSsid, apPass, apIp;

    String staIp;
    String companion;         // base URL in use, empty if none yet

    // Last Wi-Fi scan (setup portal, on-screen Wi-Fi setup). `networksSerial` changes with it.
    std::shared_ptr<const std::vector<WifiNet>> networks;
    uint32_t networksSerial = 0;
    bool scanning = false;

    // Music library browsing (HAS_LIBRARY boards), null until first opened.
    std::shared_ptr<const LibraryPage> library;
    uint32_t librarySerial = 0;

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

    // Shown on the idle clock when a place is set. `weatherSerial` changes with it.
    weather::Now weather;
    uint32_t weatherSerial = 0;
    String news;              // headlines, joined; empty = no feed set
    uint32_t newsSerial = 0;

    bool playing() const { return state == "playing"; }
    // Position extrapolated to `now` (millis()), clamped to the track duration.
    int32_t positionAt(uint32_t now) const;
};

void begin();                // starts the network task
String hostName();           // "tft-panel-xxxx" (mDNS: tft-panel-xxxx.local, the update page)
uint32_t version();          // bumps on every snapshot change (cheap to poll)
Snapshot snapshot();         // copy under the lock

void openPortal();           // start the setup portal even with Wi-Fi configured
void requestScan();          // scan for Wi-Fi networks now (result in Snapshot::networks)
void reconnect();            // re-read settings (Wi-Fi, companion, zone) and start over
void pause(bool on);         // hold all network calls (used while dumping a screenshot)

// User commands, queued to the net task (never blocks the UI). Always sent with the
// followed zone_id; never POST /api/roon/active-zone (global to the companion).
void control(const char *action);   // "playpause" | "next" | "previous" | "play" | "pause"
void volumeStep(int direction);      // +1 / -1 on every output of the zone
void selectZone(const String &zoneId);   // pin a zone; empty = follow the playing zone
void volumeSet(float value);         // absolute, on the zone's first output with a volume
void mute(bool on);                  // every output of the zone
// Saves the current track's lyrics offset in the companion (> 0 = lyrics later).
void setOffset(int32_t ms);

// Library (HAS_LIBRARY boards): Open/Close (thumbnails only load while open), Search `text`,
// Browse (Roon's root), Drill/Actions/Action on item `index`, Back, More (next page), CloseActions.
void library(LibOp op, int index = -1, const String &text = String());

}  // namespace net
