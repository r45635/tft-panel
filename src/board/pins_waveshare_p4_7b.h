// Pin map of the Waveshare ESP32-P4-WIFI6-Touch-LCD-7B (ESP32-P4 rev 3, 32 MB flash, 32 MB PSRAM,
// ESP32-C6 for Wi-Fi over SDIO). Source: Waveshare's BSP waveshare/esp32_p4_wifi6_touch_lcd_7b
// 3.0.1 (Apache-2.0) and the bring-up on the board. See docs/HARDWARE.md.
#pragma once

#include <stdint.h>

namespace board {

constexpr const char *BOARD_NAME = "Waveshare ESP32-P4 7B";
// Firmware asset of this board in a GitHub release: tft-panel-waveshare-p4-7b-<tag>.bin
constexpr const char *BOARD_ASSET = "tft-panel-waveshare-p4-7b";
// PSRAM has its own bus on the P4: flash writes do not disturb the display.
constexpr bool HAS_STATIC_SCREEN = false;
// Colour warmth out of the box: none until someone has looked at this panel's white.
constexpr uint8_t DEFAULT_WARMTH = 0;

// ---- MIPI-DSI LCD (1024x600, EK79007, 2 lanes), mounted upside down: the image is rotated
// 180 degrees on its way to the panel (the controller's MADCTL flip has no effect in video mode).
constexpr int LCD_H_RES = 1024;
constexpr int LCD_V_RES = 600;
constexpr int DSI_LANES = 2;
constexpr int DSI_LANE_MBPS = 1000;
constexpr int DSI_PHY_LDO_CHAN = 3;        // LDO_VO3 powers the MIPI D-PHY
constexpr int DSI_PHY_LDO_MV = 2500;
constexpr int DPI_CLOCK_MHZ = 52;
constexpr int LCD_HSYNC_PULSE_WIDTH = 10, LCD_HSYNC_BACK_PORCH = 160, LCD_HSYNC_FRONT_PORCH = 160;
constexpr int LCD_VSYNC_PULSE_WIDTH = 1, LCD_VSYNC_BACK_PORCH = 23, LCD_VSYNC_FRONT_PORCH = 12;
constexpr int LCD_PIN_RESET = 33;
constexpr int LCD_PIN_BACKLIGHT = 32;      // PWM, active low

// ---- I2C bus: GT911 touch (0x5D), ES8311 codec (0x18), ES7210 mic ADC (0x40) ----
constexpr int I2C_PIN_SDA = 7;
constexpr int I2C_PIN_SCL = 8;
constexpr uint32_t I2C_FREQ_HZ = 400000;
constexpr uint8_t GT911_ADDR = 0x5D;       // touch RST/INT not wired: the address is fixed
constexpr uint8_t GT911_ADDR_ALT = 0x14;

// ---- ESP32-C6 (Wi-Fi) over SDIO: Arduino's esp_hosted defaults, which match this board ----
// CLK 18, CMD 19, D0..D3 14..17, C6 reset 54.

}  // namespace board
