#pragma once

#include <cstddef>
#include <cstdint>

#include "sim/core/types.h"

namespace sim {

// Plan 3.4: the second cross-boundary channel. State streams describe "what is";
// events describe "what happened this tick" (jump, land, and later muzzle,
// impact, footstep, reload). Events never carry authoritative data: damage and
// health stay in snapshots.
enum class EventType : uint16_t {
    None = 0,
    Jump = 1,
    Land = 2,
    // M6.11: one shot's tracer. a = muzzle, b = impact point (or where the ray
    // ran out). param = weapon slot. Visual only: damage never rides an event.
    Shot = 3,
    // Reserved for M6+: Impact, Footstep, Reload, Break...
};

struct SimEvent {
    uint32_t tick = 0;               // lets the consumer drop predicted-but-rejected events
    EventType type = EventType::None;
    uint16_t entity_id = 0;
    Vec3 a;                          // position (or start)
    Vec3 b;                          // end / normal
    int32_t param = 0;               // material, damage, weapon id
};

// Fixed ring, written by the simulation and drained by the presentation layer.
// One second of events at 64 Hz is 1024 entries; overflow drops the oldest and
// counts it instead of allocating.
class EventQueue {
public:
    static constexpr std::size_t kCapacity = 1024;

    void push(const SimEvent& event);
    bool pop(SimEvent& out);
    void clear();

    std::size_t size() const { return m_count; }
    uint64_t dropped() const { return m_dropped; }

private:
    SimEvent m_events[kCapacity];
    std::size_t m_head = 0;
    std::size_t m_count = 0;
    uint64_t m_dropped = 0;
};

}  // namespace sim
