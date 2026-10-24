#include "sim/core/world.h"

#include "sim/core/hash.h"

namespace sim {
namespace {

// One player's tick, shared by step() and step_single() so the two can never
// drift apart. entity_id is the player's dense index.
void step_player(PlayerState& player, const MoveInput& input, const CollisionWorld& collision,
                 const MovementParams& params, float dt, uint32_t tick, size_t entity,
                 EventQueue* events) {
    const bool was_ground = player.on_ground;
    const float fall_speed = player.vel.z;
    // A jump is a *pressed* edge on the ground. The old test only looked at the
    // velocity, so a vault or a wall climb starting from the floor was reported as
    // a jump (wrong sound, wrong HUD, wrong statistics).
    const bool jump_pressed = input.jump && !player.jump_was_down;
    pmove(player, input, collision, params, dt);

    // M6: the trigger is replicated state, not a one-tick pulse. A lost snapshot
    // must not turn "still holding fire" into "released" (same reason the parkour
    // edges ride in the flags), so the last input is mirrored here, once, for the
    // local step and the rollback replay alike.
    player.firing = input.fire;
    player.reloading = input.reload;
    player.ads = input.ads;

    if (events == nullptr) return;

    if (!was_ground && player.on_ground && fall_speed < -1.0f) {
        // Landing after a real fall; spawning on the floor is not an event.
        SimEvent event;
        event.tick = tick;
        event.type = EventType::Land;
        event.entity_id = static_cast<uint16_t>(entity);
        event.a = player.pos;
        events->push(event);
    } else if (jump_pressed && was_ground && !player.on_ground && player.vel.z > 0.0f) {
        SimEvent event;
        event.tick = tick;
        event.type = EventType::Jump;
        event.entity_id = static_cast<uint16_t>(entity);
        event.a = player.pos;
        events->push(event);
    }
}

}  // namespace

void World::step(const std::vector<MoveInput>& inputs, float dt, EventQueue* events) {
    const size_t count = players.size() < inputs.size() ? players.size() : inputs.size();
    for (size_t i = 0; i < count; ++i) {
        step_player(players[i], inputs[i], collision, params, dt, tick, i, events);
    }
    ++tick;
}

void World::step_single(const MoveInput& input, float dt, EventQueue* events) {
    if (!players.empty()) {
        step_player(players[0], input, collision, params, dt, tick, 0, events);
    }
    ++tick;
}

uint64_t World::state_hash() const {
    Hasher h;
    h.u32(tick);
    h.u64(params_hash);
    h.u64(collision.geometry_hash());
    h.u32(static_cast<uint32_t>(players.size()));
    for (const PlayerState& p : players) {
        // Every field pmove carries into the next tick (audit R9). A field that
        // is left out makes two genuinely different states hash alike, so the
        // determinism and rollback gates stop seeing a divergence.
        h.vec3(p.pos).vec3(p.vel).f32(p.yaw);
        h.u32(p.on_ground ? 1u : 0u).u32(p.crouched ? 1u : 0u).u32(p.active ? 1u : 0u);
        h.u32(static_cast<uint32_t>(p.impulses));
        h.u32(static_cast<uint32_t>(p.state))
            .u32(static_cast<uint32_t>(p.state_ticks))
            .u32(static_cast<uint32_t>(p.dash_cooldown))
            .u32(static_cast<uint32_t>(p.slow_ticks));
        h.u32(p.jump_was_down ? 1u : 0u)
            .u32(p.slide_was_down ? 1u : 0u)
            .u32(p.dash_was_down ? 1u : 0u)
            .u32(p.vault_was_down ? 1u : 0u)
            .u32(p.firing ? 1u : 0u)
            .u32(p.reloading ? 1u : 0u)
            .u32(p.ads ? 1u : 0u)
            .u32(static_cast<uint32_t>(p.weapon_id))
            .u32(static_cast<uint32_t>(p.ammo))
            .u32(static_cast<uint32_t>(p.reserve))
            .u32(static_cast<uint32_t>(p.fire_cooldown))
            .u32(static_cast<uint32_t>(p.reload_left));
        h.vec3(p.wall_normal).vec3(p.vault_dir).f32(p.vault_rise);
        h.f32(p.speed_coefficient).f32(p.slow_factor);
        h.u32(static_cast<uint32_t>(p.recharge_progress));
        // v6: the same two fields the snapshot carries (lesson 16 - a field in
        // one and not the other is a false divergence or a silent one).
        h.u32(static_cast<uint32_t>(p.vitals.health))
            .u32(static_cast<uint32_t>(p.vitals.armor));
    }
    return h.value();
}

}  // namespace sim
