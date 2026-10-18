#pragma once

#include "sim/collision/trace.h"
#include "sim/data/config.h"

namespace sim {

struct MoveInput {
    float forward = 0.0f;  // -1..1, +X is forward at yaw 0
    float right = 0.0f;    // -1..1
    float yaw = 0.0f;      // radians
    bool jump = false;
    bool crouch = false;
    bool dash = false;
};

struct PlayerState {
    Vec3 pos;  // feet position
    Vec3 vel;
    bool on_ground = false;
    bool crouched = false;
    int impulses = 4;
    uint32_t tick = 0;

    // Multipliers from weapon (5.10) and damage (6.10). Kept in the state so the
    // rollback snapshot can never silently replay with a different max speed.
    float speed_coefficient = 1.0f;
    float slow_factor = 1.0f;
};

// max speed = base * weapon * slow * crouch (plan section 5.10).
float effective_max_speed(const PlayerState& player, const MovementParams& params);

void player_hull(const PlayerState& player, const MovementParams& params, Vec3& mins, Vec3& maxs);

void pmove(PlayerState& player, const MoveInput& input, const CollisionWorld& world,
           const MovementParams& params, float dt);

}  // namespace sim
