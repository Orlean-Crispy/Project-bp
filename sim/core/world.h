#pragma once

#include <cstdint>
#include <vector>

#include "sim/collision/trace.h"
#include "sim/core/event.h"
#include "sim/game/combat.h"
#include "sim/move/pmove.h"

namespace sim {

struct World {
    CollisionWorld collision;
    std::vector<PlayerState> players;
    MovementParams params;
    CombatParams combat;  // R8: the non-movement rules that must match to join
    uint64_t params_hash = 0;
    uint32_t tick = 0;

    // events is optional: the simulation stays pure, and only a caller that owns
    // an EventQueue (bridge, future server broadcast) pays for emission.
    void step(const std::vector<MoveInput>& inputs, float dt, EventQueue* events = nullptr);
    // R6: the client predicts one player per tick. This is exactly step({input})
    // without building a one-element vector for every tick.
    void step_single(const MoveInput& input, float dt, EventQueue* events = nullptr);
    uint64_t state_hash() const;
};

}  // namespace sim
