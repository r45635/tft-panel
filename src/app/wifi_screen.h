// Wi-Fi setup on the panel itself: the networks it hears, then the password on a touch keyboard.
// Opened from the setup screen ("Set up on this screen") and from Settings > Wi-Fi > Change.
// Saving writes the settings and restarts the panel, like the setup page on the phone.
#pragma once

#include <lvgl.h>

#include "../net/net.h"

namespace wifiui {

void build(lv_obj_t *scr);
void open();    // asks the net task for a fresh scan
void close();
bool isOpen();
void refresh(const net::Snapshot &s);   // new scan results, scanning state

// Showcase: invented networks; `password` opens the password page for the first one, partly typed.
void showcase(bool password);

}  // namespace wifiui
