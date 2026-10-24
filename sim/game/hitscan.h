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

// One ray against the world and against player volumes.
//
// "boxes" is the same HitboxSample array the hitbox history stores
// (pos/height/radius), so M6.4 can hand over a rewound frame without changing
// this signature. The nearest hit wins: a wall in front of a player hides that
// player.
//
// The caller owns the volumes and their order; entity i is boxes[i]. The ray is
// normalised here, so a direction of any length is accepted (a zero vector is a
// miss, not a NaN).
ShotHit hitscan(const CollisionWorld& world, const HitboxSample* boxes, int count,
                const Vec3& origin, const Vec3& direction, float max_distance,
                int ignore_entity = -1);

}  // namespace sim
