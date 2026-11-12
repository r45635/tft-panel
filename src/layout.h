// Screen geometry. The interface is designed on an 800 x 480 grid and scaled to the screen of
// the board the firmware is built for (identity on the CrowPanel, x1.28 / x1.25 on 1024 x 600):
//   X()  horizontal positions and widths      Y()  vertical positions and heights
//   S()  sizes that keep their proportions: fonts, icons, square buttons, covers, radii, gaps
// Full-screen objects use SCREEN_W / SCREEN_H, not X(800) / Y(480), so rounding never leaves a seam.
#pragma once

#include <stdint.h>

#include "board/board_pins.h"

namespace layout {

constexpr int32_t SCREEN_W = board::LCD_H_RES;
constexpr int32_t SCREEN_H = board::LCD_V_RES;
constexpr int32_t DESIGN_W = 800, DESIGN_H = 480;

constexpr int32_t X(int32_t v) { return (v * SCREEN_W + DESIGN_W / 2) / DESIGN_W; }
constexpr int32_t Y(int32_t v) { return (v * SCREEN_H + DESIGN_H / 2) / DESIGN_H; }
// The smaller of the two ratios, so a scaled square always fits where it did on 800 x 480.
constexpr int32_t S(int32_t v) {
    return SCREEN_W * DESIGN_H < SCREEN_H * DESIGN_W ? X(v) : Y(v);
}

}  // namespace layout
