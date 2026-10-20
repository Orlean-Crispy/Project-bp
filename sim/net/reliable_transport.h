#pragma once

#include <cstdint>
#include <deque>
#include <vector>

#include "sim/net/reliable.h"
#include "sim/net/transport.h"

namespace sim {

// A2: one socket, two semantics. Snapshots and inputs keep their unreliable
// path untouched; reliable messages ride the same pipe in their own sequence
// space (v0.4 3.2) and are tagged so the receiver can demux without guessing.
//
// The wrapper does NOT advance the underlying pipes: the caller owns the tick
// clock and advances each direction exactly once.
class ReliableTransport : public Transport {
public:
    ReliableTransport(Transport* tx, Transport* rx, uint16_t window = 32);
    ReliableTransport(const ReliableTransport&) = delete;
    ReliableTransport& operator=(const ReliableTransport&) = delete;

    // Transport: unreliable passthrough.
    void send(const std::vector<uint8_t>& data) override;
    bool recv(std::vector<uint8_t>& out) override;
    void advance(uint32_t tick) override;

    // Reliable ordered stream.
    bool send_reliable(const std::vector<uint8_t>& payload);
    bool recv_reliable(std::vector<uint8_t>& out);
    int retransmits() const { return m_sender.retransmits(); }
    bool dead() const { return m_sender.dead(); }

private:
    void flush(uint32_t now_ms);

    Transport* m_tx = nullptr;
    Transport* m_rx = nullptr;
    ReliableSender m_sender;
    ReliableReceiver m_receiver;
    std::deque<std::vector<uint8_t>> m_inbox;
    uint32_t m_now_ms = 0;
    bool m_ack_pending = false;
};

}  // namespace sim
