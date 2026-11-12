#include "settings_screen.h"

#include "../layout.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include "../net/settings.h"
#include "../board/board.h"
#include "../version.h"
#include "fonts.h"

namespace settingsui {
namespace {

using layout::S;
using layout::SCREEN_H;
using layout::SCREEN_W;
using layout::X;
using layout::Y;

constexpr uint32_t COL_BG = 0x0B0B0C, COL_ROW = 0x18181B, COL_BTN = 0x232326;
constexpr uint32_t COL_TEXT = 0xF2F2F2, COL_MID = 0xAEAEB2, COL_DIM = 0x78787C, COL_ACCENT = 0x6DB56D;

struct TzChoice {
    const char *name, *posix;
};
const TzChoice TZ_CHOICES[] = {
    {"US Eastern", "EST5EDT,M3.2.0,M11.1.0"},   {"US Central", "CST6CDT,M3.2.0,M11.1.0"},
    {"US Mountain", "MST7MDT,M3.2.0,M11.1.0"},  {"US Arizona", "MST7"},
    {"US Pacific", "PST8PDT,M3.2.0,M11.1.0"},   {"UK, Ireland, Portugal", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Central Europe (Paris)", "CET-1CEST,M3.5.0,M10.5.0/3"}, {"Eastern Europe", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Japan", "JST-9"},                         {"Australia East", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"UTC", "UTC0"},
};
constexpr int TZ_COUNT = sizeof TZ_CHOICES / sizeof TZ_CHOICES[0];
const int SLEEP_CHOICES[] = {0, 5, 10, 30, 60};   // minutes, 0 = never

Hooks g_hooks{};
lv_obj_t *g_root = nullptr, *g_list = nullptr;
lv_obj_t *g_leadValue = nullptr, *g_wifiTitle = nullptr, *g_fwTitle = nullptr, *g_fwStatus = nullptr;
lv_obj_t *g_fwButton = nullptr, *g_fwButtonLabel = nullptr, *g_about = nullptr;
lv_obj_t *g_brightness = nullptr, *g_warmth = nullptr, *g_sleep = nullptr, *g_tz = nullptr;
lv_obj_t *g_swBackdrop = nullptr, *g_swScroll = nullptr, *g_swClock = nullptr;
bool g_updateAvailable = false;
lv_obj_t *g_colourTest = nullptr, *g_ctSlider = nullptr, *g_ctValue = nullptr;

lv_obj_t *patch(lv_obj_t *parent, int x, int y, int w, int h, uint32_t colour, const char *name);
void buildColourTest();

lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t colour, const char *text = "") {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(colour), 0);
    lv_label_set_text(l, text);
    return l;
}

void setText(lv_obj_t *l, const String &t) {
    const char *cur = lv_label_get_text(l);
    if (!cur || strcmp(cur, t.c_str()) != 0) lv_label_set_text(l, t.c_str());
}

lv_obj_t *button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, lv_obj_t **labelOut = nullptr) {
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_style_radius(b, S(22), 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(COL_BTN), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(COL_ACCENT), LV_STATE_PRESSED);
    lv_obj_set_style_pad_hor(b, X(20), 0);
    lv_obj_set_style_pad_ver(b, Y(10), 0);
    lv_obj_t *l = label(b, fonts::ui20, COL_TEXT, text);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    if (labelOut) *labelOut = l;
    return b;
}

