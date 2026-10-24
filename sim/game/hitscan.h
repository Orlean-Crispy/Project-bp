#pragma once

#include "sim/collision/trace.h"
#include "sim/game/hitbox.h"

namespace sim {

// One shot's terminal event (plan 6.5: hitscan is the primary weapon model).
struct ShotHit {
    bool hit = false;
    bool is_player = false;  // false: world geometry stopped the ray
    int entity = -1;         // dense player index for a player hit
    Vec3 point;              // impact point, metres
    Vec3 normal;             // surface normal at the impact
    float distance = 0.0f;   // metres from the origin
};

// ADR 0006 D1: the weapon owns the policy, the map owns the veto. Passing this
// struct (instead of two positional numbers) is what lets a later per-material
// scale multiply in without changing the call sites.
struct PenetrationParams {
    int layers = 0;             // weapon.penetration_count
    float damage_scale = 1.0f;  // weapon.penetration_damage_scale, per layer
};

struct ShotResult {
    ShotHit hit;
    int layers = 0;              // surfaces actually crossed
    float damage_scale = 1.0f;   // product of the per-layer scale
    bool stopped_by_map = false; // a surface the map refuses to let anything cross
    bool out_of_layers = false;  // a penetrable surface this weapon cannot afford
};

// One ray against the world and against player volumes, with penetration.
//
// "boxes" is the same HitboxSample array the hitbox history stores
// (pos/height/radius), so a rewound frame can be passed without changing this
// signature. The nearest hit wins: a wall in front of a player hides that player.
// World surfaces are crossed only when the brush says penetrable and the weapon
// has layers left; each crossing multiplies damage_scale.
//
// The caller owns the volumes and their order; entity i is boxes[i]. The ray is
// normalised here, so a direction of any length is accepted (a zero vector is a
// miss, not a NaN).
ShotResult hitscan_penetrating(const CollisionWorld& world, const HitboxSample* boxes, int count,
                               const Vec3& origin, const Vec3& direction, float max_distance,
                               const PenetrationParams& penetration, int ignore_entity = -1);

// The no-penetration query, kept as a wrapper so there is only one ray
// implementation (two paths would eventually disagree).
ShotHit hitscan(const CollisionWorld& world, const HitboxSample* boxes, int count,
                const Vec3& origin, const Vec3& direction, float max_distance,
                int ignore_entity = -1);

}  // namespace sim
