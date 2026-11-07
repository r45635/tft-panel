// Persistent panel settings, stored in NVS only (never in the repo or the firmware image).
#pragma once

#include <Arduino.h>

namespace settings {

struct Settings {
    String wifiSsid;
    String wifiPass;
    String companionUrl;   // "http://host:port"; empty = discover via mDNS
    String accessToken;    // companion ACCESS_TOKEN; empty = none
    String zoneId;         // pinned zone; empty = follow the playing zone
    String apPass;         // setup AP password, generated once per device
};

void begin();                 // loads from NVS
Settings get();               // copy taken under the lock (safe from any task)

void setWifi(const String &ssid, const String &pass);
void setCompanion(const String &url);
void setToken(const String &token);
void setZone(const String &zoneId);
void forgetWifi();
void eraseAll();

}  // namespace settings
