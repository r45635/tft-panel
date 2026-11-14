// Board layer shared by the ESP32-P4 boards with a MIPI-DSI panel. What differs between them
// lives in their `pins_*.h`: the panel's vendor init table, the DSI and video timings, the reset
// and backlight pins, and whether the panel is mounted upside down.
#include "board.h"

#if defined(BOARD_WAVESHARE_P4_7B) || defined(BOARD_P4_S070_WBA)

#include <Wire.h>
#include <esp_cache.h>
#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <esp_lcd_mipi_dsi.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_ldo_regulator.h>
#include <esp_timer.h>
#include <driver/ppa.h>

#include <cstring>

#include "board_pins.h"
#include "gt911.h"
#include "white_balance.h"

namespace board {
namespace {

Info g_info;
Gt911 g_touch(Wire);
esp_lcd_panel_handle_t g_panel = nullptr;
lv_display_t *g_disp = nullptr;
lv_indev_t *g_indev = nullptr;

volatile bool g_touchDown = false;
volatile uint16_t g_touchX = 0, g_touchY = 0;
volatile uint32_t g_flushCount = 0, g_flushUs = 0;

// LVGL renders into two internal-RAM strips; the PPA turns each one 180° straight into the
// panel's PSRAM framebuffer while LVGL renders the next one (the panel is mounted upside down).
constexpr int DRAW_BUF_LINES = 48;
constexpr size_t FB_BYTES = (size_t)LCD_H_RES * LCD_V_RES * 2;

uint16_t *g_fb = nullptr;              // the panel's framebuffer, upside down
ppa_client_handle_t g_ppa = nullptr;   // scale-rotate-mirror client
uint16_t *g_upright = nullptr;         // screenshots: an upright copy, made on request

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

bool fail(const char *what, esp_err_t err) {
    g_info.error = String(what) + ": " + esp_err_to_name(err);
    return false;
}

void dcs(esp_lcd_panel_io_handle_t io, int cmd, const uint8_t *data, size_t len) {
    esp_err_t err = esp_lcd_panel_io_tx_param(io, cmd, data, len);
    if (err != ESP_OK) Serial.printf("[board] panel command 0x%02X: %s\n", cmd, esp_err_to_name(err));
}

// Walks the panel's vendor table: {command, data length, data bytes...} repeated.
void sendInitTable(esp_lcd_panel_io_handle_t io) {
    size_t i = 0;
    while (i + 1 < sizeof PANEL_INIT) {
        uint8_t cmd = PANEL_INIT[i];
        uint8_t len = PANEL_INIT[i + 1];
        dcs(io, cmd, len ? &PANEL_INIT[i + 2] : nullptr, len);
        i += 2u + len;
    }
}

bool IRAM_ATTR onRotated(ppa_client_handle_t, ppa_event_data_t *, void *) {
    lv_display_flush_ready(g_disp);
    return false;
}

bool createPanel() {
    // The D-PHY needs its LDO before the DSI bus comes up.
    esp_ldo_channel_handle_t ldo = nullptr;
    esp_ldo_channel_config_t ldoCfg = {};
    ldoCfg.chan_id = DSI_PHY_LDO_CHAN;
    ldoCfg.voltage_mv = DSI_PHY_LDO_MV;
    esp_err_t err = esp_ldo_acquire_channel(&ldoCfg, &ldo);
    if (err != ESP_OK) return fail("DSI PHY power", err);
    delay(PANEL_PHY_POWER_MS);

    esp_lcd_dsi_bus_handle_t bus = nullptr;
    esp_lcd_dsi_bus_config_t busCfg = {};
    busCfg.bus_id = 0;
    busCfg.num_data_lanes = DSI_LANES;
    busCfg.phy_clk_src = DSI_PHY_CLK_SRC;
    busCfg.lane_bit_rate_mbps = DSI_LANE_MBPS;
    err = esp_lcd_new_dsi_bus(&busCfg, &bus);
    if (err != ESP_OK) return fail("DSI bus", err);

    esp_lcd_panel_io_handle_t io = nullptr;
    esp_lcd_dbi_io_config_t dbi = {};
    dbi.virtual_channel = 0;
    dbi.lcd_cmd_bits = 8;
    dbi.lcd_param_bits = 8;
    err = esp_lcd_new_panel_io_dbi(bus, &dbi, &io);
    if (err != ESP_OK) return fail("DSI command channel", err);

    // Hardware reset, then the panel's own vendor sequence (see its pins_*.h for the source).
    if (LCD_PIN_RESET >= 0) {   // some boards route no reset line and rely on power-on
        pinMode(LCD_PIN_RESET, OUTPUT);
        digitalWrite(LCD_PIN_RESET, LOW);
        delay(PANEL_RESET_LOW_MS);
        digitalWrite(LCD_PIN_RESET, HIGH);
    }
    delay(PANEL_RESET_WAIT_MS);   // some controllers ignore everything until this has passed

    esp_lcd_dpi_panel_config_t dpi = {};
    dpi.virtual_channel = 0;
    dpi.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
    dpi.dpi_clock_freq_mhz = DPI_CLOCK_MHZ;
    dpi.pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565;
    dpi.num_fbs = 1;
    dpi.video_timing.h_size = LCD_H_RES;
    dpi.video_timing.v_size = LCD_V_RES;
    dpi.video_timing.hsync_pulse_width = LCD_HSYNC_PULSE_WIDTH;
    dpi.video_timing.hsync_back_porch = LCD_HSYNC_BACK_PORCH;
    dpi.video_timing.hsync_front_porch = LCD_HSYNC_FRONT_PORCH;
    dpi.video_timing.vsync_pulse_width = LCD_VSYNC_PULSE_WIDTH;
    dpi.video_timing.vsync_back_porch = LCD_VSYNC_BACK_PORCH;
    dpi.video_timing.vsync_front_porch = LCD_VSYNC_FRONT_PORCH;

    // 🪤 Whether the vendor table may be sent before the video stream is a property of the
    // board: on the S070 nothing leaves the command FIFO until the stream runs, and the
    // twentieth command blocks forever. The EK79007 was brought up the other way round and is
    // left as it was. Never read the panel's ID: a panel that does not answer a DSI read hangs
    // every command after it.
    err = esp_lcd_new_panel_dpi(bus, &dpi, &g_panel);
    if (err != ESP_OK) return fail("DPI panel", err);
    if (PANEL_INIT_AFTER_DPI_START) {
        err = esp_lcd_panel_init(g_panel);
        if (err != ESP_OK) return fail("DPI panel init", err);
    }
    sendInitTable(io);
    if (!PANEL_INIT_SENDS_SLEEP_OUT) dcs(io, 0x11, nullptr, 0);   // sleep out
    delay(PANEL_SLEEP_OUT_MS);
    // Some controllers wake up with the display still off and show nothing, however good the
    // video stream is, until they are told to turn it on.
    if (PANEL_NEEDS_DISPLAY_ON) {
        dcs(io, 0x29, nullptr, 0);   // display on
        delay(20);
    }
    if (!PANEL_INIT_AFTER_DPI_START) {
        err = esp_lcd_panel_init(g_panel);
        if (err != ESP_OK) return fail("DPI panel init", err);
    }

    // Black screen before the backlight comes on.
    void *fb = nullptr;
    err = esp_lcd_dpi_panel_get_frame_buffer(g_panel, 1, &fb);
    if (err != ESP_OK || !fb) return fail("DPI framebuffer", err != ESP_OK ? err : ESP_FAIL);
    g_fb = (uint16_t *)fb;
    memset(fb, 0, FB_BYTES);
    esp_cache_msync(fb, FB_BYTES, ESP_CACHE_MSYNC_FLAG_DIR_C2M);

    ppa_client_config_t pc = {};
    pc.oper_type = PPA_OPERATION_SRM;
    pc.max_pending_trans_num = 1;   // LVGL waits for each flush before the next one
    err = ppa_register_client(&pc, &g_ppa);
    if (err != ESP_OK) return fail("PPA", err);
    ppa_event_callbacks_t cbs = {};
    cbs.on_trans_done = onRotated;
    err = ppa_client_register_event_callbacks(g_ppa, &cbs);
    if (err != ESP_OK) return fail("PPA callbacks", err);
    return true;
}

void flushCb(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
    int64_t t0 = esp_timer_get_time();
    const int w = lv_area_get_width(area), h = lv_area_get_height(area);
    uint16_t *p = (uint16_t *)px;
    wb::apply(p, (size_t)w * h);
    // A panel mounted upside down gets the area turned 180° and written to the mirrored
    // position; one mounted the right way up gets it as it is, where it belongs.
    ppa_srm_oper_config_t op = {};
    op.in.buffer = p;
    op.in.pic_w = op.in.block_w = w;
    op.in.pic_h = op.in.block_h = h;
    op.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
    op.out.buffer = g_fb;
    op.out.buffer_size = FB_BYTES;
    op.out.pic_w = LCD_H_RES;
    op.out.pic_h = LCD_V_RES;
    op.out.block_offset_x = PANEL_UPSIDE_DOWN ? (LCD_H_RES - 1 - area->x2) : area->x1;
    op.out.block_offset_y = PANEL_UPSIDE_DOWN ? (LCD_V_RES - 1 - area->y2) : area->y1;
    op.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
    op.rotation_angle = PANEL_UPSIDE_DOWN ? PPA_SRM_ROTATION_ANGLE_180 : PPA_SRM_ROTATION_ANGLE_0;
    op.scale_x = op.scale_y = 1.0f;
    op.mode = PPA_TRANS_MODE_NON_BLOCKING;
    esp_err_t err = ppa_do_scale_rotate_mirror(g_ppa, &op);
    g_flushUs += (uint32_t)(esp_timer_get_time() - t0);
    g_flushCount++;
    if (err != ESP_OK) lv_display_flush_ready(disp);   // nothing queued: no completion callback
}

void touchReadCb(lv_indev_t *, lv_indev_data_t *data) {
    TouchPoint p;
    int n = g_touch.read(p);
    if (n > 0) {
        // The GT911 reports in the viewer's orientation (checked at the four corners).
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
    void *buf1 = heap_caps_aligned_alloc(64, bufBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    void *buf2 = heap_caps_aligned_alloc(64, bufBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!buf1 || !buf2) {
        g_info.error = "LVGL draw buffers: out of internal RAM";
        return false;
    }
    g_disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_color_format(g_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(g_disp, buf1, buf2, bufBytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
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
    // Backlight off until the first frame is on the panel (PWM output is inverted on this board).
    ledcAttach(LCD_PIN_BACKLIGHT, 5000, 10);
    ledcOutputInvert(LCD_PIN_BACKLIGHT, LCD_BACKLIGHT_ACTIVE_LOW);
    ledcWrite(LCD_PIN_BACKLIGHT, 0);

    g_info.psramBytes = ESP.getPsramSize();
    uint32_t flash = 0;
    if (esp_flash_get_size(nullptr, &flash) == ESP_OK) g_info.flashBytes = flash;
    if (!psramFound()) {
        g_info.error = "PSRAM missing";
        return false;
    }

    Wire.begin(I2C_PIN_SDA, I2C_PIN_SCL, I2C_FREQ_HZ);
    scanI2c();
    if (g_touch.begin(GT911_ADDR, GT911_ADDR_ALT)) {
        g_info.touch = true;
        g_info.touchAddr = g_touch.address();
        if (!g_touch.readInfo(g_info.touchId, g_info.touchFw, g_info.touchXRes, g_info.touchYRes))
            g_info.error = "GT911 answered its address but not the info registers";
    } else {
        g_info.error = "GT911 not found on I2C (0x5D / 0x14)";
    }

    if (!createPanel()) return false;
    if (!createLvgl()) return false;
    return true;
}

void touchDiag() { Serial.println("[touchdiag] not needed on this board (touch reset is not wired)"); }

const Info &info() { return g_info; }

lv_display_t *display() { return g_disp; }

// The framebuffer turned back upright: reversing the whole buffer undoes the 180°. Made on
// request (screenshots only), so no frame pays for a copy.
const uint16_t *frameBuffer() {
    if (!g_fb) return nullptr;
    if (!g_upright) g_upright = (uint16_t *)heap_caps_aligned_alloc(64, FB_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!g_upright) return nullptr;
    esp_cache_msync(g_fb, FB_BYTES, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    const size_t n = FB_BYTES / 2;
    for (size_t i = 0; i < n; i++) g_upright[i] = g_fb[n - 1 - i];
    return g_upright;
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

// Flash writes do not stall the display here: the update screen stays live (HAS_STATIC_SCREEN).
namespace {
volatile bool g_staticOn = false;
}
void staticScreenBegin(const StaticScreen &) { g_staticOn = true; }
void staticScreenProgress(int) {}
void staticScreenEnd() { g_staticOn = false; }
bool staticScreenActive() { return g_staticOn; }

void setWarmth(uint8_t percent) {
    wb::set(percent);
    if (g_disp) lv_obj_invalidate(lv_display_get_screen_active(g_disp));
}

void setBacklight(uint8_t percent) {
    if (percent > 100) percent = 100;
    ledcWrite(LCD_PIN_BACKLIGHT, 1023u * percent / 100);
}

}  // namespace board

#endif  // BOARD_WAVESHARE_P4_7B
