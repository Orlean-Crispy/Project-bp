#pragma once

#include <chrono>
#include <cstdint>

namespace sim {

constexpr uint32_t kDefaultTickRate = 64;

// Fixed-step accumulator. The simulation never sees frame time: it only ever
// advances in whole ticks, so results do not depend on the renderer's frame rate.
class TickClock {
public:
    explicit TickClock(uint32_t hz = kDefaultTickRate);

    uint32_t hz() const { return m_hz; }
    double dt() const { return 1.0 / static_cast<double>(m_hz); }
    uint32_t tick() const { return m_tick; }

    // Feeds one frame of wall time; returns how many whole ticks to simulate.
    // Excess time beyond max_catchup ticks is dropped rather than accumulated,
    // which prevents a single long stall from triggering a death spiral.
    int advance(double frame_seconds, int max_catchup = 5);

    void reset(uint32_t tick = 0);

private:
    uint32_t m_hz;
    uint32_t m_tick;
    double m_accumulator;
};

// Wall-clock helper. std::chrono::steady_clock starts at boot, so subtracting a
// larger value from a smaller one underflows on a freshly booted machine.
// Always go through these two functions.
inline int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

inline int64_t elapsed_ns(int64_t since_ns) {
    const int64_t now = now_ns();
    return now >= since_ns ? now - since_ns : 0;
}

}  // namespace sim
