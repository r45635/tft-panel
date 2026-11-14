// Where the panel gets what it shows: one interface, one implementation chosen at startup.
//
// `source_companion.*` is the TextFromTrack companion over its LAN HTTP API, which is what the
// panel has always used. A second one talking to the music server directly can be added behind
// this interface without the rest of the firmware knowing: the net task owns the snapshot, the
// position clock, the covers and the commands, and a source only answers "what is playing".
//
// Everything here runs on the net task.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include <vector>

#include "net.h"
#include "net_internal.h"

namespace net {

enum class CmdType : uint8_t { Control, Volume, Zone, VolumeSet, Mute, Offset, Library };

// A user action, queued from the UI so a tap never blocks it.
struct Command {
    CmdType type;
    int8_t direction;
    char arg[128];   // a search text, or Find's "<name>\n<artist>"
    float value;
};

// What a source knows about the followed zone at one instant, in the terms the snapshot uses.
struct NowPlaying {
    String zoneId;
    String state;            // "playing" | "paused" | "loading" | "stopped"
    String title, artist, album, imageKey;
    uint32_t durationMs = 0;
    int32_t seekMs = 0;      // whole seconds, as the server reports them
    uint32_t sampleAtMs = 0; // local millis() at which `seekMs` was true
    int32_t slackMs = 0;     // how much to widen the position window (round trip, jitter)
    bool canPlay = false, canPause = false, canNext = false, canPrevious = false;
    std::vector<OutputInfo> outputs;
};

// What this source can do, for the screens that offer it.
struct Capabilities {
    bool lyrics = false;              // it can resolve lyrics at all
    bool lyricsOffsetPersist = false; // a per-track offset is remembered beyond this session
};

class Source {
  public:
    virtual ~Source() = default;

    virtual const char *name() const = 0;          // "companion", "roon"...
    virtual Capabilities caps() const = 0;

    // --- Connection ---
    virtual bool ready() const = 0;                // a server is known and worth talking to
    // Looks for one. false = not yet; the caller waits and asks again.
    virtual bool connect(uint32_t now) = 0;
    virtual void reset() = 0;                      // forget the server and the session
    // Called every loop. A source that is pushed to reads its socket here; a polled one has
    // nothing to do.
    virtual void pump() {}

    // --- Following the playback ---
    // The zone list and the followed track, each on its own schedule. `refreshZones` returning
    // false means the caller should back off; it has already reported why.
    virtual bool zonesDue(uint32_t now) const = 0;
    virtual bool refreshZones() = 0;
    virtual bool nowPlayingDue(uint32_t now) const = 0;
    virtual void pollNowPlaying() = 0;
    virtual void zoneSelectionChanged() = 0;       // the followed zone was pinned or unpinned

    // --- Acting ---
    virtual void command(const Command &c) = 0;    // control, volume, mute, lyrics offset
    // Resolves the lyrics of what is playing and publishes them. false = this source has none,
    // and the caller will not retry.
    virtual bool fetchLyrics(const String &zoneId) = 0;

    // --- Music library (HAS_LIBRARY boards) ---
    // The body is already in the server's own terms (hierarchy, multi_session_key, item_key...).
    virtual internal::HttpResult browse(JsonDocument &body, JsonDocument &resp) = 0;
    virtual internal::HttpResult browseLoad(JsonDocument &body, JsonDocument &resp) = 0;

    // Full URL of a cover at `px` pixels, empty when there is nothing to fetch. Built on the net
    // task and passed to the cover task, which must never read a source's mutable state.
    virtual String imageUrl(const String &imageKey, int px) const = 0;
};

Source &source();   // the one in use, net task only

}  // namespace net
