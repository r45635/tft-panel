#include "ui.h"

#include <Arduino.h>
#include <esp_timer.h>
#include <lvgl.h>

#include "src/misc/cache/instance/lv_image_cache.h"   // lv_image_cache_drop (not in lvgl.h)

#include "../net/net.h"
#include "../net/ota.h"
#include "../net/settings.h"
#include "../board/board.h"
#include "fonts.h"
#include "settings_screen.h"
#include "../version.h"

#include <vector>

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
lv_obj_t *g_bg = nullptr;                      // cover backdrop, behind everything
lv_obj_t *g_thumb = nullptr;                   // cover thumbnail left of the title
std::shared_ptr<const Picture> g_shownBackdrop, g_shownThumb;
uint32_t g_seenCoverSerial = UINT32_MAX;
constexpr int TRACK_X = 24, TRACK_W = 752;     // title/artist box without a thumbnail
constexpr int THUMB_GAP = 14;

// Header
lv_obj_t *g_zone = nullptr, *g_status = nullptr;
// Track
lv_obj_t *g_title = nullptr, *g_artist = nullptr;
// Lyrics stage
lv_obj_t *g_stage = nullptr;     // fixed box, clips the sliding rows
lv_obj_t *g_rowsBox = nullptr;   // flex column of rows, translated while sliding
lv_obj_t *g_rows[LYRIC_ROWS] = {};
lv_obj_t *g_message = nullptr;   // centred message when there is nothing to sing
// Progress
lv_obj_t *g_bar = nullptr, *g_elapsed = nullptr, *g_total = nullptr;
// Two views, switched by a horizontal swipe (instantly: a full-screen frame costs ~130 ms,
// an animated wipe would only get 3-4 frames). Lyrics view = everything above in a
// transparent container; cover view = big cover, large title/artist, current lyric line.
enum class View : uint8_t { Lyrics, Cover };
constexpr int VIEW_COUNT = 2;   // swipes wrap around: left = next, right = previous
lv_obj_t *g_lyricsView = nullptr, *g_coverView = nullptr;
lv_obj_t *g_bigCover = nullptr, *g_cvTitle = nullptr, *g_cvArtist = nullptr, *g_cvAlbum = nullptr;
lv_obj_t *g_cvLyric = nullptr, *g_cvBar = nullptr, *g_cvElapsed = nullptr, *g_cvTotal = nullptr;
lv_obj_t *g_pageDots[VIEW_COUNT] = {};
std::shared_ptr<const Picture> g_shownBig;
View g_view = View::Lyrics;
// A swipe on a track without lyrics only holds for that track while it has none: it is never
// saved, so the panel never stays stuck on covers once lyrics show up.
String g_overrideTrack;
View g_overrideView = View::Lyrics;
// Setup overlay
lv_obj_t *g_setup = nullptr, *g_qrWifi = nullptr, *g_qrUrl = nullptr, *g_setupText = nullptr;
// Controls bar (shown on a tap, hides itself) and zone picker
constexpr uint32_t CONTROLS_HIDE_MS = 6000;
constexpr uint32_t COMMAND_ERROR_MS = 5000;
lv_obj_t *g_controls = nullptr, *g_btnZone = nullptr, *g_zoneBtnLabel = nullptr;
lv_obj_t *g_btnPrev = nullptr, *g_btnPlay = nullptr, *g_btnNext = nullptr, *g_playIcon = nullptr;
lv_obj_t *g_btnVolDown = nullptr, *g_btnVolUp = nullptr, *g_volLabel = nullptr;
lv_obj_t *g_picker = nullptr, *g_pickerList = nullptr;
std::vector<String> g_pickerIds;
uint32_t g_controlsShownAt = 0;
bool g_errorShown = false;

// Smooth scroll: the stage slides up from where the next line was to its new place.
// A full-stage frame costs ~88 ms (render ~68 + flush ~20, PSRAM-bound): ~11 fps. A longer
// slide gives smaller steps rather than more frames per second.
constexpr uint32_t SCROLL_MS = 400;
uint32_t g_scrollFrames = 0, g_scrollStartedAt = 0;
int32_t g_scrollFrom = 0;

// Screen sleep: dims after a minute without playback, backlight off after settings::sleepMin().
// A touch wakes it; the touch that wakes a dark screen does nothing else.
enum class Screen : uint8_t { On, Dimmed, Off };
constexpr uint32_t DIM_AFTER_MS = 60 * 1000;
constexpr uint8_t BRIGHT_DIM = 12;   // awake level: settings::brightness()
Screen g_screen = Screen::On;
uint32_t g_activeAt = 0;
int32_t g_backlight = 100;        // current level, animated
bool g_swallowTouch = false;      // the press that woke a dark screen, until its release
lv_indev_t *g_hookedIndev = nullptr;

// Firmware update: badge in the header when a release is available, confirmation dialog,
// full-screen progress while it installs.
lv_obj_t *g_otaBadge = nullptr, *g_otaBadgeLabel = nullptr;
lv_obj_t *g_otaConfirm = nullptr, *g_otaConfirmText = nullptr;
lv_obj_t *g_otaProgress = nullptr, *g_otaProgressText = nullptr, *g_otaBar = nullptr, *g_otaClose = nullptr;
uint32_t g_seenOtaVersion = UINT32_MAX;
ota::Status g_ota;

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

// Only touch LVGL when something changed: every set_text / style set invalidates the
// area, and each redraw is a CPU copy into the PSRAM framebuffer that competes with the
// RGB DMA refill (seen as flickering dots around labels refreshed 3x per second).
void setText(lv_obj_t *l, const char *t) {
    const char *cur = lv_label_get_text(l);
    if (!cur || strcmp(cur, t) != 0) lv_label_set_text(l, t);
}

