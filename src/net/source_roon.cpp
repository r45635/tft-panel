#include "source_roon.h"

#if defined(SOURCE_ROON)

#include <WiFi.h>
#include <esp_heap_caps.h>
#include <tftcore/arduino_io.h>

#include "../app/cover.h"
#include "settings.h"
#include "version.h"

namespace net {

using namespace internal;

namespace {

// The one client, with its transport. All of it lives on the net task.
tftcore::arduino::TcpStream g_stream;
tftcore::arduino::Udp g_udp;
tftcore::arduino::MillisClock g_clock;
tftcore::arduino::PsramAlloc g_alloc;   // internal RAM is what the Wi-Fi stack needs
tftcore::Client g_client(g_stream, g_udp, g_clock, g_alloc);

// What the user sees in the server's extension list. The suffix is this panel's own, so two
// panels on one server are told apart.
String g_displayName;

// ArduinoJson documents of this file allocate in PSRAM like every other one in net/.
String serialise(JsonDocument &doc) {
    String out;
    serializeJson(doc, out);
    return out;
}

}  // namespace

void DirectSource::begin() {
    if (started_) return;
    started_ = true;

    g_displayName = String("TFT Panel ") + net::hostName().substring(10);   // the xxxx suffix
    static tftcore::ExtensionInfo info;
    static String name = g_displayName;
    info.extensionId = "com.textfromtrack.panel";
    info.displayName = name.c_str();
    info.displayVersion = FW_VERSION;
    info.publisher = "TextFromTrack";
    info.email = "support@textfromtrack.com";
    info.website = "https://r45635.github.io/tft-panel/";

    settings::Settings st = settings::get();
    if (st.serverToken.length()) g_client.setToken(st.serverId.c_str(), st.serverToken.c_str());
    if (st.serverHost.length()) {
        int colon = st.serverHost.lastIndexOf(':');
        String host = colon > 0 ? st.serverHost.substring(0, colon) : st.serverHost;
        uint16_t port = colon > 0 ? (uint16_t)st.serverHost.substring(colon + 1).toInt() : 0;
        g_client.setServer(host.c_str(), port);
    }
    g_client.onZones(onZones, this);
    g_client.onToken(onToken, this);
    g_client.onState(onState, this);
    g_client.begin(info);
}

void DirectSource::reset() {
    g_client.disconnect();
    started_ = false;
    healthy_ = false;
    trackKey_ = "";
    begin();
}

bool DirectSource::ready() const { return g_client.subscribed(); }

bool DirectSource::connect(uint32_t) {
    begin();
    g_client.tick();
    return g_client.subscribed();
}

void DirectSource::pump() { g_client.tick(); }

void DirectSource::zoneSelectionChanged() {
    // The zone list is already here: re-pick from it at once instead of waiting for a push.
    tftcore::ZoneDelta delta;
    delta.listChanged = true;
    publishEverything(g_client.zones(), delta);
}

// ---- What the server pushes ----

void DirectSource::onState(void *, tftcore::State state, const char *detail) {
    switch (state) {
        case tftcore::State::Discovering:
            setPhase(Phase::Discovering, "Looking for the music server on the network...");
            break;
        case tftcore::State::Connecting:
        case tftcore::State::Registering:
            setPhase(Phase::Discovering, String("Connecting to ") + g_client.serverName());
            break;
        case tftcore::State::NeedsApproval:
            setPhase(Phase::CompanionError,
                     "Enable \"" + g_displayName + "\" in the music server's extension settings");
            break;
        case tftcore::State::Paired: {
            char url[80];
            snprintf(url, sizeof url, "http://%s:%u", g_client.serverHost(), (unsigned)g_client.serverHttpPort());
            publishServer(url);
            setPhase(Phase::Idle, String("Paired with ") + g_client.serverName());
            break;
        }
        case tftcore::State::Failed:
            setPhase(Phase::CompanionError, String("Music server: ") + (detail && *detail ? detail : "connection lost"));
            break;
        default:
            break;
    }
}

void DirectSource::onToken(void *, const char *serverId, const char *token) {
    settings::setPairing(serverId, token);
    Serial.printf("[net] paired with %s, pairing saved\n", serverId);
}

void DirectSource::onZones(void *ctx, const tftcore::ZoneList &zones, const tftcore::ZoneDelta &delta) {
    static_cast<DirectSource *>(ctx)->publishEverything(zones, delta);
}

void DirectSource::publishEverything(const tftcore::ZoneList &zones, const tftcore::ZoneDelta &delta) {
    if (delta.full || delta.listChanged) {
        auto list = std::make_shared<std::vector<ZoneInfo>>();
        for (const tftcore::Zone &z : zones.zones())
            list->push_back({z.id.c_str(), z.name.c_str(), z.state.c_str(),
                             z.nowPlaying.present ? z.nowPlaying.title.c_str() : "",
                             z.nowPlaying.present ? z.nowPlaying.artist.c_str() : ""});
        if (!publishZones(list)) {
            setPhase(Phase::Idle, "The music server has no zone");
            return;
        }
        if (!healthy_) {   // Wi-Fi up, paired, and a zone list in hand: this image works
            healthy_ = true;
            markHealthy();
        }
    }

    String followed = followedZoneId();
    const tftcore::Zone *z = zones.find(followed.c_str());
    if (!z) return;
    if (!z->nowPlaying.present) {
        if (trackKey_.length()) trackKey_ = "";
        publishIdle();
        return;
    }

    NowPlaying np;
    np.zoneId = followed;
    np.state = z->state.c_str();
    np.title = z->nowPlaying.title.c_str();
    np.artist = z->nowPlaying.artist.c_str();
    np.album = z->nowPlaying.album.c_str();
    np.imageKey = z->nowPlaying.imageKey.c_str();
    np.durationMs = z->nowPlaying.lengthSec * 1000u;
    np.seekMs = z->nowPlaying.seekSec < 0 ? 0 : z->nowPlaying.seekSec * 1000;
    // The position arrives because it changed: the server pushed it as the second ticked over,
    // so it is that second at this instant, give or take its own timer and the local network.
    // (Polling the companion converges on the same instant, the hard way.)
    np.sampleAtMs = millis();
    np.quantumMs = 0;
    np.slackMs = 60;
    np.canPlay = z->canPlay;
    np.canPause = z->canPause;
    np.canNext = z->canNext;
    np.canPrevious = z->canPrevious;
    for (const tftcore::Output &o : z->outputs) {
        OutputInfo oi;
        oi.id = o.id.c_str();
        oi.name = o.name.c_str();
        if (o.volume.present) {
            oi.volumeType = o.volume.type.c_str();
            oi.value = o.volume.value;
            oi.min = o.volume.min;
            oi.max = o.volume.max;
            oi.step = o.volume.step;
            oi.muted = o.volume.muted;
        }
        np.outputs.push_back(std::move(oi));
    }
    trackKey_ = np.title + "\x1f" + np.artist;
    publishNowPlaying(std::move(np));
}

// ---- User commands ----

void DirectSource::command(const Command &c) {
    String zoneId = followedZoneId();
    std::vector<OutputInfo> outputs = followedOutputs();
    if (!ready() || !zoneId.length()) return;

    auto report = [&](const char *what, const tftcore::Client::Reply &r) {
        if (r.ok) return;
        HttpResult res;
        res.code = -1;
        res.message = *r.name ? r.name : r.error;
        commandFailed(what, res);
    };

    if (c.type == CmdType::Control) {
        tftcore::Client::Reply r = g_client.control(zoneId.c_str(), c.arg);
        if (r.ok) Serial.printf("[net] control %s ok\n", c.arg);
        else report((String("Control ") + c.arg).c_str(), r);
    } else if (c.type == CmdType::VolumeSet) {
        for (const OutputInfo &o : outputs) {
            if (o.volumeType != "number" && o.volumeType != "db") continue;
            report(("Volume " + o.name).c_str(), g_client.changeVolume(o.id.c_str(), "absolute", c.value));
            break;   // the slider follows the first output with an absolute volume
        }
    } else if (c.type == CmdType::Mute) {
        for (const OutputInfo &o : outputs) {
            if (!o.volumeType.length()) continue;
            report((String(c.direction ? "Mute " : "Unmute ") + o.name).c_str(),
                   g_client.mute(o.id.c_str(), c.direction != 0));
        }
    } else if (c.type == CmdType::Offset) {
        // Nothing to persist without the companion: the offset holds for this track only.
        publishOffset((int32_t)c.value);
    } else {
        int sent = 0;
        for (const OutputInfo &o : outputs) {
            if (!o.volumeType.length()) continue;
            report(("Volume " + o.name).c_str(),
                   g_client.changeVolume(o.id.c_str(), "relative_step", (float)c.direction));
            sent++;
        }
        if (!sent) {
            HttpResult r;
            r.code = -1;
            r.message = "this zone has no volume control";
            commandFailed("Volume", r);
        }
    }
}

// ---- Music library ----

HttpResult DirectSource::call(bool load, JsonDocument &body, JsonDocument &resp) {
    HttpResult res;
    if (!ready()) {
        res.code = -1;
        res.message = "not connected to the music server";
        return res;
    }
    String text = serialise(body);
    tftcore::Client::Reply r = load ? g_client.browseLoad(text.c_str(), resp) : g_client.browse(text.c_str(), resp);
    if (r.ok) {
        res.code = 200;
        return res;
    }
    // The library tells a stale item_key apart by the 502 the companion returns for it.
    res.code = *r.name && strcmp(r.name, "InvalidRequest") == 0 ? 502 : -1;
    res.message = *r.name ? r.name : r.error;
    return res;
}

HttpResult DirectSource::browse(JsonDocument &body, JsonDocument &resp) { return call(false, body, resp); }
HttpResult DirectSource::browseLoad(JsonDocument &body, JsonDocument &resp) { return call(true, body, resp); }

String DirectSource::imageUrl(const String &imageKey, int px) const {
    char url[200];
    if (!g_client.imageUrl(imageKey.c_str(), px, px, url, sizeof url)) return String();
    return String(url);
}

}  // namespace net

#endif  // SOURCE_ROON
