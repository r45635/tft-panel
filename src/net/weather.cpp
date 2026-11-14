#include "weather.h"

#include "http_get.h"
#include "net_internal.h"
#include "settings.h"

namespace net::weather {

using namespace internal;

namespace {

constexpr uint32_t REFRESH_MS = 20u * 60u * 1000u;
constexpr uint32_t RETRY_MS = 2u * 60u * 1000u;

String g_place;            // the name this fix belongs to, to notice a change
float g_lat = 0, g_lon = 0;
bool g_located = false;
uint32_t g_nextAt = 0;

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

bool locate(const String &place) {
    JsonDocument doc(psram());
    String url = "https://geocoding-api.open-meteo.com/v1/search?count=1&language=en&format=json&name=" +
                 urlEncode(place);
    httpget::Result r = httpget::json(url, doc);
    if (!r.ok()) {
        Serial.printf("[weather] could not look up \"%s\": %s\n", place.c_str(), r.message.c_str());
        return false;
    }
    JsonObjectConst hit = doc["results"][0];
    if (hit.isNull()) {
        Serial.printf("[weather] no place called \"%s\"\n", place.c_str());
        publishWeather({});   // nothing to show rather than yesterday's weather
        return false;
    }
    g_lat = hit["latitude"] | 0.0f;
    g_lon = hit["longitude"] | 0.0f;
    String name = hit["name"] | place.c_str();
    String admin = hit["admin1"] | "";
    String country = hit["country_code"] | "";
    g_place = name;
    if (country.length() && country != "FR" && country != "CA") g_place += ", " + country;
    else if (admin.length() && admin != name) g_place += ", " + admin;
    g_located = true;
    Serial.printf("[weather] %s at %.3f, %.3f\n", g_place.c_str(), g_lat, g_lon);
    return true;
}

bool refresh() {
    JsonDocument doc(psram());
    char url[220];
    snprintf(url, sizeof url,
             "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,weather_code&daily=temperature_2m_max,temperature_2m_min"
             "&forecast_days=1&timezone=auto",
             g_lat, g_lon);
    httpget::Result r = httpget::json(url, doc);
    if (!r.ok()) {
        Serial.printf("[weather] no forecast: %s\n", r.message.c_str());
        return false;
    }
    Now w;
    w.valid = true;
    w.place = g_place;
    w.tempC = (int16_t)lroundf(doc["current"]["temperature_2m"] | 0.0f);
    w.code = (uint8_t)(doc["current"]["weather_code"] | 0);
    w.maxC = (int16_t)lroundf(doc["daily"]["temperature_2m_max"][0] | 0.0f);
    w.minC = (int16_t)lroundf(doc["daily"]["temperature_2m_min"][0] | 0.0f);
    Serial.printf("[weather] %s %d C, %s, today %d to %d\n", w.place.c_str(), (int)w.tempC, describe(w.code),
                  (int)w.minC, (int)w.maxC);
    publishWeather(w);
    return true;
}

}  // namespace

void reset() {
    g_located = false;
    g_place = "";
    g_nextAt = 0;
    publishWeather({});
}

const char *describe(uint8_t c) {
    // WMO 4677, as Open-Meteo uses it, in the words a clock face needs.
    switch (c) {
        case 0: return "Clear";
        case 1: return "Mainly clear";
        case 2: return "Partly cloudy";
        case 3: return "Overcast";
        case 45: case 48: return "Fog";
        case 51: case 53: case 55: return "Drizzle";
        case 56: case 57: return "Freezing drizzle";
        case 61: return "Light rain";
        case 63: return "Rain";
        case 65: return "Heavy rain";
        case 66: case 67: return "Freezing rain";
        case 71: return "Light snow";
        case 73: return "Snow";
        case 75: return "Heavy snow";
        case 77: return "Snow grains";
        case 80: case 81: return "Showers";
        case 82: return "Heavy showers";
        case 85: case 86: return "Snow showers";
        case 95: return "Thunderstorm";
        case 96: case 99: return "Thunderstorm with hail";
        default: return "";
    }
}

void tick(uint32_t now) {
    String place = settings::get().weatherPlace;
    if (!place.length()) {
        if (g_located || g_place.length()) reset();
        return;
    }
    if (g_nextAt && (int32_t)(now - g_nextAt) < 0) return;

    if (!g_located || !g_place.startsWith(place)) {
        if (!locate(place)) {
            g_nextAt = now + RETRY_MS;
            return;
        }
    }
    g_nextAt = now + (refresh() ? REFRESH_MS : RETRY_MS);
}

}  // namespace net::weather
