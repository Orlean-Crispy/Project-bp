#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sim/net/transport.h"

namespace sim {

// Real datagram transport (plan 7.1: unreliable UDP). One object is one peer:
// a client connects to the host, a listen server adopts the address of the first
// datagram it receives. The socket is non-blocking, so recv() never waits.
class UdpTransport : public Transport {
public:
    ~UdpTransport() override;

    UdpTransport() = default;
    UdpTransport(const UdpTransport&) = delete;
    UdpTransport& operator=(const UdpTransport&) = delete;

    bool open(uint16_t local_port, std::string& error);       // 0 = ephemeral
    bool connect_peer(const std::string& host, uint16_t port, std::string& error);
    bool adopt_peer(const std::string& host, uint16_t port, std::string& error);

    void send(const std::vector<uint8_t>& data) override;
    bool recv(std::vector<uint8_t>& out) override;
    // Server-side receive that reports the sender; call adopt_peer() with it,
    // then the normal send()/recv() pair talks to that peer.
    bool recv_from(std::vector<uint8_t>& out, std::string& host, uint16_t& port);
    void advance(uint32_t tick) override { (void)tick; }

    bool valid() const { return m_socket != kInvalid; }
    uint16_t local_port() const { return m_local_port; }

private:
    static constexpr uintptr_t kInvalid = ~static_cast<uintptr_t>(0);

    uintptr_t m_socket = kInvalid;
    uint16_t m_local_port = 0;
    uint32_t m_peer_addr = 0;  // network byte order
    uint16_t m_peer_port = 0;  // network byte order
    bool m_has_peer = false;
};

}  // namespace sim
