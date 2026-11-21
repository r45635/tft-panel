// Where the lyrics come from when the source of the music has none of its own.
//
// A music server hands no lyrics to its extensions, so the direct source asks elsewhere. The
// choice is a ladder (lyrics_ladder.h), not a switch:
//
//   a companion found on the network   always first: it brings what only it has - the lyrics
//                                      already in the listener's own files, the corrections,
//                                      the timing offset. One announcing api=get (v0.7.1+) is
//                                      asked LRCLIB's /api/get, which goes through the player's
//                                      chain; an older one /api/lrclib/lookup
//   the fallback server on the page    any LRCLIB-compatible server (GET /api/get): a mirror on
//                                      the local network answers in milliseconds over plain
//                                      HTTP, the public site works too over HTTPS
//   none                               the panel says there are no lyrics instead of looking
//   nothing                            the screen says what is missing and where it is typed,
//                                      and the panel looks again once a minute
//
// A bearer token is sent when one is set, so a server that wants one can be pointed at without
// touching this code again.
#pragma once

#include <Arduino.h>

namespace net::lyricsrc {

// Looks up one track and publishes the result (found, not found, instrumental or an error).
// false means nothing could be asked at all, and the caller should stop asking for this track.
bool lookup(const String &title, const String &artist, const String &album, uint32_t durationMs);

// Forget a discovered companion, so the next lookup looks for one again. Safe from any task.
void forgetDiscovered();

// Why this panel has no lyrics to show, in the listener's words, or "" when it has a server to
// ask. A screen that shows nothing and says nothing is the one failure this product cannot
// afford: lyrics are the reason it exists, so "no companion, no fallback" is said out loud, on
// the screen and on the panel's page, not only in the serial log.
String whyNoLyrics();

// true when that "no" may change on its own (a companion starting, a server typed): the caller
// keeps looking once a minute.
bool waitingForServer();

// What the last look for a companion found. Copied under a lock: safe from the web and console
// tasks, which show it on the page and in `status`.
struct CompanionInfo {
    enum class State : uint8_t { Unknown, NotFound, Found };
    State state = State::Unknown;
    String url, host, version, api;
};
CompanionInfo companionInfo();

}  // namespace net::lyricsrc
