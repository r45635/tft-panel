// What the net task shares with its helper modules: the sources (source_companion.cpp) and the
// music library (library.cpp). Net task only, never the UI.
//
// A source reads the server and calls the `publish*` functions; everything they then do — the
// position clock, the track change, the covers, the lyrics retries, the status line — belongs to
// the net task and is the same whichever source is in use.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include <memory>
#include <vector>

#include "../app/lyrics.h"
#include "net.h"

namespace net {
struct NowPlaying;
}

namespace net::internal {

struct HttpResult {
    int code = 0;          // HTTP status, or < 0 for a transport error
    String errorCode;      // server error.code when present
    String message;        // short human-readable reason
};

ArduinoJson::Allocator *psram();   // JSON documents in PSRAM

// GETs a binary body into a PSRAM buffer the caller frees (heap_caps_free). `url` is absolute.
HttpResult getBytes(const String &url, uint8_t *&buf, size_t &len, size_t maxLen, uint32_t timeoutMs);

void ensureMdns();          // our own name on the LAN, also needed before an mDNS query

// --- Status ---
void setPhase(Phase p, const String &status);
void publishServer(const String &url);     // the server in use, shown on the About line
void markHealthy();                        // a freshly installed image answered: keep it

// --- Zones and playback ---
// Publishes the zone list and picks the one to follow: the pinned one, else the current one
// while it plays, else the first playing, else the first with a track, else the first.
// false = there is no zone at all, and nothing was published.
bool publishZones(std::shared_ptr<std::vector<ZoneInfo>> zones);
String followedZoneId();                   // empty if none
std::vector<OutputInfo> followedOutputs();
void currentTrack(String &title, String &artist, String &album, uint32_t *durationMs = nullptr);
// The followed zone's track. Runs the position clock, spots a track change, asks for the cover
// and the lyrics, and sets the status line.
// Takes ownership: the outputs move into the snapshot rather than being copied under
// the lock, three times a second.
void publishNowPlaying(NowPlaying np);
void publishIdle();                        // the followed zone has nothing loaded

// --- Lyrics ---
void publishLyricsLoading();
void publishLyrics(LyricsStatus st, std::shared_ptr<const lyrics::Lyrics> lyrics, const String &source,
                   int32_t offsetMs);
void publishLyricsFailed(LyricsStatus st, const String &message);
void publishOffset(int32_t offsetMs);

// --- Commands ---
void commandFailed(const String &what, const HttpResult &r);

// --- Music library ---
void publishLibrary(std::shared_ptr<const LibraryPage> page);

}  // namespace net::internal
