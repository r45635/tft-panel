// TFT Panel — touch display client of the TextFromTrack Companion.
// Phase 1: Wi-Fi setup portal, companion discovery, synced lyrics.
#include <Arduino.h>
#include <lvgl.h>
#include <mbedtls/base64.h>

#include "app/bringup_screen.h"
#include "app/fonts.h"
#include "app/settings_screen.h"
#include "app/ui.h"
#include "version.h"
#include "net/ota.h"
#include "board/board.h"
#include "board/board_pins.h"
#include "net/net.h"
#include "net/settings.h"

namespace {

bool g_ok = false;
bool g_bringup = false;
String g_line;

void printStatus() {
    net::Snapshot s = net::snapshot();
    settings::Settings st = settings::get();
    const board::Info &info = board::info();
    ota::Status o = ota::status();
    Serial.printf("[status] firmware %s (update: %s%s%s%s), %s\n", FW_VERSION, ota::stateName(o.state),
                  o.latest.length() ? ", latest " : "", o.latest.c_str(),
                  o.error.length() ? (", " + o.error).c_str() : "", s.status.c_str());
    Serial.printf("[status] wifi ssid=\"%s\" ip=%s portal=%d ap=\"%s\"\n", st.wifiSsid.c_str(), s.staIp.c_str(),
                  s.portalActive, s.apSsid.c_str());
    Serial.printf("[status] companion=%s (%s) token=%s\n", s.companion.c_str(),
                  st.companionUrl.length() ? "manual" : "mDNS", st.accessToken.length() ? "set" : "none");
    Serial.printf("[status] zone=\"%s\" %s state=%s track=\"%s - %s\" pos=%d/%u ms\n", s.zoneName.c_str(),
                  s.zonePinned ? "pinned" : "auto", s.state.c_str(), s.artist.c_str(), s.title.c_str(),
                  (int)s.positionAt(millis()), (unsigned)s.durationMs);
    Serial.printf("[status] lyrics status=%d lines=%u synced=%d offset=%d lead=%d source=%s clock window=%d ms\n",
                  (int)s.lyricsStatus, s.lyrics ? (unsigned)s.lyrics->lines.size() : 0u, s.lyrics ? s.lyrics->synced : 0,
                  (int)s.offsetMs, (int)settings::leadMs(), s.lyricsSource.c_str(), (int)s.positionUncertaintyMs);
    Serial.printf("[status] screen %s, view %s (saved %s), backdrop=%d scroll=%d sleep=%d min\n", ui::screenState(),
                  ui::viewName(), settings::view() ? "cover" : "lyrics", settings::backdrop(), settings::scroll(),
                  (int)settings::sleepMin());
    time_t now = time(nullptr);
    struct tm lt;
    localtime_r(&now, &lt);
    char when[32];
    strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", &lt);
    Serial.printf("[status] local time %s (%s), clock %s\n", lt.tm_year + 1900 >= 2025 ? when : "not set yet",
                  settings::tz().c_str(), settings::clock() ? "on" : "off");
    Serial.printf("[status] touch=%d heap int %u KB (min %u) psram %u KB\n", info.touch,
                  (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                  (unsigned)(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024),
                  (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
}

// Dumps the framebuffer as base64 lines "~..." of RLE pairs (u16 count, u16 RGB565, little
// endian), between "[shot] begin W H" and "[shot] end <pairs>". Each line is one write so
// the net task's logs never land inside one. Decoder: tools/screenshot.py.
void screenshot() {
    const uint16_t *fb = board::frameBuffer();
    if (!fb) {
        Serial.println("[shot] ERROR: no framebuffer");
        return;
    }
    const int n = board::LCD_H_RES * board::LCD_V_RES;
    Serial.printf("[shot] begin %d %d\n", board::LCD_H_RES, board::LCD_V_RES);
    uint8_t raw[192];   // 48 pairs → 256 base64 chars
    size_t used = 0;
    uint32_t pairs = 0;
    char line[300];
    auto flush = [&]() {
        size_t olen = 0;
        line[0] = '~';
        mbedtls_base64_encode((unsigned char *)line + 1, sizeof line - 3, &olen, raw, used);
        line[1 + olen] = '\n';
        Serial.write((const uint8_t *)line, olen + 2);
        used = 0;
    };
    for (int i = 0; i < n;) {
        uint16_t px = fb[i];
        uint16_t run = 1;
        while (i + run < n && fb[i + run] == px && run < 0xFFFF) run++;
        raw[used++] = run & 0xFF;
        raw[used++] = run >> 8;
        raw[used++] = px & 0xFF;
        raw[used++] = px >> 8;
        pairs++;
        i += run;
        if (used == sizeof raw) flush();
    }
    if (used) flush();
    Serial.printf("[shot] end %u\n", (unsigned)pairs);
}

void help() {
    Serial.println("[cmd] status | portal | wifi <ssid> [password] | forget-wifi | companion <http://host:port>|auto");
    Serial.println("[cmd] token <token>|none | zone <zone_id>|auto | lead <ms> | backdrop on|off | scroll on|off | sleep <min>|off | view lyrics|cover | ota check|install | settings [close] | tz <POSIX TZ>|default | warmth <0-100> | backlight <0-100> | demo [off] | screenshot | touchdiag | erase-all | bringup | restart");
}

// Development console on the USB serial port (115200).
void handleCommand(String line) {
    line.trim();
    if (!line.length()) return;
    int sp = line.indexOf(' ');
    String cmd = sp < 0 ? line : line.substring(0, sp);
    String arg = sp < 0 ? "" : line.substring(sp + 1);
    arg.trim();

    if (cmd == "status") printStatus();
    else if (cmd == "portal") net::openPortal();
    else if (cmd == "wifi" && arg.length()) {
        // The SSID may not contain spaces here; use the setup portal for those.
        int s2 = arg.indexOf(' ');
        settings::setWifi(s2 < 0 ? arg : arg.substring(0, s2), s2 < 0 ? "" : arg.substring(s2 + 1));
        net::reconnect();
        Serial.println("[cmd] Wi-Fi saved");
    } else if (cmd == "forget-wifi") {
        settings::forgetWifi();
        net::reconnect();
        Serial.println("[cmd] Wi-Fi forgotten");
    } else if (cmd == "companion" && arg.length()) {
        settings::setCompanion(arg == "auto" ? "" : arg);
        net::reconnect();
        Serial.printf("[cmd] companion = %s\n", arg.c_str());
    } else if (cmd == "token" && arg.length()) {
        settings::setToken(arg == "none" ? "" : arg);
        Serial.println("[cmd] token saved");
    } else if (cmd == "zone" && arg.length()) {
        settings::setZone(arg == "auto" ? "" : arg);
        net::reconnect();
        Serial.printf("[cmd] zone = %s\n", arg.c_str());
    } else if (cmd == "lead" && arg.length()) {
        settings::setLead(arg.toInt());
        Serial.printf("[cmd] lyrics lead = %d ms\n", (int)settings::leadMs());
    } else if (cmd == "touchdiag") {
        board::touchDiag();
    } else if (cmd == "backdrop" && arg.length()) {
        settings::setBackdrop(arg != "off");
        Serial.printf("[cmd] cover backdrop %s\n", settings::backdrop() ? "on" : "off");
    } else if (cmd == "scroll" && arg.length()) {
        settings::setScroll(arg != "off");
        Serial.printf("[cmd] lyrics scroll %s\n", settings::scroll() ? "on" : "off");
    } else if (cmd == "sleep" && arg.length()) {
        settings::setSleepMin(arg == "off" ? 0 : arg.toInt());
        if (settings::sleepMin()) Serial.printf("[cmd] backlight off after %d min without playback\n", (int)settings::sleepMin());
        else Serial.println("[cmd] backlight never turns off (still dims after 1 min)");
    } else if (cmd == "ota" && (arg == "check" || arg == "install")) {
        if (arg == "check") ota::requestCheck();
        else ota::requestInstall();
        Serial.printf("[cmd] ota %s requested\n", arg.c_str());
    } else if (cmd == "settings") {
        if (arg == "close") settingsui::close();
        else {
            settingsui::open();
            settingsui::refresh(net::snapshot(), ota::status());
        }
    } else if (cmd == "tz" && arg.length()) {
        settings::setTz(arg == "default" ? "" : arg);
        settingsui::applyTz();
        Serial.printf("[cmd] time zone %s\n", settings::tz().c_str());
    } else if (cmd == "view" && arg.length()) {
        ui::setView(arg == "cover");
        Serial.printf("[cmd] view %s\n", arg == "cover" ? "cover" : "lyrics");
    } else if (cmd == "warmth" && arg.length()) {
        settings::setWarmth((uint8_t)constrain(arg.toInt(), 0, 100));
        board::setWarmth(settings::warmth());
        Serial.printf("[cmd] colour warmth %d %%\n", (int)settings::warmth());
    } else if (cmd == "backlight" && arg.length()) {
        // Raw PWM level for hardware checks; the sleep logic takes over again on its next change.
        board::setBacklight((uint8_t)constrain(arg.toInt(), 0, 100));
        Serial.printf("[cmd] backlight %d %%\n", (int)constrain(arg.toInt(), 0, 100));
    } else if (cmd == "demo") {
        ui::setDemo(arg != "off");
        Serial.printf("[cmd] demo %s\n", arg == "off" ? "off" : "on (\"demo off\" to go back)");
    } else if (cmd == "screenshot") {
        // ESP-IDF logs bypass Serial's lock and could land inside a data line: hold the
        // net task (the only other logger) during the dump. An in-flight request finishes first.
        net::pause(true);
        delay(300);
        screenshot();
        net::pause(false);
    } else if (cmd == "erase-all") {
        settings::eraseAll();
        Serial.println("[cmd] settings erased, restarting");
        delay(200);
        board::restart();
    } else if (cmd == "bringup") {
        g_bringup = true;
        bringup::show();
    } else if (cmd == "restart") board::restart();
    else help();
}

void pollSerial() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (g_line.length()) handleCommand(g_line);
            g_line = "";
        } else if (g_line.length() < 200) {
            g_line += c;
        }
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.printf("\n[tft-panel] boot, firmware %s\n", FW_VERSION);

    board::cleanResetIfNeeded();   // before touching the display: see board.h
    g_ok = board::begin();
    const board::Info &info = board::info();
    Serial.printf("[tft-panel] psram=%u flash=%u i2c=[%s] pca9557=%d touch=%d addr=0x%02X id=%s fw=0x%04X res=%ux%u\n",
                  (unsigned)info.psramBytes, (unsigned)info.flashBytes, info.i2cDevices.c_str(),
                  info.pca9557, info.touch, info.touchAddr, info.touchId, info.touchFw,
                  info.touchXRes, info.touchYRes);
    if (info.error.length()) Serial.printf("[tft-panel] ERROR: %s\n", info.error.c_str());
    if (!g_ok) {
        Serial.println("[tft-panel] FATAL: display not initialised, halting");
        return;
    }

    if (!fonts::begin()) Serial.println("[tft-panel] ERROR: TTF fonts unavailable, accents will be missing");
    settings::begin();
    net::begin();
    board::setWarmth(settings::warmth());   // white balance before the first frame
    ui::show();
    lv_refr_now(board::display());   // first frame into the framebuffer…
    board::setBacklight(settings::brightness());   // …then light it (Elecrow: backlight last); ui owns it from here
    Serial.println("[tft-panel] ui shown; type 'help' on this console for commands");
}

void loop() {
    if (!g_ok) {
        delay(1000);
        return;
    }
    pollSerial();
    if (g_bringup) bringup::tick();
    else ui::tick();
    lv_timer_handler();
    delay(5);
}