// One settings row: title (and optional subtitle) on the left, `control` area on the right.
lv_obj_t *row(const char *title, lv_obj_t **titleOut = nullptr, lv_obj_t **subtitleOut = nullptr) {
    lv_obj_t *r = lv_obj_create(g_list);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(r, 64, 0);
    lv_obj_set_style_bg_color(r, lv_color_hex(COL_ROW), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(r, S(10), 0);
    lv_obj_set_style_pad_hor(r, X(18), 0);
    lv_obj_set_style_pad_ver(r, Y(8), 0);
    lv_obj_set_style_pad_column(r, X(12), 0);
    lv_obj_set_scrollable(r, false);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *texts = lv_obj_create(r);
    lv_obj_remove_style_all(texts);
    lv_obj_set_size(texts, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(texts, 1);
    lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(texts, false);
    lv_obj_t *t = label(texts, fonts::ui20, COL_TEXT, title);
    lv_obj_set_width(t, lv_pct(100));
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
    if (titleOut) *titleOut = t;
    if (subtitleOut) {
        lv_obj_t *s = label(texts, fonts::ui16, COL_MID);
        lv_obj_set_width(s, lv_pct(100));
        lv_label_set_long_mode(s, LV_LABEL_LONG_MODE_WRAP);
        *subtitleOut = s;
    }
    return r;
}

lv_obj_t *switchRow(const char *title, bool on, lv_event_cb_t cb) {
    lv_obj_t *r = row(title);
    lv_obj_t *sw = lv_switch_create(r);
    lv_obj_set_size(sw, S(64), S(34));
    lv_obj_set_state(sw, LV_STATE_CHECKED, on);
    lv_obj_set_style_bg_color(sw, lv_color_hex(COL_ACCENT), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, nullptr);
    return sw;
}

bool checked(lv_event_t *e) { return lv_obj_has_state((lv_obj_t *)lv_event_get_target(e), LV_STATE_CHECKED); }

lv_obj_t *dropdown(lv_obj_t *parent, const String &options, int selected, lv_event_cb_t cb) {
    lv_obj_t *d = lv_dropdown_create(parent);
    lv_dropdown_set_options(d, options.c_str());
    lv_dropdown_set_selected(d, selected);
    lv_obj_set_width(d, X(300));
    lv_obj_set_style_text_font(d, fonts::ui20, 0);
    lv_obj_set_style_bg_color(d, lv_color_hex(COL_BTN), 0);
    lv_obj_set_style_text_color(d, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_border_width(d, 0, 0);
    lv_obj_t *list = lv_dropdown_get_list(d);
    lv_obj_set_style_text_font(list, fonts::ui20, 0);
    lv_obj_set_style_bg_color(list, lv_color_hex(COL_ROW), 0);
    lv_obj_set_style_text_color(list, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_max_height(list, 300, 0);
    lv_obj_add_event_cb(d, cb, LV_EVENT_VALUE_CHANGED, nullptr);
    return d;
}

void showLead() {
    char buf[16];
    snprintf(buf, sizeof buf, "%d ms", (int)settings::leadMs());
    setText(g_leadValue, buf);
}

void stepLead(int delta) {
    int v = settings::leadMs() + delta;
    settings::setLead(v < 0 ? 0 : v > 1000 ? 1000 : v);
    showLead();
}

void confirmForgetWifi() {
    lv_obj_t *mb = lv_msgbox_create(nullptr);
    lv_msgbox_add_title(mb, "Forget Wi-Fi?");
    lv_msgbox_add_text(mb, "The panel leaves this network and shows its setup QR codes again.");
    lv_obj_t *yes = lv_msgbox_add_footer_button(mb, "Forget");
    lv_obj_t *no = lv_msgbox_add_footer_button(mb, "Cancel");
    lv_obj_set_width(mb, X(560));
    lv_obj_set_style_text_font(mb, fonts::ui20, 0);
    lv_obj_add_event_cb(yes, [](lv_event_t *e) {
        lv_msgbox_close((lv_obj_t *)lv_event_get_user_data(e));
        close();
        settings::forgetWifi();
        net::reconnect();
    }, LV_EVENT_CLICKED, mb);
    lv_obj_add_event_cb(no, [](lv_event_t *e) { lv_msgbox_close((lv_obj_t *)lv_event_get_user_data(e)); },
                        LV_EVENT_CLICKED, mb);
}

void buildRows() {
    // Brightness: live preview while dragging, saved on release.
    lv_obj_t *r = row("Brightness");
    g_brightness = lv_slider_create(r);
    lv_obj_set_width(g_brightness, X(300));
    lv_slider_set_range(g_brightness, 10, 100);
    lv_slider_set_value(g_brightness, settings::brightness(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(g_brightness, lv_color_hex(COL_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g_brightness, lv_color_hex(COL_TEXT), LV_PART_KNOB);
    lv_obj_add_event_cb(g_brightness, [](lv_event_t *) {
        if (g_hooks.brightnessPreview) g_hooks.brightnessPreview(lv_slider_get_value(g_brightness));
    }, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(g_brightness, [](lv_event_t *) { settings::setBrightness(lv_slider_get_value(g_brightness)); },
                        LV_EVENT_RELEASED, nullptr);

    // Colour warmth: live while dragging, saved on release.
    lv_obj_t *wsub = nullptr;
    r = row("Colour warmth", nullptr, &wsub);
    lv_label_set_text(wsub, "Warmer counters the panel's bluish white.");
    g_warmth = lv_slider_create(r);
    lv_obj_set_width(g_warmth, X(300));
    lv_slider_set_range(g_warmth, 0, 100);
    lv_slider_set_value(g_warmth, settings::warmth(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(g_warmth, lv_color_hex(0xFFB060), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g_warmth, lv_color_hex(COL_TEXT), LV_PART_KNOB);
    lv_obj_add_event_cb(g_warmth, [](lv_event_t *) { board::setWarmth(lv_slider_get_value(g_warmth)); },
                        LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(g_warmth, [](lv_event_t *) { settings::setWarmth(lv_slider_get_value(g_warmth)); },
                        LV_EVENT_RELEASED, nullptr);

    // Screen off after (it always dims after a minute without playback).
    lv_obj_t *sub = nullptr;
    r = row("Screen off after", nullptr, &sub);
    lv_label_set_text(sub, "Dims after 1 min without playback, then turns off. A touch wakes it.");
    int sel = 2;
    for (int i = 0; i < 5; i++)
        if (SLEEP_CHOICES[i] == settings::sleepMin()) sel = i;
    g_sleep = dropdown(r, "Never\n5 min\n10 min\n30 min\n60 min", sel, [](lv_event_t *) {
        settings::setSleepMin(SLEEP_CHOICES[lv_dropdown_get_selected(g_sleep)]);
    });

    // Lyrics lead.
    r = row("Lyrics ahead by", nullptr, &sub);
    lv_label_set_text(sub, "Shows each line slightly before it is sung.");
    lv_obj_t *box = lv_obj_create(r);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(box, X(10), 0);
    button(box, LV_SYMBOL_MINUS, [](lv_event_t *) { stepLead(-50); });
    g_leadValue = label(box, fonts::ui20, COL_TEXT);
    lv_obj_set_width(g_leadValue, X(90));
    lv_obj_set_style_text_align(g_leadValue, LV_TEXT_ALIGN_CENTER, 0);
    button(box, LV_SYMBOL_PLUS, [](lv_event_t *) { stepLead(+50); });
    showLead();

    g_swBackdrop = switchRow("Blurred cover behind the lyrics", settings::backdrop(),
                             [](lv_event_t *e) { settings::setBackdrop(checked(e)); });
    g_swScroll = switchRow("Smooth lyrics scroll", settings::scroll(),
                           [](lv_event_t *e) { settings::setScroll(checked(e)); });
    g_swClock = switchRow("Clock when nothing plays", settings::clock(),
                          [](lv_event_t *e) { settings::setClock(checked(e)); });

    // Time zone (for the clock).
    r = row("Time zone");
    String opts, cur = settings::tz();
    int tzSel = -1;
    for (int i = 0; i < TZ_COUNT; i++) {
        if (i) opts += "\n";
        opts += TZ_CHOICES[i].name;
        if (cur == TZ_CHOICES[i].posix) tzSel = i;
    }
    if (tzSel < 0) {   // set on the console to something else: keep it visible
        opts += "\nCustom";
        tzSel = TZ_COUNT;
    }
    g_tz = dropdown(r, opts, tzSel, [](lv_event_t *) {
        uint32_t i = lv_dropdown_get_selected(g_tz);
        if (i < (uint32_t)TZ_COUNT) settings::setTz(TZ_CHOICES[i].posix);
        applyTz();
    });

    r = row("Colour test", nullptr, &sub);
    lv_label_set_text(sub, "White, greys and colours, to set the warmth by eye.");
    button(r, "Show", [](lv_event_t *) {
        if (!g_colourTest) buildColourTest();
        lv_slider_set_value(g_ctSlider, settings::warmth(), LV_ANIM_OFF);
        lv_label_set_text_fmt(g_ctValue, "Warmth %d %%", (int)settings::warmth());
        lv_obj_set_hidden(g_colourTest, false);
    });

    r = row("Zone", nullptr, &sub);
    lv_label_set_text(sub, "Which Roon zone the panel follows.");
    button(r, "Choose", [](lv_event_t *) {
        close();
        if (g_hooks.openZones) g_hooks.openZones();
    });

    r = row("Wi-Fi", &g_wifiTitle);
    lv_obj_t *wifiBox = lv_obj_create(r);
    lv_obj_remove_style_all(wifiBox);
    lv_obj_set_size(wifiBox, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(wifiBox, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(wifiBox, X(10), 0);
    button(wifiBox, "Change...", [](lv_event_t *) {
        close();
        if (g_hooks.openWifi) g_hooks.openWifi();
    });
    button(wifiBox, "Forget...", [](lv_event_t *) { confirmForgetWifi(); });

    r = row("Firmware", &g_fwTitle, &g_fwStatus);
    g_fwButton = button(r, "Check now", [](lv_event_t *) {
        if (g_updateAvailable) {
            close();
            if (g_hooks.confirmUpdate) g_hooks.confirmUpdate();
        } else {
            ota::requestCheck();
        }
    }, &g_fwButtonLabel);

    r = row("About", nullptr, &g_about);
}

lv_obj_t *patch(lv_obj_t *parent, int x, int y, int w, int h, uint32_t colour, const char *name) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    // Design-grid rectangle, scaled edge by edge so neighbouring patches never leave a seam.
    lv_obj_set_pos(o, X(x), Y(y));
    lv_obj_set_size(o, X(x + w) - X(x), Y(y + h) - Y(y));
    lv_obj_set_style_bg_color(o, lv_color_hex(colour), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_clickable(o, false);
    if (name) {
        uint32_t r = (colour >> 16) & 0xFF, g = (colour >> 8) & 0xFF, b = colour & 0xFF;
        lv_obj_t *l = label(o, fonts::ui16, r * 3 + g * 6 + b > 1200 ? 0x000000 : 0xFFFFFF, name);
        lv_obj_align(l, LV_ALIGN_BOTTOM_LEFT, X(6), -Y(4));
    }
    return o;
}

// Full-screen test card: a big white, greys, primaries and a skin tone, with the warmth slider.
void buildColourTest() {
    g_colourTest = lv_obj_create(lv_obj_get_parent(g_root));
    lv_obj_remove_style_all(g_colourTest);
    lv_obj_set_size(g_colourTest, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(g_colourTest, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(g_colourTest, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(g_colourTest, false);
    lv_obj_set_clickable(g_colourTest, true);
    patch(g_colourTest, 0, 0, 400, 200, 0xFFFFFF, "White");
    patch(g_colourTest, 400, 0, 134, 200, 0xC0C0C0, "75 %");
    patch(g_colourTest, 534, 0, 133, 200, 0x808080, "50 %");
    patch(g_colourTest, 667, 0, 133, 200, 0x404040, "25 %");
    static const struct { uint32_t c; const char *n; } row2[] = {
        {0xFF0000, "Red"}, {0x00FF00, "Green"}, {0x0000FF, "Blue"}, {0xFFFF00, "Yellow"},
        {0x00FFFF, "Cyan"}, {0xFF00FF, "Magenta"}, {0xE0AC8A, "Skin"}, {0x8A9A5B, "Olive"}};
    for (int i = 0; i < 8; i++) patch(g_colourTest, i * 100, 200, 100, 120, row2[i].c, row2[i].n);
    lv_obj_t *grad = patch(g_colourTest, 0, 320, 800, 50, 0x000000, nullptr);
    lv_obj_set_style_bg_grad_color(grad, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_grad_dir(grad, LV_GRAD_DIR_HOR, 0);

    g_ctValue = label(g_colourTest, fonts::ui20, COL_TEXT, "");
    lv_obj_set_pos(g_ctValue, X(24), Y(400));
    g_ctSlider = lv_slider_create(g_colourTest);
    lv_obj_set_size(g_ctSlider, X(420), S(16));
    lv_obj_set_pos(g_ctSlider, X(190), Y(406));
    lv_slider_set_range(g_ctSlider, 0, 100);
    lv_obj_set_style_bg_color(g_ctSlider, lv_color_hex(0xFFB060), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g_ctSlider, lv_color_hex(COL_TEXT), LV_PART_KNOB);
    lv_obj_set_style_pad_all(g_ctSlider, S(8), LV_PART_KNOB);
    lv_obj_add_event_cb(g_ctSlider, [](lv_event_t *) {
        int v = lv_slider_get_value(g_ctSlider);
        board::setWarmth(v);
        lv_label_set_text_fmt(g_ctValue, "Warmth %d %%", v);
    }, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(g_ctSlider, [](lv_event_t *) {
        settings::setWarmth(lv_slider_get_value(g_ctSlider));
        lv_slider_set_value(g_warmth, settings::warmth(), LV_ANIM_OFF);
    }, LV_EVENT_RELEASED, nullptr);
    lv_obj_t *done = button(g_colourTest, "Done", [](lv_event_t *) { lv_obj_set_hidden(g_colourTest, true); });
    lv_obj_align(done, LV_ALIGN_BOTTOM_RIGHT, -X(20), -Y(24));
    lv_obj_set_hidden(g_colourTest, true);
}

}  // namespace

void build(lv_obj_t *scr, const Hooks &hooks) {
    g_hooks = hooks;
    g_root = lv_obj_create(scr);
    lv_obj_remove_style_all(g_root);
    lv_obj_set_size(g_root, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(g_root, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(g_root, false);
    lv_obj_set_clickable(g_root, true);   // modal

    lv_obj_t *h = label(g_root, fonts::lyricsOther, COL_TEXT, "Settings");
    lv_obj_set_pos(h, X(32), Y(20));
    lv_obj_t *x = lv_button_create(g_root);
    lv_obj_set_size(x, S(64), S(64));
    lv_obj_set_style_radius(x, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(x, 0, 0);
    lv_obj_set_style_bg_color(x, lv_color_hex(COL_BTN), 0);
    lv_obj_set_style_bg_color(x, lv_color_hex(COL_ACCENT), LV_STATE_PRESSED);
    lv_obj_t *xl = label(x, fonts::icons, COL_TEXT, LV_SYMBOL_CLOSE);
    lv_obj_center(xl);
    lv_obj_align(x, LV_ALIGN_TOP_RIGHT, -X(20), Y(12));
    lv_obj_add_event_cb(x, [](lv_event_t *) { close(); }, LV_EVENT_CLICKED, nullptr);

    g_list = lv_obj_create(g_root);
    lv_obj_remove_style_all(g_list);
    lv_obj_set_size(g_list, SCREEN_W - 2 * X(20), Y(386));
    lv_obj_set_pos(g_list, X(20), Y(88));
    lv_obj_set_flex_flow(g_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(g_list, Y(8), 0);
    lv_obj_set_style_pad_bottom(g_list, Y(16), 0);
    lv_obj_set_scroll_dir(g_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_list, LV_SCROLLBAR_MODE_ACTIVE);
    buildRows();
    lv_obj_set_hidden(g_root, true);
}

void open() {
    // Values may have changed from the console since the screen was built.
    lv_slider_set_value(g_brightness, settings::brightness(), LV_ANIM_OFF);
    lv_slider_set_value(g_warmth, settings::warmth(), LV_ANIM_OFF);
    lv_obj_set_state(g_swBackdrop, LV_STATE_CHECKED, settings::backdrop());
    lv_obj_set_state(g_swScroll, LV_STATE_CHECKED, settings::scroll());
    lv_obj_set_state(g_swClock, LV_STATE_CHECKED, settings::clock());
    showLead();
    lv_obj_scroll_to_y(g_list, 0, LV_ANIM_OFF);
    lv_obj_set_hidden(g_root, false);
}

void close() {
    if (g_root) lv_obj_set_hidden(g_root, true);
    if (g_colourTest) lv_obj_set_hidden(g_colourTest, true);
}

bool isOpen() { return (g_root && !lv_obj_is_hidden(g_root)) || (g_colourTest && !lv_obj_is_hidden(g_colourTest)); }

void showColourTest() {
    if (!g_colourTest) buildColourTest();
    lv_slider_set_value(g_ctSlider, settings::warmth(), LV_ANIM_OFF);
    lv_label_set_text_fmt(g_ctValue, "Warmth %d %%", (int)settings::warmth());
    lv_obj_set_hidden(g_colourTest, false);
}

void refresh(const net::Snapshot &s, const ota::Status &o) {
    if (!isOpen()) return;
    settings::Settings st = settings::get();
    setText(g_wifiTitle, "Wi-Fi  " + (st.wifiSsid.length() ? st.wifiSsid : String("(not set)")));
    setText(g_fwTitle, String("Firmware  ") + FW_VERSION);
    g_updateAvailable = o.state == ota::State::Available;
    String status;
    switch (o.state) {
        case ota::State::Idle: status = "Checks for updates by itself every day."; break;
        case ota::State::Checking: status = "Checking for updates..."; break;
        case ota::State::UpToDate: status = "Up to date" + (o.latest.length() ? " (latest " + o.latest + ")" : String()); break;
        case ota::State::Available: status = o.latest + " is available."; break;
        case ota::State::Downloading: status = "Downloading " + o.latest + "... " + String(o.percent) + " %"; break;
        case ota::State::Installed: status = "Installed, restarting..."; break;
        case ota::State::Failed: status = "Last attempt failed: " + o.error; break;
    }
    setText(g_fwStatus, status);
    setText(g_fwButtonLabel, g_updateAvailable ? "Install " + o.latest : String("Check now"));
    char ram[24];
    snprintf(ram, sizeof ram, "%u KB", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
    setText(g_about, "Panel " + net::hostName() + ".local  (" + (s.staIp.length() ? s.staIp : String("-")) + ")\nCompanion " +
                         (s.companion.length() ? s.companion : String("searching...")) + "\nFree internal RAM " + ram);
}

void applyTz() {
    setenv("TZ", settings::tz().c_str(), 1);
    tzset();
}

}  // namespace settingsui
