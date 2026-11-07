// TFT Panel — touch display client of the TextFromTrack Companion.
// Phase 0: hardware bring-up (display, touch, backlight).
#include <Arduino.h>
#include <lvgl.h>

#include "app/bringup_screen.h"
#include "board/board.h"

static bool g_ok = false;

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n[tft-panel] boot");

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

    bringup::show();
    lv_refr_now(board::display());   // first frame into the framebuffer…
    board::setBacklight(100);         // …then light it (Elecrow: backlight last)
    Serial.println("[tft-panel] bring-up screen shown");
}

void loop() {
    if (!g_ok) {
        delay(1000);
        return;
    }
    bringup::tick();
    lv_timer_handler();
    delay(5);
}
