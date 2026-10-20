#include "net_gateway.h"

#include <cstdio>
#include <string>
#include <vector>

#include <godot_cpp/core/error_macros.hpp>

#include "sim/net/handshake.h"
#include "sim/net/ports.h"

namespace bridge {

namespace {

// The old in-place ERR_PRINT carried a "breakpoint host/join: " prefix; keep the
// same text while the detail comes from the sim layer.
bool fail(const char* prefix, std::string& error) {
    error = std::string(prefix) + error;
    return false;
}

}  // namespace

bool NetGateway::start_host(const std::string& map, const std::string& data, int port, int players,
                            std::string& error) {
    if (m_net_client) return false;
    m_server = std::make_unique<sim::NetServer>();
    if (!m_server->init(map, data, players, error)) {
        return fail("breakpoint host: ", error);
    }
    // A1: the TCP control plane owns admission. The game port is derived, never
    // passed in, so it cannot drift from the signalling port (sim/net/ports.h).
    m_match = std::make_unique<sim::MatchHost>();
    // The listen server owns slot 0 itself, so clients start at slot 1.
    if (!m_match->start(static_cast<uint16_t>(port), static_cast<uint8_t>(players), map, 7u, 1,
                        error)) {
        return fail("breakpoint host: ", error);
    }
    {
        // The join configstring now carries real identities: a client on a
        // different movement set or map is refused instead of drifting away.
        sim::ConfigBlob cfg;
        cfg.params_hash = m_server->world().params_hash;
        cfg.geometry_hash = m_server->world().collision.geometry_hash();
        m_server->set_configstring(sim::encode_config(cfg));
    }
    m_hub = std::make_unique<sim::UdpPeerHub>();
    const uint16_t game_port = sim::game_port_of(m_match->port());
    if (!m_hub->open(game_port, error) || m_hub->local_port() != game_port) {
        return fail("breakpoint host: ", error);
    }
    m_local_to_server = std::make_unique<sim::LoopbackTransport>(sim::NetConditions{}, 1);
    m_local_to_client = std::make_unique<sim::LoopbackTransport>(sim::NetConditions{}, 2);
    m_server->add_client(m_local_to_server.get(), m_local_to_client.get(), 0);
    m_net_client = std::make_unique<sim::NetClient>();
    if (!m_net_client->init(m_local_to_server.get(), m_local_to_client.get(), 0, map, data,
                            error)) {
        m_net_client.reset();
        return fail("breakpoint host: ", error);
    }
    m_hosting = true;
    m_disconnected = false;
    return true;
}

// The id the host vets over TCP (A5); the UDP hello repeats it. Until the
// bridge runs the control plane, this stands in for it.
sim::ClientId NetGateway::make_client_id() {
    static uint64_t counter = 0;
    sim::ClientId id{};
    const uint64_t seed = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    for (std::size_t i = 0; i < id.size(); ++i) {
        id[i] = static_cast<uint8_t>((seed >> ((i % 8) * 8)) ^ (counter * 31u + i * 7u));
    }
    ++counter;
    return id;
}

bool NetGateway::join(const std::string& host, int port, const std::string& map,
                      const std::string& data, std::string& error) {
    if (m_net_client) return false;
    // Stay in the network state from here on: even if admission or the socket
    // fails, the bridge must not silently resume stepping the solo map world
    // (camera snap + stale remote capsules).
    m_disconnected = true;
    m_client_id = make_client_id();
    m_join_host = host;
    m_join_port = port;
    m_has_ticket = false;
    m_map_path = map;
    m_data_path = data;
    return join_impl(host, port, nullptr, error);
}

// A5: a resume keeps the same client id and presents the ticket the host
// issued, so it comes back to its old slot instead of queueing for a new one.
bool NetGateway::reconnect() {
    if (m_hosting || !m_has_ticket) return false;
    std::string error;
    if (join_impl(m_join_host, m_join_port, &m_ticket, error)) return true;
    // reconnect() has no error out-parameter, so it reports in place like the
    // pre-extraction join path did.
    ERR_PRINT(error.c_str());
    return false;
}

bool NetGateway::join_impl(const std::string& host, int port, const sim::Ticket* resume,
                           std::string& error) {
    // A1/A5: ask the host for a slot and a ticket before touching UDP.
    sim::MatchReply reply;
    if (!sim::match_request(host.c_str(), static_cast<uint16_t>(port), m_client_id, resume,
                            reply) ||
        !reply.ok || reply.status != sim::MatchStatus::Accepted) {
        std::printf("breakpoint: join signal failed error='%s' status=%d\n",
                    reply.error.c_str(), static_cast<int>(reply.status));
        std::fflush(stdout);
        error = "breakpoint join: signalling failed";
        return false;
    }
    if (reply.has_ticket) {
        m_ticket = reply.ticket;
        m_has_ticket = true;
    }
    // Admitted: the old link (if any) can go before the new one is built.
    m_net_client.reset();
    m_udp.reset();
    m_udp = std::make_unique<sim::UdpTransport>();
    if (!m_udp->open(0, error) ||
        !m_udp->connect_peer(host.c_str(), reply.game_port, error)) {
        return fail("breakpoint join: ", error);
    }
    m_net_client = std::make_unique<sim::NetClient>();
    if (!m_net_client->init(m_udp.get(), m_udp.get(), reply.slot, m_map_path, m_data_path,
                            error)) {
        m_net_client.reset();
        return fail("breakpoint join: ", error);
    }
    std::printf("breakpoint: signal slot %d game port %u\n", static_cast<int>(reply.slot),
                static_cast<unsigned>(reply.game_port));
    m_udp->send(sim::make_hello(m_client_id));
    m_last_hello = std::chrono::steady_clock::now();
    m_disconnected = false;
    return true;
}

void NetGateway::pump(const sim::MoveInput& input, sim::EventQueue* events) {
    if (!m_net_client) return;
    const auto now = std::chrono::steady_clock::now();
    if (m_hosting) {
        m_match->set_server_tick(m_server->tick());
        for (const sim::MatchEvent& ev : m_match->drain()) {
            if (ev.status != sim::MatchStatus::Accepted) continue;
            std::printf("breakpoint: signal slot %u resumed %d\n", ev.slot, ev.resumed ? 1 : 0);
            std::fflush(stdout);
            m_admission.approve(ev.client_id, ev.slot);
        }
        m_hub->poll();
        std::string peer_host;
        uint16_t peer_port = 0;
        std::vector<uint8_t> packet;
        while (m_hub->next_unknown(peer_host, peer_port, packet)) {
            sim::ClientId who{};
            if (!sim::hello_id(packet, who)) continue;
            const int slot = m_admission.take(who);
            if (slot < 0) continue;  // never vetted over TCP
            sim::PeerTransport* peer = m_hub->add_peer(peer_host, peer_port);
            if (m_server->add_client(peer, peer, slot) < 0) continue;  // all slots taken
            std::printf("breakpoint: peer joined slot %d\n", slot);
            std::fflush(stdout);
        }
        m_net_client->send_input(input);
        m_net_client->step_local(events);
        m_server->receive();
        m_server->step();
        m_server->send();
        m_local_to_server->advance(1);
        m_local_to_client->advance(1);
        m_net_client->receive();
    } else {
        if (!m_net_client->joined() &&
            std::chrono::duration<double>(now - m_last_hello).count() >= 0.25) {
            m_udp->send(sim::make_hello(m_client_id));
            m_last_hello = now;
        }
        m_net_client->send_input(input);
        m_net_client->step_local(events);
        m_net_client->receive();
    }
}

int NetGateway::peers() const {
    if (m_hosting && m_server) return m_server->client_count();
    return (m_net_client && m_net_client->joined()) ? 1 : 0;
}

}  // namespace bridge