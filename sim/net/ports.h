#pragma once

#include <cstdint>

namespace sim {

// The only place a port number is written down (AUD-14). The game port is always
// the signal port + 1 (v0.4 section 7), so a firewall rule can separate the two
// and a host never has to type both.
//
// 27115 rather than v0.4's 27015: on Windows 27015/tcp is held by
// AppleMobileDeviceService and SO_REUSEADDR lets a hijacked bind report success
// (docs/pitfalls/port-hijack.md).
inline constexpr uint16_t kDefaultSignalPort = 27115;

// Two-process checks have to run next to a developer's session, so their port is
// derived from the default instead of being a second literal.
inline constexpr uint16_t kGatePortOffset = 15;
inline constexpr uint16_t kGateSignalPort = kDefaultSignalPort + kGatePortOffset;

constexpr uint16_t game_port_of(uint16_t signal_port) {
    return static_cast<uint16_t>(signal_port + 1);
}

}  // namespace sim
