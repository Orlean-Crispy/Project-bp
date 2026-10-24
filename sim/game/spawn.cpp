#include "sim/game/spawn.h"

#include "sim/game/weapon.h"

namespace sim {

PlayerState make_spawn_player(const MovementParams& params, const CombatParams& combat,
                              const WeaponParams& weapons, const Vec3& pos) {
    (void)combat;  // armour is part of the kit decision (M7); a spawn starts bare
    PlayerState player;
    player.pos = pos;
    player.vel = Vec3{};
    player.yaw = 0.0f;
    player.impulses = params.max_impulses;
    player.state = ParkourState::Ground;
    if (const WeaponDef* def = weapons.find_by_slot(0)) equip_weapon(player, *def);
    return player;
}

}  // namespace sim
