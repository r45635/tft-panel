#include "settings_screen.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include "../net/settings.h"
#include "../version.h"
#include "fonts.h"

namespace settingsui {
namespace {

constexpr uint32_t COL_BG = 0x0E1116, COL_ROW = 0x161B22, COL_BTN = 0x1C232B;
constexpr uint32_t COL_TEXT = 0xF2F4F7, COL_MID = 0xA4AEB9, COL_DIM = 0x6B7683, COL_ACCENT = 0x40C4FF;

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
lv_obj_t *g_brightness = nullptr, *g_sleep = nullptr, *g_tz = nullptr;
lv_obj_t *g_swBackdrop = nullptr, *g_swScroll = nullptr, *g_swClock = nullptr;
bool g_updateAvailable = false;

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
    lv_obj_set_style_radius(b, 22, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(COL_BTN), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(COL_ACCENT), LV_STATE_PRESSED);
    lv_obj_set_style_pad_hor(b, 20, 0);
    lv_obj_set_style_pad_ver(b, 10, 0);
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
    lv_obj_set_style_radius(r, 10, 0);
    lv_obj_set_style_pad_hor(r, 18, 0);
    lv_obj_set_style_pad_ver(r, 8, 0);
    lv_obj_set_style_pad_column(r, 12, 0);
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
    lv_obj_set_size(sw, 64, 34);
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
    lv_obj_set_width(d, 300);
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
    lv_obj_set_width(mb, 560);
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
    lv_obj_set_width(g_brightness, 300);
    lv_slider_set_range(g_brightness, 10, 100);
    lv_slider_set_value(g_brightness, settings::brightness(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(g_brightness, lv_color_hex(COL_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g_brightness, lv_color_hex(COL_TEXT), LV_PART_KNOB);
    lv_obj_add_event_cb(g_brightness, [](lv_event_t *) {
        if (g_hooks.brightnessPreview) g_hooks.brightnessPreview(lv_slider_get_value(g_brightness));
    }, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(g_brightness, [](lv_event_t *) { settings::setBrightness(lv_slider_get_value(g_brightness)); },
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
    lv_obj_set_style_pad_column(box, 10, 0);
    button(box, LV_SYMBOL_MINUS, [](lv_event_t *) { stepLead(-50); });
    g_leadValue = label(box, fonts::ui20, COL_TEXT);
    lv_obj_set_width(g_leadValue, 90);
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

    r = row("Zone", nullptr, &sub);
    lv_label_set_text(sub, "Which Roon zone the panel follows.");
    button(r, "Choose", [](lv_event_t *) {
        close();
        if (g_hooks.openZones) g_hooks.openZones();
    });

    r = row("Wi-Fi", &g_wifiTitle);
    button(r, "Forget...", [](lv_event_t *) { confirmForgetWifi(); });

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

}  // namespace

void build(lv_obj_t *scr, const Hooks &hooks) {
    g_hooks = hooks;
    g_root = lv_obj_create(scr);
    lv_obj_remove_style_all(g_root);
    lv_obj_set_size(g_root, 800, 480);
    lv_obj_set_style_bg_color(g_root, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(g_root, false);
    lv_obj_set_clickable(g_root, true);   // modal

    lv_obj_t *h = label(g_root, fonts::lyricsOther, COL_TEXT, "Settings");
    lv_obj_set_pos(h, 32, 20);
    lv_obj_t *x = lv_button_create(g_root);
    lv_obj_set_size(x, 64, 64);
    lv_obj_set_style_radius(x, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(x, 0, 0);
    lv_obj_set_style_bg_color(x, lv_color_hex(COL_BTN), 0);
    lv_obj_set_style_bg_color(x, lv_color_hex(COL_ACCENT), LV_STATE_PRESSED);
    lv_obj_t *xl = label(x, &lv_font_montserrat_28, COL_TEXT, LV_SYMBOL_CLOSE);
    lv_obj_center(xl);
    lv_obj_align(x, LV_ALIGN_TOP_RIGHT, -20, 12);
    lv_obj_add_event_cb(x, [](lv_event_t *) { close(); }, LV_EVENT_CLICKED, nullptr);

    g_list = lv_obj_create(g_root);
    lv_obj_remove_style_all(g_list);
    lv_obj_set_size(g_list, 760, 386);
    lv_obj_set_pos(g_list, 20, 88);
    lv_obj_set_flex_flow(g_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(g_list, 8, 0);
    lv_obj_set_style_pad_bottom(g_list, 16, 0);
    lv_obj_set_scroll_dir(g_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_list, LV_SCROLLBAR_MODE_ACTIVE);
    buildRows();
    lv_obj_set_hidden(g_root, true);
}

void open() {
    // Values may have changed from the console since the screen was built.
    lv_slider_set_value(g_brightness, settings::brightness(), LV_ANIM_OFF);
    lv_obj_set_state(g_swBackdrop, LV_STATE_CHECKED, settings::backdrop());
    lv_obj_set_state(g_swScroll, LV_STATE_CHECKED, settings::scroll());
    lv_obj_set_state(g_swClock, LV_STATE_CHECKED, settings::clock());
    showLead();
    lv_obj_scroll_to_y(g_list, 0, LV_ANIM_OFF);
    lv_obj_set_hidden(g_root, false);
}

void close() {
    if (g_root) lv_obj_set_hidden(g_root, true);
}

bool isOpen() { return g_root && !lv_obj_is_hidden(g_root); }

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
    setText(g_about, "Panel IP " + (s.staIp.length() ? s.staIp : String("-")) + "\nCompanion " +
                         (s.companion.length() ? s.companion : String("searching...")) + "\nFree internal RAM " + ram);
}

void applyTz() {
    setenv("TZ", settings::tz().c_str(), 1);
    tzset();
}

}  // namespace settingsui
