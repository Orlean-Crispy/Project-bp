#include "sim/game/spawn.h"

#include "sim/game/weapon.h"

namespace sim {

PlayerState make_spawn_player(const MovementParams& params, const CombatParams& combat,
                              const WeaponParams& weapons, const Vec3& pos) {
    PlayerState player;
    player.pos = pos;
    player.vel = Vec3{};
    player.yaw = 0.0f;
    // AUD-44: health comes from the data, not from the PlayerVitals default. A
    // server that tuned max_health to 150 used to spawn everyone at 100 and then
    // refuse to heal the difference.
    player.vitals.health = combat.max_health;
    player.impulses = params.max_impulses;
    player.state = ParkourState::Ground;
    if (const WeaponDef* def = weapons.find_by_slot(0)) equip_weapon(player, *def);
    return player;
}

}  // namespace sim
