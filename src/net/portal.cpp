#include "portal.h"

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

#include "net.h"
#include "web_update.h"
#include "settings.h"

namespace portal {
namespace {

WebServer g_http(80);
DNSServer g_dns;
bool g_active = false;
bool g_restartPending = false;

String esc(const String &s) {
    String o;
    o.reserve(s.length() + 8);
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        switch (c) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '"': o += "&quot;"; break;
            case '\'': o += "&#39;"; break;
            default: o += c;
        }
    }
    return o;
}

// Signal strength as bars, for the network list.
const char *bars(int8_t rssi) {
    return rssi > -55 ? "\u2582\u2584\u2586\u2588" : rssi > -67 ? "\u2582\u2584\u2586" : rssi > -78 ? "\u2582\u2584" : "\u2582";
}

// The networks the panel hears, as buttons that fill the name field (net task scans).
String networkList() {
    std::shared_ptr<const std::vector<net::WifiNet>> nets = net::snapshot().networks;
    if (!nets || nets->empty()) return "<p><small>Looking for networks&hellip; <a href=/>refresh</a></small></p>";
    String h = "<p>Tap your network <small>(2.4 GHz only)</small></p><div>";
    // The ten strongest: a far neighbour's network can still be typed by hand.
    for (size_t i = 0; i < nets->size() && i < 10; i++) {
        const net::WifiNet &w = (*nets)[i];
        h += "<button type=button class=net data-s=\"" + esc(w.ssid) + "\" onclick=pick(this)>" + esc(w.ssid) +
             "<span>" + String(w.open ? "" : "\U0001F512 ") + bars(w.rssi) + "</span></button>";
    }
    return h + "</div><p><small>Not in the list? Type its name below, or <a href=/>refresh</a>.</small></p>";
}

