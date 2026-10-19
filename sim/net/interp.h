#pragma once

#include <cstdint>
#include <vector>

#include "sim/net/snapshot.h"

namespace sim {

// Plan 7.1: other players are shown one tick behind the newest snapshot, and the
// renderer samples between the two frames that bracket its render tick. This is
// presentation-only: the local player is predicted, hit detection uses the
// hitbox history, and nothing here feeds back into pmove.
class SnapshotInterpolator {
public:
    static constexpr int kCapacity = 32;  // 0.5 s at 64 Hz

    void push(const Snapshot& snap);
    void reset();

    bool empty() const { return m_count == 0; }
    int size() const { return m_count; }
    uint32_t newest_tick() const { return m_count == 0 ? 0 : m_newest; }
    uint32_t oldest_tick() const;

    // tick + frac, frac in [0,1]. Outside the buffered range the nearest frame
    // is returned unchanged (no extrapolation).
    bool sample(uint32_t tick, float frac, std::vector<PlayerSnap>& out) const;

private:
    struct Entry {
        uint32_t tick = 0;
        bool valid = false;
        Snapshot snap;
    };

    const Entry* find(uint32_t tick) const;
    const Snapshot& nearest(uint32_t tick) const;

    Entry m_entries[kCapacity];
    int m_count = 0;
    uint32_t m_newest = 0;
};

}  // namespace sim
