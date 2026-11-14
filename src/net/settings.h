// Persistent panel settings, stored in NVS only (never in the repo or the firmware image).
#pragma once

#include <Arduino.h>

#include "../board/board_pins.h"

namespace settings {

// Where the panel gets what it shows. The companion is the recommended path and the only one
// with lyrics; Direct talks to the music server itself, for people who will not run it.
enum class SourceKind : uint8_t { Companion = 0, Direct = 1 };

struct Settings {
    String wifiSsid;
    String wifiPass;
    String companionUrl;   // "http://host:port"; empty = discover via mDNS
    String accessToken;    // companion ACCESS_TOKEN; empty = none
    String zoneId;         // pinned zone; empty = follow the playing zone
    SourceKind source = SourceKind::Companion;
    String serverHost;     // direct mode: "host:port" to skip discovery; empty = find it
    String serverId;       // direct mode: the server this pairing belongs to
    String serverToken;    // direct mode: the pairing token, so it is asked for only once
    String lyricsUrl;      // direct mode: an LRCLIB-compatible server; empty = no lyrics
    String apPass;         // setup AP password, generated once per device
    int32_t leadMs = 250;  // show each lyric line this much before its timestamp
    bool backdrop = true;  // cover art as the screen background
    bool scroll = true;    // animate lyric lines sliding up
    int32_t sleepMin = 10; // nothing playing this long → backlight off (0 = never; dims after 1 min)
    uint8_t view = 0;      // last view chosen by a swipe: 0 = lyrics, 1 = cover
    uint8_t brightness = 100;  // backlight when awake, 10..100 %
    bool clock = true;     // big clock on the dimmed screen when nothing plays
    uint8_t warmth = board::DEFAULT_WARMTH;   // colour warmth 0..100 %, against a bluish panel white
    String tz;             // POSIX TZ string; empty = DEFAULT_TZ
};

// US Eastern by default; changed on the settings screen (console: tz <POSIX TZ>).
constexpr const char *DEFAULT_TZ = "EST5EDT,M3.2.0,M11.1.0";

void begin();                 // loads from NVS
Settings get();               // copy taken under the lock (safe from any task)

void setWifi(const String &ssid, const String &pass);
void setCompanion(const String &url);
void setToken(const String &token);
void setZone(const String &zoneId);
void setSource(SourceKind kind);
SourceKind source();
void setServerHost(const String &hostPort);
void setLyricsUrl(const String &url);
// Direct mode's pairing, kept per server so a different one asks again.
void setPairing(const String &serverId, const String &token);
void forgetPairing();
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
void setBrightness(uint8_t percent);
uint8_t brightness();
void setClock(bool on);
bool clock();
void setWarmth(uint8_t percent);
uint8_t warmth();
void setTz(const String &tz);
String tz();                   // never empty: DEFAULT_TZ when unset
void forgetWifi();
void eraseAll();

}  // namespace settings
