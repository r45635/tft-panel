// Pin map of the Waveshare ESP32-P4-WIFI6-Touch-LCD-7B (ESP32-P4 rev 3, 32 MB flash, 32 MB PSRAM,
// ESP32-C6 for Wi-Fi over SDIO). Source: Waveshare's BSP waveshare/esp32_p4_wifi6_touch_lcd_7b
// 3.0.1 (Apache-2.0) and the bring-up on the board. See docs/HARDWARE.md.
#pragma once

#include <stdint.h>

#include <hal/mipi_dsi_types.h>

namespace board {

constexpr const char *BOARD_NAME = "Waveshare ESP32-P4 7B";
// Firmware asset of this board in a GitHub release: tft-panel-waveshare-p4-7b-<tag>.bin
constexpr const char *BOARD_ASSET = "tft-panel-waveshare-p4-7b";
// PSRAM has its own bus on the P4: flash writes do not disturb the display.
constexpr bool HAS_STATIC_SCREEN = false;
// Colour warmth out of the box: none until someone has looked at this panel's white.
constexpr uint8_t DEFAULT_WARMTH = 0;
// Firmware updates are received whole into PSRAM, then written to flash: flash writes stall the
// SDIO link to the Wi-Fi chip, which then stops answering (a streamed download died at 240 KB).
constexpr bool OTA_DOWNLOAD_FIRST = true;
// Music library browsing (search, browse, play) from the panel.
constexpr bool HAS_LIBRARY = true;

// ---- MIPI-DSI LCD (1024x600, EK79007, 2 lanes), mounted upside down: the image is rotated
// 180 degrees on its way to the panel (the controller's MADCTL flip has no effect in video mode).
constexpr int LCD_H_RES = 1024;
constexpr int LCD_V_RES = 600;
constexpr int DSI_LANES = 2;
constexpr int DSI_LANE_MBPS = 1000;
// Brought up with the driver's legacy default reference; left as it is until this board can be
// retested (see the note on P4 rev 3.0 and newer in pins_p4_s070_wba.h).
constexpr mipi_dsi_phy_clock_source_t DSI_PHY_CLK_SRC = (mipi_dsi_phy_clock_source_t)0;
// No GPIO-switched panel rails on this board: it powers its panel on its own.
constexpr int PANEL_POWER_GPIO_1 = -1;
constexpr int PANEL_POWER_GPIO_2 = -1;
constexpr int PANEL_POWER_STEP_MS = 0;
constexpr int PANEL_PHY_POWER_MS = 0;   // settling time after the PHY supply comes up
constexpr int DSI_PHY_LDO_CHAN = 3;        // LDO_VO3 powers the MIPI D-PHY
constexpr int DSI_PHY_LDO_MV = 2500;
constexpr int DPI_CLOCK_MHZ = 52;
constexpr int LCD_HSYNC_PULSE_WIDTH = 10, LCD_HSYNC_BACK_PORCH = 160, LCD_HSYNC_FRONT_PORCH = 160;
constexpr int LCD_VSYNC_PULSE_WIDTH = 1, LCD_VSYNC_BACK_PORCH = 23, LCD_VSYNC_FRONT_PORCH = 12;
constexpr int LCD_PIN_RESET = 33;
constexpr int LCD_PIN_BACKLIGHT = 32;      // PWM, active low
constexpr bool LCD_BACKLIGHT_ACTIVE_LOW = true;
// Mounted upside down: the image is rotated on its way to the panel, because the controller's
// MADCTL flip has no effect in video mode.
constexpr bool PANEL_UPSIDE_DOWN = true;
// Brought up without the 2D DMA path; left as it is.
constexpr bool PANEL_USE_DMA2D = false;

// Vendor init table, {command, data length, delay after in ms, data bytes...} repeated: two-lane
// mode then the sequence of esp_lcd_ek79007 2.0.2 (Apache-2.0). Sleep out is sent by the board
// layer, and no entry needs a delay of its own.
constexpr int PANEL_RESET_HIGH_MS = 0;   // this one needs no high pulse before the low one
constexpr int PANEL_RESET_LOW_MS = 10;
constexpr int PANEL_RESET_WAIT_MS = 20;
// Brought up sending its nine commands before the video stream started; left as it was.
constexpr bool PANEL_INIT_AFTER_DPI_START = false;
constexpr bool PANEL_INIT_SENDS_SLEEP_OUT = false;
// The EK79007 comes up displaying; it is never told to turn its display on.
constexpr bool PANEL_NEEDS_DISPLAY_ON = false;
constexpr int PANEL_SLEEP_OUT_MS = 120;
constexpr uint8_t PANEL_INIT[] = {
    0xB2, 1, 0, 0x10, 0x80, 1, 0, 0x8B, 0x81, 1, 0, 0x78, 0x82, 1, 0, 0x84, 0x83, 1, 0, 0x88,
    0x84, 1, 0, 0xA8, 0x85, 1, 0, 0xE3, 0x86, 1, 0, 0x88,
};

// ---- I2C bus: GT911 touch (0x5D), ES8311 codec (0x18), ES7210 mic ADC (0x40) ----
constexpr int I2C_PIN_SDA = 7;
constexpr int I2C_PIN_SCL = 8;
constexpr uint32_t I2C_FREQ_HZ = 400000;
constexpr uint8_t GT911_ADDR = 0x5D;       // touch RST/INT not wired: the address is fixed
constexpr uint8_t GT911_ADDR_ALT = 0x14;

// ---- ESP32-C6 (Wi-Fi) over SDIO: Arduino's esp_hosted defaults, which match this board ----
// CLK 18, CMD 19, D0..D3 14..17, C6 reset 54.

}  // namespace board
