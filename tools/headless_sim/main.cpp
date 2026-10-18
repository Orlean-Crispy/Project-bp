// headless_sim - the terminal runner that makes the simulation testable without
// Godot, a GPU or a display (plan section 3.2).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "sim/core/tick.h"
#include "sim/core/world.h"
#include "sim/input/bindings.h"

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
    bool test_bindings = false;
    bool print_keys = false;
    bool map_explicit = false;
    std::string keybinds_path = "sim/data/keybinds.toml";
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
        "  --bench [n]       time n ticks and report mean/p99/max\n");
}

bool load_world(const Options& opt, World& world, std::string& error) {
    MapData map;
    if (!sim::parse_map_file(opt.map_path, map, error)) return false;

    std::vector<std::string> warnings;
    if (!sim::load_movement_params(opt.data_path, world.params, warnings, error)) return false;
    for (const std::string& w : warnings) std::fprintf(stderr, "warning: %s\n", w.c_str());

    world.collision.build(map);
    world.params_hash = world.params.hash();

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
