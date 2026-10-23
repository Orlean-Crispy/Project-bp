#include "sim/net/udp_transport.h"

namespace sim {

bool UdpTransport::open(uint16_t local_port, std::string& error) {
    return m_socket.open(local_port, error);
}

bool UdpTransport::connect_peer(const std::string& host, uint16_t port, std::string& error) {
    if (!m_socket.valid()) return false;
    // Store the canonical dotted form, because recv() compares it with the text
    // inet_ntop produces. Storing the caller's spelling ("127.000.000.001") would
    // make every packet look like it came from a stranger.
    std::string canonical;
    if (!ipv4_canonical(host, canonical)) {
        error = "udp peer '" + host + "' is not an IPv4 address";
        return false;
    }
    m_peer_host = canonical;
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
    // AUD-26: a connected endpoint talks to exactly one peer. The old code threw
    // the source address away, so any host that could reach this ephemeral port
    // could inject snapshots or commands into a live client. A stray datagram is
    // dropped and counted, and recv() keeps draining instead of returning early
    // (otherwise one junk packet would hide a real one behind it for a tick).
    std::string host;
    uint16_t port = 0;
    while (m_socket.recv_from(out, host, port)) {
        if (!m_has_peer || (host == m_peer_host && port == m_peer_port)) return true;
        ++m_rejected;
    }
    return false;
}

bool UdpTransport::recv_from(std::vector<uint8_t>& out, std::string& host, uint16_t& port) {
    return m_socket.recv_from(out, host, port);
}

}  // namespace sim
