
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
#include "tests_demo.h"

using sim::MapData;
using sim::MoveInput;
using sim::PlayerState;
using sim::Vec3;
using sim::World;

namespace {
constexpr float kDt = 1.0f / 64.0f;
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

// One definition of "write a demo": the header pins the build, the rule hashes,
// the map and the initial state, then every tick stores the input and the world
// hash. --record and --test-demo both go through here, so the recording path the
// gate exercises can never drift from the one the tool uses (audit R13).
bool record_demo(World& world, const std::string& map_path, const std::string& script, int ticks,
                 const std::string& out_path, sim::DemoHeader& header, std::string& error) {
    header = sim::DemoHeader{};
    header.build_id = sim::kBuildId;
    header.sim_version = sim::kSimVersion;
    header.protocol_version = sim::kProtocolVersion;
    header.tick_rate = sim::kDefaultTickRate;
    header.params_hash = world.params_hash;
    header.geometry_hash = world.collision.geometry_hash();
    header.map = map_path;
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
    if (!writer.open(out_path, header, error)) return false;

    for (int i = 0; i < ticks; ++i) {
        const MoveInput in = make_input(script, static_cast<uint32_t>(i));
        world.step({in}, kDt);
        sim::DemoTick tick;
        tick.tick = static_cast<uint32_t>(i);
        tick.state_hash = world.state_hash();
        tick.inputs.push_back(in);
        writer.add(tick);
    }
    return writer.close(error);
}

int run_record(const Options& opt) {
    World world;
    std::string error;
    if (!load_world(opt, world, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }

    sim::DemoHeader header;
    if (!record_demo(world, opt.map_path, opt.script, opt.ticks, opt.record_path, header, error)) {
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
        if (!record_demo(world, record.map_path, record.script, record.ticks, demo_path, header,
                         error)) {
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
