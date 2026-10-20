#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "sim/net/ticket.h"

namespace sim {

// A5 landing: the UDP hello carries the 16-byte client id the host already
// admitted over TCP, so a stranger's packet can never claim a slot. The ticket
// itself stays on the control plane (v0.4 6.1: a resume is a TCP HELLO).
constexpr std::size_t kHelloBytes = 6 + 16;

std::vector<uint8_t> make_hello(const ClientId& id);
bool is_hello(const std::vector<uint8_t>& packet);
bool hello_id(const std::vector<uint8_t>& packet, ClientId& out);

}  // namespace sim
