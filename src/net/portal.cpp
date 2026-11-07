#include "portal.h"

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

#include "settings.h"

namespace portal {
namespace {

WebServer g_http(80);
DNSServer g_dns;
bool g_active = false;
bool g_restartPending = false;
String g_options;            // <option> list of scanned networks
uint32_t g_lastScanMs = 0;

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

void collectScan() {
    int n = WiFi.scanComplete();
    if (n < 0) return;   // still running (-1) or never started (-2)
    String opts;
    for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        if (!ssid.length() || opts.indexOf("\"" + esc(ssid) + "\"") >= 0) continue;
        opts += "<option value=\"" + esc(ssid) + "\">";
    }
    g_options = opts;
    WiFi.scanDelete();
}

const char PAGE_HEAD[] PROGMEM = R"(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>TFT Panel setup</title>
<style>body{font:16px system-ui,sans-serif;max-width:28rem;margin:0 auto;padding:1rem;background:#101418;color:#e8ecf0}
h1{font-size:1.4rem}label{display:block;margin-top:1rem}input{width:100%;box-sizing:border-box;padding:.6rem;
font-size:1rem;border-radius:.4rem;border:1px solid #3a4652;background:#1c232b;color:#e8ecf0}
button{margin-top:1.4rem;width:100%;padding:.8rem;font-size:1.05rem;border:0;border-radius:.4rem;background:#40c4ff;color:#101418}
small{color:#9aa6b2}.err{color:#ffb0a0}</style></head><body>)";

void sendForm(const String &error) {
    settings::Settings s = settings::get();
    String h = FPSTR(PAGE_HEAD);
    h += "<h1>TFT Panel setup</h1>";
    if (error.length()) h += "<p class=err>" + esc(error) + "</p>";
    h += "<form method=post action=/save>"
         "<label>Wi-Fi network<input name=ssid list=nets required maxlength=32 value=\"" + esc(s.wifiSsid) + "\"></label>"
         "<datalist id=nets>" + g_options + "</datalist>"
         "<label>Wi-Fi password<input name=pass type=password maxlength=63 placeholder=\"" +
         String(s.wifiPass.length() ? "unchanged if left empty" : "") + "\"></label>"
         "<label>Companion address <small>(optional, found automatically on the LAN)</small>"
         "<input name=companion placeholder=\"http://192.168.1.20:3888\" value=\"" + esc(s.companionUrl) + "\"></label>"
         "<label>Companion access token <small>(only if the companion has ACCESS_TOKEN)</small>"
         "<input name=token type=password placeholder=\"" +
         String(s.accessToken.length() ? "set — unchanged if left empty" : "") + "\"></label>"
         "<button>Save and restart</button></form></body></html>";
    g_http.send(200, "text/html; charset=utf-8", h);
}

void handleSave() {
    String ssid = g_http.arg("ssid");
    String pass = g_http.arg("pass");
    String companion = g_http.arg("companion");
    String token = g_http.arg("token");
    ssid.trim();
    companion.trim();
    token.trim();
    settings::Settings cur = settings::get();
    if (!pass.length() && ssid == cur.wifiSsid) pass = cur.wifiPass;   // "unchanged if left empty"

    if (!ssid.length() || ssid.length() > 32) return sendForm("The network name must be 1 to 32 characters.");
    if (pass.length() && (pass.length() < 8 || pass.length() > 63))
        return sendForm("A WPA password is 8 to 63 characters (leave it empty for an open network).");
    if (companion.length() && !companion.startsWith("http://"))
        return sendForm("The companion address starts with http:// (for example http://192.168.1.20:3888).");
    while (companion.endsWith("/")) companion.remove(companion.length() - 1);

    settings::setWifi(ssid, pass);
    settings::setCompanion(companion);
    if (token.length()) settings::setToken(token);

    String h = FPSTR(PAGE_HEAD);
    h += "<h1>Saved</h1><p>The panel restarts and joins <b>" + esc(ssid) +
         "</b>. Reconnect this phone to your usual Wi-Fi.</p></body></html>";
    g_http.send(200, "text/html; charset=utf-8", h);
    Serial.printf("[portal] settings saved for ssid \"%s\"\n", ssid.c_str());
    g_restartPending = true;
}

void redirectHome() {
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
    IPAddress ip = WiFi.softAPIP();
    g_dns.setErrorReplyCode(DNSReplyCode::NoError);
    g_dns.start(53, "*", ip);   // captive: every name resolves to the panel

    g_http.on("/", HTTP_GET, [] { sendForm(""); });
    g_http.on("/save", HTTP_POST, handleSave);
    g_http.onNotFound(redirectHome);   // generate_204, hotspot-detect.html… → captive page
    g_http.begin();

    WiFi.scanNetworks(true);
    g_lastScanMs = millis();
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
    collectScan();
    if (millis() - g_lastScanMs > 30000 && WiFi.scanComplete() == -2) {
        WiFi.scanNetworks(true);
        g_lastScanMs = millis();
    }
}

bool savedAndRestartPending() { return g_restartPending; }

}  // namespace portal
