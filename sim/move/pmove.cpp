#include "sim/move/pmove.h"

#include <algorithm>
#include <cmath>

namespace sim {
namespace {

constexpr float kGroundProbe = 0.02f;  // 2 cm probe, in metres

Vec3 wish_direction(const MoveInput& input) {
    const float s = std::sin(input.yaw);
    const float c = std::cos(input.yaw);
    // yaw 0 looks down +X; right is -Y (right handed, Z up).
    Vec3 dir(input.forward * c - input.right * s,
             input.forward * s + input.right * c,
             0.0f);
    const float len = length(dir);
    if (len > 1.0f) dir = dir / len;
    return dir;
}

void apply_friction(PlayerState& player, const MovementParams& params, float dt) {
    const float speed = length(player.vel);
    if (speed < 1e-4f) return;

    const float control = speed < params.stop_speed ? params.stop_speed : speed;
    const float drop = control * params.friction * dt;
    float new_speed = speed - drop;
    if (new_speed < 0.0f) new_speed = 0.0f;
    if (new_speed != speed) {
        player.vel *= new_speed / speed;
    }
}

void accelerate(Vec3& vel, const Vec3& wishdir, float wishspeed, float accel, float dt) {
    const float current = dot(vel, wishdir);
    const float add = wishspeed - current;
    if (add <= 0.0f) return;

    float accelspeed = accel * dt * wishspeed;
    if (accelspeed > add) accelspeed = add;
    vel += wishdir * accelspeed;
}

bool probe_ground(const PlayerState& player, const CollisionWorld& world,
                  const MovementParams& params, float& out_z) {
    Vec3 mins;
    Vec3 maxs;
    player_hull(player, params, mins, maxs);
    const TraceResult tr = world.trace_box(player.pos, player.pos - Vec3(0.0f, 0.0f, kGroundProbe),
                                           mins, maxs);
    if (tr.start_solid) {
        out_z = player.pos.z;
        return true;
    }
    if (tr.fraction < 1.0f) {
        out_z = tr.end_pos.z;
        return true;
    }
    return false;
}

}  // namespace

float effective_max_speed(const PlayerState& player, const MovementParams& params) {
    float speed = params.base_speed * player.speed_coefficient * player.slow_factor;
    if (player.crouched) speed *= params.crouch_speed_scale;
    return speed;
}

void player_hull(const PlayerState& player, const MovementParams& params, Vec3& mins, Vec3& maxs) {
    const float height = player.crouched ? params.crouch_height : params.stand_height;
    mins = Vec3(-params.player_radius, -params.player_radius, 0.0f);
    maxs = Vec3(params.player_radius, params.player_radius, height);
}

void pmove(PlayerState& player, const MoveInput& input, const CollisionWorld& world,
           const MovementParams& params, float dt) {
    Vec3 mins;
    Vec3 maxs;
    player_hull(player, params, mins, maxs);

    // Crouch state first: the hull is needed by every trace below.
    player.crouched = input.crouch;

    float ground_z = player.pos.z;
    player.on_ground = probe_ground(player, world, params, ground_z);
    if (player.on_ground) player.pos.z = ground_z;

    if (input.jump && player.on_ground) {
        player.vel.z = params.jump_velocity;
        player.on_ground = false;
    }

    if (player.on_ground) {
        apply_friction(player, params, dt);
        const Vec3 wishdir = wish_direction(input);
        const float wishspeed = effective_max_speed(player, params);
        accelerate(player.vel, wishdir, wishspeed, params.accel, dt);
        if (player.vel.z < 0.0f) player.vel.z = 0.0f;
    } else {
        const Vec3 wishdir = wish_direction(input);
        const float wishspeed = effective_max_speed(player, params);
        // Air control is deliberately weak: Apex-like redirect without Quake air
        // strafing being the only way to move fast.
        accelerate(player.vel, wishdir, wishspeed, params.air_accel, dt);
        player.vel.z -= params.gravity * dt;
    }

    const SlideResult moved = slide_move(world, player.pos, player.vel, mins, maxs, dt, params);
    player.pos = moved.pos;
    player.vel = moved.vel;

    if (probe_ground(player, world, params, ground_z)) {
        player.pos.z = ground_z;
        player.on_ground = true;
        if (player.vel.z < 0.0f) player.vel.z = 0.0f;
        if (params.recharge_on_ground && player.impulses < params.max_impulses) {
            ++player.impulses;
        }
    } else {
        player.on_ground = false;
    }

    ++player.tick;
}

}  // namespace sim
