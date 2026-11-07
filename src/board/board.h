// Board bring-up for the CrowPanel 7" V3.0: I2C, PCA9557 touch reset, RGB panel
// (esp_lcd with bounce buffers), LVGL display + touch input, backlight.
#pragma once

#include <Arduino.h>
#include <lvgl.h>

namespace board {

struct Info {
    uint32_t psramBytes = 0;
    uint32_t flashBytes = 0;
    bool pca9557 = false;
    bool touch = false;
    uint8_t touchAddr = 0;
    char touchId[5] = {0};
    uint16_t touchFw = 0, touchXRes = 0, touchYRes = 0;
    String i2cDevices;     // e.g. "0x18 0x5D"
    String error;          // empty when everything came up
};

// Call first in setup(). After a CPU-only reset (esp_restart after an OTA, rollback, panic,
// watchdog) the LCD_CAM/GDMA and I2C peripherals keep running from before: the image comes up
// shifted and touch is dead until a real reset. This does one full system reset in that case.
// Exception: a freshly installed image that has not been confirmed yet ("pending verify")
// must not be reset again or the bootloader rolls it back; then the reset is deferred and
// cleanResetDeferred() is true until the app confirms the image and calls restart().
void cleanResetIfNeeded();
bool cleanResetDeferred();

// The only way to restart on purpose: a full system reset (clean display and touch), marked
// so that the next boot does not reset again.
[[noreturn]] void restart();

// Initialises everything except the backlight. Returns false on a fatal error
// (no PSRAM, panel not created); `info().error` says why. Touch failure is not
// fatal: the screen still works and the error is reported.
bool begin();
const Info &info();

lv_display_t *display();

// Last raw touch sample (for the bring-up screen).
bool lastTouch(uint16_t &x, uint16_t &y);

// The RGB panel's PSRAM framebuffer (RGB565, LCD_H_RES x LCD_V_RES), for screenshots.
const uint16_t *frameBuffer();

// Re-runs the touch reset while the panel and backlight are on (the factory firmware's
// order), plus variants, printing expander state and I2C scans. Attaches the touch
// input if the GT911 answers. Call from the LVGL thread.
void touchDiag();

// Cumulative flush count and time (µs) spent copying into the framebuffer, for profiling.
void flushStats(uint32_t &count, uint32_t &us);

// Static screen for firmware updates: while flash is written, PSRAM stalls and a refill from
// the framebuffer would jerk the image, so the panel shows what is on screen now, kept in
// internal RAM: text rows [textY0, textY1) as a 1-bit mask (sampled colours), plus a progress
// bar drawn live in [barX0, barX1) x [barY0, barY1) with `barFg` (RGB565, before white balance).
struct StaticScreen {
    int textY0, textY1, barX0, barX1, barY0, barY1;
    uint16_t barFg;
};
void staticScreenBegin(const StaticScreen &s);   // call after the screen has been drawn
void staticScreenProgress(int percent);
void staticScreenEnd();
bool staticScreenActive();

// Colour warmth 0..100 %: 0 = the panel's own (cool, bluish) white, 100 = blue -45 %.
void setWarmth(uint8_t percent);

// 0..100 %. 0 turns the backlight off.
void setBacklight(uint8_t percent);

}  // namespace board
