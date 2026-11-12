#include "white_balance.h"

#include <esp_attr.h>

namespace board::wb {
namespace {
uint16_t g_lut[2048];
volatile bool g_on = false;
}  // namespace

bool set(uint8_t percent) {
    if (percent > 100) percent = 100;
    uint32_t gainB = 256 - percent * 115 / 100, gainG = 256 - percent * 31 / 100;
    g_on = false;   // the flush runs in the caller's (LVGL) thread: no race on the table
    for (uint32_t v = 0; v < 2048; v++) {
        uint32_t g = (v >> 5) & 0x3F, b = v & 0x1F;
        g_lut[v] = (uint16_t)((((g * gainG) >> 8) << 5) | ((b * gainB) >> 8));
    }
    g_on = percent > 0;
    return g_on;
}

bool on() { return g_on; }

// Hot loop (every flushed pixel): speed-optimised and in IRAM, two pixels per 32-bit word.
IRAM_ATTR __attribute__((optimize("O2"))) void apply(uint16_t *p, size_t n) {
    if (!g_on) return;
    const uint16_t *lut = g_lut;
    uint32_t *w = (uint32_t *)p;
    for (size_t i = 0; i < n / 2; i++) {
        uint32_t c = w[i];
        w[i] = (c & 0xF800F800u) | lut[c & 0x07FF] | ((uint32_t)lut[(c >> 16) & 0x07FF] << 16);
    }
    if (n & 1) p[n - 1] = (p[n - 1] & 0xF800) | lut[p[n - 1] & 0x07FF];
}

uint16_t colour(uint16_t c) { return g_on ? (c & 0xF800) | g_lut[c & 0x07FF] : c; }

}  // namespace board::wb
