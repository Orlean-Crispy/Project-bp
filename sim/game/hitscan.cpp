#include "sim/game/hitscan.h"

#include <cmath>

namespace sim {
namespace {

bool near_zero(float v) { return std::fabs(v) < 1e-6f; }

// Ray vs axis-aligned box, slab method. The player volume is an AABB in the
// simulation (hitbox_contains uses the same shape), so the hit test and the
// visual volume can never disagree about "inside".
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

}  // namespace

ShotHit hitscan(const CollisionWorld& world, const HitboxSample* boxes, int count,
                const Vec3& origin, const Vec3& direction, float max_distance,
                int ignore_entity) {
    ShotHit result;
    if (max_distance <= 0.0f) return result;

    const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y +
                                   direction.z * direction.z);
    if (length < 1e-6f) return result;
    const Vec3 dir(direction.x / length, direction.y / length, direction.z / length);

    // World first: it is the far surface every player hit has to beat.
    const Vec3 zero(0.0f, 0.0f, 0.0f);
    const Vec3 end(origin.x + dir.x * max_distance, origin.y + dir.y * max_distance,
                   origin.z + dir.z * max_distance);
    const TraceResult world_hit = world.trace_box(origin, end, zero, zero);
    float nearest = max_distance;
    if (world_hit.fraction < 1.0f) {
        nearest = world_hit.fraction * max_distance;
        result.hit = true;
        result.is_player = false;
        result.entity = -1;
        result.point = world_hit.end_pos;
        result.normal = world_hit.normal;
        result.distance = nearest;
    }

    for (int i = 0; i < count && boxes != nullptr; ++i) {
        if (i == ignore_entity) continue;
        const HitboxSample& box = boxes[i];
        if (box.height <= 0.0f) continue;
        const Vec3 mins(box.pos.x - box.radius, box.pos.y - box.radius, box.pos.z);
        const Vec3 maxs(box.pos.x + box.radius, box.pos.y + box.radius, box.pos.z + box.height);
        float t = 0.0f;
        Vec3 normal;
        if (!ray_box(origin, dir, mins, maxs, t, normal)) continue;
        if (t > nearest) continue;  // behind the world surface or a nearer player
        nearest = t;
        result.hit = true;
        result.is_player = true;
        result.entity = i;
        result.point = Vec3(origin.x + dir.x * t, origin.y + dir.y * t, origin.z + dir.z * t);
        result.normal = normal;
        result.distance = t;
    }

    return result;
}

}  // namespace sim
