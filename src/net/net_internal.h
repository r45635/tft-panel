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

// "http://<ip>:<port>" of a service announced on the network, empty when there is none.
// 🪤 It walks every address of every answer itself: ESPmDNS hands back only the first IPv4 of
// each, and a Mac announces every interface it has - a laptop with a dock or a second adapter
// often lists a self-assigned 169.254 address first, which is not routable from here. Those are
// refused; an address on our own /24 wins over anything else. `hostOut` takes the host name.
// `txtApi` / `txtVersion` take the `api` and `version` TXT keys of the chosen answer (a
// companion since 0.7.1 announces api=get: its /api/get goes through the player's chain).
String mdnsServiceUrl(const char *service, const char *proto, String *hostOut = nullptr,
                      String *txtApi = nullptr, String *txtVersion = nullptr);
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

// --- Idle clock extras ---
void publishWeather(const weather::Now &w);
void publishNews(const String &headlines, const String &problem = String());

}  // namespace net::internal
