#include "sim/game/rewind.h"

#include "sim/core/tick.h"

namespace sim {

float rewind_ticks(float one_way_ms) {
    // !(x > 0) also catches NaN: a bad latency sample must become the floor, not
    // a garbage rewind that lands on an arbitrary frame.
    if (!(one_way_ms > 0.0f)) one_way_ms = 0.0f;
    float ticks = one_way_ms * static_cast<float>(kDefaultTickRate) / 1000.0f + kInterpTicks;
    if (ticks > static_cast<float>(kMaxRewindTicks)) ticks = static_cast<float>(kMaxRewindTicks);
    return ticks;
}

int rewind_frame(const HitboxHistory* histories, int count, uint32_t shot_tick, float rewind,
                 HitboxSample* out, int capacity) {
    if (histories == nullptr || out == nullptr || capacity <= 0) return 0;
    if (count > capacity) count = capacity;

    float target = static_cast<float>(shot_tick) - rewind;
    if (!(target > 0.0f)) target = 0.0f;
    const uint32_t whole = static_cast<uint32_t>(target);
    const float frac = target - static_cast<float>(whole);

    for (int i = 0; i < count; ++i) out[i] = histories[i].sample(whole, frac);
    return count;
}

}  // namespace sim
