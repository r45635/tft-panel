#include "net.h"

#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include <algorithm>
#include <esp_mac.h>
#include <esp_sntp.h>

#include "../board/board.h"
#include "../board/board_pins.h"
#include "coprocessor.h"
#include "library.h"
#include "net_internal.h"
#include "ota.h"
#include "portal.h"
#include "web_update.h"
#include "position_clock.h"
#include "settings.h"
#include "source.h"
#include "source_companion.h"

namespace net {
using namespace internal;

namespace {

// ---- Timings ----
constexpr uint32_t WIFI_TIMEOUT_MS = 20000;        // one connection attempt
constexpr uint32_t WIFI_PORTAL_AFTER_MS = 60000;   // stored Wi-Fi failing that long → portal too
// Asking again for lyrics is the net task's policy, whichever source resolves them.
constexpr uint32_t LYRICS_RETRY_MS = 60000;        // "not found" may resolve later (new source)
constexpr int LYRICS_MAX_RETRIES = 3;

SemaphoreHandle_t g_lock = nullptr;
Snapshot g_snap;
volatile uint32_t g_version = 0;
volatile bool g_portalRequested = false;
volatile bool g_reconnectRequested = false;
volatile bool g_paused = false;

// Fields below are touched only by the net task.
String g_apSsid, g_apPass;
bool g_mdnsStarted = false;
uint32_t g_wifiAttemptAt = 0, g_wifiDownSince = 0;
uint8_t g_lastDisconnectReason = 0;
String g_trackKey;
String g_imageKey;          // cover currently shown (or attempted)
bool g_backdropWanted = true;
QueueHandle_t g_commands = nullptr;
volatile bool g_scanRequested = false;
uint32_t g_lastScanAt = 0;
PositionClock g_clock;
uint32_t g_lyricsFetchedAt = 0;
int g_lyricsRetries = 0;

template <typename F>
void update(F &&fn) {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    fn(g_snap);
    g_version = g_version + 1;
    xSemaphoreGive(g_lock);
}

}  // namespace

namespace internal {

void setPhase(Phase p, const String &status) {
    bool changed = false;
    xSemaphoreTake(g_lock, portMAX_DELAY);
    if (g_snap.phase != p || g_snap.status != status) {
        g_snap.phase = p;
        g_snap.status = status;
        g_version = g_version + 1;
        changed = true;
    }
    xSemaphoreGive(g_lock);
    if (changed) Serial.printf("[net] %s\n", status.c_str());
}

void publishServer(const String &url) {
    update([&](Snapshot &s) { s.companion = url; });
}

void markHealthy() { ota::markHealthy(); }

}  // namespace internal

namespace {

void openPortalNow() {
    if (portal::active()) return;
    portal::start(g_apSsid, g_apPass);
    update([](Snapshot &s) {
        s.portalActive = portal::active();
        s.apSsid = g_apSsid;
        s.apPass = g_apPass;
        s.apIp = WiFi.softAPIP().toString();
    });
}

void closePortal() {
    if (!portal::active()) return;
    portal::stop();
    update([](Snapshot &s) { s.portalActive = false; });
}

const char *disconnectReasonText(uint8_t r) {
    switch (r) {
        case WIFI_REASON_NO_AP_FOUND: return "network not found";
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT: return "wrong password?";
        case WIFI_REASON_ASSOC_FAIL: return "association refused";
        case 0: return "timeout";
        default: return WiFi.disconnectReasonName((wifi_err_reason_t)r);
    }
}

// ---- HTTP ----

// JSON documents in PSRAM: internal RAM is reserved for the Wi-Fi/lwIP stack.
}  // namespace

namespace internal {

struct PsramAllocator : ArduinoJson::Allocator {
    void *allocate(size_t n) override { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
    void deallocate(void *p) override { heap_caps_free(p); }
    void *reallocate(void *p, size_t n) override {
        return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
};
ArduinoJson::Allocator *psram() {
    static PsramAllocator a;
    return &a;
}

HttpResult getBytes(const String &url, uint8_t *&buf, size_t &len, size_t maxLen, uint32_t timeoutMs) {
    HttpResult r;
    buf = nullptr;
    len = 0;
    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(3000);
    http.setTimeout(timeoutMs);
    http.useHTTP10(true);
    if (!http.begin(client, url)) {
        r.code = -100;
        r.message = "bad image URL";
        return r;
    }
    String token = settings::get().accessToken;
    if (token.length()) http.addHeader("Authorization", "Bearer " + token);
    r.code = http.GET();
    if (r.code != 200) {
        r.message = r.code < 0 ? HTTPClient::errorToString(r.code) : "HTTP " + String(r.code);
        http.end();
        return r;
    }
    int size = http.getSize();   // -1 when the server sends no Content-Length
    size_t cap = size > 0 ? (size_t)size : maxLen;
    if (cap > maxLen) {
        r.code = -102;
        r.message = "image too large (" + String(size) + " bytes)";
        http.end();
        return r;
    }
    buf = (uint8_t *)heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        r.code = -103;
        r.message = "out of memory";
        http.end();
        return r;
    }
    WiFiClient *stream = http.getStreamPtr();
    uint32_t deadline = millis() + timeoutMs;
    while (len < cap && millis() < deadline && (http.connected() || stream->available())) {
        int n = stream->read(buf + len, cap - len);
        if (n > 0) len += n;
        else delay(2);
    }
    http.end();
    if (size > 0 && len != (size_t)size) {
        r.code = -104;
        r.message = "image truncated (" + String((unsigned)len) + "/" + String(size) + " bytes)";
        heap_caps_free(buf);
        buf = nullptr;
    }
    return r;
}

String followedZoneId() {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    String z = g_snap.zoneId;
    xSemaphoreGive(g_lock);
    return z;
}

void publishLibrary(std::shared_ptr<const LibraryPage> page) {
    update([&](Snapshot &s) {
        s.library = std::move(page);
        s.librarySerial++;
    });
}

}  // namespace internal

namespace {

void setCover(cover::Cover c) {
    update([&](Snapshot &s) {
        s.bigCover = std::move(c.big);
        s.thumb = std::move(c.thumb);
        s.backdrop = std::move(c.backdrop);
        s.coverSerial++;
    });
}

// Fetches one cover URL and builds the pictures. false with `why` on failure.
bool loadCover(const String &url, bool withBig, cover::Cover &c, size_t &len, String &why) {
    uint8_t *jpeg = nullptr;
    len = 0;
    HttpResult r = getBytes(url, jpeg, len, 256 * 1024, 5000);
    if (r.code != 200 || !jpeg) {
        why = "not loaded: " + r.message;
        return false;
    }
    bool ok = cover::build(jpeg, len, withBig, COVER_THUMB_PX, settings::backdrop(), layout::SCREEN_W, layout::SCREEN_H, c, why);
    heap_caps_free(jpeg);
    if (!ok) why = "not usable: " + why;
    return ok;
}

// The cover is fetched and built (~1-1.5 s) by its own task, as soon as the track changes,
// while the net task goes on with the lyrics (which can take seconds). A request carries a
// generation: a cover finished after the track changed again is dropped.
struct CoverRequest {
    uint32_t gen;
    char bigUrl[224];     // both sizes are resolved before the request is queued: the cover task
    char thumbUrl[224];   // runs on another core and must not read the source's address
};
QueueHandle_t g_coverQueue = nullptr;
volatile uint32_t g_coverGen = 0;

// One fetch at the big size feeds the cover view, the thumbnail and the backdrop. If the big
// one fails (e.g. a progressive JPEG), the small one still gives thumbnail and backdrop.
void buildCover(const CoverRequest &rq) {
    uint32_t t0 = millis();
    cover::Cover c;
    size_t len = 0;
    String why;
    bool ok = loadCover(rq.bigUrl, true, c, len, why);
    if (!ok) {
        Serial.printf("[net] WARNING: big cover %s, trying the small one\n", why.c_str());
        ok = loadCover(rq.thumbUrl, false, c, len, why);
        if (!ok) Serial.printf("[net] WARNING: cover %s\n", why.c_str());
    }
    if (rq.gen != g_coverGen) return;   // the track changed meanwhile
    if (!ok) {
        setCover({});
        return;
    }
    Serial.printf("[net] cover ready (%u bytes JPEG, big %dx%d, thumb %dx%d, backdrop %s, %u ms)\n", (unsigned)len,
                  c.big ? c.big->w : 0, c.big ? c.big->h : 0, c.thumb->w, c.thumb->h, c.backdrop ? "yes" : "off",
                  (unsigned)(millis() - t0));
    setCover(std::move(c));
}

void coverTask(void *) {
    CoverRequest rq;
    for (;;)
        if (xQueueReceive(g_coverQueue, &rq, portMAX_DELAY) == pdTRUE) buildCover(rq);
}

// Net task: asks for the cover of `imageKey` (empty = no cover). The latest request wins.
void fetchCover(const String &imageKey) {
    g_imageKey = imageKey;
    uint32_t gen = ++g_coverGen;
    if (!imageKey.length()) {
        setCover({});
        return;
    }
    String big = source().imageUrl(imageKey, COVER_BIG_PX);
    String thumb = source().imageUrl(imageKey, COVER_THUMB_PX);
    if (!big.length() || !thumb.length()) {
        setCover({});
        return;
    }
    CoverRequest rq{};
    rq.gen = gen;
    strlcpy(rq.bigUrl, big.c_str(), sizeof rq.bigUrl);
    strlcpy(rq.thumbUrl, thumb.c_str(), sizeof rq.thumbUrl);
    xQueueOverwrite(g_coverQueue, &rq);
}

}  // namespace

// The chip's base MAC from eFuse: valid before Wi-Fi starts, and on the P4 (whose Wi-Fi is
// a separate ESP32-C6) the same identity as on the S3, where it is also the station MAC.
void baseMac(uint8_t mac[6]) { esp_efuse_mac_get_default(mac); }

String hostName() {
    uint8_t mac[6];
    baseMac(mac);
    char host[24];
    snprintf(host, sizeof host, "tft-panel-%02x%02x", mac[4], mac[5]);
    return host;
}

namespace {

// Our own name on the LAN (tft-panel-xxxx.local, also the update page) + companion lookup.
void startMdns() {
    if (g_mdnsStarted) return;
    String host = hostName();
    g_mdnsStarted = MDNS.begin(host.c_str());
    if (g_mdnsStarted) Serial.printf("[net] this panel is %s.local\n", host.c_str());
    else Serial.println("[net] WARNING: mDNS responder failed to start");
}

}  // namespace

namespace internal {

void ensureMdns() { startMdns(); }

}  // namespace internal

namespace {

// ---- Now playing + lyrics ----

// Asks the source for the lyrics of what is playing. A source that has none says so and is
// never asked again for that track.
void askLyrics(const String &zoneId) {
    if (!source().fetchLyrics(zoneId)) {
        update([](Snapshot &s) {
            s.lyricsStatus = LyricsStatus::None;
            s.lyrics.reset();
            s.lyricsSerial++;
            s.lyricsSource = s.lyricsError = "";
        });
        g_lyricsRetries = LYRICS_MAX_RETRIES;
    }
    g_lyricsFetchedAt = millis();
}

void clearTrack() {
    g_trackKey = "";
    if (g_imageKey.length()) fetchCover("");   // also drops a cover still being built
    update([](Snapshot &s) {
        s.state = "stopped";
        s.title = s.artist = s.album = "";
        s.durationMs = 0;
        s.positionMs = 0;
        s.lyricsStatus = LyricsStatus::None;
        s.lyrics.reset();
        s.lyricsSerial++;
        s.lyricsSource = s.lyricsError = "";
    });
}

}  // namespace

namespace internal {

// ---- What a source publishes ----

std::vector<OutputInfo> followedOutputs() {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    std::vector<OutputInfo> o = g_snap.outputs;
    xSemaphoreGive(g_lock);
    return o;
}

void currentTrack(String &title, String &artist, String &album) {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    title = g_snap.title;
    artist = g_snap.artist;
    album = g_snap.album;
    xSemaphoreGive(g_lock);
}

// Picks the zone to follow: the pinned one if it exists, else keep the current zone
// while it plays, else the first playing zone, else the first with a track, else the first.
bool publishZones(std::shared_ptr<std::vector<ZoneInfo>> zones) {
    String pinned = settings::get().zoneId;
    String current = followedZoneId();

    const ZoneInfo *chosen = nullptr, *cur = nullptr, *firstPlaying = nullptr, *firstLoaded = nullptr,
                   *first = nullptr;
    for (const ZoneInfo &z : *zones) {
        if (!first) first = &z;
        if (pinned.length() && z.id == pinned) chosen = &z;
        if (z.id == current) cur = &z;
        if (!firstPlaying && z.state == "playing") firstPlaying = &z;
        if (!firstLoaded && z.title.length()) firstLoaded = &z;
    }
    bool isPinned = chosen != nullptr;
    if (!chosen && cur && cur->state == "playing") chosen = cur;
    if (!chosen) chosen = firstPlaying;
    if (!chosen) chosen = cur;
    if (!chosen) chosen = firstLoaded;
    if (!chosen) chosen = first;
    if (!chosen) return false;
    if (pinned.length() && !isPinned)
        Serial.printf("[net] WARNING: pinned zone %s not found, following another zone\n", pinned.c_str());

    String id = chosen->id, name = chosen->name;
    if (id != current) Serial.printf("[net] following zone \"%s\"%s\n", name.c_str(), isPinned ? " (pinned)" : "");
    update([&](Snapshot &s) {
        s.zoneId = id;
        s.zoneName = name;
        s.zonePinned = isPinned;
        s.zones = zones;
        s.zonesSerial++;
    });
    return true;
}

void publishIdle() {
    if (g_trackKey.length()) clearTrack();
    String zoneName;
    xSemaphoreTake(g_lock, portMAX_DELAY);
    zoneName = g_snap.zoneName;
    xSemaphoreGive(g_lock);
    setPhase(Phase::Idle, "Nothing playing in " + zoneName);
}

// The position clock, the track change, the cover and the lyrics: the same for every source.
void publishNowPlaying(NowPlaying np) {
    String key = np.zoneId + "\x1f" + np.title + "\x1f" + np.artist + "\x1f" + np.album + "\x1f" +
                 String(np.durationMs);
    bool newTrack = key != g_trackKey;
    if (newTrack) g_clock.reset();
    g_clock.observe(np.seekMs, np.sampleAtMs, np.state == "playing", np.slackMs);
    update([&](Snapshot &s) {
        s.positionMs = g_clock.positionAt(np.sampleAtMs);
        s.positionAtMs = np.sampleAtMs;
        s.positionUncertaintyMs = g_clock.uncertaintyMs();
        s.state = np.state;
        s.title = np.title;
        s.artist = np.artist;
        s.album = np.album;
        s.durationMs = np.durationMs;
        s.outputs = std::move(np.outputs);
        s.canPlay = np.canPlay;
        s.canPause = np.canPause;
        s.canNext = np.canNext;
        s.canPrevious = np.canPrevious;
        if (newTrack) {
            s.lyricsStatus = LyricsStatus::Loading;
            s.lyrics.reset();
            s.lyricsSerial++;
        }
    });
    setPhase(Phase::Playing, np.state == "playing" ? "Playing" : np.state == "paused" ? "Paused" : np.state);

    if (newTrack) {
        g_trackKey = key;
        g_lyricsRetries = 0;
        Serial.printf("[net] track: %s - %s (%u s, %s)\n", np.artist.c_str(), np.title.c_str(),
                      (unsigned)(np.durationMs / 1000), np.state.c_str());
        // Cover first, in its own task: it shows in ~1 s while the lyrics are being resolved.
        g_backdropWanted = settings::backdrop();
        fetchCover(np.imageKey);
        askLyrics(np.zoneId);
    } else {
        if (np.imageKey != g_imageKey || settings::backdrop() != g_backdropWanted) {
            g_backdropWanted = settings::backdrop();
            fetchCover(np.imageKey);
        }
        LyricsStatus ls;
        xSemaphoreTake(g_lock, portMAX_DELAY);
        ls = g_snap.lyricsStatus;
        xSemaphoreGive(g_lock);
        if ((ls == LyricsStatus::NotFound || ls == LyricsStatus::Error) && g_lyricsRetries < LYRICS_MAX_RETRIES &&
            millis() - g_lyricsFetchedAt > LYRICS_RETRY_MS) {
            g_lyricsRetries++;
            askLyrics(np.zoneId);
        }
    }
}

void publishLyricsLoading() {
    update([](Snapshot &s) { s.lyricsStatus = LyricsStatus::Loading; });
}

void publishLyrics(LyricsStatus st, std::shared_ptr<const lyrics::Lyrics> parsed, const String &source,
                   int32_t offsetMs) {
    update([&](Snapshot &s) {
        s.lyricsStatus = st;
        s.lyrics = parsed;
        s.lyricsSerial++;
        s.offsetMs = offsetMs;
        s.lyricsSource = source;
        s.lyricsError = "";
    });
}

void publishLyricsFailed(LyricsStatus st, const String &message) {
    update([&](Snapshot &s) {
        s.lyricsStatus = st;
        s.lyricsError = message;
    });
}

void publishOffset(int32_t offsetMs) {
    update([&](Snapshot &s) { s.offsetMs = offsetMs; });
}

void commandFailed(const String &what, const HttpResult &r) {
    String msg = what + " failed: " + (r.message.length() ? r.message : "HTTP " + String(r.code));
    Serial.printf("[net] %s\n", msg.c_str());
    update([&](Snapshot &s) {
        s.commandError = msg;
        s.commandErrorAt = millis();
    });
}

}  // namespace internal

namespace {

// ---- User commands ----

// A queued user action: the library and the zone choice are the panel's own business,
// everything else is the source's.
void handleCommand(const Command &c) {
    if (c.type == CmdType::Library) {   // its own module: browse session, levels, thumbnails
        if (board::HAS_LIBRARY) lib::run((LibOp)c.direction, (int)c.value, String(c.arg));
        return;
    }
    if (c.type == CmdType::Zone) {
        settings::setZone(String(c.arg));
        Serial.printf("[net] zone %s\n", c.arg[0] ? c.arg : "auto (follow the playing zone)");
        source().zoneSelectionChanged();   // re-pick at once
        return;
    }
    source().command(c);
}

// ---- Task ----

void resetSession() {
    source().reset();
    g_trackKey = "";
    update([](Snapshot &s) {
        s.companion = "";
        s.zoneId = s.zoneName = "";
    });
    clearTrack();
}

// Wi-Fi scan, asynchronous: on request (on-screen setup), when the portal opens, and every 30 s
// while the portal runs with no phone on it (a scan moves the radio off the AP's channel).
void tickScan() {
    int n = WiFi.scanComplete();
    if (n >= 0) {
        auto nets = std::make_shared<std::vector<WifiNet>>();
        for (int i = 0; i < n; i++) {
            String ssid = WiFi.SSID(i);
            if (!ssid.length()) continue;   // hidden networks
            int8_t rssi = (int8_t)WiFi.RSSI(i);
            bool open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
            auto same = std::find_if(nets->begin(), nets->end(), [&](const WifiNet &w) { return w.ssid == ssid; });
            if (same == nets->end()) nets->push_back({ssid, rssi, open});
            else if (rssi > same->rssi) same->rssi = rssi;
        }
        WiFi.scanDelete();
        std::sort(nets->begin(), nets->end(), [](const WifiNet &a, const WifiNet &b) { return a.rssi > b.rssi; });
        Serial.printf("[net] Wi-Fi scan: %u networks\n", (unsigned)nets->size());
        update([&](Snapshot &s) {
            s.networks = nets;
            s.networksSerial++;
            s.scanning = false;
        });
        return;
    }
    if (n == WIFI_SCAN_RUNNING) return;
    uint32_t now = millis();
    bool periodic = portal::active() && now - g_lastScanAt > 30000 && WiFi.softAPgetStationNum() == 0;
    if (!g_scanRequested && !periodic) return;
    g_scanRequested = false;
    g_lastScanAt = now;
    if (WiFi.scanNetworks(true) == WIFI_SCAN_FAILED) {
        Serial.println("[net] Wi-Fi scan could not start");
        update([](Snapshot &s) { s.scanning = false; });
        return;
    }
    update([](Snapshot &s) { s.scanning = true; });
}

void task(void *) {
    bool wifiStarted = false;

    WiFi.persistent(false);   // credentials live in our NVS namespace, not the driver's
    if (coprocessor::updateIfNeeded([](const String &msg) { setPhase(Phase::WifiConnecting, msg); })) {
        delay(1500);
        board::restart();
    }
    WiFi.setAutoReconnect(true);
    WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t info) {
        g_lastDisconnectReason = info.wifi_sta_disconnected.reason;
    }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

