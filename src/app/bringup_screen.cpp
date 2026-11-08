#include "bringup_screen.h"

#include <Arduino.h>
#include <lvgl.h>

#include "../board/board.h"
#include "../board/board_pins.h"

namespace bringup {
namespace {

lv_obj_t *g_touchLabel = nullptr;
lv_obj_t *g_dot = nullptr;
lv_obj_t *g_heapLabel = nullptr;
uint32_t g_lastRefresh = 0;

void addColourBars(lv_obj_t *parent) {
    // Pure R, G, B, white, black: a wrong data-pin order shows up at once.
    static const uint32_t colours[] = {0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF, 0x000000};
    static const char *names[] = {"R", "G", "B", "W", "K"};
    const int w = 60, h = 40;
    for (int i = 0; i < 5; i++) {
        lv_obj_t *bar = lv_obj_create(parent);
        lv_obj_remove_style_all(bar);
        lv_obj_set_size(bar, w, h);
        lv_obj_set_pos(bar, 20 + i * (w + 6), 330);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(bar, lv_color_hex(colours[i]), 0);
        lv_obj_set_style_border_width(bar, 1, 0);
        lv_obj_set_style_border_color(bar, lv_color_hex(0x808080), 0);
        lv_obj_t *l = lv_label_create(bar);
        lv_label_set_text(l, names[i]);
        lv_obj_set_style_text_color(l, lv_color_hex(i == 3 ? 0x000000 : 0xFFFFFF), 0);
        lv_obj_center(l);
    }
    // Horizontal gradient: banding reveals a missing low bit.
    lv_obj_t *grad = lv_obj_create(parent);
    lv_obj_remove_style_all(grad);
    lv_obj_set_size(grad, 5 * w + 4 * 6, 24);
    lv_obj_set_pos(grad, 20, 380);
    lv_obj_set_style_bg_opa(grad, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(grad, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_grad_color(grad, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_grad_dir(grad, LV_GRAD_DIR_HOR, 0);
}

void backlightChanged(lv_event_t *e) {
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    int v = lv_slider_get_value(slider);
    board::setBacklight((uint8_t)v);
}

}  // namespace

void show() {
    const board::Info &info = board::info();
    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_screen_load(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);
    lv_obj_set_style_text_color(scr, lv_color_hex(0xE8ECF0), 0);
    lv_obj_set_scrollable(scr, false);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "TFT Panel - hardware bring-up");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_pos(title, 20, 16);

    char buf[512];
    snprintf(buf, sizeof buf,
             "PSRAM  %u KB    Flash  %u KB\n"
             "LCD    %dx%d RGB565  pclk %u MHz\n"
             "I2C    %s\n"
             "PCA9557 0x18  %s\n"
             "Touch  %s  addr 0x%02X  id \"%s\"  fw 0x%04X  %ux%u\n"
             "%s%s",
             (unsigned)(info.psramBytes / 1024), (unsigned)(info.flashBytes / 1024),
             board::LCD_H_RES, board::LCD_V_RES, (unsigned)(board::LCD_PCLK_HZ / 1000000),
             info.i2cDevices.length() ? info.i2cDevices.c_str() : "(none)",
             info.pca9557 ? "OK (touch reset done)" : "MISSING",
             info.touch ? "OK" : "MISSING", info.touchAddr, info.touchId, info.touchFw,
             info.touchXRes, info.touchYRes,
             info.error.length() ? "ERROR  " : "", info.error.c_str());
    lv_obj_t *infoLabel = lv_label_create(scr);
    lv_label_set_text(infoLabel, buf);
    lv_obj_set_style_text_font(infoLabel, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(infoLabel, 20, 70);
    if (info.error.length()) lv_obj_set_style_text_color(infoLabel, lv_color_hex(0xFFB0A0), 0);

    addColourBars(scr);

    lv_obj_t *blLabel = lv_label_create(scr);
    lv_label_set_text(blLabel, "Backlight");
    lv_obj_set_pos(blLabel, 20, 422);
    lv_obj_t *slider = lv_slider_create(scr);
    lv_obj_set_size(slider, 230, 18);
    lv_obj_set_pos(slider, 130, 428);
    lv_slider_set_range(slider, 5, 100);
    lv_slider_set_value(slider, 100, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, backlightChanged, LV_EVENT_VALUE_CHANGED, nullptr);

    // Touch area on the right: a dot follows the finger, corners are labelled
    // so a mirrored axis is obvious.
    lv_obj_t *pad = lv_obj_create(scr);
    lv_obj_set_size(pad, 360, 300);
    lv_obj_set_pos(pad, 420, 120);
    lv_obj_set_scrollable(pad, false);
    lv_obj_set_clickable(pad, false);
    lv_obj_set_style_bg_color(pad, lv_color_hex(0x1C232B), 0);
    lv_obj_set_style_border_color(pad, lv_color_hex(0x3A4652), 0);
    lv_obj_t *hint = lv_label_create(pad);
    lv_label_set_text(hint, "Touch anywhere on the screen");
    lv_obj_center(hint);

    g_touchLabel = lv_label_create(scr);
    lv_label_set_text(g_touchLabel, "touch: -");
    lv_obj_set_pos(g_touchLabel, 420, 430);

    g_heapLabel = lv_label_create(scr);
    lv_obj_set_style_text_font(g_heapLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(g_heapLabel, 420, 90);

    g_dot = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_dot);
    lv_obj_set_size(g_dot, 24, 24);
    lv_obj_set_style_radius(g_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(g_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(g_dot, lv_color_hex(0x6DB56D), 0);
    lv_obj_set_hidden(g_dot, true);
    lv_obj_set_clickable(g_dot, false);
}

void tick() {
    uint16_t x, y;
    bool down = board::lastTouch(x, y);
    if (down) {
        lv_obj_set_hidden(g_dot, false);
        lv_obj_set_pos(g_dot, x - 12, y - 12);
    } else {
        lv_obj_set_hidden(g_dot, true);
    }

    uint32_t now = millis();
    if (now - g_lastRefresh < 200) return;
    g_lastRefresh = now;
    if (down) lv_label_set_text_fmt(g_touchLabel, "touch: x=%u y=%u", x, y);
    else lv_label_set_text(g_touchLabel, "touch: released");
    lv_label_set_text_fmt(g_heapLabel, "heap int %u KB free (min %u)  psram %u KB free",
                          (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                          (unsigned)(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024),
                          (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
}

}  // namespace bringup
