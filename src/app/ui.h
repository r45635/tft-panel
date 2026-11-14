// Main screens: setup (QR codes for the soft AP), status, and now playing with lyrics.
#pragma once

#include <Arduino.h>

namespace ui {

void openLibrary(bool on);     // console: the library screen (HAS_LIBRARY boards)
void findPlaying(bool album, const String &name = String());   // console: an artist/album, playing one if empty

void show();   // builds the screens and loads the main one
void tick();   // call from loop() before lv_timer_handler()

// Freezes the screen on invented lyrics (long lines, accents, punctuation) for layout
// checks and documentation screenshots without real, copyrighted lyrics. false = live.
void setDemo(bool on);
void demoSeek(int32_t positionMs);   // showcase position (selects the lyric line)
void demoManualSlide(bool on);       // next line change waits for demoSlide() frames
void demoSlide(int percent);         // 0..100 % of the current slide, for animation frames
// Opens one showcase screen: lyrics, cover, controls, settings, colours, volume, offset, zones,
// clock, update, confirm, progress, setup, code; "none" closes them. false if unknown / not in demo.
bool showcase(const String &what);

// "on", "dimmed" or "off" (backlight, for the console status).
const char *screenState();

// Shows the lyrics view (false) or the cover view (true) and saves it as the chosen view,
// like a swipe. viewName(): the view on screen, "lyrics", "cover" or "clock".
void setView(const String &name);
const char *viewName();

}  // namespace ui
