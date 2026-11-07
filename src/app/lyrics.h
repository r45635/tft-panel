// Lyrics model: LRC parsing and "which line is current" lookup. No LVGL, no Arduino
// networking, so it can be unit-tested on the host later.
#pragma once

#include <Arduino.h>

#include <vector>

namespace lyrics {

struct Line {
    int32_t timeMs;   // -1 for plain (unsynced) lyrics
    String text;
};

struct Lyrics {
    bool synced = false;
    std::vector<Line> lines;   // sorted by timeMs when synced
};

// Parses LRC ("[mm:ss.xx]text", several stamps per line allowed, metadata tags
// ignored). When no line carries a timestamp, the text is kept as plain lines.
Lyrics parse(const String &text);

// Index of the line shown at `lyricTimeMs` (position minus the track offset),
// -1 before the first line.
int currentIndex(const Lyrics &l, int32_t lyricTimeMs);

}  // namespace lyrics
