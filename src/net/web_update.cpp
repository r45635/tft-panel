#include "web_update.h"

#include "lyrics_source.h"
#include <mbedtls/sha256.h>

#include "news.h"
#include "weather.h"

#include <Update.h>
#include <WebServer.h>

#include <WiFi.h>

#include <esp_heap_caps.h>
#include <esp_ota_ops.h>

#include "../board/board.h"
#include "../board/board_pins.h"
#include "../version.h"
#include "net.h"
#include "ota.h"
#include "portal.h"
#include "settings.h"

namespace webupdate {
namespace {

constexpr uint32_t CODE_VALID_MS = 5 * 60 * 1000;

WebServer *g_server = nullptr;
volatile uint32_t g_code = 0;
volatile bool g_shots = false;
uint32_t g_codeAt = 0;
bool g_uploadAllowed = false, g_uploadOk = false;
String g_uploadError;

const char SETTINGS_PAGE[] PROGMEM = R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1"><title>%NAME%</title>
<style>body{font:17px/1.5 system-ui,sans-serif;max-width:560px;margin:32px auto;padding:0 16px;background:#15161a;color:#f6f4ee}
h1{margin-bottom:2px}.who{color:#8a8b87;font-size:14px;margin-top:0}
label{display:block;margin-top:18px}small{color:#8a8b87}
input,select,button{font:inherit;width:100%;box-sizing:border-box;padding:10px;border-radius:8px;
border:1px solid #2f3037;background:#202127;color:#f6f4ee;margin-top:4px}
button{background:#6db56d;color:#15161a;border:0;margin-top:22px}
.msg{background:#202127;border-left:3px solid #6db56d;padding:8px 12px;border-radius:4px}
.warn{background:#202127;border-left:3px solid #d08a3e;padding:8px 12px;border-radius:4px;font-size:15px}
.found{color:#8a8b87;font-size:15px;margin:6px 0 0}
.bad{background:#2a1d1b;border-left:3px solid #d2605a;padding:8px 12px;border-radius:4px}
.forget{margin-top:28px}.forget button{background:#2f3037;color:#c9cac6;width:auto;padding:6px 14px;margin:0}
a{color:#6db56d}</style></head><body>
<h1>%NAME%</h1><p class=who>%WHO%</p>
%MSG%
<form method=post action=/settings>
%CODEROW%
<label>Where the music information comes from
<select name=source><option value=companion%CSEL%>The TextFromTrack companion</option>
<option value=direct%DSEL%>The music server, directly</option></select></label>
<label>Companion address <small>(empty = found on the network)</small>
<input name=companion placeholder="http://192.168.1.20:3888" value="%COMPANION%"></label>
<label>Companion access token <small>(only if it has one; empty leaves it unchanged)</small>
<input name=token type=password autocomplete=off></label>
<label>Lyrics <small>(empty = a companion on the network, <code>none</code> = no lyrics,
 or the address of an LRCLIB-compatible server)</small>
<input name=lyrics placeholder="http://192.168.1.30:3300" value="%LYRICS%"></label>
<label>Lyrics server token <small>(only if it asks for one)</small>
<input name=lyricstoken type=password autocomplete=off></label>%LNOTE%
<label>Weather town <small>(add a region or country if the name is shared:
 <code>Montreal, Canada</code>, <code>Paris, Texas</code>, <code>Lyon, France</code>)</small>
<input name=weather placeholder="Montreal" value="%WEATHER%"></label>%WNOTE%
<label>News feed <small>(RSS or Atom, slid under the clock; empty = none)</small>
<input name=news placeholder="https://example.com/rss" value="%NEWS%"></label>%NNOTE%
%BMSG%<button>Save</button></form>
%FORGET%
<p><small>Firmware update: <a href=/update>/update</a> &middot; open source (MIT):
<a href="https://github.com/r45635/tft-panel">github.com/r45635/tft-panel</a></small></p>
</body></html>)HTML";

const char PAGE[] PROGMEM = R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1"><title>%NAME% update</title>
<style>body{font:17px/1.5 system-ui,sans-serif;max-width:560px;margin:32px auto;padding:0 16px;background:#15161a;color:#f6f4ee}
input,button{font:inherit;padding:10px;border-radius:8px;border:1px solid #2f3037;background:#202127;color:#f6f4ee}
button{background:#6db56d;color:#15161a;border:0;padding:10px 24px}#msg{margin-top:16px;color:#a9aaa6}
a{color:#6db56d}h1{margin-bottom:2px}.who{color:#8a8b87;font-size:14px;margin-top:0}</style></head><body>
<h1>Update %NAME%</h1><p class=who>%WHO%</p><p>Running <b>%VERSION%</b>. Choose a firmware file (<code>tft-panel-vX.Y.Z.bin</code>)
and type the code shown on the panel's screen.</p>
<p><input id="code" inputmode="numeric" maxlength="6" placeholder="6-digit code" autocomplete="off"></p>
<p><input id="file" type="file" accept=".bin"></p>
<p><input id="sha" maxlength="64" autocomplete="off" placeholder="sha256 from the release (optional)" style="width:100%;max-width:34em">
<br><small>Paste the line from <code>%ASSET%-vX.Y.Z.sha256</code> and the panel refuses the file if it does not match.
The panel checks the image itself either way, and goes back to this version if the new one does not start.</small></p>
<p><button id="go">Upload and install</button></p><div id="msg"></div>
<p><small>The firmware files and their sha256 are on the releases page,
<a href="https://github.com/r45635/tft-panel/releases">github.com/r45635/tft-panel/releases</a>.
This panel takes <code>%ASSET%-vX.Y.Z.bin</code> &mdash; the name matters: two different screens
can share the same chip.</small></p>
<script>
go.onclick=()=>{const f=file.files[0];if(!f||code.value.length!=6){msg.textContent='Pick a file and type the code.';return}
const fd=new FormData();fd.append('firmware',f);const x=new XMLHttpRequest();
x.upload.onprogress=e=>{msg.textContent='Uploading '+Math.round(100*e.loaded/e.total)+' %'};
x.onload=()=>{msg.textContent=x.responseText};x.onerror=()=>{msg.textContent='Upload failed (connection lost).'};
const s=(sha.value.trim().split(/\s+/)[0]||'').toLowerCase();
if(s&&!/^[0-9a-f]{64}$/.test(s)){msg.textContent='That does not look like a sha256: 64 hex characters.';return}
x.open('POST','/update?code='+encodeURIComponent(code.value)+(s?'&sha='+s:''));x.send(fd);go.disabled=true}
</script></body></html>)HTML";

// The code lives on the panel's screen; it is also printed here, because a USB cable already
// gives this console `token` and `erase-all` — and it is how you read the code screen-off.
uint32_t newCode() {
    g_code = 100000 + esp_random() % 900000;
    g_codeAt = millis();
    Serial.printf("[web] code %lu, good for %lu minutes\n", (unsigned long)g_code,
                  (unsigned long)(CODE_VALID_MS / 60000));
    return g_code;
}

bool codeValid(const String &typed) {
    return g_code && millis() - g_codeAt < CODE_VALID_MS && typed.toInt() == (long)g_code;
}

// A browser that typed the code once gets a cookie, so changing a setting later asks nothing.
// The cookie is per panel (the token lives in that panel's NVS) and dies with `forget-wifi`,
// `erase-all` or the Forget button on the page. Firmware upload always asks for a fresh code.
// The SHA-256 of what the browser sent, so the person can compare it with the `.sha256` beside
// the firmware on the release page — and have the panel refuse it if they paste that digest in.
// The image's own integrity (truncation, corruption) is caught by the OTA layer regardless;
// this answers the other question, which is whether it is the file they meant to install.
mbedtls_sha256_context g_upSha;
bool g_upShaOn = false;
size_t g_upBytes = 0;          // what actually arrived, on either write path
size_t g_upTotal = 0;          // what the browser announced, for the progress screen
String g_upWant, g_upGot;

void shaStart() {
    if (g_upShaOn) mbedtls_sha256_free(&g_upSha);
    mbedtls_sha256_init(&g_upSha);
    mbedtls_sha256_starts(&g_upSha, 0);
    g_upShaOn = true;
    g_upBytes = 0;
    g_upGot = "";
}

String shaFinish() {
    if (!g_upShaOn) return String();
    uint8_t out[32];
    mbedtls_sha256_finish(&g_upSha, out);
    mbedtls_sha256_free(&g_upSha);
    g_upShaOn = false;
    char hex[65];
    for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", out[i]);
    return String(hex);
}

bool g_trustedNow = false;      // set during the request that typed a correct code

bool trustedBrowser() {
    if (g_trustedNow) return true;
    String cookies = g_server->header("Cookie");
    if (!cookies.length()) return false;
    String want = "tftpanel=" + settings::webToken();
    for (int at = 0; at >= 0; ) {
        int end = cookies.indexOf(';', at);
        String one = end < 0 ? cookies.substring(at) : cookies.substring(at, end);
        one.trim();
        if (one == want) return true;
        at = end < 0 ? -1 : end + 1;
    }
    return false;
}

void trustThisBrowser() {
    g_trustedNow = true;        // so the page we are about to send already hides the code field
    // Path=/ so /update sees it too (it still asks for a code), a month before typing again.
    g_server->sendHeader("Set-Cookie", "tftpanel=" + settings::webToken() +
                                           "; Max-Age=2592000; Path=/; SameSite=Lax; HttpOnly");
}

// Download-first boards (board_pins.h) collect the upload in PSRAM and write it once complete:
// flash writes during the transfer would stall their Wi-Fi link.
uint8_t *g_image = nullptr;
size_t g_imageLen = 0, g_imageCap = 0;

void dropImage() {
    if (g_image) heap_caps_free(g_image);
    g_image = nullptr;
    g_imageLen = g_imageCap = 0;
}

// Every refusal goes through here: the screen is showing "Updating..." by then, and a path that
// returns without saying so leaves the panel on that picture for ever. Which is what the checksum
// branch did, the first time it ran.
// `why` answers the browser, which can hold a paragraph; `onScreen` is what the panel says, and
// it is short on purpose - two full sha256 across a screen in a living room is not a message,
// it is a wall of hex. The browser already has the digests.
void refuse(const String &why, const String &onScreen = String()) {
    g_uploadAllowed = false;
    g_uploadError = why;
    ota::uploadFailed(onScreen.length() ? onScreen : why);
}

void handleUpload() {
    HTTPUpload &up = g_server->upload();
    if (up.status == UPLOAD_FILE_START) {
        g_uploadOk = false;
        g_uploadError = "";
        dropImage();
        g_uploadAllowed = codeValid(g_server->arg("code"));
        if (!g_uploadAllowed) {
            // Burn it. The settings page draws a new code on every refusal, so guessing there is
            // one chance in 900,000 each time; without this, an upload could be retried against
            // the same six digits for the whole five minutes. The message already says to reopen
            // the page, which is what now gets a fresh code.
            g_code = 0;
            g_uploadError = "Wrong or expired code: open this page again to get a new one on the panel.";
            return;
        }
        Serial.printf("[web] firmware upload started: %s\n", up.filename.c_str());
        // The code has done its work: clearing it takes the six digits off the panel's screen
        // (ui::tickCode) and makes them unusable a second time, and the update screen takes over.
        g_code = 0;
        // The whole multipart body, a few hundred bytes more than the firmware; close enough
        // for a bar, and 0 when the client announced nothing (then the words speak alone).
        g_upTotal = g_server->clientContentLength() > 0 ? (size_t)g_server->clientContentLength() : 0;
        ota::uploadStarted();
        shaStart();
        g_upWant = g_server->arg("sha");
        g_upWant.trim();
        g_upWant.toLowerCase();
        if (board::OTA_DOWNLOAD_FIRST) {
            const esp_partition_t *slot = esp_ota_get_next_update_partition(nullptr);
            g_imageCap = slot ? slot->size : 0;
            g_image = g_imageCap ? (uint8_t *)heap_caps_malloc(g_imageCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : nullptr;
            if (!g_image) refuse("Not enough memory to receive the firmware.");
        } else if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
            refuse(String("Cannot start the update: ") + Update.errorString());
        }
    } else if (up.status == UPLOAD_FILE_WRITE && g_uploadAllowed) {
        if (g_upShaOn) {
            mbedtls_sha256_update(&g_upSha, up.buf, up.currentSize);
            g_upBytes += up.currentSize;
            ota::uploadProgress(g_upBytes, g_upTotal);
        }
        if (g_image) {
            if (g_imageLen + up.currentSize > g_imageCap) {
                refuse("File too large for a firmware slot.");
                dropImage();
            } else {
                memcpy(g_image + g_imageLen, up.buf, up.currentSize);
                g_imageLen += up.currentSize;
            }
        } else if (Update.write(up.buf, up.currentSize) != up.currentSize) {
            refuse(String("Write failed: ") + Update.errorString());
            Update.abort();
        }
    } else if (up.status == UPLOAD_FILE_END && g_uploadAllowed) {
        g_upGot = shaFinish();
        Serial.printf("[web] uploaded %u bytes, sha256 %s\n", (unsigned)g_upBytes, g_upGot.c_str());
        if (g_upWant.length() && g_upWant != g_upGot) {
            // Refused before a single byte is committed: a digest that does not match is either
            // the wrong file or a damaged transfer, and neither should reach the flash.
            refuse("Checksum does not match: nothing installed. The panel received sha256 " +
                       g_upGot + ", you asked for " + g_upWant + ".",
                   "That file is not the one you asked for: nothing installed.");
            if (g_image) dropImage();
            else Update.abort();
            return;
        }
        ota::uploadInstalling();
        if (g_image) {
            String err;
            if (ota::writeImage(g_image, g_imageLen, err, ota::uploadWriting)) g_uploadOk = true;
            else refuse("Not a valid firmware image: " + err);
            dropImage();
        } else if (Update.end(true)) {
            g_uploadOk = true;
        } else {
            refuse(String("Not a valid firmware image: ") + Update.errorString());
        }
    } else if (up.status == UPLOAD_FILE_ABORTED && g_uploadAllowed) {
        shaFinish();
        if (g_image) dropImage();
        else Update.abort();
        refuse("Upload interrupted, nothing installed.");
    }
}

void handleUploadDone() {
    if (g_uploadOk) {
        g_code = 0;
        g_server->send(200, "text/plain",
                       "Installed (sha256 " + g_upGot + "). The panel restarts now; it goes back "
                       "to the previous version by itself if this one does not start properly.");
        Serial.println("[web] firmware uploaded and verified, restarting");
        delay(1000);
        board::restart();
    }
    g_server->send(g_uploadError.startsWith("Wrong") ? 403 : 400, "text/plain",
                   g_uploadError.length() ? g_uploadError : String("No firmware received."));
    Serial.printf("[web] upload refused: %s\n", g_uploadError.c_str());
}

String esc(const String &in) {
    String o;
    o.reserve(in.length() + 8);
    for (unsigned i = 0; i < in.length(); i++) {
        char c = in[i];
        if (c == '&') o += "&amp;";
        else if (c == '<') o += "&lt;";
        else if (c == '>') o += "&gt;";
        else if (c == '"') o += "&quot;";
        else o += c;
    }
    return o;
}

// Everything that has to be typed lives here; the choices that do not are on the panel itself.
// Saving needs the code on its screen, so being on the network is not enough, and so the code
// also tells you which panel you have open.
void sendSettings(const String &message, bool error = false, bool keepTyped = false) {
    bool trusted = trustedBrowser();
    if (trusted) g_code = 0;        // nothing to type, so nothing on the panel's screen
    else newCode();
    settings::Settings st = settings::get();
    // On a refusal the page comes back with what was typed, not with what is stored.
    auto val = [&](const char *name, const String &stored) {
        return keepTyped ? g_server->arg(name) : stored;
    };
    String h = FPSTR(SETTINGS_PAGE);
    h.replace("%NAME%", esc(net::displayName()));
    // Escaped part by part: the separator is markup, not text.
    h.replace("%WHO%", esc(board::BOARD_NAME) + " &middot; " + esc(net::macAddress()) + " &middot; " +
                           esc(WiFi.localIP().toString()) + " &middot; " + esc(FW_VERSION));
    String box = message.length()
                     ? "<p class=" + String(error ? "bad" : "msg") + ">" + esc(message) + "</p>"
                     : String();
    h.replace("%MSG%", box);
    // A refusal is repeated beside the button: the top of the form is scrolled away by then.
    h.replace("%BMSG%", error ? box : String());
    h.replace("%CODEROW%",
              trusted ? String("<p class=found>This browser is remembered, so no code is needed.</p>")
                      : String("<label>Code shown on this panel's screen<input name=code "
                               "inputmode=numeric maxlength=6 required autocomplete=off "
                               "placeholder=\"6 digits\"></label>"));
    h.replace("%FORGET%", trusted ? String("<form class=forget method=post action=/forget>"
                                           "<button>Forget this browser</button></form>")
                                  : String());
    bool direct = (keepTyped ? g_server->arg("source") == "direct"
                             : st.source == settings::SourceKind::Direct);
    h.replace("%COMPANION%", esc(val("companion", st.companionUrl)));
    h.replace("%CSEL%", direct ? "" : " selected");
    h.replace("%DSEL%", direct ? " selected" : "");
    h.replace("%LYRICS%", esc(val("lyrics", st.lyricsUrl)));
    // Lyrics are why this panel exists, so the page says what it is actually asking - and warns
    // when the answer is "nobody", which otherwise only showed as a screen with no words on it.
    String lnote;
    if (st.source == settings::SourceKind::Companion)
        lnote = "<p class=found>In companion mode the companion supplies the lyrics; "
                "this setting is used in direct mode.</p>";
    else if (st.lyricsUrl == "none") lnote = "<p class=found>Lyrics are turned off.</p>";
    else if (st.lyricsUrl.length()) lnote = "<p class=found>This panel asks " + esc(st.lyricsUrl) + ".</p>";
    else
        switch (net::lyricsrc::lastCompanionSearch()) {
            case net::lyricsrc::CompanionSearch::Found:
                lnote = "<p class=found>A companion was found on your network.</p>"; break;
            case net::lyricsrc::CompanionSearch::NotFound:
                lnote = "<p class=warn>No companion is announcing itself on your network and no "
                        "address is set, so this panel has no lyrics to show. Run the "
                        "TextFromTrack companion on a computer here, or type the address of an "
                        "LRCLIB-compatible server above.</p>"; break;
            case net::lyricsrc::CompanionSearch::Unknown:
                lnote = "<p class=found>Empty: this panel looks for a companion on your network "
                        "when a track starts.</p>"; break;
        }
    h.replace("%LNOTE%", lnote);
    h.replace("%WEATHER%", esc(val("weather", st.weatherPlace)));
    net::weather::Now w = net::snapshot().weather;
    String note;
    if (w.problem.length()) note = "<p class=warn>" + esc(w.problem) + "</p>";
    else if (w.located.length()) note = "<p class=found>Showing " + esc(w.located) + "</p>";
    h.replace("%WNOTE%", note);
    h.replace("%NEWS%", esc(val("news", st.newsUrl)));
    String nnote;
    net::Snapshot snap = net::snapshot();   // a copy, like the weather note above
    if (snap.newsProblem.length()) nnote = "<p class=warn>" + esc(snap.newsProblem) + "</p>";
    else if (snap.news.length()) nnote = "<p class=found>Showing " + esc(snap.news.substring(0, 60)) + "...</p>";
    h.replace("%NNOTE%", nnote);
    g_server->send(200, "text/html; charset=utf-8", h);
}

void handleSettingsSave() {
    g_trustedNow = false;
    bool trusted = trustedBrowser();
    if (!trusted) {
        if (!codeValid(g_server->arg("code"))) {
            sendSettings("That code is wrong or has expired. Nothing was saved. The panel's screen "
                         "shows a new code; what you typed is still here.",
                         true, true);
            return;
        }
        trustThisBrowser();         // typed once, not asked again on this browser
    }
    // A field the request does not carry is left alone: a partial form must not blank a setting.
    settings::Settings was = settings::get();
    auto arg = [](const char *name, const String &unsent = String()) {
        if (!g_server->hasArg(name)) return unsent;
        String v = g_server->arg(name);
        v.trim();
        return v;
    };
    String companion = arg("companion", was.companionUrl), lyrics = arg("lyrics", was.lyricsUrl),
           weather = arg("weather", was.weatherPlace), news = arg("news", was.newsUrl);
    if (companion.length() && !companion.startsWith("http://")) {
        sendSettings("Nothing was saved. The companion address starts with http://", true, true);
        return;
    }
    if (lyrics.length() && lyrics != "none" && !lyrics.startsWith("http")) {
        sendSettings("Nothing was saved. The lyrics server starts with http:// or https://, or is the word none.", true, true);
        return;
    }
    if (news.length() && !news.startsWith("http")) {
        sendSettings("Nothing was saved. The news feed starts with http:// or https://", true, true);
        return;
    }
    while (companion.endsWith("/")) companion.remove(companion.length() - 1);
    while (lyrics.endsWith("/")) lyrics.remove(lyrics.length() - 1);

    settings::SourceKind wanted = !g_server->hasArg("source") ? was.source
                                  : arg("source") == "direct" ? settings::SourceKind::Direct
                                                              : settings::SourceKind::Companion;
    bool restart = wanted != settings::source() || companion != was.companionUrl;
    // The answer names what moved, so a typo in a field you did not mean to touch shows up.
    String changed;
    auto note = [&changed](const char *what) {
        if (changed.length()) changed += ", ";
        changed += what;
    };
    if (wanted != was.source) note("music source");
    if (companion != was.companionUrl) note("companion address");
    if (arg("token").length()) note("companion token");
    if (lyrics != was.lyricsUrl) note("lyrics");
    if (arg("lyricstoken").length()) note("lyrics token");
    if (weather != was.weatherPlace) note("weather town");
    if (news != was.newsUrl) note("news feed");
    settings::setCompanion(companion);
    if (arg("token").length()) settings::setToken(arg("token"));
    settings::setSource(wanted);
    settings::setLyricsUrl(lyrics);
    if (arg("lyricstoken").length()) settings::setLyricsToken(arg("lyricstoken"));
    settings::setWeatherPlace(weather);
    settings::setNewsUrl(news);
    net::lyricsrc::forgetDiscovered();
    net::weather::reset();
    net::news::reset();
    g_code = 0;

    Serial.println("[web] settings saved from the web page");
    if (restart) {
        g_server->send(200, "text/html; charset=utf-8",
                       String("<!doctype html><meta charset=utf-8><body style=\"font:17px system-ui;background:#15161a;"
                              "color:#f6f4ee;max-width:560px;margin:32px auto;padding:0 16px\">") +
                           "<h1>Saved</h1><p>" + esc(changed) +
                           ".</p><p>The panel restarts to use its new source.</p>");
        delay(300);
        board::restart();
        return;
    }
    sendSettings(changed.length() ? "Saved: " + changed + "." : "Nothing to change; all the fields "
                                                                 "already said that.");
}

void handleForget() {
    g_trustedNow = false;
    if (!trustedBrowser() && !codeValid(g_server->arg("code"))) {
        sendSettings("Nothing was forgotten: this browser was not remembered.", true, false);
        return;
    }
    settings::forgetWebTokens();    // every remembered browser, not only this one
    g_trustedNow = false;
    g_server->sendHeader("Set-Cookie", "tftpanel=; Max-Age=0; Path=/");
    Serial.println("[web] remembered browsers forgotten");
    sendSettings("Forgotten. The next change asks for the code on the panel's screen.");
}

void start() {
    g_server = new WebServer(80);
    static const char *kHeaders[] = {"Cookie"};   // trustedBrowser() reads it
    g_server->collectHeaders(kHeaders, 1);
    g_server->on("/", HTTP_GET, []() { g_trustedNow = false; sendSettings(""); });
    g_server->on("/settings", HTTP_POST, handleSettingsSave);
    g_server->on("/forget", HTTP_POST, handleForget);
    g_server->on("/update", HTTP_GET, []() {
        newCode();                                 // shown on the panel, typed in the browser
        String page = FPSTR(PAGE);
        // The same name the panel now prints under its code, so the right six digits go to the
        // right page; the board and MAC say which image this one takes.
        page.replace("%NAME%", esc(net::displayName()));
        page.replace("%WHO%", esc(board::BOARD_NAME) + " &middot; " + esc(net::macAddress()));
        page.replace("%VERSION%", FW_VERSION);
        page.replace("%ASSET%", board::BOARD_ASSET);
        g_server->send(200, "text/html", page);
    });
    g_server->on("/update", HTTP_POST, handleUploadDone, handleUpload);
    g_server->on("/screenshot", HTTP_GET, []() {
        const uint16_t *fb = board::frameBuffer();
        if (!g_shots || !fb) {
            g_server->send(403, "text/plain", "Screenshots are off (console: shots on).");
            return;
        }
        const uint16_t w = board::LCD_H_RES, h = board::LCD_V_RES;
        const size_t bytes = (size_t)w * h * 2;
        uint8_t head[8] = {'T', 'F', 'T', 'S', (uint8_t)w, (uint8_t)(w >> 8), (uint8_t)h, (uint8_t)(h >> 8)};
        g_server->client().setNoDelay(true);
        g_server->setContentLength(sizeof head + bytes);
        g_server->send(200, "application/octet-stream", "");
        g_server->sendContent((const char *)head, sizeof head);
        for (size_t off = 0; off < bytes; off += 32768)
            g_server->sendContent((const char *)fb + off, std::min((size_t)32768, bytes - off));
    });
    g_server->begin();
    // The service record belongs to the net task, which owns mDNS and attaches the panel's name,
    // board, MAC and version to it (internal::startMdns). Announcing it from here was a second,
    // bare registration that ran before mDNS was up and so failed every boot: "addService():
    // Failed adding service http.tcp", printed and ignored. The panel was announced anyway, by
    // the net task a moment later - which is exactly why an error nobody reads survives for ever.
    Serial.println("[web] update page on port 80 (/update)");
}

void stop() {
    g_server->stop();
    delete g_server;
    g_server = nullptr;
    g_code = 0;
}

}  // namespace

namespace {
void loop(bool wifiUp, bool portalActive) {
    bool want = wifiUp && !portalActive;   // the portal owns port 80 while it runs
    if (want && !g_server) start();
    else if (!want && g_server) stop();
    if (g_server) g_server->handleClient();
    if (g_code && millis() - g_codeAt >= CODE_VALID_MS) g_code = 0;
}
}  // namespace

void begin() {
    xTaskCreatePinnedToCore([](void *) {
        for (;;) {
            loop(WiFi.status() == WL_CONNECTED && WiFi.getMode() != WIFI_AP, portal::active());
            delay(2);
        }
    }, "web", 6144, nullptr, 1, nullptr, 0);
}

uint32_t activeCode() { return g_code; }
bool serving() { return g_server != nullptr; }
void enableScreenshots(bool on) { g_shots = on; }
void cancelCode() { g_code = 0; }

}  // namespace webupdate
