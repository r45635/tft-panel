#include "wifi_screen.h"

#include <Arduino.h>

#include <vector>

#include "../board/board.h"
#include "../layout.h"
#include "../net/settings.h"
#include "fonts.h"
#include "keyboard.h"

namespace wifiui {
namespace {

using layout::S;
using layout::SCREEN_H;
using layout::SCREEN_W;
using layout::X;
using layout::Y;

constexpr uint32_t COL_BG = 0x0B0B0C, COL_TEXT = 0xF2F2F2, COL_MID = 0xAEAEB2, COL_DIM = 0x78787C;
constexpr uint32_t COL_ACCENT = 0x6DB56D, COL_PRIMARY = 0x3B7A3B, COL_ERROR = 0xFFB0A0;

lv_obj_t *g_root = nullptr;
// List of networks
lv_obj_t *g_listPage = nullptr, *g_list = nullptr, *g_listInfo = nullptr;
// Password
lv_obj_t *g_passPage = nullptr, *g_passTitle = nullptr, *g_ta = nullptr, *g_showLabel = nullptr;
lv_obj_t *g_passError = nullptr, *g_kb = nullptr;
// Saved
lv_obj_t *g_savedPage = nullptr, *g_savedText = nullptr;

std::vector<net::WifiNet> g_nets;
uint32_t g_seenSerial = UINT32_MAX;
bool g_scanning = false;
uint32_t g_waitingFrom = UINT32_MAX;   // results older than this scan request are not the answer yet
String g_chosen;

lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t colour, const char *text = "") {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(colour), 0);
    lv_label_set_text(l, text);
    return l;
}

lv_obj_t *page() {
    lv_obj_t *p = lv_obj_create(g_root);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, SCREEN_W, SCREEN_H);
    lv_obj_set_scrollable(p, false);
    return p;
}

lv_obj_t *textButton(lv_obj_t *parent, const char *text, uint32_t bg, lv_event_cb_t cb, lv_obj_t **labelOut = nullptr) {
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_style_radius(b, S(26), 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(COL_ACCENT), LV_STATE_PRESSED);
    lv_obj_set_style_pad_hor(b, X(24), 0);
    lv_obj_set_style_pad_ver(b, Y(12), 0);
    lv_obj_t *l = label(b, fonts::ui20, COL_TEXT, text);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    if (labelOut) *labelOut = l;
    return b;
}

lv_obj_t *roundButton(lv_obj_t *parent, const char *symbol, lv_event_cb_t cb) {
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, S(64), S(64));
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x232326), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(COL_ACCENT), LV_STATE_PRESSED);
    lv_obj_center(label(b, fonts::icons, COL_TEXT, symbol));
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    return b;
}

void showPage(lv_obj_t *p) {
    for (lv_obj_t *o : {g_listPage, g_passPage, g_savedPage}) lv_obj_set_hidden(o, o != p);
}

const char *strength(int8_t rssi) {
    return rssi > -55 ? "Excellent" : rssi > -67 ? "Good" : rssi > -78 ? "Fair" : "Weak";
}

void save(const String &ssid, const String &pass) {
    settings::setWifi(ssid, pass);
    Serial.printf("[wifi] saved from the screen for ssid \"%s\", restarting\n", ssid.c_str());
    lv_label_set_text_fmt(g_savedText, "Saved. The panel restarts and joins\n%s", ssid.c_str());
    showPage(g_savedPage);
    lv_timer_create([](lv_timer_t *) { board::restart(); }, 1500, nullptr);
}

void connect() {
    String pass = lv_textarea_get_text(g_ta);
    if (pass.length() < 8 || pass.length() > 63) {
        lv_label_set_text(g_passError, "A Wi-Fi password is 8 to 63 characters.");
        return;
    }
    save(g_chosen, pass);
}

void onPick(lv_event_t *e) {
    intptr_t i = (intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= (intptr_t)g_nets.size()) return;
    g_chosen = g_nets[i].ssid;
    if (g_nets[i].open) {
        save(g_chosen, "");
        return;
    }
    lv_label_set_text_fmt(g_passTitle, "Password for %s", g_chosen.c_str());
    lv_textarea_set_text(g_ta, "");
    lv_textarea_set_password_mode(g_ta, true);
    lv_label_set_text(g_showLabel, "Show");
    lv_label_set_text(g_passError, "");
    showPage(g_passPage);
    keyboard::focus(g_ta, true);
}

