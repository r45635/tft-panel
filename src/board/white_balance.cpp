#include "white_balance.h"

#include <esp_attr.h>

namespace board::wb {
namespace {
volatile uint32_t g_gainG = 256, g_gainB = 256;   // /256
volatile bool g_on = false;
}  // namespace

bool set(uint8_t percent) {
    if (percent > 100) percent = 100;
    g_gainB = 256 - percent * 115 / 100;
    g_gainG = 256 - percent * 31 / 100;
    g_on = percent > 0;
    return g_on;
}

bool on() { return g_on; }

// Hot loop (every flushed pixel), two RGB565 pixels per 32-bit word scaled at once: each
// 5- or 6-bit field times a gain of at most 256 stays inside its own 16-bit half.
IRAM_ATTR __attribute__((optimize("O2"))) void apply(uint16_t *p, size_t n) {
    if (!g_on) return;
    const uint32_t gG = g_gainG, gB = g_gainB;
    uint32_t *w = (uint32_t *)p;
    for (size_t i = 0; i < n / 2; i++) {
        uint32_t c = w[i];
        uint32_t b = (((c & 0x001F001Fu) * gB) >> 8) & 0x001F001Fu;
        uint32_t g = ((((c >> 5) & 0x003F003Fu) * gG) >> 8) & 0x003F003Fu;
        w[i] = (c & 0xF800F800u) | (g << 5) | b;
    }
    if (n & 1) p[n - 1] = colour(p[n - 1]);
}

uint16_t colour(uint16_t c) {
    if (!g_on) return c;
    uint32_t g = (((c >> 5) & 0x3F) * g_gainG) >> 8, b = ((c & 0x1F) * g_gainB) >> 8;
    return (uint16_t)((c & 0xF800) | (g << 5) | b);
}

}  // namespace board::wb
