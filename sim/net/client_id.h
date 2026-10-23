#pragma once

#include <array>
#include <cstdint>

namespace sim {

// The stable identity of one client across reconnects: 16 bytes minted when the
// process starts, repeated in the TCP hello, the UDP hello and the A5 ticket.
// It is not a session id - a reconnect reuses it on purpose. It lives in its own
// header so that session/ does not have to include the ticket format to name a
// player.
using ClientId = std::array<uint8_t, 16>;

}  // namespace sim
