#pragma once

#include <array>
#include <cstdint>

namespace sim {

using ClientId = std::array<uint8_t, 16>;

// A5: 32-byte reconnect ticket, held only in host memory.
// client_id (16) + expiry (4, seconds) + HMAC-SHA256 truncated to 12 bytes.
struct Ticket {
    ClientId client_id{};
    uint32_t expiry = 0;
    std::array<uint8_t, 12> mac{};
};

class TicketIssuer {
public:
    TicketIssuer();  // random key, never written to disk
    explicit TicketIssuer(const std::array<uint8_t, 32>& key);

    Ticket issue(const ClientId& client_id, uint32_t now_s, uint32_t ttl_s = 120) const;
    bool verify(const Ticket& ticket, uint32_t now_s) const;

private:
    std::array<uint8_t, 32> m_key{};
};

}  // namespace sim
