#include "sim/game/game_step.h"

#include "sim/game/shoot.h"
#include "sim/game/weapon.h"

namespace sim {

void game_step(World& world, const MoveInput* inputs, int input_count, float dt,
               const GameStep& step) {
    // 1. Movement. World::step owns the tick counter and the held/edge mirrors,
    //    so reload_edge is computed there from the value it overwrites.
    world.step(inputs, input_count, dt, step.events);

    if (inputs == nullptr || input_count <= 0) return;
    const uint32_t shot_tick = world.tick - 1;
    const int players = static_cast<int>(world.players.size());
    const int count = input_count < players ? input_count : players;

    // 2. Weapons. A player with no weapon definition simply has no weapon step.
    for (int i = 0; i < count; ++i) {
        PlayerState& player = world.players[static_cast<size_t>(i)];
        const WeaponDef* def = world.weapons.find_by_slot(player.weapon_id);
        if (def == nullptr) continue;
        update_weapon(player, i, inputs[i], *def, world, step.histories,
                      step.history_count, shot_tick, step.rewind,
                      i == 0 ? step.report : nullptr, step.events);
    }

    // 3. Hitbox history, after the weapon: a shot rewinds into older frames,
    //    and today's frame is what the next shot will read.
    if (step.histories != nullptr) {
        const int frames = step.history_count < players ? step.history_count : players;
        for (int i = 0; i < frames; ++i) {
            step.histories[i].record(world.players[static_cast<size_t>(i)], world.params,
                                     world.tick);
        }
    }
}

}  // namespace sim