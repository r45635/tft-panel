#include "ui.h"

#include <Arduino.h>
#include <lvgl.h>

#include "../net/net.h"
#include "../net/settings.h"
#include "fonts.h"

namespace ui {
namespace {

constexpr uint32_t COL_BG = 0x0E1116;
constexpr uint32_t COL_TEXT = 0xF2F4F7;
constexpr uint32_t COL_DIM = 0x6B7683;
constexpr uint32_t COL_MID = 0xA4AEB9;
constexpr uint32_t COL_ACCENT = 0x40C4FF;
constexpr uint32_t COL_ERROR = 0xFFB0A0;

constexpr int LYRIC_ROWS = 5;   // 2 before, current, 2 after
constexpr int CURRENT_ROW = 2;

lv_obj_t *g_scr = nullptr;

// Header
lv_obj_t *g_zone = nullptr, *g_status = nullptr;
// Track
lv_obj_t *g_title = nullptr, *g_artist = nullptr;
// Lyrics stage
lv_obj_t *g_stage = nullptr;
lv_obj_t *g_rows[LYRIC_ROWS] = {};
lv_obj_t *g_message = nullptr;   // centred message when there is nothing to sing
// Progress
lv_obj_t *g_bar = nullptr, *g_elapsed = nullptr, *g_total = nullptr;
// Setup overlay
lv_obj_t *g_setup = nullptr, *g_qrWifi = nullptr, *g_qrUrl = nullptr, *g_setupText = nullptr;

bool g_demo = false;
uint32_t g_seenVersion = UINT32_MAX;
net::Snapshot g_s;
uint32_t g_seenLyricsSerial = UINT32_MAX;
int g_shownIndex = INT32_MIN;
uint32_t g_lastProgress = 0;
String g_qrWifiData, g_qrUrlData;

lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t colour) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(colour), 0);
    lv_label_set_text(l, "");
    return l;
}