void fillList() {
    lv_obj_clean(g_list);
    const String current = settings::get().wifiSsid;
    for (size_t i = 0; i < g_nets.size(); i++) {
        const net::WifiNet &w = g_nets[i];
        lv_obj_t *b = lv_button_create(g_list);
        lv_obj_set_width(b, lv_pct(100));
        lv_obj_set_style_radius(b, S(10), 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x18181B), 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x2C2C30), LV_STATE_PRESSED);
        lv_obj_set_style_pad_ver(b, Y(14), 0);
        lv_obj_set_style_pad_hor(b, X(18), 0);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        bool cur = w.ssid == current;
        lv_obj_t *name = label(b, fonts::ui20, cur ? COL_ACCENT : COL_TEXT,
                               ((cur ? String(LV_SYMBOL_OK "  ") : String()) + w.ssid).c_str());
        lv_obj_set_flex_grow(name, 1);
        lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
        label(b, fonts::ui16, COL_DIM, (String(strength(w.rssi)) + (w.open ? "  -  open" : "  -  secured")).c_str());
        lv_obj_add_event_cb(b, onPick, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

void showListInfo() {
    bool looking = g_scanning || g_seenSerial == g_waitingFrom;
    const char *t = !g_nets.empty() ? ""
                    : looking       ? "Looking for networks..."
                                    : "No network heard. Move the panel closer to the router, then refresh.";
    lv_label_set_text(g_listInfo, t);
    lv_obj_set_hidden(g_listInfo, !*t);
}

void buildList() {
    g_listPage = page();
    lv_obj_t *h = label(g_listPage, fonts::lyricsOther, COL_TEXT, "Choose your Wi-Fi");
    lv_obj_set_pos(h, X(32), Y(20));
    lv_obj_t *x = roundButton(g_listPage, LV_SYMBOL_CLOSE, [](lv_event_t *) { close(); });
    lv_obj_align(x, LV_ALIGN_TOP_RIGHT, -X(20), Y(12));
    lv_obj_t *r = roundButton(g_listPage, LV_SYMBOL_REFRESH, [](lv_event_t *) {
        net::requestScan();
        g_waitingFrom = g_seenSerial;
        showListInfo();
    });
    lv_obj_align(r, LV_ALIGN_TOP_RIGHT, -X(20) - S(64) - X(14), Y(12));
    lv_obj_t *note = label(g_listPage, fonts::ui16, COL_DIM, "2.4 GHz networks. Hidden network: use the setup page on a phone.");
    lv_obj_set_pos(note, X(32), Y(64));
    g_list = lv_obj_create(g_listPage);
    lv_obj_remove_style_all(g_list);
    lv_obj_set_size(g_list, SCREEN_W - 2 * X(20), SCREEN_H - Y(100));
    lv_obj_set_pos(g_list, X(20), Y(96));
    lv_obj_set_flex_flow(g_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(g_list, Y(8), 0);
    lv_obj_set_style_pad_bottom(g_list, Y(16), 0);
    lv_obj_set_scroll_dir(g_list, LV_DIR_VER);
    g_listInfo = label(g_listPage, fonts::ui20, COL_MID);
    lv_obj_set_width(g_listInfo, X(640));
    lv_label_set_long_mode(g_listInfo, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(g_listInfo, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_listInfo, LV_ALIGN_CENTER, 0, Y(20));
}

void buildPassword() {
    g_passPage = page();
    g_passTitle = label(g_passPage, fonts::ui20, COL_MID);
    lv_obj_set_width(g_passTitle, X(540));
    lv_label_set_long_mode(g_passTitle, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(g_passTitle, X(32), Y(22));

    g_ta = lv_textarea_create(g_passPage);
    lv_textarea_set_one_line(g_ta, true);
    lv_textarea_set_password_mode(g_ta, true);
    lv_textarea_set_max_length(g_ta, 63);
    lv_textarea_set_placeholder_text(g_ta, "Password");
    lv_obj_set_style_text_font(g_ta, fonts::lyricsOther, 0);
    lv_obj_set_style_text_color(g_ta, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_bg_color(g_ta, lv_color_hex(0x18181B), 0);
    lv_obj_set_style_border_color(g_ta, lv_color_hex(0x2F3037), 0);
    lv_obj_set_style_border_color(g_ta, lv_color_hex(COL_ACCENT), LV_STATE_FOCUSED);
    lv_obj_set_style_radius(g_ta, S(10), 0);
    lv_obj_set_width(g_ta, X(500));
    lv_obj_set_pos(g_ta, X(32), Y(58));

    lv_obj_t *show = textButton(g_passPage, "Show", 0x232326, [](lv_event_t *) {
        bool hidden = lv_textarea_get_password_mode(g_ta);
        lv_textarea_set_password_mode(g_ta, !hidden);
        lv_label_set_text(g_showLabel, hidden ? "Hide" : "Show");
    }, &g_showLabel);
    lv_obj_set_pos(show, X(548), Y(62));
    lv_obj_t *back = textButton(g_passPage, "Back", 0x232326, [](lv_event_t *) { showPage(g_listPage); });
    lv_obj_align(back, LV_ALIGN_TOP_RIGHT, -X(20), Y(62));

    g_passError = label(g_passPage, fonts::ui16, COL_ERROR);
    lv_obj_set_pos(g_passError, X(36), Y(128));

    g_kb = keyboard::create(g_passPage, g_ta, Y(300));
    // The keyboard's OK key connects, its keyboard key goes back to the list.
    lv_obj_add_event_cb(g_kb, [](lv_event_t *) { connect(); }, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(g_kb, [](lv_event_t *) { showPage(g_listPage); }, LV_EVENT_CANCEL, nullptr);
    lv_obj_add_event_cb(g_ta, [](lv_event_t *) { connect(); }, LV_EVENT_READY, nullptr);   // the Enter key

    lv_obj_t *go = textButton(g_passPage, "Connect", COL_PRIMARY, [](lv_event_t *) { connect(); });
    lv_obj_align_to(go, g_kb, LV_ALIGN_OUT_TOP_RIGHT, -X(20), -Y(12));
}

void buildSaved() {
    g_savedPage = page();
    g_savedText = label(g_savedPage, fonts::lyricsOther, COL_TEXT);
    lv_obj_set_width(g_savedText, X(700));
    lv_label_set_long_mode(g_savedText, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(g_savedText, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(g_savedText);
}

}  // namespace

void build(lv_obj_t *scr) {
    g_root = lv_obj_create(scr);
    lv_obj_remove_style_all(g_root);
    lv_obj_set_size(g_root, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(g_root, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(g_root, false);
    lv_obj_set_clickable(g_root, true);   // modal
    buildList();
    buildPassword();
    buildSaved();
    lv_obj_set_hidden(g_root, true);
}

void open() {
    net::requestScan();
    g_waitingFrom = g_seenSerial;
    showListInfo();
    showPage(g_listPage);
    lv_obj_set_hidden(g_root, false);
    lv_obj_move_foreground(g_root);
}

void close() { lv_obj_set_hidden(g_root, true); }

bool isOpen() { return g_root && !lv_obj_is_hidden(g_root); }

void refresh(const net::Snapshot &s) {
    if (!g_root) return;
    if (s.networksSerial != g_seenSerial) {
        g_seenSerial = s.networksSerial;
        g_nets = s.networks ? *s.networks : std::vector<net::WifiNet>();
        fillList();
    }
    g_scanning = s.scanning;
    showListInfo();
}

void showcase(bool password) {
    g_nets = {{"Neon Home", -48, false}, {"Neon Home 2.4", -61, false}, {"Garden Cafe", -72, true},
              {"Studio Guest", -80, false}};
    g_seenSerial = UINT32_MAX - 1;   // the next real scan replaces them
    g_waitingFrom = UINT32_MAX;
    g_scanning = false;
    fillList();
    showListInfo();
    lv_obj_set_hidden(g_root, false);
    lv_obj_move_foreground(g_root);
    showPage(g_listPage);
    if (password) {
        g_chosen = g_nets[0].ssid;
        lv_label_set_text_fmt(g_passTitle, "Password for %s", g_chosen.c_str());
        lv_textarea_set_text(g_ta, "autumn2026");
        lv_textarea_set_password_mode(g_ta, false);
        lv_label_set_text(g_showLabel, "Hide");
        lv_label_set_text(g_passError, "");
        showPage(g_passPage);
        keyboard::focus(g_ta, true);
    }
}

}  // namespace wifiui
