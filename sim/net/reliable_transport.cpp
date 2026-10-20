#include "sim/net/reliable_transport.h"

#include "sim/core/tick.h"
#include "sim/net/netchan.h"

namespace sim {
namespace {

constexpr uint8_t kDataTag = static_cast<uint8_t>(PacketType::ReliableData);
constexpr uint8_t kAckTag = static_cast<uint8_t>(PacketType::ReliableAck);

uint16_t get_u16(const uint8_t* p) {
    return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) |
                                 static_cast<uint16_t>(p[1] << 8));
}

uint32_t get_u32(const uint8_t* p) {
    return static_cast<uint32_t>(get_u16(p)) |
           (static_cast<uint32_t>(get_u16(p + 2)) << 16);
}

}  // namespace

ReliableTransport::ReliableTransport(Transport* tx, Transport* rx, uint16_t window)
    : m_tx(tx), m_rx(rx), m_sender(window), m_receiver(window) {}

void ReliableTransport::send(const std::vector<uint8_t>& data) { m_tx->send(data); }

bool ReliableTransport::recv(std::vector<uint8_t>& out) {
    std::vector<uint8_t> pkt;
    while (m_rx->recv(pkt)) {
        if (pkt.empty()) continue;
        if (pkt[0] == kDataTag) {
            // ReliableReceiver speaks its own [1][seq][payload] framing.
            pkt[0] = 1;
            std::vector<std::vector<uint8_t>> got;
            m_receiver.on_packet(pkt.data(), pkt.size(), got);
            for (std::vector<uint8_t>& piece : got) {
                m_inbox.push_back(std::move(piece));
            }
            m_ack_pending = true;
            continue;
        }
        if (pkt[0] == kAckTag) {
            if (pkt.size() >= 7) {
                m_sender.on_ack(get_u16(pkt.data() + 1), get_u32(pkt.data() + 3), m_now_ms);
            }
            continue;
        }
        out = std::move(pkt);
        return true;
    }
    return false;
}

void ReliableTransport::advance(uint32_t tick) {
    // Rollback moves the client clock backwards; RTO math must never see that,
    // or `now - last_sent` wraps and every packet looks overdue.
    const uint32_t raw = ticks_to_millis(tick);
    if (raw > m_now_ms) m_now_ms = raw;
    flush(m_now_ms);
}

void ReliableTransport::flush(uint32_t now_ms) {
    std::vector<std::vector<uint8_t>> due;
    m_sender.poll(now_ms, due);
    for (std::vector<uint8_t>& pkt : due) {
        pkt[0] = kDataTag;
        m_tx->send(pkt);
    }
    if (m_ack_pending) {
        std::vector<uint8_t> ack;
        m_receiver.make_ack(ack);
        if (!ack.empty()) {
            ack[0] = kAckTag;
            m_tx->send(ack);
        }
        m_ack_pending = false;
    }
}

bool ReliableTransport::send_reliable(const std::vector<uint8_t>& payload) {
    return m_sender.send(payload, m_now_ms);
}

bool ReliableTransport::recv_reliable(std::vector<uint8_t>& out) {
    if (m_inbox.empty()) return false;
    out = std::move(m_inbox.front());
    m_inbox.pop_front();
    return true;
}

}  // namespace sim
