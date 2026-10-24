// Protocol wire gates: the snapshot format is the one thing both sides must
// agree on byte for byte, so it gets its own gate instead of riding inside
// --test-net (that file is already the largest test).
//
// Protocol v6 exists because the M5 -> M6 audit found the delta mask and the
// flags word each had one bit left. These checks fail if either word silently
// narrows again, if a combat field stops surviving the wire, or if the full
// snapshot grows past the MTU safety line.

#include "tests_proto.h"

#include <cstdio>

#include "sim/core/version.h"
#include "sim/net/snapshot.h"

namespace {

// Header: tick, acked_cmd, baseline_tick (u32 x3) + player count (u8).
constexpr std::size_t kHeaderBytes = 13;
// The MTU safety line from docs/AGENTS.md section 8.4 (fragments start at 1100).
constexpr std::size_t kSnapshotBudget = 1100;

bool near(float a, float b) { return (a > b ? a - b : b - a) < 1e-6f; }

int check(bool ok, const char* what) {
    if (ok) return 0;
    std::printf("FAIL: %s\n", what);
    return 1;
}

}  // namespace

int run_snapshot_check() {
    int failures = 0;
    std::printf("  protocol v%u, demo format v%u\n",
                static_cast<unsigned>(sim::kProtocolVersion),
                static_cast<unsigned>(sim::kDemoFormatVersion));

    // 1. Every field survives a full round-trip, vitals included.
    sim::PlayerState src;
    src.pos = sim::Vec3(1.5f, -2.25f, 0.5f);
    src.vel = sim::Vec3(3.0f, 4.0f, -1.0f);
    src.yaw = 2.5f;
    src.on_ground = true;
    src.jump_was_down = true;
    src.vault_was_down = true;
    src.impulses = 2;
    src.recharge_progress = 40;
    src.speed_coefficient = 0.92f;
    src.state = sim::ParkourState::Slide;
    src.state_ticks = 7;
    src.dash_cooldown = 3;
    src.slow_ticks = 11;
    src.wall_normal = sim::Vec3(0.0f, 1.0f, 0.0f);
    src.vault_dir = sim::Vec3(1.0f, 0.0f, 0.0f);
    src.vault_rise = 0.3f;
    src.vitals.health = 63;
    src.vitals.armor = 25;
    // M6.2: the trigger/reload/aim bits are held states, so they must survive the
    // wire like every other piece of state that steers the next tick.
    src.firing = true;
    src.reloading = true;
    src.ads = true;

    sim::Snapshot full_snap;
    full_snap.tick = 100;
    full_snap.players = {sim::to_snap(src)};
    const std::vector<uint8_t> full = sim::encode_snapshot(full_snap, nullptr);
    sim::Snapshot decoded;
    const bool full_ok = sim::decode_snapshot(full.data(), full.size(), nullptr, decoded) &&
                         decoded.players.size() == 1;
    sim::PlayerState round;
    if (full_ok) sim::from_snap(decoded.players[0], round);
    const bool vitals_ok = full_ok && round.vitals.health == 63 && round.vitals.armor == 25;
    const bool fields_ok = full_ok && round.pos == src.pos && round.vel == src.vel &&
                           near(round.yaw, src.yaw) && round.on_ground == src.on_ground &&
                           round.jump_was_down == src.jump_was_down &&
                           round.vault_was_down == src.vault_was_down &&
                           round.impulses == src.impulses && round.state == src.state &&
                           round.state_ticks == src.state_ticks &&
                           round.firing == src.firing && round.reloading == src.reloading &&
                           round.ads == src.ads &&
                           near(round.speed_coefficient, src.speed_coefficient);
    failures += check(fields_ok, "a movement field no longer survives the wire");
    failures += check(vitals_ok, "hp/armor do not survive the wire (kFieldVitals)");
    std::printf("  [wire] full %d vitals %d bytes %zu\n", full_ok ? 1 : 0, vitals_ok ? 1 : 0,
                full.size());

    // 2. A change in hp only must travel as a delta. With a uint8 mask the
    //    kFieldVitals bit (1 << 7) is truncated, so the client keeps the old hp
    //    and the divergence only shows up as a mystery correction later.
    sim::Snapshot before = full_snap;
    sim::Snapshot after = full_snap;
    after.tick = 101;
    after.players[0].hp = 55;
    const std::vector<uint8_t> delta = sim::encode_snapshot(after, &before);
    sim::Snapshot decoded_delta;
    const bool delta_ok = sim::decode_snapshot(delta.data(), delta.size(), &before, decoded_delta) &&
                          decoded_delta.players.size() == 1 && decoded_delta.players[0].hp == 55 &&
                          decoded_delta.players[0].armor == 25;
    failures += check(delta_ok, "a hp-only delta is lost (mask no longer fits 16 bits)");

    // 2b. Releasing the trigger must travel too. The bits share the flags word
    //     with on_ground/crouched, so if the Status group ever stops covering
    //     them a remote player would stay frozen mid-burst.
    sim::Snapshot trigger_before = full_snap;
    sim::Snapshot trigger_after = full_snap;
    trigger_after.tick = 102;
    trigger_after.players[0].flags = static_cast<uint16_t>(trigger_after.players[0].flags &
                                                           ~(1u << 7));
    const std::vector<uint8_t> trigger_delta =
        sim::encode_snapshot(trigger_after, &trigger_before);
    sim::Snapshot trigger_out;
    const bool trigger_ok = sim::decode_snapshot(trigger_delta.data(), trigger_delta.size(),
                                                 &trigger_before, trigger_out) &&
                            trigger_out.players.size() == 1 &&
                            (trigger_out.players[0].flags & (1u << 7)) == 0 &&
                            (trigger_out.players[0].flags & (1u << 9)) != 0;
    failures += check(trigger_ok, "releasing the trigger does not survive a delta");

    // 3. The flags word is 16 bits wide on the wire too. Bits 7..10 are reserved
    //    for M6 (firing/reloading/ads/alive); an 8-bit field would truncate them.
    sim::Snapshot wide;
    wide.tick = 7;
    sim::PlayerSnap flagged;
    flagged.flags = 0x07FF;
    wide.players = {flagged};
    const std::vector<uint8_t> wide_bytes = sim::encode_snapshot(wide, nullptr);
    sim::Snapshot wide_out;
    const bool flags_ok = sim::decode_snapshot(wide_bytes.data(), wide_bytes.size(), nullptr,
                                              wide_out) &&
                          wide_out.players.size() == 1 && wide_out.players[0].flags == 0x07FF;
    std::printf("  [wire] flags 0x%03X %d\n", 0x07FF, flags_ok ? 1 : 0);
    failures += check(flags_ok, "the flags word is truncated to 8 bits");

    // 4. Delta economy: an unchanged snapshot must cost one mask per player and
    //    nothing else. This is what keeps the reserved Weapon group free.
    const std::vector<uint8_t> idle = sim::encode_snapshot(before, &before);
    const std::size_t idle_expected = kHeaderBytes + 2;  // one u16 mask per player
    std::printf("  [wire] idle delta %zu B (expected %zu)\n", idle.size(), idle_expected);
    failures += check(idle.size() == idle_expected, "an unchanged delta is not mask-only");

    // 5. Ten players at full size must stay under the fragmentation line, so M6
    //    does not turn every snapshot into IP fragments by accident.
    sim::Snapshot ten;
    ten.tick = 9;
    for (int i = 0; i < 10; ++i) {
        sim::PlayerState p = src;
        p.pos = sim::Vec3(static_cast<float>(i), 0.0f, 0.0f);
        ten.players.push_back(sim::to_snap(p));
    }
    const std::vector<uint8_t> ten_bytes = sim::encode_snapshot(ten, nullptr);
    const std::size_t per_player = (ten_bytes.size() - kHeaderBytes) / ten.players.size();
    std::printf("  [wire] 10 players %zu B, %zu B/player, budget %zu B\n", ten_bytes.size(),
                per_player, kSnapshotBudget);
    failures += check(ten_bytes.size() <= kSnapshotBudget,
                      "a 10-player full snapshot is over the MTU safety line");

    // 6. A delta without its baseline is still refused rather than half-decoded.
    sim::Snapshot orphan;
    failures += check(!sim::decode_snapshot(delta.data(), delta.size(), nullptr, orphan),
                      "a delta with no baseline decoded anyway");

    // 7. The advertised protocol version is the one this format implements.
    failures += check(sim::kProtocolVersion == 7, "protocol version is not 7");

    std::printf("  [snapshot] %s\n", failures == 0 ? "ok" : "FAILED");
    return failures;
}
