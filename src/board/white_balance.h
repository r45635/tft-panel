// Colour warmth shared by the board layers: red stays, green and blue are scaled on every
// pixel on its way to the panel, through one table on the 11 G+B bits of RGB565 (4 KB).
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace board::wb {

// 0..100 %: 0 = the panel's own white, 100 = blue -45 %, green -12 %. Returns true when active.
bool set(uint8_t percent);
bool on();
void apply(uint16_t *p, size_t n);   // in place; p 4-byte aligned (LVGL draw buffers are)
uint16_t colour(uint16_t c);         // one pixel, for colours computed outside LVGL

}  // namespace board::wb
