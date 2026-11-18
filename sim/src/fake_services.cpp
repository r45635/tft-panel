// What the screens ask of the rest of the firmware, answered on the host: settings in memory,
// no network (the UI runs in its showcase mode, fed by src/app/showcase.cpp), no updates.
#include <Arduino.h>

#include "../../src/app/showcase.h"
#include "../../src/net/lyrics_source.h"
#include "../../src/net/net.h"
#include "../../src/net/ota.h"
#include "../../src/net/settings.h"
#include "../../src/net/web_update.h"
#include "../../src/net/weather.h"
#include "../../src/version.h"

// The version and settings of the panel that took docs/images, so a picture can be compared with
// its device capture pixel for pixel. git describe would change them with every checkout.
const char *const FW_VERSION = "v0.1.18-30-ge716008-dirty";

namespace settings {
namespace {
Settings capturePanel() {
    Settings s;
    s.brightness = 72;
    s.warmth = 0;  // tools/showcase.py captures at 0 %: the UI's own colours
    s.sleepMin = 5;
    s.leadMs = 350;
    return s;
}
Settings g = capturePanel();
}
void begin() {}
Settings get() { return g; }
void setWifi(const String &ssid, const String &pass) { g.wifiSsid = ssid, g.wifiPass = pass; }
void setCompanion(const String &url) { g.companionUrl = url; }
void setToken(const String &token) { g.accessToken = token; }
void setZone(const String &zoneId) { g.zoneId = zoneId; }
void setSource(SourceKind kind) { g.source = kind; }
SourceKind source() { return g.source; }
void setServerHost(const String &hostPort) { g.serverHost = hostPort; }
void setLyricsUrl(const String &url) { g.lyricsUrl = url; }
void setLyricsToken(const String &token) { g.lyricsToken = token; }
void setWeatherPlace(const String &place) { g.weatherPlace = place; }
String webToken() { return g.webToken; }
void forgetWebTokens() { g.webToken = String(); }
void setNewsUrl(const String &url) { g.newsUrl = url; }
void setPairing(const String &serverId, const String &token) { g.serverId = serverId, g.serverToken = token; }
void forgetPairing() { setPairing(String(), String()); }
void setLead(int32_t ms) { g.leadMs = ms; }
int32_t leadMs() { return g.leadMs; }
void setBackdrop(bool on) { g.backdrop = on; }
bool backdrop() { return g.backdrop; }
void setScroll(bool on) { g.scroll = on; }
bool scroll() { return g.scroll; }
void setSleepMin(int32_t minutes) { g.sleepMin = minutes; }
int32_t sleepMin() { return g.sleepMin; }
void setView(uint8_t view) { g.view = view; }
uint8_t view() { return g.view; }
void setBrightness(uint8_t percent) { g.brightness = percent; }
uint8_t brightness() { return g.brightness; }
void setClock(bool on) { g.clock = on; }
bool clock() { return g.clock; }
void setWarmth(uint8_t percent) { g.warmth = percent; }
uint8_t warmth() { return g.warmth; }
void setTz(const String &tz) { g.tz = tz; }
String tz() { return g.tz.length() ? g.tz : String(DEFAULT_TZ); }
void forgetWifi() { setWifi(String(), String()); }
void eraseAll() { g = capturePanel(); }
}  // namespace settings

namespace net {
// Same as src/net/net.cpp, which needs the network stack to compile.
int32_t Snapshot::positionAt(uint32_t now) const {
    int32_t p = positionMs;
    if (playing()) p += (int32_t)(now - positionAtMs);
    if (durationMs && p > (int32_t)durationMs) p = durationMs;
    return p < 0 ? 0 : p;
}

void begin() {}
String hostName() { return "tft-panel-demo"; }
String macAddress() { return "02:00:00:00:00:00"; }
String displayName() { return "TFT Panel demo"; }
uint32_t version() { return 1; }
Snapshot snapshot() { return showcase::make(98000); }
void openPortal() {}
void requestScan() {}
void reconnect() {}
void pause(bool) {}
void control(const char *) {}
void volumeStep(int) {}
void selectZone(const String &) {}
void volumeSet(float) {}
void mute(bool) {}
void setOffset(int32_t) {}
void library(LibOp, int, const String &) {}

namespace weather {
const char *describe(uint8_t) { return "Clear"; }
}
namespace lyricsrc {
void forgetDiscovered() {}
}
}  // namespace net

namespace ota {
uint32_t version() { return 1; }
Status status() { return Status(); }
void requestCheck() {}
void requestInstall() {}
const char *stateName(State) { return "idle"; }
}  // namespace ota

namespace webupdate {
uint32_t activeCode() { return 0; }
void cancelCode() {}
}  // namespace webupdate
