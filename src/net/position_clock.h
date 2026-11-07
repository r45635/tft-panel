// Playback position estimator from Roon's whole-second seek positions.
//
// Each sample says: at local time t (ms), the true position p(t) lies in [seek, seek + 1000).
// With p(t) = t + offset, that bounds `offset` to [seek - t, seek + 1000 - t). Intersecting
// those windows over successive samples converges on the moment each second ticks over,
// far tighter than any single sample (the companion web player gets the same precision
// from its push channel; we poll). Each window is widened by the request's half round
// trip, the uncertainty of t. An empty intersection means a seek or a stall: start over.
//
// Header-only and free of Arduino types so it can be unit-tested on the host.
#pragma once

#include <stdint.h>

class PositionClock {
public:
    // seekMs: reported position (whole seconds, in ms); t: local time of the sample
    // (middle of the request); slackMs: half the round trip.
    void observe(int32_t seekMs, uint32_t t, bool playing, int32_t slackMs) {
        if (!playing) {
            valid_ = false;
            pausedAt_ = seekMs;
            return;
        }
        int64_t lo = (int64_t)seekMs - t - slackMs;
        int64_t hi = (int64_t)seekMs + 1000 - t + slackMs;
        if (valid_ && lo < hi_ && hi > lo_) {
            if (lo > lo_) lo_ = lo;
            if (hi < hi_) hi_ = hi;
        } else {
            lo_ = lo;
            hi_ = hi;
            valid_ = true;
        }
    }

    void reset() { valid_ = false; pausedAt_ = 0; }

    // Position at local time t (ms): middle of the current window.
    int32_t positionAt(uint32_t t) const {
        if (!valid_) return pausedAt_;
        return (int32_t)((int64_t)t + (lo_ + hi_) / 2);
    }

    // Width of the uncertainty window (ms), for diagnostics.
    int32_t uncertaintyMs() const { return valid_ ? (int32_t)(hi_ - lo_) : -1; }
    bool running() const { return valid_; }

private:
    bool valid_ = false;
    int64_t lo_ = 0, hi_ = 0;
    int32_t pausedAt_ = 0;
};
