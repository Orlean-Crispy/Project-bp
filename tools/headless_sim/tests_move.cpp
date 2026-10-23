
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <thread>
#include <vector>

#include "sim/core/tick.h"
#include "sim/core/version.h"
#include "sim/core/world.h"
#include "sim/demo/demo.h"
#include "sim/input/bindings.h"
#include "sim/game/combat.h"
#include "sim/net/fragment.h"
#include "sim/net/admission.h"
#include "sim/net/handshake.h"
#include "sim/net/matchmaking.h"
#include "sim/net/netchan.h"
#include "sim/net/reliable.h"
#include "sim/net/reliable_transport.h"
#include "sim/net/session.h"
#include "sim/net/signaling.h"
#include "sim/net/ticket.h"
#include "sim/net/transport.h"

#include "options.h"
#include "test_util.h"
#include "tests_move.h"

using sim::MapData;
using sim::SlideResult;
using sim::MoveInput;
using sim::PlayerState;
using sim::Vec3;
using sim::World;

namespace {
constexpr float kDt = 1.0f / 64.0f;
}

int run_info(const Options& opt) {
    MapData map;
    std::string error;
    if (!sim::parse_map_file(opt.map_path, map, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }

    int brushes = 0;
    for (const auto& e : map.entities) brushes += static_cast<int>(e.brushes.size());

    std::printf("map       %s\n", opt.map_path.c_str());
    std::printf("entities  %zu\n", map.entities.size());
    std::printf("brushes   %d\n", brushes);
    std::printf("bounds    mins (%.3f, %.3f, %.3f)  maxs (%.3f, %.3f, %.3f)  [metres]\n",
                map.bounds.mins.x, map.bounds.mins.y, map.bounds.mins.z,
                map.bounds.maxs.x, map.bounds.maxs.y, map.bounds.maxs.z);
    for (const auto& e : map.entities) {
        const auto it = e.keys.find("classname");
        if (it != e.keys.end()) {
            std::printf("entity    %-20s brushes=%zu\n", it->second.c_str(), e.brushes.size());
        }
    }

    // AGENTS section 3 gate 1: "entities/brushes correct, bounds in metres". Until
    // now this function only printed, so an empty map (or inches instead of
    // metres) still exited 0 and smoke stayed green.
    bool ok = !map.entities.empty() && brushes > 0;
    if (!opt.map_explicit) {
        // test_box.map is the fixture every other gate is tuned against; its
        // shape is the contract.
        ok = ok && map.entities.size() == 2 && brushes == 8 &&
             std::fabs(map.bounds.mins.x + 10.5f) < 0.001f &&
             std::fabs(map.bounds.mins.y + 10.5f) < 0.001f &&
             std::fabs(map.bounds.mins.z + 0.5f) < 0.001f &&
             std::fabs(map.bounds.maxs.x - 10.5f) < 0.001f &&
             std::fabs(map.bounds.maxs.y - 10.5f) < 0.001f &&
             std::fabs(map.bounds.maxs.z - 8.5f) < 0.001f;
    }
    // A double conversion (0.0254 applied twice) would shrink a 21 m room to
    // 0.53 m; the metre-scale check catches that without pinning every map.
    const float extent = map.bounds.maxs.x - map.bounds.mins.x;
    ok = ok && extent > 1.0f && extent < 1000.0f;
    if (!ok) {
        std::printf("FAIL: map statistics are wrong (entities %zu, brushes %d, extent %.3f m)\n",
                    map.entities.size(), brushes, static_cast<double>(extent));
        return 1;
    }
    std::printf("PASS: %zu entities, %d brushes, %.1f m across\n", map.entities.size(), brushes,
                static_cast<double>(extent));
    return 0;
}

int run_selftest(const Options& opt) {
    std::string error;

    World a;
    World b;
    if (!load_world(opt, a, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }
    if (!load_world(opt, b, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }

    // (a.params_hash != b.params_hash used to be checked here, but both worlds are
    // loaded from the same file by a pure function: it could never fail.)

    std::vector<uint64_t> hash_a;
    std::vector<uint64_t> hash_b;
    hash_a.reserve(static_cast<size_t>(opt.ticks));
    hash_b.reserve(static_cast<size_t>(opt.ticks));

    for (int i = 0; i < opt.ticks; ++i) {
        const MoveInput in = make_input(opt.script, static_cast<uint32_t>(i));
        a.step({in}, kDt);
        hash_a.push_back(a.state_hash());
    }
    for (int i = 0; i < opt.ticks; ++i) {
        const MoveInput in = make_input(opt.script, static_cast<uint32_t>(i));
        b.step({in}, kDt);
        hash_b.push_back(b.state_hash());
    }

    size_t mismatch = 0;
    for (size_t i = 0; i < hash_a.size(); ++i) {
        if (hash_a[i] != hash_b[i]) {
            if (mismatch < 5) {
                std::printf("MISMATCH tick %zu\n", i);
            }
            ++mismatch;
        }
    }

    std::printf("ticks        %d\n", opt.ticks);
    std::printf("params_hash  %016llx\n", static_cast<unsigned long long>(a.params_hash));
    std::printf("geometry     %016llx\n",
                static_cast<unsigned long long>(a.collision.geometry_hash()));
    std::printf("final_hash   %016llx\n",
                static_cast<unsigned long long>(a.state_hash()));
    std::printf("final_pos    (%.4f, %.4f, %.4f)\n", a.players[0].pos.x, a.players[0].pos.y,
                a.players[0].pos.z);

    // R9: the hash must cover every field that feeds the next tick. Mutating
    // exactly one field of an otherwise identical world must change the hash;
    // yaw alone was invisible before the fix.
    int fields_hashed = 0;
    int fields_total = 0;
    {
        World probe;
        if (!load_world(opt, probe, error)) {
            std::fprintf(stderr, "error: %s\n", error.c_str());
            return 1;
        }
        const uint64_t base = probe.state_hash();
        const auto differs = [&](auto mutate) {
            const PlayerState saved = probe.players[0];
            mutate(probe.players[0]);
            const bool changed = probe.state_hash() != base;
            probe.players[0] = saved;
            return changed;
        };
        // Every field World::state_hash() claims to cover (audit R9), one probe
        // each. The list used to stop at eight, so dropping slow_ticks or the
        // slide/dash/vault edges from the hash would still print 8/8.
        const auto probe_field = [&](auto mutate) {
            ++fields_total;
            fields_hashed += differs(mutate) ? 1 : 0;
        };
        probe_field([](PlayerState& p) { p.pos.x += 0.5f; });
        probe_field([](PlayerState& p) { p.vel.z += 0.5f; });
        probe_field([](PlayerState& p) { p.yaw += 0.5f; });
        probe_field([](PlayerState& p) { p.active = false; });
        probe_field([](PlayerState& p) { p.on_ground = true; });  // spawns airborne
        probe_field([](PlayerState& p) { p.crouched = true; });
        probe_field([](PlayerState& p) { p.impulses = 1; });
        probe_field([](PlayerState& p) { p.recharge_progress = 3; });
        probe_field([](PlayerState& p) { p.state = sim::ParkourState::WallRun; });
        probe_field([](PlayerState& p) { p.state_ticks = 7; });
        probe_field([](PlayerState& p) { p.dash_cooldown = 5; });
        probe_field([](PlayerState& p) { p.slow_ticks = 9; });
        probe_field([](PlayerState& p) { p.jump_was_down = true; });
        probe_field([](PlayerState& p) { p.slide_was_down = true; });
        probe_field([](PlayerState& p) { p.dash_was_down = true; });
        probe_field([](PlayerState& p) { p.vault_was_down = true; });
        probe_field([](PlayerState& p) { p.wall_normal = Vec3(0.0f, 0.0f, -1.0f); });
        probe_field([](PlayerState& p) { p.vault_dir = Vec3(0.0f, 1.0f, 0.0f); });
        probe_field([](PlayerState& p) { p.vault_rise = 0.9f; });
        probe_field([](PlayerState& p) { p.speed_coefficient = 0.5f; });
        probe_field([](PlayerState& p) { p.slow_factor = 0.85f; });
    }
    std::printf("state_fields  %d/%d\n", fields_hashed, fields_total);

    if (mismatch != 0 || fields_hashed != fields_total) {
        std::printf("FAIL: %zu of %d ticks differ, %d/%d state fields hashed\n", mismatch,
                    opt.ticks, fields_hashed, fields_total);
        return 1;
    }
    std::printf("PASS: %d ticks identical on both runs\n", opt.ticks);
    return 0;
}

// Fires the player at the thin wall without friction, so the test measures the
// sweep itself rather than the ground friction that would slow a normal sprint.
int run_wall_test(const Options& opt) {
    std::string error;
    World world;
    if (!load_world(opt, world, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }

    const float speeds[] = {8.13f, 15.24f, 20.0f, 25.0f, 30.0f};
    const float wall_near = -0.05f;  // thin wall front face, metres
    const float radius = world.params.player_radius;
    const float contact_x = wall_near - radius;  // expected resting x
    int failures = 0;

    std::printf("wall front x = %.3f, expected stop x = %.3f (+-0.01)\n", wall_near, contact_x);

    for (float speed : speeds) {
        int penetrations = 0;
        float worst_x = -1000.0f;
        float max_step = 0.0f;

        for (int trial = 0; trial < 100; ++trial) {
            PlayerState p;
            // Vary the start so the 100 trials are not the same test copied 100
            // times: contact epsilon and bump order depend on where the box lands.
            const float jitter = 0.002f * static_cast<float>(trial % 10);
            p.pos = sim::Vec3(-1.0f - jitter, 0.001f * static_cast<float>(trial / 10), 0.02f);
            p.vel = sim::Vec3(speed, 0.0f, 0.0f);
            sim::Vec3 mins;
            sim::Vec3 maxs;
            sim::player_hull(p, world.params, mins, maxs);

            for (int t = 0; t < 10; ++t) {
                const SlideResult r =
                    sim::slide_move(world.collision, p.pos, p.vel, mins, maxs, kDt, world.params);
                p.pos = r.pos;
                p.vel = r.vel;
                max_step = std::max(max_step, r.max_step_length);
            }

            worst_x = std::max(worst_x, p.pos.x);
            if (p.pos.x > contact_x + 0.01f) ++penetrations;
        }

        // trace.h promises that no sub-step is longer than min_wall_thickness.
        // That only holds while ceil(speed*dt/thickness) fits inside the step cap:
        // the speed ceiling is max_slide_steps * min_wall_thickness * tick_rate
        // = 4 * 0.15 * 64 = 38.4 m/s. The tiers below stop at 30 m/s, so a
        // failure here means the sweep regressed, not the test range.
        const bool contained = penetrations == 0;
        const bool steps_ok = max_step <= world.params.min_wall_thickness;
        // A player that never moved also has zero penetrations: require that the
        // box actually reached the wall before accepting "did not pass through".
        const bool reached = worst_x >= contact_x - 0.01f;
        const bool moved = worst_x > -0.9f;
        const bool ok = contained && steps_ok && reached && moved;
        if (!ok) ++failures;
        std::printf("  %5.2f m/s  penetrations=%3d  worst_x=%7.4f  max_step=%.4f m (limit %.3f)"
                    "  contact %d moved %d  %s\n",
                    static_cast<double>(speed), penetrations, static_cast<double>(worst_x),
                    static_cast<double>(max_step),
                    static_cast<double>(world.params.min_wall_thickness), reached ? 1 : 0,
                    moved ? 1 : 0, ok ? "ok" : "FAIL");
    }

    // Sliding must survive the contact-epsilon clamp: a player pressed into the
    // wall at an angle has to keep moving along it, not stick.
    {
        PlayerState p;
        p.pos = sim::Vec3(-1.0f, 0.0f, 0.02f);
        p.vel = sim::Vec3(8.0f, 6.0f, 0.0f);
        sim::Vec3 mins;
        sim::Vec3 maxs;
        sim::player_hull(p, world.params, mins, maxs);
        const float start_y = p.pos.y;
        for (int t = 0; t < 20; ++t) {
            const SlideResult r =
                sim::step_slide_move(world.collision, p.pos, p.vel, mins, maxs, kDt, world.params).moved;
            p.pos = r.pos;
            p.vel = r.vel;
        }
        const float slid = p.pos.y - start_y;
        const bool ok = slid > 0.5f && p.pos.x < contact_x + 0.01f;
        if (!ok) ++failures;
        std::printf("  %5s  slid %.3f m along wall, x=%.3f\n", ok ? "slide" : "STUCK",
                    static_cast<double>(slid), static_cast<double>(p.pos.x));
    }

    if (failures != 0) {
        std::printf("FAIL: %d speed tiers penetrated the wall or stepped longer than it\n", failures);
        return 1;
    }
    std::printf("PASS: no penetration at any tested speed, wall sliding preserved\n");
    return 0;
}

// Bullets and projectiles are swept as one segment per tick; at 500 m/s a tick
// covers 7.8 m, which is 78x the wall thickness. This checks that the sweep is a
// real sweep and not point sampling (plan section 6.5 / B2).
int run_ray_test(const Options& opt) {
    std::string error;
    World world;
    if (!load_world(opt, world, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }

    int failures = 0;
    // fraction is part of the judgement: a sweep that degenerates to "the start is
    // already inside the wall" still reports a hit, but at fraction 0.
    auto expect_hit = [&](const char* label, bool hit, float fraction, bool want_hit, float lo,
                          float hi) {
        bool ok = hit == want_hit;
        if (ok && want_hit) ok = fraction >= lo && fraction <= hi;
        if (ok && !want_hit) ok = fraction >= 1.0f;
        if (!ok) ++failures;
        std::printf("  %-28s hit=%s fraction=%.4f  %s\n", label, hit ? "yes" : "no",
                    static_cast<double>(fraction), ok ? "ok" : "FAIL");
    };

    const sim::Vec3 zero(0.0f, 0.0f, 0.0f);
    const sim::TraceResult point =
        world.collision.trace_box(sim::Vec3(-5.0f, 0.0f, 1.0f), sim::Vec3(5.0f, 0.0f, 1.0f), zero, zero);
    expect_hit("point across 0.1 m wall", point.fraction < 1.0f, point.fraction, true, 0.3f,
               0.7f);

    const sim::TraceResult point_miss =
        world.collision.trace_box(sim::Vec3(-5.0f, 5.0f, 1.0f), sim::Vec3(5.0f, 5.0f, 1.0f), zero, zero);
    expect_hit("point beside the wall", point_miss.fraction < 1.0f, point_miss.fraction, false,
               0.0f, 0.0f);

    // 7.8 m of travel in a single segment: the 500 m/s case.
    const sim::TraceResult fast =
        world.collision.trace_box(sim::Vec3(-5.0f, 0.0f, 1.0f), sim::Vec3(2.8f, 0.0f, 1.0f), zero, zero);
    expect_hit("point, 7.8 m segment", fast.fraction < 1.0f, fast.fraction, true, 0.5f, 0.8f);

    PlayerState p;
    p.pos = sim::Vec3(-5.0f, 0.0f, 0.01f);
    sim::Vec3 mins;
    sim::Vec3 maxs;
    sim::player_hull(p, world.params, mins, maxs);
    const sim::TraceResult box =
        world.collision.trace_box(p.pos, sim::Vec3(5.0f, 0.0f, 0.01f), mins, maxs);
    expect_hit("box across wall, one segment", box.fraction < 1.0f, box.fraction, true, 0.3f,
               0.6f);

    std::printf("  box contact x = %.4f (expected %.4f)\n", static_cast<double>(box.end_pos.x),
                static_cast<double>(-0.05f - world.params.player_radius));
    if (box.end_pos.x > -0.05f - world.params.player_radius + 0.01f) ++failures;

    if (failures != 0) {
        std::printf("FAIL: %d ray checks failed\n", failures);
        return 1;
    }
    std::printf("PASS: swept traces hit thin geometry at any segment length\n");
    return 0;
}

int run_step_test(const Options& opt) {
    Options local = opt;
    if (!opt.map_explicit) local.map_path = "sim/data/maps/step_test.map";

    std::string error;
    World world;
    if (!load_world(local, world, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }

    int failures = 0;

    auto scenario = [&](float x, float y, int ticks, bool crouch, bool start_crouched,
                        PlayerState& out) {
        PlayerState p;
        p.pos = sim::Vec3(x, y, 0.01f);
        p.crouched = start_crouched;
        p.impulses = world.params.max_impulses;
        world.players.assign(1, p);
        world.tick = 0;
        for (int i = 0; i < ticks; ++i) {
            MoveInput in;
            in.forward = 1.0f;
            in.crouch = crouch;
            world.step({in}, kDt);
        }
        out = world.players[0];
    };

    auto expect_z = [&](const char* label, float got, float want, float tol) {
        const bool ok = std::fabs(got - want) <= tol;
        if (!ok) ++failures;
        std::printf("  %-34s z=%.4f (want %.4f +-%.3f)  %s\n", label,
                    static_cast<double>(got), static_cast<double>(want),
                    static_cast<double>(tol), ok ? "ok" : "FAIL");
    };

    PlayerState out;

    scenario(-2.0f, -4.0f, 90, false, false, out);
    expect_z("0.20 m step: climbed", out.pos.z, 0.20f, 0.02f);

    scenario(-2.0f, -2.0f, 90, false, false, out);
    expect_z("0.35 m step: climbed", out.pos.z, 0.35f, 0.02f);

    scenario(-2.0f, 2.0f, 140, false, false, out);
    expect_z("three 0.15 m stairs", out.pos.z, 0.45f, 0.02f);

    scenario(-2.0f, 0.0f, 90, false, false, out);
    // -2.0 is the start: a frozen player satisfies "x < -0.4" too, so require that
    // it closed some distance before being stopped.
    const bool blocked = out.pos.z < 0.05f && out.pos.x < -0.4f && out.pos.x > -1.5f;
    if (!blocked) ++failures;
    std::printf("  %-34s z=%.4f x=%.4f  %s\n", "0.55 m wall: blocked",
                static_cast<double>(out.pos.z), static_cast<double>(out.pos.x),
                blocked ? "ok" : "FAIL");

    // Crouch under a 1.0 m ceiling, then try to stand up again.
    scenario(1.0f, 4.0f, 40, true, true, out);
    const float crouched_z = out.pos.z;
    const bool stayed_crouched_while_held = out.crouched;
    scenario(1.0f, 4.0f, 40, false, true, out);
    const bool kept_crouched = out.crouched && out.pos.z < 0.05f;
    if (!stayed_crouched_while_held || !kept_crouched) ++failures;
    std::printf("  %-34s crouched=%s z=%.4f  %s\n", "headroom: cannot stand under ceiling",
                out.crouched ? "yes" : "no", static_cast<double>(out.pos.z),
                kept_crouched ? "ok" : "FAIL");
    (void)crouched_z;

    if (failures != 0) {
        std::printf("FAIL: %d step checks failed\n", failures);
        return 1;
    }
    std::printf("PASS: stairs climb, walls block, crouch respects headroom\n");
    return 0;
}

int run_parkour_test(const Options& opt) {
    Options local = opt;
    if (!opt.map_explicit) local.map_path = "sim/data/maps/parkour_test.map";

    std::string error;
    World world;
    if (!load_world(local, world, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }

    int failures = 0;

    auto reset = [&](float x, float y) {
        PlayerState p;
        p.pos = sim::Vec3(x, y, 0.01f);
        p.impulses = world.params.max_impulses;
        world.players.assign(1, p);
        world.tick = 0;
    };

    // Control: a standing run must be stopped by the 1.0 m low ceiling.
    reset(-5.0f, 0.5f);
    for (int i = 0; i < 80; ++i) {
        MoveInput in;
        in.forward = 1.0f;
        world.step({in}, kDt);
    }
    {
        const PlayerState& p = world.players[0];
        // Start is x = -5: require real progress before the ceiling stopped it.
        const bool blocked = p.pos.x < 0.1f && p.pos.z < 0.1f && p.pos.x > -4.5f;
        if (!blocked) ++failures;
        std::printf("  %-34s x=%.3f z=%.3f  %s\n", "standing run blocked by low ceiling",
                    static_cast<double>(p.pos.x), static_cast<double>(p.pos.z), blocked ? "ok" : "FAIL");
    }

    // Slide: build speed, then slide under the same ceiling.
    reset(-5.0f, 0.5f);
    bool slid = false;
    bool crouched_under = false;
    for (int i = 0; i < 130; ++i) {
        MoveInput in;
        in.forward = 1.0f;
        in.slide = (i >= 30);
        world.step({in}, kDt);
        const PlayerState& p = world.players[0];
        if (p.state == sim::ParkourState::Slide) slid = true;
        if (p.pos.x > 0.6f && p.pos.x < 2.0f && p.crouched) crouched_under = true;
    }
    {
        const PlayerState& p = world.players[0];
        const bool passed = p.pos.x > 0.8f;
        if (!slid || !passed || !crouched_under) ++failures;
        std::printf("  %-34s slid=%d crouched=%d x=%.3f  %s\n", "slide under low ceiling",
                    slid ? 1 : 0, crouched_under ? 1 : 0, static_cast<double>(p.pos.x),
                    (slid && passed && crouched_under) ? "ok" : "FAIL");
    }

    // Dash: same start, same tick count, with and without the dash.
    auto run_dash = [&](bool dash, double& distance) {
        reset(-10.0f, 7.0f);
        double peak = 0.0;
        for (int i = 0; i < 12; ++i) {
            MoveInput in;
            in.forward = 1.0f;
            in.dash = dash && i == 2;
            world.step({in}, kDt);
            const sim::Vec3& v = world.players[0].vel;
            peak = std::max(peak, std::sqrt(static_cast<double>(v.x) * v.x + static_cast<double>(v.y) * v.y));
        }
        distance = world.players[0].pos.x + 10.0f;
        return peak;
    };
    double plain_dist = 0.0;
    double dash_dist = 0.0;
    const double plain_peak = run_dash(false, plain_dist);
    const double dash_peak = run_dash(true, dash_dist);
    {
        // The dash must (a) reach its configured speed and (b) actually cover
        // more ground in the same number of ticks.
        const bool ok = dash_peak >= world.params.dash_speed * 0.95 &&
                        dash_peak > plain_peak * 1.25 && dash_dist > plain_dist * 1.2;
        if (!ok) ++failures;
        std::printf("  %-34s peak %.2f vs %.2f m/s, %.3f vs %.3f m  %s\n", "dash burst",
                    dash_peak, plain_peak, dash_dist, plain_dist, ok ? "ok" : "FAIL");
    }

    // Wall run: airborne next to a runnable wall, holding the key.
    auto run_wallrun = [&](bool use_wallrun, float& end_z, bool& entered) {
        PlayerState p;
        p.pos = sim::Vec3(3.0f, -2.55f, 4.0f);
        p.vel = sim::Vec3(8.0f, -3.0f, 0.0f);
        p.on_ground = false;
        p.impulses = world.params.max_impulses;
        world.players.assign(1, p);
        world.tick = 0;
        entered = false;
        for (int i = 0; i < 50; ++i) {
            MoveInput in;
            in.forward = 1.0f;
            in.wallrun = use_wallrun;
            world.step({in}, kDt);
            if (world.players[0].state == sim::ParkourState::WallRun) entered = true;
        }
        end_z = world.players[0].pos.z;
    };
    {
        float run_z = 0.0f;
        float fall_z = 0.0f;
        bool entered = false;
        run_wallrun(true, run_z, entered);
        bool ignored = false;
        run_wallrun(false, fall_z, ignored);
        const bool ok = entered && run_z > 1.0f && run_z > fall_z + 0.8f;
        if (!ok) ++failures;
        std::printf("  %-34s entered=%d z %.2f vs %.2f  %s\n", "wall run slows the fall",
                    entered ? 1 : 0, static_cast<double>(run_z), static_cast<double>(fall_z),
                    ok ? "ok" : "FAIL");
    }

    // Wall climb: grounded against a climbable wall, holding the key.
    {
        PlayerState p;
        p.pos = sim::Vec3(15.5f, 4.0f, 0.01f);
        p.impulses = world.params.max_impulses;
        world.players.assign(1, p);
        world.tick = 0;
        bool entered = false;
        for (int i = 0; i < 40; ++i) {
            MoveInput in;
            in.wallclimb = true;
            world.step({in}, kDt);
            if (world.players[0].state == sim::ParkourState::WallClimb) entered = true;
        }
        const float z = world.players[0].pos.z;
        const bool ok = entered && z > 2.0f;
        if (!ok) ++failures;
        std::printf("  %-34s entered=%d z=%.2f m  %s\n", "wall climb gains height",
                    entered ? 1 : 0, static_cast<double>(z), ok ? "ok" : "FAIL");
    }

    // Vault: run at a 1.0 m platform and climb over it.
    {
        PlayerState p;
        p.pos = sim::Vec3(8.5f, 9.5f, 0.01f);
        p.impulses = world.params.max_impulses;
        world.players.assign(1, p);
        world.tick = 0;
        bool entered = false;
        for (int i = 0; i < 90; ++i) {
            MoveInput in;
            in.forward = 1.0f;
            in.vault = (i >= 25 && i < 45);
            world.step({in}, kDt);
            if (world.players[0].state == sim::ParkourState::Vault) entered = true;
        }
        const PlayerState& q = world.players[0];
        const bool ok = entered && q.pos.z > 0.9f && q.pos.x > 10.0f;
        if (!ok) ++failures;
        std::printf("  %-34s entered=%d x=%.2f z=%.2f  %s\n", "vault onto 1.0 m platform",
                    entered ? 1 : 0, static_cast<double>(q.pos.x), static_cast<double>(q.pos.z),
                    ok ? "ok" : "FAIL");
    }

    // R2: vault and wall climb must not both fire in one tick. Starting a
    // vault next to a climbable 1 m block while holding wallclimb used to spend
    // two impulses and end the tick in WallClimb, because busy was computed
    // before the vault start.
    {
        PlayerState p;
        p.pos = sim::Vec3(16.55f, 10.0f, 0.01f);
        p.impulses = world.params.max_impulses;
        world.players.assign(1, p);
        world.tick = 0;
        MoveInput in;
        in.forward = 1.0f;
        in.vault = true;
        in.wallclimb = true;
        world.step({in}, kDt);
        const PlayerState& q = world.players[0];
        const bool single = q.state == sim::ParkourState::Vault &&
                            q.impulses == world.params.max_impulses - 1;
        if (!single) {
            ++failures;
            std::printf("  vault_climb detail: state %d impulses %d/%d pos (%.2f %.2f %.2f)\n",
                        static_cast<int>(q.state), q.impulses, world.params.max_impulses,
                        static_cast<double>(q.pos.x), static_cast<double>(q.pos.y),
                        static_cast<double>(q.pos.z));
        }
        std::printf("  [parkour] vault_climb_single %d\n", single ? 1 : 0);
    }

    // R7: movement.toml [damage] is live in the effective-speed path: a hit
    // costs 15% speed for 1.5 s, a second hit refreshes (never stacks), and the
    // speed comes back when the timer runs out.
    {
        reset(-10.0f, 7.0f);
        sim::apply_damage_slow(world.players[0], world.params);
        float slowed_peak = 0.0f;
        for (int i = 0; i < 20; ++i) {
            MoveInput in;
            in.forward = 1.0f;
            world.step({in}, kDt);
            const sim::Vec3& v = world.players[0].vel;
            slowed_peak = std::max(slowed_peak, std::sqrt(v.x * v.x + v.y * v.y));
        }
        sim::apply_damage_slow(world.players[0], world.params);
        const bool refreshed = world.players[0].slow_ticks == world.params.damage_slow_ticks;
        for (int i = 0; i < world.params.damage_slow_ticks + 8; ++i) {
            MoveInput in;
            in.forward = 1.0f;
            world.step({in}, kDt);
        }
        float restored_peak = 0.0f;
        for (int i = 0; i < 10; ++i) {
            MoveInput in;
            in.forward = 1.0f;
            world.step({in}, kDt);
            const sim::Vec3& v = world.players[0].vel;
            restored_peak = std::max(restored_peak, std::sqrt(v.x * v.x + v.y * v.y));
        }
        const float expected = world.params.base_speed * world.params.damage_slow_factor;
        const bool slow_ok = std::fabs(slowed_peak - expected) < 0.05f && refreshed &&
                             std::fabs(restored_peak - world.params.base_speed) < 0.05f;
        if (!slow_ok) ++failures;
        std::printf("  %-34s slowed %.2f restored %.2f m/s  %s\n", "damage slow -15% for 1.5 s",
                    static_cast<double>(slowed_peak), static_cast<double>(restored_peak),
                    slow_ok ? "ok" : "FAIL");
    }

    // R7 follow-up: impulse recharge is one per recharge_ticks of ground contact,
    // not one per tick. Returning all four in 62 ms made every parkour cost
    // meaningless, and the old code had no gate at all for it.
    {
        reset(-10.0f, 7.0f);
        world.players[0].impulses = 0;
        MoveInput idle;
        const int period = world.params.recharge_ticks;
        for (int i = 0; i < period - 1; ++i) world.step({idle}, kDt);
        const bool not_yet = world.players[0].impulses == 0;
        world.step({idle}, kDt);
        const bool after = world.players[0].impulses == 1;
        if (!not_yet || !after) ++failures;
        std::printf("  %-34s top-up after %d ticks=%d  %s\n", "impulse recharge interval", period,
                    world.players[0].impulses, (not_yet && after) ? "ok" : "FAIL");
    }

    if (failures != 0) {
        std::printf("FAIL: %d parkour checks failed\n", failures);
        return 1;
    }
    std::printf("PASS: slide / dash / wall run / wall climb / vault behave\n");
    return 0;
}

int run_keys(const Options& opt) {
    sim::Bindings bindings;
    std::string error;
    std::vector<std::string> warnings;
    if (!bindings.load(opt.keybinds_path, error, warnings)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }
    for (const std::string& w : warnings) std::fprintf(stderr, "warning: %s\n", w.c_str());

    std::printf("keybinds: %s\n\n", opt.keybinds_path.c_str());
    std::printf("  %-12s %s\n", "ACTION", "KEY");
    for (size_t i = 0; i < sim::kActionCount; ++i) {
        const sim::Action action = static_cast<sim::Action>(i);
        std::printf("  %-12s %s\n", sim::action_name(action), sim::key_name(bindings.key(action)));
    }
    if (!bindings.conflicts().empty()) {
        std::printf("\nconflicts:\n");
        for (const std::string& c : bindings.conflicts()) std::printf("  %s\n", c.c_str());
    }
    return 0;
}

// Proves the three properties that make the keymap "remappable" rather than
// merely "configurable on paper": it loads from data, changes take effect
// immediately, and the change survives a save/reload.
int run_keybind_test(const Options& opt) {
    int failures = 0;
    auto check = [&](const char* label, bool ok) {
        if (!ok) ++failures;
        std::printf("  %-42s %s\n", label, ok ? "ok" : "FAIL");
    };

    sim::Bindings bindings;
    std::string error;
    std::vector<std::string> warnings;
    if (!bindings.load(opt.keybinds_path, error, warnings)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }
    for (const std::string& w : warnings) std::fprintf(stderr, "warning: %s\n", w.c_str());

    check("every action has a key", bindings.unbound().empty());
    check("no key is bound twice", bindings.conflicts().empty());
    check("wall run on Q (Valorant layout)",
          bindings.key(sim::Action::WallRun) == sim::Key::Q);
    check("wall climb on V", bindings.key(sim::Action::WallClimb) == sim::Key::V);
    check("interact on E", bindings.key(sim::Action::Interact) == sim::Key::E);
    check("vault on C", bindings.key(sim::Action::Vault) == sim::Key::C);
    check("dash on X", bindings.key(sim::Action::Dash) == sim::Key::X);
    // The whole point of the move: nothing else may claim E, or the interact key
    // silently fires a movement ability instead (M6 entry condition 6).
    bool e_free = true;
    for (sim::Action a : {sim::Action::WallRun, sim::Action::WallClimb, sim::Action::Vault,
                          sim::Action::Dash, sim::Action::Slide, sim::Action::Crouch,
                          sim::Action::Jump}) {
        if (bindings.key(a) == sim::Key::E) e_free = false;
    }
    check("E is reserved for interact", e_free);

    sim::KeyState keys;
    keys.set(sim::Key::W, true);
    keys.set(sim::Key::Space, true);
    sim::MoveInput input = sim::build_move_input(bindings, keys, 0.0f);
    check("W + SPACE produces forward + jump", input.forward > 0.5f && input.jump);

    // Rebind at runtime; the old key must stop working immediately.
    // Z, not V: V is the wall climb key now, and binding jump there would make
    // the conflict check below fail for the wrong reason.
    bindings.set_key(sim::Action::Jump, sim::Key::Z);
    input = sim::build_move_input(bindings, keys, 0.0f);
    check("after rebinding, SPACE no longer jumps", !input.jump);
    keys.set(sim::Key::Z, true);
    input = sim::build_move_input(bindings, keys, 0.0f);
    check("after rebinding, Z jumps", input.jump);

    // Persist and reload into a fresh object.
    const std::string saved = "build/keybinds_test.toml";
    if (!bindings.save(saved, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }
    sim::Bindings reloaded;
    if (!reloaded.load(saved, error, warnings)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }
    check("saved keymap reloads with Z on jump",
          reloaded.key(sim::Action::Jump) == sim::Key::Z);

    // Positive control for conflict detection: this must be able to fail.
    bindings.set_key(sim::Action::Slide, sim::Key::Q);
    check("duplicate bind is reported", !bindings.conflicts().empty());
    bindings.set_key(sim::Action::Slide, sim::Key::LeftShift);
    check("restoring the bind clears the conflict", bindings.conflicts().empty());

    if (failures != 0) {
        std::printf("FAIL: %d keybind checks failed\n", failures);
        return 1;
    }
    std::printf("PASS: keymap loads from data, remaps live, and persists\n");
    return 0;
}


// M6 groundwork: HP 100 and the three armour types. Absorption and regen timing
// are proposals (see combat.toml), so these checks pin the *rules*, not the tuning.
int run_health_test(const Options& opt) {
    (void)opt;
    sim::CombatParams params;
    std::string error;
    std::vector<std::string> warnings;
    if (!sim::load_combat_beside(opt.data_path, params, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }

    int failures = 0;
    auto check = [&](const char* label, bool ok) {
        if (!ok) ++failures;
        std::printf("  %-46s %s\n", label, ok ? "ok" : "FAIL");
    };

    sim::PlayerVitals bare;
    bare.health = params.max_health;
    const int dealt = sim::apply_damage(bare, 30, params);
    check("no armour: 30 damage -> 70 HP", bare.health == 70 && dealt == 30);

    sim::PlayerVitals light = bare;
    light.health = params.max_health;
    sim::equip_armor(light, sim::ArmorType::Light, params);
    sim::apply_damage(light, 30, params);
    // 30 * 66% = 19 absorbed, 11 to health, armour 25 -> 6.
    check("light armour: 30 damage -> 19 absorbed, 11 HP", light.armor == 6 && light.health == 89);

    sim::PlayerVitals heavy = bare;
    heavy.health = params.max_health;
    sim::equip_armor(heavy, sim::ArmorType::Heavy, params);
    sim::apply_damage(heavy, 40, params);
    // 40 * 66% = 26 absorbed, 14 to health, armour 50 -> 24.
    check("heavy armour: 40 damage -> 26 absorbed, 14 HP", heavy.armor == 24 && heavy.health == 86);

    // R17: bad input must neither index the armour table out of bounds nor turn
    // a hit into healing. An unknown type behaves like "no armour"; >100%
    // absorption is clamped to 100% at use even without the loader.
    sim::PlayerVitals bogus;
    bogus.health = params.max_health;
    bogus.armor = 50;
    bogus.armor_type = static_cast<sim::ArmorType>(99);
    sim::apply_damage(bogus, 25, params);
    check("invalid armour type is ignored, not indexed", bogus.health == 75 && bogus.armor == 50);

    sim::CombatParams clamping = params;
    clamping.armor[static_cast<int>(sim::ArmorType::Heavy)].absorb_percent = 500;
    sim::PlayerVitals clamped;
    clamped.health = params.max_health;
    sim::equip_armor(clamped, sim::ArmorType::Heavy, clamping);
    sim::apply_damage(clamped, 25, clamping);
    check("absorption above 100% cannot heal", clamped.health == 100 && clamped.armor == 25);

    sim::PlayerVitals broken = bare;
    broken.health = params.max_health;
    sim::equip_armor(broken, sim::ArmorType::Light, params);
    sim::apply_damage(broken, 100, params);
    // Armour caps at 25 absorbed: 75 spills to health.
    check("armour caps out: 100 damage -> 25 absorbed, 75 HP lost",
          broken.armor == 0 && broken.health == 25);

    sim::PlayerVitals regen = bare;
    regen.health = params.max_health;
    sim::equip_armor(regen, sim::ArmorType::Regen, params);
    sim::apply_damage(regen, 20, params);
    const int armour_after_hit = regen.armor;
    // Inside the delay nothing regenerates.
    for (int i = 0; i < 100; ++i) sim::tick_vitals(regen, params);
    check("regen armour: no refill during the delay", regen.armor == armour_after_hit);

    for (int i = 0; i < 2000; ++i) sim::tick_vitals(regen, params);
    check("regen armour: refills to its maximum", regen.armor == 25);

    sim::apply_damage(regen, 10, params);
    for (int i = 0; i < 100; ++i) sim::tick_vitals(regen, params);
    check("regen armour: taking damage restarts the delay", regen.ticks_since_damage <= 100);

    // Sanity: does 25 damage per shot at 600 RPM land inside the 0.6-1.2 s TTK band?
    // The shots go through the real apply_damage(), not a copy of its arithmetic:
    // changing absorption in combat.cpp must move this number, not leave the gate
    // measuring the formula it was written from (audit R13).
    int shots = 0;
    sim::PlayerVitals ttk_target;
    ttk_target.health = params.max_health;
    sim::equip_armor(ttk_target, sim::ArmorType::Heavy, params);
    while (ttk_target.health > 0 && shots < 100) {
        ++shots;
        sim::apply_damage(ttk_target, 25, params);
    }
    const float ttk = static_cast<float>(shots) / 10.0f;  // 600 RPM = 10 shots/s
    std::printf("  body TTK with heavy armour: %d shots = %.2f s\n", shots,
                static_cast<double>(ttk));
    check("body TTK stays inside the 0.6-1.2 s band", ttk >= 0.6f && ttk <= 1.2f);

    if (failures != 0) {
        std::printf("FAIL: %d health/armour checks failed\n", failures);
        return 1;
    }
    std::printf("PASS: HP/armour rules behave and TTK lands in the plan's band\n");
    return 0;
}

int run_event_test(const Options& opt) {
    Options local = opt;
    if (!opt.map_explicit) local.map_path = "sim/data/maps/test_box.map";

    std::string error;
    int failures = 0;

    auto collect = [&](std::vector<std::pair<uint32_t, uint16_t>>& out) {
        World world;
        if (!load_world(local, world, error)) return false;
        sim::EventQueue events;
        for (int i = 0; i < 90; ++i) {
            MoveInput in;
            in.jump = (i == 5);
            world.step({in}, kDt, &events);
            sim::SimEvent event;
            while (events.pop(event)) {
                out.emplace_back(event.tick, static_cast<uint16_t>(event.type));
            }
        }
        return true;
    };

    std::vector<std::pair<uint32_t, uint16_t>> first;
    std::vector<std::pair<uint32_t, uint16_t>> second;
    if (!collect(first) || !collect(second)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }

    uint32_t jump_tick = 0;
    uint32_t land_tick = 0;
    int jumps = 0;
    int lands = 0;
    for (const auto& e : first) {
        if (e.second == static_cast<uint16_t>(sim::EventType::Jump)) {
            ++jumps;
            jump_tick = e.first;
        } else if (e.second == static_cast<uint16_t>(sim::EventType::Land)) {
            ++lands;
            land_tick = e.first;
        }
    }

    const bool deterministic = first == second;
    const bool sequence_ok = jumps == 1 && lands == 1 && jump_tick == 5 && land_tick > jump_tick;
    std::printf("  events: jump tick %u, land tick %u, jumps %d, lands %d, deterministic %d\n",
                jump_tick, land_tick, jumps, lands, deterministic);
    if (!sequence_ok || !deterministic) {
        std::printf("FAIL: event emission is wrong or non-deterministic\n");
        ++failures;
    }

    // Ring overflow drops the oldest and counts it, never allocates.
    sim::EventQueue ring;
    for (uint32_t i = 0; i < sim::EventQueue::kCapacity + 76; ++i) {
        sim::SimEvent event;
        event.tick = i;
        event.type = sim::EventType::Land;
        ring.push(event);
    }
    const size_t ring_size = ring.size();
    sim::SimEvent oldest;
    ring.pop(oldest);
    const bool ring_ok = ring_size == sim::EventQueue::kCapacity && ring.dropped() == 76 &&
                         oldest.tick == 76;
    std::printf("  ring: size %zu, dropped %llu, first tick %u\n", ring_size,
                static_cast<unsigned long long>(ring.dropped()), oldest.tick);
    if (!ring_ok) {
        std::printf("FAIL: event ring does not drop the oldest entry\n");
        ++failures;
    }

    if (failures != 0) {
        std::printf("FAIL: %d event checks failed\n", failures);
        return 1;
    }
    std::printf("PASS: events carry ticks, are deterministic, and never grow\n");
    return 0;
}

int run_hitbox_test(const Options& opt) {
    sim::MovementParams params;
    std::vector<std::string> warnings;
    std::string error;
    if (!sim::load_movement_params(opt.data_path, params, warnings, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }

    int failures = 0;
    auto check = [&](const char* what, bool ok) {
        std::printf("  %-50s %s\n", what, ok ? "ok" : "FAIL");
        if (!ok) ++failures;
    };

    sim::HitboxHistory history;
    sim::PlayerState p;
    p.pos = sim::Vec3(0.0f, 0.0f, 0.0f);
    for (uint32_t t = 10; t <= 12; ++t) {
        p.pos.x = static_cast<float>(t - 10);
        history.record(p, params, t);
    }

    check("history records the sampled ticks", history.size() == 3);
    check("exact frame lookup", history.at(11).pos.x == 1.0f);
    const sim::HitboxSample mid = history.sample(11, 0.5f);
    check("midpoint interpolation", std::fabs(mid.pos.x - 1.5f) < 1e-5f);
    check("frac 0 returns the earlier frame", history.sample(11, 0.0f).pos.x == 1.0f);
    // sample() always copies the tick argument into the result, so asserting the
    // tick proved nothing. The position is what must not run past the newest
    // frame: there is no tick 13, so frac 0.5 must stay at x = 2.0, not 2.5.
    check("no extrapolation past the newest frame",
          std::fabs(history.sample(12, 0.5f).pos.x - 2.0f) < 1e-6f);

    // Plan 6.8: head > 0.82 h, chest > 0.45 h. Standing h = 1.42 m.
    const sim::HitboxSample stand = history.at(11);
    check("standing height is 1.42 m", std::fabs(stand.height - 1.42f) < 1e-4f);
    check("head band", sim::hit_zone_at(stand, 1.30f) == sim::HitZone::Head);
    check("head multiplier is x5", sim::hit_multiplier(stand, 1.30f, params) == 5.0f);
    check("chest band", sim::hit_zone_at(stand, 0.90f) == sim::HitZone::Chest);
    // The zone table was only checked for head and legs; a chest multiplier of 1.5
    // would have passed the "x5 / x1 / x0.7" gate.
    check("chest multiplier is x1", sim::hit_multiplier(stand, 0.90f, params) == 1.0f);
    check("leg band", sim::hit_zone_at(stand, 0.30f) == sim::HitZone::Legs);
    check("leg multiplier is x0.7", sim::hit_multiplier(stand, 0.30f, params) == 0.7f);
    // The value is tuning data now: a changed params must change the result, or
    // it is still a literal wearing a field's name.
    {
        sim::MovementParams tuned = params;
        tuned.legshot_mult = 0.55f;
        check("leg multiplier reads movement.toml",
              sim::hit_multiplier(stand, 0.30f, tuned) == 0.55f);
    }
    check("just above the head line is head", sim::hit_zone_at(stand, 0.84f * 1.42f) ==
                                                  sim::HitZone::Head);
    check("just below the head line is chest", sim::hit_zone_at(stand, 0.80f * 1.42f) ==
                                                   sim::HitZone::Chest);

    // Crouching moves the bands with the hull (0.91 m), no special case.
    p.crouched = true;
    p.pos = sim::Vec3(0.0f, 0.0f, 0.0f);
    history.record(p, params, 20);
    const sim::HitboxSample crouch = history.at(20);
    check("crouched height is 0.91 m", std::fabs(crouch.height - 0.91f) < 1e-4f);
    check("crouched head band", sim::hit_zone_at(crouch, 0.80f) == sim::HitZone::Head);
    check("crouched chest band", sim::hit_zone_at(crouch, 0.50f) == sim::HitZone::Chest);
    check("crouched leg band", sim::hit_zone_at(crouch, 0.20f) == sim::HitZone::Legs);

    const sim::Vec3 center = stand.pos;
    check("point inside the hull",
          sim::hitbox_contains(stand, sim::Vec3(center.x + 0.40f, center.y, center.z + 0.50f)));
    check("point outside the radius",
          !sim::hitbox_contains(stand, sim::Vec3(center.x + 0.41f, center.y, center.z + 0.50f)));
    check("point above the head",
          !sim::hitbox_contains(stand, sim::Vec3(center.x, center.y, center.z + 1.43f)));

    // Plan 7.3 ring: 64 ticks = 1 s; older ticks are overwritten.
    for (uint32_t t = 0; t <= 69; ++t) {
        p.crouched = false;
        p.pos = sim::Vec3(0.01f * static_cast<float>(t), 0.0f, 0.0f);
        history.record(p, params, t);
    }
    check("ring keeps one second (64 ticks)", history.size() == 64);
    check("oldest tick advances", history.oldest_tick() == 6);
    check("newest tick is the last record", history.newest_tick() == 69);
    check("overwritten tick clamps to the oldest", history.at(5).tick == 6);
    check("wrapped lookup works", std::fabs(history.at(6).pos.x - 0.06f) < 1e-5f);

    if (failures != 0) {
        std::printf("FAIL: %d hitbox checks failed\n", failures);
        return 1;
    }
    std::printf("PASS: hitbox history interpolates and the 3-zone bands follow crouch\n");
    return 0;
}

int run_jump_test(const Options& opt) {
    Options local = opt;
    if (!opt.map_explicit) local.map_path = "sim/data/maps/metrics_flat.map";

    std::string error;
    World world;
    if (!load_world(local, world, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }

    auto count_takeoffs = [&](bool held, int taps) {
        PlayerState p;
        p.pos = sim::Vec3(-8.0f, 5.0f, 0.01f);
        p.impulses = world.params.max_impulses;
        world.players.assign(1, p);
        world.tick = 0;

        int takeoffs = 0;
        bool was_ground = true;
        for (int i = 0; i < 300; ++i) {
            MoveInput in;
            if (taps > 0) {
                // 60 ticks apart: longer than the ~44 tick air time, so each tap
                // happens while grounded.
                in.jump = (i % 60) < 2 && (i / 60) < taps;
            } else {
                in.jump = held;
            }
            world.step({in}, kDt);
            const bool now_ground = world.players[0].on_ground;
            if (was_ground && !now_ground) ++takeoffs;
            was_ground = now_ground;
        }
        return takeoffs;
    };

    const int held = count_takeoffs(true, 0);
    const int tapped = count_takeoffs(false, 3);

    std::printf("  holding space for 300 ticks -> %d takeoff(s)\n", held);
    std::printf("  tapping space 3 times      -> %d takeoff(s)\n", tapped);

    if (held != 1 || tapped != 3) {
        std::printf("FAIL: jump is not edge triggered\n");
        return 1;
    }
    std::printf("PASS: jump fires on press, not while held\n");
    return 0;
}

int run_soak_test(const Options& opt) {
    Options local = opt;
    if (!opt.map_explicit) local.map_path = "sim/data/maps/playground.map";

    std::string error;
    World world;
    if (!load_world(local, world, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }

    int solid_streak = 0;
    int max_solid_streak = 0;
    int frozen_streak = 0;
    int max_frozen_streak = 0;
    sim::Vec3 last = world.players[0].pos;

    for (int i = 0; i < 6000; ++i) {
        MoveInput in;
        in.forward = 1.0f;
        in.yaw = 0.9f * std::sin(static_cast<float>(i) * 0.01f);
        in.jump = (i % 47) == 0;
        in.slide = (i % 90) < 12;
        in.dash = (i % 137) == 0;
        in.wallrun = (i % 160) < 24;
        in.wallclimb = (i % 211) < 18;
        in.vault = (i % 173) == 0;
        world.step({in}, kDt);

        const PlayerState& p = world.players[0];
        sim::Vec3 mins;
        sim::Vec3 maxs;
        sim::player_hull(p, world.params, mins, maxs);
        const bool solid = world.collision.trace_box(p.pos, p.pos, mins, maxs).start_solid;
        solid_streak = solid ? solid_streak + 1 : 0;
        max_solid_streak = std::max(max_solid_streak, solid_streak);

        const float moved = sim::length(p.pos - last);
        frozen_streak = moved < 1e-4f ? frozen_streak + 1 : 0;
        max_frozen_streak = std::max(max_frozen_streak, frozen_streak);
        last = p.pos;
    }

    std::printf("  ticks 6000   max solid streak %d   max frozen streak %d\n", max_solid_streak,
                max_frozen_streak);
    // A real freeze (the box wedged in a brush) shows up as a solid streak; being
    // pressed against a wall while holding forward is legitimate and lasts as long
    // as the mash keeps pushing. 64 ticks (1 s) still catches "camera moves, player
    // does not" without failing on wall contact (the mash reaches ~45).
    if (max_solid_streak > 2 || max_frozen_streak > 64) {
        std::printf("FAIL: player was stuck (solid %d ticks, frozen %d ticks)\n", max_solid_streak,
                    max_frozen_streak);
        return 1;
    }
    std::printf("PASS: no persistent solid state across 6000 mashing ticks\n");
    return 0;
}

int run_stuck_test(const Options& opt) {
    std::string error;
    World world;
    if (!load_world(opt, world, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }

    PlayerState p;
    p.pos = sim::Vec3(0.0f, 0.0f, 0.5f);  // inside the 0.1 m thin wall
    p.impulses = world.params.max_impulses;
    world.players.assign(1, p);
    world.tick = 0;

    const float x0 = world.players[0].pos.x;
    for (int i = 0; i < 60; ++i) {
        MoveInput in;
        in.forward = 1.0f;
        world.step({in}, kDt);
    }
    const PlayerState& q = world.players[0];
    const float moved = q.pos.x - x0;

    std::printf("  start inside wall, after 60 ticks: x=%.3f z=%.3f moved=%.3f\n",
                static_cast<double>(q.pos.x), static_cast<double>(q.pos.z),
                static_cast<double>(moved));
    if (moved < 1.0f) {
        std::printf("FAIL: still frozen after starting inside geometry\n");
        return 1;
    }
    std::printf("PASS: depenetration frees the player\n");
    return 0;
}

int run_edge_test(const Options& opt) {
    Options local = opt;
    if (!opt.map_explicit) local.map_path = "sim/data/maps/playground.map";

    std::string error;
    World world;
    if (!load_world(local, world, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }

    // Drop onto the platform so that only ~20 cm of the box overlaps it: this is
    // the "half a foot on the lip" case the player hit.
    PlayerState p;
    p.pos = sim::Vec3(9.8f, 6.0f, 1.4f);
    p.impulses = world.params.max_impulses;
    world.players.assign(1, p);
    world.tick = 0;

    for (int i = 0; i < 30; ++i) {
        MoveInput in;
        world.step({in}, kDt);
    }
    const PlayerState& q = world.players[0];
    std::printf("  after falling onto the lip: x=%.3f z=%.3f state=%d\n",
                static_cast<double>(q.pos.x), static_cast<double>(q.pos.z),
                static_cast<int>(q.state));

    const float x_before = q.pos.x;
    for (int i = 0; i < 40; ++i) {
        MoveInput in;
        in.forward = 1.0f;
        world.step({in}, kDt);
    }
    const float moved = world.players[0].pos.x - x_before;

    const bool on_platform = q.pos.z > 0.9f;
    const bool still_moves = moved > 1.0f;
    std::printf("  continued %.3f m in 40 ticks\n", static_cast<double>(moved));

    if (!on_platform || !still_moves) {
        std::printf("FAIL: %s%s\n", on_platform ? "" : "did not land on the platform; ",
                    still_moves ? "" : "stuck after landing");
        return 1;
    }

    // Grazing approach to the wide stairs (y -2..2): drift +Y while running +X.
    {
        PlayerState s;
        s.pos = sim::Vec3(-3.0f, -1.5f, 0.01f);
        s.impulses = world.params.max_impulses;
        world.players.assign(1, s);
        world.tick = 0;
        float max_z = 0.0f;
        for (int i = 0; i < 160; ++i) {
            MoveInput in;
            in.forward = 1.0f;
            in.yaw = 0.15f;
            world.step({in}, kDt);
            max_z = std::max(max_z, world.players[0].pos.z);
        }
        const PlayerState& q2 = world.players[0];
        const bool climbed = max_z > 0.4f;
        if (!climbed) {
            std::printf("FAIL: grazing approach did not climb the stairs (z=%.3f x=%.2f y=%.2f)\n",
                        static_cast<double>(q2.pos.z), static_cast<double>(q2.pos.x),
                        static_cast<double>(q2.pos.y));
            return 1;
        }
        std::printf("  grazing approach onto the stairs: max z=%.3f x=%.2f\n",
                    static_cast<double>(max_z), static_cast<double>(q2.pos.x));
    }

    std::printf("PASS: landing on the lip does not freeze the player\n");
    return 0;
}


int run_bench(const Options& opt) {
    // The CLI already rejects these, but a gate should not depend on its caller.
    if (opt.bench_ticks <= 0 || opt.bench_players <= 0) {
        std::fprintf(stderr, "error: --bench needs positive tick and player counts\n");
        return 2;
    }
    std::string error;
    World world;
    if (!load_world(opt, world, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }

    // Players never collide with each other, so sharing a spawn point measures
    // the per-player simulation cost without inventing gameplay semantics.
    if (opt.bench_players > 1) {
        const PlayerState spawn = world.players[0];
        world.players.assign(static_cast<size_t>(opt.bench_players), spawn);
    }

    std::vector<double> samples;
    samples.reserve(static_cast<size_t>(opt.bench_ticks));
    std::vector<MoveInput> inputs(world.players.size());

    const unsigned long long alloc_before = allocation_count();
    for (int i = 0; i < opt.bench_ticks; ++i) {
        const MoveInput in = make_input("jump", static_cast<uint32_t>(i));
        for (MoveInput& slot : inputs) slot = in;
        const int64_t start = sim::now_ns();
        world.step(inputs, kDt);
        const int64_t took = sim::elapsed_ns(start);
        samples.push_back(static_cast<double>(took) / 1000.0);
    }

    std::sort(samples.begin(), samples.end());
    double sum = 0.0;
    for (double s : samples) sum += s;
    const size_t p99 = static_cast<size_t>(static_cast<double>(samples.size()) * 0.99);
    const double p99_us = samples[std::min(p99, samples.size() - 1)];
    const double max_us = samples.back();
    const unsigned long long allocs = allocation_count() - alloc_before;
    const double per_tick = static_cast<double>(allocs) / static_cast<double>(opt.bench_ticks);
    std::printf("ticks  %d   players %d\n", opt.bench_ticks, opt.bench_players);
    std::printf("mean   %.2f us\n", sum / static_cast<double>(samples.size()));
    std::printf("p99    %.2f us  (budget 4000 us)\n", p99_us);
    std::printf("max    %.2f us\n", max_us);
    std::printf("allocs %.2f / tick\n", per_tick);

    // Iron law E was never a gate: the numbers were printed and smoke only looked
    // at the exit code, so a regression to 5 ms or to allocating per tick would
    // still be ALL GREEN.
    const bool budget_ok = p99_us <= 4000.0;
    const bool alloc_ok = allocs == 0;
    if (!budget_ok || !alloc_ok) {
        std::printf("FAIL: p99 %.2f us (budget 4000) or %llu allocations (iron law E)\n", p99_us,
                    allocs);
        return 1;
    }
    std::printf("PASS: p99 within the 4 ms budget, zero allocations per tick\n");
    return 0;
}



