// Where the lyrics come from when the source of the music has none of its own.
//
// A music server hands no lyrics to its extensions, so the direct source asks elsewhere. Three
// kinds of server answer, and the panel speaks to all of them over the same two questions:
//
//   auto       a TextFromTrack companion found on the network, asked by metadata alone
//              (GET /api/lrclib/lookup). It brings what only it has: the lyrics already in the
//              listener's own files, its ranking, and whatever it has cached.
//   a URL      any LRCLIB-compatible server (GET /api/get): a mirror on the local network
//              answers in milliseconds over plain HTTP, the public site works too over HTTPS.
//   none       the panel says there are no lyrics instead of pretending to look.
//
// A bearer token is sent when one is set, so a server that wants one can be pointed at without
// touching this code again.
#pragma once

#include <Arduino.h>

namespace net::lyricsrc {

// Looks up one track and publishes the result (found, not found, instrumental or an error).
// false means no provider is configured at all, and the caller should stop asking.
bool lookup(const String &title, const String &artist, const String &album, uint32_t durationMs);

// Forget a discovered companion, so the next lookup looks for one again.
void forgetDiscovered();

}  // namespace net::lyricsrc
