// net_loop - two-process LAN check for the session layer.
//   net_loop --serve --players 2 --seconds 4
//   net_loop --join 127.0.0.1 --slot 0 --seconds 4
// --port defaults to the gate port in sim/net/ports.h (AUD-14).
// The server binds a real UDP socket; clients send HELLO, receive FULL_STATE and
// then predict at 64 Hz. Not a gate (it depends on wall-clock scheduling), but
// the two-process equivalent of the in-process UDP session test.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "sim/net/admission.h"
#include "sim/net/handshake.h"
#include "sim/net/matchmaking.h"
#include "sim/net/ports.h"
#include "sim/net/session.h"
#include "sim/net/udp_hub.h"
#include "sim/net/udp_transport.h"

namespace {

constexpr double kDt = 1.0 / 64.0;

struct Options {
    bool serve = false;
    bool join = false;
    std::string host = "127.0.0.1";
    std::string map = "sim/data/maps/metrics_flat.map";
    std::string data = "sim/data/movement.toml";
    uint16_t port = sim::kGateSignalPort;
    int players = 2;
    int slot = 0;
    double seconds = 4.0;
};

sim::MoveInput script_input(int slot, int tick) {
    sim::MoveInput in;
    in.forward = 1.0f;
    in.yaw = 0.5f + 1.0f * static_cast<float>(slot) +
             0.3f * std::sin(static_cast<float>(tick) * 0.01f);
    return in;
}

int run_server(const Options& opt) {
    sim::NetServer server;
    std::string error;
    if (!server.init(opt.map, opt.data, opt.players, error)) {
        std::fprintf(stderr, "server init: %s\n", error.c_str());
        return 1;
    }
    // A1: the TCP control plane owns admission; UDP only carries the session.
    sim::MatchHost matcher;
    if (!matcher.start(opt.port, static_cast<uint8_t>(opt.players), opt.map, 7u, 0, error)) {
        std::fprintf(stderr, "signal listen: %s\n", error.c_str());
        return 1;
    }
    sim::UdpPeerHub hub;
    const uint16_t game_port = sim::game_port_of(matcher.port());
    if (!hub.open(game_port, error) || hub.local_port() != game_port) {
        std::fprintf(stderr, "hub open: %s\n", error.c_str());
        return 1;
    }
    std::printf("server signal=%u game=%u players=%d\n", matcher.port(), hub.local_port(),
                opt.players);
    sim::Admission admission;

    int tick = 0;
    const auto start = std::chrono::steady_clock::now();
    auto next = start;
    auto last_report = start;

    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() <
           opt.seconds) {
        next += std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(kDt));
        hub.poll();
        std::string host;
        uint16_t port = 0;
        std::vector<uint8_t> packet;
        for (const sim::MatchEvent& ev : matcher.drain()) {
            if (ev.status != sim::MatchStatus::Accepted) {
                std::printf("signal rejected reason=%u\n", static_cast<unsigned>(ev.status));
                continue;
            }
            std::printf("signal ok slot=%u resumed=%d\n", ev.slot, ev.resumed ? 1 : 0);
            admission.approve(ev.client_id, ev.slot);
        }
        while (hub.next_unknown(host, port, packet)) {
            sim::ClientId who{};
            if (!sim::hello_id(packet, who)) continue;
            const int slot = admission.take(who);
            if (slot < 0) continue;  // never admitted over TCP
            sim::PeerTransport* peer = hub.add_peer(host, port);
            if (server.add_client(peer, peer, slot) < 0) continue;
            std::printf("join slot=%d addr=%s:%u server_tick=%u\n", slot, host.c_str(), port,
                        server.tick());
            std::fflush(stdout);
        }
        server.receive();
        server.step();
        server.send();
        ++tick;

        const auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration<double>(now - last_report).count() >= 1.0) {
            last_report = now;
            const auto& p0 = server.world().players[0];
            const double elapsed = std::chrono::duration<double>(now - start).count();
            std::printf("t=%.1f tick=%d srv=%u exec0=%u cmd0=%u seen0=%u acc=%d late=%d far=%d dup=%d depth=%d slot0=%.3f,%.3f,%.3f\n",
                        elapsed, tick, server.tick(), server.last_executed_tick(0),
                        server.last_cmd_tick(0), server.last_seen_tick(0),
                        server.accepted_commands(),
                        server.late_commands(), server.far_commands(),
                        server.duplicate_commands(), server.max_input_depth(),
                        static_cast<double>(p0.pos.x), static_cast<double>(p0.pos.y),
                        static_cast<double>(p0.pos.z));
            std::fflush(stdout);
        }
        std::this_thread::sleep_until(next);
    }

    for (int i = 0; i < opt.players; ++i) {
        const auto& p = server.world().players[static_cast<size_t>(i)];
        std::printf("final slot=%d pos=%.4f,%.4f,%.4f\n", i,
                    static_cast<double>(p.pos.x), static_cast<double>(p.pos.y),
                    static_cast<double>(p.pos.z));
    }
    return 0;
}

