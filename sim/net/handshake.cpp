#include "sim/net/handshake.h"

#include <cstring>

#include "sim/core/version.h"

namespace sim {
namespace {
constexpr uint8_t kMagic[4] = {'B', 'P', 'H', '0'};
constexpr std::size_t kIdOffset = 6;
}  // namespace

std::vector<uint8_t> make_hello(const ClientId& id) {
    std::vector<uint8_t> out(kHelloBytes, 0);
    std::memcpy(out.data(), kMagic, sizeof(kMagic));
    out[4] = static_cast<uint8_t>(kProtocolVersion & 0xFF);
    out[5] = static_cast<uint8_t>(kProtocolVersion >> 8);
    std::memcpy(out.data() + kIdOffset, id.data(), id.size());
    return out;
}

bool is_hello(const std::vector<uint8_t>& packet) {
    if (packet.size() != kHelloBytes) return false;
    if (std::memcmp(packet.data(), kMagic, sizeof(kMagic)) != 0) return false;
    const uint16_t version = static_cast<uint16_t>(packet[4] | (packet[5] << 8));
    return version == kProtocolVersion;
}

bool hello_id(const std::vector<uint8_t>& packet, ClientId& out) {
    if (!is_hello(packet)) return false;
    std::memcpy(out.data(), packet.data() + kIdOffset, out.size());
    return true;
}

}  // namespace sim
