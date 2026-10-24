#pragma once

#include <cstdint>

#include "sim/core/world.h"
#include "sim/game/combat.h"
#include "sim/game/hitbox.h"
#include "sim/game/weapon.h"

namespace sim {

// Damage of one hit before it is applied: falloff x zone x penetration, rounded
// once. Shared by the shot path and the TTK table so the two cannot drift.
int shot_damage(float base, HitZone zone, const MovementParams& params, float penetration_scale);

struct ShotReport {
    int shots = 0;
    int hits = 0;
    int damage = 0;
};

// Advances one player's weapon by one tick and fires when the trigger is held
// and the cadence allows it.
//
// Server and client both call this with the same input, so ammo and timers agree
// on both sides; only a world that actually contains the victim (the server, or
// a solo world with bots) can deal damage. That is why damage is still
// server-authoritative (v0.4 A4): the client's shot is a trace into a world with
// one player in it.
//
// histories is the per-player 64-tick hitbox ring (entity i = histories[i]); the
// first kMaxShotTargets entries are used. rewind is the shot's age in ticks
// (sim/game/rewind.h) - the caller owns that policy.
void update_weapon(PlayerState& shooter, int shooter_index, const MoveInput& input,
                   const WeaponDef& def, World& world, const HitboxHistory* histories,
                   int history_count, uint32_t tick, float rewind, ShotReport* report,
                   EventQueue* events = nullptr);

}  // namespace sim
