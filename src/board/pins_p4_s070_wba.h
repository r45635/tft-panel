// Pin map and panel constants of the VIEWE 7" 1024x600 ESP32-P4 board, sold as
// UEP4S070H1024V600C-WB-C and calling itself ESP32_P4_S070_WBA in its own firmware.
// ESP32-P4 rev 3.2, 16 MB flash, 32 MB PSRAM, ESP32-C6 for Wi-Fi over SDIO, UE070WS panel glass
// behind a JD9165 controller, GT911 touch, serial through a CH340 (not the P4's native USB).
//
// Every value below comes from the vendor's own board support package, which is public:
// github.com/VIEWESMART/7-1024X600-ESP32-P4-C6-TOUCH-DISPLAY, under
// examples/esp-idf/11_7inch_lvgl_demo_v9/components/esp32_p4_s070_wba/ (MIT at the repository
// root, Apache-2.0 on the component). That demo is the firmware the board ships with, so these
// are the numbers it is known to work with, not an inference from a look-alike board.
//
// 🪤 **The panel has two supply rails switched by GPIOs** (1.8 V then 3.3 V, 30 ms apart). An
// afternoon was lost to not driving them: an unpowered panel acknowledges nothing, so DSI
// commands pile up in the host's FIFO until the twentieth call blocks forever, a panel-ID read
// never returns, and no video appears however right the timings are. Power first, always.
#pragma once

#include <stdint.h>

#include <hal/mipi_dsi_types.h>

