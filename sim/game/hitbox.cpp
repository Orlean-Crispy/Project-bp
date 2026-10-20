#include "sim/game/hitbox.h"

#include <cmath>

namespace sim {
namespace {

// Plan 6.8 thresholds. Kept as ratio-of-height so crouching needs no special case.
constexpr float kHeadStart = 0.82f;
constexpr float kChestStart = 0.45f;

}  // namespace

HitZone hit_zone_at(const HitboxSample& sample, float y) {
    if (sample.height <= 0.0f) return HitZone::Chest;
    const float ratio = (y - sample.pos.z) / sample.height;
    if (ratio > kHeadStart) return HitZone::Head;
    if (ratio > kChestStart) return HitZone::Chest;
    return HitZone::Legs;
}

float zone_multiplier(HitZone zone, const MovementParams& params) {
    switch (zone) {
        case HitZone::Head:
            // plan 6.10: v0.3 raised the headshot multiplier from 4; the value
            // is tuning data now (movement.toml [damage].headshot_mult).
            return params.headshot_mult;
        case HitZone::Chest:
            return 1.0f;
        case HitZone::Legs:
            return 0.7f;
    }
    return 1.0f;
}

float hit_multiplier(const HitboxSample& sample, float y, const MovementParams& params) {
    return zone_multiplier(hit_zone_at(sample, y), params);
}

bool hitbox_contains(const HitboxSample& sample, const Vec3& point) {
    if (std::fabs(point.x - sample.pos.x) > sample.radius) return false;
    if (std::fabs(point.y - sample.pos.y) > sample.radius) return false;
    return point.z >= sample.pos.z && point.z <= sample.pos.z + sample.height;
}

void HitboxHistory::record(const PlayerState& player, const MovementParams& params,
                           uint32_t tick) {
    Vec3 mins;
    Vec3 maxs;
    player_hull(player, params, mins, maxs);

    Entry& entry = m_entries[tick % static_cast<uint32_t>(kCapacity)];
    if (!entry.valid) ++m_count;
    entry.valid = true;
    entry.tick = tick;
    entry.sample.pos = player.pos;
    entry.sample.height = maxs.z - mins.z;
    entry.sample.radius = maxs.x;
    entry.sample.crouched = player.crouched;
    entry.sample.tick = tick;

    // O(1): the oldest frame is only needed by diagnostics/tests, so it is
    // scanned on demand instead of on every recorded tick.
    if (m_count == 1 || tick > m_newest) m_newest = tick;
}

uint32_t HitboxHistory::oldest_tick() const {
    uint32_t oldest = 0;
    bool first = true;
    for (const Entry& e : m_entries) {
        if (!e.valid) continue;
        if (first || e.tick < oldest) oldest = e.tick;
        first = false;
    }
    return oldest;
}

void HitboxHistory::reset() {
    for (Entry& e : m_entries) {
        e.valid = false;
        e.tick = 0;
        e.sample = HitboxSample{};
    }
    m_count = 0;
    m_newest = 0;
}

const HitboxHistory::Entry* HitboxHistory::find(uint32_t tick) const {
    const Entry& e = m_entries[tick % static_cast<uint32_t>(kCapacity)];
    return e.valid && e.tick == tick ? &e : nullptr;
}

bool HitboxHistory::has(uint32_t tick) const { return find(tick) != nullptr; }

HitboxSample HitboxHistory::nearest(uint32_t tick) const {
    const Entry* best = nullptr;
    for (const Entry& e : m_entries) {
        if (!e.valid) continue;
        if (best == nullptr) {
            best = &e;
            continue;
        }
        const uint32_t d = e.tick > tick ? e.tick - tick : tick - e.tick;
        const uint32_t bd = best->tick > tick ? best->tick - tick : tick - best->tick;
        if (d < bd) best = &e;
    }
    return best != nullptr ? best->sample : HitboxSample{};
}

HitboxSample HitboxHistory::at(uint32_t tick) const {
    const Entry* exact = find(tick);
    if (exact != nullptr) return exact->sample;
    return nearest(tick);
}

HitboxSample HitboxHistory::sample(uint32_t tick, float frac) const {
    const Entry* a = find(tick);
    const Entry* b = find(tick + 1);
    if (a == nullptr) return nearest(tick);
    if (b == nullptr || frac <= 0.0f) return a->sample;

    const float t = frac > 1.0f ? 1.0f : frac;
    HitboxSample out = a->sample;
    out.pos = a->sample.pos + (b->sample.pos - a->sample.pos) * t;
    out.height = a->sample.height + (b->sample.height - a->sample.height) * t;
    out.radius = a->sample.radius + (b->sample.radius - a->sample.radius) * t;
    out.crouched = a->sample.crouched;
    out.tick = tick;
    return out;
}

}  // namespace sim
