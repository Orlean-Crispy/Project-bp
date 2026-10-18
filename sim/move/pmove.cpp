#include "sim/move/pmove.h"

#include <algorithm>
#include <cmath>

namespace sim {
namespace {

constexpr float kGroundProbe = 0.02f;   // 2 cm
constexpr float kProbeBack = 0.05f;     // start the wall probe slightly behind
constexpr float kProbeForward = 0.15f;  // ...so touching a wall still registers
constexpr float kVerticalNormal = 0.7f; // |n.z| above this is floor/ceiling, not wall

float horizontal_speed(const Vec3& v) {
    return std::sqrt(v.x * v.x + v.y * v.y);
}

Vec3 facing_direction(const MoveInput& input) {
    return Vec3(std::cos(input.yaw), std::sin(input.yaw), 0.0f);
}

Vec3 wish_direction(const MoveInput& input) {
    const float s = std::sin(input.yaw);
    const float c = std::cos(input.yaw);
    // yaw 0 looks down +X; in a right-handed Z-up frame the right vector is
    // (sin yaw, -cos yaw), so the right input contributes with a MINUS on Y.
    Vec3 dir(input.forward * c + input.right * s,
             input.forward * s - input.right * c,
             0.0f);
    if (length(dir) > 1.0f) dir = normalized(dir);
    return dir;
}

void apply_friction(PlayerState& player, const MovementParams& params, float scale, float dt) {
    const float speed = horizontal_speed(player.vel);
    if (speed < 1e-4f) return;

    const float control = speed < params.stop_speed ? params.stop_speed : speed;
    const float drop = control * params.friction * scale * dt;
    float new_speed = speed - drop;
    if (new_speed < 0.0f) new_speed = 0.0f;
    if (new_speed != speed) {
        const float k = new_speed / speed;
        player.vel.x *= k;
        player.vel.y *= k;
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

bool hull_fits(const CollisionWorld& world, const Vec3& pos, const Vec3& mins, const Vec3& maxs) {
    const TraceResult tr = world.trace_box(pos, pos, mins, maxs);
    return !tr.start_solid;
}

// If the box somehow ends up inside geometry, every later move starts solid and
// the player freezes with a working camera. Push it out deterministically: up
// first (sinking into a step or the floor is the common case), then sideways.
Vec3 resolve_penetration(const CollisionWorld& world, const Vec3& pos, const Vec3& mins,
                         const Vec3& maxs) {
    for (int i = 1; i <= 12; ++i) {
        const Vec3 p = pos + Vec3(0.0f, 0.0f, 0.1f * static_cast<float>(i));
        if (hull_fits(world, p, mins, maxs)) return p;
    }
    const Vec3 dirs[4] = {Vec3(1, 0, 0), Vec3(-1, 0, 0), Vec3(0, 1, 0), Vec3(0, -1, 0)};
    for (int i = 1; i <= 8; ++i) {
        const float d = 0.1f * static_cast<float>(i);
        for (const Vec3& dir : dirs) {
            const Vec3 p = pos + dir * d;
            if (hull_fits(world, p, mins, maxs)) return p;
        }
    }
    return pos;
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

struct WallProbe {
    bool hit = false;
    Vec3 normal;
    const Brush* brush = nullptr;
};

// Looks for a vertical surface within reach along dir. Only wall-like normals
// count, so standing next to a crate does not start a wall run on the floor.
WallProbe probe_wall(const CollisionWorld& world, const Vec3& pos, const Vec3& dir,
                     const Vec3& mins, const Vec3& maxs) {
    WallProbe result;
    if (length(dir) < 1e-4f) return result;
    const Vec3 unit = normalized(dir);
    const TraceResult tr = world.trace_box(pos - unit * kProbeBack, pos + unit * kProbeForward,
                                           mins, maxs);
    if (tr.start_solid || tr.fraction >= 1.0f) return result;
    if (std::fabs(tr.normal.z) > kVerticalNormal) return result;
    result.hit = true;
    result.normal = tr.normal;
    result.brush = tr.brush;
    return result;
}

void try_start_slide(PlayerState& player, const MoveInput& input, const MovementParams& params) {
    if (player.state == ParkourState::Slide) return;
    if (!input.slide || player.slide_was_down) return;
    if (!player.on_ground || player.impulses <= 0) return;
    if (horizontal_speed(player.vel) < params.slide_min_speed) return;

    player.state = ParkourState::Slide;
    player.state_ticks = 0;
    player.impulses = std::max(0, player.impulses - 1);

    const float speed = horizontal_speed(player.vel);
    if (speed > 1e-4f) {
        const float boost = speed * params.slide_entry_boost;
        player.vel.x *= boost / speed;
        player.vel.y *= boost / speed;
    }
    player.crouched = true;
}

bool update_slide(PlayerState& player, const MoveInput& input, const MovementParams& params,
                  float hz) {
    ++player.state_ticks;
    const float speed = horizontal_speed(player.vel);
    const int max_ticks = static_cast<int>(params.slide_max_duration * hz + 0.5f);

    if (!player.on_ground) return false;
    if (speed < params.slide_min_speed * 0.5f) return false;
    if (player.state_ticks > max_ticks) return false;
    if (!input.slide && player.state_ticks > 8) return false;
    return true;
}

// A vault is only legal when the obstacle top is above step_height (otherwise
// step-up handles it) and below vault_max_height (otherwise it is a wall), and
// there is standing headroom on top of it.
struct VaultProbe {
    bool ok = false;
    Vec3 dir;
    float rise = 0.0f;
};

VaultProbe probe_vault(const CollisionWorld& world, const PlayerState& player,
                       const MovementParams& params, const Vec3& dir,
                       const Vec3& mins, const Vec3& maxs) {
    VaultProbe result;
    if (length(dir) < 1e-4f) return result;
    const Vec3 unit = normalized(dir);

    // A wall-like face within reach in front of the player.
    // Start slightly behind the chest: at contact the swept box is exactly on the
    // face, which the trace reports as "no hit" (see pitfalls/contact-epsilon.md).
    const Vec3 chest = player.pos + Vec3(0.0f, 0.0f, 0.1f) - unit * 0.05f;
    const TraceResult face = world.trace_box(chest, chest + unit * (params.player_radius + 0.35f),
                                             mins, maxs);
    if (face.start_solid || face.fraction >= 1.0f) return result;
    if (std::fabs(face.normal.z) > kVerticalNormal) return result;

    // Height of the surface just past that face.
    // Step past the contact point by the player radius plus a margin, so the
    // sample point is actually on top of the obstacle rather than in front of it.
    const Vec3 beyond = face.end_pos + unit * (params.player_radius + 0.2f);
    const Vec3 from(beyond.x, beyond.y, player.pos.z + params.vault_max_height + 0.3f);
    const Vec3 to(beyond.x, beyond.y, player.pos.z - 0.05f);
    const TraceResult down = world.trace_box(from, to, Vec3(), Vec3());
    if (down.start_solid || down.fraction >= 1.0f) return result;

    const float rise = down.end_pos.z - player.pos.z;
    if (rise <= params.step_height || rise > params.vault_max_height) return result;

    // Standing headroom on the landing surface.
    const Vec3 top(beyond.x, beyond.y, down.end_pos.z + 0.02f);
    const Vec3 stand_mins(-params.player_radius, -params.player_radius, 0.0f);
    const Vec3 stand_maxs(params.player_radius, params.player_radius, params.stand_height);
    if (!hull_fits(world, top, stand_mins, stand_maxs)) return result;

    result.ok = true;
    result.dir = unit;
    result.rise = rise;
    return result;
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
    const float hz = 1.0f / dt;
    if (player.dash_cooldown > 0) --player.dash_cooldown;

    const bool do_dash = input.dash && !player.dash_was_down && player.dash_cooldown == 0;
    player.dash_was_down = input.dash;

    float ground_z = player.pos.z;
    player.on_ground = probe_ground(player, world, params, ground_z);
    if (player.on_ground) player.pos.z = ground_z;

    try_start_slide(player, input, params);

    if (player.state == ParkourState::Slide) {
        if (!update_slide(player, input, params, hz)) {
            player.state = player.on_ground ? ParkourState::Ground : ParkourState::Air;
            player.state_ticks = 0;
        }
    }

    // Crouch state: slide forces it; standing up needs headroom.
    bool want_crouch = input.crouch || player.state == ParkourState::Slide;
    if (!want_crouch && player.crouched) {
        Vec3 stand_mins(-params.player_radius, -params.player_radius, 0.0f);
        Vec3 stand_maxs(params.player_radius, params.player_radius, params.stand_height);
        if (!hull_fits(world, player.pos, stand_mins, stand_maxs)) want_crouch = true;
    }
    player.crouched = want_crouch;

    Vec3 mins;
    Vec3 maxs;
    player_hull(player, params, mins, maxs);

    // Never start a tick inside geometry: that is the "camera works, player is
    // frozen" bug.
    if (!hull_fits(world, player.pos, mins, maxs)) {
        player.pos = resolve_penetration(world, player.pos, mins, maxs);
    }

    const bool sliding = player.state == ParkourState::Slide;

    if (input.jump && player.on_ground) {
        player.vel.z = params.jump_velocity * (sliding ? params.slide_jump_boost : 1.0f);
        player.on_ground = false;
        if (sliding) {
            player.state = ParkourState::Air;
            player.state_ticks = 0;
        }
    }

    // ---- wall run -------------------------------------------------------
    if (player.state == ParkourState::WallRun) {
        ++player.state_ticks;
        // After the velocity is projected onto the wall plane it runs parallel to
        // the wall, so probing along the velocity would always miss. Probe back
        // along the stored normal instead.
        const WallProbe wall = probe_wall(world, player.pos, -player.wall_normal, mins, maxs);
        const int max_ticks = static_cast<int>(params.wallrun_max_duration * hz + 0.5f);
        const bool keep = player.on_ground == false && input.wallrun && wall.hit &&
                          player.state_ticks <= max_ticks;
        if (!keep) {
            player.state = player.on_ground ? ParkourState::Ground : ParkourState::Air;
            player.state_ticks = 0;
        } else {
            player.wall_normal = wall.normal;
        }
    } else if (player.state == ParkourState::WallClimb) {
        ++player.state_ticks;
        const int max_ticks = static_cast<int>(params.wallclimb_max_duration * hz + 0.5f);
        const WallProbe wall = probe_wall(world, player.pos, facing_direction(input), mins, maxs);
        const bool keep = input.wallclimb && wall.hit && player.state_ticks <= max_ticks;
        if (!keep) {
            player.state = player.on_ground ? ParkourState::Ground : ParkourState::Air;
            player.state_ticks = 0;
        }
    }

    // ---- ability starts -------------------------------------------------
    const bool busy = player.state == ParkourState::WallRun ||
                      player.state == ParkourState::WallClimb ||
                      player.state == ParkourState::Vault;

    if (!busy && player.on_ground && player.state != ParkourState::Slide && input.vault &&
        !player.vault_was_down && player.impulses > 0) {
        Vec3 dir = wish_direction(input);
        if (length(dir) < 0.5f) dir = facing_direction(input);
        const VaultProbe plan = probe_vault(world, player, params, dir, mins, maxs);
        if (plan.ok) {
            player.state = ParkourState::Vault;
            player.state_ticks = 0;
            player.vault_dir = plan.dir;
            player.vault_rise = plan.rise;
            player.impulses = std::max(0, player.impulses - 1);
        }
    }

    if (player.state == ParkourState::Vault) {
        ++player.state_ticks;
        const int max_ticks = static_cast<int>(params.vault_duration * hz + 0.5f);
        if (player.state_ticks > max_ticks) {
            player.state = ParkourState::Air;
            player.state_ticks = 0;
        }
    }

    // Wall climb works from the ground too (Apex style), wall run does not.
    if (!busy && input.wallclimb && player.impulses > 0) {
        const WallProbe wall = probe_wall(world, player.pos, facing_direction(input), mins, maxs);
        if (wall.hit && wall.brush && wall.brush->climbable) {
            player.state = ParkourState::WallClimb;
            player.state_ticks = 0;
            player.wall_normal = wall.normal;
            player.impulses = std::max(0, player.impulses - 1);
        }
    }

    if (!busy && !player.on_ground && player.state != ParkourState::WallClimb &&
        input.wallrun && player.impulses > 0 &&
        horizontal_speed(player.vel) >= params.wallrun_min_approach_speed) {
        const Vec3 move_dir(player.vel.x, player.vel.y, 0.0f);
        const WallProbe wall = probe_wall(world, player.pos, move_dir, mins, maxs);
        if (wall.hit && wall.brush && wall.brush->wallrunnable) {
            player.state = ParkourState::WallRun;
            player.state_ticks = 0;
            player.wall_normal = wall.normal;
            player.impulses = std::max(0, player.impulses - 1);
            const float into = dot(player.vel, wall.normal);
            if (into < 0.0f) player.vel -= wall.normal * into;
            player.vel.z = std::max(player.vel.z, 0.0f);
        }
    }

    // ---- movement -------------------------------------------------------
    if (player.state == ParkourState::WallRun) {
        player.vel.z -= params.gravity * params.wallrun_gravity_scale * dt;
        // stay parallel to the wall while sliding along it, and keep a small
        // inward bias so the box does not drift off the surface
        const float into = dot(player.vel, player.wall_normal);
        if (into < 0.0f) player.vel -= player.wall_normal * into;
        player.vel -= player.wall_normal * 0.2f;
    } else if (player.state == ParkourState::WallClimb) {
        player.vel = Vec3(0.0f, 0.0f, params.wallclimb_speed);
    } else if (player.state == ParkourState::Vault) {
        // Scripted traversal: no friction, no gravity, fixed forward speed that
        // clears the ledge within vault_duration.
        const float speed = (params.player_radius + 0.55f) / params.vault_duration;
        player.vel.x = player.vault_dir.x * speed;
        player.vel.y = player.vault_dir.y * speed;
        player.vel.z = player.vault_rise / params.vault_duration;
    } else if (sliding) {
        apply_friction(player, params, params.slide_friction_scale, dt);
        if (player.vel.z < 0.0f) player.vel.z = 0.0f;
    } else if (player.on_ground) {
        apply_friction(player, params, 1.0f, dt);
        const Vec3 wishdir = wish_direction(input);
        accelerate(player.vel, wishdir, effective_max_speed(player, params), params.accel, dt);
        if (player.vel.z < 0.0f) player.vel.z = 0.0f;
    } else {
        const Vec3 wishdir = wish_direction(input);
        accelerate(player.vel, wishdir, effective_max_speed(player, params), params.air_accel, dt);
        player.vel.z -= params.gravity * dt;
    }

    if (do_dash) {
        Vec3 dir = wish_direction(input);
        if (length(dir) < 0.5f) {
            const float s = horizontal_speed(player.vel);
            dir = s > 1e-4f ? Vec3(player.vel.x / s, player.vel.y / s, 0.0f) : Vec3(1.0f, 0.0f, 0.0f);
        }
        player.vel.x = dir.x * params.dash_speed;
        player.vel.y = dir.y * params.dash_speed;
        player.dash_cooldown = static_cast<int>(params.dash_cooldown * hz + 0.5f);
    }

    const StepResult moved = step_slide_move(world, player.pos, player.vel, mins, maxs, dt, params);
    player.pos = moved.moved.pos;
    player.vel = moved.moved.vel;

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

    if (player.state == ParkourState::Slide && !player.on_ground) {
        player.state = ParkourState::Air;
        player.state_ticks = 0;
    } else if (player.state == ParkourState::WallRun && player.on_ground) {
        player.state = ParkourState::Ground;
        player.state_ticks = 0;
    } else if (player.state == ParkourState::WallClimb && player.on_ground) {
        player.state = ParkourState::Ground;
        player.state_ticks = 0;
    } else if (player.state == ParkourState::Ground && !player.on_ground) {
        player.state = ParkourState::Air;
    } else if (player.state == ParkourState::Air && player.on_ground) {
        player.state = ParkourState::Ground;
    }

    player.slide_was_down = input.slide;
    player.wallrun_was_down = input.wallrun;
    player.wallclimb_was_down = input.wallclimb;
    player.vault_was_down = input.vault;
    ++player.tick;
}

}  // namespace sim
