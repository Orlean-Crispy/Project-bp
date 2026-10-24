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
    float yaw = 0.0f;
    // Protocol v6: 16 bits, not 8. bit0 on_ground, bit1 crouched, bit2 active,
    // bit3 jump_was_down, bit4 slide_was_down, bit5 dash_was_down,
    // bit6 vault_was_down; bit7 firing, bit8 reloading, bit9 ads (M6 inputs);
    // bit10 alive is still reserved for M6.9. The four *_was_down bits are input
    // edges from the previous tick: without them a rollback replays a held
    // slide/dash/vault as a fresh press. firing/reloading/ads are held states for
    // the same reason: a lost snapshot must not read as "released".
    uint16_t flags = 0;
    uint8_t impulses = 0;
    uint8_t state = 0;
    uint16_t state_ticks = 0;
    uint16_t dash_cooldown = 0;
    uint16_t slow_ticks = 0;  // remaining post-hit movement penalty (6.10)
    // Both feed the next tick: recharge_progress decides when the next impulse
    // comes back, speed_coefficient scales max speed. Leaving them out of the
    // snapshot made a rollback replay at a different speed than the server.
    uint8_t recharge = 0;
    float speed_coefficient = 1.0f;
    // R9: these survive a tick and steer the next one (wall run direction,
    // scripted vault), so a reconciliation that cannot see them would accept a
    // divergent prediction. Sent as their own delta field group.
    Vec3 wall_normal;
    Vec3 vault_dir;
    float vault_rise = 0.0f;
    // Protocol v6 / kFieldVitals: hit points are server-authoritative. The client
    // displays them but never predicts damage (v0.4 A4), so they ride in the
    // snapshot and in the rollback state, not in the client's prediction inputs.
    uint16_t hp = 100;
    uint16_t armor = 0;
    // Protocol v6 / kFieldWeapon: the wire format for M6 is fixed here so that
    // adding a weapon cannot force another protocol bump. All three stay zero
    // until M6 fills them, and a zero group costs one mask bit per snapshot.
    uint8_t weapon_id = 0;
    uint16_t ammo = 0;
    uint16_t fire_cooldown = 0;
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

}  // namespace sim
