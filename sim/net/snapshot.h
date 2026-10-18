#pragma once

#include <cstdint>
#include <vector>

#include "sim/core/types.h"
#include "sim/move/pmove.h"

namespace sim {

// Compact per-player movement state. This is exactly the data the client needs
// to reconcile its prediction and the server needs to describe the world.
struct PlayerSnap {
    Vec3 pos;
    Vec3 vel;
    uint8_t flags = 0;  // bit0 on_ground, bit1 crouched
    uint8_t impulses = 0;
    uint8_t state = 0;
    uint16_t state_ticks = 0;
    uint16_t dash_cooldown = 0;
};

struct Snapshot {
    uint32_t tick = 0;
    uint32_t acked_cmd = 0;
    uint32_t baseline_tick = 0;  // 0 means "full snapshot"
    std::vector<PlayerSnap> players;
};

PlayerSnap to_snap(const PlayerState& player);
void from_snap(const PlayerSnap& snap, PlayerState& player);

// Delta encoding against the last snapshot the client acknowledged (plan 7.1).
// A field group that did not change costs one bit in the per-player mask.
std::vector<uint8_t> encode_snapshot(const Snapshot& current, const Snapshot* baseline);
bool decode_snapshot(const uint8_t* data, size_t size, const Snapshot* baseline, Snapshot& out);

// Reads just the baseline tick so the receiver can pick the right snapshot to
// decode against (the server's baseline lags behind the client's newest one).
uint32_t peek_baseline_tick(const uint8_t* data, size_t size);

Snapshot make_full_baseline(const Snapshot& snap);

}  // namespace sim
