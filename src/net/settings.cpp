#include "settings.h"

#include <Preferences.h>
#include <bootloader_random.h>

namespace settings {
namespace {

constexpr const char *NS = "tftpanel";
Settings g_s;
Preferences g_prefs;
SemaphoreHandle_t g_lock = nullptr;

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
    g_s.apPass = read("ap_pass");
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
    xSemaphoreGive(g_lock);
}

}  // namespace settings
