#include "ui.h"

#include <Arduino.h>
#include <esp_timer.h>
#include <lvgl.h>

#include "src/misc/cache/instance/lv_image_cache.h"   // lv_image_cache_drop (not in lvgl.h)

#include "../net/net.h"
#include "../net/ota.h"
#include "../net/web_update.h"
#include "../net/settings.h"
#include "../board/board.h"
#include "../board/board_pins.h"
#include "../layout.h"
#include "fonts.h"
#include "settings_screen.h"
#include "wifi_screen.h"
#include "showcase.h"
#include "../version.h"

#include <vector>

extern "C" const lv_image_dsc_t tft_icon;   // tft_icon.c (tools/tft_icon.py)

namespace ui {
namespace {

using layout::S;
using layout::SCREEN_H;
using layout::SCREEN_W;
using layout::X;
using layout::Y;

constexpr uint32_t COL_BG = 0x0B0B0C;
constexpr uint32_t COL_TEXT = 0xF2F2F2;
constexpr uint32_t COL_DIM = 0x78787C;
constexpr uint32_t COL_MID = 0xAEAEB2;
constexpr uint32_t COL_ACCENT = 0x6DB56D;
constexpr uint32_t COL_PRIMARY = 0x3B7A3B;   // main button of a dialog: the accent, dark enough for white text
constexpr uint32_t COL_ERROR = 0xFFB0A0;

constexpr int LYRIC_ROWS = 5;   // 2 before, current, 2 after
constexpr int CURRENT_ROW = 2;

lv_obj_t *g_scr = nullptr;
lv_obj_t *g_bg = nullptr;                      // cover backdrop, behind everything
lv_obj_t *g_thumb = nullptr;                   // cover thumbnail left of the title
std::shared_ptr<const Picture> g_shownBackdrop, g_shownThumb;
uint32_t g_seenCoverSerial = UINT32_MAX;
const int TRACK_X = X(24), TRACK_W = SCREEN_W - 2 * X(24);   // title/artist box without a thumbnail
const int THUMB_GAP = X(14);

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

// Idle clock: over everything but the update screens, while the screen is dimmed and nothing
// plays (settings::clock()). Redrawn once a minute.
lv_obj_t *g_clock = nullptr, *g_clockTime = nullptr, *g_clockDate = nullptr, *g_clockTrack = nullptr;
int g_clockMinute = -1;
bool g_clockForced = false;    // showcase: shown with a fixed time
bool g_demoManualSlide = false;   // showcase: the slide is stepped by the console, not animated
int32_t g_demoPos = 98000;

// Volume popup (tap the volume value): slider + mute. Offset popup (long press on the lyrics):
// -/+ 250 ms, applied at once on screen, saved to the companion after a short pause.
lv_obj_t *g_volPopup = nullptr, *g_volTitle = nullptr, *g_volSlider = nullptr, *g_volValue = nullptr;
lv_obj_t *g_muteLabel = nullptr;
uint32_t g_volTouchedAt = 0, g_volSentAt = 0;
bool g_volDirty = false;
lv_obj_t *g_offPopup = nullptr, *g_offValue = nullptr;
int32_t g_offsetLocal = 0;
uint32_t g_offsetChangedAt = 0;
bool g_offsetPending = false;
String g_offsetTrack;

// Code for the local upload page (http://tft-panel-xxxx.local/update), shown while valid.
lv_obj_t *g_codePopup = nullptr, *g_codeValue = nullptr;
uint32_t g_shownCode = 0;

uint32_t g_seenVersion = UINT32_MAX;
net::Snapshot g_s;
uint32_t g_seenLyricsSerial = UINT32_MAX;
int g_shownIndex = INT32_MIN;
uint32_t g_lastProgress = 0;
String g_qrWifiData, g_qrUrlData;

void syncVolumePopup();

// Showcase (console "demo"): invented content through the normal UI path; nothing is ever sent
// to the companion while it is on (live() guards every command).
bool g_demo = false;
bool live() { return !g_demo; }

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

// Too long for its box: the text slides to the end, pauses, slides back (LVGL's bounce scroll),
// instead of being cut with "...". Slow, so it reads and costs few redraws.
void marquee(lv_obj_t *l) {
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_SCROLL);
    lv_obj_set_style_anim_duration(l, lv_anim_speed_clamped(S(35), 600, 30000), 0);
}

// Multi-line boxes (cover view): wrapped on up to `maxLines` (set by boundedLabel) when it
// fits, else on one line that scrolls.
void setFitText(lv_obj_t *l, const char *t) {
    int maxLines = (int)(intptr_t)lv_obj_get_user_data(l);
    const lv_font_t *font = lv_obj_get_style_text_font(l, LV_PART_MAIN);
    lv_point_t size;
    // The style width (fixed), not the coordinates, which are 0 before the first layout.
    lv_text_get_size(&size, t, font, 0, 0, lv_obj_get_style_width(l, LV_PART_MAIN), LV_TEXT_FLAG_NONE);
    int lines = (size.y + lv_font_get_line_height(font) / 2) / lv_font_get_line_height(font);
    lv_label_long_mode_t mode = lines > maxLines ? LV_LABEL_LONG_MODE_SCROLL : LV_LABEL_LONG_MODE_WRAP;
    if (lv_label_get_long_mode(l) != mode) {
        if (mode == LV_LABEL_LONG_MODE_SCROLL) marquee(l);
        else lv_label_set_long_mode(l, mode);
    }
    setText(l, t);
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

lv_obj_t *textButton(lv_obj_t *parent, const char *text, uint32_t bg, void (*cb)(lv_event_t *));

void buildSetup() {
    g_setup = lv_obj_create(g_scr);
    lv_obj_remove_style_all(g_setup);
    lv_obj_set_size(g_setup, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(g_setup, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(g_setup, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(g_setup, false);

    lv_obj_t *h = label(g_setup, fonts::lyricsOther, COL_TEXT);
    setText(h, "Set up TFT Panel");
    lv_obj_set_pos(h, X(40), Y(24));

    auto qrBlock = [](int x, const char *step) {
        lv_obj_t *qr = lv_qrcode_create(g_setup);
        lv_qrcode_set_size(qr, S(200));
        lv_qrcode_set_dark_color(qr, lv_color_hex(0x000000));
        lv_qrcode_set_light_color(qr, lv_color_hex(0xFFFFFF));
        lv_obj_set_style_border_color(qr, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_width(qr, S(8), 0);
        lv_obj_set_pos(qr, X(x), Y(90));
        lv_obj_t *t = label(g_setup, fonts::ui20, COL_MID);
        setText(t, step);
        lv_obj_set_width(t, X(260));
        lv_obj_set_pos(t, X(x) + S(100) - X(130), Y(312));   // centred under the code
        lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
        return qr;
    };
    g_qrWifi = qrBlock(80, "1. Scan to join the panel's Wi-Fi");
    g_qrUrl = qrBlock(330, "2. Scan to open the setup page");

    g_setupText = label(g_setup, fonts::ui20, COL_TEXT);
    lv_obj_set_pos(g_setupText, X(580), Y(100));
    lv_obj_set_width(g_setupText, X(200));
    lv_label_set_long_mode(g_setupText, LV_LABEL_LONG_MODE_WRAP);

    // Or without a phone: the networks on screen and a touch keyboard.
    lv_obj_t *here = textButton(g_setup, "Set up on this screen", COL_PRIMARY, [](lv_event_t *) { wifiui::open(); });
    lv_obj_align(here, LV_ALIGN_BOTTOM_MID, 0, -Y(28));
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
    lv_obj_set_size(b, S(76), S(76));
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x232326), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(COL_ACCENT), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, fonts::icons, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(COL_TEXT), 0);
    setText(l, symbol);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    if (iconOut) *iconOut = l;
    return b;
}

void onPrev(lv_event_t *) {
    touchControls();
    if (live()) net::control("previous");
}
void onNext(lv_event_t *) {
    touchControls();
    if (live()) net::control("next");
}
void onPlay(lv_event_t *) {
    touchControls();
    // Optimistic icon: the next now-playing poll (≤ 300 ms) confirms or corrects it.
    bool playing = g_s.playing();
    setText(g_playIcon, playing ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE);
    if (live()) net::control("playpause");
}
void onVolDown(lv_event_t *) {
    touchControls();
    if (live()) net::volumeStep(-1);
}
void onVolUp(lv_event_t *) {
    touchControls();
    if (live()) net::volumeStep(+1);
}

void closePicker() { setHidden(g_picker, true); }

void onPickZone(lv_event_t *e) {
    intptr_t i = (intptr_t)lv_event_get_user_data(e);
    if (live() && i >= 0 && i < (intptr_t)g_pickerIds.size()) net::selectZone(g_pickerIds[i]);
    closePicker();
    showControls(false);
}

void fillPicker() {
    lv_obj_clean(g_pickerList);
    g_pickerIds.clear();
    auto add = [](const String &id, const String &text, bool current) {
        lv_obj_t *b = lv_button_create(g_pickerList);
        lv_obj_set_width(b, lv_pct(100));
        lv_obj_set_style_radius(b, S(10), 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x18181B), 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x2C2C30), LV_STATE_PRESSED);
        lv_obj_set_style_pad_ver(b, Y(16), 0);
        lv_obj_t *l = label(b, fonts::ui20, current ? COL_ACCENT : COL_TEXT);
        lv_obj_set_width(l, lv_pct(100));
        marquee(l);
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
    lv_obj_set_size(g_controls, SCREEN_W, Y(112));
    lv_obj_align(g_controls, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(g_controls, lv_color_hex(0x0A0A0B), 0);
    lv_obj_set_style_bg_opa(g_controls, LV_OPA_90, 0);
    lv_obj_set_scrollable(g_controls, false);
    lv_obj_set_flex_flow(g_controls, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_controls, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    g_btnZone = lv_button_create(g_controls);
    lv_obj_set_size(g_btnZone, X(150), S(64));
    lv_obj_set_style_radius(g_btnZone, S(32), 0);
    lv_obj_set_style_bg_color(g_btnZone, lv_color_hex(0x232326), 0);
    lv_obj_set_style_bg_color(g_btnZone, lv_color_hex(COL_ACCENT), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(g_btnZone, 0, 0);
    g_zoneBtnLabel = label(g_btnZone, fonts::ui16, COL_TEXT);
    lv_obj_set_width(g_zoneBtnLabel, X(124));
    marquee(g_zoneBtnLabel);
    lv_obj_set_style_text_align(g_zoneBtnLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(g_zoneBtnLabel);
    lv_obj_add_event_cb(g_btnZone, onZone, LV_EVENT_CLICKED, nullptr);

    g_btnPrev = roundButton(g_controls, LV_SYMBOL_PREV, nullptr, onPrev);
    g_btnPlay = roundButton(g_controls, LV_SYMBOL_PLAY, &g_playIcon, onPlay);
    g_btnNext = roundButton(g_controls, LV_SYMBOL_NEXT, nullptr, onNext);
    g_btnVolDown = roundButton(g_controls, LV_SYMBOL_MINUS, nullptr, onVolDown);
    g_volLabel = label(g_controls, fonts::ui20, COL_MID);
    lv_obj_set_width(g_volLabel, X(80));
    lv_obj_set_height(g_volLabel, S(64));   // a finger-sized target: a tap opens the volume slider
    lv_obj_set_style_pad_top(g_volLabel, S(20), 0);
    lv_obj_set_clickable(g_volLabel, true);
    lv_obj_add_event_cb(g_volLabel, [](lv_event_t *) {
        showControls(false);
        setHidden(g_volPopup, false);
        syncVolumePopup();
    }, LV_EVENT_CLICKED, nullptr);
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
    lv_obj_set_size(g_picker, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(g_picker, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(g_picker, LV_OPA_COVER, 0);
    lv_obj_t *h = label(g_picker, fonts::lyricsOther, COL_TEXT);
    setText(h, "Zone");
    lv_obj_set_pos(h, X(32), Y(20));
    lv_obj_t *close = roundButton(g_picker, LV_SYMBOL_CLOSE, nullptr, [](lv_event_t *) { closePicker(); });
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, -X(20), Y(12));
    g_pickerList = lv_obj_create(g_picker);
    lv_obj_remove_style_all(g_pickerList);
    lv_obj_set_size(g_pickerList, SCREEN_W - 2 * X(20), Y(370));
    lv_obj_set_pos(g_pickerList, X(20), Y(100));
    lv_obj_set_flex_flow(g_pickerList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(g_pickerList, Y(8), 0);
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

const int CV_COVER_X = X(40), CV_COVER_Y = Y(56);
const int CV_COL_X = CV_COVER_X + net::COVER_BIG_PX + X(32), CV_COL_W = SCREEN_W - X(24) - CV_COL_X;

lv_obj_t *boundedLabel(lv_obj_t *parent, const lv_font_t *font, uint32_t colour, int maxLines) {
    lv_obj_t *l = label(parent, font, colour);
    lv_obj_set_width(l, CV_COL_W);
    lv_obj_set_style_max_height(l, maxLines * lv_font_get_line_height(font), 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_user_data(l, (void *)(intptr_t)maxLines);   // for setFitText
    return l;
}

lv_obj_t *progressBar(lv_obj_t *parent) {
    lv_obj_t *b = lv_bar_create(parent);
    lv_bar_set_range(b, 0, 1000);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x2E2E32), LV_PART_MAIN);
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
    int barY = CV_COVER_Y + net::COVER_BIG_PX - Y(26);
    lv_obj_t *col = lv_obj_create(g_coverView);
    lv_obj_remove_style_all(col);
    lv_obj_set_pos(col, CV_COL_X, CV_COVER_Y);
    lv_obj_set_size(col, CV_COL_W, barY - Y(10) - CV_COVER_Y);
    lv_obj_set_scrollable(col, false);
    lv_obj_set_clickable(col, false);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, Y(10), 0);
    g_cvTitle = boundedLabel(col, fonts::lyricsCurrent, COL_TEXT, 2);
    g_cvArtist = boundedLabel(col, fonts::lyricsOther, COL_MID, 2);
    g_cvAlbum = boundedLabel(col, fonts::ui20, COL_DIM, 1);
    lv_obj_t *gap = lv_obj_create(col);   // breathing space before the lyric line
    lv_obj_remove_style_all(gap);
    lv_obj_set_size(gap, 1, Y(12));
    g_cvLyric = boundedLabel(col, fonts::ui20, COL_MID, 2);

    g_cvBar = progressBar(g_coverView);
    lv_obj_set_size(g_cvBar, CV_COL_W, S(4));
    lv_obj_set_pos(g_cvBar, CV_COL_X, barY);
    g_cvElapsed = label(g_coverView, fonts::ui16, COL_DIM);
    lv_obj_set_pos(g_cvElapsed, CV_COL_X, barY + Y(10));
    g_cvTotal = label(g_coverView, fonts::ui16, COL_DIM);
    lv_obj_set_width(g_cvTotal, X(80));
    lv_obj_set_style_text_align(g_cvTotal, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(g_cvTotal, CV_COL_X + CV_COL_W - X(80), barY + Y(10));
}

void buildPageDots() {
    for (int i = 0; i < VIEW_COUNT; i++) {
        lv_obj_t *d = lv_obj_create(g_scr);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, S(8), S(8));
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(d, lv_color_hex(COL_DIM), 0);
        lv_obj_set_pos(d, SCREEN_W / 2 - VIEW_COUNT * S(7) + S(3) + i * S(14), Y(18));
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
    if (!indev || g_s.portalActive || !isHidden(g_picker) || settingsui::isOpen() || wifiui::isOpen()) return;
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
    lv_obj_set_style_radius(b, S(28), 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(COL_ACCENT), LV_STATE_PRESSED);
    lv_obj_set_style_pad_hor(b, X(28), 0);
    lv_obj_set_style_pad_ver(b, Y(14), 0);
    lv_obj_t *l = label(b, fonts::ui20, COL_TEXT);
    setText(l, text);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    return b;
}

lv_obj_t *fullScreen(lv_opa_t opa) {
    lv_obj_t *o = lv_obj_create(g_scr);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(o, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_clickable(o, true);   // modal: nothing underneath gets the touch
    lv_obj_set_hidden(o, true);
    return o;
}

const net::OutputInfo *volumeOutput() {
    for (const net::OutputInfo &o : g_s.outputs)
        if (o.volumeType == "number" || o.volumeType == "db") return &o;
    return nullptr;
}

String volumeText(float v, const net::OutputInfo &o) {
    char buf[24];
    if (o.volumeType == "db") snprintf(buf, sizeof buf, "%.1f dB", v);
    else snprintf(buf, sizeof buf, "%.0f", v);
    return buf;
}

float sliderToVolume(const net::OutputInfo &o) {
    float step = o.step > 0 ? o.step : 1;
    return o.min + lv_slider_get_value(g_volSlider) * step;
}

void syncVolumePopup() {
    if (isHidden(g_volPopup)) return;
    const net::OutputInfo *o = volumeOutput();
    bool any = false;
    for (const net::OutputInfo &x : g_s.outputs) any |= x.volumeType.length() > 0;
    if (!any) {
        setHidden(g_volPopup, true);
        return;
    }
    bool muted = false;
    for (const net::OutputInfo &x : g_s.outputs)
        if (x.volumeType.length() && x.muted) muted = true;
    setText(g_muteLabel, muted ? LV_SYMBOL_VOLUME_MAX "  Unmute" : LV_SYMBOL_MUTE "  Mute");
    setHidden(g_volSlider, !o);
    setText(g_volTitle, o ? ("Volume  -  " + o->name).c_str() : "Volume (this zone only has - / +)");
    if (!o) {
        setText(g_volValue, "");
        return;
    }
    float step = o->step > 0 ? o->step : 1;
    int32_t n = (int32_t)lroundf((o->max - o->min) / step);
    if (lv_slider_get_max_value(g_volSlider) != n) lv_slider_set_range(g_volSlider, 0, n);
    // Don't fight the finger: follow the zone only when the user is not adjusting it.
    bool touching = lv_obj_has_state(g_volSlider, LV_STATE_PRESSED) || millis() - g_volTouchedAt < 1500;
    if (!touching) {
        lv_slider_set_value(g_volSlider, (int32_t)lroundf((o->value - o->min) / step), LV_ANIM_OFF);
        setText(g_volValue, (muted ? String("muted") : volumeText(o->value, *o)).c_str());
    }
}

void onVolSlider(lv_event_t *e) {
    const net::OutputInfo *o = volumeOutput();
    if (!o) return;
    g_volTouchedAt = millis();
    float v = sliderToVolume(*o);
    setText(g_volValue, volumeText(v, *o).c_str());
    bool released = lv_event_get_code(e) == LV_EVENT_RELEASED;
    if (released || millis() - g_volSentAt >= 250) {   // at most 4 requests a second while dragging
        g_volSentAt = millis();
        g_volDirty = false;
        if (live()) net::volumeSet(v);
    } else {
        g_volDirty = true;
    }
}

lv_obj_t *popupBox(int h) {
    lv_obj_t *dim = fullScreen(LV_OPA_70);
    lv_obj_add_event_cb(dim, [](lv_event_t *e) {   // tap outside the box closes it
        if (lv_event_get_target(e) == lv_event_get_current_target(e)) setHidden((lv_obj_t *)lv_event_get_current_target(e), true);
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *box = lv_obj_create(dim);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, X(680), Y(h));
    lv_obj_align(box, LV_ALIGN_BOTTOM_MID, 0, -Y(24));
    lv_obj_set_style_bg_color(box, lv_color_hex(0x18181B), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, S(16), 0);
    lv_obj_set_scrollable(box, false);
    lv_obj_set_clickable(box, true);
    return box;
}

void buildVolumePopup() {
    lv_obj_t *box = popupBox(220);
    g_volPopup = lv_obj_get_parent(box);
    g_volTitle = label(box, fonts::ui20, COL_MID);
    lv_obj_set_width(g_volTitle, X(460));
    lv_label_set_long_mode(g_volTitle, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(g_volTitle, X(28), Y(22));
    g_volValue = label(box, fonts::lyricsOther, COL_TEXT);
    lv_obj_set_width(g_volValue, X(160));
    lv_obj_set_style_text_align(g_volValue, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(g_volValue, LV_ALIGN_TOP_RIGHT, -X(28), Y(14));
    g_volSlider = lv_slider_create(box);
    lv_obj_set_size(g_volSlider, X(600), S(22));
    lv_obj_align(g_volSlider, LV_ALIGN_TOP_MID, 0, Y(90));
    lv_obj_set_style_bg_color(g_volSlider, lv_color_hex(COL_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g_volSlider, lv_color_hex(COL_TEXT), LV_PART_KNOB);
    lv_obj_set_style_pad_all(g_volSlider, S(10), LV_PART_KNOB);
    lv_obj_add_event_cb(g_volSlider, onVolSlider, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(g_volSlider, onVolSlider, LV_EVENT_RELEASED, nullptr);
    lv_obj_t *mute = textButton(box, "Mute", 0x232326, [](lv_event_t *) {
        bool muted = false;
        for (const net::OutputInfo &x : g_s.outputs)
            if (x.volumeType.length() && x.muted) muted = true;
        if (live()) net::mute(!muted);
    });
    g_muteLabel = lv_obj_get_child(mute, 0);
    lv_obj_align(mute, LV_ALIGN_BOTTOM_LEFT, X(28), -Y(20));
    lv_obj_t *done = textButton(box, "Done", COL_PRIMARY, [](lv_event_t *) { setHidden(g_volPopup, true); });
    lv_obj_align(done, LV_ALIGN_BOTTOM_RIGHT, -X(28), -Y(20));
}

int32_t effectiveOffset() {
    // The local value wins while it is being edited or saved, then the companion's.
    if (g_offsetTrack == trackKey() && (g_offsetPending || millis() - g_offsetChangedAt < 5000)) return g_offsetLocal;
    return g_s.offsetMs;
}

void showOffset() {
    int32_t v = effectiveOffset();
    char buf[40];
    if (v == 0) snprintf(buf, sizeof buf, "In sync");
    else snprintf(buf, sizeof buf, "%.2f s %s", fabsf(v / 1000.0f), v > 0 ? "later" : "earlier");
    setText(g_offValue, buf);
}

void stepOffset(int32_t delta) {
    int32_t base = effectiveOffset();
    g_offsetTrack = trackKey();
    g_offsetLocal = delta ? base + delta : 0;
    if (g_offsetLocal > 10000) g_offsetLocal = 10000;
    if (g_offsetLocal < -10000) g_offsetLocal = -10000;
    g_offsetChangedAt = millis();
    g_offsetPending = true;
    g_shownIndex = INT32_MIN;   // re-place the lyrics now
    showOffset();
}

void buildOffsetPopup() {
    lv_obj_t *box = popupBox(200);
    g_offPopup = lv_obj_get_parent(box);
    lv_obj_t *t = label(box, fonts::ui20, COL_MID);
    setText(t, "Lyrics timing for this track");
    lv_obj_set_pos(t, X(28), Y(22));
    g_offValue = label(box, fonts::lyricsOther, COL_TEXT);
    lv_obj_align(g_offValue, LV_ALIGN_TOP_RIGHT, -X(28), Y(14));
    lv_obj_t *row = lv_obj_create(box);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, X(640), Y(80));
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, -Y(14));
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(row, false);
    textButton(row, "Earlier", 0x232326, [](lv_event_t *) { stepOffset(-250); });
    textButton(row, "Later", 0x232326, [](lv_event_t *) { stepOffset(+250); });
    textButton(row, "Reset", 0x232326, [](lv_event_t *) { stepOffset(0); });
    textButton(row, "Done", COL_PRIMARY, [](lv_event_t *) { setHidden(g_offPopup, true); });
}

void onLongPress(lv_event_t *) {
    lv_indev_t *indev = lv_indev_active();
    if (g_view != View::Lyrics || g_s.portalActive || !isHidden(g_picker) || settingsui::isOpen() || wifiui::isOpen()) return;
    if (g_s.lyricsStatus != net::LyricsStatus::Found || !g_s.lyrics || !g_s.lyrics->synced) return;
    if (indev) lv_indev_wait_release(indev);   // no tap (controls toggle) at the release
    showControls(false);
    showOffset();
    setHidden(g_offPopup, false);
}

// Sends the pending volume drag and the offset once the user paused.
void tickPopups(uint32_t now) {
    if (g_volDirty && now - g_volSentAt >= 250) {
        const net::OutputInfo *o = volumeOutput();
        g_volDirty = false;
        g_volSentAt = now;
        if (o && live()) net::volumeSet(sliderToVolume(*o));
    }
    if (g_offsetPending && now - g_offsetChangedAt >= 800) {
        g_offsetPending = false;
        if (g_offsetTrack == trackKey() && live()) net::setOffset(g_offsetLocal);
    }
    if (!isHidden(g_offPopup)) showOffset();
}

void buildCodePopup() {
    g_codePopup = fullScreen(LV_OPA_90);
    lv_obj_t *t = label(g_codePopup, fonts::ui20, COL_MID);
    lv_obj_set_width(t, X(640));
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    setText(t, "Firmware update from a browser\n\nType this code on the page to allow the upload (valid 5 minutes):");
    lv_obj_align(t, LV_ALIGN_CENTER, 0, -Y(110));
    g_codeValue = label(g_codePopup, fonts::clock, COL_TEXT);
    lv_obj_align(g_codeValue, LV_ALIGN_CENTER, 0, Y(10));
    lv_obj_t *cancel = textButton(g_codePopup, "Cancel", 0x232326, [](lv_event_t *) { webupdate::cancelCode(); });
    lv_obj_align(cancel, LV_ALIGN_CENTER, 0, Y(150));
}

void tickCode() {
    if (g_demo) return;
    uint32_t code = webupdate::activeCode();
    if (code == g_shownCode) return;
    g_shownCode = code;
    if (code) {
        char buf[12];
        snprintf(buf, sizeof buf, "%06u", (unsigned)code);
        setText(g_codeValue, buf);
        g_activeAt = millis();   // wake the screen: the user is in front of it
    }
    setHidden(g_codePopup, code == 0);
}

void buildClock() {
    g_clock = lv_obj_create(g_scr);
    lv_obj_remove_style_all(g_clock);
    lv_obj_set_size(g_clock, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(g_clock, lv_color_hex(0x05070A), 0);
    lv_obj_set_style_bg_opa(g_clock, LV_OPA_80, 0);
    lv_obj_set_scrollable(g_clock, false);
    lv_obj_set_clickable(g_clock, false);   // a tap goes through: it wakes the screen as usual
    g_clockTime = label(g_clock, fonts::clock, COL_TEXT);
    lv_obj_align(g_clockTime, LV_ALIGN_CENTER, 0, -Y(40));
    g_clockDate = label(g_clock, fonts::lyricsOther, COL_MID);
    lv_obj_align(g_clockDate, LV_ALIGN_CENTER, 0, Y(70));
    g_clockTrack = label(g_clock, fonts::ui20, COL_DIM);
    lv_obj_set_width(g_clockTrack, X(700));
    marquee(g_clockTrack);
    lv_obj_set_style_text_align(g_clockTrack, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_clockTrack, LV_ALIGN_BOTTOM_MID, 0, -Y(36));
    lv_obj_set_hidden(g_clock, true);
}

void buildOta() {
    g_otaBadge = lv_button_create(g_scr);
    lv_obj_set_height(g_otaBadge, S(30));
    lv_obj_set_style_radius(g_otaBadge, S(15), 0);
    lv_obj_set_style_shadow_width(g_otaBadge, 0, 0);
    lv_obj_set_style_pad_hor(g_otaBadge, X(14), 0);
    lv_obj_set_style_pad_ver(g_otaBadge, 0, 0);
    lv_obj_set_style_bg_color(g_otaBadge, lv_color_hex(COL_ACCENT), 0);
    lv_obj_set_pos(g_otaBadge, X(430), Y(7));
    g_otaBadgeLabel = label(g_otaBadge, fonts::ui16, 0x15161A);
    lv_obj_center(g_otaBadgeLabel);
    lv_obj_add_event_cb(g_otaBadge, [](lv_event_t *) { setHidden(g_otaConfirm, false); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_set_hidden(g_otaBadge, true);

    g_otaConfirm = fullScreen(LV_OPA_90);
    g_otaConfirmText = label(g_otaConfirm, fonts::ui20, COL_TEXT);
    lv_obj_set_width(g_otaConfirmText, X(620));
    lv_label_set_long_mode(g_otaConfirmText, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(g_otaConfirmText, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_otaConfirmText, LV_ALIGN_CENTER, 0, -Y(60));
    lv_obj_t *install = textButton(g_otaConfirm, "Install now", COL_PRIMARY, [](lv_event_t *) {
        setHidden(g_otaConfirm, true);
        if (live()) ota::requestInstall();
    });
    lv_obj_align(install, LV_ALIGN_CENTER, -X(110), Y(90));
    lv_obj_t *later = textButton(g_otaConfirm, "Later", 0x232326, [](lv_event_t *) { setHidden(g_otaConfirm, true); });
    lv_obj_align(later, LV_ALIGN_CENTER, X(110), Y(90));

    g_otaProgress = fullScreen(LV_OPA_COVER);
    g_otaProgressText = label(g_otaProgress, fonts::lyricsOther, COL_TEXT);
    lv_obj_set_width(g_otaProgressText, X(680));
    lv_label_set_long_mode(g_otaProgressText, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(g_otaProgressText, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_otaProgressText, LV_ALIGN_CENTER, 0, -Y(50));
    g_otaBar = progressBar(g_otaProgress);
    lv_bar_set_range(g_otaBar, 0, 100);
    lv_obj_set_size(g_otaBar, X(520), S(8));
    lv_obj_align(g_otaBar, LV_ALIGN_CENTER, 0, Y(40));
    g_otaClose = textButton(g_otaProgress, "Close", 0x232326, [](lv_event_t *) { setHidden(g_otaProgress, true); });
    lv_obj_align(g_otaClose, LV_ALIGN_CENTER, 0, Y(120));
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
        if (!board::staticScreenActive()) {
            // Draw the update screen once, then freeze it in internal RAM: flash writes stall
            // PSRAM and would make the live image jerk (board::staticScreenBegin).
            setHidden(g_otaProgress, false);
            setHidden(g_otaClose, true);
            setHidden(g_otaBar, false);
            setText(g_otaProgressText, ("Updating to " + g_ota.latest + "\nDo not unplug the panel").c_str());
            lv_bar_set_value(g_otaBar, 0, LV_ANIM_OFF);
            lv_refr_now(nullptr);
            lv_area_t t, b;
            lv_obj_get_coords(g_otaProgressText, &t);
            lv_obj_get_coords(g_otaBar, &b);
            lv_color_t acc = lv_color_hex(COL_ACCENT);
            board::staticScreenBegin(board::StaticScreen{t.y1 - 4, t.y2 + 5, b.x1, b.x2 + 1, b.y1, b.y2 + 1,
                                                         lv_color_to_u16(acc)});
        }
        board::staticScreenProgress(g_ota.percent);
        if (!board::HAS_STATIC_SCREEN) lv_bar_set_value(g_otaBar, g_ota.percent, LV_ANIM_OFF);   // live screen
    } else if (failedNow) {
        board::staticScreenEnd();
        setHidden(g_otaBar, true);
        setHidden(g_otaClose, false);
        setText(g_otaProgressText, ("Update failed: " + g_ota.error + "\n\nStill running " + String(FW_VERSION) + ".").c_str());
    }
}

void build() {
    // LVGL's theme colours (slider tracks, switches, dropdowns, message boxes): the brand green,
    // not the default blue.
    lv_display_t *disp = lv_display_get_default();
    lv_display_set_theme(disp, lv_theme_default_init(disp, lv_color_hex(COL_ACCENT), lv_color_hex(COL_PRIMARY), false,
                                                     LV_FONT_DEFAULT));
    g_scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(g_scr, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_text_color(g_scr, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_scrollable(g_scr, false);

    g_bg = lv_image_create(g_scr);
    lv_obj_set_pos(g_bg, 0, 0);
    lv_obj_set_size(g_bg, SCREEN_W, SCREEN_H);
    lv_obj_set_clickable(g_bg, false);
    lv_obj_set_hidden(g_bg, true);

    // The TextFromTrack mark before the zone name, like the companion's desktop UI.
    lv_obj_t *mark = lv_image_create(g_scr);
    lv_image_set_src(mark, &tft_icon);
    lv_obj_set_pos(mark, X(24), Y(11));
    lv_obj_set_clickable(mark, false);
    g_zone = label(g_scr, fonts::ui16, COL_MID);
    lv_obj_set_pos(g_zone, X(24) + tft_icon.header.w + S(8), Y(12));
    g_status = label(g_scr, fonts::ui16, COL_DIM);
    lv_obj_set_width(g_status, X(460));
    lv_obj_set_style_text_align(g_status, LV_TEXT_ALIGN_RIGHT, 0);
    marquee(g_status);
    lv_obj_align(g_status, LV_ALIGN_TOP_RIGHT, -X(24), Y(12));

    auto viewBox = []() {
        lv_obj_t *v = lv_obj_create(g_scr);
        lv_obj_remove_style_all(v);
        lv_obj_set_size(v, SCREEN_W, SCREEN_H);
        lv_obj_set_scrollable(v, false);
        lv_obj_set_clickable(v, false);   // taps and swipes reach the screen
        return v;
    };
    g_lyricsView = viewBox();

    g_thumb = lv_image_create(g_lyricsView);
    lv_obj_set_pos(g_thumb, TRACK_X, Y(37));
    lv_obj_set_size(g_thumb, net::COVER_THUMB_PX, net::COVER_THUMB_PX);
    lv_image_set_inner_align(g_thumb, LV_IMAGE_ALIGN_CENTER);
    lv_obj_set_clickable(g_thumb, false);
    lv_obj_set_hidden(g_thumb, true);

    g_title = label(g_lyricsView, fonts::ui20, COL_TEXT);
    lv_obj_set_width(g_title, TRACK_W);
    marquee(g_title);
    lv_obj_set_pos(g_title, TRACK_X, Y(40));
    g_artist = label(g_lyricsView, fonts::ui16, COL_MID);
    lv_obj_set_width(g_artist, TRACK_W);
    marquee(g_artist);
    lv_obj_set_pos(g_artist, TRACK_X, Y(68));

    g_stage = lv_obj_create(g_lyricsView);
    lv_obj_remove_style_all(g_stage);
    lv_obj_set_size(g_stage, SCREEN_W - 2 * X(20), Y(340));
    lv_obj_set_pos(g_stage, X(20), Y(98));
    lv_obj_set_scrollable(g_stage, false);
    g_rowsBox = lv_obj_create(g_stage);
    lv_obj_remove_style_all(g_rowsBox);
    lv_obj_set_size(g_rowsBox, lv_pct(100), lv_pct(100));
    lv_obj_set_scrollable(g_rowsBox, false);
    lv_obj_set_clickable(g_rowsBox, false);
    lv_obj_set_flex_flow(g_rowsBox, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_rowsBox, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(g_rowsBox, Y(10), 0);
    // Fixed budget so the stage never overflows: neighbours on one line, the current line on
    // at most two; longer text ends with "…". 4 x 41 + 2 x 60 + gaps fits the 340 px stage.
    for (int i = 0; i < LYRIC_ROWS; i++) {
        bool cur = i == CURRENT_ROW;
        const lv_font_t *font = cur ? fonts::lyricsCurrent : fonts::lyricsOther;
        lv_obj_t *l = label(g_rowsBox, font, cur ? COL_TEXT : COL_DIM);
        int32_t lh = lv_font_get_line_height(font);
        lv_obj_set_width(l, SCREEN_W - 2 * X(20));
        lv_obj_set_height(l, LV_SIZE_CONTENT);
        lv_obj_set_style_max_height(l, cur ? 2 * lh : lh, 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        g_rows[i] = l;
    }

    g_message = label(g_lyricsView, fonts::lyricsOther, COL_MID);
    lv_obj_set_width(g_message, X(700));
    lv_label_set_long_mode(g_message, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(g_message, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g_message, LV_ALIGN_CENTER, 0, Y(10));

    g_bar = lv_bar_create(g_lyricsView);
    lv_obj_set_size(g_bar, X(640), S(4));
    lv_obj_align(g_bar, LV_ALIGN_BOTTOM_MID, 0, -Y(20));
    lv_bar_set_range(g_bar, 0, 1000);
    lv_obj_set_style_bg_color(g_bar, lv_color_hex(0x2E2E32), LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_bar, lv_color_hex(COL_ACCENT), LV_PART_INDICATOR);
    g_elapsed = label(g_lyricsView, fonts::ui16, COL_DIM);
    lv_obj_align(g_elapsed, LV_ALIGN_BOTTOM_LEFT, X(24), -Y(10));
    g_total = label(g_lyricsView, fonts::ui16, COL_DIM);
    lv_obj_align(g_total, LV_ALIGN_BOTTOM_RIGHT, -X(24), -Y(10));

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
        []() { wifiui::open(); },
    });
    wifiui::build(g_scr);
    buildVolumePopup();
    buildOffsetPopup();
    buildClock();
    buildCodePopup();
    buildOta();
    for (lv_obj_t *o : {g_stage, g_setup}) lv_obj_set_clickable(o, false);
    lv_obj_add_event_cb(g_scr, onScreenTap, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(g_scr, onGesture, LV_EVENT_GESTURE, nullptr);
    lv_obj_add_event_cb(g_scr, onLongPress, LV_EVENT_LONG_PRESSED, nullptr);
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
        if (r == CURRENT_ROW) setFitText(g_cvLyric, text);
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
    lv_obj_set_pos(g_title, x, Y(40));
    lv_obj_set_width(g_title, w);
    lv_obj_set_pos(g_artist, x, Y(68));
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
    setFitText(g_cvTitle, track ? g_s.title.c_str() : "");
    setFitText(g_cvArtist, track ? g_s.artist.c_str() : "");
    setFitText(g_cvAlbum, track ? g_s.album.c_str() : "");
    if (g_s.lyricsStatus != net::LyricsStatus::Found) setFitText(g_cvLyric, "");
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
        index = lyrics::currentIndex(l, pos + settings::leadMs() - effectiveOffset());
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
    if (g_demo && g_demoManualSlide) {   // frames for the showcase animation: see demoSlide()
        g_scrollFrom = from;
        scrollExec(g_rowsBox, from);
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

void tickClock() {
    if (g_clockForced) return;
    time_t t = time(nullptr);
    struct tm lt;
    localtime_r(&t, &lt);
    bool timeKnown = lt.tm_year + 1900 >= 2025;   // SNTP has answered
    bool show = settings::clock() && timeKnown && g_screen == Screen::Dimmed && !g_s.playing() && !g_demo &&
                !g_s.portalActive;
    setHidden(g_clock, !show);
    if (!show) {
        g_clockMinute = -1;
        return;
    }
    if (lt.tm_min == g_clockMinute) return;
    g_clockMinute = lt.tm_min;
    char buf[48];
    strftime(buf, sizeof buf, "%H:%M", &lt);
    setText(g_clockTime, buf);
    static const char *days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    static const char *months[] = {"January", "February", "March", "April", "May", "June", "July",
                                   "August", "September", "October", "November", "December"};
    snprintf(buf, sizeof buf, "%s %d %s", days[lt.tm_wday], lt.tm_mday, months[lt.tm_mon]);
    setText(g_clockDate, buf);
    String track = g_s.title.length() ? (g_s.state == "paused" ? "Paused  -  " : "") + g_s.title +
                                            (g_s.artist.length() ? "  -  " + g_s.artist : "")
                                      : String();
    setText(g_clockTrack, track.c_str());
}

void tickSleep(uint32_t now) {
    hookTouch();
    bool busy = g_demo || g_s.portalActive || g_s.playing() || !isHidden(g_picker) || g_swallowTouch ||
                !isHidden(g_otaConfirm) || !isHidden(g_otaProgress) || settingsui::isOpen() || wifiui::isOpen() ||
                !isHidden(g_volPopup) || !isHidden(g_offPopup) || !isHidden(g_codePopup);
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
        setHidden(g_volPopup, true);
        setHidden(g_offPopup, true);
    }
    g_screen = want;
    tickClock();   // show / hide the idle clock with the dim, not up to a frame later
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

void loadShowcase() {
    g_s = showcase::make(g_demoPos);
    g_seenCoverSerial = UINT32_MAX;
    g_shownIndex = INT32_MIN;
    applySnapshot();
    syncVolumePopup();
}

void setDemo(bool on) {
    g_demo = on;
    g_seenVersion = UINT32_MAX;   // live mode redraws everything on the next tick
    g_seenCoverSerial = UINT32_MAX;
    g_shownIndex = INT32_MIN;
    if (on) {
        loadShowcase();
        return;
    }
    showcase("none");
    g_demoManualSlide = false;
    g_seenOtaVersion = UINT32_MAX;   // back to the real update status
}

void demoSeek(int32_t positionMs) {
    if (!g_demo) return;
    g_demoPos = positionMs;
    g_s.positionMs = positionMs;
    g_s.positionAtMs = millis();
}

void demoManualSlide(bool on) { g_demoManualSlide = on; }

void demoSlide(int percent) {
    if (!g_demo || g_scrollFrom <= 0) return;
    float t = percent / 100.0f;
    float eased = 1 - (1 - t) * (1 - t) * (1 - t);   // ease-out, like lv_anim_path_ease_out
    scrollExec(g_rowsBox, (int32_t)lroundf(g_scrollFrom * (1 - eased)));
}

bool showcase(const String &what) {
    if (what == "none") {
        settingsui::close();
        wifiui::close();
        for (lv_obj_t *o : {g_volPopup, g_offPopup, g_codePopup, g_otaConfirm, g_otaProgress, g_otaBadge, g_clock})
            setHidden(o, true);
        closePicker();
        showControls(false);
        g_clockForced = false;
        g_ota = ota::Status();
        if (g_demo) loadShowcase();
        return true;
    }
    if (!g_demo) return false;
    if (what == "lyrics" || what == "cover") showView(what == "cover" ? View::Cover : View::Lyrics);
    else if (what == "controls") showControls(true);
    else if (what == "settings") {
        settingsui::open();
        ota::Status o;
        o.state = ota::State::UpToDate;
        o.latest = FW_VERSION;
        settingsui::refresh(g_s, o);
    } else if (what == "colours") settingsui::showColourTest();
    else if (what == "volume") {
        setHidden(g_volPopup, false);
        syncVolumePopup();
    } else if (what == "offset") {
        g_offsetTrack = trackKey();
        g_offsetLocal = 250;
        g_offsetChangedAt = millis() + 3600000;   // keep "0.25 s later" on screen
        showOffset();
        setHidden(g_offPopup, false);
    } else if (what == "zones") onZone(nullptr);
    else if (what == "clock") {
        g_clockForced = true;
        setText(g_clockTime, "21:47");
        setText(g_clockDate, "Saturday 7 November");
        setText(g_clockTrack, "Paused  -  Neon Autumn  -  The Demo Lights");
        setHidden(g_clock, false);
    } else if (what == "update" || what == "confirm") {
        g_ota.state = ota::State::Available;
        g_ota.latest = "v0.2.0";
        applyOta();
        if (what == "confirm") setHidden(g_otaConfirm, false);
    } else if (what == "progress") {
        setHidden(g_otaProgress, false);
        setHidden(g_otaClose, true);
        setHidden(g_otaBar, false);
        setText(g_otaProgressText, "Updating to v0.2.0\nDo not unplug the panel");
        lv_bar_set_value(g_otaBar, 42, LV_ANIM_OFF);
    } else if (what == "setup") {
        showcase::fakePortal(g_s);
        applySnapshot();
    } else if (what == "wifi" || what == "wifi-password") {
        wifiui::showcase(what == "wifi-password");
    } else if (what == "code") {
        setText(g_codeValue, "482913");
        setHidden(g_codePopup, false);
    } else return false;
    return true;
}

void tick() {
    uint32_t now = millis();
    tickCode();
    tickSleep(now);
    static uint32_t lastClock = 0;
    if (now - lastClock >= 500) {
        lastClock = now;
        tickClock();
    }
    if (g_demo) {   // frozen showcase: no network, the position stays still
        g_s.positionAtMs = now;
        tickLyrics(now);
        tickProgress(now);
        return;
    }
    uint32_t v = net::version();
    if (v != g_seenVersion) {
        g_seenVersion = v;
        g_s = net::snapshot();
        applySnapshot();
        syncVolumePopup();
        wifiui::refresh(g_s);
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
    tickPopups(now);
    if (!isHidden(g_controls) && isHidden(g_picker) && now - g_controlsShownAt > CONTROLS_HIDE_MS)
        showControls(false);
    if (g_errorShown && now - g_s.commandErrorAt >= COMMAND_ERROR_MS) applySnapshot();
    tickLyrics(now);
    tickProgress(now);
}

}  // namespace ui
