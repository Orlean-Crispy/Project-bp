// Damage gate (M6.6/M6.7 framework): pull the trigger, spend a round, hit the
// hull, lose health. The numbers come from weapons.toml and combat.toml, so this
// gate measures the pipeline, not a balance decision.
#pragma once

struct Options;

int run_damage_test(const Options& opt);
