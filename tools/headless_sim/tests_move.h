// Movement, world and bench gates.
#pragma once

#include <string>

struct Options;

int run_info(const Options& opt);
int run_selftest(const Options& opt);
int run_wall_test(const Options& opt);
int run_ray_test(const Options& opt);
int run_step_test(const Options& opt);
int run_parkour_test(const Options& opt);
int run_keys(const Options& opt);
int run_keybind_test(const Options& opt);
int run_health_test(const Options& opt);
int run_event_test(const Options& opt);
int run_hitbox_test(const Options& opt);
int run_jump_test(const Options& opt);
int run_soak_test(const Options& opt);
int run_stuck_test(const Options& opt);
int run_edge_test(const Options& opt);
int run_bench(const Options& opt);
