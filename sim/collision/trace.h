#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sim/collision/brush.h"
#include "sim/collision/map_parse.h"
#include "sim/data/config.h"

namespace sim {

struct TraceResult {
    bool start_solid = false;
    float fraction = 1.0f;
    Vec3 end_pos;
    Vec3 normal;
    const Brush* brush = nullptr;
};

class CollisionWorld {
public:
    void build(const MapData& map);

    TraceResult trace_box(const Vec3& start, const Vec3& end,
                          const Vec3& mins, const Vec3& maxs) const;

    const std::vector<Brush>& brushes() const { return m_brushes; }
    const Bounds& bounds() const { return m_bounds; }
    uint64_t geometry_hash() const;

private:
    std::vector<Brush> m_brushes;
    Bounds m_bounds;
};

struct SlideResult {
    Vec3 pos;
    Vec3 vel;
    bool hit = false;
    int bumps = 0;
    Vec3 hit_normal;
    float max_step_length = 0.0f;  // largest sub-step used, checked against wall thickness
};

// Stepped box sweep (plan section 5.9). The step length is derived from
// min_wall_thickness so a fast slide cannot tunnel through a thin wall.
// Two fixes over the pseudo-code in the plan:
//   * the remaining distance is recomputed after every clip (otherwise the
//     travelled distance silently shrinks on each bump);
//   * a zeroed velocity exits the loop instead of feeding normalize() a
//     zero vector.
SlideResult slide_move(const CollisionWorld& world, const Vec3& start, const Vec3& velocity,
                       const Vec3& mins, const Vec3& maxs, float dt, const MovementParams& params);

struct StepResult {
    SlideResult moved;
    bool stepped = false;
};

// Stair stepping (Q3 PM_StepSlideMove): when the flat move is blocked, retry it
// from step_height higher and drop back down. The stepped attempt is only taken
// when it actually gains horizontal ground and lands on a walkable surface, so a
// wall cannot be climbed by "stepping".
StepResult step_slide_move(const CollisionWorld& world, const Vec3& start, const Vec3& velocity,
                           const Vec3& mins, const Vec3& maxs, float dt,
                           const MovementParams& params);

}  // namespace sim
