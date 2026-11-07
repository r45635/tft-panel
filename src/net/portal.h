// Setup portal: a WPA2 soft AP with a captive web page to enter the home Wi-Fi and,
// optionally, the companion address and access token. GET only reads; the single
// write is POST /save.
#pragma once

#include <Arduino.h>

namespace portal {

// Starts the AP (keeps the station side if it is already configured) and the web server.
void start(const String &ssid, const String &pass);
void stop();
bool active();
void loop();                 // serve DNS + HTTP; call often from the net task

// Set by POST /save; the net task restarts the panel when it sees it.
bool savedAndRestartPending();

}  // namespace portal
