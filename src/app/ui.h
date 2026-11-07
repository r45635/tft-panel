// Main screens: setup (QR codes for the soft AP), status, and now playing with lyrics.
#pragma once

namespace ui {

void show();   // builds the screens and loads the main one
void tick();   // call from loop() before lv_timer_handler()

}  // namespace ui
