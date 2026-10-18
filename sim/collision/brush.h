#pragma once

#include <string>
#include <vector>

#include "sim/core/types.h"

namespace sim {

// A plane is n.x = dist; the brush interior is the halfspace n.x <= dist.
struct Plane {
    Vec3 normal;
    float dist = 0.0f;
    int type = 3;  // 0..2 when the normal is axis aligned, 3 otherwise
};

Plane make_plane(const Vec3& a, const Vec3& b, const Vec3& c);

struct Brush {
    std::vector<Plane> planes;
    Bounds bounds;
    std::string material;
    bool climbable = false;
    bool wallrunnable = false;
    int entity_index = -1;
};

// Intersects every plane triple, keeps the points inside all halfspaces, and
// derives the bounds. Returns false for degenerate (open or flat) brushes.
bool build_brush(const std::vector<Plane>& planes, Brush& out, std::string* error = nullptr);

bool point_inside(const Brush& brush, const Vec3& p, float eps = 0.0f);

}  // namespace sim
