#pragma once

// The networking half of the Godot bridge: hosting, joining, reconnecting and
// the per-tick pump. It owns every socket and session object, so SimWorld only
// forwards calls and reads the results.

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "sim/net/admission.h"
#include "sim/net/matchmaking.h"
#include "sim/net/session.h"
#include "sim/net/ticket.h"
#include "sim/net/fragment.h"
#include "sim/net/transport.h"
#include "sim/net/udp_hub.h"
#include "sim/net/udp_transport.h"

namespace bridge {

class NetGateway {
public:
    bool start_host(const std::string& map, const std::string& data, int port, int players,
                    std::string& error);
    bool join(const std::string& host, int port, const std::string& map, const std::string& data,
              std::string& error);
    bool reconnect();
    // AUD-01: the client's predicted world emits jump/land events; pump forwards
    // the bridge's queue so LAN play fills it exactly like solo play does.
    void pump(const sim::MoveInput& input, sim::EventQueue* events);

    // AUD-06: a join that got past signalling but could not open UDP is still a
    // network session ("disconnected"), not a reason to drop back to solo play.
    bool active() const { return m_net_client != nullptr || m_disconnected; }
    bool hosting() const { return m_hosting; }
    bool joined() const { return m_net_client != nullptr && m_net_client->joined(); }
    int slot() const { return m_net_client ? m_net_client->player_index() : 0; }
    int peers() const;
    int snapshot_age() const { return m_net_client ? m_net_client->snapshot_age() : -1; }
    const sim::NetClient* client() const { return m_net_client.get(); }
    sim::NetClient* client() { return m_net_client.get(); }
    // The host's authoritative world, for the gate-only damage seam in SimWorld.
    sim::NetServer* server() { return m_server.get(); }
    const sim::NetServer* server() const { return m_server.get(); }

private:
    bool join_impl(const std::string& host, int port, const sim::Ticket* resume,
                   std::string& error);
    // Stops the listener/thread/hub after a partial host start.
    void abort_host();
    static sim::ClientId make_client_id();

    std::unique_ptr<sim::NetServer> m_server;
    std::unique_ptr<sim::MatchHost> m_match;
    sim::Admission m_admission;
    std::unique_ptr<sim::NetClient> m_net_client;
    std::unique_ptr<sim::UdpPeerHub> m_hub;
    std::unique_ptr<sim::UdpTransport> m_udp;
    // AUD-23: every real-link datagram goes through the fragmenter, so a large
    // snapshot is split here instead of at the IP layer. Declared after m_udp
    // because it references it.
    std::unique_ptr<sim::FragmentedTransport> m_frag;
    std::unique_ptr<sim::LoopbackTransport> m_local_to_server;
    std::unique_ptr<sim::LoopbackTransport> m_local_to_client;
    bool m_hosting = false;
    bool m_disconnected = false;
    sim::ClientId m_client_id{};
    sim::Ticket m_ticket{};
    bool m_has_ticket = false;
    std::string m_join_host;
    int m_join_port = 0;
    std::chrono::steady_clock::time_point m_last_hello{};
    // reconnect() has no map/data parameters; it reuses the ones join() got.
    std::string m_map_path;
    std::string m_data_path;
};

}  // namespace bridge
