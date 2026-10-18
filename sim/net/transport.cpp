#include "sim/net/transport.h"

#include <algorithm>

#include "sim/core/rng.h"

namespace sim {

LoopbackTransport::LoopbackTransport(NetConditions conditions, uint32_t seed)
    : m_conditions(conditions), m_seed(seed) {}

void LoopbackTransport::send(const std::vector<uint8_t>& data) {
    ++m_sent;
    const uint32_t seq = m_seq++;

    // Loss and duplication are decided per packet from (seed, seq), so the same
    // run always drops the same packets.
    const float roll = static_cast<float>(rng_at(seq, 0, 0, m_seed) >> 8) * (1.0f / 16777216.0f);
    if (roll < m_conditions.loss) {
        ++m_dropped;
        return;
    }

    const float jitter = m_conditions.jitter_ms > 0.0f
                             ? rng_signed(seq, 1, 0, m_seed) * m_conditions.jitter_ms
                             : 0.0f;
    const float delay_ms = m_conditions.latency_ms + jitter;
    const uint32_t delay_ticks =
        static_cast<uint32_t>(std::max(1.0f, delay_ms * 64.0f / 1000.0f + 0.5f));

    InFlight packet;
    packet.data = data;
    packet.deliver_tick = delay_ticks;  // ticks relative to now; advance() counts down
    packet.seq = seq;
    m_queue.push_back(std::move(packet));

    const float dup = static_cast<float>(rng_at(seq, 2, 0, m_seed) >> 8) * (1.0f / 16777216.0f);
    if (dup < m_conditions.duplicate) {
        InFlight copy = m_queue.back();
        copy.seq = seq;
        m_queue.push_back(std::move(copy));
    }
}

bool LoopbackTransport::recv(std::vector<uint8_t>& out) {
    if (m_inbox.empty()) return false;
    out = std::move(m_inbox.front());
    m_inbox.erase(m_inbox.begin());
    return true;
}

void LoopbackTransport::advance(uint32_t tick) {
    (void)tick;
    for (size_t i = 0; i < m_queue.size();) {
        if (m_queue[i].deliver_tick == 0) {
            m_inbox.push_back(std::move(m_queue[i].data));
            ++m_delivered;
            m_queue.erase(m_queue.begin() + static_cast<long>(i));
        } else {
            --m_queue[i].deliver_tick;
            ++i;
        }
    }
}

}  // namespace sim
