// Recording / replay gates (M4).
#pragma once

#include <string>

struct Options;

int run_record(const Options& opt);
int run_play(const Options& opt);
int run_compare(const Options& opt);
int run_demo_test(const Options& opt);
