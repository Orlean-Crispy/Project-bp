// Weapon data gates (M6.1): the first table a designer will edit, so a typo or
// an impossible value has to fail here instead of in a match.
#pragma once

struct Options;

int run_weapon_test(const Options& opt);
