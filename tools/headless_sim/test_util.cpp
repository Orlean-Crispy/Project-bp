#include "test_util.h"

#include "options.h"

#include <cstdio>

#include "sim/collision/map_parse.h"
#include "sim/data/config.h"

using sim::MapData;
using sim::MoveInput;
using sim::PlayerState;
using sim::World;

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

