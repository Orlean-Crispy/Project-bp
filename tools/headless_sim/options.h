// Command-line options shared by main.cpp and the test translation units.
#pragma once

#include <string>

struct Options {
    std::string map_path = "sim/data/maps/test_box.map";
    std::string data_path = "sim/data/movement.toml";
    std::string script = "jump";
    int ticks = 600;
    int bench_ticks = 1000;
    int bench_players = 1;
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
    bool test_hitbox = false;
    bool test_events = false;
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
