#include "sim/game/shoot.h"

#include "sim/game/combat.h"
#include "sim/core/event.h"
#include "sim/game/hitscan.h"
#include "sim/game/rewind.h"

namespace sim {
namespace {

// The per-player hitbox rings are short; 16 players is far above the v0.1 room
// size and keeps the shot path allocation free.
constexpr int kMaxShotTargets = 16;

int damage_for(float base, HitZone zone, const MovementParams& params, float penetration_scale) {
    if (!(base > 0.0f)) return 0;
    const float scaled = base * zone_multiplier(zone, params) * penetration_scale;
    const int rounded = static_cast<int>(scaled + 0.5f);
    return rounded > 1 ? rounded : 1;
}

}  // namespace

void update_weapon(PlayerState& shooter, int shooter_index, const MoveInput& input,
                   const WeaponDef& def, World& world, const HitboxHistory* histories,
                   int history_count, uint32_t tick, float rewind, ShotReport* report,
                   EventQueue* events) {
    // A dead player holds no trigger: no rounds, no reload, no timers.
    if (shooter.vitals.health <= 0) return;

    // Timers first: a cooldown that ends this tick may fire this tick.
    if (shooter.fire_cooldown > 0) --shooter.fire_cooldown;
    if (shooter.reload_left > 0) {
        if (--shooter.reload_left == 0) {
            const int need = def.magazine - shooter.ammo;
            const int take = need < shooter.reserve ? need : shooter.reserve;
            shooter.ammo += take;
            shooter.reserve -= take;
        }
        return;  // the magazine is out: no firing until it is back
    }
    if (input.reload && !shooter.reloading && shooter.ammo < def.magazine &&
        shooter.reserve > 0) {
        shooter.reload_left = reload_ticks(def);
        return;
    }
    if (!input.fire || shooter.fire_cooldown > 0 || shooter.ammo <= 0) return;

    // One shot. The round is spent even if it hits nothing.
    --shooter.ammo;
    shooter.fire_cooldown = fire_interval_ticks(def);
    if (report != nullptr) ++report->shots;

    HitboxSample frame[kMaxShotTargets];
    const int count = history_count < kMaxShotTargets ? history_count : kMaxShotTargets;
    rewind_frame(histories, count, tick, rewind, frame, count);

    PenetrationParams penetration;
    penetration.layers = def.penetration_count;
    penetration.damage_scale = def.penetration_damage_scale;

    const Vec3 eye = eye_position(shooter, world.params);
    const Vec3 dir = view_forward(shooter.yaw, input.pitch);
    const ShotResult shot = hitscan_penetrating(world.collision, frame, count, eye, dir, def.range,
                                                penetration, shooter_index);

    // The tracer is presentation only, and it leaves before the damage branch:
    // a shot that hits a wall (or nothing) still drew a line.
    if (events != nullptr) {
        SimEvent event;
        event.tick = tick;
        event.type = EventType::Shot;
        event.entity_id = static_cast<uint16_t>(shooter_index);
        event.a = eye;
        if (shot.hit.hit) {
            event.b = shot.hit.point;
        } else {
            event.b = Vec3(eye.x + dir.x * def.range, eye.y + dir.y * def.range,
                           eye.z + dir.z * def.range);
        }
        event.param = def.slot;
        events->push(event);
    }

    if (!shot.hit.is_player) return;
    if (shot.hit.entity < 0 || shot.hit.entity >= static_cast<int>(world.players.size())) return;
    if (shot.hit.entity == shooter_index) return;

    PlayerState& victim = world.players[static_cast<std::size_t>(shot.hit.entity)];
    const HitZone zone = hit_zone_at(frame[shot.hit.entity], shot.hit.point.z);
    const int damage =
        damage_for(damage_at_range(def, shot.hit.distance), zone, world.params, shot.damage_scale);
    if (damage <= 0) return;

    apply_damage(victim.vitals, damage, zone, world.combat);
    apply_damage_slow(victim, world.params);
    if (report != nullptr) {
        ++report->hits;
        report->damage += damage;
    }
}

}  // namespace sim
