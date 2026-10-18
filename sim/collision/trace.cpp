#include "sim/collision/trace.h"

#include <algorithm>
#include <cmath>

#include "sim/core/hash.h"

namespace sim {
namespace {

constexpr float kClipEpsilon = 1e-3f;   // 1 mm, avoids sticking on contact
constexpr float kMinSpeed = 1e-4f;

// Minimum support distance of the box along the plane normal. The box overlaps
// the brush iff its centre satisfies n.c <= d - min_support for every plane;
// using the maximum instead tests "box entirely inside", which lets every box
// pass straight through a wall.
float support_min(const Plane& plane, const Vec3& mins, const Vec3& maxs) {
    float s = 0.0f;
    s += plane.normal.x > 0.0f ? mins.x * plane.normal.x : maxs.x * plane.normal.x;
    s += plane.normal.y > 0.0f ? mins.y * plane.normal.y : maxs.y * plane.normal.y;
    s += plane.normal.z > 0.0f ? mins.z * plane.normal.z : maxs.z * plane.normal.z;
    return s;
}

Bounds segment_bounds(const Vec3& a, const Vec3& b, const Vec3& mins, const Vec3& maxs) {
    Bounds box(vmin(a, b) + mins, vmax(a, b) + maxs);
    return box;
}

}  // namespace

void CollisionWorld::build(const MapData& map) {
    m_brushes = map.brushes;
    m_bounds = map.bounds;
}

TraceResult CollisionWorld::trace_box(const Vec3& start, const Vec3& end,
                                      const Vec3& mins, const Vec3& maxs) const {
    TraceResult best;
    best.end_pos = end;
    best.fraction = 1.0f;

    const Bounds sweep = segment_bounds(start, end, mins, maxs);

    for (size_t bi = 0; bi < m_brushes.size(); ++bi) {
        const Brush& brush = m_brushes[bi];
        if (!sweep.overlaps(brush.bounds)) continue;

        float brush_enter = -1.0f;
        float brush_leave = 1.0f;
        Vec3 brush_normal;
        bool outside_any = false;
        bool separated = false;

        for (const Plane& plane : brush.planes) {
            const float offset = plane.dist - support_min(plane, mins, maxs);
            const float d1 = dot(plane.normal, start) - offset;
            const float d2 = dot(plane.normal, end) - offset;

            if (d1 > 0.0f) outside_any = true;

            if (d1 > 0.0f && d2 >= 0.0f) {
                separated = true;  // the whole segment is in front of this face
                break;
            }
            if (d1 <= 0.0f && d2 <= 0.0f) continue;

            if (d1 > d2) {
                // Resting exactly one epsilon off the face makes the numerator zero
                // or slightly negative. Clamp instead of discarding: an entry that
                // has already happened is a hit at fraction 0, and dropping it lets
                // the player creep 19 mm into the wall every tick.
                float f = (d1 - kClipEpsilon) / (d1 - d2);
                if (f < 0.0f) f = 0.0f;
                if (f > brush_enter) {
                    brush_enter = f;
                    brush_normal = plane.normal;
                }
            } else {
                const float f = (d1 + kClipEpsilon) / (d1 - d2);
                if (f < brush_leave) brush_leave = f;
            }
        }

        if (separated) continue;

        if (!outside_any) {
            // Starts inside this brush.
            if (!best.start_solid) {
                best.start_solid = true;
                best.fraction = 0.0f;
                best.end_pos = start;
                best.normal = Vec3();
                best.brush_index = static_cast<int>(bi);
                best.brush = &brush;
            }
            continue;
        }

        if (brush_enter < 0.0f) continue;
        if (brush_enter >= brush_leave) continue;
        if (brush_enter >= best.fraction) continue;

        best.fraction = brush_enter;
        best.end_pos = start + (end - start) * brush_enter;
        best.normal = brush_normal;
        best.brush_index = static_cast<int>(bi);
        best.brush = &brush;
    }

    if (best.fraction < 0.0f) best.fraction = 0.0f;
    return best;
}

uint64_t CollisionWorld::geometry_hash() const {
    Hasher h;
    h.u32(static_cast<uint32_t>(m_brushes.size()));
    for (const Brush& b : m_brushes) {
        h.u32(static_cast<uint32_t>(b.planes.size()));
        for (const Plane& p : b.planes) h.vec3(p.normal).f32(p.dist);
    }
    return h.value();
}

SlideResult slide_move(const CollisionWorld& world, const Vec3& start, const Vec3& velocity,
                       const Vec3& mins, const Vec3& maxs, float dt, const MovementParams& params) {
    SlideResult result;
    result.pos = start;

    Vec3 vel = velocity;
    if (dt <= 0.0f) {
        result.vel = vel;
        return result;
    }

    const float thickness = std::max(params.min_wall_thickness, 0.01f);
    const int max_steps = std::max(1, params.max_slide_steps);
    float time_left = dt;
    int bumps = 0;

    while (time_left > 1e-6f && bumps <= params.max_bumps) {
        const float speed = length(vel);
        if (speed < kMinSpeed) {
            vel = Vec3();
            break;
        }

        const float dist = speed * time_left;
        int steps = static_cast<int>(std::ceil(dist / thickness));
        steps = std::max(1, std::min(steps, max_steps));
        const float step_len = dist / static_cast<float>(steps);
        result.max_step_length = std::max(result.max_step_length, step_len);

        bool bumped = false;
        for (int s = 0; s < steps; ++s) {
            const float live_speed = length(vel);
            if (live_speed < kMinSpeed) {
                vel = Vec3();
                bumped = true;
                break;
            }
            const Vec3 step = (vel / live_speed) * step_len;
            const TraceResult tr = world.trace_box(result.pos, result.pos + step, mins, maxs);

            if (tr.start_solid) {
                vel = Vec3();
                bumped = true;
                break;
            }

            if (tr.fraction >= 1.0f) {
                result.pos += step;
                time_left -= step_len / live_speed;
                continue;
            }

            result.pos = tr.end_pos;
            time_left -= (step_len * tr.fraction) / live_speed;

            const float into = dot(vel, tr.normal);
            if (into < 0.0f) vel -= tr.normal * into;

            result.hit = true;
            result.hit_normal = tr.normal;
            ++bumps;
            bumped = true;
            break;
        }

        if (!bumped) break;
        if (bumps > params.max_bumps) {
            vel = Vec3();
            break;
        }
    }

    result.vel = vel;
    result.bumps = bumps;
    return result;
}

namespace {

float horizontal_distance(const Vec3& a, const Vec3& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

}  // namespace

StepResult step_slide_move(const CollisionWorld& world, const Vec3& start, const Vec3& velocity,
                           const Vec3& mins, const Vec3& maxs, float dt,
                           const MovementParams& params) {
    StepResult result;
    result.moved = slide_move(world, start, velocity, mins, maxs, dt, params);
    if (!result.moved.hit) return result;

    const float step = params.step_height;
    if (step <= 0.0f) return result;

    // 1. Lift.
    const TraceResult up = world.trace_box(start, start + Vec3(0.0f, 0.0f, step), mins, maxs);
    if (up.start_solid || up.fraction <= 0.0f) return result;
    const Vec3 lifted = up.end_pos;

    // 2. Repeat the horizontal move from the raised position.
    const SlideResult raised = slide_move(world, lifted, velocity, mins, maxs, dt, params);

    // 3. Drop back down.
    const TraceResult down = world.trace_box(raised.pos, raised.pos - Vec3(0.0f, 0.0f, step + 0.01f),
                                             mins, maxs);
    if (down.start_solid) return result;
    const Vec3 landed = down.end_pos;

    // 4. Accept only when it is a real gain: either more ground covered, or the
    // landing is higher than the flat result. The second half matters when the
    // player approaches at an angle: sliding along the riser covers more ground
    // than climbing it, and the old check therefore refused every diagonal step.
    const bool walkable = down.fraction < 1.0f && down.normal.z > 0.7f;
    if (!walkable) return result;
    // Never accept a landing that is already inside another brush.
    if (world.trace_box(landed, landed, mins, maxs).start_solid) return result;

    const float flat_gain = horizontal_distance(result.moved.pos, start);
    const float step_gain = horizontal_distance(landed, start);
    const bool climbed = landed.z > result.moved.pos.z + 0.05f;
    if (step_gain <= flat_gain + 1e-4f && !climbed) return result;

    result.moved.pos = landed;
    result.moved.vel = raised.vel;
    result.moved.hit = raised.hit;
    result.moved.hit_normal = raised.hit_normal;
    result.moved.bumps = raised.bumps;
    result.moved.max_step_length = raised.max_step_length;
    result.stepped = true;
    return result;
}

}  // namespace sim
