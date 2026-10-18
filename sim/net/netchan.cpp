#include "sim/net/netchan.h"

#include "sim/net/bytes.h"

namespace sim {
namespace {
constexpr size_t kHeaderSize = 1 + 2 + 2 + 4;
}

std::vector<uint8_t> wrap_packet(const PacketHeader& header, const std::vector<uint8_t>& payload) {
    ByteWriter w;
    w.u8(static_cast<uint8_t>(header.type));
    w.u16(header.seq);
    w.u16(header.ack);
    w.u32(header.ack_bits);
    w.raw(payload.data(), payload.size());
    return w.data();
}

bool unwrap_packet(const std::vector<uint8_t>& packet, PacketHeader& header,
                   const uint8_t*& payload, size_t& payload_size) {
    if (packet.size() < kHeaderSize) return false;
    ByteReader r(packet.data(), packet.size());
    header.type = static_cast<PacketType>(r.u8());
    header.seq = r.u16();
    header.ack = r.u16();
    header.ack_bits = r.u32();
    if (!r.ok()) return false;
    payload = packet.data() + kHeaderSize;
    payload_size = packet.size() - kHeaderSize;
    return true;
}

void AckTracker::on_receive(uint16_t seq, uint32_t ack_bits) {
    if (!m_has_latest) {
        m_latest = seq;
        m_bits = ack_bits;
        m_has_latest = true;
        return;
    }
    // Only advance forward; a reordered older packet must not rewind the window.
    const uint16_t diff = static_cast<uint16_t>(seq - m_latest);
    if (diff == 0 || diff >= 0x8000) return;
    m_latest = seq;
    m_bits = ack_bits;
}

bool AckTracker::is_acked(uint16_t seq) const {
    if (!m_has_latest) return false;
    const uint16_t diff = static_cast<uint16_t>(m_latest - seq);
    if (diff >= 32) return false;
    // ack_bits bit 0 means "m_latest - 1 acknowledged".
    if (diff == 0) return true;
    return (m_bits & (1u << (diff - 1))) != 0;
}

}  // namespace sim
