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
// sequence and the last sequence seen from the peer. Nothing blocks on a lost
// packet.
//
// ack_bits is on the wire but always zero in v0.1: the delta path only needs the
// cumulative ack (baseline = the snapshot the client acknowledged). Filling the
// mask would let the server pick a better baseline, but nothing consumes it yet,
// so it is documented here instead of pretending to work (AUDIT open item).
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
