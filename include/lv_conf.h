// LVGL 9.6 configuration for TFT Panel. Only non-default values are set here;
// lv_conf_internal.h supplies the rest.
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

// Use the C library allocator: on ESP32 with PSRAM, large blocks go to PSRAM.
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

#define LV_USE_OS LV_OS_NONE
#define LV_DEF_REFR_PERIOD 16

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1

// FPS / CPU overlay for bring-up and performance work.
#define LV_USE_SYSMON 1
#define LV_USE_PERF_MONITOR 1
#define LV_USE_PERF_MONITOR_POS LV_ALIGN_BOTTOM_RIGHT

// Fonts. LVGL's built-in Montserrat is ASCII + symbols only: no accents.
// Lyrics use the embedded Noto Sans through tiny_ttf; Montserrat stays for symbols.
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_20

// Run-time TTF rendering for the embedded Noto Sans (src/app/fonts.cpp). Glyph and
// kerning caches are per font; the bitmaps go to PSRAM through the C allocator.
#define LV_USE_TINY_TTF 1
#define LV_TINY_TTF_FILE_SUPPORT 0
#define LV_TINY_TTF_CACHE_GLYPH_CNT 256
#define LV_TINY_TTF_CACHE_KERNING_CNT 256

// Setup screen: QR codes to join the panel's access point and open its page.
#define LV_USE_QRCODE 1

#define LV_BUILD_EXAMPLES 0
#define LV_BUILD_DEMOS 0

#endif  // LV_CONF_H