namespace board {

constexpr const char *BOARD_NAME = "VIEWE ESP32-P4 7in";
// Firmware asset of this board in a GitHub release: tft-panel-p4-s070-wba-<tag>.bin
constexpr const char *BOARD_ASSET = "tft-panel-p4-s070-wba";
// PSRAM has its own bus on the P4: flash writes do not disturb the display.
constexpr bool HAS_STATIC_SCREEN = false;
// Colour warmth out of the box: none until someone has looked at this panel's white.
constexpr uint8_t DEFAULT_WARMTH = 0;
// Flash writes stall the SDIO link to the Wi-Fi chip, so an update is received whole first.
constexpr bool OTA_DOWNLOAD_FIRST = true;
// Music library browsing (search, browse, play) from the panel.
constexpr bool HAS_LIBRARY = true;

// ---- Panel supply: raised before anything else touches the display ----
constexpr int PANEL_POWER_GPIO_1 = 4;     // LCM 1.8 V
constexpr int PANEL_POWER_GPIO_2 = 5;     // LCD 3.3 V
constexpr int PANEL_POWER_STEP_MS = 30;   // between the two rails

// ---- MIPI-DSI LCD, 1024x600, JD9165, 2 lanes ----
constexpr int LCD_H_RES = 1024;
constexpr int LCD_V_RES = 600;
constexpr int DSI_LANES = 2;
constexpr int DSI_LANE_MBPS = 600;
// The driver's own default reference. 🪤 Asking for XTAL instead, with 1000 Mbps, stops the video
// stream from ever starting: esp_lcd_panel_init never returns.
constexpr mipi_dsi_phy_clock_source_t DSI_PHY_CLK_SRC = (mipi_dsi_phy_clock_source_t)0;
constexpr int PANEL_PHY_POWER_MS = 0;      // the rails above already settled
constexpr int DSI_PHY_LDO_CHAN = 3;        // LDO_VO3 powers the MIPI D-PHY
constexpr int DSI_PHY_LDO_MV = 2500;
// This panel is mounted the right way up, unlike the Waveshare's.
constexpr bool PANEL_UPSIDE_DOWN = false;
// The frame buffer is fed through the 2D DMA, as the vendor configures it.
constexpr bool PANEL_USE_DMA2D = true;

// Espressif's reference configuration for a 1024x600 JD9165 (esp_lcd_jd9165 1.0.4,
// JD9165_1024_600_PANEL_60HZ_DPI_CONFIG), the component and version the vendor's demo pins.
constexpr int DPI_CLOCK_MHZ = 50;
constexpr int LCD_HSYNC_PULSE_WIDTH = 20, LCD_HSYNC_BACK_PORCH = 136, LCD_HSYNC_FRONT_PORCH = 160;
constexpr int LCD_VSYNC_PULSE_WIDTH = 2, LCD_VSYNC_BACK_PORCH = 12, LCD_VSYNC_FRONT_PORCH = 20;

// Reset as esp_lcd_jd9165 pulses it: high, low, high, then 120 ms before any command.
constexpr int LCD_PIN_RESET = 22;
constexpr int PANEL_RESET_HIGH_MS = 5;
constexpr int PANEL_RESET_LOW_MS = 10;
constexpr int PANEL_RESET_WAIT_MS = 120;
// The table carries its own waits and ends with sleep out and display on, so the board layer
// adds neither, and sends the table before starting the video, as the vendor's driver does.
constexpr bool PANEL_INIT_AFTER_DPI_START = false;
constexpr bool PANEL_INIT_SENDS_SLEEP_OUT = true;
constexpr bool PANEL_NEEDS_DISPLAY_ON = false;
constexpr int PANEL_SLEEP_OUT_MS = 0;

// Vendor init table, {command, data length, delay after in ms, data bytes...} repeated.
// Transcribed from lcd_init_cmds[] in the vendor BSP's esp32_p4_s070_wba.c. MADCTL (0x36 = 0x00,
// RGB order, no flip) comes first, as esp_lcd_jd9165 sends it before the vendor sequence.
constexpr uint8_t PANEL_INIT[] = {
    0x36, 1, 0, 0x00,
    0x30, 1, 0, 0x00, 0xF7, 4, 0, 0x49, 0x61, 0x02, 0x00, 0x30, 1, 0, 0x01, 0x04, 1, 0, 0x0C,
    0x05, 1, 0, 0x08, 0x0B, 1, 0, 0x11, 0x20, 1, 0, 0x04, 0x1F, 1, 0, 0x00, 0x23, 1, 0, 0x38,
    0x28, 1, 0, 0x18, 0x29, 1, 0, 0x29, 0x2A, 1, 0, 0x01, 0x2B, 1, 0, 0x29, 0x2C, 1, 0, 0x01,
    0x30, 1, 0, 0x02, 0x00, 1, 0, 0x05, 0x01, 1, 0, 0x22, 0x02, 1, 0, 0x08, 0x03, 1, 0, 0x12,
    0x04, 1, 0, 0x16, 0x05, 1, 0, 0x64, 0x06, 1, 0, 0x00, 0x07, 1, 0, 0x00, 0x08, 1, 0, 0x78,
    0x09, 1, 0, 0x00, 0x0A, 1, 0, 0x04,
    0x0B, 11, 0, 0x16, 0x17, 0x0B, 0x0D, 0x0D, 0x0D, 0x11, 0x10, 0x07, 0x07, 0x09,
    0x0C, 11, 0, 0x09, 0x1E, 0x1E, 0x1C, 0x1C, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D,
    0x0D, 11, 0, 0x0A, 0x05, 0x0B, 0x0D, 0x0D, 0x0D, 0x11, 0x10, 0x06, 0x06, 0x08,
    0x0E, 11, 0, 0x08, 0x1F, 0x1F, 0x1D, 0x1D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D,
    0x0F, 11, 0, 0x0A, 0x05, 0x0D, 0x0B, 0x0D, 0x0D, 0x11, 0x10, 0x1D, 0x1D, 0x1F,
    0x10, 11, 0, 0x1F, 0x08, 0x08, 0x06, 0x06, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D,
    0x11, 11, 0, 0x16, 0x17, 0x0D, 0x0B, 0x0D, 0x0D, 0x11, 0x10, 0x1C, 0x1C, 0x1E,
    0x12, 11, 0, 0x1E, 0x09, 0x09, 0x07, 0x07, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D,
    0x13, 4, 0, 0x00, 0x00, 0x00, 0x00, 0x14, 4, 0, 0x00, 0x00, 0x41, 0x41,
    0x15, 4, 0, 0x00, 0x00, 0x00, 0x00, 0x17, 1, 0, 0x00, 0x18, 1, 0, 0x85, 0x19, 2, 0, 0x06, 0x09,
    0x1A, 2, 0, 0x05, 0x08, 0x1B, 2, 0, 0x0A, 0x04, 0x26, 1, 0, 0x00, 0x27, 1, 0, 0x00, 0x30, 1, 0, 0x06,
    0x12, 14, 0, 0x3F, 0x26, 0x27, 0x35, 0x2D, 0x34, 0x3F, 0x3F, 0x3F, 0x35, 0x2A, 0x20, 0x16, 0x08,
    0x13, 14, 0, 0x3F, 0x26, 0x28, 0x35, 0x27, 0x29, 0x29, 0x2F, 0x35, 0x2F, 0x26, 0x20, 0x16, 0x08,
    0x30, 1, 0, 0x0A, 0x02, 1, 0, 0x4F, 0x0B, 1, 0, 0x40, 0x30, 1, 0, 0x0D, 0x0D, 1, 0, 0x04,
    0x10, 1, 0, 0x0C, 0x11, 1, 0, 0x0C, 0x12, 1, 0, 0x0C, 0x13, 1, 0, 0x0C, 0x30, 1, 0, 0x00, 0x11, 0, 120,
    0x29, 0, 20,
};

// Backlight: LEDC on GPIO 23, active high (the Waveshare's is active low).
constexpr int LCD_PIN_BACKLIGHT = 23;
constexpr bool LCD_BACKLIGHT_ACTIVE_LOW = false;

// ---- I2C bus: GT911 touch at 0x5D, verified on the board ----
constexpr int I2C_PIN_SDA = 7;
constexpr int I2C_PIN_SCL = 8;
constexpr uint32_t I2C_FREQ_HZ = 400000;
constexpr uint8_t GT911_ADDR = 0x5D;
constexpr uint8_t GT911_ADDR_ALT = 0x14;

// ---- ESP32-C6 (Wi-Fi) over SDIO: Arduino's esp_hosted defaults, which this board matches ----
// CLK 18, CMD 19, D0..D3 14..17, C6 reset 54. Verified: a scan works, and the firmware's own
// co-processor update brought it to 2.12.13.

}  // namespace board
