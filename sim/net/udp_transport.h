#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sim/net/transport.h"
#include "sim/net/udp_socket.h"

namespace sim {

// One connected UDP endpoint (plan 7.1: unreliable datagrams). The socket is
// non-blocking, so recv() never waits for the network.
class UdpTransport : public Transport {
public:
    UdpTransport() = default;
    UdpTransport(const UdpTransport&) = delete;
    UdpTransport& operator=(const UdpTransport&) = delete;

    bool open(uint16_t local_port, std::string& error);
    bool connect_peer(const std::string& host, uint16_t port, std::string& error);
    bool adopt_peer(const std::string& host, uint16_t port, std::string& error);

    void send(const std::vector<uint8_t>& data) override;
    bool recv(std::vector<uint8_t>& out) override;
    bool recv_from(std::vector<uint8_t>& out, std::string& host, uint16_t& port);
    void advance(uint32_t tick) override { (void)tick; }

    bool valid() const { return m_socket.valid(); }
    uint16_t local_port() const { return m_socket.local_port(); }

private:
    UdpSocket m_socket;
    std::string m_peer_host;
    uint16_t m_peer_port = 0;
    bool m_has_peer = false;
};

}  // namespace sim
