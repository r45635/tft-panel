#include "http_get.h"

#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>

namespace net::httpget {
namespace {

// Opens the connection and returns the status, leaving `http` ready to read on 200.
int open(HTTPClient &http, NetworkClientSecure &secure, WiFiClient &plain, const String &url, const String &bearer,
         uint32_t timeoutMs, String &message) {
    bool https = url.startsWith("https://");
    if (https) secure.useBuiltinCACertBundle();   // the roots the update check already trusts
    http.setConnectTimeout(3000);
    http.setTimeout(timeoutMs);
    http.setReuse(false);
    http.useHTTP10(true);          // no chunked encoding: read straight from the socket
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (!(https ? http.begin(secure, url) : http.begin(plain, url))) {
        message = "bad address";
        return -100;
    }
    if (bearer.length()) http.addHeader("Authorization", "Bearer " + bearer);
    int code = http.GET();
    if (code < 0) message = HTTPClient::errorToString(code);
    else if (code != 200) message = "HTTP " + String(code);
    return code;
}

}  // namespace

Result json(const String &url, JsonDocument &doc, const String &bearer, uint32_t timeoutMs) {
    Result r;
    NetworkClientSecure secure;
    WiFiClient plain;
    HTTPClient http;
    r.code = open(http, secure, plain, url, bearer, timeoutMs, r.message);
    if (r.code == 200) {
        DeserializationError err = deserializeJson(doc, http.getStream());
        if (err) {
            r.code = -101;
            r.message = String("bad answer (") + err.c_str() + ")";
        }
    }
    if (r.code != -100) http.end();
    return r;
}

Result text(const String &url, String &out, size_t maxBytes, const String &bearer, uint32_t timeoutMs) {
    Result r;
    out = "";
    NetworkClientSecure secure;
    WiFiClient plain;
    HTTPClient http;
    r.code = open(http, secure, plain, url, bearer, timeoutMs, r.message);
    if (r.code == 200) {
        WiFiClient *stream = http.getStreamPtr();
        out.reserve(maxBytes > 4096 ? 4096 : maxBytes);
        uint32_t deadline = millis() + timeoutMs;
        uint8_t buf[512];
        while (out.length() < maxBytes && millis() < deadline && (http.connected() || stream->available())) {
            int n = stream->read(buf, sizeof buf);
            if (n > 0) {
                size_t room = maxBytes - out.length();
                out.concat((const char *)buf, (size_t)n < room ? (size_t)n : room);
            } else {
                delay(2);
            }
        }
    }
    if (r.code != -100) http.end();
    return r;
}

}  // namespace net::httpget