void setTextColour(lv_obj_t *o, uint32_t colour) {
    lv_color_t c = lv_color_hex(colour);
    if (!lv_color_eq(lv_obj_get_style_text_color(o, LV_PART_MAIN), c)) lv_obj_set_style_text_color(o, c, 0);
}

void setHidden(lv_obj_t *o, bool hidden) {
    if (lv_obj_is_hidden(o) != hidden) lv_obj_set_hidden(o, hidden);
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
    setText(h, "Set up TFT Panel");
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
        setText(t, step);
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
    setHidden(g_setup, true);
}

bool isHidden(lv_obj_t *o) { return lv_obj_is_hidden(o); }

void touchControls() { g_controlsShownAt = millis(); }

void showControls(bool on) {
    setHidden(g_controls, !on);
    if (on) touchControls();
}

lv_obj_t *roundButton(lv_obj_t *parent, const char *symbol, lv_obj_t **iconOut, void (*cb)(lv_event_t *)) {
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, 76, 76);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x1C232B), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(COL_ACCENT), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(COL_TEXT), 0);
    setText(l, symbol);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    if (iconOut) *iconOut = l;
    return b;
}

void onPrev(lv_event_t *) { touchControls(); net::control("previous"); }
void onNext(lv_event_t *) { touchControls(); net::control("next"); }
void onPlay(lv_event_t *) {
    touchControls();
    // Optimistic icon: the next now-playing poll (≤ 300 ms) confirms or corrects it.
    bool playing = g_s.playing();
    setText(g_playIcon, playing ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE);
    net::control("playpause");
}
void onVolDown(lv_event_t *) { touchControls(); net::volumeStep(-1); }
void onVolUp(lv_event_t *) { touchControls(); net::volumeStep(+1); }

void closePicker() { setHidden(g_picker, true); }

void onPickZone(lv_event_t *e) {
    intptr_t i = (intptr_t)lv_event_get_user_data(e);
    if (i >= 0 && i < (intptr_t)g_pickerIds.size()) net::selectZone(g_pickerIds[i]);
    closePicker();
    showControls(false);
}

void fillPicker() {
    lv_obj_clean(g_pickerList);
    g_pickerIds.clear();
    auto add = [](const String &id, const String &text, bool current) {
        lv_obj_t *b = lv_button_create(g_pickerList);
        lv_obj_set_width(b, lv_pct(100));
        lv_obj_set_style_radius(b, 10, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x161B22), 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x24303C), LV_STATE_PRESSED);
        lv_obj_set_style_pad_ver(b, 16, 0);
        lv_obj_t *l = label(b, fonts::ui20, current ? COL_ACCENT : COL_TEXT);
        lv_obj_set_width(l, lv_pct(100));
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
        setText(l, ((current ? String(LV_SYMBOL_OK "  ") : String("     ")) + text).c_str());
        lv_obj_add_event_cb(b, onPickZone, LV_EVENT_CLICKED, (void *)(intptr_t)g_pickerIds.size());
        g_pickerIds.push_back(id);
    };
    add("", "Follow the playing zone (auto)", !g_s.zonePinned);
    if (!g_s.zones) return;
    for (const net::ZoneInfo &z : *g_s.zones) {
        String t = z.name + "  -  " + z.state;
        if (z.title.length()) t += "  -  " + z.title + (z.artist.length() ? " (" + z.artist + ")" : "");
        add(z.id, t, g_s.zonePinned && z.id == g_s.zoneId);
    }
}

void onZone(lv_event_t *) {
    fillPicker();
    setHidden(g_picker, false);
}

