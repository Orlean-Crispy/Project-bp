#include <cstdio>
#include <string>
#include <vector>

#include "options.h"
#include "tests_net.h"

#include "sim/net/admission.h"
#include "sim/net/configstring.h"
#include "sim/net/fragment.h"
#include "sim/net/handshake.h"
#include "sim/net/matchmaking.h"
#include "sim/net/netchan.h"
#include "sim/net/reliable.h"
#include "sim/net/reliable_transport.h"
#include "sim/net/session.h"
#include "sim/net/signaling.h"
#include "sim/net/ticket.h"
#include "sim/net/transport.h"
#include "sim/net/udp_hub.h"
#include "sim/net/udp_transport.h"


#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <thread>
#include <vector>

#include "sim/core/tick.h"
#include "sim/core/version.h"
#include "sim/core/world.h"
#include "sim/demo/demo.h"
#include "sim/input/bindings.h"
#include "sim/game/combat.h"
#include "sim/net/fragment.h"
#include "sim/net/admission.h"
#include "sim/net/handshake.h"
#include "sim/net/matchmaking.h"
#include "sim/net/netchan.h"
#include "sim/net/reliable.h"
#include "sim/net/reliable_transport.h"
#include "sim/net/session.h"
#include "sim/net/signaling.h"
#include "sim/net/ticket.h"
#include "sim/net/transport.h"

struct NetRunStats {
    bool ok = false;
    float deviation = 0.0f;
    int rollbacks = 0;
    int snapshots = 0;
    int full = 0;
    int delta = 0;
    int late = 0;
    int duplicate = 0;
    int far = 0;
    int max_depth = 0;
    int corrections = 0;
    float max_correction = 0.0f;
    int hitbox_size = 0;
    uint32_t hitbox_newest = 0;
    uint32_t server_tick = 0;
    int remote_frames = 0;
    uint32_t remote_newest = 0;
    uint32_t last_snapshot = 0;
};

sim::MoveInput net_test_input(int i) {
    sim::MoveInput in;
    in.forward = 1.0f;
    in.yaw = 0.35f * std::sin(static_cast<float>(i) * 0.01f);
    in.jump = (i % 90) == 0;
    return in;
}

