#include "coprocessor.h"

#include <sdkconfig.h>

#if CONFIG_ESP_HOSTED_ENABLED && defined(BOARD_WAVESHARE_P4_7B)

#include <Preferences.h>
#include <WiFi.h>

#include "esp32-hal-hosted.h"

// Embedded by platformio.ini (fetched and verified by tools/hosted_fw.py).
extern const uint8_t c6_start[] asm("_binary_assets_hosted_esp32c6_bin_start");
extern const uint8_t c6_end[] asm("_binary_assets_hosted_esp32c6_bin_end");

namespace coprocessor {

bool updateIfNeeded(const std::function<void(const String &)> &status) {
    if (!hostedIsInitialized()) WiFi.mode(WIFI_STA);   // starts esp_hosted
    if (!hostedIsInitialized()) {
        Serial.println("[c6] ERROR: esp_hosted did not start: the Wi-Fi chip does not answer");
        return false;
    }
    // The image's app description holds its version string at offset 0x30 (esp_app_desc_t).
    const size_t size = c6_end - c6_start;
    char imageVersion[16] = {0};
    if (size > 0x40) memcpy(imageVersion, c6_start + 0x30, sizeof imageVersion - 1);
    uint32_t hM, hm, hp, sM, sm, sp;
    hostedGetHostVersion(&hM, &hm, &hp);
    char hostVersion[16];
    snprintf(hostVersion, sizeof hostVersion, "%u.%u.%u", (unsigned)hM, (unsigned)hm, (unsigned)hp);
    if (!hostedHasUpdate()) {   // also reads the C6's version (0.0.0 when it is too old to say)
        hostedGetSlaveVersion(&sM, &sm, &sp);
        Serial.printf("[c6] Wi-Fi chip firmware %u.%u.%u, up to date\n", (unsigned)sM, (unsigned)sm, (unsigned)sp);
        return false;
    }
    hostedGetSlaveVersion(&sM, &sm, &sp);
    if (strcmp(imageVersion, hostVersion) != 0) {
        Serial.printf("[c6] ERROR: embedded C6 image is %s, this firmware needs %s: not installed\n", imageVersion,
                      hostVersion);
        return false;
    }
    Preferences prefs;
    prefs.begin("c6", false);
    String key = String("try-") + imageVersion;
    uint8_t tries = prefs.getUChar(key.c_str(), 0);
    if (tries >= 2) {
        prefs.end();
        Serial.printf("[c6] Wi-Fi chip still on %u.%u.%u after two attempts at %s: not trying again\n", (unsigned)sM,
                      (unsigned)sm, (unsigned)sp, imageVersion);
        return false;
    }
    prefs.putUChar(key.c_str(), tries + 1);
    prefs.end();

    Serial.printf("[c6] updating the Wi-Fi chip from %u.%u.%u to %s (%u bytes)\n", (unsigned)sM, (unsigned)sm,
                  (unsigned)sp, imageVersion, (unsigned)size);
    status(String("Updating the Wi-Fi chip to ") + imageVersion + " - about a minute, do not unplug the panel");
    if (!hostedBeginUpdate()) {
        Serial.println("[c6] ERROR: the Wi-Fi chip refused the update");
        return false;
    }
    uint8_t buf[2048];   // esp_hosted takes the image in chunks; RAM copy (flash data may be cached)
    int lastPct = -1;
    for (size_t off = 0; off < size;) {
        size_t n = std::min(sizeof buf, size - off);
        memcpy(buf, c6_start + off, n);
        if (!hostedWriteUpdate(buf, n)) {
            Serial.printf("[c6] ERROR: write failed at %u of %u bytes\n", (unsigned)off, (unsigned)size);
            return false;
        }
        off += n;
        int pct = (int)(off * 100 / size);
        if (pct / 10 != lastPct / 10) {
            lastPct = pct;
            Serial.printf("[c6] %d %%\n", pct);
            status(String("Updating the Wi-Fi chip to ") + imageVersion + ": " + pct + " % - do not unplug the panel");
        }
    }
    if (!hostedEndUpdate() || !hostedActivateUpdate()) {
        Serial.println("[c6] ERROR: the Wi-Fi chip did not accept the new firmware");
        return false;
    }
    Serial.printf("[c6] Wi-Fi chip updated to %s, restarting\n", imageVersion);
    return true;
}

}  // namespace coprocessor

#else

namespace coprocessor {
bool updateIfNeeded(const std::function<void(const String &)> &) { return false; }
}  // namespace coprocessor

#endif
