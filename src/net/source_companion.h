// The TextFromTrack companion over its LAN HTTP API: the panel's original and recommended
// source. Discovery by mDNS or a fixed address, polling, and the only path to lyrics.
#pragma once

#include "source.h"

namespace net {

class CompanionSource : public Source {
  public:
    const char *name() const override { return "companion"; }
    Capabilities caps() const override { return {true, true}; }

    bool ready() const override { return baseUrl_.length() > 0; }
    bool connect(uint32_t now) override;
    void reset() override;

    bool zonesDue(uint32_t now) const override;
    bool refreshZones() override;
    bool nowPlayingDue(uint32_t now) const override;
    void pollNowPlaying() override;
    void zoneSelectionChanged() override { lastZonesAt_ = 0; }

    void command(const Command &c) override;
    bool fetchLyrics(const String &zoneId) override;

    internal::HttpResult browse(JsonDocument &body, JsonDocument &resp) override;
    internal::HttpResult browseLoad(JsonDocument &body, JsonDocument &resp) override;
    String imageUrl(const String &imageKey, int px) const override;

  private:
    internal::HttpResult request(const String &path, const JsonDocument *body, JsonDocument &doc,
                                 uint32_t timeoutMs);
    internal::HttpResult getJson(const String &path, JsonDocument &doc, uint32_t timeoutMs) {
        return request(path, nullptr, doc, timeoutMs);
    }
    void reportFailure(const char *what, const internal::HttpResult &r);

    String baseUrl_;            // "http://host:port", empty until discovered
    bool fromMdns_ = false;     // a discovered address is dropped after repeated failures
    int consecutiveFails_ = 0;
    uint32_t lastDiscoveryAt_ = 0, lastZonesAt_ = 0, lastNowPlayingAt_ = 0;
};

}  // namespace net