void buildControls() {
    g_controls = lv_obj_create(g_scr);
    lv_obj_remove_style_all(g_controls);
    lv_obj_set_size(g_controls, 800, 112);
    lv_obj_align(g_controls, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(g_controls, lv_color_hex(0x0A0D11), 0);
    lv_obj_set_style_bg_opa(g_controls, LV_OPA_90, 0);
    lv_obj_set_scrollable(g_controls, false);
    lv_obj_set_flex_flow(g_controls, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_controls, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    g_btnZone = lv_button_create(g_controls);
    lv_obj_set_size(g_btnZone, 150, 64);
    lv_obj_set_style_radius(g_btnZone, 32, 0);
    lv_obj_set_style_bg_color(g_btnZone, lv_color_hex(0x1C232B), 0);
    lv_obj_set_style_bg_color(g_btnZone, lv_color_hex(COL_ACCENT), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(g_btnZone, 0, 0);
    g_zoneBtnLabel = label(g_btnZone, fonts::ui16, COL_TEXT);
    lv_obj_set_width(g_zoneBtnLabel, 124);
    lv_label_set_long_mode(g_zoneBtnLabel, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(g_zoneBtnLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(g_zoneBtnLabel);
    lv_obj_add_event_cb(g_btnZone, onZone, LV_EVENT_CLICKED, nullptr);

    g_btnPrev = roundButton(g_controls, LV_SYMBOL_PREV, nullptr, onPrev);
    g_btnPlay = roundButton(g_controls, LV_SYMBOL_PLAY, &g_playIcon, onPlay);
    g_btnNext = roundButton(g_controls, LV_SYMBOL_NEXT, nullptr, onNext);
    g_btnVolDown = roundButton(g_controls, LV_SYMBOL_MINUS, nullptr, onVolDown);
    g_volLabel = label(g_controls, fonts::ui20, COL_MID);
    lv_obj_set_width(g_volLabel, 80);
    lv_obj_set_style_text_align(g_volLabel, LV_TEXT_ALIGN_CENTER, 0);
    g_btnVolUp = roundButton(g_controls, LV_SYMBOL_PLUS, nullptr, onVolUp);
    roundButton(g_controls, LV_SYMBOL_SETTINGS, nullptr, [](lv_event_t *) {
        showControls(false);
        settingsui::open();
        settingsui::refresh(g_s, g_ota);
    });
    setHidden(g_controls, true);

    // Zone picker: full-screen list.
    g_picker = lv_obj_create(g_scr);
    lv_obj_remove_style_all(g_picker);
    lv_obj_set_size(g_picker, 800, 480);
    lv_obj_set_style_bg_color(g_picker, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(g_picker, LV_OPA_COVER, 0);
    lv_obj_t *h = label(g_picker, fonts::lyricsOther, COL_TEXT);
    setText(h, "Zone");
    lv_obj_set_pos(h, 32, 20);
    lv_obj_t *close = roundButton(g_picker, LV_SYMBOL_CLOSE, nullptr, [](lv_event_t *) { closePicker(); });
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, -20, 12);
    g_pickerList = lv_obj_create(g_picker);
    lv_obj_remove_style_all(g_pickerList);
    lv_obj_set_size(g_pickerList, 760, 370);
    lv_obj_set_pos(g_pickerList, 20, 100);
    lv_obj_set_flex_flow(g_pickerList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(g_pickerList, 8, 0);
    lv_obj_set_scroll_dir(g_pickerList, LV_DIR_VER);
    setHidden(g_picker, true);
}

void onScreenTap(lv_event_t *) {
    if (g_s.portalActive || !isHidden(g_picker)) return;
    showControls(isHidden(g_controls));   // tap toggles the bar
}

void updateControls() {
    setText(g_zoneBtnLabel, g_s.zoneName.length()
                                          ? (String(LV_SYMBOL_LIST " ") + g_s.zoneName).c_str()
                                          : LV_SYMBOL_LIST " Zone");
    bool track = g_s.phase == net::Phase::Playing;
    setText(g_playIcon, g_s.playing() ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    lv_obj_set_state(g_btnPlay, LV_STATE_DISABLED, !track || !(g_s.canPlay || g_s.canPause));
    lv_obj_set_state(g_btnPrev, LV_STATE_DISABLED, !track || !g_s.canPrevious);
    lv_obj_set_state(g_btnNext, LV_STATE_DISABLED, !track || !g_s.canNext);

    const net::OutputInfo *vol = nullptr;
    for (const net::OutputInfo &o : g_s.outputs)
        if (o.volumeType.length()) { vol = &o; break; }
    lv_obj_set_state(g_btnVolDown, LV_STATE_DISABLED, !vol);
    lv_obj_set_state(g_btnVolUp, LV_STATE_DISABLED, !vol);
    char buf[24] = "";
    if (vol && vol->muted) snprintf(buf, sizeof buf, "muted");
    else if (vol && vol->volumeType == "db") snprintf(buf, sizeof buf, "%.1f dB", vol->value);
    else if (vol && vol->volumeType == "number") snprintf(buf, sizeof buf, "%.0f", vol->value);
    setText(g_volLabel, buf);
}

constexpr int CV_COVER_X = 40, CV_COVER_Y = 56;
constexpr int CV_COL_X = CV_COVER_X + net::COVER_BIG_PX + 32, CV_COL_W = 800 - 24 - CV_COL_X;

lv_obj_t *boundedLabel(lv_obj_t *parent, const lv_font_t *font, uint32_t colour, int maxLines) {
    lv_obj_t *l = label(parent, font, colour);
    lv_obj_set_width(l, CV_COL_W);
    lv_obj_set_style_max_height(l, maxLines * lv_font_get_line_height(font), 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    return l;
}

lv_obj_t *progressBar(lv_obj_t *parent) {
    lv_obj_t *b = lv_bar_create(parent);
    lv_bar_set_range(b, 0, 1000);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x2A323C), LV_PART_MAIN);
    lv_obj_set_style_bg_color(b, lv_color_hex(COL_ACCENT), LV_PART_INDICATOR);
    return b;
}

void buildCoverView() {
    g_bigCover = lv_image_create(g_coverView);
    lv_obj_set_pos(g_bigCover, CV_COVER_X, CV_COVER_Y);
    lv_obj_set_size(g_bigCover, net::COVER_BIG_PX, net::COVER_BIG_PX);
    lv_image_set_inner_align(g_bigCover, LV_IMAGE_ALIGN_CENTER);
    lv_obj_set_clickable(g_bigCover, false);
    lv_obj_set_hidden(g_bigCover, true);

    // Right column, top to bottom in a flex column so wrapped titles push the rest down.
    // Budget (348 px): title 2 x 60 + artist 2 x 41 + album 27 + lyric 2 x 27 + gaps fits.
    int barY = CV_COVER_Y + net::COVER_BIG_PX - 26;
    lv_obj_t *col = lv_obj_create(g_coverView);
    lv_obj_remove_style_all(col);
    lv_obj_set_pos(col, CV_COL_X, CV_COVER_Y);
    lv_obj_set_size(col, CV_COL_W, barY - 10 - CV_COVER_Y);
    lv_obj_set_scrollable(col, false);
    lv_obj_set_clickable(col, false);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 10, 0);
    g_cvTitle = boundedLabel(col, fonts::lyricsCurrent, COL_TEXT, 2);
    g_cvArtist = boundedLabel(col, fonts::lyricsOther, COL_MID, 2);
    g_cvAlbum = boundedLabel(col, fonts::ui20, COL_DIM, 1);
    lv_obj_t *gap = lv_obj_create(col);   // breathing space before the lyric line
    lv_obj_remove_style_all(gap);
    lv_obj_set_size(gap, 1, 12);
    g_cvLyric = boundedLabel(col, fonts::ui20, COL_MID, 2);

    g_cvBar = progressBar(g_coverView);
    lv_obj_set_size(g_cvBar, CV_COL_W, 4);
    lv_obj_set_pos(g_cvBar, CV_COL_X, barY);
    g_cvElapsed = label(g_coverView, fonts::ui16, COL_DIM);
    lv_obj_set_pos(g_cvElapsed, CV_COL_X, barY + 10);
    g_cvTotal = label(g_coverView, fonts::ui16, COL_DIM);
    lv_obj_set_width(g_cvTotal, 80);
    lv_obj_set_style_text_align(g_cvTotal, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(g_cvTotal, CV_COL_X + CV_COL_W - 80, barY + 10);
}

void buildPageDots() {
    for (int i = 0; i < VIEW_COUNT; i++) {
        lv_obj_t *d = lv_obj_create(g_scr);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, 8, 8);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(d, lv_color_hex(COL_DIM), 0);
        lv_obj_set_pos(d, 400 - VIEW_COUNT * 7 + 3 + i * 14, 18);
        lv_obj_set_clickable(d, false);
        g_pageDots[i] = d;
    }
}

void showView(View v) {
    g_view = v;
    setHidden(g_lyricsView, v != View::Lyrics);
    setHidden(g_coverView, v != View::Cover);
    for (int i = 0; i < VIEW_COUNT; i++) {
        lv_color_t c = lv_color_hex(i == (int)v ? COL_TEXT : COL_DIM);
        if (!lv_color_eq(lv_obj_get_style_bg_color(g_pageDots[i], LV_PART_MAIN), c))
            lv_obj_set_style_bg_color(g_pageDots[i], c, 0);
    }
    g_lastProgress = 0;   // refresh the visible bar now
}

String trackKey() { return g_s.title + "\n" + g_s.artist; }

bool hasTrack() {
    bool bad = g_s.phase == net::Phase::WifiFailed || g_s.phase == net::Phase::CompanionError;
    return g_s.phase == net::Phase::Playing || (g_s.title.length() && bad);
}

bool noLyrics() {
    return g_s.lyricsStatus == net::LyricsStatus::NotFound || g_s.lyricsStatus == net::LyricsStatus::Instrumental ||
           g_s.lyricsStatus == net::LyricsStatus::Error;
}

// No track → lyrics view (it carries the status message). A track without lyrics → cover view,
// or what the user swiped to on this track. Lyrics found or loading → the saved view.
View wantedView() {
    if (!hasTrack()) return View::Lyrics;
    if (noLyrics()) return g_overrideTrack == trackKey() ? g_overrideView : View::Cover;
    return settings::view() ? View::Cover : View::Lyrics;
}

void applyView() {
    if (g_demo) return;
    View v = wantedView();
    if (v != g_view) showView(v);
}

// A user choice (swipe or console). Saved only when the track has lyrics: that is a real
// choice between lyrics and cover, not a way around the automatic cover view.
void chooseView(View v) {
    if (hasTrack() && noLyrics() && !g_demo) {
        g_overrideTrack = trackKey();
        g_overrideView = v;
    } else {
        settings::setView(v == View::Cover ? 1 : 0);
    }
    showView(v);
}

void onGesture(lv_event_t *) {
    lv_indev_t *indev = lv_indev_active();
    if (!indev || g_s.portalActive || !isHidden(g_picker) || settingsui::isOpen()) return;
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir != LV_DIR_LEFT && dir != LV_DIR_RIGHT) return;
    // No click at the end of a swipe, on the screen or on a button the finger started on.
    lv_indev_wait_release(indev);
    if (!hasTrack() && !g_demo) return;
    int step = dir == LV_DIR_LEFT ? 1 : VIEW_COUNT - 1;
    chooseView((View)(((int)g_view + step) % VIEW_COUNT));
}

void backlightExec(void *, int32_t v);

lv_obj_t *textButton(lv_obj_t *parent, const char *text, uint32_t bg, void (*cb)(lv_event_t *)) {
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_style_radius(b, 28, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(COL_ACCENT), LV_STATE_PRESSED);
    lv_obj_set_style_pad_hor(b, 28, 0);
    lv_obj_set_style_pad_ver(b, 14, 0);
    lv_obj_t *l = label(b, fonts::ui20, COL_TEXT);
    setText(l, text);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    return b;
}

lv_obj_t *fullScreen(lv_opa_t opa) {
    lv_obj_t *o = lv_obj_create(g_scr);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, 800, 480);
    lv_obj_set_style_bg_color(o, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_clickable(o, true);   // modal: nothing underneath gets the touch
    lv_obj_set_hidden(o, true);
    return o;
}

void buildOta() {
    g_otaBadge = lv_button_create(g_scr);
    lv_obj_set_height(g_otaBadge, 30);
    lv_obj_set_style_radius(g_otaBadge, 15, 0);
    lv_obj_set_style_shadow_width(g_otaBadge, 0, 0);
    lv_obj_set_style_pad_hor(g_otaBadge, 14, 0);
    lv_obj_set_style_pad_ver(g_otaBadge, 0, 0);
    lv_obj_set_style_bg_color(g_otaBadge, lv_color_hex(COL_ACCENT), 0);
    lv_obj_set_pos(g_otaBadge, 430, 7);
    g_otaBadgeLabel = label(g_otaBadge, fonts::ui16, 0x06121A);
    lv_obj_center(g_otaBadgeLabel);
    lv_obj_add_event_cb(g_otaBadge, [](lv_event_t *) { setHidden(g_otaConfirm, false); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_set_hidden(g_otaBadge, true);

    g_otaConfirm = fullScreen(LV_OPA_90);
    g_otaConfirmText = label(g_otaConfirm, fonts::ui20, COL_TEXT);
    lv_obj_set_width(g_otaConfirmText, 620);
    lv_label_set_long_mode(g_otaConfirmText, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(g_otaConfirmText, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_otaConfirmText, LV_ALIGN_CENTER, 0, -60);
    lv_obj_t *install = textButton(g_otaConfirm, "Install now", 0x1F6F9E, [](lv_event_t *) {
        setHidden(g_otaConfirm, true);
        ota::requestInstall();
    });
    lv_obj_align(install, LV_ALIGN_CENTER, -110, 90);
    lv_obj_t *later = textButton(g_otaConfirm, "Later", 0x1C232B, [](lv_event_t *) { setHidden(g_otaConfirm, true); });
    lv_obj_align(later, LV_ALIGN_CENTER, 110, 90);

    g_otaProgress = fullScreen(LV_OPA_COVER);
    g_otaProgressText = label(g_otaProgress, fonts::lyricsOther, COL_TEXT);
    lv_obj_set_width(g_otaProgressText, 680);
    lv_label_set_long_mode(g_otaProgressText, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(g_otaProgressText, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_otaProgressText, LV_ALIGN_CENTER, 0, -50);
    g_otaBar = progressBar(g_otaProgress);
    lv_bar_set_range(g_otaBar, 0, 100);
    lv_obj_set_size(g_otaBar, 520, 8);
    lv_obj_align(g_otaBar, LV_ALIGN_CENTER, 0, 40);
    g_otaClose = textButton(g_otaProgress, "Close", 0x1C232B, [](lv_event_t *) { setHidden(g_otaProgress, true); });
    lv_obj_align(g_otaClose, LV_ALIGN_CENTER, 0, 120);
}

void applyOta() {
    bool available = g_ota.state == ota::State::Available;
    setHidden(g_otaBadge, !available || g_s.portalActive);
    if (available) {
        setText(g_otaBadgeLabel, (String(LV_SYMBOL_DOWNLOAD " Update ") + g_ota.latest).c_str());
        setText(g_otaConfirmText, ("Install TFT Panel " + g_ota.latest + "?\n\nIt downloads in about a minute, then the panel "
                                   "restarts. If the new version does not start properly, the panel goes back to " +
                                   String(FW_VERSION) + " by itself.").c_str());
    }
    bool busy = g_ota.state == ota::State::Downloading || g_ota.state == ota::State::Installed;
    bool failedNow = g_ota.state == ota::State::Failed && !isHidden(g_otaProgress);
    if (busy) {
        setHidden(g_otaProgress, false);
        setHidden(g_otaClose, true);
        setHidden(g_otaBar, false);
        setText(g_otaProgressText, g_ota.state == ota::State::Installed
                                       ? ("Installed " + g_ota.latest + ", restarting...").c_str()
                                       : ("Updating to " + g_ota.latest + "...  " + String(g_ota.percent) + " %").c_str());
        lv_bar_set_value(g_otaBar, g_ota.percent, LV_ANIM_OFF);
    } else if (failedNow) {
        setHidden(g_otaBar, true);
        setHidden(g_otaClose, false);
        setText(g_otaProgressText, ("Update failed: " + g_ota.error + "\n\nStill running " + String(FW_VERSION) + ".").c_str());
    }
}

void build() {
    g_scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(g_scr, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_text_color(g_scr, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_scrollable(g_scr, false);

    g_bg = lv_image_create(g_scr);
    lv_obj_set_pos(g_bg, 0, 0);
    lv_obj_set_size(g_bg, 800, 480);
    lv_obj_set_clickable(g_bg, false);
    lv_obj_set_hidden(g_bg, true);

    g_zone = label(g_scr, fonts::ui16, COL_MID);
    lv_obj_set_pos(g_zone, 24, 12);
    g_status = label(g_scr, fonts::ui16, COL_DIM);
    lv_obj_set_width(g_status, 460);
    lv_obj_set_style_text_align(g_status, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(g_status, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(g_status, LV_ALIGN_TOP_RIGHT, -24, 12);

    auto viewBox = []() {
        lv_obj_t *v = lv_obj_create(g_scr);
        lv_obj_remove_style_all(v);
        lv_obj_set_size(v, 800, 480);
        lv_obj_set_scrollable(v, false);
        lv_obj_set_clickable(v, false);   // taps and swipes reach the screen
        return v;
    };
    g_lyricsView = viewBox();

    g_thumb = lv_image_create(g_lyricsView);
    lv_obj_set_pos(g_thumb, TRACK_X, 37);
    lv_obj_set_size(g_thumb, net::COVER_THUMB_PX, net::COVER_THUMB_PX);
    lv_image_set_inner_align(g_thumb, LV_IMAGE_ALIGN_CENTER);
    lv_obj_set_clickable(g_thumb, false);
    lv_obj_set_hidden(g_thumb, true);

    g_title = label(g_lyricsView, fonts::ui20, COL_TEXT);
    lv_obj_set_width(g_title, TRACK_W);
    lv_label_set_long_mode(g_title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(g_title, TRACK_X, 40);
    g_artist = label(g_lyricsView, fonts::ui16, COL_MID);
    lv_obj_set_width(g_artist, TRACK_W);
    lv_label_set_long_mode(g_artist, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(g_artist, TRACK_X, 68);

    g_stage = lv_obj_create(g_lyricsView);
    lv_obj_remove_style_all(g_stage);
    lv_obj_set_size(g_stage, 760, 340);
    lv_obj_set_pos(g_stage, 20, 98);
    lv_obj_set_scrollable(g_stage, false);
    g_rowsBox = lv_obj_create(g_stage);
    lv_obj_remove_style_all(g_rowsBox);
    lv_obj_set_size(g_rowsBox, lv_pct(100), lv_pct(100));
    lv_obj_set_scrollable(g_rowsBox, false);
    lv_obj_set_clickable(g_rowsBox, false);
    lv_obj_set_flex_flow(g_rowsBox, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_rowsBox, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(g_rowsBox, 10, 0);
    // Fixed budget so the stage never overflows: neighbours on one line, the current line on
    // at most two; longer text ends with "…". 4 x 41 + 2 x 60 + gaps fits the 340 px stage.
    for (int i = 0; i < LYRIC_ROWS; i++) {
        bool cur = i == CURRENT_ROW;
        const lv_font_t *font = cur ? fonts::lyricsCurrent : fonts::lyricsOther;
        lv_obj_t *l = label(g_rowsBox, font, cur ? COL_TEXT : COL_DIM);
        int32_t lh = lv_font_get_line_height(font);
        lv_obj_set_width(l, 760);
        lv_obj_set_height(l, LV_SIZE_CONTENT);
        lv_obj_set_style_max_height(l, cur ? 2 * lh : lh, 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        g_rows[i] = l;
    }

    g_message = label(g_lyricsView, fonts::lyricsOther, COL_MID);
    lv_obj_set_width(g_message, 700);
    lv_label_set_long_mode(g_message, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(g_message, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_message, LV_ALIGN_CENTER, 0, 10);

    g_bar = lv_bar_create(g_lyricsView);
    lv_obj_set_size(g_bar, 640, 4);
    lv_obj_align(g_bar, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_bar_set_range(g_bar, 0, 1000);
    lv_obj_set_style_bg_color(g_bar, lv_color_hex(0x2A323C), LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_bar, lv_color_hex(COL_ACCENT), LV_PART_INDICATOR);
    g_elapsed = label(g_lyricsView, fonts::ui16, COL_DIM);
    lv_obj_align(g_elapsed, LV_ALIGN_BOTTOM_LEFT, 24, -10);
    g_total = label(g_lyricsView, fonts::ui16, COL_DIM);
    lv_obj_align(g_total, LV_ALIGN_BOTTOM_RIGHT, -24, -10);

    g_coverView = viewBox();
    buildCoverView();
    buildPageDots();

    buildSetup();
    buildControls();
    settingsui::build(g_scr, settingsui::Hooks{
        [](int percent) {   // live preview while the slider moves (the screen is awake)
            lv_anim_delete(&g_backlight, backlightExec);
            g_backlight = percent;
            board::setBacklight((uint8_t)percent);
        },
        []() { onZone(nullptr); },
        []() { setHidden(g_otaConfirm, false); },
    });
    buildOta();
    for (lv_obj_t *o : {g_stage, g_setup}) lv_obj_set_clickable(o, false);
    lv_obj_add_event_cb(g_scr, onScreenTap, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(g_scr, onGesture, LV_EVENT_GESTURE, nullptr);
    showView(settings::view() ? View::Cover : View::Lyrics);
}

void setRows(int index) {
    const lyrics::Lyrics *l = g_s.lyrics.get();
    for (int r = 0; r < LYRIC_ROWS; r++) {
        int i = index - CURRENT_ROW + r;
        const char *text = "";
        if (l && i >= 0 && i < (int)l->lines.size()) text = l->lines[i].text.c_str();
        // Before the first line, show the first lines dimmed below an empty current row.
        setText(g_rows[r], text);
        if (r == CURRENT_ROW) setText(g_cvLyric, text);
    }
}

void showMessage(const String &msg, uint32_t colour) {
    setHidden(g_stage, true);
    setHidden(g_message, false);
    setTextColour(g_message, colour);
    setText(g_message, msg.c_str());
}

// Swaps an image's source; the old buffer is freed with the last shared_ptr, so LVGL
// must not keep a cached reference to it.
void swapPicture(lv_obj_t *img, std::shared_ptr<const Picture> &shown, const std::shared_ptr<const Picture> &next) {
    if (shown == next) return;
    std::shared_ptr<const Picture> old = shown;
    shown = next;
    if (shown) {
        lv_image_set_src(img, &shown->dsc);
        setHidden(img, false);
    } else {
        setHidden(img, true);
        lv_image_set_src(img, nullptr);
    }
    if (old) lv_image_cache_drop(&old->dsc);
}

void placeTrackText(bool withThumb) {
    int x = withThumb ? TRACK_X + net::COVER_THUMB_PX + THUMB_GAP : TRACK_X;
    if (lv_obj_get_x(g_title) == x) return;
    int w = TRACK_W - (x - TRACK_X);
    lv_obj_set_pos(g_title, x, 40);
    lv_obj_set_width(g_title, w);
    lv_obj_set_pos(g_artist, x, 68);
    lv_obj_set_width(g_artist, w);
}

void applyCover(bool track) {
    if (g_s.coverSerial != g_seenCoverSerial) {
        g_seenCoverSerial = g_s.coverSerial;
        swapPicture(g_bg, g_shownBackdrop, g_s.backdrop);
        swapPicture(g_thumb, g_shownThumb, g_s.thumb);
        swapPicture(g_bigCover, g_shownBig, g_s.bigCover);
    }
    bool thumb = track && g_shownThumb;
    setHidden(g_thumb, !thumb);
    placeTrackText(thumb);
}

void applySnapshot() {
    // Setup overlay
    bool setup = g_s.portalActive;
    if (setup) applyCover(false);
    setHidden(g_setup, !setup);
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
        setText(g_setupText, t.c_str());
        return;
    }

    setText(g_zone, g_s.zoneName.length()
                                  ? (g_s.zoneName + (g_s.zonePinned ? "" : "  (auto)")).c_str()
                                  : "");
    bool bad = g_s.phase == net::Phase::WifiFailed || g_s.phase == net::Phase::CompanionError;
    g_errorShown = g_s.commandError.length() && millis() - g_s.commandErrorAt < COMMAND_ERROR_MS;
    setTextColour(g_status, bad || g_errorShown ? COL_ERROR : COL_DIM);
    setText(g_status, g_errorShown ? g_s.commandError.c_str() : g_s.status.c_str());
    updateControls();

    bool track = g_s.phase == net::Phase::Playing || (g_s.title.length() && bad);
    applyCover(track);
    setText(g_title, track ? g_s.title.c_str() : "");
    setText(g_artist, track ? (g_s.artist + (g_s.album.length() ? "  -  " + g_s.album : "")).c_str() : "");
    setText(g_cvTitle, track ? g_s.title.c_str() : "");
    setText(g_cvArtist, track ? g_s.artist.c_str() : "");
    setText(g_cvAlbum, track ? g_s.album.c_str() : "");
    if (g_s.lyricsStatus != net::LyricsStatus::Found) setText(g_cvLyric, "");
    applyView();
    setHidden(g_bar, !track);
    setHidden(g_elapsed, !track);
    setHidden(g_total, !track);

    if (!track) {
        showMessage(g_s.status, bad ? COL_ERROR : COL_MID);
        return;
    }
    switch (g_s.lyricsStatus) {
        case net::LyricsStatus::Found:
            setHidden(g_message, true);
            setHidden(g_stage, false);
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

int32_t rowCentre(lv_obj_t *row) { return lv_obj_get_y(row) + lv_obj_get_height(row) / 2; }

void scrollExec(void *obj, int32_t v) {
    lv_obj_set_style_translate_y((lv_obj_t *)obj, v, 0);
    // The incoming last line fades in as it slides into the clipped stage.
    lv_opa_t opa = g_scrollFrom > 0 ? (lv_opa_t)(255 - 255 * v / g_scrollFrom) : LV_OPA_COVER;
    lv_obj_set_style_opa(g_rows[LYRIC_ROWS - 1], opa, 0);
    g_scrollFrames++;
}

// Profiling of the slides: frames, refresh time (render + flush), flush time.
uint32_t g_refrUs = 0, g_refrCount = 0, g_flushCount0 = 0, g_flushUs0 = 0;
int64_t g_refrStart = 0;

void onRefr(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_REFR_START) g_refrStart = esp_timer_get_time();
    else if (g_refrStart) {
        g_refrUs += (uint32_t)(esp_timer_get_time() - g_refrStart);
        g_refrCount++;
    }
}

void scrollDone(lv_anim_t *) {
    static uint8_t logged = 0;
    if (logged >= 3) return;   // a few samples per boot, for tuning
    logged++;
    uint32_t ms = millis() - g_scrollStartedAt, fc, fus;
    board::flushStats(fc, fus);
    Serial.printf("[ui] scroll: %u steps in %u ms, %u refreshes %u ms, %u flushes %u ms\n", (unsigned)g_scrollFrames,
                  (unsigned)ms, (unsigned)g_refrCount, (unsigned)(g_refrUs / 1000), (unsigned)(fc - g_flushCount0),
                  (unsigned)((fus - g_flushUs0) / 1000));
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
    // One line forward: slide. A seek, a new track or going back: jump.
    bool slide = settings::scroll() && g_shownIndex != INT32_MIN && index == g_shownIndex + 1;
    int32_t before = 0;
    if (slide) {
        lv_obj_update_layout(g_stage);
        before = rowCentre(g_rows[CURRENT_ROW + 1]);   // where the new current line is now
    }
    g_shownIndex = index;
    setRows(index);
    if (!slide) {
        lv_anim_delete(g_rowsBox, scrollExec);
        if (lv_obj_get_style_translate_y(g_rowsBox, LV_PART_MAIN) != 0) scrollExec(g_rowsBox, 0);
        return;
    }
    lv_obj_update_layout(g_stage);
    int32_t from = before - rowCentre(g_rows[CURRENT_ROW]) + lv_obj_get_style_translate_y(g_rowsBox, LV_PART_MAIN);
    lv_anim_delete(g_rowsBox, scrollExec);
    if (from <= 0) {
        scrollExec(g_rowsBox, 0);
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, g_rowsBox);
    lv_anim_set_exec_cb(&a, scrollExec);
    lv_anim_set_values(&a, from, 0);
    lv_anim_set_duration(&a, SCROLL_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&a, scrollDone);
    g_scrollFrom = from;
    scrollExec(g_rowsBox, from);
    g_scrollFrames = 0;
    g_scrollStartedAt = millis();
    g_refrUs = g_refrCount = 0;
    board::flushStats(g_flushCount0, g_flushUs0);
    lv_anim_start(&a);
}

void tickProgress(uint32_t now) {
    if (g_s.portalActive || !g_s.durationMs || now - g_lastProgress < 250) return;
    g_lastProgress = now;
    int32_t pos = g_s.positionAt(now);
    // Only the visible view: hidden widgets would still cost a redraw when shown again anyway.
    bool cover = g_view == View::Cover;
    lv_bar_set_value(cover ? g_cvBar : g_bar, (int32_t)((int64_t)pos * 1000 / g_s.durationMs), LV_ANIM_OFF);
    char a[12], b[12];
    fmtTime(a, sizeof a, pos);
    fmtTime(b, sizeof b, g_s.durationMs);
    setText(cover ? g_cvElapsed : g_elapsed, a);
    setText(cover ? g_cvTotal : g_total, b);
}

void backlightExec(void *, int32_t v) {
    g_backlight = v;
    board::setBacklight((uint8_t)v);
}

void fadeBacklight(int32_t to, uint32_t ms) {
    lv_anim_delete(&g_backlight, backlightExec);
    if (g_backlight == to) return;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &g_backlight);
    lv_anim_set_exec_cb(&a, backlightExec);
    lv_anim_set_values(&a, g_backlight, to);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_completed_cb(&a, [](lv_anim_t *) { Serial.printf("[ui] backlight %d %%\n", (int)g_backlight); });
    lv_anim_start(&a);
}

// Every touch keeps the screen awake. The one that lands on a dark screen only wakes it:
// the indev sees the event before any object, and stops it there until the release.
void onIndevEvent(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    g_activeAt = millis();
    if (code == LV_EVENT_PRESSED && g_screen == Screen::Off) g_swallowTouch = true;
    if (g_swallowTouch) {
        lv_indev_stop_processing(lv_indev_active());
        if (code == LV_EVENT_RELEASED) g_swallowTouch = false;
    }
}

void hookTouch() {
    if (g_hookedIndev) return;
    lv_indev_t *indev = lv_indev_get_next(nullptr);   // touch may come up late (console touchdiag)
    if (!indev) return;
    g_hookedIndev = indev;
    for (lv_event_code_t c : {LV_EVENT_PRESSED, LV_EVENT_RELEASED, LV_EVENT_CLICKED, LV_EVENT_SHORT_CLICKED,
                              LV_EVENT_LONG_PRESSED, LV_EVENT_LONG_PRESSED_REPEAT})
        lv_indev_add_event_cb(indev, onIndevEvent, c, nullptr);
}

void tickSleep(uint32_t now) {
    hookTouch();
    bool busy = g_demo || g_s.portalActive || g_s.playing() || !isHidden(g_picker) || g_swallowTouch ||
                !isHidden(g_otaConfirm) || !isHidden(g_otaProgress) || settingsui::isOpen();
    if (busy) g_activeAt = now;
    uint32_t idle = now - g_activeAt;
    uint32_t offAfter = (uint32_t)settings::sleepMin() * 60000u;
    Screen want = Screen::On;
    if (idle >= DIM_AFTER_MS) want = Screen::Dimmed;
    // Without touch nothing could wake it but playback: dim only.
    if (offAfter && idle >= offAfter && g_hookedIndev) want = Screen::Off;
    if (want == g_screen) return;
    Serial.printf("[ui] screen %s\n", want == Screen::On ? "on" : want == Screen::Dimmed ? "dimmed" : "off");
    if (want == Screen::Off) {
        showControls(false);
        closePicker();
        settingsui::close();
    }
    g_screen = want;
    fadeBacklight(want == Screen::On ? settings::brightness() : want == Screen::Dimmed ? BRIGHT_DIM : 0,
                  want == Screen::On ? 250 : 1500);
}

}  // namespace

void setView(bool cover) { chooseView(cover ? View::Cover : View::Lyrics); }

const char *viewName() { return g_view == View::Cover ? "cover" : "lyrics"; }

const char *screenState() {
    return g_screen == Screen::On ? "on" : g_screen == Screen::Dimmed ? "dimmed" : "off";
}

void show() {
    build();
    lv_screen_load(g_scr);
    g_activeAt = millis();
    g_backlight = settings::brightness();
    settingsui::applyTz();
    lv_display_add_event_cb(board::display(), onRefr, LV_EVENT_REFR_START, nullptr);
    lv_display_add_event_cb(board::display(), onRefr, LV_EVENT_REFR_READY, nullptr);
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
    setHidden(g_setup, true);
    setHidden(g_message, true);
    setHidden(g_stage, false);
    for (int r = 0; r < LYRIC_ROWS; r++) setText(g_rows[r], rows[r]);
    setText(g_zone, "Living Room");
    setText(g_status, "Playing");
    setText(g_title, "Les Néons d'automne");
    setText(g_artist, "Jean‐Luc Démo  -  Démonstration");
    for (lv_obj_t *o : {g_bar, g_elapsed, g_total}) setHidden(o, false);
    lv_bar_set_value(g_bar, 420, LV_ANIM_OFF);
    setText(g_elapsed, "1:38");
    setText(g_total, "3:54");
    setText(g_cvTitle, "Les Néons d'automne (version longue enregistrée à l'aube)");
    setText(g_cvArtist, "Jean‐Luc Démo & l'Orchestre de la Côte");
    setText(g_cvAlbum, "Démonstration");
    setText(g_cvLyric, rows[CURRENT_ROW]);
    lv_bar_set_value(g_cvBar, 420, LV_ANIM_OFF);
    setText(g_cvElapsed, "1:38");
    setText(g_cvTotal, "3:54");
}

void tick() {
    uint32_t now = millis();
    tickSleep(now);
    if (g_demo) return;
    uint32_t v = net::version();
    if (v != g_seenVersion) {
        g_seenVersion = v;
        g_s = net::snapshot();
        applySnapshot();
        g_seenOtaVersion = UINT32_MAX;   // badge visibility depends on the portal state
    }
    static uint32_t lastRefresh = 0;
    uint32_t ov = ota::version();
    if (ov != g_seenOtaVersion) {
        g_seenOtaVersion = ov;
        g_ota = ota::status();
        applyOta();
        settingsui::refresh(g_s, g_ota);
    }
    if (now - lastRefresh > 1000) {   // the about row shows live memory
        lastRefresh = now;
        settingsui::refresh(g_s, g_ota);
    }
    if (!isHidden(g_controls) && isHidden(g_picker) && now - g_controlsShownAt > CONTROLS_HIDE_MS)
        showControls(false);
    if (g_errorShown && now - g_s.commandErrorAt >= COMMAND_ERROR_MS) applySnapshot();
    tickLyrics(now);
    tickProgress(now);
}

}  // namespace ui
