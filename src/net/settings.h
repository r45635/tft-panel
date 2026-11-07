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
    int32_t leadMs = 250;  // show each lyric line this much before its timestamp
    bool backdrop = true;  // cover art as the screen background
    bool scroll = true;    // animate lyric lines sliding up
    int32_t sleepMin = 10; // nothing playing this long → backlight off (0 = never; dims after 1 min)
    uint8_t view = 0;      // last view chosen by a swipe: 0 = lyrics, 1 = cover
};

void begin();                 // loads from NVS
Settings get();               // copy taken under the lock (safe from any task)

void setWifi(const String &ssid, const String &pass);
void setCompanion(const String &url);
void setToken(const String &token);
void setZone(const String &zoneId);
void setLead(int32_t ms);
int32_t leadMs();              // cheap, lock-free (read every frame by the UI)
void setBackdrop(bool on);
bool backdrop();
void setScroll(bool on);
bool scroll();
void setSleepMin(int32_t minutes);
int32_t sleepMin();
void setView(uint8_t view);
uint8_t view();
void forgetWifi();
void eraseAll();

}  // namespace settings
