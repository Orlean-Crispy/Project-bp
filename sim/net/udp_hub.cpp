#include "sim/net/udp_hub.h"

namespace sim {

std::string UdpPeerHub::key(const std::string& host, uint16_t port) {
    return host + ":" + std::to_string(port);
}

void UdpPeerHub::poll() {
    std::vector<uint8_t> data;
    std::string host;
    uint16_t port = 0;
    while (m_socket.recv_from(data, host, port)) {
        const auto it = m_peers.find(key(host, port));
        if (it != m_peers.end()) {
            it->second->deliver(data);
        } else if (m_unknown.size() < 64) {  // bounded: never let strangers grow memory
            m_unknown.push_back({host, port, data});
        }
    }
}

bool UdpPeerHub::next_unknown(std::string& host, uint16_t& port, std::vector<uint8_t>& data) {
    if (m_unknown.empty()) return false;
    host = m_unknown.front().host;
    port = m_unknown.front().port;
    data = std::move(m_unknown.front().data);
    m_unknown.pop_front();
    return true;
}

Transport* UdpPeerHub::add_peer(const std::string& host, uint16_t port) {
    const std::string id = key(host, port);
    const auto it = m_links.find(id);
    if (it != m_links.end()) return it->second.get();
    auto peer = std::make_unique<PeerTransport>(m_socket, host, port);
    PeerTransport* raw = peer.get();
    m_peers.emplace(id, std::move(peer));
    auto link = std::make_unique<FragmentedTransport>(*raw);
    Transport* view = link.get();
    m_links.emplace(id, std::move(link));
    return view;
}

void UdpPeerHub::remove_peer(const std::string& host, uint16_t port) {
    // Drop the fragment view first: it holds a reference to the peer transport.
    m_links.erase(key(host, port));
    m_peers.erase(key(host, port));
}

}  // namespace sim
