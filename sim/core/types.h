#pragma once

#include <algorithm>
#include <cstdint>
#include <cmath>

namespace sim {

constexpr float kEpsilon = 1e-6f;

// All simulation units are SI: metres, seconds, m/s, m/s^2. Z is up.
struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    Vec3() = default;
    Vec3(float ix, float iy, float iz) : x(ix), y(iy), z(iz) {}

    float& operator[](int i) { return (&x)[i]; }
    float operator[](int i) const { return (&x)[i]; }
};

inline Vec3 operator+(const Vec3& a, const Vec3& b) { return Vec3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline Vec3 operator-(const Vec3& a, const Vec3& b) { return Vec3(a.x - b.x, a.y - b.y, a.z - b.z); }
inline Vec3 operator-(const Vec3& a) { return Vec3(-a.x, -a.y, -a.z); }
inline Vec3 operator*(const Vec3& a, float s) { return Vec3(a.x * s, a.y * s, a.z * s); }
inline Vec3 operator*(float s, const Vec3& a) { return a * s; }
inline Vec3 operator/(const Vec3& a, float s) { return Vec3(a.x / s, a.y / s, a.z / s); }
inline Vec3& operator+=(Vec3& a, const Vec3& b) { a.x += b.x; a.y += b.y; a.z += b.z; return a; }
inline Vec3& operator-=(Vec3& a, const Vec3& b) { a.x -= b.x; a.y -= b.y; a.z -= b.z; return a; }
inline Vec3& operator*=(Vec3& a, float s) { a.x *= s; a.y *= s; a.z *= s; return a; }

inline bool operator==(const Vec3& a, const Vec3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
inline bool operator!=(const Vec3& a, const Vec3& b) { return !(a == b); }

inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return Vec3(a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x);
}

inline float length_sq(const Vec3& v) { return dot(v, v); }
inline float length(const Vec3& v) { return std::sqrt(dot(v, v)); }

inline Vec3 normalized(const Vec3& v) {
    const float len = length(v);
    if (len < kEpsilon) return Vec3();
    return v / len;
}

inline Vec3 vmin(const Vec3& a, const Vec3& b) { return Vec3(std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)); }
inline Vec3 vmax(const Vec3& a, const Vec3& b) { return Vec3(std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)); }



struct Bounds {
    Vec3 mins;
    Vec3 maxs;

    Bounds() = default;
    Bounds(const Vec3& a, const Vec3& b) : mins(a), maxs(b) {}

    bool overlaps(const Bounds& o) const {
        return !(mins.x > o.maxs.x || maxs.x < o.mins.x ||
                 mins.y > o.maxs.y || maxs.y < o.mins.y ||
                 mins.z > o.maxs.z || maxs.z < o.mins.z);
    }
    void expand(const Vec3& p) { mins = vmin(mins, p); maxs = vmax(maxs, p); }
    Vec3 center() const { return (mins + maxs) * 0.5f; }
};

}  // namespace sim
