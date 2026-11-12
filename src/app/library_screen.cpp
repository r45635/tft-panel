#include "library_screen.h"

#include <Arduino.h>

#include <vector>

#include "../layout.h"
#include "fonts.h"
#include "keyboard.h"

namespace libui {
namespace {

using layout::S;
using layout::SCREEN_H;
using layout::SCREEN_W;
using layout::X;
using layout::Y;

constexpr uint32_t COL_BG = 0x0B0B0C, COL_TEXT = 0xF2F2F2, COL_MID = 0xAEAEB2, COL_DIM = 0x78787C;
constexpr uint32_t COL_ACCENT = 0x6DB56D, COL_PRIMARY = 0x3B7A3B, COL_ERROR = 0xFFB0A0, COL_BUTTON = 0x232326;
constexpr uint32_t MESSAGE_MS = 5000;

enum class Tab : uint8_t { Search, Browse };

lv_obj_t *g_root = nullptr;
lv_obj_t *g_tabSearch = nullptr, *g_tabBrowse = nullptr, *g_zone = nullptr;
lv_obj_t *g_searchRow = nullptr, *g_ta = nullptr, *g_kb = nullptr;
lv_obj_t *g_titleRow = nullptr, *g_back = nullptr, *g_title = nullptr;
lv_obj_t *g_list = nullptr, *g_info = nullptr, *g_toast = nullptr;
lv_obj_t *g_sheet = nullptr, *g_sheetBox = nullptr;

struct Row {
    lv_obj_t *img = nullptr;
    bool hasThumb = false;
};
std::vector<Row> g_rows;
lv_obj_t *g_moreButton = nullptr;

Tab g_tab = Tab::Search;
std::shared_ptr<const net::LibraryPage> g_page;
uint32_t g_seenSerial = UINT32_MAX, g_shownId = 0, g_shownMessageAt = 0, g_toastAt = 0;
size_t g_shownCount = 0;
String g_sheetFor;
bool g_waiting = false;   // a request went out, its page has not come back yet
bool g_demo = false;

lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t colour, const char *text = "") {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(colour), 0);
    lv_label_set_text(l, text);
    return l;
}

void styleButton(lv_obj_t *b, uint32_t bg, int32_t radius) {
    lv_obj_set_style_radius(b, radius, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(COL_ACCENT), LV_STATE_PRESSED);
}

lv_obj_t *textButton(lv_obj_t *parent, const char *text, uint32_t bg, lv_event_cb_t cb, void *user = nullptr) {
    lv_obj_t *b = lv_button_create(parent);
    styleButton(b, bg, S(24));
    lv_obj_set_style_pad_hor(b, X(22), 0);
    lv_obj_set_style_pad_ver(b, Y(12), 0);
    lv_obj_center(label(b, fonts::ui20, COL_TEXT, text));
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    return b;
}

lv_obj_t *roundButton(lv_obj_t *parent, const char *symbol, lv_event_cb_t cb) {
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, S(56), S(56));
    styleButton(b, COL_BUTTON, LV_RADIUS_CIRCLE);
    lv_obj_center(label(b, fonts::icons, COL_TEXT, symbol));
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    return b;
}

void send(net::LibOp op, int index = -1, const String &text = String()) {
    if (g_demo) return;
    g_waiting = true;
    lv_label_set_text(g_info, "Loading...");
    lv_obj_set_hidden(g_info, false);
    lv_obj_move_foreground(g_info);
    net::library(op, index, text);
}

void showKeyboard(bool on) {
    lv_obj_set_hidden(g_kb, !on);
    keyboard::focus(g_ta, on);
}

// The header's second row: the search field at the top of the Search tab, else Back + the path.
void showHeader() {
    bool searchRoot = g_tab == Tab::Search && (!g_page || g_page->hierarchy != "search" || g_page->path.size() <= 1);
    lv_obj_set_hidden(g_searchRow, !searchRoot);
    lv_obj_set_hidden(g_titleRow, searchRoot);
    if (!searchRoot && g_page) {
        String t;
        size_t n = g_page->path.size();
        for (size_t i = n > 2 ? n - 2 : 0; i < n; i++) t += (t.length() ? "  " LV_SYMBOL_RIGHT "  " : "") + g_page->path[i];
        lv_label_set_text(g_title, t.c_str());
        lv_obj_set_hidden(g_back, n <= 1);
    }
    for (auto [b, tab] : {std::pair<lv_obj_t *, Tab>{g_tabSearch, Tab::Search}, {g_tabBrowse, Tab::Browse}})
        lv_obj_set_style_bg_color(b, lv_color_hex(g_tab == tab ? COL_PRIMARY : COL_BUTTON), 0);
}

