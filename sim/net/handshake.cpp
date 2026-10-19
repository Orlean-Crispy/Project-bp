#include "sim/net/handshake.h"

#include "sim/core/version.h"

namespace sim {
namespace {
constexpr uint8_t kMagic[4] = {'B', 'P', 'H', '0'};
}

std::vector<uint8_t> make_hello() {
    std::vector<uint8_t> out(kHelloBytes, 0);
    out[0] = kMagic[0];
    out[1] = kMagic[1];
    out[2] = kMagic[2];
    out[3] = kMagic[3];
    out[4] = static_cast<uint8_t>(kProtocolVersion & 0xFF);
    out[5] = static_cast<uint8_t>(kProtocolVersion >> 8);
    return out;
}

bool is_hello(const std::vector<uint8_t>& packet) {
    if (packet.size() != kHelloBytes) return false;
    if (packet[0] != kMagic[0] || packet[1] != kMagic[1] || packet[2] != kMagic[2] ||
        packet[3] != kMagic[3]) {
        return false;
    }
    const uint16_t version = static_cast<uint16_t>(packet[4] | (packet[5] << 8));
    return version == kProtocolVersion;
}

}  // namespace sim
