// One place for the panel's plain outgoing GETs: lyrics, weather, a news feed.
//
// Not the companion's calls, which carry its own error shape, nor firmware downloads, which
// stream and hash as they go. Just "fetch this and tell me what came back". HTTPS is handled
// with the same certificate roots the update check trusts.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

namespace net::httpget {

struct Result {
    int code = 0;      // HTTP status, or < 0 for a transport error
    String message;    // short reason when something went wrong
    bool ok() const { return code == 200; }
};

// Parses the answer into `doc` (allocated by the caller, in PSRAM).
Result json(const String &url, JsonDocument &doc, const String &bearer = String(), uint32_t timeoutMs = 8000);

// Reads at most `maxBytes` of the answer as text; longer answers are cut, not an error.
Result text(const String &url, String &out, size_t maxBytes, const String &bearer = String(),
            uint32_t timeoutMs = 8000);

}  // namespace net::httpget
