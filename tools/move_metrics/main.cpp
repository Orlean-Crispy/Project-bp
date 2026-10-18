// move_metrics - turns "the feel is right" into four numbers that can be diffed
// across builds (plan section 5.8). Shares the same headless runner as the
// determinism tests on purpose.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "sim/core/world.h"

namespace {

using sim::MoveInput;
using sim::PlayerState;
using sim::World;

constexpr float kDt = 1.0f / 64.0f;

struct Row {
    std::string metric;
    double value;
    std::string unit;
    std::string note;
};

bool init_world(World& world, const std::string& map_path, const std::string& data_path,
                std::string& error) {
    sim::MapData map;
    if (!sim::parse_map_file(map_path, map, error)) return false;
    std::vector<std::string> warnings;
    if (!sim::load_movement_params(data_path, world.params, warnings, error)) return false;
    for (const std::string& w : warnings) std::fprintf(stderr, "warning: %s\n", w.c_str());
    world.collision.build(map);
    world.params_hash = world.params.hash();

    PlayerState p;
    p.pos = sim::Vec3(-8.0f, 5.0f, 0.01f);
    p.impulses = world.params.max_impulses;
    world.players.push_back(p);
    return true;
}

double horizontal_speed(const sim::Vec3& v) {
    return std::sqrt(static_cast<double>(v.x) * v.x + static_cast<double>(v.y) * v.y);
}

void reset(World& world) {
    PlayerState p;
    p.pos = sim::Vec3(-8.0f, 5.0f, 0.01f);
    p.impulses = world.params.max_impulses;
    world.players.assign(1, p);
    world.tick = 0;
}

// 1. Peak height of a standing jump.
double measure_jump_height(World& world) {
    reset(world);
    double peak = 0.0;
    for (int t = 0; t < 200; ++t) {
        MoveInput in;
        in.jump = (t == 5);
        world.step({in}, kDt);
        peak = std::max(peak, static_cast<double>(world.players[0].pos.z));
    }
    return peak;
}

// 2. Horizontal distance covered by a running jump (takeoff to landing).
double measure_jump_distance(World& world) {
    reset(world);
    double start_x = 0.0;
    bool airborne = false;
    for (int t = 0; t < 400; ++t) {
        MoveInput in;
        in.forward = 1.0f;
        in.jump = (t == 120);
        const bool was_ground = world.players[0].on_ground;
        world.step({in}, kDt);
        const bool now_ground = world.players[0].on_ground;
        if (was_ground && !now_ground && !airborne) {
            airborne = true;
            start_x = world.players[0].pos.x;
        }
        if (airborne && now_ground && t > 130) {
            return world.players[0].pos.x - start_x;
        }
    }
    return 0.0;
}

// 3. Speed gained by an air strafe (jump + forward + right, yaw turning).
// Ground strafing cannot exceed max speed by construction, so measuring it on
// the ground would always report zero and prove nothing.
double measure_strafe_gain(World& world) {
    reset(world);
    for (int t = 0; t < 100; ++t) {
        MoveInput in;
        in.forward = 1.0f;
        world.step({in}, kDt);
    }

    MoveInput jump;
    jump.forward = 1.0f;
    jump.jump = true;
    world.step({jump}, kDt);

    const double takeoff = horizontal_speed(world.players[0].vel);
    const float turn_per_tick = 150.0f * kDt * 3.14159265f / 180.0f;

    for (int t = 0; t < 128; ++t) {
        MoveInput in;
        in.forward = 1.0f;
        in.right = 1.0f;
        in.yaw = static_cast<float>(t) * turn_per_tick;
        world.step({in}, kDt);
        if (world.players[0].on_ground) break;
    }
    const double landing = horizontal_speed(world.players[0].vel);
    return landing - takeoff;
}

// 4. Ticks from standstill to 99% of max speed.
int measure_accel_ticks(World& world) {
    reset(world);
    const float target = 0.99f * sim::effective_max_speed(world.players[0], world.params);
    for (int t = 0; t < 400; ++t) {
        MoveInput in;
        in.forward = 1.0f;
        world.step({in}, kDt);
        if (horizontal_speed(world.players[0].vel) >= target) return t + 1;
    }
    return -1;
}

}  // namespace

int main(int argc, char** argv) {
    std::string map_path = "sim/data/maps/metrics_flat.map";
    std::string data_path = "sim/data/movement.toml";
    std::string out_path;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--map" && i + 1 < argc) {
            map_path = argv[++i];
        } else if (arg == "--data" && i + 1 < argc) {
            data_path = argv[++i];
        } else if (arg == "--out" && i + 1 < argc) {
            out_path = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::printf("move_metrics [--map file] [--data file] [--out csv]\n");
            return 0;
        } else {
            std::fprintf(stderr, "error: unknown argument %s\n", arg.c_str());
            return 2;
        }
    }

    World world;
    std::string error;
    if (!init_world(world, map_path, data_path, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }

    std::vector<Row> rows;
    rows.push_back({"jump_height", measure_jump_height(world), "m", "standing jump, peak z"});
    rows.push_back({"jump_distance", measure_jump_distance(world), "m", "running jump, horizontal"});
    rows.push_back({"strafe_gain", measure_strafe_gain(world), "m/s", "64 ticks of pure side strafe"});
    rows.push_back({"accel_ticks", static_cast<double>(measure_accel_ticks(world)), "ticks",
                    "0 to 99% of max speed"});

    std::string csv = "metric,value,unit,note\n";
    for (const Row& r : rows) {
        char line[256];
        std::snprintf(line, sizeof(line), "%s,%.4f,%s,%s\n", r.metric.c_str(), r.value,
                      r.unit.c_str(), r.note.c_str());
        csv += line;
    }

    std::printf("%s", csv.c_str());

    if (!out_path.empty()) {
        FILE* f = std::fopen(out_path.c_str(), "wb");
        if (!f) {
            std::fprintf(stderr, "error: cannot write %s\n", out_path.c_str());
            return 1;
        }
        std::fwrite(csv.data(), 1, csv.size(), f);
        std::fclose(f);
        std::fprintf(stderr, "wrote %s\n", out_path.c_str());
    }
    return 0;
}
