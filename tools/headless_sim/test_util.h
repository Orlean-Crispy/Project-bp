// World loading shared by main.cpp and the demo gates.
#pragma once

#include <string>

#include "sim/core/world.h"
#include "sim/move/pmove.h"

struct Options;

bool load_world_from(const std::string& map_path, const std::string& data_path,
                     sim::World& world, std::string& error);

bool load_world(const Options& opt, sim::World& world, std::string& error);
sim::MoveInput make_input(const std::string& script, uint32_t tick);
