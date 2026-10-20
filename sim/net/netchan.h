#pragma once

#include <cstdint>
#include <vector>

namespace sim {

enum class PacketType : uint8_t {
    Snapshot = 1,
    Command = 2,
    FullState = 3,
    // A2: the reliable stream shares the socket but keeps its own sequence.
    ReliableData = 4,
    ReliableAck = 5,
};

// Unreliable channel header (plan section 7.1): every packet carries its own
// sequence and the last sequence seen from the peer plus a 32-bit ack mask, which
// is all Q3's netchan needs for an unreliable stream. Nothing blocks on a lost
// packet.
struct PacketHeader {
    PacketType type = PacketType::Snapshot;
    uint16_t seq = 0;
    uint16_t ack = 0;
    uint32_t ack_bits = 0;
};

std::vector<uint8_t> wrap_packet(const PacketHeader& header, const std::vector<uint8_t>& payload);
bool unwrap_packet(const std::vector<uint8_t>& packet, PacketHeader& header,
                   const uint8_t*& payload, size_t& payload_size);

}  // namespace sim
