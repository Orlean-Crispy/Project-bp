#include "sim/net/udp_transport.h"

namespace sim {

bool UdpTransport::open(uint16_t local_port, std::string& error) {
    return m_socket.open(local_port, error);
}

bool UdpTransport::connect_peer(const std::string& host, uint16_t port, std::string&) {
    if (!m_socket.valid()) return false;
    m_peer_host = host;
    m_peer_port = port;
    m_has_peer = true;
    return true;
}

bool UdpTransport::adopt_peer(const std::string& host, uint16_t port, std::string& error) {
    return connect_peer(host, port, error);
}

void UdpTransport::send(const std::vector<uint8_t>& data) {
    if (!m_has_peer) return;
    m_socket.send_to(data.data(), data.size(), m_peer_host, m_peer_port);
}

bool UdpTransport::recv(std::vector<uint8_t>& out) {
    std::string host;
    uint16_t port = 0;
    return m_socket.recv_from(out, host, port);
}

bool UdpTransport::recv_from(std::vector<uint8_t>& out, std::string& host, uint16_t& port) {
    return m_socket.recv_from(out, host, port);
}

}  // namespace sim
