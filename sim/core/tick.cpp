#include "sim/core/tick.h"

#include <algorithm>

namespace sim {

TickClock::TickClock(uint32_t hz) : m_hz(hz == 0 ? kDefaultTickRate : hz), m_tick(0), m_accumulator(0.0) {}

int TickClock::advance(double frame_seconds, int max_catchup) {
    if (!(frame_seconds > 0.0)) return 0;
    m_accumulator += frame_seconds;

    const double step = dt();
    int ticks = 0;
    while (m_accumulator >= step && ticks < max_catchup) {
        m_accumulator -= step;
        ++m_tick;
        ++ticks;
    }
    if (ticks >= max_catchup) {
        // Drop the backlog; keeping it would make the next frame worse.
        m_accumulator = 0.0;
    }
    return ticks;
}

void TickClock::reset(uint32_t tick) {
    m_tick = tick;
    m_accumulator = 0.0;
}

}  // namespace sim
