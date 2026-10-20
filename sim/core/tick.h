#pragma once

#include <chrono>
#include <cstdint>

namespace sim {

constexpr uint32_t kDefaultTickRate = 64;

// The tick rate is stated once. Anything that needs a duration (seconds per
// tick, a timeout in ticks, a tick count in milliseconds) derives it here
// instead of repeating 64 (audit R14).
constexpr float tick_seconds(uint32_t hz = kDefaultTickRate) {
    return 1.0f / static_cast<float>(hz);
}

constexpr uint32_t seconds_to_ticks(float seconds, uint32_t hz = kDefaultTickRate) {
    return static_cast<uint32_t>(seconds * static_cast<float>(hz) + 0.5f);
}

constexpr uint32_t ticks_to_millis(uint32_t ticks, uint32_t hz = kDefaultTickRate) {
    return static_cast<uint32_t>((static_cast<uint64_t>(ticks) * 1000ull) / hz);
}

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
