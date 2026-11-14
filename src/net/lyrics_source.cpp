#include "lyrics_source.h"

#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>

#include "../app/lyrics.h"
#include "net_internal.h"
#include "settings.h"

namespace net::lyricsrc {

using namespace internal;

namespace {

String g_discovered;        // a companion found on the network, remembered between tracks
uint32_t g_discoveryAt = 0;
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

// The companion announces itself; nothing else on the network does, so this is how "auto" works.
String findCompanion() {
    uint32_t now = millis();
    if (g_discovered.length() && now - g_discoveryAt < REDISCOVER_MS) return g_discovered;
    int n = MDNS.queryService("textfromtrack", "tcp");
    g_discoveryAt = now ? now : 1;
    for (int i = 0; i < n; i++) {
        IPAddress ip = MDNS.address(i);
        if (ip == IPAddress((uint32_t)0)) continue;
        g_discovered = "http://" + ip.toString() + ":" + String(MDNS.port(i));
        Serial.printf("[net] lyrics from the companion at %s\n", g_discovered.c_str());
        return g_discovered;
    }
    g_discovered = "";
    return g_discovered;
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
void publish(JsonDocument &doc, bool companionShape, const char *sourceName) {
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
    Serial.printf("[net] lyrics: found, %u lines, %s, from %s\n", (unsigned)parsed->lines.size(),
                  parsed->synced ? "synced" : "plain", sourceName);
    publishLyrics(LyricsStatus::Found, parsed, sourceName, 0);
}

}  // namespace

void forgetDiscovered() {
    g_discovered = "";
    g_discoveryAt = 0;
}

bool lookup(const String &title, const String &artist, const String &album, uint32_t durationMs) {
    settings::Settings st = settings::get();
    if (st.lyricsUrl == "none") return false;
    if (!title.length() || !artist.length()) return false;

    bool companionShape = false;
    String base = st.lyricsUrl;
    if (!base.length()) {   // "auto": the companion if there is one, nothing otherwise
        base = findCompanion();
        companionShape = true;
        if (!base.length()) return false;
    }
    while (base.endsWith("/")) base.remove(base.length() - 1);

    publishLyricsLoading();
    String url = base;
    if (companionShape) {
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
        if (companionShape) forgetDiscovered();   // it may have moved or stopped
        return true;
    }
    // The companion answers 200 with found:false when it has nothing.
    if (companionShape && !(a.doc["found"] | false)) {
        Serial.println("[net] lyrics: none for this track");
        publishLyricsFailed(LyricsStatus::NotFound, "");
        return true;
    }
    publish(a.doc, companionShape, companionShape ? "companion" : "lyrics server");
    return true;
}

}  // namespace net::lyricsrc
