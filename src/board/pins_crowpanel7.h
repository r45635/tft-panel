// Pin map of the Elecrow CrowPanel ESP32 Display 7.0" (DIS08070H), board V3.0.
// Source: Elecrow "CrowPanel ESP32 7.0-inch V3.0 Product Hardware Driver Guide"
// (Eagle_SCH&PCB/V3.0/readme.md in Elecrow-RD/CrowPanel-7.0-HMI-ESP32-Display-800x480)
// and the V3.0 schematic dated 2024-04-20. See docs/HARDWARE.md.
#pragma once

#include <stdint.h>

namespace board {

constexpr const char *BOARD_NAME = "Elecrow CrowPanel 7\" V3.0";
// Firmware asset of this board in a GitHub release: tft-panel-<tag>.bin (the first board kept the plain name).
constexpr const char *BOARD_ASSET = "tft-panel";
constexpr bool HAS_STATIC_SCREEN = true;   // OTA: freeze the image in internal RAM (see board.h)
// Colour warmth out of the box: this panel's white is bluish (Vincent settled on 76 %).
constexpr uint8_t DEFAULT_WARMTH = 75;

// ---- RGB LCD (800x480, RGB565, EK9716BD3 + EK73002ACGB) ----
constexpr int LCD_H_RES = 800;
constexpr int LCD_V_RES = 480;

// esp_lcd data order: index 0 = LSB of RGB565 = B0 ... index 15 = R4.
constexpr int8_t LCD_DATA_PINS[16] = {
    15, 7, 6, 5, 4,       // B0..B4
    9, 46, 3, 8, 16, 1,   // G0..G5
    14, 21, 47, 48, 45,   // R0..R4
};
constexpr int LCD_PIN_DE = 41;
constexpr int LCD_PIN_VSYNC = 40;
constexpr int LCD_PIN_HSYNC = 39;
constexpr int LCD_PIN_PCLK = 0;   // also BOOT strapping pin — never press BOOT at runtime

// Timings from Elecrow's verified config (falling-edge sampling, active-low syncs).
constexpr int LCD_HSYNC_FRONT_PORCH = 40;
constexpr int LCD_HSYNC_PULSE_WIDTH = 48;
constexpr int LCD_HSYNC_BACK_PORCH = 40;
constexpr int LCD_VSYNC_FRONT_PORCH = 1;
constexpr int LCD_VSYNC_PULSE_WIDTH = 31;
constexpr int LCD_VSYNC_BACK_PORCH = 13;

// Elecrow uses 15 MHz (old example) to 24 MHz (course code). Start conservative:
// PSRAM bandwidth is shared with Wi-Fi and LVGL rendering.
constexpr uint32_t LCD_PCLK_HZ = 16 * 1000 * 1000;

// Backlight: GPIO2 drives the MT9201 EN pin, active high, PWM-capable.
constexpr int LCD_PIN_BACKLIGHT = 2;

// ---- I2C bus (GT911 touch + PCA9557 expander + J4/J6 headers), 4.7k pull-ups ----
constexpr int I2C_PIN_SDA = 19;
constexpr int I2C_PIN_SCL = 20;
constexpr uint32_t I2C_FREQ_HZ = 400000;

constexpr uint8_t PCA9557_ADDR = 0x18;   // A0..A2 grounded (library comment saying 0x41 is wrong)
constexpr uint8_t PCA9557_IO_TP_RESET = 0;
constexpr uint8_t PCA9557_IO_TP_INT = 1;

constexpr uint8_t GT911_ADDR = 0x5D;     // INT held low during reset selects 0x5D
constexpr uint8_t GT911_ADDR_ALT = 0x14;

// ---- Others (not used yet) ----
constexpr int SD_PIN_CS = 10, SD_PIN_MOSI = 11, SD_PIN_SCK = 12, SD_PIN_MISO = 13;
constexpr int I2S_PIN_DOUT = 17, I2S_PIN_BCLK = 42, I2S_PIN_LRCLK = 18;   // NS4168 amp
constexpr int GPIO_D = 38;   // J4/J7 header; drive low at boot

}  // namespace board
