// Run-time TTF fonts (Noto Sans, Latin + Latin Extended-A, rendered by LVGL tiny_ttf).
// LVGL symbols (LV_SYMBOL_*) fall back to the built-in Montserrat.
#pragma once

#include <lvgl.h>

namespace fonts {

bool begin();   // false if a font could not be created (falls back to Montserrat)

// Sizes are given on the 800 x 480 design grid and scaled to the screen (layout::S).
extern const lv_font_t *lyricsCurrent;   // 44 px semibold
extern const lv_font_t *lyricsOther;     // 30 px regular
extern const lv_font_t *ui20;
extern const lv_font_t *ui16;
extern const lv_font_t *clock;           // 128 px semibold, digits for the idle clock
extern const lv_font_t *icons;           // LVGL symbols for buttons (Montserrat 28, or 36 when scaled)

}  // namespace fonts
