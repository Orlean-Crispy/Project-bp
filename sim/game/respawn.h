#pragma once

#include <cstdint>

#include "sim/core/world.h"

namespace sim {

// Plan 6.9: five seconds is long enough to read the kill feed and short enough
// that a duel does not stall. A respawn is a *new* player at a spawn point, not
// a healed corpse: make_spawn_player() decides what that means.
constexpr int kRespawnTicks = 320;

// Advances one dead player's respawn timer. Returns true on the tick they came
// back, so the caller can reset its hitbox history (the old frames are at the
// old position and would still be inside a rewind window).
bool tick_respawn(World& world, std::size_t index, uint16_t& timer, const Vec3& spawn);

}  // namespace sim
