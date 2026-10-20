// headless_sim - the terminal runner that makes the simulation testable without
// Godot, a GPU or a display (plan section 3.2).

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
#include "sim/net/udp_hub.h"
#include "sim/net/udp_transport.h"

// Allocation counter for the "steady state allocates nothing" rule (iron law E).
// These replace the global operators for this test executable only.
static unsigned long long g_allocations = 0;
unsigned long long allocation_count() { return g_allocations; }

void* operator new(std::size_t size) {
    ++g_allocations;
    if (void* p = std::malloc(size == 0 ? 1 : size)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return operator new(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    ++g_allocations;
    return std::malloc(size == 0 ? 1 : size);
}
// The counters deliberately pair malloc with free; GCC cannot see through the
// replacement and would flag the sized forms when callers inline.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
#pragma GCC diagnostic pop

#include "options.h"
unsigned long long allocation_count();
#include "test_util.h"
#include "tests_demo.h"
#include "tests_move.h"
#include "tests_net.h"

namespace {

using sim::CollisionWorld;
using sim::MapData;
using sim::MoveInput;
using sim::MovementParams;
using sim::PlayerState;
using sim::SlideResult;
using sim::World;

constexpr float kDt = 1.0f / 64.0f;



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
        "  --test-hitbox     verify the 64-tick hitbox history and the 3-zone multipliers\n"
        "  --test-events     event channel: jump/land emission, determinism, ring overflow\n"
        "  --bench [n]       time n ticks and report mean/p99/max\n"
        "  --players <n>     with --bench: simulate n players (default 1)\n");
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
        } else if (arg == "--test-hitbox") {
            opt.test_hitbox = true;
        } else if (arg == "--test-events") {
            opt.test_events = true;
        } else if (arg == "--players") {
            const char* v = need_value("--players");
            if (!v) return 2;
            opt.bench_players = std::atoi(v);
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

    if (opt.test_hitbox) return run_hitbox_test(opt);
    if (opt.test_events) return run_event_test(opt);
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
