#include "lyrics_source.h"

#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>

#include "../app/lyrics.h"
#include "lyrics_ladder.h"
#include "net.h"
#include "net_internal.h"
#include "settings.h"

namespace net::lyricsrc {

using namespace internal;

namespace {

// What the last look found, read by the web and console tasks: always copied under the lock,
// since the net task rewrites it.
CompanionInfo g_companion;
SemaphoreHandle_t companionLock() {
    static SemaphoreHandle_t l = xSemaphoreCreateMutex();
    return l;
}
void setCompanion(const CompanionInfo &ci) {
    xSemaphoreTake(companionLock(), portMAX_DELAY);
    g_companion = ci;
    xSemaphoreGive(companionLock());
}
// The result - found or not - is kept a minute: a search blocks the net task up to 3 s, and a
// song with nothing to show would otherwise pay it on every retry.
uint32_t g_searchedAt = 0;
bool g_searched = false;
volatile bool g_forgetRequested = false;   // set from any task, honoured by the net task
constexpr uint32_t REDISCOVER_MS = 60000;

// Percent-encodes a query value: track and artist names carry spaces, accents and punctuation.
String urlEncode(const String &in) {
    static const char *DIGITS = "0123456789ABCDEF";
    String out;
    out.reserve(in.length() + 16);
    for (unsigned i = 0; i < in.length(); i++) {
        uint8_t c = (uint8_t)in[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (char)c;
        else {
            out += '%';
            out += DIGITS[c >> 4];
            out += DIGITS[c & 0x0f];
        }
    }
    return out;
}

// The companion announces itself; nothing else on the network does. Net task only.
CompanionInfo findCompanion() {
    uint32_t now = millis();
    if (g_forgetRequested) {
        g_forgetRequested = false;
        g_searched = false;
    }
    if (g_searched && now - g_searchedAt < REDISCOVER_MS) return companionInfo();
    g_searchedAt = now;
    g_searched = true;
    CompanionInfo ci;
    ci.url = mdnsServiceUrl("textfromtrack", "tcp", &ci.host, &ci.api, &ci.version);
    ci.state = ci.url.length() ? CompanionInfo::State::Found : CompanionInfo::State::NotFound;
    setCompanion(ci);
    // Said out loud either way: a track with no lyrics and no reason given is the one thing
    // this screen must not do. The memo above keeps it to once a minute at worst.
    if (ci.url.length())
        Serial.printf("[net] lyrics: companion %s%s at %s, asked on %s\n", ci.version.length() ? "v" : "",
                      ci.version.c_str(), ci.url.c_str(), ci.api == "get" ? "/api/get" : "/api/lrclib/lookup");
    else Serial.println("[net] lyrics: no companion announced on the network");
    return ci;
}

// The ladder, from what is typed and what the network answered.
using Choice = ladder::Choice<String>;
Choice decide(const settings::Settings &st, const CompanionInfo &ci) {
    return ladder::choose<String>(st.lyricsUrl, ci.state == CompanionInfo::State::Found ? ci.url : String(),
                                  ci.api, net::hostName());
}

struct Answer {
    int code = 0;          // HTTP status, or < 0 for a transport error
    String message;
    JsonDocument doc{psram()};
};

Answer get(const String &url, const String &token) {
    Answer a;
    NetworkClientSecure secure;
    WiFiClient plain;
    bool https = url.startsWith("https://");
    if (https) secure.useBuiltinCACertBundle();   // the same roots the update check trusts
    HTTPClient http;
    http.setConnectTimeout(3000);
    http.setTimeout(8000);
    http.setReuse(false);
    http.useHTTP10(true);
    if (!(https ? http.begin(secure, url) : http.begin(plain, url))) {
        a.code = -100;
        a.message = "bad lyrics server address";
        return a;
    }
    if (token.length()) http.addHeader("Authorization", "Bearer " + token);
    a.code = http.GET();
    if (a.code < 0) {
        a.message = HTTPClient::errorToString(a.code);
        http.end();
        return a;
    }
    if (a.code == 200) {
        DeserializationError err = deserializeJson(a.doc, http.getStream());
        if (err) {
            a.code = -101;
            a.message = String("bad answer (") + err.c_str() + ")";
        }
    } else {
        a.message = "HTTP " + String(a.code);
    }
    http.end();
    return a;
}

// Publishes what one answer holds. The two server kinds name their fields differently and agree
// on everything else, so one reader covers both.
void publish(JsonDocument &doc, bool companionShape, const char *sourceName, int32_t offsetMs) {
    bool instrumental = (companionShape ? doc["instrumental"] : doc["instrumental"]) | false;
    if (instrumental) {
        Serial.println("[net] lyrics: instrumental");
        publishLyrics(LyricsStatus::Instrumental, nullptr, sourceName, 0);
        return;
    }
    String text = (companionShape ? doc["synced"] : doc["syncedLyrics"]) | "";
    if (!text.length()) text = (companionShape ? doc["plain"] : doc["plainLyrics"]) | "";
    if (!text.length()) {
        Serial.println("[net] lyrics: none for this track");
        publishLyricsFailed(LyricsStatus::NotFound, "");
        return;
    }
    auto parsed = std::make_shared<const lyrics::Lyrics>(lyrics::parse(text));
    if (parsed->lines.empty()) {
        publishLyricsFailed(LyricsStatus::NotFound, "");
        return;
    }
    Serial.printf("[net] lyrics: found, %u lines, %s, from %s%s\n", (unsigned)parsed->lines.size(),
                  parsed->synced ? "synced" : "plain", sourceName, offsetMs ? " (with its timing offset)" : "");
    publishLyrics(LyricsStatus::Found, parsed, sourceName, offsetMs);
}

}  // namespace

void forgetDiscovered() {
    g_forgetRequested = true;
    setCompanion(CompanionInfo{});
}

CompanionInfo companionInfo() {
    xSemaphoreTake(companionLock(), portMAX_DELAY);
    CompanionInfo ci = g_companion;
    xSemaphoreGive(companionLock());
    return ci;
}

String whyNoLyrics() { return decide(settings::get(), companionInfo()).why; }

bool waitingForServer() {
    return decide(settings::get(), companionInfo()).kind == Choice::Kind::None;
}

bool lookup(const String &title, const String &artist, const String &album, uint32_t durationMs) {
    settings::Settings st = settings::get();
    if (st.lyricsUrl == "none") return false;   // a decision: no look on the network either
    if (!title.length() || !artist.length()) return false;

    Choice c = decide(st, findCompanion());
    if (c.kind == Choice::Kind::None) {
        Serial.println("[net] no lyrics server: no companion on the network, and no fallback typed on the page");
        return false;
    }
    const bool companion = c.kind == Choice::Kind::Companion;
    const bool lookupShape = companion && !c.getApi;   // a companion before 0.7.1
    const char *sourceName = companion ? "companion" : "lyrics server";

    publishLyricsLoading();
    String url = c.base;
    if (lookupShape) {
        url += "/api/lrclib/lookup?title=" + urlEncode(title) + "&artist=" + urlEncode(artist);
        if (album.length()) url += "&album=" + urlEncode(album);
        if (durationMs) url += "&duration=" + String(durationMs / 1000);
    } else {
        url += "/api/get?track_name=" + urlEncode(title) + "&artist_name=" + urlEncode(artist);
        if (album.length()) url += "&album_name=" + urlEncode(album);
        if (durationMs) url += "&duration=" + String(durationMs / 1000);
    }

    Answer a = get(url, st.lyricsToken);
    // A 404 is the one answer that means "not here". Everything else is a fault worth retrying,
    // which is what the lyrics servers' own specifications ask for.
    if (a.code == 404) {
        Serial.println("[net] lyrics: not on the lyrics server");
        publishLyricsFailed(LyricsStatus::NotFound, "");
        return true;
    }
    if (a.code != 200) {
        Serial.printf("[net] lyrics failed: %s\n", a.message.c_str());
        publishLyricsFailed(LyricsStatus::Error, a.message);
        if (companion) forgetDiscovered();   // it may have moved or stopped: the retry climbs down
        return true;
    }
    // The old companion endpoint answers 200 with found:false when it has nothing.
    if (lookupShape && !(a.doc["found"] | false)) {
        Serial.println("[net] lyrics: none for this track");
        publishLyricsFailed(LyricsStatus::NotFound, "");
        return true;
    }
    // /api/get from a companion carries the per-track timing offset the listener set there.
    const int32_t offsetMs = companion && c.getApi ? (int32_t)(a.doc["offsetMs"] | 0) : 0;
    publish(a.doc, lookupShape, sourceName, offsetMs);
    return true;
}

}  // namespace net::lyricsrc
