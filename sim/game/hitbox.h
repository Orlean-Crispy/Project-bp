#pragma once

#include <cstdint>

#include "sim/move/pmove.h"

namespace sim {

// Plan 6.8: three height bands, no bones. The bands are fractions of the
// movement hull height, so crouching follows automatically.
enum class HitZone : uint8_t {
    Legs = 0,
    Chest = 1,
    Head = 2,
};

// One tick of a player's damageable volume (plan 7.3). Multipliers are not
// stored per sample: zone_multiplier() is the single source and never changes.
struct HitboxSample {
    Vec3 pos;  // feet position
    float height = 1.42f;
    float radius = 0.405f;
    bool crouched = false;
    uint32_t tick = 0;
};

HitZone hit_zone_at(const HitboxSample& sample, float y);
float zone_multiplier(HitZone zone);
float hit_multiplier(const HitboxSample& sample, float y);
bool hitbox_contains(const HitboxSample& sample, const Vec3& point);

// Plan 7.3: 64 ticks = 1 second at 64 Hz, recorded every tick by both sides.
// v0.4 9.2: hit tests interpolate the two frames around the rewind tick, because
// a whole-tick step is visible on a 600 u/s slide.
class HitboxHistory {
public:
    static constexpr int kCapacity = 64;

    void record(const PlayerState& player, const MovementParams& params, uint32_t tick);
    void reset();

    bool has(uint32_t tick) const;
    HitboxSample at(uint32_t tick) const;
    HitboxSample sample(uint32_t tick, float frac) const;

    int size() const { return m_count; }
    uint32_t oldest_tick() const { return m_count == 0 ? 0 : m_oldest; }
    uint32_t newest_tick() const { return m_count == 0 ? 0 : m_newest; }

private:
    struct Entry {
        uint32_t tick = 0;
        bool valid = false;
        HitboxSample sample;
    };

    const Entry* find(uint32_t tick) const;
    HitboxSample nearest(uint32_t tick) const;

    Entry m_entries[kCapacity];
    int m_count = 0;
    uint32_t m_oldest = 0;
    uint32_t m_newest = 0;
};

}  // namespace sim
