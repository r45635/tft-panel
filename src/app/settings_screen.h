// On-screen settings (gear button in the controls bar): everything a user may need without
// the serial console. Each change is saved to NVS at once.
#pragma once

#include <lvgl.h>

#include "../net/net.h"
#include "../net/ota.h"

namespace settingsui {

// What the settings screen needs from the main UI.
struct Hooks {
    void (*brightnessPreview)(int percent);   // live backlight while the slider moves
    void (*openZones)();                      // the zone picker
    void (*confirmUpdate)();                  // the firmware update confirmation
};

void build(lv_obj_t *scr, const Hooks &hooks);
void open();
void close();
bool isOpen();
void showColourTest();   // the colour test card (also from the console: colours)

// Live values (Wi-Fi, firmware, about); cheap when nothing changed.
void refresh(const net::Snapshot &s, const ota::Status &o);

// Applies the stored time zone to the C library (localtime). Call at boot and on change.
void applyTz();

}  // namespace settingsui
