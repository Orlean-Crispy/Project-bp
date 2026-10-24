#include "sim/net/snapshot.h"

#include <algorithm>

#include "sim/net/bytes.h"

namespace sim {
namespace {

// Protocol v6: a uint16 mask. bits 0..6 are the v5 groups, bits 7 and 8 are the
// combat groups M6 needs, 9..15 stay free. The assertions at the bottom of this
// block turn "the mask is full" into a compile error instead of a mid-milestone
// protocol change (the M5 -> M6 audit's P0-1).
enum FieldMask : uint16_t {
    kFieldPos = 1u << 0,
    kFieldVel = 1u << 1,
    kFieldStatus = 1u << 2,   // flags, impulses, state
    kFieldTimers = 1u << 3,   // state_ticks, dash_cooldown, slow_ticks
    kFieldYaw = 1u << 4,      // view yaw (remote orientation)
    kFieldExtra = 1u << 5,    // wall_normal, vault_dir, vault_rise (R9)
    kFieldCharge = 1u << 6,   // recharge progress, speed_coefficient
    kFieldVitals = 1u << 7,   // hp, armor
    kFieldWeapon = 1u << 8,   // weapon_id, ammo, fire_cooldown (reserved for M6)
    kFieldAll = 0x01FF,
};

// Same split for the flags word. Only bits 0..6 have a producer today; 7..10 are
// reserved by protocol v6 so M6 can set them without changing the wire format.
enum FlagBit : uint16_t {
    kFlagOnGround = 1u << 0,
    kFlagCrouched = 1u << 1,
    kFlagActive = 1u << 2,
    kFlagJumpWasDown = 1u << 3,
    kFlagSlideWasDown = 1u << 4,
    kFlagDashWasDown = 1u << 5,
    kFlagVaultWasDown = 1u << 6,
    kFlagFiring = 1u << 7,
    kFlagReloading = 1u << 8,
    kFlagAds = 1u << 9,
    kFlagAlive = 1u << 10,
    kFlagsAll = 0x07FF,
};

constexpr uint16_t kFieldsUsed = kFieldPos | kFieldVel | kFieldStatus | kFieldTimers |
                                 kFieldYaw | kFieldExtra | kFieldCharge | kFieldVitals |
                                 kFieldWeapon;
constexpr uint16_t kFlagsUsed = kFlagOnGround | kFlagCrouched | kFlagActive | kFlagJumpWasDown |
                                kFlagSlideWasDown | kFlagDashWasDown | kFlagVaultWasDown |
                                kFlagFiring | kFlagReloading | kFlagAds;
static_assert((kFieldsUsed & ~kFieldAll) == 0, "kFieldAll does not cover every field group");
static_assert(kFieldAll != 0xFFFF, "field mask is full: widen it to uint32");
static_assert((kFlagsUsed & ~kFlagsAll) == 0, "kFlagsAll does not cover every used flag bit");
static_assert(kFlagsAll != 0xFFFF, "flags word is full: widen it to uint32");

bool same_pos(const PlayerSnap& a, const PlayerSnap& b) { return a.pos == b.pos; }
bool same_vel(const PlayerSnap& a, const PlayerSnap& b) { return a.vel == b.vel; }
bool same_status(const PlayerSnap& a, const PlayerSnap& b) {
    return a.flags == b.flags && a.impulses == b.impulses && a.state == b.state;
}
bool same_timers(const PlayerSnap& a, const PlayerSnap& b) {
    return a.state_ticks == b.state_ticks && a.dash_cooldown == b.dash_cooldown &&
           a.slow_ticks == b.slow_ticks;
}
bool same_yaw(const PlayerSnap& a, const PlayerSnap& b) { return a.yaw == b.yaw; }
bool same_extra(const PlayerSnap& a, const PlayerSnap& b) {
    return a.wall_normal == b.wall_normal && a.vault_dir == b.vault_dir &&
           a.vault_rise == b.vault_rise;
}
bool same_charge(const PlayerSnap& a, const PlayerSnap& b) {
    return a.recharge == b.recharge && a.speed_coefficient == b.speed_coefficient;
}
bool same_vitals(const PlayerSnap& a, const PlayerSnap& b) {
    return a.hp == b.hp && a.armor == b.armor;
}
bool same_weapon(const PlayerSnap& a, const PlayerSnap& b) {
    return a.weapon_id == b.weapon_id && a.ammo == b.ammo && a.fire_cooldown == b.fire_cooldown;
}

}  // namespace

PlayerSnap to_snap(const PlayerState& p) {
    PlayerSnap s;
    s.pos = p.pos;
    s.vel = p.vel;
    s.yaw = p.yaw;
    s.flags = static_cast<uint16_t>((p.on_ground ? kFlagOnGround : 0) |
                                    (p.crouched ? kFlagCrouched : 0) |
                                    (p.active ? kFlagActive : 0) |
                                    (p.jump_was_down ? kFlagJumpWasDown : 0) |
                                    (p.slide_was_down ? kFlagSlideWasDown : 0) |
                                    (p.dash_was_down ? kFlagDashWasDown : 0) |
                                    (p.vault_was_down ? kFlagVaultWasDown : 0) |
                                    (p.firing ? kFlagFiring : 0) |
                                    (p.reloading ? kFlagReloading : 0) |
                                    (p.ads ? kFlagAds : 0));
    s.impulses = static_cast<uint8_t>(p.impulses);
    s.state = static_cast<uint8_t>(p.state);
    s.state_ticks = static_cast<uint16_t>(p.state_ticks);
    s.dash_cooldown = static_cast<uint16_t>(p.dash_cooldown);
    s.slow_ticks = static_cast<uint16_t>(p.slow_ticks);
    s.recharge = static_cast<uint8_t>(std::min(255, std::max(0, p.recharge_progress)));
    s.speed_coefficient = p.speed_coefficient;
    s.wall_normal = p.wall_normal;
    s.vault_dir = p.vault_dir;
    s.vault_rise = p.vault_rise;
    s.hp = static_cast<uint16_t>(std::min(65535, std::max(0, p.vitals.health)));
    s.armor = static_cast<uint16_t>(std::min(65535, std::max(0, p.vitals.armor)));
    // weapon_id / ammo / fire_cooldown stay zero until M6 gives them a producer.
    return s;
}

void from_snap(const PlayerSnap& s, PlayerState& p) {
    p.pos = s.pos;
    p.vel = s.vel;
    p.yaw = s.yaw;
    p.on_ground = (s.flags & kFlagOnGround) != 0;
    p.crouched = (s.flags & kFlagCrouched) != 0;
    p.active = (s.flags & kFlagActive) != 0;
    p.impulses = s.impulses;
    p.state = static_cast<ParkourState>(s.state);
    p.state_ticks = s.state_ticks;
    p.dash_cooldown = s.dash_cooldown;
    p.slow_ticks = s.slow_ticks;
    p.jump_was_down = (s.flags & kFlagJumpWasDown) != 0;
    p.slide_was_down = (s.flags & kFlagSlideWasDown) != 0;
    p.dash_was_down = (s.flags & kFlagDashWasDown) != 0;
    p.vault_was_down = (s.flags & kFlagVaultWasDown) != 0;
    p.firing = (s.flags & kFlagFiring) != 0;
    p.reloading = (s.flags & kFlagReloading) != 0;
    p.ads = (s.flags & kFlagAds) != 0;
    p.recharge_progress = s.recharge;
    p.speed_coefficient = s.speed_coefficient;
    p.wall_normal = s.wall_normal;
    p.vault_dir = s.vault_dir;
    p.vault_rise = s.vault_rise;
    p.vitals.health = s.hp;
    p.vitals.armor = s.armor;
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
        uint16_t mask = kFieldAll;
        if (use_delta) {
            const PlayerSnap& before = baseline->players[i];
            mask = 0;
            if (!same_pos(now, before)) mask |= kFieldPos;
            if (!same_vel(now, before)) mask |= kFieldVel;
            if (!same_status(now, before)) mask |= kFieldStatus;
            if (!same_timers(now, before)) mask |= kFieldTimers;
            if (!same_yaw(now, before)) mask |= kFieldYaw;
            if (!same_extra(now, before)) mask |= kFieldExtra;
            if (!same_charge(now, before)) mask |= kFieldCharge;
            if (!same_vitals(now, before)) mask |= kFieldVitals;
            if (!same_weapon(now, before)) mask |= kFieldWeapon;
        }
        w.u16(mask);
        if (mask & kFieldPos) w.vec3(now.pos);
        if (mask & kFieldVel) w.vec3(now.vel);
        if (mask & kFieldStatus) {
            w.u16(now.flags);
            w.u8(now.impulses);
            w.u8(now.state);
        }
        if (mask & kFieldTimers) {
            w.u16(now.state_ticks);
            w.u16(now.dash_cooldown);
            w.u16(now.slow_ticks);
        }
        if (mask & kFieldYaw) w.f32(now.yaw);
        if (mask & kFieldExtra) {
            w.vec3(now.wall_normal);
            w.vec3(now.vault_dir);
            w.f32(now.vault_rise);
        }
        if (mask & kFieldCharge) {
            w.u8(now.recharge);
            w.f32(now.speed_coefficient);
        }
        if (mask & kFieldVitals) {
            w.u16(now.hp);
            w.u16(now.armor);
        }
        if (mask & kFieldWeapon) {
            w.u8(now.weapon_id);
            w.u16(now.ammo);
            w.u16(now.fire_cooldown);
        }
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

        const uint16_t mask = r.u16();
        if (mask & kFieldPos) p.pos = r.vec3();
        if (mask & kFieldVel) p.vel = r.vec3();
        if (mask & kFieldStatus) {
            p.flags = r.u16();
            p.impulses = r.u8();
            p.state = r.u8();
        }
        if (mask & kFieldTimers) {
            p.state_ticks = r.u16();
            p.dash_cooldown = r.u16();
            p.slow_ticks = r.u16();
        }
        if (mask & kFieldYaw) p.yaw = r.f32();
        if (mask & kFieldExtra) {
            p.wall_normal = r.vec3();
            p.vault_dir = r.vec3();
            p.vault_rise = r.f32();
        }
        if (mask & kFieldCharge) {
            p.recharge = r.u8();
            p.speed_coefficient = r.f32();
        }
        if (mask & kFieldVitals) {
            p.hp = r.u16();
            p.armor = r.u16();
        }
        if (mask & kFieldWeapon) {
            p.weapon_id = r.u8();
            p.ammo = r.u16();
            p.fire_cooldown = r.u16();
        }
        if (!r.ok()) return false;
    }

    out = s;
    return true;
}

}  // namespace sim
