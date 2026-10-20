#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "sim/net/transport.h"
#include "sim/net/udp_socket.h"

namespace sim {

// One peer on a shared server socket. The hub demultiplexes incoming datagrams
// by source address, so NetServer sees a normal Transport per client.
class PeerTransport : public Transport {
public:
    PeerTransport(UdpSocket& socket, std::string host, uint16_t port)
        : m_socket(socket), m_host(std::move(host)), m_port(port) {}

    void send(const std::vector<uint8_t>& data) override {
        m_socket.send_to(data.data(), data.size(), m_host, m_port);
    }
    bool recv(std::vector<uint8_t>& out) override {
        if (m_inbox.empty()) return false;
        out = std::move(m_inbox.front());
        m_inbox.pop_front();
        return true;
    }
    void advance(uint32_t tick) override { (void)tick; }

    // A peer whose link the server dropped keeps sending until its own timeout.
    // The hub removes the peer on that path, but the cap is the safety net: an
    // uncapped queue grows for the rest of the match at packet rate.
    void deliver(std::vector<uint8_t> data) {
        constexpr std::size_t kMaxInbox = 64;
        if (m_inbox.size() >= kMaxInbox) m_inbox.pop_front();
        m_inbox.push_back(std::move(data));
    }
    const std::string& host() const { return m_host; }
    uint16_t port() const { return m_port; }

private:
    UdpSocket& m_socket;
    std::string m_host;
    uint16_t m_port = 0;
    std::deque<std::vector<uint8_t>> m_inbox;
};

// Server-side demultiplexer. Unknown senders are queued for the matchmaking
// layer; once a peer is added, its datagrams go straight to that transport.
class UdpPeerHub {
public:
    bool open(uint16_t port, std::string& error) { return m_socket.open(port, error); }
    uint16_t local_port() const { return m_socket.local_port(); }

    void poll();
    bool next_unknown(std::string& host, uint16_t& port, std::vector<uint8_t>& data);
    PeerTransport* add_peer(const std::string& host, uint16_t port);
    void remove_peer(const std::string& host, uint16_t port);

private:
    struct Unknown {
        std::string host;
        uint16_t port = 0;
        std::vector<uint8_t> data;
    };

    static std::string key(const std::string& host, uint16_t port);

    UdpSocket m_socket;
    std::map<std::string, std::unique_ptr<PeerTransport>> m_peers;
    std::deque<Unknown> m_unknown;
};

}  // namespace sim
