// Board layer of the Elecrow CrowPanel 7" V3.0 (ESP32-S3, RGB panel).
#include "board.h"

#if defined(BOARD_CROWPANEL7)

#include <esp32s3/rom/cache.h>
#include <algorithm>
#include <esp_system.h>
#include <esp_timer.h>

#include <Wire.h>
#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>

#include <cstring>

#include "board_pins.h"
#include "gt911.h"
#include "pca9557.h"
#include "white_balance.h"

namespace board {
namespace {

Info g_info;
Pca9557 g_pca(Wire, PCA9557_ADDR);
Gt911 g_touch(Wire);
esp_lcd_panel_handle_t g_panel = nullptr;
lv_display_t *g_disp = nullptr;
lv_indev_t *g_indev = nullptr;

volatile bool g_touchDown = false;
volatile uint16_t g_touchX = 0, g_touchY = 0;

// LVGL renders into one internal-RAM strip; flush copies it into the PSRAM framebuffer
// owned by the RGB driver. The flush is a synchronous CPU copy, so a second strip would
// buy no overlap: one 48-line strip (same RAM as 2 x 24) halves the passes over the text,
// which dominate a full-stage redraw (profiled: ~105 ms render vs ~30 ms flush at 24 lines).
constexpr int DRAW_BUF_LINES = 48;

// Bounce buffers: the RGB DMA reads from internal SRAM, refilled from the PSRAM
// framebuffer by the CPU. Avoids the image drifting when Wi-Fi/flash load PSRAM.
// Must divide 800*480: 20 lines = 16000 px → 24 refills per frame (2 x 32 KB internal RAM).
// 10 lines left too little slack while LVGL flushes into PSRAM: stray dots near redrawn labels.
constexpr int BOUNCE_BUF_PX = LCD_H_RES * 20;

constexpr int BACKLIGHT_PWM_HZ = 5000;
constexpr int BACKLIGHT_PWM_BITS = 8;

void scanI2c() {
    g_info.i2cDevices = "";
    for (uint8_t a = 0x08; a < 0x78; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) {
            char buf[6];
            snprintf(buf, sizeof buf, "0x%02X", a);
            if (g_info.i2cDevices.length()) g_info.i2cDevices += ' ';
            g_info.i2cDevices += buf;
        }
    }
}

// Elecrow's working V3.0 sequence: all expander pins outputs latched high
// (IO2 LCD-RESET and IO3 LCD-POWER stay released/on), then pulse TP-RESET (IO0)
// with TP-INT (IO1) low so the GT911 selects address 0x5D, then release INT.
bool resetTouchViaExpander() {
    if (!g_pca.present()) return false;
    bool ok = g_pca.writeReg(Pca9557::REG_POLARITY, 0x00);
    ok &= g_pca.setOutputs(0xFF);
    ok &= g_pca.setDirections(0x00);
    ok &= g_pca.setPin(PCA9557_IO_TP_RESET, false);
    ok &= g_pca.setPin(PCA9557_IO_TP_INT, false);
    delay(20);
    ok &= g_pca.setPin(PCA9557_IO_TP_RESET, true);
    delay(100);
    ok &= g_pca.setPinInput(PCA9557_IO_TP_INT, true);
    return ok;
}

// ---- Framebuffer and bounce-buffer fill ----
//
// Flash and PSRAM share the MSPI bus: while flash is erased or written (an update writes 1.8 MB),
// PSRAM reads stall for milliseconds and a refill from the framebuffer comes too late: the image
// jerks. So during an update the panel shows a static screen generated here from internal RAM
// only: background, the message as a 1-bit mask taken from the framebuffer, and a progress bar.
uint16_t *g_fb = nullptr;
constexpr int FB_PX = LCD_H_RES * LCD_V_RES;

struct Static {
    volatile bool on = false;
    uint8_t *mask = nullptr;   // text rows, 1 bit per pixel
    int textY0 = 0, textY1 = 0, barX0 = 0, barX1 = 0, barY0 = 0, barY1 = 0;
    uint16_t bg = 0, fg = 0, barBg = 0, barFg = 0;
    volatile int barFillX = 0;
} g_static;

IRAM_ATTR bool bounceFill(esp_lcd_panel_handle_t, void *buf, int pos_px, int len_bytes, void *) {
    if (!g_static.on) {
        memcpy(buf, g_fb + pos_px, len_bytes);
        int next = pos_px + len_bytes / 2;
        if (next >= FB_PX) next = 0;
        Cache_Start_DCache_Preload((uint32_t)(g_fb + next), len_bytes, 0);   // as the driver does
        return false;
    }
    uint16_t *o = (uint16_t *)buf;
    const int y0 = pos_px / LCD_H_RES, rows = len_bytes / 2 / LCD_H_RES;
    for (int r = 0; r < rows; r++, o += LCD_H_RES) {
        int y = y0 + r;
        if (y >= g_static.textY0 && y < g_static.textY1 && g_static.mask) {
            const uint8_t *m = g_static.mask + (size_t)(y - g_static.textY0) * (LCD_H_RES / 8);
            for (int x = 0; x < LCD_H_RES; x++) o[x] = (m[x >> 3] >> (7 - (x & 7))) & 1 ? g_static.fg : g_static.bg;
        } else if (y >= g_static.barY0 && y < g_static.barY1) {
            int fill = g_static.barFillX;
            for (int x = 0; x < LCD_H_RES; x++)
                o[x] = x < g_static.barX0 || x >= g_static.barX1 ? g_static.bg : x < fill ? g_static.barFg : g_static.barBg;
        } else {
            for (int x = 0; x < LCD_H_RES; x++) o[x] = g_static.bg;
        }
    }
    return false;
}

bool createPanel() {
    esp_lcd_rgb_panel_config_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.clk_src = LCD_CLK_SRC_DEFAULT;
    cfg.timings.pclk_hz = LCD_PCLK_HZ;
    cfg.timings.h_res = LCD_H_RES;
    cfg.timings.v_res = LCD_V_RES;
    cfg.timings.hsync_pulse_width = LCD_HSYNC_PULSE_WIDTH;
    cfg.timings.hsync_back_porch = LCD_HSYNC_BACK_PORCH;
    cfg.timings.hsync_front_porch = LCD_HSYNC_FRONT_PORCH;
    cfg.timings.vsync_pulse_width = LCD_VSYNC_PULSE_WIDTH;
    cfg.timings.vsync_back_porch = LCD_VSYNC_BACK_PORCH;
    cfg.timings.vsync_front_porch = LCD_VSYNC_FRONT_PORCH;
    cfg.timings.flags.pclk_active_neg = 1;
    cfg.data_width = 16;
    cfg.bits_per_pixel = 16;
    // We own the framebuffer and fill the bounce buffers ourselves (on_bounce_empty): normally a
    // copy from it, during an update a static screen generated from internal RAM (see below).
    cfg.num_fbs = 0;
    cfg.flags.no_fb = 1;
    cfg.bounce_buffer_size_px = BOUNCE_BUF_PX;
    cfg.dma_burst_size = 64;
    cfg.hsync_gpio_num = LCD_PIN_HSYNC;
    cfg.vsync_gpio_num = LCD_PIN_VSYNC;
    cfg.de_gpio_num = LCD_PIN_DE;
    cfg.pclk_gpio_num = LCD_PIN_PCLK;
    cfg.disp_gpio_num = -1;
    for (int i = 0; i < 16; i++) cfg.data_gpio_nums[i] = LCD_DATA_PINS[i];
    g_fb = (uint16_t *)heap_caps_aligned_calloc(64, 1, (size_t)LCD_H_RES * LCD_V_RES * 2,
                                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!g_fb) {
        g_info.error = "framebuffer: out of PSRAM";
        return false;
    }
    esp_err_t err = esp_lcd_new_rgb_panel(&cfg, &g_panel);
    if (err != ESP_OK) {
        g_info.error = String("esp_lcd_new_rgb_panel: ") + esp_err_to_name(err);
        return false;
    }
    esp_lcd_rgb_panel_event_callbacks_t cbs = {};
    cbs.on_bounce_empty = bounceFill;
    err = esp_lcd_rgb_panel_register_event_callbacks(g_panel, &cbs, nullptr);
    if (err != ESP_OK) {
        g_info.error = String("bounce callback: ") + esp_err_to_name(err);
        return false;
    }
    err = esp_lcd_panel_reset(g_panel);
    if (err == ESP_OK) err = esp_lcd_panel_init(g_panel);
    if (err != ESP_OK) {
        g_info.error = String("esp_lcd_panel_init: ") + esp_err_to_name(err);
        return false;
    }
    return true;
}

volatile uint32_t g_flushCount = 0, g_flushUs = 0;

void flushCb(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
    // Synchronous CPU copy into the PSRAM framebuffer (num_fbs = 1).
    int64_t t0 = esp_timer_get_time();
    wb::apply((uint16_t *)px, (size_t)lv_area_get_width(area) * lv_area_get_height(area));
    const int w = lv_area_get_width(area);
    const uint16_t *src = (const uint16_t *)px;
    for (int y = area->y1; y <= area->y2; y++, src += w) memcpy(g_fb + y * LCD_H_RES + area->x1, src, w * 2);
    g_flushUs += (uint32_t)(esp_timer_get_time() - t0);
    g_flushCount++;
    lv_display_flush_ready(disp);
}

void touchReadCb(lv_indev_t *, lv_indev_data_t *data) {
    TouchPoint p;
    int n = g_touch.read(p);
    if (n > 0) {
        // Raw GT911 coordinates match the panel orientation on this board
        // (Elecrow's TAMC rotation + map() cancel out). Verified on the bring-up screen.
        uint16_t x = p.x < LCD_H_RES ? p.x : LCD_H_RES - 1;
        uint16_t y = p.y < LCD_V_RES ? p.y : LCD_V_RES - 1;
        g_touchX = x;
        g_touchY = y;
        g_touchDown = true;
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        g_touchDown = false;
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

bool createLvgl() {
    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return millis(); });

    const size_t bufBytes = LCD_H_RES * DRAW_BUF_LINES * sizeof(uint16_t);
    void *buf1 = heap_caps_malloc(bufBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buf1) {
        g_info.error = "LVGL draw buffers: out of internal RAM";
        return false;
    }

    g_disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_color_format(g_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(g_disp, buf1, nullptr, bufBytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(g_disp, flushCb);

    if (g_info.touch) {
        g_indev = lv_indev_create();
        lv_indev_set_type(g_indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(g_indev, touchReadCb);
        lv_indev_set_display(g_indev, g_disp);
    }
    return true;
}

}  // namespace

bool begin() {
    // GPIO38 goes to the J4/J7 headers: keep it in a safe state.
    pinMode(GPIO_D, OUTPUT);
    digitalWrite(GPIO_D, LOW);

    // Backlight off until the first frame is in the framebuffer.
    ledcAttach(LCD_PIN_BACKLIGHT, BACKLIGHT_PWM_HZ, BACKLIGHT_PWM_BITS);
    ledcWrite(LCD_PIN_BACKLIGHT, 0);

    g_info.psramBytes = ESP.getPsramSize();
    uint32_t flash = 0;
    if (esp_flash_get_size(nullptr, &flash) == ESP_OK) g_info.flashBytes = flash;
    if (!psramFound() || g_info.psramBytes < 4 * 1024 * 1024) {
        g_info.error = "PSRAM missing — the RGB framebuffer needs octal PSRAM (memory_type = qio_opi)";
        return false;
    }

    Wire.begin(I2C_PIN_SDA, I2C_PIN_SCL, I2C_FREQ_HZ);
    g_info.pca9557 = resetTouchViaExpander();
    scanI2c();

    // Diagnostic: expander state after the sequence, then wait for the GT911.
    uint8_t pin = 0, pout = 0, pcfg = 0;
    g_pca.readReg(Pca9557::REG_INPUT, pin);
    g_pca.readReg(Pca9557::REG_OUTPUT, pout);
    g_pca.readReg(Pca9557::REG_CONFIG, pcfg);
    Serial.printf("[board] pca9557 in=0x%02X out=0x%02X cfg=0x%02X\n", pin, pout, pcfg);
#ifdef TOUCH_DIAG
    {
        auto rd = [](const char *tag) {
            uint8_t i = 0, o = 0, c = 0;
            g_pca.readReg(Pca9557::REG_INPUT, i);
            g_pca.readReg(Pca9557::REG_OUTPUT, o);
            g_pca.readReg(Pca9557::REG_CONFIG, c);
            delay(300);
            scanI2c();
            Serial.printf("[diag] %-28s in=0x%02X out=0x%02X cfg=0x%02X i2c=[%s]\n", tag, i, o, c,
                          g_info.i2cDevices.c_str());
        };
        g_pca.setDirections(0xFF); rd("all inputs (released)");
        // INT high during reset -> GT911 should pick 0x14
        g_pca.setOutputs(0xFF); g_pca.setDirections(0xFC);   // IO0, IO1 outputs, high
        g_pca.setPin(PCA9557_IO_TP_RESET, false); delay(20);
        g_pca.setPin(PCA9557_IO_TP_RESET, true); delay(100);
        g_pca.setPinInput(PCA9557_IO_TP_INT, true); rd("reset with INT high");
        // Long reset low then release, INT low (0x5D)
        g_pca.setOutputs(0xFC); g_pca.setDirections(0x00); delay(200);
        rd("IO0+IO1 held low");
        g_pca.setPin(PCA9557_IO_TP_RESET, true); delay(200);
        g_pca.setPinInput(PCA9557_IO_TP_INT, true); rd("released after 200ms");
        Wire.setClock(100000); rd("same at 100 kHz");
        Wire.setClock(I2C_FREQ_HZ);
    }
#endif
    bool found = false;
    for (int i = 0; i < 20 && !found; i++) {
        found = g_touch.begin(GT911_ADDR, GT911_ADDR_ALT);
        if (!found) delay(50);
        if (found || i % 5 == 4) {
            scanI2c();
            Serial.printf("[board] t+%dms i2c=[%s]\n", 100 + i * 50, g_info.i2cDevices.c_str());
        }
    }

    if (found) {
        g_info.touch = true;
        g_info.touchAddr = g_touch.address();
        if (!g_touch.readInfo(g_info.touchId, g_info.touchFw, g_info.touchXRes, g_info.touchYRes)) {
            g_info.error = "GT911 answered its address but not the info registers";
        }
    } else {
        g_info.error = g_info.pca9557 ? "GT911 not found on I2C (0x5D / 0x14)"
                                      : "PCA9557 (0x18) not found: touch reset not done";
    }

    if (!createPanel()) return false;
    if (!createLvgl()) return false;
    return true;
}

void touchDiag() {
    auto report = [](const char *tag) {
        uint8_t i = 0, o = 0, c = 0;
        g_pca.readReg(Pca9557::REG_INPUT, i);
        g_pca.readReg(Pca9557::REG_OUTPUT, o);
        g_pca.readReg(Pca9557::REG_CONFIG, c);
        scanI2c();
        Serial.printf("[touchdiag] %-34s in=0x%02X out=0x%02X cfg=0x%02X i2c=[%s]\n", tag, i, o, c,
                      g_info.i2cDevices.c_str());
    };
    auto waitForTouch = [&](const char *tag, uint32_t ms) {
        uint32_t t0 = millis();
        while (millis() - t0 < ms) {
            if (g_touch.begin(GT911_ADDR, GT911_ADDR_ALT)) {
                Serial.printf("[touchdiag] %s: GT911 answered at 0x%02X after %u ms\n", tag, g_touch.address(),
                              (unsigned)(millis() - t0));
                return true;
            }
            delay(20);
        }
        report(tag);
        return false;
    };

    // I2S pins low like the factory firmware (they idle on the amplifier).
    for (int pin : {I2S_PIN_DOUT, I2S_PIN_LRCLK, I2S_PIN_BCLK}) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
    }
    report("before (panel + backlight on)");

    bool found = false;
    for (uint32_t hz : {400000u, 100000u}) {
        Wire.setClock(hz);
        char tag[48];
        // 1. Factory order and timings: reset() → all outputs → IO0+IO1 low 20 ms → IO0 high → 100 ms → IO1 input.
        g_pca.setDirections(0xFF);
        g_pca.setOutputs(0xFF);
        g_pca.writeReg(Pca9557::REG_POLARITY, 0x00);
        g_pca.setDirections(0x00);
        g_pca.setPin(PCA9557_IO_TP_RESET, false);
        g_pca.setPin(PCA9557_IO_TP_INT, false);
        delay(20);
        g_pca.setPin(PCA9557_IO_TP_RESET, true);
        delay(100);
        g_pca.setPinInput(PCA9557_IO_TP_INT, true);
        snprintf(tag, sizeof tag, "factory sequence @%u kHz", (unsigned)(hz / 1000));
        if ((found = waitForTouch(tag, 3000))) break;

        // 2. Long reset (GT911 datasheet minimum is 100 us; some modules need far more).
        g_pca.setPin(PCA9557_IO_TP_INT, false);
        g_pca.setPinInput(PCA9557_IO_TP_INT, false);
        g_pca.setPin(PCA9557_IO_TP_RESET, false);
        delay(500);
        g_pca.setPin(PCA9557_IO_TP_RESET, true);
        delay(60);
        g_pca.setPinInput(PCA9557_IO_TP_INT, true);
        snprintf(tag, sizeof tag, "500 ms reset @%u kHz", (unsigned)(hz / 1000));
        if ((found = waitForTouch(tag, 3000))) break;

        // 3. Everything released (power-on state of the expander): GT911 free-running.
        g_pca.setDirections(0xFF);
        snprintf(tag, sizeof tag, "expander all inputs @%u kHz", (unsigned)(hz / 1000));
        if ((found = waitForTouch(tag, 3000))) break;
        g_pca.setOutputs(0xFF);
        g_pca.setDirections(0x00);
    }
    Wire.setClock(I2C_FREQ_HZ);
    // Leave the expander as at boot: all outputs high except TP-INT, an input.
    g_pca.setOutputs(0xFF);
    g_pca.setDirections(1u << PCA9557_IO_TP_INT);

    if (!found) {
        Serial.println("[touchdiag] RESULT: no GT911 on the bus in any variant");
        return;
    }
    g_info.touch = true;
    g_info.touchAddr = g_touch.address();
    g_touch.readInfo(g_info.touchId, g_info.touchFw, g_info.touchXRes, g_info.touchYRes);
    Serial.printf("[touchdiag] RESULT: GT911 id=\"%s\" fw=0x%04X res=%ux%u - touch input attached\n", g_info.touchId,
                  g_info.touchFw, g_info.touchXRes, g_info.touchYRes);
    if (!g_indev) {
        g_indev = lv_indev_create();
        lv_indev_set_type(g_indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(g_indev, touchReadCb);
        lv_indev_set_display(g_indev, g_disp);
    }
}

const Info &info() { return g_info; }

lv_display_t *display() { return g_disp; }

const uint16_t *frameBuffer() {
    return g_fb;
}

void flushStats(uint32_t &count, uint32_t &us) {
    count = g_flushCount;
    us = g_flushUs;
}

bool lastTouch(uint16_t &x, uint16_t &y) {
    x = g_touchX;
    y = g_touchY;
    return g_touchDown;
}

uint32_t luma(uint16_t c) { return ((c >> 11) & 0x1F) * 2 + ((c >> 5) & 0x3F) * 2 + (c & 0x1F); }

void staticScreenBegin(const StaticScreen &s) {
    if (g_static.on || !g_fb) return;
    int ty0 = std::max(0, s.textY0), ty1 = std::min(LCD_V_RES, s.textY1);
    size_t maskBytes = (size_t)(ty1 - ty0) * (LCD_H_RES / 8);
    uint8_t *mask = (uint8_t *)heap_caps_calloc(1, maskBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    // Colours sampled from what is on screen (already white-balanced).
    uint16_t bg = g_fb[(size_t)std::max(0, ty0 - 2) * LCD_H_RES + 2], fg = bg;
    uint32_t bgL = luma(bg), fgL = bgL;
    for (int y = ty0; y < ty1; y++)
        for (int x = 0; x < LCD_H_RES; x++) {
            uint16_t c = g_fb[(size_t)y * LCD_H_RES + x];
            if (luma(c) > fgL) {
                fgL = luma(c);
                fg = c;
            }
        }
    uint32_t threshold = (bgL + fgL) / 2;
    if (mask)
        for (int y = ty0; y < ty1; y++)
            for (int x = 0; x < LCD_H_RES; x++)
                if (luma(g_fb[(size_t)y * LCD_H_RES + x]) > threshold)
                    mask[(size_t)(y - ty0) * (LCD_H_RES / 8) + (x >> 3)] |= 0x80 >> (x & 7);
    g_static.mask = mask;
    g_static.textY0 = ty0;
    g_static.textY1 = ty1;
    g_static.barX0 = s.barX0;
    g_static.barX1 = s.barX1;
    g_static.barY0 = s.barY0;
    g_static.barY1 = s.barY1;
    g_static.bg = bg;
    g_static.fg = fg;
    g_static.barBg = g_fb[(size_t)((s.barY0 + s.barY1) / 2) * LCD_H_RES + s.barX1 - 2];
    g_static.barFg = wb::colour(s.barFg);
    g_static.barFillX = s.barX0;
    g_static.on = true;
}

void staticScreenProgress(int percent) {
    percent = percent < 0 ? 0 : percent > 100 ? 100 : percent;
    g_static.barFillX = g_static.barX0 + (g_static.barX1 - g_static.barX0) * percent / 100;
}

void staticScreenEnd() {
    if (!g_static.on) return;
    g_static.on = false;
    delay(40);   // let a frame go by before the mask goes away
    if (g_static.mask) heap_caps_free(g_static.mask);
    g_static.mask = nullptr;
}

bool staticScreenActive() { return g_static.on; }

void setWarmth(uint8_t percent) {
    wb::set(percent);
    if (g_disp) lv_obj_invalidate(lv_display_get_screen_active(g_disp));   // repaint with the new balance
}

void setBacklight(uint8_t percent) {
    if (percent > 100) percent = 100;
    const uint32_t maxDuty = (1u << BACKLIGHT_PWM_BITS) - 1;
    ledcWrite(LCD_PIN_BACKLIGHT, maxDuty * percent / 100);
}

}  // namespace board

#endif  // BOARD_CROWPANEL7
