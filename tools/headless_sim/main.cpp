// headless_sim - the terminal runner that makes the simulation testable without
// Godot, a GPU or a display (plan section 3.2).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "sim/core/tick.h"
#include "sim/core/version.h"
#include "sim/core/world.h"
#include "sim/demo/demo.h"
#include "sim/input/bindings.h"
#include "sim/game/combat.h"
#include "sim/net/session.h"
#include "sim/net/transport.h"

namespace {

using sim::CollisionWorld;
using sim::MapData;
using sim::MoveInput;
using sim::MovementParams;
using sim::PlayerState;
using sim::SlideResult;
using sim::World;

constexpr float kDt = 1.0f / 64.0f;

struct Options {
    std::string map_path = "sim/data/maps/test_box.map";
    std::string data_path = "sim/data/movement.toml";
    std::string script = "jump";
    int ticks = 600;
    int bench_ticks = 1000;
    bool selftest = false;
    bool test_wall = false;
    bool test_ray = false;
    bool test_step = false;
    bool test_parkour = false;
    bool test_edge = false;
    bool test_stuck = false;
    bool test_soak = false;
    bool test_jump = false;
    bool test_net = false;
    bool test_health = false;
    bool test_bindings = false;
    bool print_keys = false;
    bool map_explicit = false;
    std::string keybinds_path = "sim/data/keybinds.toml";
    std::string record_path;
    std::string play_path;
    std::string dump_path;
    std::string compare_a;
    std::string compare_b;
    bool test_demo = false;
    double tolerance = 0.01;
    bool bench = false;
    bool info = false;
    bool print_hash = false;
};

void usage() {
    std::printf(
        "headless_sim - Project Breakpoint simulation runner\n"
        "\n"
        "  --map <file>      .map file (default sim/data/maps/test_box.map)\n"
        "  --data <file>     movement.toml (default sim/data/movement.toml)\n"
        "  --ticks <n>       ticks to simulate (default 600)\n"
        "  --script <name>   idle | run | jump | strafe (default jump)\n"
        "  --hash            print the world hash of every tick\n"
        "  --info            print map statistics and exit\n"
        "  --selftest        run the same input twice and compare per-tick hashes\n"
        "  --test-wall       fire a player at a 0.1 m wall 100 times, count penetrations\n"
        "  --test-ray        sweep point and box traces across the thin wall in one segment\n"
        "  --test-step       stair stepping and crouch headroom (uses step_test.map)\n"
        "  --test-parkour    slide tunnel and dash (uses parkour_test.map)\n"
        "  --keys            print the resolved key bindings\n"
        "  --test-bindings   verify the keymap loads, remaps and persists\n"
        "  --keybinds <file> keybind file (default sim/data/keybinds.toml)\n"
        "  --record <file>   record the scripted run to a demo\n"
        "  --play <file>     replay a demo and compare per-tick hashes\n"
        "  --dump <csv>      with --play: write per-tick positions\n"
        "  --compare <a> <b> compare two --dump files, report max deviation\n"
        "  --test-demo       record, replay, and prove divergence detection works\n"
        "  --test-edge       jump onto a platform edge and keep walking (uses playground.map)\n"
        "  --test-stuck      start inside a brush and prove the player can get out\n"
        "  --test-soak       mash movement abilities for 6000 ticks, watch for freezes\n"
        "  --test-jump       holding space must jump once, tapping must jump each time\n"
        "  --test-net        client/server over a 30%% loss link: prediction converges\n"
        "  --test-health     HP 100 + 25 / 25 regen / 50 armour damage and regen rules\n"
        "  --bench [n]       time n ticks and report mean/p99/max\n");
}

bool load_world_from(const std::string& map_path, const std::string& data_path, World& world,
                     std::string& error) {
    MapData map;
    if (!sim::parse_map_file(map_path, map, error)) return false;

    std::vector<std::string> warnings;
    if (!sim::load_movement_params(data_path, world.params, warnings, error)) return false;
    for (const std::string& w : warnings) std::fprintf(stderr, "warning: %s\n", w.c_str());

    world.collision.build(map);
    world.params_hash = world.params.hash();
    return true;
}

bool load_world(const Options& opt, World& world, std::string& error) {
    if (!load_world_from(opt.map_path, opt.data_path, world, error)) return false;

    PlayerState player;
    // Spawn in the open lane (y = 5), clear of the 0.1 m test wall at x = 0.
    player.pos = sim::Vec3(-5.0f, 5.0f, 0.01f);
    player.impulses = world.params.max_impulses;
    world.players.push_back(player);
    return true;
}

MoveInput make_input(const std::string& script, uint32_t tick) {
    MoveInput in;
    if (script == "run") {
        in.forward = 1.0f;
    } else if (script == "jump") {
        in.forward = 1.0f;
        in.jump = (tick % 45) == 0;
    } else if (script == "strafe") {
        in.forward = tick < 128 ? 1.0f : 0.0f;
        in.right = tick >= 128 ? 1.0f : 0.0f;
    }
    return in;
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

    if (a.params_hash != b.params_hash) {
        std::fprintf(stderr, "FAIL: parameter hash differs between loads\n");
        return 1;
    }

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

    if (mismatch != 0) {
        std::printf("FAIL: %zu of %d ticks differ\n", mismatch, opt.ticks);
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
            p.pos = sim::Vec3(-1.0f, 0.0f, 0.02f);
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

        const bool ok = penetrations == 0;
        if (!ok) ++failures;
        std::printf("  %5.2f m/s  penetrations=%3d  worst_x=%7.4f  max_step=%.4f m  %s\n",
                    static_cast<double>(speed), penetrations, static_cast<double>(worst_x),
                    static_cast<double>(max_step), ok ? "ok" : "FAIL");
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
        std::printf("  %5s  slid %.3f m along wall, x=%.3f\n", ok ? "slide" : "slide",
                    static_cast<double>(slid), static_cast<double>(p.pos.x));
    }

    if (failures != 0) {
        std::printf("FAIL: %d speed tiers penetrated the 0.1 m wall\n", failures);
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
    auto expect_hit = [&](const char* label, bool hit, float fraction, bool want_hit) {
        const bool ok = hit == want_hit;
        if (!ok) ++failures;
        std::printf("  %-28s hit=%s fraction=%.4f  %s\n", label, hit ? "yes" : "no",
                    static_cast<double>(fraction), ok ? "ok" : "FAIL");
    };

    const sim::Vec3 zero(0.0f, 0.0f, 0.0f);
    const sim::TraceResult point =
        world.collision.trace_box(sim::Vec3(-5.0f, 0.0f, 1.0f), sim::Vec3(5.0f, 0.0f, 1.0f), zero, zero);
    expect_hit("point across 0.1 m wall", point.fraction < 1.0f, point.fraction, true);

    const sim::TraceResult point_miss =
        world.collision.trace_box(sim::Vec3(-5.0f, 5.0f, 1.0f), sim::Vec3(5.0f, 5.0f, 1.0f), zero, zero);
    expect_hit("point beside the wall", point_miss.fraction < 1.0f, point_miss.fraction, false);

    // 7.8 m of travel in a single segment: the 500 m/s case.
    const sim::TraceResult fast =
        world.collision.trace_box(sim::Vec3(-5.0f, 0.0f, 1.0f), sim::Vec3(2.8f, 0.0f, 1.0f), zero, zero);
    expect_hit("point, 7.8 m segment", fast.fraction < 1.0f, fast.fraction, true);

    PlayerState p;
    p.pos = sim::Vec3(-5.0f, 0.0f, 0.01f);
    sim::Vec3 mins;
    sim::Vec3 maxs;
    sim::player_hull(p, world.params, mins, maxs);
    const sim::TraceResult box =
        world.collision.trace_box(p.pos, sim::Vec3(5.0f, 0.0f, 0.01f), mins, maxs);
    expect_hit("box across wall, one segment", box.fraction < 1.0f, box.fraction, true);

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
    const bool blocked = out.pos.z < 0.05f && out.pos.x < -0.4f;
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
        const bool blocked = p.pos.x < 0.1f && p.pos.z < 0.1f;
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
    check("wall climb on E", bindings.key(sim::Action::WallClimb) == sim::Key::E);
    check("vault on C", bindings.key(sim::Action::Vault) == sim::Key::C);
    check("dash on X", bindings.key(sim::Action::Dash) == sim::Key::X);

    sim::KeyState keys;
    keys.set(sim::Key::W, true);
    keys.set(sim::Key::Space, true);
    sim::MoveInput input = sim::build_move_input(bindings, keys, 0.0f);
    check("W + SPACE produces forward + jump", input.forward > 0.5f && input.jump);

    // Rebind at runtime; the old key must stop working immediately.
    bindings.set_key(sim::Action::Jump, sim::Key::V);
    input = sim::build_move_input(bindings, keys, 0.0f);
    check("after rebinding, SPACE no longer jumps", !input.jump);
    keys.set(sim::Key::V, true);
    input = sim::build_move_input(bindings, keys, 0.0f);
    check("after rebinding, V jumps", input.jump);

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
    check("saved keymap reloads with V on jump",
          reloaded.key(sim::Action::Jump) == sim::Key::V);

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

// ---------------------------------------------------------------------------
// Demo record / replay (M4). A demo is inputs + seed + initial state; playback
// recomputes every tick and compares the stored world hash.
// ---------------------------------------------------------------------------

struct PlayOutcome {
    uint32_t ticks = 0;
    int mismatches = 0;
    uint32_t first_bad = 0;
    bool header_mismatch = false;
    std::string header_reason;
};

bool play_file(const std::string& demo_path, const std::string& params_path,
               const std::string& map_override, const std::string& dump_path,
               PlayOutcome& out, std::string& error) {
    sim::DemoReader reader;
    sim::DemoHeader header;
    if (!reader.open(demo_path, header, error)) return false;

    World world;
    const std::string map_path = map_override.empty() ? header.map : map_override;
    if (!load_world_from(map_path, params_path, world, error)) return false;

    world.players.clear();
    for (const sim::DemoPlayerInit& init : header.players) {
        PlayerState p;
        p.pos = init.pos;
        p.vel = init.vel;
        p.speed_coefficient = init.speed_coefficient;
        p.slow_factor = init.slow_factor;
        p.crouched = init.crouched;
        p.impulses = init.impulses;
        world.players.push_back(p);
    }
    world.tick = 0;

    out.header_reason = sim::demo_mismatch_reason(header, sim::kSimVersion, sim::kProtocolVersion,
                                                  world.params_hash,
                                                  world.collision.geometry_hash());
    out.header_mismatch = !out.header_reason.empty();

    FILE* dump = nullptr;
    if (!dump_path.empty()) {
        dump = std::fopen(dump_path.c_str(), "wb");
        if (!dump) {
            error = "cannot write " + dump_path;
            return false;
        }
        std::fprintf(dump, "tick,x,y,z,vx,vy,vz\n");
    }

    sim::DemoTick tick;
    bool has_tick = false;
    for (;;) {
        if (!reader.next(tick, has_tick, error)) {
            if (dump) std::fclose(dump);
            return false;
        }
        if (!has_tick) break;

        world.step(tick.inputs, kDt);
        if (world.state_hash() != tick.state_hash) {
            if (out.mismatches == 0) out.first_bad = tick.tick;
            ++out.mismatches;
        }
        if (dump && !world.players.empty()) {
            const PlayerState& p = world.players[0];
            std::fprintf(dump, "%u,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n", tick.tick,
                         static_cast<double>(p.pos.x), static_cast<double>(p.pos.y),
                         static_cast<double>(p.pos.z), static_cast<double>(p.vel.x),
                         static_cast<double>(p.vel.y), static_cast<double>(p.vel.z));
        }
        ++out.ticks;
    }
    if (dump) std::fclose(dump);
    return true;
}

int run_record(const Options& opt) {
    World world;
    std::string error;
    if (!load_world(opt, world, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }

    sim::DemoHeader header;
    header.build_id = sim::kBuildId;
    header.sim_version = sim::kSimVersion;
    header.protocol_version = sim::kProtocolVersion;
    header.tick_rate = sim::kDefaultTickRate;
    header.params_hash = world.params_hash;
    header.geometry_hash = world.collision.geometry_hash();
    header.map = opt.map_path;
    for (const PlayerState& p : world.players) {
        sim::DemoPlayerInit init;
        init.pos = p.pos;
        init.vel = p.vel;
        init.speed_coefficient = p.speed_coefficient;
        init.slow_factor = p.slow_factor;
        init.crouched = p.crouched;
        init.impulses = p.impulses;
        header.players.push_back(init);
    }

    sim::DemoWriter writer;
    if (!writer.open(opt.record_path, header, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }

    for (int i = 0; i < opt.ticks; ++i) {
        const MoveInput in = make_input(opt.script, static_cast<uint32_t>(i));
        world.step({in}, kDt);
        sim::DemoTick tick;
        tick.tick = static_cast<uint32_t>(i);
        tick.state_hash = world.state_hash();
        tick.inputs.push_back(in);
        writer.add(tick);
    }

    if (!writer.close(error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }

    std::printf("recorded %d ticks\n", opt.ticks);
    std::printf("file       %s\n", opt.record_path.c_str());
    std::printf("build      %s  sim v%u  proto v%u\n", header.build_id.c_str(),
                header.sim_version, header.protocol_version);
    std::printf("params     %016llx\n", static_cast<unsigned long long>(header.params_hash));
    std::printf("geometry   %016llx\n", static_cast<unsigned long long>(header.geometry_hash));
    return 0;
}

int run_play(const Options& opt) {
    PlayOutcome outcome;
    std::string error;
    if (!play_file(opt.play_path, opt.data_path, opt.map_explicit ? opt.map_path : "",
                   opt.dump_path, outcome, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }

    std::printf("demo       %s\n", opt.play_path.c_str());
    std::printf("ticks      %u\n", outcome.ticks);
    if (outcome.header_mismatch) {
        std::printf("WARNING    header mismatch: %s\n", outcome.header_reason.c_str());
    }
    if (outcome.mismatches != 0) {
        std::printf("MISMATCH   %d of %u ticks differ, first at tick %u\n", outcome.mismatches,
                    outcome.ticks, outcome.first_bad);
        return 1;
    }
    std::printf("PASS       all %u ticks reproduced\n", outcome.ticks);
    return 0;
}

bool compare_dumps(const std::string& a_path, const std::string& b_path, double& max_deviation,
                   uint32_t& rows, std::string& error) {
    FILE* a = std::fopen(a_path.c_str(), "rb");
    FILE* b = std::fopen(b_path.c_str(), "rb");
    if (!a || !b) {
        if (a) std::fclose(a);
        if (b) std::fclose(b);
        error = "cannot open dump files";
        return false;
    }

    char line_a[512];
    char line_b[512];
    if (!std::fgets(line_a, sizeof(line_a), a) || !std::fgets(line_b, sizeof(line_b), b)) {
        std::fclose(a);
        std::fclose(b);
        error = "empty dump file";
        return false;
    }

    rows = 0;
    max_deviation = 0.0;
    while (std::fgets(line_a, sizeof(line_a), a) && std::fgets(line_b, sizeof(line_b), b)) {
        unsigned tick_a = 0;
        unsigned tick_b = 0;
        float ax = 0.0f, ay = 0.0f, az = 0.0f, avx = 0.0f, avy = 0.0f, avz = 0.0f;
        float bx = 0.0f, by = 0.0f, bz = 0.0f, bvx = 0.0f, bvy = 0.0f, bvz = 0.0f;
        const int na = std::sscanf(line_a, "%u,%f,%f,%f,%f,%f,%f", &tick_a, &ax, &ay, &az, &avx, &avy, &avz);
        const int nb = std::sscanf(line_b, "%u,%f,%f,%f,%f,%f,%f", &tick_b, &bx, &by, &bz, &bvx, &bvy, &bvz);
        if (na != 7 || nb != 7 || tick_a != tick_b) {
            std::fclose(a);
            std::fclose(b);
            error = "dump files are not aligned";
            return false;
        }
        const double dx = std::fabs(static_cast<double>(ax) - bx);
        const double dy = std::fabs(static_cast<double>(ay) - by);
        const double dz = std::fabs(static_cast<double>(az) - bz);
        const double dev = std::max(dx, std::max(dy, dz));
        max_deviation = std::max(max_deviation, dev);
        ++rows;
    }
    std::fclose(a);
    std::fclose(b);
    return true;
}

int run_compare(const Options& opt) {
    double deviation = 0.0;
    uint32_t rows = 0;
    std::string error;
    if (!compare_dumps(opt.compare_a, opt.compare_b, deviation, rows, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }

    const bool ok = deviation <= opt.tolerance;
    std::printf("rows       %u\n", rows);
    std::printf("max dev    %.6f m (tolerance %.6f)\n", deviation, opt.tolerance);
    std::printf("%s\n", ok ? "PASS: within tolerance" : "FAIL: beyond tolerance");
    return ok ? 0 : 1;
}

// The gate for M4: a demo must replay exactly on the same build, and a changed
// movement.toml must be caught at the tick where it first matters.
// Landing on the lip of a 1 m platform used to leave the box overlapping the
// brush by a hair, which made every later horizontal trace report start_solid and
// froze the player. This is the regression gate for that.
// Reproduction of "camera moves but the player is frozen": the box is inside a
// brush, so every trace starts solid and zeroes the velocity. pmove must push it
// out and normal movement must resume.
// Chaos soak: deterministic pseudo-random input mash. The failure this guards
// against is "camera works, player does not move", i.e. the box wedged in a
// brush, so it checks for solid states and for long stretches without movement.
// Plan M5: unreliable UDP + delta snapshots + client prediction, over a link that
// drops 30% of packets in both directions with 30 ms of latency and jitter.
// M6 groundwork: HP 100 and the three armour types. Absorption and regen timing
// are proposals (see combat.toml), so these checks pin the *rules*, not the tuning.
int run_health_test(const Options& opt) {
    (void)opt;
    sim::CombatParams params;
    std::string error;
    std::vector<std::string> warnings;
    if (!sim::load_combat_params("sim/data/combat.toml", params, warnings, error)) {
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
    int shots = 0;
    int armour = 50;
    int health = 100;
    while (health > 0 && shots < 100) {
        ++shots;
        int absorbed = 25 * 66 / 100;
        if (absorbed > armour) absorbed = armour;
        armour -= absorbed;
        health -= (25 - absorbed);
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

int run_net_test(const Options& opt) {
    const std::string map_path = "sim/data/maps/metrics_flat.map";

    sim::NetServer server;
    std::string error;
    if (!server.init(map_path, opt.data_path, 1, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }

    sim::NetConditions conditions;
    conditions.loss = 0.30f;
    conditions.latency_ms = 30.0f;
    conditions.jitter_ms = 10.0f;
    conditions.duplicate = 0.02f;

    sim::LoopbackTransport to_server(conditions, 1234);
    sim::LoopbackTransport to_client(conditions, 5678);
    server.add_client(&to_server, &to_client, 0);

    sim::NetClient client;
    if (!client.init(&to_server, &to_client, 0, map_path, opt.data_path, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }

    int failures = 0;
    auto tick_once = [&](const MoveInput& in) {
        client.send_input(in);
        client.step_local();
        server.receive();
        server.step();
        server.send();
        to_server.advance(0);
        to_client.advance(0);
        client.receive();
    };

    const int ticks = 2000;
    for (int i = 0; i < ticks; ++i) {
        MoveInput in;
        in.forward = 1.0f;
        in.yaw = 0.35f * std::sin(static_cast<float>(i) * 0.01f);
        in.jump = (i % 90) == 0;
        tick_once(in);
    }

    // Let both sides come to rest so positions are comparable without latency.
    for (int i = 0; i < 100; ++i) tick_once(MoveInput{});

    const sim::Vec3 predicted = client.predicted().pos;
    const sim::Vec3 authoritative = server.world().players[0].pos;
    const float deviation = sim::length(predicted - authoritative);

    std::printf("  ticks %d   rollbacks %d   snapshots %d   full %d   delta %d   dropped cmds %d\n",
                ticks, client.rollbacks(), client.snapshots_received(), server.full_snapshots(),
                server.delta_snapshots(), server.dropped_commands());
    std::printf("  client: decode failures %d   stale skips %d\n", client.decode_failures(),
                client.stale_skips());
    std::printf("  client (%.4f, %.4f, %.4f)  server (%.4f, %.4f, %.4f)  dev %.5f m\n",
                static_cast<double>(predicted.x), static_cast<double>(predicted.y),
                static_cast<double>(predicted.z), static_cast<double>(authoritative.x),
                static_cast<double>(authoritative.y), static_cast<double>(authoritative.z),
                static_cast<double>(deviation));

    if (deviation > 0.01f) {
        std::printf("FAIL: client and server disagree by more than 1 cm\n");
        ++failures;
    }
    if (client.rollbacks() == 0) {
        std::printf("FAIL: no rollback happened, the lossy link should force at least one\n");
        ++failures;
    }
    if (client.snapshots_received() < ticks / 2) {
        std::printf("FAIL: too few snapshots arrived (head-of-line blocking?)\n");
        ++failures;
    }
    if (server.full_snapshots() == 0) {
        std::printf("FAIL: never fell back to a full snapshot despite lost acks\n");
        ++failures;
    }
    if (server.delta_snapshots() <= server.full_snapshots()) {
        std::printf("FAIL: delta snapshots are not the common case\n");
        ++failures;
    }

    if (failures != 0) {
        std::printf("FAIL: %d network checks failed\n", failures);
        return 1;
    }
    std::printf("PASS: prediction converges over a 30%% loss link with delta snapshots\n");
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
    if (max_solid_streak > 2 || max_frozen_streak > 120) {
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

int run_demo_test(const Options& opt) {
    int failures = 0;
    auto check = [&](const char* label, bool ok) {
        if (!ok) ++failures;
        std::printf("  %-46s %s\n", label, ok ? "ok" : "FAIL");
    };

    const std::string demo_path = "build/demo_test.bpd";
    const std::string params_a = opt.data_path;
    const std::string params_b = "build/perturbed.toml";

    // A second parameter set that differs only in acceleration.
    FILE* f = std::fopen(params_b.c_str(), "wb");
    if (!f) {
        std::fprintf(stderr, "error: cannot write %s\n", params_b.c_str());
        return 1;
    }
    std::fprintf(f, "[base]\naccel = 12.0\n");
    std::fclose(f);

    // Record a run.
    Options record = opt;
    record.map_path = "sim/data/maps/test_box.map";
    record.data_path = params_a;
    record.record_path = demo_path;
    record.ticks = 400;
    record.script = "jump";
    {
        World world;
        std::string error;
        if (!load_world(record, world, error)) {
            std::fprintf(stderr, "error: %s\n", error.c_str());
            return 1;
        }
        sim::DemoHeader header;
        header.build_id = sim::kBuildId;
        header.sim_version = sim::kSimVersion;
        header.protocol_version = sim::kProtocolVersion;
        header.tick_rate = sim::kDefaultTickRate;
        header.params_hash = world.params_hash;
        header.geometry_hash = world.collision.geometry_hash();
        header.map = record.map_path;
        for (const PlayerState& p : world.players) {
            sim::DemoPlayerInit init;
            init.pos = p.pos;
            init.vel = p.vel;
            init.speed_coefficient = p.speed_coefficient;
            init.slow_factor = p.slow_factor;
            init.crouched = p.crouched;
            init.impulses = p.impulses;
            header.players.push_back(init);
        }
        sim::DemoWriter writer;
        if (!writer.open(demo_path, header, error)) {
            std::fprintf(stderr, "error: %s\n", error.c_str());
            return 1;
        }
        for (int i = 0; i < record.ticks; ++i) {
            const MoveInput in = make_input(record.script, static_cast<uint32_t>(i));
            world.step({in}, kDt);
            sim::DemoTick tick;
            tick.tick = static_cast<uint32_t>(i);
            tick.state_hash = world.state_hash();
            tick.inputs.push_back(in);
            writer.add(tick);
        }
        if (!writer.close(error)) {
            std::fprintf(stderr, "error: %s\n", error.c_str());
            return 1;
        }
    }

    PlayOutcome same;
    std::string error;
    const bool played = play_file(demo_path, params_a, "", "", same, error);
    check("demo replays on the same build", played && same.mismatches == 0);
    check("no header mismatch on the same build", played && !same.header_mismatch);

    PlayOutcome changed;
    const bool played_b = play_file(demo_path, params_b, "", "", changed, error);
    check("changed movement.toml is detected", played_b && changed.mismatches > 0);
    if (played_b && changed.mismatches > 0) {
        std::printf("    first divergence at tick %u (of %u)\n", changed.first_bad, changed.ticks);
    }

    // Cross-machine comparison path: two dumps, one identical, one perturbed.
    PlayOutcome dump_a;
    PlayOutcome dump_b;
    play_file(demo_path, params_a, "", "build/dump_a.csv", dump_a, error);
    play_file(demo_path, params_a, "", "build/dump_b.csv", dump_b, error);
    play_file(demo_path, params_b, "", "build/dump_c.csv", changed, error);

    double dev = 0.0;
    uint32_t rows = 0;
    const bool same_ok = compare_dumps("build/dump_a.csv", "build/dump_b.csv", dev, rows, error);
    check("identical dumps compare to zero", same_ok && dev == 0.0 && rows > 0);

    double dev_changed = 0.0;
    const bool diff_ok = compare_dumps("build/dump_a.csv", "build/dump_c.csv", dev_changed, rows, error);
    check("perturbed dump exceeds 0.01 m", diff_ok && dev_changed > 0.01);
    if (diff_ok) std::printf("    max deviation with changed accel: %.4f m\n", dev_changed);

    if (failures != 0) {
        std::printf("FAIL: %d demo checks failed\n", failures);
        return 1;
    }
    std::printf("PASS: demos replay exactly and divergence is localised\n");
    return 0;
}

int run_bench(const Options& opt) {
    std::string error;
    World world;
    if (!load_world(opt, world, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }

    std::vector<double> samples;
    samples.reserve(static_cast<size_t>(opt.bench_ticks));

    for (int i = 0; i < opt.bench_ticks; ++i) {
        const MoveInput in = make_input("jump", static_cast<uint32_t>(i));
        const int64_t start = sim::now_ns();
        world.step({in}, kDt);
        const int64_t took = sim::elapsed_ns(start);
        samples.push_back(static_cast<double>(took) / 1000.0);
    }

    std::sort(samples.begin(), samples.end());
    double sum = 0.0;
    for (double s : samples) sum += s;
    const size_t p99 = static_cast<size_t>(static_cast<double>(samples.size()) * 0.99);
    std::printf("ticks  %d\n", opt.bench_ticks);
    std::printf("mean   %.2f us\n", sum / static_cast<double>(samples.size()));
    std::printf("p99    %.2f us  (budget 4000 us)\n",
                samples[std::min(p99, samples.size() - 1)]);
    std::printf("max    %.2f us\n", samples.back());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto need_value = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "error: %s needs a value\n", name);
                return nullptr;
            }
            return argv[++i];
        };

        if (arg == "--map") {
            const char* v = need_value("--map");
            if (!v) return 2;
            opt.map_path = v;
            opt.map_explicit = true;
        } else if (arg == "--data") {
            const char* v = need_value("--data");
            if (!v) return 2;
            opt.data_path = v;
        } else if (arg == "--ticks") {
            const char* v = need_value("--ticks");
            if (!v) return 2;
            opt.ticks = std::atoi(v);
        } else if (arg == "--script") {
            const char* v = need_value("--script");
            if (!v) return 2;
            opt.script = v;
        } else if (arg == "--hash") {
            opt.print_hash = true;
        } else if (arg == "--info") {
            opt.info = true;
        } else if (arg == "--selftest") {
            opt.selftest = true;
        } else if (arg == "--test-wall") {
            opt.test_wall = true;
        } else if (arg == "--test-ray") {
            opt.test_ray = true;
        } else if (arg == "--test-step") {
            opt.test_step = true;
        } else if (arg == "--test-parkour") {
            opt.test_parkour = true;
        } else if (arg == "--test-bindings") {
            opt.test_bindings = true;
        } else if (arg == "--keys") {
            opt.print_keys = true;
        } else if (arg == "--keybinds") {
            const char* v = need_value("--keybinds");
            if (!v) return 2;
            opt.keybinds_path = v;
        } else if (arg == "--record") {
            const char* v = need_value("--record");
            if (!v) return 2;
            opt.record_path = v;
        } else if (arg == "--play") {
            const char* v = need_value("--play");
            if (!v) return 2;
            opt.play_path = v;
        } else if (arg == "--dump") {
            const char* v = need_value("--dump");
            if (!v) return 2;
            opt.dump_path = v;
        } else if (arg == "--compare") {
            const char* v = need_value("--compare");
            if (!v) return 2;
            opt.compare_a = v;
            if (i + 1 >= argc) {
                std::fprintf(stderr, "error: --compare needs two files\n");
                return 2;
            }
            opt.compare_b = argv[++i];
        } else if (arg == "--tolerance") {
            const char* v = need_value("--tolerance");
            if (!v) return 2;
            opt.tolerance = std::atof(v);
        } else if (arg == "--test-demo") {
            opt.test_demo = true;
        } else if (arg == "--test-edge") {
            opt.test_edge = true;
        } else if (arg == "--test-stuck") {
            opt.test_stuck = true;
        } else if (arg == "--test-soak") {
            opt.test_soak = true;
        } else if (arg == "--test-jump") {
            opt.test_jump = true;
        } else if (arg == "--test-net") {
            opt.test_net = true;
        } else if (arg == "--test-health") {
            opt.test_health = true;
        } else if (arg == "--bench") {
            opt.bench = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') opt.bench_ticks = std::atoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            usage();
            return 0;
        } else {
            std::fprintf(stderr, "error: unknown argument %s\n", arg.c_str());
            usage();
            return 2;
        }
    }

    if (opt.test_health) return run_health_test(opt);
    if (opt.test_net) return run_net_test(opt);
    if (opt.test_jump) return run_jump_test(opt);
    if (opt.test_soak) return run_soak_test(opt);
    if (opt.test_stuck) return run_stuck_test(opt);
    if (opt.test_edge) return run_edge_test(opt);
    if (opt.test_demo) return run_demo_test(opt);
    if (!opt.record_path.empty()) return run_record(opt);
    if (!opt.play_path.empty()) return run_play(opt);
    if (!opt.compare_a.empty()) return run_compare(opt);
    if (opt.print_keys) return run_keys(opt);
    if (opt.info) return run_info(opt);
    if (opt.selftest) return run_selftest(opt);
    if (opt.test_wall) return run_wall_test(opt);
    if (opt.test_ray) return run_ray_test(opt);
    if (opt.test_step) return run_step_test(opt);
    if (opt.test_parkour) return run_parkour_test(opt);
    if (opt.test_bindings) return run_keybind_test(opt);
    if (opt.bench) return run_bench(opt);

    std::string error;
    World world;
    if (!load_world(opt, world, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }

    for (int i = 0; i < opt.ticks; ++i) {
        const MoveInput in = make_input(opt.script, static_cast<uint32_t>(i));
        world.step({in}, kDt);
        if (opt.print_hash) std::printf("%6d  %016llx\n", i, static_cast<unsigned long long>(world.state_hash()));
    }

    const PlayerState& p = world.players[0];
    std::printf("ticks      %d\n", opt.ticks);
    std::printf("pos        (%.4f, %.4f, %.4f)  [m]\n", p.pos.x, p.pos.y, p.pos.z);
    std::printf("vel        (%.4f, %.4f, %.4f)  [m/s]\n", p.vel.x, p.vel.y, p.vel.z);
    std::printf("on_ground  %s\n", p.on_ground ? "true" : "false");
    std::printf("hash       %016llx\n", static_cast<unsigned long long>(world.state_hash()));
    return 0;
}
