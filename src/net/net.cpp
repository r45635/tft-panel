#include "net.h"

#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_sntp.h>

#include "../board/board.h"
#include "ota.h"
#include "portal.h"
#include "web_update.h"
#include "position_clock.h"
#include "settings.h"

namespace net {
namespace {

// ---- Timings ----
constexpr uint32_t WIFI_TIMEOUT_MS = 20000;        // one connection attempt
constexpr uint32_t WIFI_PORTAL_AFTER_MS = 60000;   // stored Wi-Fi failing that long → portal too
constexpr uint32_t NOW_PLAYING_PERIOD_MS = 300;   // narrower windows for PositionClock
constexpr uint32_t ZONES_PERIOD_MS = 5000;
constexpr uint32_t DISCOVERY_PERIOD_MS = 5000;
constexpr uint32_t LYRICS_RETRY_MS = 60000;        // "not found" may resolve later (new source)
constexpr int LYRICS_MAX_RETRIES = 3;
constexpr int FAILS_BEFORE_REDISCOVERY = 6;        // ~3 s of failed polls

SemaphoreHandle_t g_lock = nullptr;
Snapshot g_snap;
volatile uint32_t g_version = 0;
volatile bool g_portalRequested = false;
volatile bool g_reconnectRequested = false;
volatile bool g_paused = false;

// Fields below are touched only by the net task.
String g_apSsid, g_apPass;
String g_baseUrl;
bool g_baseFromMdns = false;
bool g_mdnsStarted = false;
int g_consecutiveFails = 0;
uint32_t g_wifiAttemptAt = 0, g_wifiDownSince = 0;
uint8_t g_lastDisconnectReason = 0;
String g_trackKey;
String g_imageKey;          // cover currently shown (or attempted)
bool g_backdropWanted = true;
uint32_t g_lastZonesAt = 0, g_lastNowPlayingAt = 0;

enum class CmdType : uint8_t { Control, Volume, Zone, VolumeSet, Mute, Offset };
struct Command {
    CmdType type;
    int8_t direction;
    char arg[48];
    float value;
};
QueueHandle_t g_commands = nullptr;
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

struct HttpResult {
    int code = 0;          // HTTP status, or < 0 for a transport error
    String errorCode;      // companion error.code when present
    String message;        // short human-readable reason
};

// GET when `body` is null, else POST it as JSON.
HttpResult request(const String &path, const JsonDocument *body, JsonDocument &doc, uint32_t timeoutMs) {
    HttpResult r;
    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(3000);
    http.setTimeout(timeoutMs);
    http.setReuse(false);
    if (!http.begin(client, g_baseUrl + path)) {
        r.code = -100;
        r.message = "bad companion URL";
        return r;
    }
    String token = settings::get().accessToken;
    if (token.length()) http.addHeader("Authorization", "Bearer " + token);
    http.useHTTP10(true);   // no chunked encoding: parse straight from the socket, no body String
    if (body) {
        String payload;
        serializeJson(*body, payload);
        http.addHeader("Content-Type", "application/json");
        r.code = http.POST(payload);
    } else {
        r.code = http.GET();
    }
    if (r.code < 0) {
        r.message = HTTPClient::errorToString(r.code);
        http.end();
        return r;
    }
    DeserializationError err = deserializeJson(doc, http.getStream());
    http.end();
    if (err) {
        r.message = String("bad JSON (") + err.c_str() + ")";
        if (r.code == 200) r.code = -101;
        return r;
    }
    if (r.code != 200) {
        r.errorCode = doc["error"]["code"] | "";
        r.message = doc["error"]["message"] | ("HTTP " + String(r.code)).c_str();
    }
    return r;
}

HttpResult getJson(const String &path, JsonDocument &doc, uint32_t timeoutMs) {
    return request(path, nullptr, doc, timeoutMs);
}

// GETs a binary body into a PSRAM buffer the caller frees (heap_caps_free).
HttpResult getBytes(const String &path, uint8_t *&buf, size_t &len, size_t maxLen, uint32_t timeoutMs) {
    HttpResult r;
    buf = nullptr;
    len = 0;
    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(3000);
    http.setTimeout(timeoutMs);
    http.useHTTP10(true);
    if (!http.begin(client, g_baseUrl + path)) {
        r.code = -100;
        r.message = "bad companion URL";
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

void setCover(cover::Cover c) {
    update([&](Snapshot &s) {
        s.bigCover = std::move(c.big);
        s.thumb = std::move(c.thumb);
        s.backdrop = std::move(c.backdrop);
        s.coverSerial++;
    });
}

// Fetches the cover at `px` and builds the pictures. false with `why` on failure.
bool loadCover(const String &imageKey, int px, bool withBig, cover::Cover &c, size_t &len, String &why) {
    uint8_t *jpeg = nullptr;
    len = 0;
    String q = "?w=" + String(px) + "&h=" + String(px);
    HttpResult r = getBytes("/api/roon/image/" + imageKey + q, jpeg, len, 256 * 1024, 5000);
    if (r.code != 200 || !jpeg) {
        why = "not loaded: " + r.message;
        return false;
    }
    bool ok = cover::build(jpeg, len, withBig, COVER_THUMB_PX, settings::backdrop(), 800, 480, c, why);
    heap_caps_free(jpeg);
    if (!ok) why = "not usable: " + why;
    return ok;
}

// One fetch at the big size feeds the cover view, the thumbnail and the backdrop. If the big
// one fails (e.g. a progressive JPEG), the small one still gives thumbnail and backdrop.
void fetchCover(const String &imageKey) {
    g_imageKey = imageKey;
    if (!imageKey.length()) {
        setCover({});
        return;
    }
    uint32_t t0 = millis();
    cover::Cover c;
    size_t len = 0;
    String why;
    if (!loadCover(imageKey, COVER_BIG_PX, true, c, len, why)) {
        Serial.printf("[net] WARNING: big cover %s, trying the small one\n", why.c_str());
        if (!loadCover(imageKey, COVER_THUMB_PX, false, c, len, why)) {
            Serial.printf("[net] WARNING: cover %s\n", why.c_str());
            setCover({});
            return;
        }
    }
    Serial.printf("[net] cover ready (%u bytes JPEG, big %dx%d, thumb %dx%d, backdrop %s, %u ms)\n", (unsigned)len,
                  c.big ? c.big->w : 0, c.big ? c.big->h : 0, c.thumb->w, c.thumb->h, c.backdrop ? "yes" : "off",
                  (unsigned)(millis() - t0));
    setCover(std::move(c));
}

// Turns a failed call into the status line, and drops a discovered companion after
// several failures in a row so a restarted one (new IP/port) is found again.
void reportCompanionFailure(const char *what, const HttpResult &r) {
    g_consecutiveFails++;
    String msg;
    if (r.code == 401) msg = "Companion needs its access token (set it in the setup portal)";
    else if (r.code == 503) msg = "Companion: Roon Core not connected";
    else msg = String("Companion ") + what + " failed: " + r.message;
    setPhase(Phase::CompanionError, msg);
    if (g_baseFromMdns && g_consecutiveFails >= FAILS_BEFORE_REDISCOVERY) {
        Serial.printf("[net] dropping %s after %d failures, rediscovering\n", g_baseUrl.c_str(), g_consecutiveFails);
        g_baseUrl = "";
        update([](Snapshot &s) { s.companion = ""; });
    }
}

// ---- Companion discovery ----

// Our own name on the LAN (tft-panel-xxxx.local, also the update page) + companion lookup.
void startMdns() {
    if (g_mdnsStarted) return;
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    String host = "tft-panel-" + mac.substring(8);
    host.toLowerCase();
    g_mdnsStarted = MDNS.begin(host.c_str());
    if (g_mdnsStarted) Serial.printf("[net] this panel is %s.local\n", host.c_str());
    else Serial.println("[net] WARNING: mDNS responder failed to start");
}

bool discover() {
    settings::Settings st = settings::get();
    if (st.companionUrl.length()) {
        g_baseUrl = st.companionUrl;
        g_baseFromMdns = false;
    } else {
        startMdns();
        setPhase(Phase::Discovering, "Looking for the TextFromTrack companion on the network...");
        int n = MDNS.queryService("textfromtrack", "tcp");
        for (int i = 0; i < n; i++) {
            IPAddress ip = MDNS.address(i);
            if (ip == IPAddress((uint32_t)0)) continue;
            g_baseUrl = "http://" + ip.toString() + ":" + String(MDNS.port(i));
            g_baseFromMdns = true;
            Serial.printf("[net] companion \"%s\" at %s\n", MDNS.hostname(i).c_str(), g_baseUrl.c_str());
            break;
        }
        if (!g_baseUrl.length()) {
            setPhase(Phase::Discovering,
                     n < 0 ? "mDNS query failed - set the companion address in the setup portal"
                           : "No companion found on the network yet (is it running?)");
            return false;
        }
    }
    g_consecutiveFails = 0;
    update([](Snapshot &s) { s.companion = g_baseUrl; });
    return true;
}

// ---- Zones ----

// Picks the zone to follow: the pinned one if it exists, else keep the current zone
// while it plays, else the first playing zone, else the first with a track, else the first.
bool refreshZones() {
    JsonDocument doc(psram());
    HttpResult r = getJson("/api/roon/zones", doc, 4000);
    if (r.code != 200) {
        reportCompanionFailure("zones", r);
        return false;
    }
    JsonArrayConst zones = doc["zones"].as<JsonArrayConst>();
    String pinned = settings::get().zoneId;
    String current;
    xSemaphoreTake(g_lock, portMAX_DELAY);
    current = g_snap.zoneId;
    xSemaphoreGive(g_lock);

    auto list = std::make_shared<std::vector<ZoneInfo>>();
    JsonObjectConst chosen, firstPlaying, firstLoaded, first, cur;
    for (JsonObjectConst z : zones) {
        String id = z["zone_id"] | "";
        String state = z["state"] | "";
        list->push_back({id, z["display_name"] | "", state, z["now_playing_title"] | "", z["now_playing_artist"] | ""});
        if (first.isNull()) first = z;
        if (pinned.length() && id == pinned) chosen = z;
        if (id == current) cur = z;
        if (firstPlaying.isNull() && state == "playing") firstPlaying = z;
        if (firstLoaded.isNull() && !z["now_playing_title"].isNull()) firstLoaded = z;
    }
    bool isPinned = !chosen.isNull();
    if (chosen.isNull() && !cur.isNull() && String(cur["state"] | "") == "playing") chosen = cur;
    if (chosen.isNull()) chosen = firstPlaying;
    if (chosen.isNull() && !cur.isNull()) chosen = cur;
    if (chosen.isNull()) chosen = firstLoaded;
    if (chosen.isNull()) chosen = first;
    if (chosen.isNull()) {
        g_consecutiveFails = 0;
        setPhase(Phase::Idle, "The companion sees no Roon zone");
        return false;
    }
    if (pinned.length() && !isPinned)
        Serial.printf("[net] WARNING: pinned zone %s not found, following another zone\n", pinned.c_str());

    String id = chosen["zone_id"] | "";
    String name = chosen["display_name"] | "";
    if (id != current) Serial.printf("[net] following zone \"%s\"%s\n", name.c_str(), isPinned ? " (pinned)" : "");
    update([&](Snapshot &s) {
        s.zoneId = id;
        s.zoneName = name;
        s.zonePinned = isPinned;
        s.zones = list;
        s.zonesSerial++;
    });
    return true;
}

// ---- Now playing + lyrics ----

void clearTrack() {
    g_trackKey = "";
    if (g_imageKey.length()) {
        g_imageKey = "";
        setCover({});
    }
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

void fetchLyrics(const String &zoneId) {
    update([](Snapshot &s) { s.lyricsStatus = LyricsStatus::Loading; });
    JsonDocument doc(psram());
    // The companion resolves lyrics before answering (cache, tags, LRCLIB…): allow time.
    HttpResult r = getJson("/api/lyrics/current?zone_id=" + zoneId, doc, 20000);
    g_lyricsFetchedAt = millis();
    if (r.code != 200) {
        Serial.printf("[net] lyrics failed: %d %s\n", r.code, r.message.c_str());
        update([&](Snapshot &s) {
            s.lyricsStatus = r.errorCode == "NO_CURRENT_TRACK" ? LyricsStatus::None : LyricsStatus::Error;
            s.lyricsError = r.message;
        });
        return;
    }
    JsonObjectConst l = doc["lyrics"];
    bool found = l["found"] | false;
    bool instrumental = l["instrumental"] | false;
    String text = l["text"] | "";
    String source = l["source"] | "";
    int32_t offset = l["offset_ms"] | 0;
    std::shared_ptr<const lyrics::Lyrics> parsed;
    LyricsStatus st;
    if (instrumental) st = LyricsStatus::Instrumental;
    else if (!found || !text.length()) st = LyricsStatus::NotFound;
    else {
        parsed = std::make_shared<const lyrics::Lyrics>(lyrics::parse(text));
        st = parsed->lines.empty() ? LyricsStatus::NotFound : LyricsStatus::Found;
    }
    Serial.printf("[net] lyrics: %s, %u lines, %s, source %s, offset %d ms\n",
                  st == LyricsStatus::Found ? "found" : st == LyricsStatus::Instrumental ? "instrumental" : "not found",
                  parsed ? (unsigned)parsed->lines.size() : 0u, parsed && parsed->synced ? "synced" : "plain",
                  source.c_str(), (int)offset);
    update([&](Snapshot &s) {
        s.lyricsStatus = st;
        s.lyrics = parsed;
        s.lyricsSerial++;
        s.offsetMs = offset;
        s.lyricsSource = source;
        s.lyricsError = "";
    });
}

void pollNowPlaying() {
    String zoneId;
    xSemaphoreTake(g_lock, portMAX_DELAY);
    zoneId = g_snap.zoneId;
    xSemaphoreGive(g_lock);
    if (!zoneId.length()) return;

    JsonDocument doc(psram());
    uint32_t sentAt = millis();
    HttpResult r = getJson("/api/roon/now-playing?zone_id=" + zoneId, doc, 3000);
    uint32_t now = millis();
    if (r.code == 404 && r.errorCode == "NO_CURRENT_TRACK") {
        g_consecutiveFails = 0;
        if (g_trackKey.length()) clearTrack();
        String zoneName;
        xSemaphoreTake(g_lock, portMAX_DELAY);
        zoneName = g_snap.zoneName;
        xSemaphoreGive(g_lock);
        setPhase(Phase::Idle, "Nothing playing in " + zoneName);
        return;
    }
    if (r.code != 200) {
        reportCompanionFailure("now-playing", r);
        return;
    }
    g_consecutiveFails = 0;

    JsonObjectConst np = doc["now_playing"];
    String title = np["title"] | "";
    String artist = np["artist"] | "";
    String album = np["album"] | "";
    String state = np["state"] | "";
    String imageKey = np["image_key"] | "";
    uint32_t durationMs = (uint32_t)(np["duration_seconds"] | 0) * 1000u;
    int32_t seekMs = (int32_t)(np["seek_position_seconds"] | 0) * 1000;
    String key = zoneId + "\x1f" + title + "\x1f" + artist + "\x1f" + album + "\x1f" + String(durationMs);
    bool newTrack = key != g_trackKey;
    // Middle of the request round trip: best local time for the reported position.
    uint32_t sampleAt = sentAt + (now - sentAt) / 2;

    std::vector<OutputInfo> outputs;
    for (JsonObjectConst o : np["outputs"].as<JsonArrayConst>()) {
        OutputInfo oi;
        oi.id = o["output_id"] | "";
        oi.name = o["display_name"] | "";
        JsonObjectConst v = o["volume"];
        if (!v.isNull()) {
            oi.volumeType = v["type"] | "";
            oi.value = v["value"] | 0.0f;
            oi.min = v["min"] | 0.0f;
            oi.max = v["max"] | 0.0f;
            oi.step = v["step"] | 1.0f;
            oi.muted = v["is_muted"] | false;
        }
        outputs.push_back(oi);
    }
    bool canPlay = np["is_play_allowed"] | false, canPause = np["is_pause_allowed"] | false;
    bool canNext = np["is_next_allowed"] | false, canPrevious = np["is_previous_allowed"] | false;
    if (newTrack) g_clock.reset();
    g_clock.observe(seekMs, sampleAt, state == "playing", (int32_t)(now - sentAt) / 2 + 10);
    update([&](Snapshot &s) {
        s.positionMs = g_clock.positionAt(sampleAt);
        s.positionAtMs = sampleAt;
        s.positionUncertaintyMs = g_clock.uncertaintyMs();
        s.state = state;
        s.title = title;
        s.artist = artist;
        s.album = album;
        s.durationMs = durationMs;
        s.outputs = std::move(outputs);
        s.canPlay = canPlay;
        s.canPause = canPause;
        s.canNext = canNext;
        s.canPrevious = canPrevious;
        if (newTrack) {
            s.lyricsStatus = LyricsStatus::Loading;
            s.lyrics.reset();
            s.lyricsSerial++;
        }
    });
    setPhase(Phase::Playing, state == "playing" ? "Playing" : state == "paused" ? "Paused" : state);

    if (newTrack) {
        g_trackKey = key;
        g_lyricsRetries = 0;
        Serial.printf("[net] track: %s - %s (%u s, %s)\n", artist.c_str(), title.c_str(),
                      (unsigned)(durationMs / 1000), state.c_str());
        fetchLyrics(zoneId);
    } else {
        if (imageKey != g_imageKey || settings::backdrop() != g_backdropWanted) {
            g_backdropWanted = settings::backdrop();
            fetchCover(imageKey);
        }
        LyricsStatus ls;
        xSemaphoreTake(g_lock, portMAX_DELAY);
        ls = g_snap.lyricsStatus;
        xSemaphoreGive(g_lock);
        if ((ls == LyricsStatus::NotFound || ls == LyricsStatus::Error) && g_lyricsRetries < LYRICS_MAX_RETRIES &&
            millis() - g_lyricsFetchedAt > LYRICS_RETRY_MS) {
            g_lyricsRetries++;
            fetchLyrics(zoneId);
        }
    }
}

// ---- User commands ----

void commandFailed(const String &what, const HttpResult &r) {
    String msg = what + " failed: " + (r.message.length() ? r.message : "HTTP " + String(r.code));
    Serial.printf("[net] %s\n", msg.c_str());
    update([&](Snapshot &s) {
        s.commandError = msg;
        s.commandErrorAt = millis();
    });
}

void runCommand(const Command &c) {
    String zoneId;
    std::vector<OutputInfo> outputs;
    xSemaphoreTake(g_lock, portMAX_DELAY);
    zoneId = g_snap.zoneId;
    outputs = g_snap.outputs;
    xSemaphoreGive(g_lock);

    if (c.type == CmdType::Zone) {
        settings::setZone(String(c.arg));
        Serial.printf("[net] zone %s\n", c.arg[0] ? c.arg : "auto (follow the playing zone)");
        g_lastZonesAt = 0;   // re-pick at once
        return;
    }
    if (!g_baseUrl.length() || !zoneId.length()) return;
    JsonDocument body(psram()), resp(psram());
    if (c.type == CmdType::Control) {
        body["action"] = c.arg;
        body["zone_id"] = zoneId;   // always: never act on the companion's global active zone
        HttpResult r = request("/api/roon/control", &body, resp, 4000);
        if (r.code == 200) Serial.printf("[net] control %s ok\n", c.arg);
        else commandFailed(String("Control ") + c.arg, r);
    } else if (c.type == CmdType::VolumeSet) {
        for (const OutputInfo &o : outputs) {
            if (o.volumeType != "number" && o.volumeType != "db") continue;
            body["output_id"] = o.id;
            body["how"] = "absolute";
            body["value"] = c.value;
            HttpResult r = request("/api/roon/volume", &body, resp, 4000);
            if (r.code != 200) commandFailed("Volume " + o.name, r);
            break;   // the slider follows the first output with an absolute volume
        }
    } else if (c.type == CmdType::Mute) {
        for (const OutputInfo &o : outputs) {
            if (!o.volumeType.length()) continue;
            body.clear();
            body["output_id"] = o.id;
            body["how"] = c.direction ? "mute" : "unmute";
            HttpResult r = request("/api/roon/mute", &body, resp, 4000);
            if (r.code != 200) commandFailed(String(c.direction ? "Mute " : "Unmute ") + o.name, r);
        }
    } else if (c.type == CmdType::Offset) {
        String title, artist, album;
        xSemaphoreTake(g_lock, portMAX_DELAY);
        title = g_snap.title;
        artist = g_snap.artist;
        album = g_snap.album;
        xSemaphoreGive(g_lock);
        if (!title.length()) return;
        body["title"] = title;
        body["artist"] = artist;
        if (album.length()) body["album"] = album;
        body["offset_ms"] = (int32_t)c.value;
        HttpResult r = request("/api/lyrics/offset", &body, resp, 4000);
        if (r.code != 200) {
            commandFailed("Lyrics offset", r);
            return;
        }
        int32_t saved = resp["offset_ms"] | (int32_t)c.value;
        update([&](Snapshot &s) { s.offsetMs = saved; });
        Serial.printf("[net] lyrics offset %d ms saved for \"%s\"\n", (int)saved, title.c_str());
        return;
    } else {
        int sent = 0;
        for (const OutputInfo &o : outputs) {
            if (!o.volumeType.length()) continue;
            body.clear();
            body["output_id"] = o.id;
            body["how"] = "relative_step";
            body["value"] = c.direction;
            HttpResult r = request("/api/roon/volume", &body, resp, 4000);
            if (r.code != 200) commandFailed("Volume " + o.name, r);
            sent++;
        }
        if (!sent) {
            HttpResult r;
            r.message = "this zone has no volume control";
            commandFailed("Volume", r);
        }
    }
    g_lastNowPlayingAt = 0;   // show the effect right away
}

// ---- Task ----

void resetSession() {
    g_baseUrl = "";
    g_consecutiveFails = 0;
    g_trackKey = "";
    update([](Snapshot &s) {
        s.companion = "";
        s.zoneId = s.zoneName = "";
    });
    clearTrack();
}

void task(void *) {
    uint32_t lastDiscovery = 0;
    bool wifiStarted = false;

    WiFi.persistent(false);   // credentials live in our NVS namespace, not the driver's
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
            webupdate::loop(false, portal::active());
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
        ota::tick(true);   // update check (boot + daily) and a confirmed install; no companion needed
        webupdate::loop(true, portal::active());
        if (!g_baseUrl.length()) {
            if (now - lastDiscovery < DISCOVERY_PERIOD_MS && lastDiscovery) {
                delay(20);
                continue;
            }
            lastDiscovery = now;
            if (!discover()) continue;
            g_lastZonesAt = 0;
        }

        Command cmd;
        while (xQueueReceive(g_commands, &cmd, 0) == pdTRUE) runCommand(cmd);

        now = millis();
        if (!g_lastZonesAt || now - g_lastZonesAt > ZONES_PERIOD_MS) {
            g_lastZonesAt = now ? now : 1;
            if (!refreshZones()) {
                delay(200);
                continue;
            }
            ota::markHealthy();   // Wi-Fi + companion answered: a freshly installed image is good
        }
        if (!g_lastNowPlayingAt || now - g_lastNowPlayingAt >= NOW_PLAYING_PERIOD_MS) {
            g_lastNowPlayingAt = now ? now : 1;
            pollNowPlaying();
        }
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

void begin() {
    ota::begin();
    g_lock = xSemaphoreCreateMutex();
    g_commands = xQueueCreate(8, sizeof(Command));
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);   // eFuse MAC: valid before Wi-Fi starts
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
void reconnect() { g_reconnectRequested = true; }
void pause(bool on) { g_paused = on; }

namespace {
void enqueue(CmdType type, const char *arg, int8_t direction, float value = 0) {
    Command c{};
    c.type = type;
    c.direction = direction;
    c.value = value;
    strlcpy(c.arg, arg ? arg : "", sizeof c.arg);
    if (xQueueSend(g_commands, &c, 0) != pdTRUE) Serial.println("[net] WARNING: command queue full, command dropped");
}
}  // namespace

void control(const char *action) { enqueue(CmdType::Control, action, 0); }
void volumeStep(int direction) { enqueue(CmdType::Volume, nullptr, direction > 0 ? 1 : -1); }
void selectZone(const String &zoneId) { enqueue(CmdType::Zone, zoneId.c_str(), 0); }
void volumeSet(float value) { enqueue(CmdType::VolumeSet, nullptr, 0, value); }
void mute(bool on) { enqueue(CmdType::Mute, nullptr, on ? 1 : 0); }
void setOffset(int32_t ms) { enqueue(CmdType::Offset, nullptr, 0, (float)ms); }

}  // namespace net
