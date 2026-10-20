#pragma once

#include <cstdint>
#include <vector>

namespace sim {

// A datagram transport. The real implementation will wrap a UDP socket; tests use
// the loopback below with configurable loss, latency, jitter and duplication.
class Transport {
public:
    virtual ~Transport() = default;
    virtual void send(const std::vector<uint8_t>& data) = 0;
    virtual bool recv(std::vector<uint8_t>& out) = 0;
    virtual void advance(uint32_t tick) = 0;
};

struct NetConditions {
    float loss = 0.0f;        // 0..1
    float latency_ms = 0.0f;
    float jitter_ms = 0.0f;
    float duplicate = 0.0f;   // 0..1
};

// Deterministic in-process channel: delivery times are derived from the tick and
// a counter-based RNG, never from wall time, so a test replay is reproducible.
class LoopbackTransport : public Transport {
public:
    LoopbackTransport(NetConditions conditions, uint32_t seed);

    void send(const std::vector<uint8_t>& data) override;
    bool recv(std::vector<uint8_t>& out) override;
    void advance(uint32_t tick) override;

    int dropped_packets() const { return m_dropped; }

private:
    struct InFlight {
        std::vector<uint8_t> data;
        uint32_t deliver_tick = 0;
        uint32_t seq = 0;
    };

    NetConditions m_conditions;
    uint32_t m_seed;
    uint32_t m_seq = 0;
    std::vector<InFlight> m_queue;
    std::vector<std::vector<uint8_t>> m_inbox;
    int m_dropped = 0;
};

}  // namespace sim
