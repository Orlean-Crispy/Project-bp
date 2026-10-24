#include "sim/game/respawn.h"

#include "sim/game/spawn.h"

namespace sim {

bool tick_respawn(World& world, std::size_t index, uint16_t& timer, const Vec3& spawn) {
    if (index >= world.players.size()) return false;
    PlayerState& player = world.players[index];
    if (player.vitals.health > 0) {
        timer = 0;
        return false;
    }
    if (timer == 0) {
        timer = static_cast<uint16_t>(kRespawnTicks);
        return false;
    }
    if (--timer > 0) return false;

    const PlayerState fresh =
        make_spawn_player(world.params, world.combat, world.weapons, spawn);
    const bool active = player.active;
    player = fresh;
    player.active = active;  // the slot is still this client's
    return true;
}

}  // namespace sim
