#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "sim/net/transport.h"

namespace sim {

// Plan v0.4 B1: one datagram must stay under 1200 bytes. This decorator splits
// larger packets into fragments and reassembles them on the peer. Small packets
// (the common snapshot path) pass through untouched and allocation-free.
class FragmentedTransport : public Transport {
public:
    static constexpr std::size_t kMaxDatagramBytes = 1200;
    static constexpr std::size_t kPayloadBytes = 1100;
    static constexpr std::size_t kHeaderBytes = 5;

    explicit FragmentedTransport(Transport& inner) : m_inner(inner) {}

    void send(const std::vector<uint8_t>& packet) override;
    bool recv(std::vector<uint8_t>& out) override;
    void advance(uint32_t tick) override { m_inner.advance(tick); }

    // Counters for the real-link gate (AUD-23): how many datagrams the decorator
    // split, and how many messages it reassembled whole. Both are zero on the
    // normal snapshot path, which is the point.
    int dropped_packets() const { return m_dropped; }
    int fragments_sent() const { return m_fragments_sent; }
    int reassembled() const { return m_reassembled; }

private:
    void abandon();

    Transport& m_inner;
    uint16_t m_next_id = 1;
    uint16_t m_id = 0;
    uint8_t m_total = 0;
    uint8_t m_received = 0;
    std::vector<uint8_t> m_buffer;
    std::vector<uint16_t> m_sizes;
    int m_dropped = 0;
    int m_fragments_sent = 0;
    int m_reassembled = 0;
};

}  // namespace sim