void onRow(lv_event_t *e) {
    intptr_t i = (intptr_t)lv_event_get_user_data(e);
    if (!g_page || i < 0 || i >= (intptr_t)g_page->items.size()) return;
    const net::LibItem &it = g_page->items[i];
    if (it.hint == "list") send(net::LibOp::Drill, i);
    else if (it.hint == "action_list" || it.hint == "action") send(net::LibOp::Actions, i);
}

lv_obj_t *addRow(size_t i) {
    const net::LibItem &it = g_page->items[i];
    if (it.hint == "header") {
        lv_obj_t *h = label(g_list, fonts::ui16, COL_ACCENT, it.title.c_str());
        lv_obj_set_style_pad_top(h, Y(8), 0);
        g_rows.push_back({});
        return h;
    }
    lv_obj_t *b = lv_button_create(g_list);
    lv_obj_set_width(b, lv_pct(100));
    styleButton(b, 0x18181B, S(10));
    lv_obj_set_style_bg_color(b, lv_color_hex(0x2C2C30), LV_STATE_PRESSED);
    lv_obj_set_style_pad_all(b, S(8), 0);
    lv_obj_set_style_pad_column(b, X(14), 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    Row row;
    row.img = lv_image_create(b);
    lv_obj_set_size(row.img, net::COVER_THUMB_PX, net::COVER_THUMB_PX);
    lv_image_set_inner_align(row.img, LV_IMAGE_ALIGN_CENTER);
    lv_obj_set_style_bg_color(row.img, lv_color_hex(0x232326), 0);
    lv_obj_set_style_bg_opa(row.img, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row.img, S(6), 0);
    lv_obj_set_style_clip_corner(row.img, true, 0);
    if (it.thumb) {
        lv_image_set_src(row.img, &it.thumb->dsc);
        row.hasThumb = true;
    }
    lv_obj_t *texts = lv_obj_create(b);
    lv_obj_remove_style_all(texts);
    lv_obj_set_flex_grow(texts, 1);
    lv_obj_set_height(texts, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(texts, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *t = label(texts, fonts::ui20, COL_TEXT, it.title.c_str());
    lv_obj_set_width(t, lv_pct(100));
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
    if (it.subtitle.length()) {
        lv_obj_t *st = label(texts, fonts::ui16, COL_DIM, it.subtitle.c_str());
        lv_obj_set_width(st, lv_pct(100));
        lv_label_set_long_mode(st, LV_LABEL_LONG_MODE_DOTS);
    }
    bool playable = it.hint == "action_list" || it.hint == "action";
    label(b, fonts::icons, playable ? COL_ACCENT : COL_DIM, playable ? LV_SYMBOL_PLAY : LV_SYMBOL_RIGHT);
    lv_obj_add_event_cb(b, onRow, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    g_rows.push_back(row);
    return b;
}

void placeMoreButton() {
    if (g_moreButton) {
        lv_obj_delete(g_moreButton);
        g_moreButton = nullptr;
    }
    if (!g_page || g_page->total <= (int)g_page->items.size()) return;
    String t = "Show more (" + String(g_page->total - (int)g_page->items.size()) + " left)";
    g_moreButton = textButton(g_list, t.c_str(), COL_BUTTON, [](lv_event_t *) { send(net::LibOp::More); });
}

void fillRows(bool append) {
    if (!append) {
        lv_obj_clean(g_list);
        g_moreButton = nullptr;
        g_rows.clear();
        g_shownCount = 0;
    }
    for (size_t i = g_shownCount; i < g_page->items.size(); i++) addRow(i);
    g_shownCount = g_page->items.size();
    placeMoreButton();
    if (!append) lv_obj_scroll_to_y(g_list, 0, LV_ANIM_OFF);
}

void updateThumbs() {
    for (size_t i = 0; i < g_rows.size() && i < g_page->items.size(); i++) {
        Row &r = g_rows[i];
        const net::LibItem &it = g_page->items[i];
        if (r.img && !r.hasThumb && it.thumb) {
            lv_image_set_src(r.img, &it.thumb->dsc);
            r.hasThumb = true;
        }
    }
}

void showToast(const String &msg, bool error) {
    lv_label_set_text(g_toast, msg.c_str());
    lv_obj_set_style_text_color(g_toast, lv_color_hex(error ? COL_ERROR : COL_TEXT), 0);
    lv_obj_set_hidden(g_toast, false);
    lv_obj_move_foreground(g_toast);
    g_toastAt = millis();
}

void onAction(lv_event_t *e) {
    lv_obj_set_hidden(g_sheet, true);
    send(net::LibOp::Action, (int)(intptr_t)lv_event_get_user_data(e));
}

void showSheet() {
    bool open = g_page && !g_page->actions.empty();
    if (!open) {
        lv_obj_set_hidden(g_sheet, true);
        g_sheetFor = "";
        return;
    }
    if (g_sheetFor == g_page->actionsFor && !lv_obj_is_hidden(g_sheet)) return;
    g_sheetFor = g_page->actionsFor;
    lv_obj_clean(g_sheetBox);
    lv_obj_t *t = label(g_sheetBox, fonts::ui20, COL_MID, g_page->actionsFor.c_str());
    lv_obj_set_width(t, lv_pct(100));
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
    for (size_t i = 0; i < g_page->actions.size(); i++) {
        lv_obj_t *b = textButton(g_sheetBox, g_page->actions[i].title.c_str(), i == 0 ? COL_PRIMARY : COL_BUTTON,
                                 onAction, (void *)(intptr_t)i);
        lv_obj_set_width(b, lv_pct(100));
    }
    lv_obj_t *c = textButton(g_sheetBox, "Cancel", 0x18181B, [](lv_event_t *) {
        lv_obj_set_hidden(g_sheet, true);
        send(net::LibOp::CloseActions);
    });
    lv_obj_set_width(c, lv_pct(100));
    lv_obj_set_hidden(g_sheet, false);
    lv_obj_move_foreground(g_sheet);
}

void applyPage() {
    if (!g_page) return;
    if (g_page->id != g_shownId) {
        g_shownId = g_page->id;
        fillRows(false);
        if (!g_page->items.empty()) showKeyboard(false);   // new results: make room for them
    } else if (g_page->items.size() > g_shownCount) {
        fillRows(true);
    } else {
        updateThumbs();
    }
    if (g_page->messageAt != g_shownMessageAt && g_page->message.length()) {
        g_shownMessageAt = g_page->messageAt;
        showToast(g_page->message, g_page->error);
    }
    bool empty = g_page->items.empty();
    const char *info = g_page->loading ? "Loading..." : empty ? (g_page->error ? "" : "Nothing here") : "";
    lv_label_set_text(g_info, info);
    lv_obj_set_hidden(g_info, !*info);
    showHeader();
    showSheet();
}

void selectTab(Tab t) {
    g_tab = t;
    if (t == Tab::Browse) {
        showKeyboard(false);
        if (!g_page || g_page->hierarchy != "browse") send(net::LibOp::Browse);
    } else if (!g_page || g_page->hierarchy != "search") {
        lv_obj_clean(g_list);
        g_rows.clear();
        g_shownCount = 0;
        g_moreButton = nullptr;
        g_shownId = 0;
        lv_obj_set_hidden(g_info, true);
        showKeyboard(true);
    }
    showHeader();
}

void search() {
    String q = lv_textarea_get_text(g_ta);
    q.trim();
    if (!q.length()) return;
    showKeyboard(false);
    send(net::LibOp::Search, -1, q);
}

void buildHeader() {
    g_tabSearch = textButton(g_root, "Search", COL_PRIMARY, [](lv_event_t *) { selectTab(Tab::Search); });
    lv_obj_set_pos(g_tabSearch, X(20), Y(12));
    g_tabBrowse = textButton(g_root, "Browse", COL_BUTTON, [](lv_event_t *) { selectTab(Tab::Browse); });
    lv_obj_align_to(g_tabBrowse, g_tabSearch, LV_ALIGN_OUT_RIGHT_MID, X(10), 0);
    lv_obj_t *x = roundButton(g_root, LV_SYMBOL_CLOSE, [](lv_event_t *) { close(); });
    lv_obj_align(x, LV_ALIGN_TOP_RIGHT, -X(20), Y(10));
    g_zone = label(g_root, fonts::ui16, COL_DIM);
    lv_obj_set_width(g_zone, X(300));
    lv_label_set_long_mode(g_zone, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(g_zone, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(g_zone, LV_ALIGN_TOP_RIGHT, -X(20) - S(56) - X(14), Y(28));

    g_searchRow = lv_obj_create(g_root);
    lv_obj_remove_style_all(g_searchRow);
    lv_obj_set_size(g_searchRow, SCREEN_W - 2 * X(20), Y(56));
    lv_obj_set_pos(g_searchRow, X(20), Y(76));
    g_ta = lv_textarea_create(g_searchRow);
    lv_textarea_set_one_line(g_ta, true);
    lv_textarea_set_max_length(g_ta, 40);
    lv_textarea_set_placeholder_text(g_ta, "Artist, album, track...");
    lv_obj_set_style_text_font(g_ta, fonts::ui20, 0);
    lv_obj_set_style_text_color(g_ta, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_bg_color(g_ta, lv_color_hex(0x18181B), 0);
    lv_obj_set_style_border_color(g_ta, lv_color_hex(0x2F3037), 0);
    lv_obj_set_style_border_color(g_ta, lv_color_hex(COL_ACCENT), LV_STATE_FOCUSED);
    lv_obj_set_style_radius(g_ta, S(10), 0);
    lv_obj_set_width(g_ta, SCREEN_W - 2 * X(20) - X(150));
    lv_obj_align(g_ta, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(g_ta, [](lv_event_t *) { showKeyboard(true); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(g_ta, [](lv_event_t *) { search(); }, LV_EVENT_READY, nullptr);
    lv_obj_t *go = textButton(g_searchRow, "Search", COL_PRIMARY, [](lv_event_t *) { search(); });
    lv_obj_align(go, LV_ALIGN_RIGHT_MID, 0, 0);

    g_titleRow = lv_obj_create(g_root);
    lv_obj_remove_style_all(g_titleRow);
    lv_obj_set_size(g_titleRow, SCREEN_W - 2 * X(20), Y(56));
    lv_obj_set_pos(g_titleRow, X(20), Y(76));
    lv_obj_set_flex_flow(g_titleRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_titleRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(g_titleRow, X(14), 0);
    g_back = roundButton(g_titleRow, LV_SYMBOL_LEFT, [](lv_event_t *) { send(net::LibOp::Back); });
    lv_obj_set_size(g_back, S(48), S(48));
    g_title = label(g_titleRow, fonts::ui20, COL_TEXT);
    lv_obj_set_flex_grow(g_title, 1);
    lv_label_set_long_mode(g_title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_hidden(g_titleRow, true);
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
    buildHeader();

    g_list = lv_obj_create(g_root);
    lv_obj_remove_style_all(g_list);
    lv_obj_set_size(g_list, SCREEN_W - 2 * X(20), SCREEN_H - Y(144));
    lv_obj_set_pos(g_list, X(20), Y(140));
    lv_obj_set_flex_flow(g_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(g_list, Y(6), 0);
    lv_obj_set_style_pad_bottom(g_list, Y(16), 0);
    lv_obj_set_scroll_dir(g_list, LV_DIR_VER);

    g_info = label(g_root, fonts::ui20, COL_MID);
    lv_obj_align(g_info, LV_ALIGN_CENTER, 0, Y(40));
    lv_obj_set_hidden(g_info, true);

    g_kb = keyboard::create(g_root, g_ta, Y(260));
    lv_obj_add_event_cb(g_kb, [](lv_event_t *) { search(); }, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(g_kb, [](lv_event_t *) { showKeyboard(false); }, LV_EVENT_CANCEL, nullptr);

    g_toast = label(g_root, fonts::ui20, COL_TEXT);
    lv_obj_set_style_bg_color(g_toast, lv_color_hex(0x202127), 0);
    lv_obj_set_style_bg_opa(g_toast, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g_toast, S(12), 0);
    lv_obj_set_style_pad_hor(g_toast, X(20), 0);
    lv_obj_set_style_pad_ver(g_toast, Y(12), 0);
    lv_obj_set_style_max_width(g_toast, X(700), 0);
    lv_label_set_long_mode(g_toast, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(g_toast, LV_ALIGN_BOTTOM_MID, 0, -Y(20));
    lv_obj_set_hidden(g_toast, true);

    // Action sheet: Roon's actions for an item, over a dimmed screen.
    g_sheet = lv_obj_create(g_root);
    lv_obj_remove_style_all(g_sheet);
    lv_obj_set_size(g_sheet, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(g_sheet, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(g_sheet, LV_OPA_70, 0);
    lv_obj_set_clickable(g_sheet, true);
    g_sheetBox = lv_obj_create(g_sheet);
    lv_obj_remove_style_all(g_sheetBox);
    lv_obj_set_width(g_sheetBox, X(480));
    lv_obj_set_height(g_sheetBox, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(g_sheetBox, SCREEN_H - Y(40), 0);
    lv_obj_center(g_sheetBox);
    lv_obj_set_style_bg_color(g_sheetBox, lv_color_hex(0x18181B), 0);
    lv_obj_set_style_bg_opa(g_sheetBox, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g_sheetBox, S(16), 0);
    lv_obj_set_style_pad_all(g_sheetBox, S(20), 0);
    lv_obj_set_style_pad_row(g_sheetBox, Y(10), 0);
    lv_obj_set_flex_flow(g_sheetBox, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_hidden(g_sheet, true);

    lv_obj_set_hidden(g_root, true);
}

void open() {
    if (g_demo) {   // leaving the showcase: drop its invented page, the next refresh brings the real one
        g_demo = false;
        g_page = nullptr;
        g_shownId = 0;
        g_sheetFor = "";
        g_seenSerial = UINT32_MAX;
        lv_obj_set_hidden(g_sheet, true);
        lv_obj_clean(g_list);
        g_rows.clear();
        g_shownCount = 0;
        g_moreButton = nullptr;
        net::Snapshot s = net::snapshot();
        refresh(s);
    }
    net::library(net::LibOp::Open);
    lv_obj_set_hidden(g_root, false);
    lv_obj_move_foreground(g_root);
    if (!g_page) selectTab(Tab::Search);
    else applyPage();
}

void close() {
    if (!g_root) return;
    lv_obj_set_hidden(g_root, true);
    showKeyboard(false);
    if (!g_demo) net::library(net::LibOp::Close);
}

bool isOpen() { return g_root && !lv_obj_is_hidden(g_root); }

void refresh(const net::Snapshot &s) {
    if (!g_root || g_demo) return;
    String zone = s.zoneName.length() ? "Plays on: " + s.zoneName : String("No zone: pick one in the controls bar");
    if (strcmp(lv_label_get_text(g_zone), zone.c_str()) != 0) lv_label_set_text(g_zone, zone.c_str());
    if (s.librarySerial == g_seenSerial) return;
    g_seenSerial = s.librarySerial;
    g_page = s.library;
    g_waiting = false;
    if (g_page) g_tab = g_page->hierarchy == "browse" ? Tab::Browse : Tab::Search;
    if (isOpen()) applyPage();
}

void tick(uint32_t now) {
    if (g_toast && !lv_obj_is_hidden(g_toast) && now - g_toastAt > MESSAGE_MS) lv_obj_set_hidden(g_toast, true);
}

void showcase(const std::shared_ptr<const Picture> &thumb, bool actions) {
    auto page = std::make_shared<net::LibraryPage>();
    page->id = 0x5EED;
    page->hierarchy = "search";
    page->query = "neon";
    page->path = {"Results for \"neon\"", "Albums", "Night Drive"};
    const char *tracks[] = {"Play Album", "Neon Autumn", "City Lights at Two", "Radio Static Heart",
                            "Slow Train Home", "Paper Moon Avenue", "The Last Arcade"};
    for (size_t i = 0; i < sizeof tracks / sizeof *tracks; i++) {
        net::LibItem it;
        it.title = i ? String(i) + ". " + tracks[i] : tracks[i];
        it.subtitle = i ? "The Demo Lights" : "";
        it.hint = "action_list";
        it.thumb = thumb;
        page->items.push_back(it);
    }
    page->total = page->items.size();
    if (actions) {
        page->actionsFor = "1. Neon Autumn";
        for (const char *a : {"Play Now", "Add Next", "Queue", "Start Radio"}) {
            net::LibItem it;
            it.title = a;
            it.hint = "action";
            page->actions.push_back(it);
        }
    }
    g_demo = true;
    g_page = page;
    g_tab = Tab::Search;
    g_shownId = 0;
    g_sheetFor = "";
    lv_label_set_text(g_zone, "Plays on: Living Room");
    lv_obj_set_hidden(g_root, false);
    lv_obj_move_foreground(g_root);
    showKeyboard(false);
    applyPage();
}

}  // namespace libui