const char PAGE_HEAD[] PROGMEM = R"(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>TFT Panel setup</title>
<style>body{font:16px system-ui,sans-serif;max-width:28rem;margin:0 auto;padding:1rem;background:#15161a;color:#f6f4ee}
h1{font-size:1.4rem}label{display:block;margin-top:1rem}input,select{width:100%;box-sizing:border-box;padding:.6rem;
font-size:1rem;border-radius:.4rem;border:1px solid #2f3037;background:#202127;color:#f6f4ee}
button{margin-top:1.4rem;width:100%;padding:.8rem;font-size:1.05rem;border:0;border-radius:.4rem;background:#6db56d;color:#15161a}
small{color:#a9aaa6}a{color:#6db56d}.net{display:flex;justify-content:space-between;box-sizing:border-box;margin:.4rem 0 0;padding:.75rem .9rem;text-align:left;background:#202127;color:#f6f4ee;border:1px solid #2f3037}.net span{color:#a9aaa6;margin-left:1rem;white-space:nowrap;min-width:5.5em;text-align:right}.err{color:#15161a;background:#ffb0a0;padding:.8rem;border-radius:.4rem}</style></head><body>)";

void sendForm(const String &error, const String &typedSsid = String()) {
    settings::Settings s = settings::get();
    if (typedSsid.length()) s.wifiSsid = typedSsid;
    String h = FPSTR(PAGE_HEAD);
    h += "<h1>TFT Panel setup</h1>";
    if (error.length()) h += "<p class=err>" + esc(error) + "</p>";
    h += networkList();
    h += "<form method=post action=/save>"
         "<label>Wi-Fi network<input id=ssid name=ssid required maxlength=32 value=\"" + esc(s.wifiSsid) + "\"></label>"
         "<label>Wi-Fi password<input id=pass name=pass type=password maxlength=63 placeholder=\"" +
         String(s.wifiPass.length() ? "unchanged if left empty" : "") + "\"></label>"
         "<label>Companion address <small>(optional, found automatically on the LAN)</small>"
         "<input name=companion placeholder=\"http://192.168.1.20:3888\" value=\"" + esc(s.companionUrl) + "\"></label>"
         "<label>Companion access token <small>(only if the companion has ACCESS_TOKEN)</small>"
         "<input name=token type=password placeholder=\"" +
         String(s.accessToken.length() ? "set — unchanged if left empty" : "") + "\"></label>"
         "<label>Where the music information comes from"
         "<select name=source>"
         "<option value=companion" + String(s.source == settings::SourceKind::Companion ? " selected" : "") +
         ">The TextFromTrack companion</option>"
         "<option value=direct" + String(s.source == settings::SourceKind::Direct ? " selected" : "") +
         ">The music server, directly</option></select></label>"
         "<label>Lyrics server <small>(direct only; empty = a companion on the network, "
         "<code>none</code> = no lyrics)</small>"
         "<input name=lyrics placeholder=\"http://192.168.1.30:3300\" value=\"" + esc(s.lyricsUrl) + "\"></label>"
         "<label>Lyrics server token <small>(only if it asks for one)</small>"
         "<input name=lyricstoken type=password placeholder=\"" +
         String(s.lyricsToken.length() ? "set — unchanged if left empty" : "") + "\"></label>"
         "<button>Save and restart</button></form>"
         "<script>function pick(b){ssid.value=b.dataset.s;pass.focus()}</script></body></html>";
    g_http.send(200, "text/html; charset=utf-8", h);
}

void handleSave() {
    String ssid = g_http.arg("ssid");
    String pass = g_http.arg("pass");
    String companion = g_http.arg("companion");
    String token = g_http.arg("token");
    String source = g_http.arg("source");
    String lyrics = g_http.arg("lyrics");
    String lyricsToken = g_http.arg("lyricstoken");
    ssid.trim();
    companion.trim();
    token.trim();
    lyrics.trim();
    lyricsToken.trim();
    settings::Settings cur = settings::get();
    if (!pass.length() && ssid == cur.wifiSsid) pass = cur.wifiPass;   // "unchanged if left empty"

    String error;
    if (!ssid.length() || ssid.length() > 32) error = "The network name must be 1 to 32 characters.";
    else if (pass.length() && (pass.length() < 8 || pass.length() > 63))
        error = "A WPA password is 8 to 63 characters (leave it empty for an open network).";
    else if (companion.length() && !companion.startsWith("http://"))
        error = "The companion address starts with http:// (for example http://192.168.1.20:3888).";
    else if (lyrics.length() && lyrics != "none" && !lyrics.startsWith("http://") && !lyrics.startsWith("https://"))
        error = "The lyrics server starts with http:// or https://, or is the word none.";
    if (error.length()) {
        Serial.printf("[portal] save rejected: %s (args=%d, ssid len %u, pass len %u)\n", error.c_str(),
                      g_http.args(), ssid.length(), pass.length());
        return sendForm(error, ssid);
    }
    while (companion.endsWith("/")) companion.remove(companion.length() - 1);

    while (lyrics.endsWith("/")) lyrics.remove(lyrics.length() - 1);
    settings::setWifi(ssid, pass);
    settings::setCompanion(companion);
    if (token.length()) settings::setToken(token);
    if (source.length()) settings::setSource(source == "direct" ? settings::SourceKind::Direct
                                                                : settings::SourceKind::Companion);
    settings::setLyricsUrl(lyrics);
    if (lyricsToken.length()) settings::setLyricsToken(lyricsToken == "none" ? "" : lyricsToken);

    String h = FPSTR(PAGE_HEAD);
    h += "<h1>Saved</h1><p>The panel restarts and joins <b>" + esc(ssid) +
         "</b>. Reconnect this phone to your usual Wi-Fi.</p></body></html>";
    g_http.send(200, "text/html; charset=utf-8", h);
    Serial.printf("[portal] settings saved for ssid \"%s\"\n", ssid.c_str());
    g_restartPending = true;
}

void logRequest() {
    static const char *names[] = {"ANY", "GET", "HEAD", "POST", "PUT", "PATCH", "DELETE", "OPTIONS"};
    int m = (int)g_http.method();
    Serial.printf("[portal] %s %s%s args=%d\n", (m >= 0 && m < 8) ? names[m] : "?", g_http.hostHeader().c_str(),
                  g_http.uri().c_str(), g_http.args());
}

void redirectHome() {
    logRequest();
    g_http.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
    g_http.send(302, "text/plain", "");
}

}  // namespace

void start(const String &ssid, const String &pass) {
    if (g_active) return;
    WiFi.mode(settings::get().wifiSsid.length() ? WIFI_AP_STA : WIFI_AP);
    if (!WiFi.softAP(ssid.c_str(), pass.c_str())) {
        Serial.println("[portal] ERROR: softAP failed to start");
        return;
    }
    delay(100);
    // Port 80 belongs to the update page while Wi-Fi is up: active() makes its task let go first,
    // or the portal's server cannot bind and the setup page never answers.
    g_active = true;
    for (uint32_t t0 = millis(); webupdate::serving() && millis() - t0 < 2000;) delay(10);
    IPAddress ip = WiFi.softAPIP();
    g_dns.setErrorReplyCode(DNSReplyCode::NoError);
    g_dns.start(53, "*", ip);   // captive: every name resolves to the panel

    g_http.on("/", HTTP_GET, [] {
        logRequest();
        sendForm("");
    });
    g_http.on("/save", HTTP_POST, [] {
        logRequest();
        handleSave();
    });
    g_http.onNotFound(redirectHome);   // generate_204, hotspot-detect.html… → captive page
    g_http.begin();

    static bool eventsHooked = false;
    if (!eventsHooked) {
        eventsHooked = true;
        WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t) { Serial.println("[portal] a device joined the AP"); },
                     ARDUINO_EVENT_WIFI_AP_STACONNECTED);
        WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t) { Serial.println("[portal] a device left the AP"); },
                     ARDUINO_EVENT_WIFI_AP_STADISCONNECTED);
    }
    net::requestScan();   // the list on the page (net task)
    g_active = true;
    Serial.printf("[portal] AP \"%s\" up at %s\n", ssid.c_str(), ip.toString().c_str());
}

void stop() {
    if (!g_active) return;
    g_http.stop();
    g_dns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    g_active = false;
    Serial.println("[portal] AP stopped");
}

bool active() { return g_active; }

void loop() {
    if (!g_active) return;
    g_dns.processNextRequest();
    g_http.handleClient();
}

bool savedAndRestartPending() { return g_restartPending; }

}  // namespace portal
