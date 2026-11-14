// Talking to the music server itself, with no companion in between (docs/ARCHITECTURE.md).
//
// Where the companion polls, this subscribes: the server pushes the zones and the playing
// position once a second, so the position is known more precisely and the network carries a
// twentieth of the traffic. What it cannot do is lyrics, which are the companion's whole job.
#pragma once

#if defined(SOURCE_ROON)

#include <tftcore/client.h>

#include "source.h"

namespace net {

class DirectSource : public Source {
  public:
    const char *name() const override { return "direct"; }
    // No lyrics at all: nothing here reads the music files or resolves anything.
    Capabilities caps() const override { return {false, false}; }

    bool ready() const override;
    bool connect(uint32_t now) override;
    void reset() override;
    void pump() override;

    // Nothing is polled: the server pushes, and pump() publishes what arrives.
    bool zonesDue(uint32_t) const override { return false; }
    bool refreshZones() override { return true; }
    bool nowPlayingDue(uint32_t) const override { return false; }
    void pollNowPlaying() override {}
    void zoneSelectionChanged() override;

    void command(const Command &c) override;
    bool fetchLyrics(const String &) override { return false; }

    internal::HttpResult browse(JsonDocument &body, JsonDocument &resp) override;
    internal::HttpResult browseLoad(JsonDocument &body, JsonDocument &resp) override;
    String imageUrl(const String &imageKey, int px) const override;

  private:
    void begin();
    void publishEverything(const tftcore::ZoneList &zones, const tftcore::ZoneDelta &delta);
    internal::HttpResult call(bool load, JsonDocument &body, JsonDocument &resp);

    static void onZones(void *ctx, const tftcore::ZoneList &zones, const tftcore::ZoneDelta &delta);
    static void onToken(void *ctx, const char *serverId, const char *token);
    static void onState(void *ctx, tftcore::State state, const char *detail);

    bool started_ = false;
    bool healthy_ = false;      // the first full zone list confirms a freshly installed image
    String trackKey_;           // to spot a track change before publishing
};

}  // namespace net

#endif  // SOURCE_ROON
