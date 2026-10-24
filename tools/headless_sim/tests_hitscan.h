// Hitscan gate (M6.3): a ray against the world and against player volumes.
// E1: aimed at the chest it hits; one box width off it misses.
#pragma once

struct Options;

int run_hitscan_test(const Options& opt);
