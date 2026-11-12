#include "fonts.h"

#include <Arduino.h>

#include "../layout.h"

// Embedded by platformio.ini (board_build.embed_files).
extern const uint8_t noto400_start[] asm("_binary_assets_fonts_NotoSans_400_ttf_start");
extern const uint8_t noto400_end[] asm("_binary_assets_fonts_NotoSans_400_ttf_end");
extern const uint8_t noto600_start[] asm("_binary_assets_fonts_NotoSans_600_ttf_start");
extern const uint8_t noto600_end[] asm("_binary_assets_fonts_NotoSans_600_ttf_end");

namespace fonts {

const lv_font_t *lyricsCurrent = &lv_font_montserrat_28;
const lv_font_t *lyricsOther = &lv_font_montserrat_28;
const lv_font_t *ui20 = &lv_font_montserrat_20;
const lv_font_t *ui16 = &lv_font_montserrat_14;
const lv_font_t *clock = &lv_font_montserrat_28;

// Symbol fallbacks (LVGL's Montserrat has fixed sizes): the closest to the scaled text.
#if LV_FONT_MONTSERRAT_36
const lv_font_t *icons = &lv_font_montserrat_36;
#define SYM_BIG &lv_font_montserrat_36
#define SYM_MID &lv_font_montserrat_24
#define SYM_SMALL &lv_font_montserrat_18
#else
const lv_font_t *icons = &lv_font_montserrat_28;
#define SYM_BIG &lv_font_montserrat_28
#define SYM_MID &lv_font_montserrat_20
#define SYM_SMALL &lv_font_montserrat_14
#endif

namespace {

const lv_font_t *make(const uint8_t *start, const uint8_t *end, int32_t px, const lv_font_t *symbols,
                      const lv_font_t *fallbackIfFailed, bool &ok) {
    // Glyph cache per font: one lyrics screen holds ~150 distinct glyphs per size.
    lv_font_t *f = lv_tiny_ttf_create_data_ex(start, end - start, px, LV_FONT_KERNING_NORMAL, 256);
    if (!f) {
        Serial.printf("[fonts] ERROR: tiny_ttf could not load the %d px font, using Montserrat (no accents)\n", (int)px);
        ok = false;
        return fallbackIfFailed;
    }
    f->fallback = symbols;
    return f;
}

}  // namespace

bool begin() {
    bool ok = true;
    using layout::S;
    lyricsCurrent = make(noto600_start, noto600_end, S(44), SYM_BIG, SYM_BIG, ok);
    lyricsOther = make(noto400_start, noto400_end, S(30), SYM_BIG, SYM_BIG, ok);
    ui20 = make(noto400_start, noto400_end, S(20), SYM_MID, SYM_MID, ok);
    ui16 = make(noto400_start, noto400_end, S(16), SYM_SMALL, SYM_SMALL, ok);
    clock = make(noto600_start, noto600_end, S(128), SYM_BIG, SYM_BIG, ok);
    return ok;
}

}  // namespace fonts
