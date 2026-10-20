#pragma once

#include <cstdint>

namespace sim {

// Counter-based (stateless) RNG, per plan section 7.8.
// Every random decision is addressable by (tick, entity, index, salt) so that a
// client replaying tick N gets the same value as the server that ran tick N.
// No global state, no rand(), no std::mt19937 anywhere in sim/.
inline uint32_t rng_at(uint32_t tick, uint16_t entity, uint16_t index, uint32_t salt) {
    uint32_t x = tick * 0x9E3779B9u
               ^ static_cast<uint32_t>(entity) * 0x85EBCA6Bu
               ^ static_cast<uint32_t>(index) * 0xC2B2AE35u
               ^ salt * 0x27D4EB2Fu;
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

// Uniform in [0, 1). 24-bit mantissa keeps the result identical on every IEEE754
// machine without needing doubles.
inline float rng_unit(uint32_t tick, uint16_t entity, uint16_t index, uint32_t salt) {
    return static_cast<float>(rng_at(tick, entity, index, salt) >> 8) * (1.0f / 16777216.0f);
}

// Uniform in [-1, 1).
inline float rng_signed(uint32_t tick, uint16_t entity, uint16_t index, uint32_t salt) {
    return rng_unit(tick, entity, index, salt) * 2.0f - 1.0f;
}

}  // namespace sim
