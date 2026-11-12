// Pieces of the net task shared with its helper modules (library.cpp). Net task only.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "net.h"

namespace net::internal {

struct HttpResult {
    int code = 0;          // HTTP status, or < 0 for a transport error
    String errorCode;      // companion error.code when present
    String message;        // short human-readable reason
};

ArduinoJson::Allocator *psram();   // JSON documents in PSRAM

// Companion call: GET when `body` is null, else POST it as JSON (bearer token when set).
HttpResult request(const String &path, const JsonDocument *body, JsonDocument &doc, uint32_t timeoutMs);

// GETs a binary body into a PSRAM buffer the caller frees (heap_caps_free).
HttpResult getBytes(const String &base, const String &path, uint8_t *&buf, size_t &len, size_t maxLen,
                    uint32_t timeoutMs);

const String &baseUrl();    // companion in use, empty if none yet
String followedZoneId();    // the zone the panel follows, empty if none

void publishLibrary(std::shared_ptr<const LibraryPage> page);   // into the snapshot

}  // namespace net::internal
