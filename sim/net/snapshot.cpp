#include "sim/net/snapshot.h"

#include "sim/net/bytes.h"

namespace sim {
namespace {

enum FieldMask : uint8_t {
    kFieldPos = 1u << 0,
    kFieldVel = 1u << 1,
    kFieldStatus = 1u << 2,   // flags, impulses, state
    kFieldTimers = 1u << 3,   // state_ticks, dash_cooldown
    kFieldYaw = 1u << 4,      // view yaw (remote orientation)
    kFieldAll = 0x1F,
};

bool same_pos(const PlayerSnap& a, const PlayerSnap& b) { return a.pos == b.pos; }
bool same_vel(const PlayerSnap& a, const PlayerSnap& b) { return a.vel == b.vel; }
bool same_status(const PlayerSnap& a, const PlayerSnap& b) {
    return a.flags == b.flags && a.impulses == b.impulses && a.state == b.state;
}
bool same_timers(const PlayerSnap& a, const PlayerSnap& b) {
    return a.state_ticks == b.state_ticks && a.dash_cooldown == b.dash_cooldown;
}
bool same_yaw(const PlayerSnap& a, const PlayerSnap& b) { return a.yaw == b.yaw; }

}  // namespace

PlayerSnap to_snap(const PlayerState& p) {
    PlayerSnap s;
    s.pos = p.pos;
    s.vel = p.vel;
    s.yaw = p.yaw;
    s.flags = static_cast<uint8_t>((p.on_ground ? 1 : 0) | (p.crouched ? 2 : 0));
    s.impulses = static_cast<uint8_t>(p.impulses);
    s.state = static_cast<uint8_t>(p.state);
    s.state_ticks = static_cast<uint16_t>(p.state_ticks);
    s.dash_cooldown = static_cast<uint16_t>(p.dash_cooldown);
    return s;
}

void from_snap(const PlayerSnap& s, PlayerState& p) {
    p.pos = s.pos;
    p.vel = s.vel;
    p.yaw = s.yaw;
    p.on_ground = (s.flags & 1) != 0;
    p.crouched = (s.flags & 2) != 0;
    p.impulses = s.impulses;
    p.state = static_cast<ParkourState>(s.state);
    p.state_ticks = s.state_ticks;
    p.dash_cooldown = s.dash_cooldown;
}

Snapshot make_full_baseline(const Snapshot& snap) {
    Snapshot copy = snap;
    copy.baseline_tick = 0;
    return copy;
}

std::vector<uint8_t> encode_snapshot(const Snapshot& current, const Snapshot* baseline) {
    const bool use_delta = baseline != nullptr && baseline->players.size() == current.players.size();

    ByteWriter w;
    w.u32(current.tick);
    w.u32(current.acked_cmd);
    w.u32(use_delta ? baseline->tick : 0);
    w.u8(static_cast<uint8_t>(current.players.size()));

    for (size_t i = 0; i < current.players.size(); ++i) {
        const PlayerSnap& now = current.players[i];
        uint8_t mask = kFieldAll;
        if (use_delta) {
            const PlayerSnap& before = baseline->players[i];
            mask = 0;
            if (!same_pos(now, before)) mask |= kFieldPos;
            if (!same_vel(now, before)) mask |= kFieldVel;
            if (!same_status(now, before)) mask |= kFieldStatus;
            if (!same_timers(now, before)) mask |= kFieldTimers;
            if (!same_yaw(now, before)) mask |= kFieldYaw;
        }
        w.u8(mask);
        if (mask & kFieldPos) w.vec3(now.pos);
        if (mask & kFieldVel) w.vec3(now.vel);
        if (mask & kFieldStatus) {
            w.u8(now.flags);
            w.u8(now.impulses);
            w.u8(now.state);
        }
        if (mask & kFieldTimers) {
            w.u16(now.state_ticks);
            w.u16(now.dash_cooldown);
        }
        if (mask & kFieldYaw) w.f32(now.yaw);
    }
    return w.data();
}

uint32_t peek_baseline_tick(const uint8_t* data, size_t size) {
    if (size < 12) return 0;
    ByteReader r(data, size);
    r.u32();  // tick
    r.u32();  // acked_cmd
    return r.u32();
}

bool decode_snapshot(const uint8_t* data, size_t size, const Snapshot* baseline, Snapshot& out) {
    ByteReader r(data, size);
    Snapshot s;
    s.tick = r.u32();
    s.acked_cmd = r.u32();
    s.baseline_tick = r.u32();
    const uint8_t count = r.u8();
    if (!r.ok()) return false;

    const bool have_baseline = s.baseline_tick != 0 && baseline != nullptr &&
                               baseline->players.size() == count && baseline->tick == s.baseline_tick;
    // A delta we cannot resolve is not "mostly right": the missing fields would
    // silently decode as zeros. Drop it and let the server fall back to a full
    // snapshot after the missing acks.
    if (s.baseline_tick != 0 && !have_baseline) return false;

    s.players.resize(count);
    for (size_t i = 0; i < count; ++i) {
        PlayerSnap& p = s.players[i];
        if (have_baseline) p = baseline->players[i];

        const uint8_t mask = r.u8();
        if (mask & kFieldPos) p.pos = r.vec3();
        if (mask & kFieldVel) p.vel = r.vec3();
        if (mask & kFieldStatus) {
            p.flags = r.u8();
            p.impulses = r.u8();
            p.state = r.u8();
        }
        if (mask & kFieldTimers) {
            p.state_ticks = r.u16();
            p.dash_cooldown = r.u16();
        }
        if (mask & kFieldYaw) p.yaw = r.f32();
        if (!r.ok()) return false;
    }

    out = s;
    return true;
}

}  // namespace sim
