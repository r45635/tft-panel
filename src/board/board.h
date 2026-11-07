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

// 0..100 %. 0 turns the backlight off.
void setBacklight(uint8_t percent);

}  // namespace board