int run_client(const Options& opt) {
    sim::UdpTransport transport;
    sim::NetClient client;
    std::string error;
    bool inited = false;
    int slot = opt.slot;
    uint16_t game_port = 0;
    // The id is what the host vetted over TCP; the UDP hello repeats it.
    sim::ClientId id{};
    id[0] = 0x5A;
    id[15] = static_cast<uint8_t>(opt.slot + 1);

    const auto start = std::chrono::steady_clock::now();
    auto next = start;
    auto last_hello = start - std::chrono::seconds(1);
    auto last_try = start - std::chrono::seconds(1);
    auto last_report = start;
    int tick = 0;

    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() <
           opt.seconds) {
        next += std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(kDt));
        const auto now = std::chrono::steady_clock::now();
        if (!inited && std::chrono::duration<double>(now - last_try).count() >= 0.25) {
            last_try = now;
            sim::MatchReply reply;
            if (sim::match_request(opt.host, opt.port, id, nullptr, reply) && reply.ok &&
                reply.status == sim::MatchStatus::Accepted) {
                slot = reply.slot;
                game_port = reply.game_port;
                if (transport.open(0, error) &&
                    transport.connect_peer(opt.host, game_port, error) &&
                    client.init(&transport, &transport, slot, opt.map, opt.data, error)) {
                    inited = true;
                    std::printf("signal slot=%d game_port=%u\n", slot, game_port);
                    std::fflush(stdout);
                    transport.send(sim::make_hello(id));
                    last_hello = now;
                } else {
                    std::fprintf(stderr, "client socket: %s\n", error.c_str());
                }
            }
        }
        if (!inited) {
            ++tick;
            std::this_thread::sleep_until(next);
            continue;
        }
        if (!client.joined() &&
            std::chrono::duration<double>(now - last_hello).count() >= 0.25) {
            transport.send(sim::make_hello(id));
            last_hello = now;
        }
        client.send_input(script_input(slot, tick));
        client.step_local();
        client.receive();
        ++tick;

        if (std::chrono::duration<double>(now - last_report).count() >= 1.0) {
            last_report = now;
            const auto& p = client.predicted();
            const double elapsed = std::chrono::duration<double>(now - start).count();
            std::printf("t=%.1f tick=%d local=%u clock=%u sent=%u skips=%d join=%u full=%d joined=%d snapshots=%d bad=%d slot=%d pos=%.3f,%.3f,%.3f\n",
                        elapsed, tick, client.tick(), client.clock().command_tick(),
                        client.last_sent_tick(), client.clock_skips(), client.join_tick(),
                        client.full_states(), client.joined() ? 1 : 0,
                        client.snapshots_received(), client.bad_snapshots(), client.player_index(),
                        static_cast<double>(p.pos.x), static_cast<double>(p.pos.y),
                        static_cast<double>(p.pos.z));
            std::fflush(stdout);
        }
        std::this_thread::sleep_until(next);
    }

    const auto& p = client.predicted();
    std::printf("final joined=%d snapshots=%d slot=%d pos=%.4f,%.4f,%.4f\n",
                client.joined() ? 1 : 0, client.snapshots_received(), client.player_index(),
                static_cast<double>(p.pos.x), static_cast<double>(p.pos.y),
                static_cast<double>(p.pos.z));
    return client.joined() ? 0 : 1;
}

void usage() {
    std::printf(
        "net_loop --serve [--port %u] [--players 2] [--seconds 4] [--map f] [--data f]\n"
        "net_loop --join <ip> [--port %u] [--slot 0] [--seconds 4] [--map f] [--data f]\n",
        static_cast<unsigned>(sim::kGateSignalPort), static_cast<unsigned>(sim::kGateSignalPort));
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "error: %s needs a value\n", name);
                return nullptr;
            }
            return argv[++i];
        };
        if (arg == "--serve") {
            opt.serve = true;
        } else if (arg == "--join") {
            const char* v = value("--join");
            if (v == nullptr) return 2;
            opt.join = true;
            opt.host = v;
        } else if (arg == "--port") {
            const char* v = value("--port");
            if (v == nullptr) return 2;
            opt.port = static_cast<uint16_t>(std::atoi(v));
        } else if (arg == "--players") {
            const char* v = value("--players");
            if (v == nullptr) return 2;
            opt.players = std::atoi(v);
        } else if (arg == "--slot") {
            const char* v = value("--slot");
            if (v == nullptr) return 2;
            opt.slot = std::atoi(v);
        } else if (arg == "--seconds") {
            const char* v = value("--seconds");
            if (v == nullptr) return 2;
            opt.seconds = std::atof(v);
        } else if (arg == "--map") {
            const char* v = value("--map");
            if (v == nullptr) return 2;
            opt.map = v;
        } else if (arg == "--data") {
            const char* v = value("--data");
            if (v == nullptr) return 2;
            opt.data = v;
        } else if (arg == "--help" || arg == "-h") {
            usage();
            return 0;
        } else {
            std::fprintf(stderr, "error: unknown argument %s\n", arg.c_str());
            usage();
            return 2;
        }
    }
    if (opt.serve == opt.join) {
        usage();
        return 2;
    }
    return opt.serve ? run_server(opt) : run_client(opt);
}