NetRunStats run_net_scenario(const std::string& data_path, float loss, float jitter_ms, int ticks,
                             int settle_ticks) {
    NetRunStats stats;
    const std::string map_path = "sim/data/maps/metrics_flat.map";

    sim::NetServer server;
    std::string error;
    if (!server.init(map_path, data_path, 1, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return stats;
    }

    sim::NetConditions conditions;
    conditions.loss = loss;
    conditions.latency_ms = 30.0f;
    conditions.jitter_ms = jitter_ms;
    conditions.duplicate = 0.02f;

    sim::LoopbackTransport to_server(conditions, 1234);
    sim::LoopbackTransport to_client(conditions, 5678);
    server.add_client(&to_server, &to_client, 0);

    sim::NetClient client;
    if (!client.init(&to_server, &to_client, 0, map_path, data_path, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return stats;
    }

    auto tick_once = [&](const sim::MoveInput& in) {
        client.send_input(in);
        client.step_local();
        server.receive();
        server.step();
        server.send();
        to_server.advance(0);
        to_client.advance(0);
        client.receive();
    };

    for (int i = 0; i < ticks; ++i) tick_once(net_test_input(i));

    stats.rollbacks = client.rollbacks();
    stats.snapshots = client.snapshots_received();
    stats.corrections = client.corrections();
    stats.max_correction = client.max_correction();
    stats.full = server.full_snapshots();
    stats.delta = server.delta_snapshots();
    stats.late = server.late_commands();
    stats.duplicate = server.duplicate_commands();
    stats.far = server.far_commands();
    stats.max_depth = server.max_input_depth();
    stats.server_tick = server.tick();
    stats.hitbox_size = server.hitboxes(0).size();
    stats.hitbox_newest = server.hitboxes(0).newest_tick();
    stats.remote_frames = client.remote().size();
    stats.remote_newest = client.remote().newest_tick();
    stats.last_snapshot = client.last_snapshot_tick();

    // Let both sides come to rest so positions are comparable without latency.
    client.reset_stats();
    for (int i = 0; i < settle_ticks; ++i) tick_once(sim::MoveInput{});

    stats.deviation = sim::length(client.predicted().pos - server.world().players[0].pos);
    stats.ok = true;
    return stats;
}


int run_join_phase(const std::string& data_path) {
    const std::string map_path = "sim/data/maps/metrics_flat.map";
    int failures = 0;

    sim::NetServer server;
    std::string error;
    if (!server.init(map_path, data_path, 1, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }
    // The match is already running when the first client arrives.
    for (int i = 0; i < 200; ++i) server.step();

    sim::NetConditions conditions;
    conditions.loss = 0.05f;
    conditions.latency_ms = 30.0f;
    conditions.jitter_ms = 5.0f;
    sim::LoopbackTransport to_server(conditions, 999);
    sim::LoopbackTransport to_client(conditions, 1000);

    int link = server.add_client(&to_server, &to_client, 0);
    sim::NetClient client;
    if (!client.init(&to_server, &to_client, 0, map_path, data_path, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }

    auto tick_once = [&](const sim::MoveInput& in) {
        client.send_input(in);
        client.step_local();
        server.receive();
        server.step();
        server.send();
        to_server.advance(0);
        to_client.advance(0);
        client.receive();
    };

    for (int i = 0; i < 300; ++i) tick_once(net_test_input(i));
    const uint32_t first_join = client.join_tick();
    const int first_full_states = client.full_states();
    for (int i = 0; i < 100; ++i) tick_once(sim::MoveInput{});
    const float first_deviation =
        sim::length(client.predicted().pos - server.world().players[0].pos);

    // Pull the link for 100 ticks, then re-admit the same player. The server
    // keeps simulating; the client's state goes stale until FULL_STATE returns.
    server.remove_client(link);
    for (int i = 0; i < 100; ++i) server.step();
    link = server.add_client(&to_server, &to_client, 0);

    for (int i = 0; i < 300; ++i) tick_once(net_test_input(i + 300));
    for (int i = 0; i < 100; ++i) tick_once(sim::MoveInput{});
    const float second_deviation =
        sim::length(client.predicted().pos - server.world().players[0].pos);

    std::printf("  [join/reconnect] first join tick %u, full states %d -> %d, "
                "reconnect tick %u, dev %.5f / %.5f m\n",
                first_join, first_full_states, client.full_states(), client.join_tick(),
                static_cast<double>(first_deviation), static_cast<double>(second_deviation));

    if (!client.joined() || client.join_failed()) {
        std::printf("FAIL: client did not join or rejected the full state\n");
        ++failures;
    }
    if (first_join < 200) {
        std::printf("FAIL: first join happened before the match had started\n");
        ++failures;
    }
    if (client.full_states() < 2) {
        std::printf("FAIL: reconnect did not deliver a second full state\n");
        ++failures;
    }
    if (client.join_tick() <= first_join) {
        std::printf("FAIL: reconnect full state did not advance the server tick\n");
        ++failures;
    }
    if (first_deviation > 0.01f || second_deviation > 0.01f) {
        std::printf("FAIL: join/reconnect did not converge to within 1 cm\n");
        ++failures;
    }

    // A client whose params_hash differs must refuse the full state instead of
    // simulating a different world (plan 7.7; v0.4 A1 config hash).
    FILE* bad = std::fopen("build/join_bad.toml", "wb");
    if (bad != nullptr) {
        std::fprintf(bad, "[base]\naccel = 12.0\n");
        std::fclose(bad);

        sim::NetServer bad_server;
        if (!bad_server.init(map_path, data_path, 1, error)) return failures + 1;
        sim::LoopbackTransport bad_to_server(conditions, 42);
        sim::LoopbackTransport bad_to_client(conditions, 43);
        bad_server.add_client(&bad_to_server, &bad_to_client, 0);

        sim::NetClient bad_client;
        if (!bad_client.init(&bad_to_server, &bad_to_client, 0, map_path,
                             "build/join_bad.toml", error)) {
            return failures + 1;
        }
        for (int i = 0; i < 20; ++i) {
            bad_client.send_input(sim::MoveInput{});
            bad_client.step_local();
            bad_server.receive();
            bad_server.step();
            bad_server.send();
            bad_to_server.advance(0);
            bad_to_client.advance(0);
            bad_client.receive();
        }
        const bool rejected = bad_client.join_failed() && !bad_client.joined();
        std::printf("  [join] mismatched params hash rejected  %s\n", rejected ? "ok" : "FAIL");
        if (!rejected) ++failures;
    }
    return failures;
}

int run_udp_check() {
    int failures = 0;
    std::string error;
    sim::UdpTransport server;
    sim::UdpTransport client;
    if (!server.open(0, error) || !client.open(0, error)) {
        std::printf("FAIL: udp open: %s\n", error.c_str());
        return 1;
    }
    if (!client.connect_peer("127.0.0.1", server.local_port(), error)) {
        std::printf("FAIL: udp connect: %s\n", error.c_str());
        return 1;
    }

    const std::vector<uint8_t> hello = {0x42, 0x50, 0x01, 0x02};
    std::vector<uint8_t> got;
    std::string from_host;
    uint16_t from_port = 0;
    std::vector<uint8_t> pong;

    auto poll = [](auto&& fn) {
        for (int i = 0; i < 200; ++i) {
            if (fn()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    };

    client.send(hello);
    const bool hello_ok = poll([&] { return server.recv_from(got, from_host, from_port); }) &&
                          got == hello;
    const bool adopted = hello_ok && server.adopt_peer(from_host, from_port, error);

    bool pong_ok = false;
    bool large_ok = false;
    bool empty_ok = false;
    if (adopted) {
        pong = {0x50, 0x4f, 0x4e, 0x47};
        server.send(pong);
        pong_ok = poll([&] { return client.recv(got); }) && got == pong;

        std::vector<uint8_t> large(1200);
        for (size_t i = 0; i < large.size(); ++i) {
            large[i] = static_cast<uint8_t>((i * 7 + 3) & 0xFF);
        }
        client.send(large);
        large_ok = poll([&] { return server.recv(got); }) && got == large;
        empty_ok = !client.recv(got);
    }

    std::printf("  [udp] hello %d adopt %d pong %d 1200B %d empty-poll %d\n", hello_ok,
                adopted, pong_ok, large_ok, empty_ok);
    if (!hello_ok || !adopted || !pong_ok || !large_ok || !empty_ok) {
        std::printf("FAIL: UDP transport round-trip failed\n");
        ++failures;
    }
    return failures;
}

int run_fragment_check() {
    int failures = 0;
    sim::NetConditions clean;
    sim::LoopbackTransport link(clean, 11);
    sim::FragmentedTransport fa(link);
    sim::FragmentedTransport fb(link);

    auto pump = [&] {
        for (int k = 0; k < 4; ++k) link.advance(0);
    };

    std::vector<uint8_t> big(3000);
    for (size_t i = 0; i < big.size(); ++i) {
        big[i] = static_cast<uint8_t>((i * 13 + 7) & 0xFF);
    }
    std::vector<uint8_t> got;
    fa.send(big);
    pump();
    const bool big_ok = fb.recv(got) && got == big;

    const std::vector<uint8_t> small = {1, 2, 3, 4};
    fa.send(small);
    pump();
    const bool small_ok = fb.recv(got) && got == small;

    // Incomplete fragment sets must vanish, never arrive truncated or mixed.
    sim::NetConditions lossy;
    lossy.loss = 0.30f;
    sim::LoopbackTransport lossy_link(lossy, 21);
    sim::FragmentedTransport lfa(lossy_link);
    sim::FragmentedTransport lfb(lossy_link);
    int delivered = 0;
    int corrupted = 0;
    for (int i = 0; i < 20; ++i) {
        std::vector<uint8_t> payload(2500, static_cast<uint8_t>(i + 1));
        lfa.send(payload);
        for (int k = 0; k < 4; ++k) lossy_link.advance(0);
        while (lfb.recv(got)) {
            if (got.size() != payload.size() || got != payload) {
                ++corrupted;
            } else {
                ++delivered;
            }
        }
    }

    std::printf("  [fragment] 3000B %d small %d lossy delivered %d corrupted %d\n", big_ok,
                small_ok, delivered, corrupted);
    if (!big_ok || !small_ok || corrupted != 0) {
        std::printf("FAIL: fragment reassembly is wrong\n");
        ++failures;
    }
    return failures;
}

int run_udp_session_check(const std::string& data_path) {
    const std::string map_path = "sim/data/maps/metrics_flat.map";
    std::string error;

    sim::UdpPeerHub hub;
    if (!hub.open(0, error)) {
        std::printf("FAIL: hub open: %s\n", error.c_str());
        return 1;
    }
    sim::NetServer server;
    if (!server.init(map_path, data_path, 2, error)) {
        std::printf("FAIL: server init: %s\n", error.c_str());
        return 1;
    }
    sim::UdpTransport transport;
    if (!transport.open(0, error) ||
        !transport.connect_peer("127.0.0.1", hub.local_port(), error)) {
        std::printf("FAIL: client socket: %s\n", error.c_str());
        return 1;
    }
    sim::NetClient client;
    if (!client.init(&transport, &transport, 0, map_path, data_path, error)) {
        std::printf("FAIL: client init: %s\n", error.c_str());
        return 1;
    }

    sim::UdpTransport transport2;
    if (!transport2.open(0, error) ||
        !transport2.connect_peer("127.0.0.1", hub.local_port(), error)) {
        std::printf("FAIL: second client socket: %s\n", error.c_str());
        return 1;
    }
    sim::NetClient client2;
    if (!client2.init(&transport2, &transport2, 1, map_path, data_path, error)) {
        std::printf("FAIL: second client init: %s\n", error.c_str());
        return 1;
    }

    // Each client announces the id the control plane vetted; the server maps it
    // to a slot instead of guessing from arrival order.
    sim::ClientId id0{};
    id0[0] = 0x11;
    sim::ClientId id1{};
    id1[0] = 0x22;
    transport.send(sim::make_hello(id0));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    transport2.send(sim::make_hello(id1));
    int ticks = 0;
    double accumulator = 0.0;
    auto last = std::chrono::steady_clock::now();
    const auto started = last;

    auto pump = [&](const sim::MoveInput& input) {
        hub.poll();
        std::string host;
        uint16_t port = 0;
        std::vector<uint8_t> packet;
        while (hub.next_unknown(host, port, packet)) {
            sim::ClientId who{};
            if (!sim::hello_id(packet, who)) continue;
            int slot = -1;
            if (who == id0) slot = 0;
            if (who == id1) slot = 1;
            if (slot < 0) continue;  // unknown id: never admitted
            sim::PeerTransport* peer = hub.add_peer(host, port);
            server.add_client(peer, peer, slot);
        }
        client.send_input(input);
        client.step_local();
        client2.send_input(input);
        client2.step_local();
        server.receive();
        server.step();
        server.send();
        client.receive();
        client2.receive();
        ++ticks;
    };

    while (ticks < 120) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const auto now = std::chrono::steady_clock::now();
        accumulator += std::chrono::duration<double>(now - last).count();
        last = now;
        while (accumulator >= 1.0 / 64.0) {
            sim::MoveInput input;
            input.forward = 1.0f;
            input.yaw = 0.4f * std::sin(static_cast<float>(ticks) * 0.01f);
            pump(input);
            accumulator -= 1.0 / 64.0;
        }
        if (std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() > 5.0) {
            break;
        }
    }
    for (int i = 0; i < 30; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        pump(sim::MoveInput{});
    }

    const float deviation =
        sim::length(client.predicted().pos - server.world().players[0].pos);
    const float deviation2 =
        sim::length(client2.predicted().pos - server.world().players[1].pos);
    std::printf("  [udp session] joined %d/%d snapshots %d/%d dev %.5f / %.5f m\n",
                client.joined() ? 1 : 0, client2.joined() ? 1 : 0,
                client.snapshots_received(), client2.snapshots_received(),
                static_cast<double>(deviation), static_cast<double>(deviation2));

    int failures = 0;
    if (!client.joined() || !client2.joined() || client.join_failed() ||
        client2.join_failed()) {
        std::printf("FAIL: a client did not join over UDP\n");
        ++failures;
    }
    if (client.snapshots_received() < 20 || client2.snapshots_received() < 20) {
        std::printf("FAIL: too few snapshots over UDP\n");
        ++failures;
    }
    if (deviation > 0.01f || deviation2 > 0.01f) {
        std::printf("FAIL: UDP session diverged\n");
        ++failures;
    }
    return failures;
}

int run_timeout_test() {
    const std::string map_path = "sim/data/maps/metrics_flat.map";
    std::string error;
    sim::NetServer server;
    if (!server.init(map_path, "sim/data/movement.toml", 1, error)) return 1;
    sim::LoopbackTransport to_server(sim::NetConditions{}, 41);
    sim::LoopbackTransport to_client(sim::NetConditions{}, 42);
    server.add_client(&to_server, &to_client, 0);
    sim::NetClient client;
    if (!client.init(&to_server, &to_client, 0, map_path, "sim/data/movement.toml", error)) return 1;

    sim::MoveInput in;
    in.forward = 1.0f;
    for (int i = 0; i < 120; ++i) {
        client.send_input(in);
        client.step_local();
        server.receive();
        server.step();
        server.send();
        to_server.advance(0);
        to_client.advance(0);
        client.receive();
    }
    const bool joined = server.client_count() == 1;
    for (int i = 0; i < 400; ++i) {
        server.receive();
        server.step();
        server.send();
        to_client.advance(0);
    }
    const bool dropped = server.client_count() == 0 && server.timed_out_clients() == 1 &&
                         !server.world().players[0].active;
    sim::LoopbackTransport reconnect_to_server(sim::NetConditions{}, 43);
    sim::LoopbackTransport reconnect_to_client(sim::NetConditions{}, 44);
    const int slot = server.add_client(&reconnect_to_server, &reconnect_to_client, -1);
    const bool reused = slot == 0 && server.world().players[0].active &&
                        server.client_count() == 1;
    std::printf("  [timeout] joined %d dropped %d reuse %d\n", joined ? 1 : 0, dropped ? 1 : 0,
                reused ? 1 : 0);
    if (!joined || !dropped || !reused) {
        std::printf("FAIL: silent client drop or slot reuse failed\n");
        return 1;
    }
    return 0;
}

int run_ticket_test() {
    std::array<uint8_t, 32> key{};
    for (size_t i = 0; i < key.size(); ++i) key[i] = static_cast<uint8_t>(i * 3 + 1);
    sim::TicketIssuer issuer(key);

    sim::ClientId id{};
    for (size_t i = 0; i < id.size(); ++i) id[i] = static_cast<uint8_t>(0x10 + i);
    const sim::Ticket ticket = issuer.issue(id, 1000, 120);

    const bool valid = issuer.verify(ticket, 1119);
    const bool expired = !issuer.verify(ticket, 1120);
    sim::Ticket tampered = ticket;
    tampered.client_id[0] ^= 0x01;
    const bool tamper = !issuer.verify(tampered, 1000);
    std::array<uint8_t, 32> other_key{};
    other_key[0] = 0x7F;
    sim::TicketIssuer other(other_key);
    const bool wrong_key = !other.verify(ticket, 1000);

    std::printf("  [ticket] valid %d expired %d tamper %d wrongkey %d\n", valid ? 1 : 0,
                expired ? 1 : 0, tamper ? 1 : 0, wrong_key ? 1 : 0);
    if (!valid || !expired || !tamper || !wrong_key) {
        std::printf("FAIL: A5 ticket test failed\n");
        return 1;
    }
    return 0;
}

int run_reliable_test() {
    std::vector<uint8_t> data(5000);
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<uint8_t>((i * 31 + 7) & 0xFF);
    }
    const std::vector<std::vector<uint8_t>> chunks = sim::chunk_config(data, 512);

    sim::ReliableSender sender;
    sim::ReliableReceiver receiver;
    sim::ConfigAssembler assembler;
    for (const auto& chunk : chunks) sender.send(chunk, 0);

    std::string error;
    bool complete = false;
    uint32_t now = 0;
    for (int step = 0; step < 600 && !complete; ++step) {
        now += 16;
        std::vector<std::vector<uint8_t>> out;
        sender.poll(now, out);
        int index = 0;
        for (const auto& packet : out) {
            ++index;
            if ((step * 7 + index * 13) % 10 < 3) continue;  // 30% loss
            std::vector<std::vector<uint8_t>> delivered;
            receiver.on_packet(packet.data(), packet.size(), delivered);
            for (const auto& chunk : delivered) {
                if (!assembler.add(chunk.data(), chunk.size(), error)) {
                    std::printf("FAIL: reliable chunk rejected: %s\n", error.c_str());
                    return 1;
                }
            }
        }
        std::vector<uint8_t> ack;
        receiver.make_ack(ack);
        const uint16_t ack_seq = static_cast<uint16_t>(ack[1]) |
                                 static_cast<uint16_t>(ack[2] << 8);
        const uint32_t bitmap = static_cast<uint32_t>(ack[3]) |
                                (static_cast<uint32_t>(ack[4]) << 8) |
                                (static_cast<uint32_t>(ack[5]) << 16) |
                                (static_cast<uint32_t>(ack[6]) << 24);
        sender.on_ack(ack_seq, bitmap, now);
        complete = assembler.complete();
    }

    std::vector<uint8_t> bad = chunks[0];
    bad[bad.size() - 1] ^= 0xFF;
    sim::ConfigAssembler corrupt;
    std::string corrupt_error;
    const bool rejected = !corrupt.add(bad.data(), bad.size(), corrupt_error);

    const bool same = assembler.assemble() == data;
    std::printf("  [reliable] chunks %zu retrans %d complete %d same %d corrupt %d dead %d\n",
                chunks.size(), sender.retransmits(), complete ? 1 : 0, same ? 1 : 0,
                rejected ? 1 : 0, sender.dead() ? 1 : 0);
    if (!complete || !same || sender.retransmits() == 0 || !rejected || sender.dead()) {
        std::printf("FAIL: A2 reliable stream test failed\n");
        return 1;
    }
    return 0;
}

int run_match_test() {
    sim::MatchHost host;
    std::string error;
    if (!host.start(0, 2, "playground", 7, 0, error)) {
        std::printf("FAIL: match host start: %s\n", error.c_str());
        return 1;
    }
    host.set_server_tick(900);

    sim::ClientId a{};
    a[0] = 0x01;
    sim::ClientId b{};
    b[0] = 0x02;
    sim::ClientId c{};
    c[0] = 0x03;

    const uint16_t port = host.port();
    sim::MatchReply ra;
    const bool accepted = sim::match_request("127.0.0.1", port, a, nullptr, ra) && ra.ok &&
                          ra.status == sim::MatchStatus::Accepted && ra.slot == 0 &&
                          ra.has_ticket && ra.seed == 7 && ra.players == 2 &&
                          ra.map == "playground" && ra.game_port == static_cast<uint16_t>(port + 1);

    // A5: the same client id + ticket returns to its old slot.
    sim::MatchReply rb;
    const bool resume = sim::match_request("127.0.0.1", port, a, &ra.ticket, rb) && rb.ok &&
                        rb.status == sim::MatchStatus::Accepted && rb.slot == 0;

    // A tampered MAC is refused even though the id is known.
    sim::Ticket tampered = ra.ticket;
    tampered.mac[0] ^= 0xFF;
    sim::MatchReply rc;
    const bool tamper = sim::match_request("127.0.0.1", port, a, &tampered, rc) && rc.ok &&
                        rc.status == sim::MatchStatus::BadTicket;

    // A second identity takes slot 1; a third is turned away.
    sim::MatchReply rd;
    const bool second = sim::match_request("127.0.0.1", port, b, nullptr, rd) && rd.ok &&
                        rd.status == sim::MatchStatus::Accepted && rd.slot == 1;
    sim::MatchReply re;
    const bool full = sim::match_request("127.0.0.1", port, c, nullptr, re) && re.ok &&
                      re.status == sim::MatchStatus::RoomFull;

    host.stop();
    bool event_accepted = false;
    bool event_resumed = false;
    bool event_full = false;
    const std::vector<sim::MatchEvent> events = host.drain();
    for (const sim::MatchEvent& ev : events) {
        if (ev.status == sim::MatchStatus::Accepted && ev.client_id == a && ev.slot == 0) {
            if (ev.resumed) {
                event_resumed = true;
            } else {
                event_accepted = true;
            }
        } else if (ev.status == sim::MatchStatus::RoomFull && ev.client_id == c) {
            event_full = true;
        }
    }

    // One admission rule for every UDP owner: approved ids are consumed once.
    sim::Admission admission;
    for (const sim::MatchEvent& ev : events) {
        if (ev.status == sim::MatchStatus::Accepted) admission.approve(ev.client_id, ev.slot);
    }
    // A was approved twice (cold join + resume), B once; an unknown id gets -1.
    const bool admit_ok = admission.take(a) == 0 && admission.take(a) == 0 &&
                          admission.take(b) == 1 && admission.take(c) == -1 &&
                          admission.pending() == 0;

    std::printf("  [match] accepted %d slot 0 ticket %d resume %d tamper %d full %d events %d admit %d\n",
                accepted, resume, resume, tamper, full,
                event_accepted && event_resumed && event_full ? 1 : 0, admit_ok ? 1 : 0);
    if (!accepted || !resume || !tamper || !second || !full || !event_accepted ||
        !event_resumed || !event_full || !admit_ok) {
        std::printf("FAIL: A1/A5 matchmaking failed: %s\n", error.c_str());
        return 1;
    }
    return 0;
}

int run_reconnect_test() {
    sim::NetServer server;
    std::string error;
    if (!server.init("sim/data/maps/test_box.map", "sim/data/movement.toml", 1, error)) {
        std::printf("FAIL: reconnect server init: %s\n", error.c_str());
        return 1;
    }
    const sim::NetConditions clean;
    sim::LoopbackTransport s2c(clean, 3);
    sim::LoopbackTransport c2s(clean, 4);

    const int first = server.add_client(&s2c, &c2s, 0);
    for (int tick = 0; tick < 10; ++tick) {
        server.receive();
        server.step();
        server.send();
        s2c.advance(static_cast<uint32_t>(tick));
        c2s.advance(static_cast<uint32_t>(tick));
    }
    // Same slot rejoins before the timeout retires the old link.
    server.add_client(&s2c, &c2s, 0);
    const int links = server.client_count();
    const bool active = server.world().players[0].active;

    std::printf("  [reconnect] first %d links %d active %d\n", first, links, active ? 1 : 0);
    if (first < 0 || links != 1 || !active) {
        std::printf("FAIL: reconnect must replace the stale link, not add a second one\n");
        return 1;
    }
    return 0;
}

int run_seqwrap_check(const std::string& data_path) {
    const std::string map_path = "sim/data/maps/metrics_flat.map";
    std::string error;
    sim::NetServer server;
    if (!server.init(map_path, data_path, 1, error)) {
        std::printf("FAIL: seqwrap server init: %s\n", error.c_str());
        return 1;
    }
    sim::LoopbackTransport to_server(sim::NetConditions{}, 71);
    sim::LoopbackTransport to_client(sim::NetConditions{}, 72);
    server.add_client(&to_server, &to_client, 0);

    sim::NetClient client;
    if (!client.init(&to_server, &to_client, 0, map_path, data_path, error)) {
        std::printf("FAIL: seqwrap client init: %s\n", error.c_str());
        return 1;
    }

    auto tick_once = [&](const sim::MoveInput& in) {
        client.send_input(in);
        client.step_local();
        server.receive();
        server.step();
        server.send();
        to_server.advance(0);
        to_client.advance(0);
        client.receive();
    };

    for (int i = 0; i < 120; ++i) tick_once(net_test_input(i));

    // Park the sequence just below the wrap and run one full queue of sends
    // (0xFFC0..0xFFFF), so the next snapshot is the first wrapped one. This is
    // the state a real 17-minute session reaches: stale entries with high seqs.
    const uint16_t jump =
        static_cast<uint16_t>(0xFFC0u - static_cast<uint32_t>(server.debug_out_seq(0)));
    server.debug_advance_seq(jump);
    for (int i = 0; i < 64; ++i) tick_once(net_test_input(i + 120));

    const int delta_before = server.delta_snapshots();
    const int full_before = server.full_snapshots();

    // Cross the wrap and keep playing. A seq-keyed map evicts every fresh entry
    // here, so the acks stop matching and the server falls back to full forever.
    for (int i = 0; i < 300; ++i) tick_once(net_test_input(i + 184));

    const int delta_after = server.delta_snapshots();
    const int full_after = server.full_snapshots();
    const int delta_gain = delta_after - delta_before;
    const int full_gain = full_after - full_before;
    const bool ok = delta_gain > 250 && full_gain <= 2 && client.joined() &&
                    !client.join_failed() && server.client_count() == 1;

    std::printf("  [seqwrap] ok %d  delta +%d  full +%d\n", ok ? 1 : 0, delta_gain, full_gain);
    if (!ok) {
        std::printf("FAIL: snapshot acks stop matching across the u16 sequence wrap\n");
        return 1;
    }
    return 0;
}

int run_config_test() {
    sim::NetConditions cond;
    cond.loss = 0.5f;  // high enough that a chunk is certain to need a retransmit
    cond.latency_ms = 20.0f;
    sim::LoopbackTransport s2c(cond, 31);
    sim::LoopbackTransport c2s(cond, 41);
    sim::ReliableTransport srv(&s2c, &c2s);
    sim::ReliableTransport cli(&c2s, &s2c);

    std::string error;
    sim::NetServer server;
    if (!server.init("sim/data/maps/test_box.map", "sim/data/movement.toml", 1, error)) {
        std::printf("FAIL: config server init: %s\n", error.c_str());
        return 1;
    }
    sim::ConfigBlob config;
    config.params_hash = server.world().params_hash;
    config.geometry_hash = server.world().collision.geometry_hash();
    const std::vector<uint8_t> blob = sim::encode_config(config);
    server.set_configstring(blob);
    server.add_client(&srv, &srv, 0, &srv);

    sim::NetClient client;
    if (!client.init(&cli, &cli, 0, "sim/data/maps/test_box.map", "sim/data/movement.toml",
                      error, &cli)) {
        std::printf("FAIL: config client init: %s\n", error.c_str());
        return 1;
    }

    int ticks = 0;
    for (; ticks < 400; ++ticks) {
        server.receive();
        server.step();
        server.send();
        s2c.advance(static_cast<uint32_t>(ticks));
        c2s.advance(static_cast<uint32_t>(ticks));
        client.receive();
        if (client.config_ready()) break;
    }

    const bool ready = client.config_ready();
    const bool same = ready && client.config_string() == blob;
    const bool matched = !client.config_mismatch();
    const bool joined = client.full_states() > 0;

    const bool retransmitted = srv.retransmits() > 0;

    // Same pipeline, one hash bit flipped: the client must refuse the match.
    sim::NetServer other;
    other.init("sim/data/maps/test_box.map", "sim/data/movement.toml", 1, error);
    sim::ConfigBlob wrong = config;
    wrong.params_hash ^= 1u;
    other.set_configstring(sim::encode_config(wrong));
    sim::LoopbackTransport s2c2(cond, 51);
    sim::LoopbackTransport c2s2(cond, 61);
    sim::ReliableTransport srv2(&s2c2, &c2s2);
    sim::ReliableTransport cli2(&c2s2, &s2c2);
    other.add_client(&srv2, &srv2, 0, &srv2);
    sim::NetClient client2;
    client2.init(&cli2, &cli2, 0, "sim/data/maps/test_box.map", "sim/data/movement.toml",
                 error, &cli2);
    for (int t = 0; t < 200 && !client2.config_ready(); ++t) {
        other.receive();
        other.step();
        other.send();
        s2c2.advance(static_cast<uint32_t>(t));
        c2s2.advance(static_cast<uint32_t>(t));
        client2.receive();
    }
    const bool refused = client2.config_ready() && client2.config_mismatch();

    std::printf("  [config] chunks %u same %d matched %d refused %d full_state %d ticks %d retrans %d\n",
                static_cast<unsigned>(client.config_chunks()), same ? 1 : 0, matched ? 1 : 0,
                refused ? 1 : 0, joined ? 1 : 0, ticks, srv.retransmits());
    if (!ready || !same || !matched || !refused || !joined || !retransmitted) {
        std::printf("FAIL: A2 configstring over the reliable stream failed\n");
        return 1;
    }
    return 0;
}

int run_dual_channel_test() {
    sim::NetConditions cond;
    cond.loss = 0.2f;
    cond.latency_ms = 20.0f;
    sim::LoopbackTransport s2c(cond, 11);
    sim::LoopbackTransport c2s(cond, 22);
    sim::ReliableTransport server(&s2c, &c2s);
    sim::ReliableTransport client(&c2s, &s2c);

    // A2 ships the configstring as 512 B chunks; use the real chunk format.
    std::vector<uint8_t> blob(4096);
    for (std::size_t i = 0; i < blob.size(); ++i) {
        blob[i] = static_cast<uint8_t>((i * 37u) & 0xFFu);
    }
    const std::vector<std::vector<uint8_t>> chunks = sim::chunk_config(blob, 512);

    sim::ConfigAssembler assembler;
    std::string error;
    std::size_t next_chunk = 0;
    int snapshots_out = 0;
    int snapshots_in = 0;
    for (int tick = 0; tick < 400; ++tick) {
        while (next_chunk < chunks.size() && server.send_reliable(chunks[next_chunk])) ++next_chunk;

        std::vector<uint8_t> snap(8, 0);
        snap[0] = static_cast<uint8_t>(sim::PacketType::Snapshot);
        snap[7] = static_cast<uint8_t>(tick & 0xFF);
        server.send(snap);
        ++snapshots_out;

        server.advance(static_cast<uint32_t>(tick));
        s2c.advance(static_cast<uint32_t>(tick));
        client.advance(static_cast<uint32_t>(tick));
        c2s.advance(static_cast<uint32_t>(tick));

        std::vector<uint8_t> pkt;
        while (client.recv(pkt)) ++snapshots_in;
        std::vector<uint8_t> piece;
        while (client.recv_reliable(piece)) {
            if (!assembler.add(piece.data(), piece.size(), error)) {
                std::printf("FAIL: dual chunk rejected: %s\n", error.c_str());
                return 1;
            }
        }
        std::vector<uint8_t> ack_in;
        while (server.recv(ack_in)) {
        }
    }

    const bool complete = assembler.complete();
    const bool same = complete && assembler.assemble() == blob;
    const bool retransmitted = server.retransmits() > 0;
    const bool snapshots_ok = snapshots_in > snapshots_out * 3 / 4;
    const bool alive = !server.dead() && !client.dead();

    std::printf("  [dual] chunks %u/%u same %d retrans %d snapshot %d/%d dead %d\n",
                static_cast<unsigned>(assembler.total()),
                static_cast<unsigned>(chunks.size()), same ? 1 : 0, server.retransmits(),
                snapshots_in, snapshots_out, alive ? 0 : 1);
    if (!complete || !same || !retransmitted || !snapshots_ok || !alive) {
        std::printf("FAIL: A2 shared socket test failed (%s)\n", error.c_str());
        return 1;
    }
    return 0;
}

int run_signal_test() {
    std::string error;
    sim::TcpListener listener;
    if (!listener.listen(0, error)) {
        std::printf("FAIL: signal listen: %s\n", error.c_str());
        return 1;
    }
    sim::TcpStream client;
    if (!client.connect("127.0.0.1", listener.local_port(), 1000, error)) {
        std::printf("FAIL: signal connect: %s\n", error.c_str());
        return 1;
    }
    std::unique_ptr<sim::TcpStream> peer = listener.accept(1000, error);
    if (!peer) {
        std::printf("FAIL: signal accept: %s\n", error.c_str());
        return 1;
    }

    bool hello_ok = false, ack_ok = false, manifest_ok = false, ready_ok = false;
    bool match_ok = false, full_ok = false;
    sim::SignalMessage in;

    sim::ClientId id{};
    for (std::size_t i = 0; i < id.size(); ++i) id[i] = static_cast<uint8_t>(0x40 + i);
    sim::SignalMessage hello;
    hello.type = sim::SignalType::Hello;
    hello.protocol = sim::kSignalProtocol;
    hello.client_id = id;
    hello.has_ticket = true;
    hello.ticket.client_id = id;
    hello.ticket.expiry = 1119;
    hello.ticket.mac[0] = 0xA0;
    hello_ok = client.send_message(hello, error) && peer->recv_message(in, 1000, error) &&
               in.type == sim::SignalType::Hello && in.protocol == sim::kSignalProtocol &&
               in.client_id == id && in.has_ticket && in.ticket.expiry == 1119 &&
               in.ticket.mac[0] == 0xA0;

    sim::SignalMessage ack;
    ack.type = sim::SignalType::HelloAck;
    ack.accepted = true;
    ack.reason = 3;
    ack.server_tick = 120;
    ack.config_hash = 0xDEADBEEFu;
    ack.slot = 1;
    ack.has_ticket = true;
    ack.ticket.client_id = id;
    ack.ticket.expiry = 1239;
    ack_ok = peer->send_message(ack, error) && client.recv_message(in, 1000, error) &&
             in.type == sim::SignalType::HelloAck && in.accepted && in.reason == 3 &&
             in.server_tick == 120 && in.config_hash == 0xDEADBEEFu && in.slot == 1 &&
             in.has_ticket && in.ticket.client_id == id && in.ticket.expiry == 1239;

    sim::SignalMessage manifest;
    manifest.type = sim::SignalType::AssetManifest;
    manifest.assets.push_back({0x10u, 2048u, "playground.map"});
    manifest.assets.push_back({0x20u, 512u, "materials.toml"});
    manifest_ok = peer->send_message(manifest, error) &&
                  client.recv_message(in, 1000, error) &&
                  in.type == sim::SignalType::AssetManifest && in.assets.size() == 2 &&
                  in.assets[0].name == "playground.map" && in.assets[0].hash == 0x10u &&
                  in.assets[1].name == "materials.toml" && in.assets[1].size == 512u;

    sim::SignalMessage ready;
    ready.type = sim::SignalType::AssetReady;
    ready.ready = {0x10u, 0x20u};
    ready_ok = client.send_message(ready, error) && peer->recv_message(in, 1000, error) &&
               in.type == sim::SignalType::AssetReady && in.ready.size() == 2 &&
               in.ready[1] == 0x20u;

    sim::SignalMessage match;
    match.type = sim::SignalType::MatchStart;
    match.map = "playground";
    match.seed = 7;
    match.players = 2;
    match.game_port = 27016;
    match_ok = peer->send_message(match, error) && client.recv_message(in, 1000, error) &&
               in.type == sim::SignalType::MatchStart && in.map == "playground" &&
               in.seed == 7 && in.players == 2 && in.game_port == 27016;

    sim::SignalMessage full;
    full.type = sim::SignalType::FullState;
    full.tick = 4567;
    full_ok = peer->send_message(full, error) && client.recv_message(in, 1000, error) &&
              in.type == sim::SignalType::FullState && in.tick == 4567;

    std::printf("  [signal] hello %d ack %d manifest %d ready %d match %d full %d\n",
                hello_ok, ack_ok, manifest_ok, ready_ok, match_ok, full_ok);
    if (!hello_ok || !ack_ok || !manifest_ok || !ready_ok || !match_ok || !full_ok) {
        std::printf("FAIL: A1 signal roundtrip failed: %s\n", error.c_str());
        return 1;
    }
    return 0;
}

int run_clock_check(const std::string& data_path) {
    const std::string map_path = "sim/data/maps/metrics_flat.map";
    std::string error;

    sim::NetServer server;
    if (!server.init(map_path, data_path, 1, error)) {
        std::printf("FAIL: clock server init: %s\n", error.c_str());
        return 1;
    }
    sim::LoopbackTransport to_server(sim::NetConditions{}, 31);
    sim::LoopbackTransport to_client(sim::NetConditions{}, 32);
    server.add_client(&to_server, &to_client, 0);

    sim::NetClient client;
    if (!client.init(&to_server, &to_client, 0, map_path, data_path, error)) {
        std::printf("FAIL: clock client init: %s\n", error.c_str());
        return 1;
    }

    auto tick_once = [&](const sim::MoveInput& in) {
        client.send_input(in);
        client.step_local();
        server.receive();
        server.step();
        server.send();
        to_server.advance(0);
        to_client.advance(0);
        client.receive();
    };

    sim::MoveInput input;
    input.forward = 1.0f;
    input.yaw = 0.4f;
    for (int i = 0; i < 120; ++i) tick_once(input);

    // Simulate a client loop that ran faster than the server: six extra local
    // ticks, each with its own command, so the lead grows by six.
    for (int i = 0; i < 6; ++i) {
        client.send_input(input);
        client.step_local();
    }
    for (int i = 0; i < 240; ++i) tick_once(input);
    for (int i = 0; i < 60; ++i) tick_once(sim::MoveInput{});

    const float deviation = sim::length(client.predicted().pos - server.world().players[0].pos);
    std::printf("  [clock] skips %d far %d dev %.5f m\n", client.clock_skips(),
                server.far_commands(), static_cast<double>(deviation));

    int failures = 0;
    if (!client.joined()) {
        std::printf("FAIL: clock client did not join\n");
        ++failures;
    }
    if (client.clock_skips() == 0) {
        std::printf("FAIL: forced drift did not trigger a clock skip\n");
        ++failures;
    }
    if (server.far_commands() != 0) {
        std::printf("FAIL: drift pushed commands outside the input window\n");
        ++failures;
    }
    if (deviation > 0.01f) {
        std::printf("FAIL: clock correction diverged\n");
        ++failures;
    }
    return failures;
}

int run_net_test(const Options& opt) {
    int failures = 0;

    // The real transport is exercised first: the deterministic loopback tests
    // above it are meaningless if a datagram cannot leave the process.
    failures += run_udp_check();
    failures += run_fragment_check();
    failures += run_udp_session_check(opt.data_path);
    failures += run_clock_check(opt.data_path);
    failures += run_signal_test();
    failures += run_match_test();
    failures += run_dual_channel_test();
    failures += run_config_test();
    failures += run_reconnect_test();
    failures += run_seqwrap_check(opt.data_path);
    failures += run_reliable_test();
    failures += run_ticket_test();
    failures += run_timeout_test();

    // v0.4 5.2: duplicates, late and far commands never enter the queue twice.
    {
        sim::InputWindow window;
        sim::MoveInput in;
        in.forward = 1.0f;
        const bool accepted = window.push(2, in) == sim::InputWindow::Result::Accepted;
        const bool duplicate = window.push(2, in) == sim::InputWindow::Result::Duplicate;
        window.take(2, in);
        const bool late = window.push(1, in) == sim::InputWindow::Result::Late;
        const bool executed_duplicate =
            window.push(2, in) == sim::InputWindow::Result::Duplicate;
        const bool far = window.push(40, in) == sim::InputWindow::Result::TooFar;
        for (uint32_t t = 3; t <= 18; ++t) window.push(t, in);
        const bool full_window = window.push(19, in) == sim::InputWindow::Result::TooFar;
        const bool bounded = window.depth() == 16;
        std::printf("  window: accept %d dup %d late %d far %d exec-dup %d full-window %d depth %zu\n",
                    accepted, duplicate, late, far, executed_duplicate, full_window,
                    window.depth());
        if (!accepted || !duplicate || !late || !far || !executed_duplicate || !full_window ||
            !bounded) {
            std::printf("FAIL: input window does not reject duplicate/late/far commands\n");
            ++failures;
        }
    }

    // Plan 7.1: remote players interpolate between the two bracketing frames;
    // outside the buffer the nearest frame is used, never an extrapolation.
    {
        sim::SnapshotInterpolator interp;
        sim::PlayerSnap a;
        sim::PlayerSnap b;
        for (uint32_t t = 10; t <= 12; ++t) {
            sim::Snapshot snap;
            snap.tick = t;
            a.pos = sim::Vec3(static_cast<float>(t - 10), 0.0f, 0.0f);
            a.vel = sim::Vec3(1.0f, 0.0f, 0.0f);
            b.pos = sim::Vec3(10.0f - static_cast<float>(t - 10), 0.0f, 0.0f);
            b.vel = sim::Vec3(-1.0f, 0.0f, 0.0f);
            snap.players = {a, b};
            interp.push(snap);
        }
        std::vector<sim::PlayerSnap> out;
        const bool sampled = interp.sample(11, 0.5f, out);
        const bool midpoint =
            sampled && out.size() == 2 && std::fabs(out[0].pos.x - 1.5f) < 1e-5f &&
            std::fabs(out[1].pos.x - 8.5f) < 1e-5f;
        const bool exact = interp.sample(12, 0.0f, out) && std::fabs(out[0].pos.x - 2.0f) < 1e-6f;
        const bool clamped =
            interp.sample(20, 0.5f, out) && std::fabs(out[0].pos.x - 2.0f) < 1e-6f;
        const int before_dup = interp.size();
        sim::Snapshot duplicate;
        duplicate.tick = 11;
        duplicate.players = {a, b};
        interp.push(duplicate);
        const bool duplicate_ignored = interp.size() == before_dup;
        std::printf("  interp: midpoint %d exact %d clamp %d duplicate-ignored %d\n",
                    midpoint, exact, clamped, duplicate_ignored);
        if (!midpoint || !exact || !clamped || !duplicate_ignored) {
            std::printf("FAIL: snapshot interpolation is wrong\n");
            ++failures;
        }
    }

    // View yaw rides in the snapshot (its own field group), in full and delta form.
    {
        sim::Snapshot a;
        a.tick = 5;
        sim::PlayerSnap p;
        p.yaw = 1.25f;
        a.players = {p};
        const std::vector<uint8_t> full = sim::encode_snapshot(a, nullptr);
        sim::Snapshot decoded;
        const bool full_ok = sim::decode_snapshot(full.data(), full.size(), nullptr, decoded) &&
                             decoded.players.size() == 1 &&
                             std::fabs(decoded.players[0].yaw - 1.25f) < 1e-6f;

        sim::Snapshot b = a;
        b.tick = 6;
        b.players[0].yaw = 2.5f;
        const std::vector<uint8_t> delta = sim::encode_snapshot(b, &a);
        sim::Snapshot decoded_delta;
        const bool delta_ok = sim::decode_snapshot(delta.data(), delta.size(), &a, decoded_delta) &&
                              decoded_delta.players.size() == 1 &&
                              std::fabs(decoded_delta.players[0].yaw - 2.5f) < 1e-6f;
        std::printf("  [yaw] full %d delta %d\n", full_ok, delta_ok);
        if (!full_ok || !delta_ok) {
            std::printf("FAIL: view yaw does not survive snapshot encoding\n");
            ++failures;
        }
    }

    // Gate 8a: the original lossy link must converge and stay delta-based.
    const int ticks = 2000;
    const NetRunStats lossy = run_net_scenario(opt.data_path, 0.30f, 10.0f, ticks, 100);
    if (!lossy.ok) return 1;
    std::printf("  [30%% loss] rollbacks %d   snapshots %d   full %d   delta %d   depth %d\n",
                lossy.rollbacks, lossy.snapshots, lossy.full, lossy.delta, lossy.max_depth);
    std::printf("              late %d   dup %d   far %d   dev %.5f m\n", lossy.late,
                lossy.duplicate, lossy.far, static_cast<double>(lossy.deviation));
    std::printf("              hitbox %d frames, newest %u (server tick %u)\n",
                lossy.hitbox_size, lossy.hitbox_newest, lossy.server_tick);
    std::printf("              remote buffer %d frames, newest %u (last snapshot %u)\n",
                lossy.remote_frames, lossy.remote_newest, lossy.last_snapshot);

    if (lossy.deviation > 0.01f) {
        std::printf("FAIL: client and server disagree by more than 1 cm\n");
        ++failures;
    }
    if (lossy.rollbacks == 0) {
        std::printf("FAIL: no rollback happened, the lossy link should force at least one\n");
        ++failures;
    }
    if (lossy.rollbacks > ticks / 4) {
        std::printf("FAIL: %d rollbacks in %d ticks, cmd.tick is still not aligned\n",
                    lossy.rollbacks, ticks);
        ++failures;
    }
    if (lossy.snapshots < ticks / 2) {
        std::printf("FAIL: too few snapshots arrived (head-of-line blocking?)\n");
        ++failures;
    }
    if (lossy.full == 0) {
        std::printf("FAIL: never fell back to a full snapshot despite lost acks\n");
        ++failures;
    }
    if (lossy.delta <= lossy.full) {
        std::printf("FAIL: delta snapshots are not the common case\n");
        ++failures;
    }
    if (lossy.far != 0) {
        std::printf("FAIL: %d commands were outside the input window\n", lossy.far);
        ++failures;
    }
    if (lossy.hitbox_size != sim::HitboxHistory::kCapacity ||
        lossy.hitbox_newest != lossy.server_tick) {
        std::printf("FAIL: server hitbox history is not one sample per tick\n");
        ++failures;
    }
    if (lossy.remote_frames == 0 || lossy.remote_newest != lossy.last_snapshot) {
        std::printf("FAIL: client remote buffer does not track the newest snapshot\n");
        ++failures;
    }

    // Gate 8b (v0.4 N3): 5% loss + 30 ms + 10 ms jitter for 60 s.
    const int minute = 64 * 60;
    const NetRunStats light = run_net_scenario(opt.data_path, 0.05f, 10.0f, minute, 100);
    if (!light.ok) return 1;
    std::printf("  [5%% loss, 60 s] corrections %d   max %.4f m   rollbacks %d   dev %.5f m\n",
                light.corrections, static_cast<double>(light.max_correction), light.rollbacks,
                static_cast<double>(light.deviation));
    if (light.corrections >= 3) {
        std::printf("FAIL: %d position corrections in a minute (N3 allows < 3)\n",
                    light.corrections);
        ++failures;
    }
    if (light.max_correction > 0.5f) {
        std::printf("FAIL: single correction %.3f m exceeds 0.5 m (N3)\n",
                    static_cast<double>(light.max_correction));
        ++failures;
    }
    if (light.deviation > 0.01f) {
        std::printf("FAIL: light-loss link did not converge\n");
        ++failures;
    }

    // Gate 8c (plan 7.7): mid-match join and reconnect both go through FULL_STATE.
    failures += run_join_phase(opt.data_path);

    if (failures != 0) {
        std::printf("FAIL: %d network checks failed\n", failures);
        return 1;
    }
    std::printf("PASS: cmd.tick aligned, input dedup, and <3 corrections/min on a 5%% loss link\n");
    return 0;
}

