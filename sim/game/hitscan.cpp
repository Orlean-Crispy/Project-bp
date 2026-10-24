#include "sim/game/hitscan.h"

#include <cmath>

namespace sim {
namespace {

// 1 mm past a surface: far below any wall thickness in the maps, far above the
// float error of the exit computation.
constexpr float kExitEpsilon = 1e-3f;

bool near_zero(float v) { return std::fabs(v) < 1e-6f; }

Vec3 add_scaled(const Vec3& p, const Vec3& dir, float t) {
    return Vec3(p.x + dir.x * t, p.y + dir.y * t, p.z + dir.z * t);
}

// Ray vs axis-aligned box, slab method. The player volume is an AABB in the
// simulation (hitbox_contains uses the same shape), so the hit test and the
// visible volume can never disagree about "inside".
bool ray_box(const Vec3& origin, const Vec3& dir, const Vec3& mins, const Vec3& maxs, float& t_hit,
             Vec3& normal) {
    float tmin = 0.0f;
    float tmax = 1e30f;
    int axis = -1;
    bool negative_face = false;

    const float o[3] = {origin.x, origin.y, origin.z};
    const float d[3] = {dir.x, dir.y, dir.z};
    const float lo[3] = {mins.x, mins.y, mins.z};
    const float hi[3] = {maxs.x, maxs.y, maxs.z};

    for (int i = 0; i < 3; ++i) {
        if (near_zero(d[i])) {
            // Parallel to this slab: either inside it for the whole ray or never.
            if (o[i] < lo[i] || o[i] > hi[i]) return false;
            continue;
        }
        const float inv = 1.0f / d[i];
        float t0 = (lo[i] - o[i]) * inv;
        float t1 = (hi[i] - o[i]) * inv;
        bool negative = false;
        if (t0 > t1) {
            const float tmp = t0;
            t0 = t1;
            t1 = tmp;
            negative = true;  // entering through the max face
        }
        if (t0 > tmin) {
            tmin = t0;
            axis = i;
            negative_face = negative;
        }
        if (t1 < tmax) tmax = t1;
        if (tmin > tmax) return false;
    }

    if (tmax < 0.0f) return false;  // entirely behind the ray
    t_hit = tmin;
    normal = Vec3(0.0f, 0.0f, 0.0f);
    if (axis >= 0) {
        const float sign = negative_face ? 1.0f : -1.0f;
        if (axis == 0) normal.x = sign;
        if (axis == 1) normal.y = sign;
        if (axis == 2) normal.z = sign;
    }
    return true;
}

// Distance at which the ray leaves a convex brush it is currently inside of.
// Halfspaces are n.x <= d, so only faces whose normal points along the ray can
// be the exit; the nearest of those is the way out. Returns 0 when the geometry
// gives no usable answer, which the caller treats as "blocked" rather than
// inventing a path through the world.
float brush_exit_distance(const Brush& brush, const Vec3& origin, const Vec3& dir) {
    float exit_t = -1.0f;
    for (const Plane& plane : brush.planes) {
        const float denom = dot(plane.normal, dir);
        if (denom <= 1e-6f) continue;
        const float t = (plane.dist - dot(plane.normal, origin)) / denom;
        if (t < 0.0f) continue;
        if (exit_t < 0.0f || t < exit_t) exit_t = t;
    }
    return exit_t < 0.0f ? 0.0f : exit_t;
}

}  // namespace

ShotResult hitscan_penetrating(const CollisionWorld& world, const HitboxSample* boxes, int count,
                               const Vec3& origin, const Vec3& direction, float max_distance,
                               const PenetrationParams& penetration, int ignore_entity) {
    ShotResult result;
    if (max_distance <= 0.0f) return result;

    const float dir_length = length(direction);
    if (dir_length < 1e-6f) return result;
    const Vec3 dir =
        Vec3(direction.x / dir_length, direction.y / dir_length, direction.z / dir_length);
    const int max_layers = penetration.layers > 0 ? penetration.layers : 0;
    const Vec3 zero(0.0f, 0.0f, 0.0f);

    Vec3 from = origin;
    float travelled = 0.0f;

    for (;;) {
        const float remaining = max_distance - travelled;
        if (!(remaining > 0.0f)) return result;

        const Vec3 end = add_scaled(from, dir, remaining);
        const TraceResult wall = world.trace_box(from, end, zero, zero);
        const float wall_dist = wall.fraction < 1.0f ? wall.fraction * remaining : remaining;

        // A player closer than the next surface is hit first, through whatever
        // layers were already crossed.
        float best_t = 1e30f;
        int best_entity = -1;
        Vec3 best_normal;
        for (int i = 0; i < count && boxes != nullptr; ++i) {
            if (i == ignore_entity) continue;
            const HitboxSample& box = boxes[i];
            if (box.height <= 0.0f) continue;
            const Vec3 mins(box.pos.x - box.radius, box.pos.y - box.radius, box.pos.z);
            const Vec3 maxs(box.pos.x + box.radius, box.pos.y + box.radius, box.pos.z + box.height);
            float t = 0.0f;
            Vec3 normal;
            if (!ray_box(from, dir, mins, maxs, t, normal)) continue;
            if (t < 0.0f) t = 0.0f;
            if (t > wall_dist) continue;
            if (t < best_t) {
                best_t = t;
                best_entity = i;
                best_normal = normal;
            }
        }
        if (best_entity >= 0) {
            result.hit.hit = true;
            result.hit.is_player = true;
            result.hit.entity = best_entity;
            result.hit.point = add_scaled(from, dir, best_t);
            result.hit.normal = best_normal;
            result.hit.distance = travelled + best_t;
            return result;
        }

        if (wall.fraction >= 1.0f || wall.brush == nullptr) return result;  // nothing ahead

        result.hit.hit = true;
        result.hit.is_player = false;
        result.hit.entity = -1;
        result.hit.point = add_scaled(from, dir, wall_dist);
        result.hit.normal = wall.normal;
        result.hit.distance = travelled + wall_dist;

        // ADR 0006 D1: the map vetoes first, the weapon's budget second.
        if (!wall.brush->penetrable) {
            result.stopped_by_map = true;
            return result;
        }
        if (result.layers >= max_layers) {
            result.out_of_layers = true;
            return result;
        }

        const float exit_t = brush_exit_distance(*wall.brush, from, dir);
        const float advance = exit_t + kExitEpsilon;
        if (!(advance > 0.0f)) {
            // No usable exit: stop here instead of looping or teleporting.
            result.stopped_by_map = true;
            return result;
        }
        if (advance >= remaining) return result;  // the shot ran out inside the wall

        ++result.layers;
        result.damage_scale *= penetration.damage_scale;
        from = add_scaled(from, dir, advance);
        travelled += advance;
    }
}

ShotHit hitscan(const CollisionWorld& world, const HitboxSample* boxes, int count,
                const Vec3& origin, const Vec3& direction, float max_distance,
                int ignore_entity) {
    return hitscan_penetrating(world, boxes, count, origin, direction, max_distance,
                               PenetrationParams{}, ignore_entity)
        .hit;
}

}  // namespace sim
