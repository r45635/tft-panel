// Run-time TTF fonts (Noto Sans, Latin + Latin Extended-A, rendered by LVGL tiny_ttf).
// LVGL symbols (LV_SYMBOL_*) fall back to the built-in Montserrat.
#pragma once

#include <lvgl.h>

namespace fonts {

bool begin();   // false if a font could not be created (falls back to Montserrat)

extern const lv_font_t *lyricsCurrent;   // 44 px semibold
extern const lv_font_t *lyricsOther;     // 30 px regular
extern const lv_font_t *ui20;
extern const lv_font_t *ui16;
extern const lv_font_t *clock;           // 128 px semibold, digits for the idle clock

}  // namespace fonts
