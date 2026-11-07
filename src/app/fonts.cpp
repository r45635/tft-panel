#include "fonts.h"

#include <Arduino.h>

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
    lyricsCurrent = make(noto600_start, noto600_end, 44, &lv_font_montserrat_28, &lv_font_montserrat_28, ok);
    lyricsOther = make(noto400_start, noto400_end, 30, &lv_font_montserrat_28, &lv_font_montserrat_28, ok);
    ui20 = make(noto400_start, noto400_end, 20, &lv_font_montserrat_20, &lv_font_montserrat_20, ok);
    ui16 = make(noto400_start, noto400_end, 16, &lv_font_montserrat_14, &lv_font_montserrat_14, ok);
    clock = make(noto600_start, noto600_end, 128, &lv_font_montserrat_28, &lv_font_montserrat_28, ok);
    return ok;
}

}  // namespace fonts
