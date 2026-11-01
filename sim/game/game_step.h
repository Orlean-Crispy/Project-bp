#pragma once

// One authoritative tick, in one place (ADR 0008 / AUD-36).
//
// Before this existed, the server, the client's prediction, solo play and the
// demo each glued World::step + update_weapon together themselves, with slightly
// different arguments. That is how the reload press spent months never firing in
// production while the unit test was green (lesson 18).
//
// Phase order is fixed and must not be reordered without its own change:
//   1. movement  World::step        (mirrors held/edge states, emits jump/land)
//   2. weapon    update_weapon      (tick = world.tick - 1, the command's tick)
//   3. hitbox    HitboxHistory::record (after the weapon: shots rewind to old frames)
//
// Deliberately NOT here: respawn (server-only post phase) and snapshot sending.

#include <vector>

#include "sim/core/event.h"
#include "sim/core/world.h"
#include "sim/game/hitbox.h"

namespace sim {

struct ShotReport;

struct GameStep {
    EventQueue* events = nullptr;  // presentation only; the server passes null
    HitboxHistory* histories = nullptr;  // one per player, or null (solo play)
    int history_count = 0;
    float rewind = 1.0f;                 // rewind_ticks(one-way latency)
    ShotReport* report = nullptr;        // diagnostics, player 0 only
};

void game_step(World& world, const MoveInput* inputs, int input_count, float dt,
               const GameStep& step);

inline void game_step(World& world, const std::vector<MoveInput>& inputs, float dt,
                      const GameStep& step) {
    game_step(world, inputs.data(), static_cast<int>(inputs.size()), dt, step);
}

}  // namespace sim