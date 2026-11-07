#include "web_update.h"

#include <ESPmDNS.h>
#include <Update.h>
#include <WebServer.h>

#include "../board/board.h"
#include "../version.h"

namespace webupdate {
namespace {

constexpr uint32_t CODE_VALID_MS = 5 * 60 * 1000;

WebServer *g_server = nullptr;
volatile uint32_t g_code = 0;
uint32_t g_codeAt = 0;
bool g_uploadAllowed = false, g_uploadOk = false;
String g_uploadError;

const char PAGE[] PROGMEM = R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1"><title>TFT Panel update</title>
<style>body{font:17px/1.5 system-ui,sans-serif;max-width:560px;margin:32px auto;padding:0 16px;background:#0e1116;color:#f2f4f7}
input,button{font:inherit;padding:10px;border-radius:8px;border:1px solid #2a323c;background:#161b22;color:#f2f4f7}
button{background:#1f6f9e;border:0;padding:10px 24px}#msg{margin-top:16px;color:#a4aeb9}</style></head><body>
<h1>Update TFT Panel</h1><p>Running <b>%VERSION%</b>. Choose a firmware file (<code>tft-panel-vX.Y.Z.bin</code>)
and type the code shown on the panel's screen.</p>
<p><input id="code" inputmode="numeric" maxlength="6" placeholder="6-digit code" autocomplete="off"></p>
<p><input id="file" type="file" accept=".bin"></p><p><button id="go">Upload and install</button></p><div id="msg"></div>
<script>
go.onclick=()=>{const f=file.files[0];if(!f||code.value.length!=6){msg.textContent='Pick a file and type the code.';return}
const fd=new FormData();fd.append('firmware',f);const x=new XMLHttpRequest();
x.upload.onprogress=e=>{msg.textContent='Uploading '+Math.round(100*e.loaded/e.total)+' %'};
x.onload=()=>{msg.textContent=x.responseText};x.onerror=()=>{msg.textContent='Upload failed (connection lost).'};
x.open('POST','/update?code='+encodeURIComponent(code.value));x.send(fd);go.disabled=true}
</script></body></html>)HTML";

bool codeValid(const String &typed) {
    return g_code && millis() - g_codeAt < CODE_VALID_MS && typed.toInt() == (long)g_code;
}

void handleUpload() {
    HTTPUpload &up = g_server->upload();
    if (up.status == UPLOAD_FILE_START) {
        g_uploadOk = false;
        g_uploadError = "";
        g_uploadAllowed = codeValid(g_server->arg("code"));
        if (!g_uploadAllowed) {
            g_uploadError = "Wrong or expired code: open this page again to get a new one on the panel.";
            return;
        }
        Serial.printf("[web] firmware upload started: %s\n", up.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
            g_uploadAllowed = false;
            g_uploadError = String("Cannot start the update: ") + Update.errorString();
        }
    } else if (up.status == UPLOAD_FILE_WRITE && g_uploadAllowed) {
        if (Update.write(up.buf, up.currentSize) != up.currentSize) {
            g_uploadAllowed = false;
            g_uploadError = String("Write failed: ") + Update.errorString();
            Update.abort();
        }
    } else if (up.status == UPLOAD_FILE_END && g_uploadAllowed) {
        if (Update.end(true)) g_uploadOk = true;
        else g_uploadError = String("Not a valid firmware image: ") + Update.errorString();
    } else if (up.status == UPLOAD_FILE_ABORTED && g_uploadAllowed) {
        Update.abort();
        g_uploadError = "Upload interrupted, nothing installed.";
    }
}

void handleUploadDone() {
    if (g_uploadOk) {
        g_code = 0;
        g_server->send(200, "text/plain", "Installed. The panel restarts now; it goes back to the previous version "
                                          "by itself if this one does not start properly.");
        Serial.println("[web] firmware uploaded and verified, restarting");
        delay(1000);
        board::restart();
    }
    g_server->send(g_uploadError.startsWith("Wrong") ? 403 : 400, "text/plain",
                   g_uploadError.length() ? g_uploadError : String("No firmware received."));
    Serial.printf("[web] upload refused: %s\n", g_uploadError.c_str());
}

void start() {
    g_server = new WebServer(80);
    g_server->on("/", HTTP_GET, []() {
        g_server->send(200, "text/plain", String("TFT Panel ") + FW_VERSION + "\nFirmware upload: /update\n");
    });
    g_server->on("/update", HTTP_GET, []() {
        g_code = 100000 + esp_random() % 900000;   // shown on the panel, typed in the browser
        g_codeAt = millis();
        String page = FPSTR(PAGE);
        page.replace("%VERSION%", FW_VERSION);
        g_server->send(200, "text/html", page);
    });
    g_server->on("/update", HTTP_POST, handleUploadDone, handleUpload);
    g_server->begin();
    MDNS.addService("http", "tcp", 80);
    Serial.println("[web] update page on port 80 (/update)");
}

void stop() {
    g_server->stop();
    delete g_server;
    g_server = nullptr;
    g_code = 0;
}

}  // namespace

void loop(bool wifiUp, bool portalActive) {
    bool want = wifiUp && !portalActive;   // the portal owns port 80 while it runs
    if (want && !g_server) start();
    else if (!want && g_server) stop();
    if (g_server) g_server->handleClient();
    if (g_code && millis() - g_codeAt >= CODE_VALID_MS) g_code = 0;
}

uint32_t activeCode() { return g_code; }
void cancelCode() { g_code = 0; }

}  // namespace webupdate
