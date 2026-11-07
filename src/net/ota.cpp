#include "ota.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <Update.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <esp_timer.h>
#include <mbedtls/sha256.h>

#include "../board/board.h"
#include "../version.h"

// Defer Arduino's automatic "image valid" at boot: we decide in markHealthy().
extern "C" bool verifyRollbackLater() { return true; }

namespace ota {
namespace {

constexpr const char *LATEST_URL = "https://api.github.com/repos/r45635/tft-panel/releases/latest";
constexpr uint32_t CHECK_PERIOD_MS = 24UL * 3600 * 1000;
constexpr uint32_t FIRST_CHECK_DELAY_MS = 30 * 1000;   // let lyrics and the cover load first
constexpr uint64_t VERIFY_DEADLINE_US = 180ULL * 1000 * 1000;

SemaphoreHandle_t g_lock = nullptr;
Status g_status;
volatile uint32_t g_version = 0;
volatile bool g_checkRequested = false, g_installRequested = false;
uint32_t g_lastCheckAt = 0;
bool g_checkedOnce = false;
String g_binUrl, g_shaUrl;
volatile bool g_pendingVerify = false;
esp_timer_handle_t g_deadline = nullptr;

template <typename F>
void update(F &&fn) {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    fn(g_status);
    g_version = g_version + 1;
    xSemaphoreGive(g_lock);
}

void fail(const String &why) {
    Serial.printf("[ota] ERROR: %s\n", why.c_str());
    update([&](Status &s) {
        s.state = State::Failed;
        s.error = why;
    });
}

struct PsramAllocator : ArduinoJson::Allocator {
    void *allocate(size_t n) override { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
    void deallocate(void *p) override { heap_caps_free(p); }
    void *reallocate(void *p, size_t n) override { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
};

// "v1.2.3[-anything]" → {1,2,3}, dev = has a suffix. false if not a version.
bool parseVersion(const String &v, int out[3], bool &dev) {
    int n = sscanf(v.c_str(), "v%d.%d.%d", &out[0], &out[1], &out[2]);
    int dash = v.indexOf('-');
    dev = dash >= 0;
    return n == 3;
}

// Newer release than what runs? A dev build (suffix) counts as newer than its base tag.
bool isNewer(const String &tag, const String &running) {
    int a[3], b[3];
    bool devA, devB;
    if (!parseVersion(tag, a, devA) || devA) return false;   // only plain release tags
    if (!parseVersion(running, b, devB)) return true;
    for (int i = 0; i < 3; i++)
        if (a[i] != b[i]) return a[i] > b[i];
    return false;   // same base: the release is not newer (even if running a dev build of it)
}

void beginHttps(HTTPClient &http, NetworkClientSecure &client, const String &url) {
    client.useBuiltinCACertBundle();
    http.setConnectTimeout(8000);
    http.setTimeout(15000);
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);   // assets redirect to a CDN
    http.setUserAgent(String("tft-panel/") + FW_VERSION);    // GitHub's API requires one
    http.begin(client, url);
}

void check() {
    update([](Status &s) {
        s.state = State::Checking;
        s.error = "";
    });
    NetworkClientSecure client;
    HTTPClient http;
    beginHttps(http, client, LATEST_URL);
    http.addHeader("Accept", "application/vnd.github+json");
    // HTTP/1.1 and the whole body first (~7 KB): with HTTP/1.0 GitHub closes the connection
    // right after the body and the TLS client dropped its tail (IncompleteInput on the stream).
    int code = http.GET();
    if (code == 404) {   // no release published yet
        http.end();
        update([](Status &s) { s.state = State::UpToDate; });
        Serial.println("[ota] no release published yet");
        return;
    }
    if (code != 200) {
        http.end();
        fail("update check: " + (code < 0 ? HTTPClient::errorToString(code) : "HTTP " + String(code)));
        return;
    }
    PsramAllocator alloc;
    JsonDocument filter, doc(&alloc);
    filter["tag_name"] = true;
    filter["assets"][0]["name"] = true;
    filter["assets"][0]["browser_download_url"] = true;
    String body = http.getString();
    http.end();
    DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
    if (err) {
        fail(String("update check: bad JSON (") + err.c_str() + ", " + body.length() + " bytes)");
        return;
    }
    String tag = doc["tag_name"] | "";
    String binName = "tft-panel-" + tag + ".bin", shaName = "tft-panel-" + tag + ".sha256";
    String bin, sha;
    for (JsonObject a : doc["assets"].as<JsonArray>()) {
        String name = a["name"] | "";
        if (name == binName) bin = a["browser_download_url"] | "";
        if (name == shaName) sha = a["browser_download_url"] | "";
    }
    bool newer = isNewer(tag, FW_VERSION) && bin.length() && sha.length();
    g_binUrl = newer ? bin : String();   // install() only ever takes a newer release
    g_shaUrl = newer ? sha : String();
    Serial.printf("[ota] latest release %s, running %s: %s\n", tag.c_str(), FW_VERSION,
                  newer ? "update available" : "up to date");
    update([&](Status &s) {
        s.latest = tag;
        s.state = newer ? State::Available : State::UpToDate;
    });
}

bool fetchSha(uint8_t out[32]) {
    NetworkClientSecure client;
    HTTPClient http;
    beginHttps(http, client, g_shaUrl);
    int code = http.GET();
    String body = code == 200 ? http.getString() : String();
    http.end();
    if (code != 200) {
        fail("checksum download: " + (code < 0 ? HTTPClient::errorToString(code) : "HTTP " + String(code)));
        return false;
    }
    body.trim();
    if (body.length() < 64) {
        fail("checksum file malformed");
        return false;
    }
    for (int i = 0; i < 32; i++) {
        char hex[3] = {body[2 * i], body[2 * i + 1], 0};
        char *end;
        out[i] = (uint8_t)strtoul(hex, &end, 16);
        if (*end) {
            fail("checksum file malformed");
            return false;
        }
    }
    return true;
}

void install() {
    if (!g_binUrl.length() || status().state != State::Available) {
        fail("no newer release to install");
        return;
    }
    uint8_t want[32];
    update([](Status &s) {
        s.state = State::Downloading;
        s.percent = 0;
        s.error = "";
    });
    if (!fetchSha(want)) return;

    NetworkClientSecure client;
    HTTPClient http;
    beginHttps(http, client, g_binUrl);
    int code = http.GET();
    if (code != 200) {
        http.end();
        fail("download: " + (code < 0 ? HTTPClient::errorToString(code) : "HTTP " + String(code)));
        return;
    }
    int size = http.getSize();
    if (size <= 0 || !Update.begin(size, U_FLASH)) {
        http.end();
        fail(size <= 0 ? "download: unknown size" : String("cannot start update: ") + Update.errorString());
        return;
    }
    Serial.printf("[ota] installing %s (%d bytes)\n", g_status.latest.c_str(), size);
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
    uint8_t *buf = (uint8_t *)heap_caps_malloc(4096, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    NetworkClient *stream = http.getStreamPtr();
    int done = 0, lastPercent = -1;
    uint32_t lastData = millis();
    bool ok = buf != nullptr;
    while (ok && done < size) {
        size_t avail = stream->available();
        if (!avail) {
            if (!http.connected() || millis() - lastData > 15000) {
                ok = false;
                break;
            }
            delay(2);
            continue;
        }
        int n = stream->readBytes(buf, std::min(avail, (size_t)4096));
        if (n <= 0) continue;
        lastData = millis();
        mbedtls_sha256_update(&sha, buf, n);
        if (Update.write(buf, n) != (size_t)n) {
            ok = false;
            break;
        }
        done += n;
        int percent = (int)((int64_t)done * 100 / size);
        if (percent != lastPercent) {
            lastPercent = percent;
            update([&](Status &s) { s.percent = percent; });
        }
    }
    if (buf) heap_caps_free(buf);
    http.end();
    uint8_t got[32];
    mbedtls_sha256_finish(&sha, got);
    mbedtls_sha256_free(&sha);
    if (!ok || done != size) {
        Update.abort();
        fail(String("download interrupted at ") + done + "/" + size + " bytes" +
             (Update.hasError() ? String(" (") + Update.errorString() + ")" : ""));
        return;
    }
    if (memcmp(got, want, 32) != 0) {
        Update.abort();
        fail("checksum mismatch: download corrupted, nothing installed");
        return;
    }
    if (!Update.end(true)) {
        fail(String("cannot finish update: ") + Update.errorString());
        return;
    }
    Serial.printf("[ota] %s installed and verified, restarting\n", g_status.latest.c_str());
    update([](Status &s) {
        s.state = State::Installed;
        s.percent = 100;
    });
    delay(1500);
    board::restart();
}

}  // namespace

void begin() {
    g_lock = xSemaphoreCreateMutex();
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    if (running && esp_ota_get_state_partition(running, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
        g_pendingVerify = true;
        Serial.printf("[ota] first boot of %s: rollback in 3 min unless Wi-Fi and the companion answer\n",
                      FW_VERSION);
        esp_timer_create_args_t args = {};
        args.callback = [](void *) {
            if (!g_pendingVerify) return;
            Serial.println("[ota] new firmware never got healthy: restarting into the previous one");
            esp_ota_mark_app_invalid_rollback_and_reboot();
        };
        args.name = "ota-verify";
        esp_timer_create(&args, &g_deadline);
        esp_timer_start_once(g_deadline, VERIFY_DEADLINE_US);
    }
}

uint32_t version() { return g_version; }

Status status() {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    Status s = g_status;
    xSemaphoreGive(g_lock);
    return s;
}

void tick(bool wifiUp) {
    if (!wifiUp) return;
    if (g_installRequested) {
        g_installRequested = false;
        install();
        return;
    }
    uint32_t now = millis();
    bool due = g_checkedOnce ? now - g_lastCheckAt > CHECK_PERIOD_MS : now > FIRST_CHECK_DELAY_MS;
    if (g_checkRequested || due) {
        g_checkRequested = false;
        g_checkedOnce = true;
        g_lastCheckAt = now;
        check();
    }
}

void markHealthy() {
#ifdef OTA_TEST_NEVER_HEALTHY   // test build for the rollback path (PLATFORMIO_SRC_BUILD_FLAGS)
    return;
#endif
    if (!g_pendingVerify) return;
    g_pendingVerify = false;
    if (g_deadline) esp_timer_stop(g_deadline);
    esp_ota_mark_app_valid_cancel_rollback();
    Serial.printf("[ota] %s confirmed working, rollback cancelled\n", FW_VERSION);
    if (board::cleanResetDeferred()) {   // started by an older firmware's CPU-only restart
        Serial.println("[ota] full reset now that the update is confirmed (clean display and touch)");
        board::restart();
    }
}

void requestCheck() { g_checkRequested = true; }
void requestInstall() { g_installRequested = true; }

const char *stateName(State s) {
    switch (s) {
        case State::Idle: return "idle";
        case State::Checking: return "checking";
        case State::UpToDate: return "up to date";
        case State::Available: return "available";
        case State::Downloading: return "downloading";
        case State::Installed: return "installed";
        case State::Failed: return "failed";
    }
    return "?";
}

}  // namespace ota
