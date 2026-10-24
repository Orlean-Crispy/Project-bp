#pragma once

#include "sim/collision/trace.h"
#include "sim/data/config.h"
#include "sim/game/combat.h"

namespace sim {

struct MoveInput {
    float forward = 0.0f;  // -1..1, +X is forward at yaw 0
    float right = 0.0f;    // -1..1
    float yaw = 0.0f;      // radians
    bool jump = false;
    bool crouch = false;
    bool slide = false;
    bool dash = false;
    bool wallrun = false;
    bool wallclimb = false;
    bool vault = false;
    // M6 combat inputs. They are held states, not one-tick edges: a lost snapshot
    // must not turn "still holding the trigger" into "released" (the same reason
    // the parkour edges ride in the flags).
    bool fire = false;
    bool reload = false;
    bool ads = false;
};

// Timed abilities are counted in whole ticks, never in accumulated float time
// (iron law B): a rollback replay must land on exactly the same tick.
enum class ParkourState : uint8_t {
    Ground = 0,
    Air,
    Slide,
    WallRun,
    WallClimb,
    Vault,
};

struct PlayerState {
    Vec3 pos;  // feet position
    Vec3 vel;
    float yaw = 0.0f;  // view yaw; replicated so remote players can face the right way
    bool active = true;  // false for reserved slots nobody has joined yet
    bool on_ground = false;
    bool crouched = false;
    int impulses = 4;
    // Ticks of ground contact towards the next impulse (see recharge_ticks).
    int recharge_progress = 0;

    ParkourState state = ParkourState::Ground;
    int state_ticks = 0;        // ticks spent in the current timed state
    int dash_cooldown = 0;      // ticks until dash is available again
    Vec3 wall_normal;           // valid while wall running / climbing
    bool jump_was_down = false;
    bool slide_was_down = false;
    bool dash_was_down = false;
    bool vault_was_down = false;
    // M6: the trigger, the reload key and the aim state as of the tick that just
    // ran. Mirrors MoveInput (iron law C: state leaves the sim through the
    // snapshot), and gives the fire cadence the previous trigger state it needs
    // to tell a held trigger from a fresh press.
    bool firing = false;
    bool reloading = false;
    bool ads = false;
    Vec3 vault_dir;      // horizontal direction of the vault in progress
    float vault_rise = 0.0f;  // metres to climb during the vault

    // Multipliers from weapon (5.10) and damage (6.10). Kept in the state so the
    // rollback snapshot can never silently replay with a different max speed.
    float speed_coefficient = 1.0f;
    float slow_factor = 1.0f;
    // Section 6.10: ticks of the post-hit movement penalty still to serve. pmove
    // derives slow_factor from this every tick, so the penalty can never be left
    // on by accident (whole ticks, no float time - iron law B).
    int slow_ticks = 0;
    // Protocol v6: health and armour live in the player state so they can be
    // hashed and replicated. Nothing damages them yet (M6 owns the weapon); the
    // M5 -> M6 audit required the wire capacity to exist before that work starts.
    PlayerVitals vitals;
};

// max speed = base * weapon * slow * crouch (plan section 5.10).
float effective_max_speed(const PlayerState& player, const MovementParams& params);

// Section 6.10: a hit slows movement by damage_slow_factor for damage_slow_ticks
// (15% for 1.5 s at the default tuning). Repeated hits refresh, never stack.
void apply_damage_slow(PlayerState& player, const MovementParams& params);

void player_hull(const PlayerState& player, const MovementParams& params, Vec3& mins, Vec3& maxs);

void pmove(PlayerState& player, const MoveInput& input, const CollisionWorld& world,
           const MovementParams& params, float dt);

}  // namespace sim
