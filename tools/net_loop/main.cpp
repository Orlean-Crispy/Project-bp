// net_loop - two-process LAN check for the session layer.
//   net_loop --serve --port 27016 --players 2 --seconds 4
//   net_loop --join 127.0.0.1 --port 27016 --slot 0 --seconds 4
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

#include "sim/net/handshake.h"
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
    uint16_t port = 27016;
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
    sim::UdpPeerHub hub;
    if (!hub.open(opt.port, error)) {
        std::fprintf(stderr, "hub open: %s\n", error.c_str());
        return 1;
    }
    std::printf("server port=%u players=%d\n", hub.local_port(), opt.players);

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
        while (hub.next_unknown(host, port, packet)) {
            if (!sim::is_hello(packet)) continue;
            sim::PeerTransport* peer = hub.add_peer(host, port);
            const int slot = server.add_client(peer, peer, -1);
            if (slot < 0) continue;
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
    std::string error;
    if (!transport.open(0, error) ||
        !transport.connect_peer(opt.host, opt.port, error)) {
        std::fprintf(stderr, "client socket: %s\n", error.c_str());
        return 1;
    }
    sim::NetClient client;
    if (!client.init(&transport, &transport, opt.slot, opt.map, opt.data, error)) {
        std::fprintf(stderr, "client init: %s\n", error.c_str());
        return 1;
    }

    const auto start = std::chrono::steady_clock::now();
    auto next = start;
    auto last_hello = start - std::chrono::seconds(1);
    auto last_report = start;
    int tick = 0;

    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() <
           opt.seconds) {
        next += std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(kDt));
        const auto now = std::chrono::steady_clock::now();
        if (!client.joined() &&
            std::chrono::duration<double>(now - last_hello).count() >= 0.25) {
            transport.send(sim::make_hello());
            last_hello = now;
        }
        client.send_input(script_input(opt.slot, tick));
        client.step_local();
        client.receive();
        ++tick;

        if (std::chrono::duration<double>(now - last_report).count() >= 1.0) {
            last_report = now;
            const auto& p = client.predicted();
            const double elapsed = std::chrono::duration<double>(now - start).count();
            std::printf("t=%.1f tick=%d local=%u clock=%u sent=%u skips=%d join=%u full=%d joined=%d snapshots=%d slot=%d pos=%.3f,%.3f,%.3f\n",
                        elapsed, tick, client.tick(), client.clock().command_tick(),
                        client.last_sent_tick(), client.clock_skips(), client.join_tick(),
                        client.full_states(), client.joined() ? 1 : 0,
                        client.snapshots_received(), client.player_index(),
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
        "net_loop --serve [--port 27016] [--players 2] [--seconds 4] [--map f] [--data f]\n"
        "net_loop --join <ip> [--port 27016] [--slot 0] [--seconds 4] [--map f] [--data f]\n");
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
