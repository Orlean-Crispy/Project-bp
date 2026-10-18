#pragma once

#include <cstdint>
#include <vector>

#include "sim/collision/trace.h"
#include "sim/move/pmove.h"

namespace sim {

struct World {
    CollisionWorld collision;
    std::vector<PlayerState> players;
    MovementParams params;
    uint64_t params_hash = 0;
    uint32_t tick = 0;

    void step(const std::vector<MoveInput>& inputs, float dt);
    uint64_t state_hash() const;
};

}  // namespace sim