void fmtTime(char *buf, size_t n, uint32_t ms) {
    uint32_t s = ms / 1000;
    snprintf(buf, n, "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
}

// Escapes ; , : \ " for the WIFI: QR format.
String qrEscape(const String &v) {
    String o;
    for (size_t i = 0; i < v.length(); i++) {
        char c = v[i];
        if (c == ';' || c == ',' || c == ':' || c == '\\' || c == '"') o += '\\';
        o += c;
    }
    return o;
}

void buildSetup() {
    g_setup = lv_obj_create(g_scr);
    lv_obj_remove_style_all(g_setup);
    lv_obj_set_size(g_setup, 800, 480);
    lv_obj_set_style_bg_color(g_setup, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(g_setup, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(g_setup, false);

    lv_obj_t *h = label(g_setup, fonts::lyricsOther, COL_TEXT);
    lv_label_set_text(h, "Set up TFT Panel");
    lv_obj_set_pos(h, 40, 24);

    auto qrBlock = [](int x, const char *step) {
        lv_obj_t *qr = lv_qrcode_create(g_setup);
        lv_qrcode_set_size(qr, 200);
        lv_qrcode_set_dark_color(qr, lv_color_hex(0x000000));
        lv_qrcode_set_light_color(qr, lv_color_hex(0xFFFFFF));
        lv_obj_set_style_border_color(qr, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_width(qr, 8, 0);
        lv_obj_set_pos(qr, x, 90);
        lv_obj_t *t = label(g_setup, fonts::ui20, COL_MID);
        lv_label_set_text(t, step);
        lv_obj_set_width(t, 260);
        lv_obj_set_pos(t, x - 22, 312);
        lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
        return qr;
    };
    g_qrWifi = qrBlock(80, "1. Scan to join the panel's Wi-Fi");
    g_qrUrl = qrBlock(330, "2. Scan to open the setup page");

    g_setupText = label(g_setup, fonts::ui20, COL_TEXT);
    lv_obj_set_pos(g_setupText, 580, 100);
    lv_obj_set_width(g_setupText, 200);
    lv_label_set_long_mode(g_setupText, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_hidden(g_setup, true);
}

void build() {
    g_scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(g_scr, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_text_color(g_scr, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_scrollable(g_scr, false);

    g_zone = label(g_scr, fonts::ui16, COL_MID);
    lv_obj_set_pos(g_zone, 24, 12);
    g_status = label(g_scr, fonts::ui16, COL_DIM);
    lv_obj_set_width(g_status, 460);
    lv_obj_set_style_text_align(g_status, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(g_status, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(g_status, LV_ALIGN_TOP_RIGHT, -24, 12);

    g_title = label(g_scr, fonts::ui20, COL_TEXT);
    lv_obj_set_width(g_title, 752);
    lv_label_set_long_mode(g_title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(g_title, 24, 40);
    g_artist = label(g_scr, fonts::ui16, COL_MID);
    lv_obj_set_width(g_artist, 752);
    lv_label_set_long_mode(g_artist, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(g_artist, 24, 68);

    g_stage = lv_obj_create(g_scr);
    lv_obj_remove_style_all(g_stage);
    lv_obj_set_size(g_stage, 760, 340);
    lv_obj_set_pos(g_stage, 20, 98);
    lv_obj_set_scrollable(g_stage, false);
    lv_obj_set_flex_flow(g_stage, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_stage, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(g_stage, 10, 0);
    // Fixed budget so the stage never overflows: neighbours on one line, the current line on
    // at most two; longer text ends with "…". 4 x 41 + 2 x 60 + gaps fits the 340 px stage.
    for (int i = 0; i < LYRIC_ROWS; i++) {
        bool cur = i == CURRENT_ROW;
        const lv_font_t *font = cur ? fonts::lyricsCurrent : fonts::lyricsOther;
        lv_obj_t *l = label(g_stage, font, cur ? COL_TEXT : COL_DIM);
        int32_t lh = lv_font_get_line_height(font);
        lv_obj_set_width(l, 760);
        lv_obj_set_height(l, LV_SIZE_CONTENT);
        lv_obj_set_style_max_height(l, cur ? 2 * lh : lh, 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        g_rows[i] = l;
    }

    g_message = label(g_scr, fonts::lyricsOther, COL_MID);
    lv_obj_set_width(g_message, 700);
    lv_label_set_long_mode(g_message, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(g_message, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_message, LV_ALIGN_CENTER, 0, 10);

    g_bar = lv_bar_create(g_scr);
    lv_obj_set_size(g_bar, 640, 4);
    lv_obj_align(g_bar, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_bar_set_range(g_bar, 0, 1000);
    lv_obj_set_style_bg_color(g_bar, lv_color_hex(0x2A323C), LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_bar, lv_color_hex(COL_ACCENT), LV_PART_INDICATOR);
    g_elapsed = label(g_scr, fonts::ui16, COL_DIM);
    lv_obj_align(g_elapsed, LV_ALIGN_BOTTOM_LEFT, 24, -10);
    g_total = label(g_scr, fonts::ui16, COL_DIM);
    lv_obj_align(g_total, LV_ALIGN_BOTTOM_RIGHT, -24, -10);

    buildSetup();
}

void setRows(int index) {
    const lyrics::Lyrics *l = g_s.lyrics.get();
    for (int r = 0; r < LYRIC_ROWS; r++) {
        int i = index - CURRENT_ROW + r;
        const char *text = "";
        if (l && i >= 0 && i < (int)l->lines.size()) text = l->lines[i].text.c_str();
        // Before the first line, show the first lines dimmed below an empty current row.
        lv_label_set_text(g_rows[r], text);
    }
}

void showMessage(const String &msg, uint32_t colour) {
    lv_obj_set_hidden(g_stage, true);
    lv_obj_set_hidden(g_message, false);
    lv_obj_set_style_text_color(g_message, lv_color_hex(colour), 0);
    lv_label_set_text(g_message, msg.c_str());
}

void applySnapshot() {
    // Setup overlay
    bool setup = g_s.portalActive;
    lv_obj_set_hidden(g_setup, !setup);
    if (setup) {
        String wifi = "WIFI:T:WPA;S:" + qrEscape(g_s.apSsid) + ";P:" + qrEscape(g_s.apPass) + ";;";
        String url = "http://" + g_s.apIp + "/";
        if (wifi != g_qrWifiData) {
            g_qrWifiData = wifi;
            lv_qrcode_update(g_qrWifi, wifi.c_str(), wifi.length());
        }
        if (url != g_qrUrlData) {
            g_qrUrlData = url;
            lv_qrcode_update(g_qrUrl, url.c_str(), url.length());
        }
        String t = "Or join by hand:\n\nNetwork\n" + g_s.apSsid + "\n\nPassword\n" + g_s.apPass + "\n\nthen open\n" + url;
        if (g_s.phase == net::Phase::WifiFailed) t += "\n\n" + g_s.status;
        lv_label_set_text(g_setupText, t.c_str());
        return;
    }

    lv_label_set_text(g_zone, g_s.zoneName.length()
                                  ? (g_s.zoneName + (g_s.zonePinned ? "" : "  (auto)")).c_str()
                                  : "");
    bool bad = g_s.phase == net::Phase::WifiFailed || g_s.phase == net::Phase::CompanionError;
    lv_obj_set_style_text_color(g_status, lv_color_hex(bad ? COL_ERROR : COL_DIM), 0);
    lv_label_set_text(g_status, g_s.status.c_str());

    bool track = g_s.phase == net::Phase::Playing || (g_s.title.length() && bad);
    lv_label_set_text(g_title, track ? g_s.title.c_str() : "");
    lv_label_set_text(g_artist, track ? (g_s.artist + (g_s.album.length() ? "  -  " + g_s.album : "")).c_str() : "");
    lv_obj_set_hidden(g_bar, !track);
    lv_obj_set_hidden(g_elapsed, !track);
    lv_obj_set_hidden(g_total, !track);

    if (!track) {
        showMessage(g_s.status, bad ? COL_ERROR : COL_MID);
        return;
    }
    switch (g_s.lyricsStatus) {
        case net::LyricsStatus::Found:
            lv_obj_set_hidden(g_message, true);
            lv_obj_set_hidden(g_stage, false);
            if (g_s.lyricsSerial != g_seenLyricsSerial) {
                g_seenLyricsSerial = g_s.lyricsSerial;
                g_shownIndex = INT32_MIN;   // force a redraw of the rows
            }
            break;
        case net::LyricsStatus::Loading: showMessage("Looking for lyrics...", COL_DIM); break;
        case net::LyricsStatus::Instrumental: showMessage("Instrumental", COL_MID); break;
        case net::LyricsStatus::NotFound: showMessage("No lyrics found for this track", COL_DIM); break;
        case net::LyricsStatus::Error: showMessage("Lyrics unavailable: " + g_s.lyricsError, COL_ERROR); break;
        case net::LyricsStatus::None: showMessage("", COL_DIM); break;
    }
}

void tickLyrics(uint32_t now) {
    if (g_s.portalActive || g_s.lyricsStatus != net::LyricsStatus::Found || !g_s.lyrics) return;
    const lyrics::Lyrics &l = *g_s.lyrics;
    int32_t pos = g_s.positionAt(now);
    int index;
    if (l.synced) {
        index = lyrics::currentIndex(l, pos + settings::leadMs() - g_s.offsetMs);
    } else {
        // Plain lyrics: no timing, so scroll in proportion to the track position.
        index = g_s.durationMs ? (int)((int64_t)pos * (int64_t)l.lines.size() / g_s.durationMs) : 0;
        if (index >= (int)l.lines.size()) index = l.lines.size() - 1;
    }
    if (index == g_shownIndex) return;
    g_shownIndex = index;
    setRows(index);
}

void tickProgress(uint32_t now) {
    if (g_s.portalActive || !g_s.durationMs || now - g_lastProgress < 250) return;
    g_lastProgress = now;
    int32_t pos = g_s.positionAt(now);
    lv_bar_set_value(g_bar, (int32_t)((int64_t)pos * 1000 / g_s.durationMs), LV_ANIM_OFF);
    char a[12], b[12];
    fmtTime(a, sizeof a, pos);
    fmtTime(b, sizeof b, g_s.durationMs);
    lv_label_set_text(g_elapsed, a);
    lv_label_set_text(g_total, b);
}

}  // namespace

void show() {
    build();
    lv_screen_load(g_scr);
}

void setDemo(bool on) {
    g_demo = on;
    g_seenVersion = UINT32_MAX;   // live mode redraws everything on the next tick
    g_shownIndex = INT32_MIN;
    if (!on) return;
    static const char *rows[LYRIC_ROWS] = {
        "Sous la pluie d'automne, les néons s'éteignent un à un",
        "« Où vas-tu ? » m'a-t-elle dit — je n'ai rien répondu…",
        "Et l'été s'en allait sur la Côte, à l'heure où l'on rêve encore d'Œdipe",
        "Żółw, façade, garçon, naïve : l'aube est déjà là",
        "Ça ira mieux demain",
    };
    lv_obj_set_hidden(g_setup, true);
    lv_obj_set_hidden(g_message, true);
    lv_obj_set_hidden(g_stage, false);
    for (int r = 0; r < LYRIC_ROWS; r++) lv_label_set_text(g_rows[r], rows[r]);
    lv_label_set_text(g_zone, "Living Room");
    lv_label_set_text(g_status, "Playing");
    lv_label_set_text(g_title, "Les Néons d'automne");
    lv_label_set_text(g_artist, "Jean‐Luc Démo  -  Démonstration");
    for (lv_obj_t *o : {g_bar, g_elapsed, g_total}) lv_obj_set_hidden(o, false);
    lv_bar_set_value(g_bar, 420, LV_ANIM_OFF);
    lv_label_set_text(g_elapsed, "1:38");
    lv_label_set_text(g_total, "3:54");
}

void tick() {
    if (g_demo) return;
    uint32_t v = net::version();
    if (v != g_seenVersion) {
        g_seenVersion = v;
        g_s = net::snapshot();
        applySnapshot();
    }
    uint32_t now = millis();
    tickLyrics(now);
    tickProgress(now);
}

}  // namespace ui
