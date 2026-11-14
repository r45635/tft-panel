#include "settings.h"

#include <Preferences.h>
#include <bootloader_random.h>

namespace settings {
namespace {

constexpr const char *NS = "tftpanel";
Settings g_s;
Preferences g_prefs;
SemaphoreHandle_t g_lock = nullptr;
volatile int32_t g_leadMs = 250;
volatile bool g_backdrop = true;
volatile bool g_scroll = true;
volatile int32_t g_sleepMin = 10;
volatile uint8_t g_view = 0;
volatile uint8_t g_brightness = 100;
volatile bool g_clock = true;
volatile uint8_t g_warmth = board::DEFAULT_WARMTH;

void putString(const char *key, const String &value) {
    g_prefs.begin(NS, false);
    if (value.length()) g_prefs.putString(key, value);
    else g_prefs.remove(key);
    g_prefs.end();
}

}  // namespace

void begin() {
    if (!g_lock) g_lock = xSemaphoreCreateMutex();
    // Read-write so the namespace is created on first boot (read-only fails when absent).
    g_prefs.begin(NS, false);
    // isKey() first: getString() logs an error for every key that was never set.
    auto read = [](const char *key) { return g_prefs.isKey(key) ? g_prefs.getString(key, "") : String(); };
    g_s.wifiSsid = read("ssid");
    g_s.wifiPass = read("pass");
    g_s.companionUrl = read("companion");
    g_s.accessToken = read("token");
    g_s.zoneId = read("zone");
    g_s.source = g_prefs.isKey("source") && g_prefs.getUChar("source", 0) == 1 ? SourceKind::Direct
                                                                               : SourceKind::Companion;
    g_s.serverHost = read("srv_host");
    g_s.serverId = read("srv_id");
    g_s.serverToken = read("srv_token");
    g_s.lyricsUrl = read("lyrics_url");
    g_s.apPass = read("ap_pass");
    g_s.leadMs = g_prefs.isKey("lead") ? g_prefs.getInt("lead", 250) : 250;
    g_leadMs = g_s.leadMs;
    g_s.backdrop = g_prefs.isKey("backdrop") ? g_prefs.getBool("backdrop", true) : true;
    g_backdrop = g_s.backdrop;
    g_s.scroll = g_prefs.isKey("scroll") ? g_prefs.getBool("scroll", true) : true;
    g_scroll = g_s.scroll;
    g_s.sleepMin = g_prefs.isKey("sleep") ? g_prefs.getInt("sleep", 10) : 10;
    g_sleepMin = g_s.sleepMin;
    g_s.view = g_prefs.isKey("view") ? g_prefs.getUChar("view", 0) : 0;
    g_view = g_s.view;
    g_s.brightness = g_prefs.isKey("bright") ? g_prefs.getUChar("bright", 100) : 100;
    g_brightness = g_s.brightness;
    g_s.clock = g_prefs.isKey("clock") ? g_prefs.getBool("clock", true) : true;
    g_clock = g_s.clock;
    g_s.warmth = g_prefs.isKey("warmth") ? g_prefs.getUChar("warmth", board::DEFAULT_WARMTH) : board::DEFAULT_WARMTH;
    g_warmth = g_s.warmth;
    g_s.tz = read("tz");
    if (g_s.apPass.length() < 8) {
        // Setup AP password: random once per device, then stable so a reboot during
        // setup does not invalidate the network the phone has already joined.
        static const char alphabet[] = "abcdefghjkmnpqrstuvwxyz23456789";   // no l/1/o/0/i
        g_s.apPass = "";
        bootloader_random_enable();   // true entropy before the radio is on (Wi-Fi not started yet)
        for (int i = 0; i < 10; i++) g_s.apPass += alphabet[esp_random() % (sizeof alphabet - 1)];
        bootloader_random_disable();
        g_prefs.putString("ap_pass", g_s.apPass);
    }
    g_prefs.end();
}

Settings get() {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    Settings copy = g_s;
    xSemaphoreGive(g_lock);
    return copy;
}

// Writers run from the serial console or the setup portal; readers are the net task.
// Strings are replaced under a lock so a reader never sees a half-written value.
void setWifi(const String &ssid, const String &pass) {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    g_s.wifiSsid = ssid;
    g_s.wifiPass = pass;
    xSemaphoreGive(g_lock);
    putString("ssid", ssid);
    putString("pass", pass);
}

void setCompanion(const String &url) {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    g_s.companionUrl = url;
    xSemaphoreGive(g_lock);
    putString("companion", url);
}

void setToken(const String &token) {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    g_s.accessToken = token;
    xSemaphoreGive(g_lock);
    putString("token", token);
}

void setZone(const String &zoneId) {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    g_s.zoneId = zoneId;
    xSemaphoreGive(g_lock);
    putString("zone", zoneId);
}

void setSource(SourceKind kind) {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    g_s.source = kind;
    xSemaphoreGive(g_lock);
    g_prefs.begin(NS, false);
    g_prefs.putUChar("source", (uint8_t)kind);
    g_prefs.end();
}

SourceKind source() {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    SourceKind k = g_s.source;
    xSemaphoreGive(g_lock);
    return k;
}

void setServerHost(const String &hostPort) {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    g_s.serverHost = hostPort;
    xSemaphoreGive(g_lock);
    putString("srv_host", hostPort);
}

void setLyricsUrl(const String &url) {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    g_s.lyricsUrl = url;
    xSemaphoreGive(g_lock);
    putString("lyrics_url", url);
}

void setPairing(const String &serverId, const String &token) {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    g_s.serverId = serverId;
    g_s.serverToken = token;
    xSemaphoreGive(g_lock);
    putString("srv_id", serverId);
    putString("srv_token", token);
}

void forgetPairing() { setPairing("", ""); }

void setLead(int32_t ms) {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    g_s.leadMs = ms;
    g_leadMs = ms;
    xSemaphoreGive(g_lock);
    g_prefs.begin(NS, false);
    g_prefs.putInt("lead", ms);
    g_prefs.end();
}

int32_t leadMs() { return g_leadMs; }

void setBackdrop(bool on) {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    g_s.backdrop = on;
    g_backdrop = on;
    xSemaphoreGive(g_lock);
    g_prefs.begin(NS, false);
    g_prefs.putBool("backdrop", on);
    g_prefs.end();
}

bool backdrop() { return g_backdrop; }

void setScroll(bool on) {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    g_s.scroll = on;
    g_scroll = on;
    xSemaphoreGive(g_lock);
    g_prefs.begin(NS, false);
    g_prefs.putBool("scroll", on);
    g_prefs.end();
}

bool scroll() { return g_scroll; }

void setSleepMin(int32_t minutes) {
    if (minutes < 0) minutes = 0;
    if (minutes > 24 * 60) minutes = 24 * 60;
    xSemaphoreTake(g_lock, portMAX_DELAY);
    g_s.sleepMin = minutes;
    g_sleepMin = minutes;
    xSemaphoreGive(g_lock);
    g_prefs.begin(NS, false);
    g_prefs.putInt("sleep", minutes);
    g_prefs.end();
}

int32_t sleepMin() { return g_sleepMin; }

void setView(uint8_t view) {
    view = view ? 1 : 0;
    if (view == g_view) return;   // a swipe back and forth should not rewrite flash for nothing
    xSemaphoreTake(g_lock, portMAX_DELAY);
    g_s.view = view;
    g_view = view;
    xSemaphoreGive(g_lock);
    g_prefs.begin(NS, false);
    g_prefs.putUChar("view", view);
    g_prefs.end();
}

uint8_t view() { return g_view; }

void setBrightness(uint8_t percent) {
    percent = percent < 10 ? 10 : percent > 100 ? 100 : percent;
    xSemaphoreTake(g_lock, portMAX_DELAY);
    g_s.brightness = percent;
    g_brightness = percent;
    xSemaphoreGive(g_lock);
    g_prefs.begin(NS, false);
    g_prefs.putUChar("bright", percent);
    g_prefs.end();
}

uint8_t brightness() { return g_brightness; }

void setClock(bool on) {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    g_s.clock = on;
    g_clock = on;
    xSemaphoreGive(g_lock);
    g_prefs.begin(NS, false);
    g_prefs.putBool("clock", on);
    g_prefs.end();
}

bool clock() { return g_clock; }

void setWarmth(uint8_t percent) {
    if (percent > 100) percent = 100;
    xSemaphoreTake(g_lock, portMAX_DELAY);
    g_s.warmth = percent;
    g_warmth = percent;
    xSemaphoreGive(g_lock);
    g_prefs.begin(NS, false);
    g_prefs.putUChar("warmth", percent);
    g_prefs.end();
}

uint8_t warmth() { return g_warmth; }

void setTz(const String &tz) {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    g_s.tz = tz;
    xSemaphoreGive(g_lock);
    putString("tz", tz);
}

String tz() {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    String t = g_s.tz;
    xSemaphoreGive(g_lock);
    return t.length() ? t : String(DEFAULT_TZ);
}

void forgetWifi() { setWifi("", ""); }

void eraseAll() {
    String apPass = g_s.apPass;   // printed on the setup screen; keep it across a reset
    g_prefs.begin(NS, false);
    g_prefs.clear();
    g_prefs.putString("ap_pass", apPass);
    g_prefs.end();
    xSemaphoreTake(g_lock, portMAX_DELAY);
    g_s = Settings();
    g_s.apPass = apPass;
    g_leadMs = g_s.leadMs;
    g_backdrop = g_s.backdrop;
    g_scroll = g_s.scroll;
    g_sleepMin = g_s.sleepMin;
    g_view = g_s.view;
    g_brightness = g_s.brightness;
    g_clock = g_s.clock;
    g_warmth = g_s.warmth;
    xSemaphoreGive(g_lock);
}

}  // namespace settings
