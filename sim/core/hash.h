#pragma once

#include <cstdint>
#include <cstring>
#include <string>

#include "sim/core/types.h"

namespace sim {

// FNV-1a 64. Stable across builds and platforms; never seeded by time or address.
constexpr uint64_t kFnvOffset = 1469598103934665603ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

inline uint64_t hash_bytes(uint64_t h, const void* data, size_t n) {
    const auto* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < n; ++i) {
        h ^= static_cast<uint64_t>(p[i]);
        h *= kFnvPrime;
    }
    return h;
}

inline uint64_t hash_u32(uint64_t h, uint32_t v) { return hash_bytes(h, &v, sizeof(v)); }
inline uint64_t hash_u64(uint64_t h, uint64_t v) { return hash_bytes(h, &v, sizeof(v)); }

// Floats are hashed by bit pattern: 0.0f and -0.0f are different states, NaN is
// poison (see the finite check in world.cpp).
inline uint64_t hash_f32(uint64_t h, float v) {
    uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    return hash_u32(h, bits);
}

inline uint64_t hash_vec3(uint64_t h, const Vec3& v) {
    h = hash_f32(h, v.x);
    h = hash_f32(h, v.y);
    return hash_f32(h, v.z);
}

inline uint64_t hash_string(uint64_t h, const std::string& s) {
    return hash_bytes(h, s.data(), s.size());
}

// Incremental hasher; callers must feed fields in a fixed order.
class Hasher {
public:
    explicit Hasher(uint64_t seed = kFnvOffset) : m_h(seed) {}

    Hasher& u32(uint32_t v) { m_h = hash_u32(m_h, v); return *this; }
    Hasher& u64(uint64_t v) { m_h = hash_u64(m_h, v); return *this; }
    Hasher& f32(float v) { m_h = hash_f32(m_h, v); return *this; }
    Hasher& vec3(const Vec3& v) { m_h = hash_vec3(m_h, v); return *this; }
    Hasher& str(const std::string& s) { m_h = hash_string(m_h, s); return *this; }

    uint64_t value() const { return m_h; }

private:
    uint64_t m_h;
};

}  // namespace sim
