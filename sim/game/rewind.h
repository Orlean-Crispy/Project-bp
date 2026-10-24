#pragma once

#include <cstdint>

#include "sim/game/hitbox.h"

namespace sim {

// Plan v0.4 9.1: the rewind window is a calculation, not an experience value.
// 30 ms RTT + one interpolation tick (15.6 ms) + 5 ms jitter is about 51 ms,
// i.e. 4 ticks at 64 Hz; 6 ticks (93.75 ms) is the safety margin.
constexpr int kMaxRewindTicks = 6;

// The client draws one tick behind its own simulation tick (v0.4 9.2), so even a
// zero-latency shot refers to the previous frame.
constexpr float kInterpTicks = 1.0f;

// How far back one shot is judged: one-way latency plus the interpolation tick,
// clamped to the window above. A negative or NaN latency clamps to the floor.
float rewind_ticks(float one_way_ms);

// One interpolated frame per player, taken "rewind" ticks before shot_tick.
// Entity i is histories[i]; at most capacity entries are written and the count is
// returned. No allocation: the caller owns the buffer. A target older than the
// oldest frame clamps to it instead of extrapolating.
int rewind_frame(const HitboxHistory* histories, int count, uint32_t shot_tick, float rewind,
                 HitboxSample* out, int capacity);

}  // namespace sim
