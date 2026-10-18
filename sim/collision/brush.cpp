#include "sim/collision/brush.h"

#include <cmath>

namespace sim {
namespace {

constexpr float kPlaneEpsilon = 1e-4f;
constexpr float kVertexTolerance = 0.01f;  // 0.01 m = 1 cm

void classify_plane(Plane& p) {
    const float ax = std::fabs(p.normal.x);
    const float ay = std::fabs(p.normal.y);
    const float az = std::fabs(p.normal.z);
    if (ax > 1.0f - kPlaneEpsilon) p.type = 0;
    else if (ay > 1.0f - kPlaneEpsilon) p.type = 1;
    else if (az > 1.0f - kPlaneEpsilon) p.type = 2;
    else p.type = 3;
}

bool plane_intersection(const Plane& a, const Plane& b, const Plane& c, Vec3& out) {
    const Vec3 bc = cross(b.normal, c.normal);
    const float det = dot(a.normal, bc);
    if (std::fabs(det) < 1e-6f) return false;
    const Vec3 ca = cross(c.normal, a.normal);
    const Vec3 ab = cross(a.normal, b.normal);
    out = (bc * a.dist + ca * b.dist + ab * c.dist) / det;
    return true;
}

}  // namespace

Plane make_plane(const Vec3& a, const Vec3& b, const Vec3& c) {
    // Quake convention: points are listed clockwise seen from the front, so
    // cross(a - b, c - b) points out of the brush.
    const Vec3 normal = normalized(cross(a - b, c - b));
    Plane p;
    p.normal = normal;
    p.dist = dot(c, normal);
    classify_plane(p);
    return p;
}

bool build_brush(const std::vector<Plane>& planes, Brush& out, std::string* error) {
    if (planes.size() < 4) {
        if (error) *error = "brush has fewer than 4 planes";
        return false;
    }

    Brush brush;
    brush.planes = planes;

    std::vector<Vec3> vertices;
    for (size_t i = 0; i < planes.size(); ++i) {
        for (size_t j = i + 1; j < planes.size(); ++j) {
            for (size_t k = j + 1; k < planes.size(); ++k) {
                Vec3 v;
                if (!plane_intersection(planes[i], planes[j], planes[k], v)) continue;

                bool inside = true;
                for (const Plane& p : planes) {
                    if (dot(p.normal, v) - p.dist > kVertexTolerance) { inside = false; break; }
                }
                if (!inside) continue;

                bool duplicate = false;
                for (const Vec3& existing : vertices) {
                    if (length_sq(existing - v) < 1e-6f) { duplicate = true; break; }
                }
                if (!duplicate) vertices.push_back(v);
            }
        }
    }

    if (vertices.size() < 4) {
        if (error) *error = "brush is open or flat (" + std::to_string(vertices.size()) + " vertices)";
        return false;
    }

    brush.bounds = Bounds(vertices[0], vertices[0]);
    for (const Vec3& v : vertices) brush.bounds.expand(v);

    out = brush;
    return true;
}

bool point_inside(const Brush& brush, const Vec3& p, float eps) {
    for (const Plane& plane : brush.planes) {
        if (dot(plane.normal, p) - plane.dist > eps) return false;
    }
    return true;
}

}  // namespace sim
