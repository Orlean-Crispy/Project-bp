#include "sim/net/interp.h"

#include <cmath>

namespace sim {

void SnapshotInterpolator::push(const Snapshot& snap) {
    if (snap.players.empty()) return;

    const Entry& slot = m_entries[snap.tick % static_cast<uint32_t>(kCapacity)];
    if (slot.valid && slot.tick == snap.tick) return;  // duplicate / reordered

    // A gap larger than the ring means every older frame is unusable.
    if (m_count > 0 && snap.tick > m_newest + static_cast<uint32_t>(kCapacity)) {
        reset();
    }

    Entry& entry = m_entries[snap.tick % static_cast<uint32_t>(kCapacity)];
    if (!entry.valid) ++m_count;
    entry.valid = true;
    entry.tick = snap.tick;
    entry.snap = snap;
    if (m_count == 1 || snap.tick > m_newest) m_newest = snap.tick;
}

void SnapshotInterpolator::reset() {
    for (Entry& e : m_entries) {
        e.valid = false;
        e.tick = 0;
        e.snap = Snapshot{};
    }
    m_count = 0;
    m_newest = 0;
}

const SnapshotInterpolator::Entry* SnapshotInterpolator::find(uint32_t tick) const {
    const Entry& e = m_entries[tick % static_cast<uint32_t>(kCapacity)];
    return e.valid && e.tick == tick ? &e : nullptr;
}

const Snapshot& SnapshotInterpolator::nearest(uint32_t tick) const {
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
    static const Snapshot kEmpty;
    return best != nullptr ? best->snap : kEmpty;
}

bool SnapshotInterpolator::sample(uint32_t tick, float frac,
                                  std::vector<PlayerSnap>& out) const {
    if (m_count == 0) return false;

    const Entry* a = find(tick);
    const Entry* b = find(tick + 1);
    if (a == nullptr) {
        out = nearest(tick).players;
        return true;
    }
    if (b == nullptr || frac <= 0.0f) {
        out = a->snap.players;
        return true;
    }

    const float t = frac > 1.0f ? 1.0f : frac;
    const size_t count = a->snap.players.size() < b->snap.players.size()
                             ? a->snap.players.size()
                             : b->snap.players.size();
    out.resize(count);
    for (size_t i = 0; i < count; ++i) {
        const PlayerSnap& pa = a->snap.players[i];
        const PlayerSnap& pb = b->snap.players[i];
        PlayerSnap p = pa;
        p.pos = pa.pos + (pb.pos - pa.pos) * t;
        p.vel = pa.vel + (pb.vel - pa.vel) * t;
        out[i] = p;
    }
    return true;
}

}  // namespace sim
