#pragma once

#include <cstdint>

// AUD-05: remote players used to be sampled at "predicted tick - 1" with
// frac = 0. That tick lives on the client's predicted axis, not on the
// interpolator's snapshot axis, so sample() usually found no frame at all and
// fell back to "nearest" - section 7.1 interpolation was effectively off.
//
// The bridge now samples on the client's snapshot axis:
//
//   sample_tick = newest received snapshot tick - kRemoteDelayTicks
//   frac        = time since that snapshot arrived / one tick, clamped to [0, 1]
//
// One snapshot of delay means both frame N-1 and frame N are buffered, so the
// interpolator really blends two frames. When snapshots stop arriving, frac
// freezes at 1 (the newest frame) instead of extrapolating. The tick is clamped
// into [oldest, newest], so a single buffered frame still yields a legal sample
// and the rule can be asserted from a gate.
namespace bridge {

constexpr uint32_t kRemoteDelayTicks = 1;
constexpr double kTickSeconds = 1.0 / 64.0;

struct RemoteSample {
    uint32_t tick = 0;
    float frac = 0.0f;
};

inline bool remote_sample(uint32_t oldest_tick, uint32_t newest_tick,
                          double seconds_since_newest, RemoteSample& out) {
    if (newest_tick == 0 || newest_tick < oldest_tick) return false;
    uint32_t tick =
        newest_tick > kRemoteDelayTicks ? newest_tick - kRemoteDelayTicks : oldest_tick;
    if (tick < oldest_tick) tick = oldest_tick;
    if (tick > newest_tick) tick = newest_tick;

    double frac = seconds_since_newest / kTickSeconds;
    if (frac < 0.0) frac = 0.0;
    if (frac > 1.0) frac = 1.0;
    out.tick = tick;
    out.frac = static_cast<float>(frac);
    return true;
}

}  // namespace bridge
