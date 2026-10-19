#pragma once

#include <cstdint>
#include <vector>

namespace sim {

// Minimal v0.1 LAN handshake. A1 (TCP signalling + asset manifest) replaces this
// when assets exist; for now the only job is "prove you speak this build".
constexpr std::size_t kHelloBytes = 6;

std::vector<uint8_t> make_hello();
bool is_hello(const std::vector<uint8_t>& packet);

}  // namespace sim