    for (;;) {
        if (g_paused) {
            delay(50);
            continue;
        }
        portal::loop();
        tickScan();
        if (portal::savedAndRestartPending()) {
            delay(1500);   // let the confirmation page reach the phone
            board::restart();
        }
        if (g_portalRequested) {
            g_portalRequested = false;
            openPortalNow();
        }
        if (g_reconnectRequested) {
            g_reconnectRequested = false;
            WiFi.disconnect();
            wifiStarted = false;
            resetSession();
        }

        settings::Settings st = settings::get();
        if (!st.wifiSsid.length()) {
            openPortalNow();
            setPhase(Phase::Setup, "Wi-Fi not set up: join the panel's network to configure it");
            delay(10);
            continue;
        }

        if (WiFi.status() != WL_CONNECTED) {
            uint32_t now = millis();
            if (!g_wifiDownSince) g_wifiDownSince = now;
            if (!wifiStarted || now - g_wifiAttemptAt > WIFI_TIMEOUT_MS) {
                if (wifiStarted) {
                    setPhase(Phase::WifiFailed, "Cannot join \"" + st.wifiSsid + "\": " +
                                                    disconnectReasonText(g_lastDisconnectReason) + " - retrying");
                } else {
                    setPhase(Phase::WifiConnecting, "Joining Wi-Fi \"" + st.wifiSsid + "\"...");
                }
                if (!portal::active()) WiFi.mode(WIFI_STA);
                WiFi.begin(st.wifiSsid.c_str(), st.wifiPass.c_str());
                wifiStarted = true;
                g_wifiAttemptAt = now;
            }
            if (now - g_wifiDownSince > WIFI_PORTAL_AFTER_MS) openPortalNow();
            delay(20);
            continue;
        }

        if (g_wifiDownSince) {
            g_wifiDownSince = 0;
            String ip = WiFi.localIP().toString();
            Serial.printf("[net] Wi-Fi up, ip %s, rssi %d dBm\n", ip.c_str(), WiFi.RSSI());
            update([&](Snapshot &s) { s.staIp = ip; });
            closePortal();
            startMdns();
            if (!esp_sntp_enabled()) {   // wall clock for the idle screen; TZ is set by the UI
                esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
                esp_sntp_setservername(0, "pool.ntp.org");
                esp_sntp_init();
            }
        }

        uint32_t now = millis();
        static uint32_t lastHeapWarn = 0;
        size_t freeInternal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        if (freeInternal < 20 * 1024 && now - lastHeapWarn > 30000) {
            lastHeapWarn = now;
            Serial.printf("[net] WARNING: internal RAM low: %u KB free (min %u KB)\n", (unsigned)(freeInternal / 1024),
                          (unsigned)(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024));
        }
        ota::tick(true);   // update check (boot + daily) and a confirmed install; no server needed
        if (!source().ready() && !source().connect(now)) {
            delay(20);
            continue;
        }

        Command cmd;
        while (xQueueReceive(g_commands, &cmd, 0) == pdTRUE) handleCommand(cmd);
        source().pump();   // a source that is pushed to reads its socket; a polled one does nothing

        now = millis();
        if (source().zonesDue(now)) {
            if (!source().refreshZones()) {
                delay(200);
                continue;
            }
            internal::markHealthy();   // Wi-Fi + server answered: a freshly installed image is good
        }
        if (source().nowPlayingDue(now)) source().pollNowPlaying();
        if (board::HAS_LIBRARY) lib::tick();   // next thumbnail while the library is open
        delay(10);
    }
}

}  // namespace

int32_t Snapshot::positionAt(uint32_t now) const {
    int32_t p = positionMs;
    if (playing()) p += (int32_t)(now - positionAtMs);
    if (durationMs && p > (int32_t)durationMs) p = durationMs;
    return p < 0 ? 0 : p;
}

// The one source in use. A second one (talking to the music server directly) plugs in here,
// chosen from the settings at startup; everything above is written against the interface only.
namespace {
CompanionSource g_companion;
}  // namespace

Source &source() { return g_companion; }

void begin() {
    g_coverQueue = xQueueCreate(1, sizeof(CoverRequest));
    xTaskCreatePinnedToCore(coverTask, "cover", 8192, nullptr, 1, nullptr, 0);
    ota::begin();
    webupdate::begin();
    g_lock = xSemaphoreCreateMutex();
    g_commands = xQueueCreate(8, sizeof(Command));
    uint8_t mac[6] = {0};
    baseMac(mac);
    char suffix[5];
    snprintf(suffix, sizeof suffix, "%02X%02X", mac[4], mac[5]);
    g_apSsid = String("TFT-Panel-") + suffix;
    g_apPass = settings::get().apPass;   // settings::begin() must run first
    // Core 0 next to the Wi-Fi stack; LVGL runs in loop() on core 1.
    xTaskCreatePinnedToCore(task, "net", 12288, nullptr, 3, nullptr, 0);
}

uint32_t version() { return g_version; }

Snapshot snapshot() {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    Snapshot copy = g_snap;
    xSemaphoreGive(g_lock);
    return copy;
}

void openPortal() { g_portalRequested = true; }
void requestScan() { g_scanRequested = true; }
void reconnect() { g_reconnectRequested = true; }
void pause(bool on) { g_paused = on; }

namespace {
void enqueue(CmdType type, const char *arg, int8_t direction, float value = 0) {
    Command c{};
    c.type = type;
    c.direction = direction;
    c.value = value;
    if (strlcpy(c.arg, arg ? arg : "", sizeof c.arg) >= sizeof c.arg)
        Serial.printf("[net] WARNING: command text cut to %u characters\n", (unsigned)(sizeof c.arg - 1));
    if (xQueueSend(g_commands, &c, 0) != pdTRUE) Serial.println("[net] WARNING: command queue full, command dropped");
}
}  // namespace

void control(const char *action) { enqueue(CmdType::Control, action, 0); }
void volumeStep(int direction) { enqueue(CmdType::Volume, nullptr, direction > 0 ? 1 : -1); }
void selectZone(const String &zoneId) { enqueue(CmdType::Zone, zoneId.c_str(), 0); }
void volumeSet(float value) { enqueue(CmdType::VolumeSet, nullptr, 0, value); }
void mute(bool on) { enqueue(CmdType::Mute, nullptr, on ? 1 : 0); }
void setOffset(int32_t ms) { enqueue(CmdType::Offset, nullptr, 0, (float)ms); }
void library(LibOp op, int index, const String &text) {
    if (board::HAS_LIBRARY) enqueue(CmdType::Library, text.c_str(), (int8_t)op, (float)index);
}

}  // namespace net
