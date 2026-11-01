#include "sim/net/event_wire.h"

#include "sim/core/event.h"
#include "sim/net/bytes.h"

namespace sim {

void append_events(ByteWriter& out, const SimEvent* events, int count, int skip_entity) {
    // Built aside first: ByteWriter has no in-place patch, and the count byte has to
    // come before events that are only known after the loop.
    ByteWriter body;
    uint8_t kept = 0;
    for (int i = 0; i < count; ++i) {
        if (kept >= kMaxEventsPerPacket) break;
        const SimEvent& e = events[i];
        if (e.type == EventType::None) continue;
        if (static_cast<int>(e.entity_id) == skip_entity) continue;
        body.u32(e.tick);
        body.u16(static_cast<uint16_t>(e.type));
        body.u16(e.entity_id);
        body.vec3(e.a);
        body.vec3(e.b);
        body.u32(static_cast<uint32_t>(e.param));
        ++kept;
    }
    out.u8(kept);
    if (kept != 0) out.raw(body.data().data(), body.size());
}

int read_events(const uint8_t* data, std::size_t size, EventQueue& out) {
    ByteReader r(data, size);
    const uint8_t count = r.u8();
    if (!r.ok() || count == 0 || count > kMaxEventsPerPacket) return 0;
    // Parsed aside: a truncated block must deliver nothing at all. Pushing as we
    // read would leave half a burst of tracers on screen with no way to retract it.
    SimEvent events[kMaxEventsPerPacket];
    for (uint8_t i = 0; i < count; ++i) {
        SimEvent& e = events[i];
        e.tick = r.u32();
        e.type = static_cast<EventType>(r.u16());
        e.entity_id = r.u16();
        e.a = r.vec3();
        e.b = r.vec3();
        e.param = static_cast<int32_t>(r.u32());
        if (!r.ok()) return 0;
    }
    for (uint8_t i = 0; i < count; ++i) out.push(events[i]);
    return count;
}

bool EventDedup::seen(uint32_t tick, uint16_t entity, uint16_t type) {
    for (const Key& k : m_keys) {
        if (k.tick == tick && k.entity == entity && k.type == type) return true;
    }
    m_keys[m_next] = Key{tick, entity, type};
    m_next = (m_next + 1) % kCapacity;
    return false;
}

}  // namespace sim