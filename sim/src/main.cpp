// TFT Panel simulator: the firmware's real screens (src/app) on a desktop, in their showcase mode
// (invented song, lyrics, zones and cover). Headless, it writes one PNG per screen named on the
// command line; with --window it opens an SDL window driven by the mouse.
//
//   tft-sim --out DIR [--shots lyrics,cover,clock,library,settings]
//   tft-sim --window
#include <Arduino.h>
#include <lvgl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <zlib.h>

#include <chrono>
#include <filesystem>
#include <thread>
#include <vector>

#include "../../src/app/fonts.h"
#include "../../src/app/ui.h"
#include "../../src/board/board.h"
#include "../../src/layout.h"
#include "../../src/net/settings.h"

using layout::SCREEN_H;
using layout::SCREEN_W;

namespace {

bool g_realTime = false;
// A panel that has been up a minute: the UI reads "less than 1.5 s since 0" as a finger still on
// the volume slider, and a real panel is never that young when someone opens it.
uint32_t g_virtualMs = 60000;
lv_display_t *g_display = nullptr;
std::vector<uint16_t> g_frame;   // RGB565, what the panel would show

uint32_t realMs() {
    using namespace std::chrono;
    static const auto start = steady_clock::now();
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now() - start).count();
}

void flushDone(lv_display_t *d, const lv_area_t *, uint8_t *) { lv_display_flush_ready(d); }

lv_display_t *headlessDisplay() {
    g_frame.assign((size_t)SCREEN_W * SCREEN_H, 0);
    lv_display_t *d = lv_display_create(SCREEN_W, SCREEN_H);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(d, g_frame.data(), nullptr, g_frame.size() * sizeof(uint16_t),
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(d, flushDone);
    return d;
}

// Advances the clock in 16 ms frames, as the firmware's loop() does, so animations settle.
void run(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 16) {
        ui::tick();
        lv_timer_handler();
        g_virtualMs += 16;
    }
    ui::tick();
    lv_refr_now(g_display);
}

void put32(std::vector<uint8_t> &out, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) out.push_back((uint8_t)(v >> s));
}

void chunk(std::vector<uint8_t> &out, const char *type, const std::vector<uint8_t> &data) {
    put32(out, (uint32_t)data.size());
    size_t at = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    put32(out, (uint32_t)crc32(0, out.data() + at, (uInt)(out.size() - at)));
}

bool writePng(const char *path) {
    std::vector<uint8_t> raw;
    raw.reserve((size_t)SCREEN_H * (1 + SCREEN_W * 3));
    for (int y = 0; y < SCREEN_H; y++) {
        raw.push_back(0);   // filter: none
        for (int x = 0; x < SCREEN_W; x++) {
            uint16_t p = g_frame[(size_t)y * SCREEN_W + x];
            uint8_t r = p >> 11, g = (p >> 5) & 0x3F, b = p & 0x1F;
            raw.push_back((uint8_t)(r << 3 | r >> 2));
            raw.push_back((uint8_t)(g << 2 | g >> 4));
            raw.push_back((uint8_t)(b << 3 | b >> 2));
        }
    }
    uLongf zlen = compressBound(raw.size());
    std::vector<uint8_t> z(zlen);
    if (compress2(z.data(), &zlen, raw.data(), raw.size(), 9) != Z_OK) return false;
    z.resize(zlen);

    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<uint8_t> ihdr;
    put32(ihdr, SCREEN_W);
    put32(ihdr, SCREEN_H);
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});   // 8-bit RGB
    chunk(png, "IHDR", ihdr);
    chunk(png, "IDAT", z);
    chunk(png, "IEND", {});
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite(png.data(), 1, png.size(), f) == png.size();
    return fclose(f) == 0 && ok;
}

void start(bool window) {
    g_realTime = window;
    lv_init();
    lv_tick_set_cb(millis);
    lv_log_register_print_cb([](lv_log_level_t, const char *line) { fputs(line, stderr); });
    if (window) {
        g_display = lv_sdl_window_create(SCREEN_W, SCREEN_H);
        lv_sdl_window_set_title(g_display, "TFT Panel simulator");
        lv_sdl_mouse_create();
    } else {
        g_display = headlessDisplay();
    }
    if (!fonts::begin()) fprintf(stderr, "[sim] TTF fonts unavailable\n");
    ui::show();
    ui::setDemo(true);
}

// Same sequence as tools/showcase.py on a panel: "show none", then the screen.
int shoot(const String &name, const String &out) {
    ui::showcase("none");
    if (!ui::showcase(name)) {
        fprintf(stderr, "[sim] unknown screen: %s\n", name.c_str());
        return 1;
    }
    run(1500);
    String path = out + "/" + name + "-" + String(SCREEN_W) + "x" + String(SCREEN_H) + ".png";
    if (!writePng(path.c_str())) {
        fprintf(stderr, "[sim] cannot write %s\n", path.c_str());
        return 1;
    }
    printf("%s\n", path.c_str());
    return 0;
}

int usage() {
    fprintf(stderr,
            "usage: tft-sim --out DIR [--shots NAME,...]   headless, one PNG per screen\n"
            "       tft-sim --window                       SDL window, mouse as touch\n"
            "NAME: any showcase screen of the console's `show` command (lyrics, cover, clockview,\n"
            "controls, settings, colours, volume, offset, zones, clock, update, confirm, progress,\n"
            "setup, searching, library, library-actions, wifi, wifi-password, code)\n");
    return 2;
}

}  // namespace

uint32_t millis() { return g_realTime ? realMs() : g_virtualMs; }

namespace board {
lv_display_t *display() { return g_display; }
const uint16_t *frameBuffer() { return g_frame.data(); }
bool lastTouch(uint16_t &, uint16_t &) { return false; }
const Info &info() {
    static Info i;
    return i;
}
void flushStats(uint32_t &count, uint32_t &us) { count = us = 0; }
void setBacklight(uint8_t) {}
void setWarmth(uint8_t) {}
void staticScreenBegin(const StaticScreen &) {}
void staticScreenProgress(int) {}
void staticScreenEnd() {}
bool staticScreenActive() { return false; }
[[noreturn]] void restart() { exit(0); }
}  // namespace board

int main(int argc, char **argv) {
    String out, shots = "lyrics,cover,clock,library,settings";
    bool window = false;
    for (int i = 1; i < argc; i++) {
        String a = argv[i];
        if (a == "--window") window = true;
        else if (a == "--out" && i + 1 < argc) out = argv[++i];
        else if (a == "--shots" && i + 1 < argc) shots = argv[++i];
        else return usage();
    }
    if (!window && !out.length()) return usage();
    std::error_code dirError;
    if (!window) std::filesystem::create_directories(out.c_str(), dirError);

    if (window) {
        start(true);
        for (;;) {
            ui::tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(lv_timer_handler()));
        }
    }

    // One process per screen, so a picture never depends on the screens drawn before it.
    int failed = 0, saved = 0;
    for (int from = 0; from <= (int)shots.length();) {
        int comma = shots.indexOf(',', from);
        if (comma < 0) comma = shots.length();
        String name = shots.substring(from, comma);
        from = comma + 1;
        if (!name.length()) continue;
        fflush(stdout);
        pid_t pid = fork();
        if (pid == 0) {
            start(false);
            exit(shoot(name, out));
        }
        int status = 0;
        if (pid > 0 && waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0) saved++;
        else failed++;
    }
    return failed || !saved ? 1 : 0;
}
