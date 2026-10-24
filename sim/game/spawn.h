#pragma once

#include "sim/core/world.h"

namespace sim {

// Plan 5.3: one place that turns "this player is (re)spawning" into a
// PlayerState, so joining and respawning cannot drift apart. Anything a fresh
// player should not carry (armour, timers, half a magazine) is cleared here.
PlayerState make_spawn_player(const MovementParams& params, const CombatParams& combat,
                              const WeaponParams& weapons, const Vec3& pos);

}  // namespace sim
