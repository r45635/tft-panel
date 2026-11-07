#include "showcase.h"

#include <esp_heap_caps.h>

#include <cmath>

#include "cover.h"

namespace showcase {
namespace {

// Original lyrics written for the showcase (no rights attached), timed every ~4.6 s.
const char *const LINES[] = {
    "City lights are falling slowly, one by one",
    "Every window tells a story of someone",
    "We were driving through the static of the rain",
    "Singing louder than the thunder and the train",
    "Hold on, the night is young and so are we",
    "Neon autumn written on the sky for you and me",
    "Turn the radio up and let the city sing",
    "Every heartbeat is a promise of the spring",
    "Café lights on the corner, déjà vu",
    "Every song I ever loved was about you",
    "And the streets keep the rhythm of our feet",
    "Like a chorus that the morning can't repeat",
    "So we follow every echo down the line",
    "All the colours of the evening turn to gold, and for a moment everything is fine",
    "Neon autumn, neon autumn, stay a while",
    "Paint the windows with the shadow of your smile",
    "When the last train leaves the station after two",
    "I'll be humming every word I wrote for you",
    "Hold on, the night is young and so are we",
    "Neon autumn written on the sky for you and me",
    "Turn the radio up and let the city sing",
    "Every heartbeat is a promise of the spring",
    "City lights are fading slowly, one by one",
    "And the night becomes the morning, and we're done",
};
constexpr int LINE_COUNT = sizeof LINES / sizeof LINES[0];
constexpr int32_t FIRST_LINE_MS = 12000, LINE_STEP_MS = 4600;

std::shared_ptr<const lyrics::Lyrics> makeLyrics() {
    auto l = std::make_shared<lyrics::Lyrics>();
    l->synced = true;
    for (int i = 0; i < LINE_COUNT; i++) l->lines.push_back({FIRST_LINE_MS + i * LINE_STEP_MS, LINES[i]});
    return l;
}

float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

// A retro sunset: violet to orange sky, a striped sun with a soft glow, a dark horizon.
void paintArt(uint8_t *rgb, int n) {
    const float cx = n * 0.5f, cy = n * 0.58f, r = n * 0.27f;
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) {
            float t = (float)y / n;
            float R = 28 + (242 - 28) * t, G = 14 + (96 - 14) * t * t, B = 58 + (70 - 58) * t;
            float dx = x - cx, dy = y - cy, d = sqrtf(dx * dx + dy * dy);
            float glow = clamp01(1.0f - (d - r) / (n * 0.35f)) * 0.55f;
            R += (255 - R) * glow * 0.6f, G += (170 - G) * glow * 0.5f, B += (120 - B) * glow * 0.3f;
            if (d < r) {
                float k = (y - (cy - r)) / (2 * r);   // 0 top .. 1 bottom of the sun
                float sr = 255, sg = 214 - 120 * k, sb = 92 - 40 * k;
                // Horizontal cut-outs, thicker towards the bottom.
                int stripe = (int)((y - cy) / (n * 0.035f));
                bool cut = y > cy && (stripe % 2 == 1) && ((y - cy) / r) > 0.15f;
                if (!cut) R = sr, G = sg, B = sb;
            }
            if (y > n * 0.78f) {   // horizon with a faint grid
                float h = (y - n * 0.78f) / (n * 0.22f);
                R = 24 + 30 * (1 - h), G = 10 + 12 * (1 - h), B = 48 + 30 * (1 - h);
                bool grid = ((int)(y * 1.0f) % 12 == 0) || (abs((int)((x - cx) / (1 + h * 4))) % 22 == 0);
                if (grid) R = 255, G = 92, B = 160;
            }
            uint8_t *p = rgb + ((size_t)y * n + x) * 3;
            p[0] = (uint8_t)R, p[1] = (uint8_t)G, p[2] = (uint8_t)B;
        }
}

cover::Cover g_cover;   // built once (generated art + the normal cover pipeline)

}  // namespace

net::Snapshot make(int32_t positionMs) {
    net::Snapshot s;
    s.phase = net::Phase::Playing;
    s.status = "Playing";
    s.staIp = "192.168.1.42";
    s.companion = "http://textfromtrack.local:3888";
    s.zoneId = "zone-living";
    s.zoneName = "Living Room";
    s.zonePinned = false;
    s.state = "playing";
    s.title = "Neon Autumn";
    s.artist = "The Demo Lights";
    s.album = "Night Drive";
    s.durationMs = 234000;
    s.canPlay = s.canPause = s.canNext = s.canPrevious = true;
    net::OutputInfo o;
    o.id = "out-living";
    o.name = "Living Room";
    o.volumeType = "number";
    o.value = 42;
    o.min = 0;
    o.max = 100;
    o.step = 1;
    s.outputs.push_back(o);
    auto zones = std::make_shared<std::vector<net::ZoneInfo>>();
    zones->push_back({"zone-living", "Living Room", "playing", "Neon Autumn", "The Demo Lights"});
    zones->push_back({"zone-kitchen", "Kitchen", "paused", "Morning Coffee", "Sunday Club"});
    zones->push_back({"zone-office", "Office", "stopped", "", ""});
    zones->push_back({"zone-garden", "Garden", "stopped", "", ""});
    s.zones = zones;
    s.zonesSerial = 1;
    if (!g_cover.big) {
        const int n = 384;
        uint8_t *rgb = (uint8_t *)heap_caps_malloc((size_t)n * n * 3, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (rgb) {
            paintArt(rgb, n);
            String err;
            cover::buildFromRgb(rgb, n, n, net::COVER_THUMB_PX, true, 800, 480, g_cover, err);
            heap_caps_free(rgb);
        }
    }
    s.bigCover = g_cover.big;
    s.thumb = g_cover.thumb;
    s.backdrop = g_cover.backdrop;
    s.coverSerial = 0x5C0;
    s.positionMs = positionMs;
    s.positionAtMs = millis();
    s.positionUncertaintyMs = 36;
    s.lyricsStatus = net::LyricsStatus::Found;
    s.lyricsSerial = 0x5C0;
    s.offsetMs = 0;
    s.lyrics = makeLyrics();
    s.lyricsSource = "showcase";
    return s;
}

void fakePortal(net::Snapshot &s) {
    s.portalActive = true;
    s.phase = net::Phase::Setup;
    s.status = "Wi-Fi not set up: join the panel's network to configure it";
    s.apSsid = "TFT-Panel-A1B2";
    s.apPass = "demo-setup-7";
    s.apIp = "192.168.4.1";
}

}  // namespace showcase
