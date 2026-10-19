#include "sim/core/event.h"

namespace sim {

void EventQueue::push(const SimEvent& event) {
    if (m_count == kCapacity) {
        m_head = (m_head + 1) % kCapacity;
        --m_count;
        ++m_dropped;
    }
    m_events[(m_head + m_count) % kCapacity] = event;
    ++m_count;
}

bool EventQueue::pop(SimEvent& out) {
    if (m_count == 0) return false;
    out = m_events[m_head];
    m_head = (m_head + 1) % kCapacity;
    --m_count;
    return true;
}

void EventQueue::clear() {
    m_head = 0;
    m_count = 0;
}

}  // namespace sim
