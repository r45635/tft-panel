#include "source_companion.h"

#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "../app/lyrics.h"
#include "settings.h"

namespace net {

using namespace internal;

namespace {

constexpr uint32_t NOW_PLAYING_PERIOD_MS = 300;   // narrow windows for PositionClock
constexpr uint32_t ZONES_PERIOD_MS = 5000;
constexpr uint32_t DISCOVERY_PERIOD_MS = 5000;
constexpr int FAILS_BEFORE_REDISCOVERY = 6;       // ~3 s of failed polls

}  // namespace

// ---- HTTP ----

// GET when `body` is null, else POST it as JSON.
HttpResult CompanionSource::request(const String &path, const JsonDocument *body, JsonDocument &doc,
                                    uint32_t timeoutMs) {
    HttpResult r;
    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(3000);
    http.setTimeout(timeoutMs);
    http.setReuse(false);
    if (!http.begin(client, baseUrl_ + path)) {
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

// Turns a failed call into the status line, and drops a discovered companion after
// several failures in a row so a restarted one (new IP/port) is found again.
void CompanionSource::reportFailure(const char *what, const HttpResult &r) {
    consecutiveFails_++;
    String msg;
    if (r.code == 401) msg = "Companion needs its access token (set it in the setup portal)";
    else if (r.code == 503) msg = "Companion: Roon Core not connected";
    else msg = String("Companion ") + what + " failed: " + r.message;
    setPhase(Phase::CompanionError, msg);
    if (fromMdns_ && consecutiveFails_ >= FAILS_BEFORE_REDISCOVERY) {
        Serial.printf("[net] dropping %s after %d failures, rediscovering\n", baseUrl_.c_str(), consecutiveFails_);
        baseUrl_ = "";
        publishServer("");
    }
}

// ---- Discovery ----

void CompanionSource::reset() {
    baseUrl_ = "";
    consecutiveFails_ = 0;
    lastDiscoveryAt_ = 0;
    lastZonesAt_ = 0;
    lastNowPlayingAt_ = 0;
}

bool CompanionSource::connect(uint32_t now) {
    if (lastDiscoveryAt_ && now - lastDiscoveryAt_ < DISCOVERY_PERIOD_MS) return false;
    lastDiscoveryAt_ = now;

    settings::Settings st = settings::get();
    if (st.companionUrl.length()) {
        baseUrl_ = st.companionUrl;
        fromMdns_ = false;
    } else {
        ensureMdns();
        setPhase(Phase::Discovering, "Looking for the TextFromTrack companion on the network...");
        int n = MDNS.queryService("textfromtrack", "tcp");
        for (int i = 0; i < n; i++) {
            IPAddress ip = MDNS.address(i);
            if (ip == IPAddress((uint32_t)0)) continue;
            baseUrl_ = "http://" + ip.toString() + ":" + String(MDNS.port(i));
            fromMdns_ = true;
            Serial.printf("[net] companion \"%s\" at %s\n", MDNS.hostname(i).c_str(), baseUrl_.c_str());
            break;
        }
        if (!baseUrl_.length()) {
            setPhase(Phase::Discovering,
                     n < 0 ? "mDNS query failed - set the companion address in the setup portal"
                           : "No companion found on the network yet (is it running?)");
            return false;
        }
    }
    consecutiveFails_ = 0;
    publishServer(baseUrl_);
    lastZonesAt_ = 0;   // ask for the zones at once
    return true;
}

// ---- Zones ----

bool CompanionSource::zonesDue(uint32_t now) const { return !lastZonesAt_ || now - lastZonesAt_ > ZONES_PERIOD_MS; }

bool CompanionSource::refreshZones() {
    lastZonesAt_ = millis() ? millis() : 1;
    JsonDocument doc(psram());
    HttpResult r = getJson("/api/roon/zones", doc, 4000);
    if (r.code != 200) {
        reportFailure("zones", r);
        return false;
    }
    auto list = std::make_shared<std::vector<ZoneInfo>>();
    for (JsonObjectConst z : doc["zones"].as<JsonArrayConst>())
        list->push_back({z["zone_id"] | "", z["display_name"] | "", z["state"] | "",
                         z["now_playing_title"] | "", z["now_playing_artist"] | ""});
    if (!publishZones(list)) {
        consecutiveFails_ = 0;
        setPhase(Phase::Idle, "The companion sees no Roon zone");
        return false;
    }
    return true;
}

// ---- Now playing ----

bool CompanionSource::nowPlayingDue(uint32_t now) const {
    return !lastNowPlayingAt_ || now - lastNowPlayingAt_ >= NOW_PLAYING_PERIOD_MS;
}

void CompanionSource::pollNowPlaying() {
    lastNowPlayingAt_ = millis() ? millis() : 1;
    String zoneId = followedZoneId();
    if (!zoneId.length()) return;

    JsonDocument doc(psram());
    uint32_t sentAt = millis();
    HttpResult r = getJson("/api/roon/now-playing?zone_id=" + zoneId, doc, 3000);
    uint32_t now = millis();
    if (r.code == 404 && r.errorCode == "NO_CURRENT_TRACK") {
        consecutiveFails_ = 0;
        publishIdle();
        return;
    }
    if (r.code != 200) {
        reportFailure("now-playing", r);
        return;
    }
    consecutiveFails_ = 0;

    JsonObjectConst np = doc["now_playing"];
    NowPlaying n;
    n.zoneId = zoneId;
    n.title = np["title"] | "";
    n.artist = np["artist"] | "";
    n.album = np["album"] | "";
    n.state = np["state"] | "";
    n.imageKey = np["image_key"] | "";
    n.durationMs = (uint32_t)(np["duration_seconds"] | 0) * 1000u;
    n.seekMs = (int32_t)(np["seek_position_seconds"] | 0) * 1000;
    // Middle of the request round trip: best local time for the reported position.
    n.sampleAtMs = sentAt + (now - sentAt) / 2;
    n.slackMs = (int32_t)(now - sentAt) / 2 + 10;
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
        n.outputs.push_back(oi);
    }
    n.canPlay = np["is_play_allowed"] | false;
    n.canPause = np["is_pause_allowed"] | false;
    n.canNext = np["is_next_allowed"] | false;
    n.canPrevious = np["is_previous_allowed"] | false;
    publishNowPlaying(std::move(n));
}

// ---- Lyrics ----

bool CompanionSource::fetchLyrics(const String &zoneId) {
    publishLyricsLoading();
    JsonDocument doc(psram());
    // The companion resolves lyrics before answering (cache, tags, LRCLIB…): allow time.
    HttpResult r = getJson("/api/lyrics/current?zone_id=" + zoneId, doc, 20000);
    if (r.code != 200) {
        Serial.printf("[net] lyrics failed: %d %s\n", r.code, r.message.c_str());
        publishLyricsFailed(r.errorCode == "NO_CURRENT_TRACK" ? LyricsStatus::None : LyricsStatus::Error, r.message);
        return true;
    }
    JsonObjectConst l = doc["lyrics"];
    bool found = l["found"] | false;
    bool instrumental = l["instrumental"] | false;
    String text = l["text"] | "";
    String src = l["source"] | "";
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
                  src.c_str(), (int)offset);
    publishLyrics(st, parsed, src, offset);
    return true;
}

// ---- Commands ----

void CompanionSource::command(const Command &c) {
    String zoneId = followedZoneId();
    std::vector<OutputInfo> outputs = followedOutputs();
    if (!ready() || !zoneId.length()) return;

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
        currentTrack(title, artist, album);
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
        publishOffset(saved);
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
    lastNowPlayingAt_ = 0;   // show the effect right away
}

// ---- Library ----

HttpResult CompanionSource::browse(JsonDocument &body, JsonDocument &resp) {
    return request("/api/roon/browse", &body, resp, 10000);
}

HttpResult CompanionSource::browseLoad(JsonDocument &body, JsonDocument &resp) {
    return request("/api/roon/browse/load", &body, resp, 10000);
}

String CompanionSource::imageUrl(const String &imageKey, int px) const {
    if (!imageKey.length() || !baseUrl_.length()) return String();
    return baseUrl_ + "/api/roon/image/" + imageKey + "?w=" + String(px) + "&h=" + String(px);
}

}  // namespace net
