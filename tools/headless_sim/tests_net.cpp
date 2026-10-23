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

#include "options.h"
#include "tests_net.h"
#include "test_util.h"

#include "bridge/remote_sampling.h"
#include "sim/core/tick.h"
#include "sim/core/version.h"
#include "sim/core/world.h"
#include "sim/demo/demo.h"
#include "sim/game/combat.h"
#include "sim/input/bindings.h"
#include "sim/net/admission.h"
#include "sim/net/configstring.h"
#include "sim/net/fragment.h"
#include "sim/net/handshake.h"
#include "sim/net/matchmaking.h"
#include "sim/net/netchan.h"
#include "sim/net/ports.h"
#include "sim/net/reliable.h"
#include "sim/net/reliable_transport.h"
#include "sim/net/session.h"
#include "sim/net/signaling.h"
#include "sim/net/ticket.h"
#include "sim/net/transport.h"
#include "sim/net/udp_hub.h"
#include "sim/net/udp_socket.h"
#include "sim/net/udp_transport.h"

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


// AUD-01: the event channel must work over the network, not only in solo play.
// A scripted jump has to reach the client's drainable EventQueue.
int run_net_events_check(const std::string& data_path) {
    const std::string map_path = "sim/data/maps/metrics_flat.map";
    std::string error;
    sim::NetServer server;
    if (!server.init(map_path, data_path, 1, error)) {
        std::printf("FAIL: netevents server init: %s\n", error.c_str());
        return 1;
    }
    sim::NetConditions conditions;
    sim::LoopbackTransport to_server(conditions, 4321);
    sim::LoopbackTransport to_client(conditions, 8765);
    server.add_client(&to_server, &to_client, 0);

    sim::NetClient client;
    if (!client.init(&to_server, &to_client, 0, map_path, data_path, error)) {
        std::printf("FAIL: netevents client init: %s\n", error.c_str());
        return 1;
    }

    sim::EventQueue events;
    auto tick_once = [&](const sim::MoveInput& in) {
        client.send_input(in);
        client.step_local(&events);
        server.receive();
        server.step();
        server.send();
        to_server.advance(0);
        to_client.advance(0);
        client.receive();
    };

    int jumps = 0;
    int lands = 0;
    int total = 0;
    for (int i = 0; i < 120; ++i) {
        sim::MoveInput in;
        in.forward = 1.0f;
        // One pressed edge on the ground: jump, then fall and land.
        in.jump = i == 40;
        tick_once(in);
        sim::SimEvent ev;
        while (events.pop(ev)) {
            ++total;
            if (ev.type == sim::EventType::Jump) ++jumps;
            if (ev.type == sim::EventType::Land) ++lands;
        }
    }

    // AUD-01 promised jump *and* land: the land half was only printed, so an event
    // channel that dropped every landing still passed.
    const bool ok = total > 0 && jumps > 0 && lands > 0;
    std::printf("  [netevents] %d\n", ok ? 1 : 0);
    if (!ok) {
        std::printf("FAIL: a networked jump produced no client events (total %d, jumps %d, "
                    "lands %d)\n",
                    total, jumps, lands);
        return 1;
    }
    return 0;
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
    if (bad == nullptr) {
        // Silently skipping this made the "wrong rules are refused" gate vanish on
        // a machine without a build directory.
        std::printf("FAIL: cannot write build/join_bad.toml for the join-hash check\n");
        return failures + 1;
    }
    {
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

    // R5 (docs/pitfalls/port-hijack.md): the game port must be exclusive. A
    // second socket trying to bind the very port we just took has to fail, and
    // the failed socket has to be closed again.
    sim::UdpSocket owner;
    sim::UdpSocket intruder;
    bool exclusive = false;
    if (owner.open(0, error) && owner.local_port() != 0) {
        exclusive = !intruder.open(owner.local_port(), error) && !intruder.valid();
    }
    std::printf("  [udp] exclusive %d\n", exclusive ? 1 : 0);
    if (!exclusive) {
        std::printf("FAIL: a second socket bound the same UDP port\n");
        ++failures;
    }

    // AUD-26: the client must ignore datagrams from anyone but connect_peer's
    // address, and say so. Two behaviours are checked, because either one alone
    // is easy to fake: the injection is not delivered, and a legitimate packet
    // sent right afterwards still is.
    bool source_ok = false;
    bool legit_after = false;
    int rejected = 0;
    sim::UdpSocket attacker;
    if (attacker.open(0, error)) {
        const std::vector<uint8_t> inject = {0xde, 0xad, 0xbe, 0xef};
        std::vector<uint8_t> sink;
        attacker.send_to(inject.data(), inject.size(), "127.0.0.1", client.local_port());
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        source_ok = !client.recv(sink);
        rejected = client.rejected();
        const std::vector<uint8_t> pong2 = {0x50, 0x49, 0x4e, 0x47, 0x32};
        server.send(pong2);
        legit_after = poll([&] { return client.recv(sink); }) && sink == pong2;
    }
    std::printf("  [udpsrc] rejected %d injection-drop %d legit-after %d\n", rejected,
                source_ok ? 1 : 0, legit_after ? 1 : 0);
    if (!source_ok || !legit_after || rejected != 1) {
        std::printf("FAIL: the client accepts UDP datagrams from an unexpected source (AUD-26)\n");
        ++failures;
    }
    return failures;
}

// AUD-23: the fragmenter must be on the real link, not only in a loopback gate.
// Two checks in one, because either alone is misleading:
//   * a 10-player full snapshot crosses a real socket intact and does NOT
//     fragment (the counters must stay 0: fragmentation is insurance, not the
//     normal path);
//   * a 3000 B message does fragment and arrive byte-identical, with the
//     counters proving the split happened.
int run_udp_fragment_check() {
    int failures = 0;
    std::string error;
    sim::UdpTransport a;
    sim::UdpTransport b;
    if (!a.open(0, error) || !b.open(0, error)) {
        std::printf("FAIL: frag udp open: %s\n", error.c_str());
        return 1;
    }
    if (!a.connect_peer("127.0.0.1", b.local_port(), error) ||
        !b.connect_peer("127.0.0.1", a.local_port(), error)) {
        std::printf("FAIL: frag udp connect: %s\n", error.c_str());
        return 1;
    }
    sim::FragmentedTransport fa(a);
    sim::FragmentedTransport fb(b);

    auto poll = [](auto&& fn) {
        for (int i = 0; i < 2000; ++i) {
            if (fn()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    };

    sim::Snapshot ten;
    ten.tick = 3;
    for (int i = 0; i < 10; ++i) {
        sim::PlayerState p;
        p.pos = sim::Vec3(static_cast<float>(i) * 2.0f, -1.0f, 0.05f);
        p.vitals.health = static_cast<int>(100 - i);
        p.vitals.armor = 25;
        ten.players.push_back(sim::to_snap(p));
    }
    const std::vector<uint8_t> bytes = sim::encode_snapshot(ten, nullptr);
    fa.send(bytes);
    std::vector<uint8_t> got;
    const bool ten_arrived = poll([&] { return fb.recv(got); }) && got == bytes;
    sim::Snapshot back;
    const bool ten_decoded = ten_arrived &&
                             sim::decode_snapshot(got.data(), got.size(), nullptr, back) &&
                             back.players.size() == 10 && back.players[9].hp == 91;
    const bool no_frag = fa.fragments_sent() == 0 && fb.reassembled() == 0;

    std::vector<uint8_t> big(3000);
    for (std::size_t i = 0; i < big.size(); ++i) {
        big[i] = static_cast<uint8_t>((i * 13 + 7) & 0xFF);
    }
    fa.send(big);
    const bool big_arrived = poll([&] { return fb.recv(got); }) && got == big;
    const bool split = fa.fragments_sent() == 3 && fb.reassembled() == 1;

    const std::vector<uint8_t> small = {1, 2, 3, 4};
    fa.send(small);
    const bool small_ok = poll([&] { return fb.recv(got); }) && got == small &&
                          fa.fragments_sent() == 3 && fb.reassembled() == 1;

    std::printf("  [frag] real-link 10p %zu B ten %d decode %d nofrag %d 3000B %d split %d small %d\n",
                bytes.size(), ten_arrived ? 1 : 0, ten_decoded ? 1 : 0, no_frag ? 1 : 0,
                big_arrived ? 1 : 0, split ? 1 : 0, small_ok ? 1 : 0);
    if (!ten_arrived || !ten_decoded || !no_frag || !big_arrived || !split || !small_ok) {
        std::printf("FAIL: the real link does not fragment correctly (AUD-23)\n");
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
    // At 30% loss some payloads must still arrive intact; without a lower bound a
    // receiver that never reassembles anything also reported corrupted == 0.
    // 20 payloads at 30% loss: a few are completely lost, so require a real
    // fraction to arrive (9-15 is the normal range), not all of them.
    const bool delivered_ok = delivered >= 5;
    if (!big_ok || !small_ok || corrupted != 0 || !delivered_ok) {
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
            sim::Transport* peer = hub.add_peer(host, port);
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
                          ra.map == "playground" && ra.game_port == sim::game_port_of(port);

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

    // R4: an aborted handshake gives its slot back. Take the reservation
    // (HELLO_ACK) and close before AssetReady: the same id must be able to
    // reserve again, and a fresh id must still get the second slot.
    sim::MatchHost aborted;
    bool release_ok = false;
    bool slots_cleared = false;
    if (!aborted.start(0, 2, "playground", 7, 0, error)) {
        std::printf("FAIL: aborted-handshake host start: %s\n", error.c_str());
        return 1;
    }
    sim::ClientId d{};
    d[0] = 0x04;
    sim::ClientId e{};
    e[0] = 0x05;
    bool reserved = false;
    {
        sim::TcpStream probe;
        if (probe.connect("127.0.0.1", aborted.port(), 2000, error)) {
            sim::SignalMessage hello;
            hello.type = sim::SignalType::Hello;
            hello.protocol = sim::kSignalProtocol;
            hello.client_id = d;
            sim::SignalMessage ack;
            reserved = probe.send_message(hello, error) &&
                       probe.recv_message(ack, 3000, error) &&
                       ack.type == sim::SignalType::HelloAck && ack.accepted && ack.slot == 0 &&
                       aborted.slot_of(d) == 0;
        }
        // Leaving this scope closes the stream without sending AssetReady.
    }
    bool released = false;
    for (int i = 0; i < 400 && !released; ++i) {
        if (aborted.slot_of(d) < 0) {
            released = true;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    sim::MatchReply rd2;
    const bool reclaim = released && sim::match_request("127.0.0.1", aborted.port(), d, nullptr, rd2) &&
                         rd2.ok && rd2.status == sim::MatchStatus::Accepted && rd2.slot == 0;
    sim::MatchReply re2;
    const bool second_slot =
        sim::match_request("127.0.0.1", aborted.port(), e, nullptr, re2) && re2.ok &&
        re2.status == sim::MatchStatus::Accepted && re2.slot == 1;
    aborted.stop();
    slots_cleared = aborted.slot_of(d) < 0 && aborted.slot_of(e) < 0;
    release_ok = reserved && released && reclaim && second_slot;

    std::printf("  [match] accepted %d slot 0 ticket %d resume %d second %d tamper %d full %d "
                "events %d admit %d release %d slots %d\n",
                accepted, resume, resume, second, tamper, full,
                event_accepted && event_resumed && event_full ? 1 : 0, admit_ok ? 1 : 0,
                release_ok ? 1 : 0, slots_cleared ? 1 : 0);
    if (!accepted || !resume || !tamper || !second || !full || !event_accepted ||
        !event_resumed || !event_full || !admit_ok || !release_ok || !slots_cleared) {
        std::printf("FAIL: A1/A5 matchmaking failed: %s\n", error.c_str());
        return 1;
    }
    return 0;
}

// AUD-27: on a two-slot room the second client used to be refused forever once
// both ids had joined once, because a normal leave never released its slot. The
// chain under test is the real one: the server drops the link, reports the id it
// was admitted with, and the matchmaking host frees that slot for the next id.
int run_slot_release_test() {
    int failures = 0;
    std::string error;
    sim::MatchHost host;
    // players 2, one reserved for the listen server: exactly one client slot.
    if (!host.start(0, 2, "playground", 7, 1, error)) {
        std::printf("FAIL: slot host start: %s\n", error.c_str());
        return 1;
    }
    sim::NetServer server;
    if (!server.init("sim/data/maps/metrics_flat.map", "sim/data/movement.toml", 2, error)) {
        std::printf("FAIL: slot server init: %s\n", error.c_str());
        host.stop();
        return 1;
    }
    sim::LoopbackTransport down(sim::NetConditions{}, 21);
    sim::LoopbackTransport up(sim::NetConditions{}, 22);

    // The host's own slot 0 stays reserved: the first client must land on 1.
    sim::ClientId first_id{};
    first_id[0] = 0x31;
    sim::MatchReply ra;
    const bool first = sim::match_request("127.0.0.1", host.port(), first_id, nullptr, ra) &&
                       ra.ok && ra.status == sim::MatchStatus::Accepted && ra.slot == 1;

    sim::ClientId blocked{};
    blocked[0] = 0x32;
    sim::MatchReply rb;
    const bool full = sim::match_request("127.0.0.1", host.port(), blocked, nullptr, rb) &&
                      rb.ok && rb.status == sim::MatchStatus::RoomFull;

    bool reported = false;
    bool freed = false;
    if (first) {
        const int link = server.add_client(&down, &up, ra.slot, nullptr, &first_id);
        if (link >= 0) {
            server.remove_client(link);
            std::vector<sim::ClientId> departed;
            reported = server.drain_departed(departed) == 1 && departed.size() == 1 &&
                       departed[0] == first_id;
            for (const sim::ClientId& id : departed) host.release(id);
        }
        sim::MatchReply rc;
        freed = sim::match_request("127.0.0.1", host.port(), blocked, nullptr, rc) && rc.ok &&
                rc.status == sim::MatchStatus::Accepted && rc.slot == 1;
    }

    // The id that took the freed slot also leaves before the next round.
    if (freed) host.release(blocked);

    // Three full join/leave cycles with fresh ids: the second round is the one
    // that used to fail.
    bool cycles = true;
    int rejoined = 0;
    for (int i = 0; i < 3 && cycles; ++i) {
        sim::ClientId fresh{};
        fresh[0] = static_cast<uint8_t>(0x40 + i);
        sim::MatchReply rr;
        if (!(sim::match_request("127.0.0.1", host.port(), fresh, nullptr, rr) && rr.ok &&
              rr.status == sim::MatchStatus::Accepted && rr.slot == 1)) {
            cycles = false;
            break;
        }
        const int link = server.add_client(&down, &up, rr.slot, nullptr, &fresh);
        if (link < 0) {
            cycles = false;
            break;
        }
        ++rejoined;
        server.remove_client(link);
        std::vector<sim::ClientId> departed;
        if (server.drain_departed(departed) != 1) {
            cycles = false;
            break;
        }
        for (const sim::ClientId& id : departed) host.release(id);
    }

    // A link with no id (the deterministic gates add those) must not enqueue a
    // departure: releasing an empty id would free whichever slot holds zeros.
    const int anonymous = server.add_client(&down, &up, 0);
    if (anonymous >= 0) server.remove_client(anonymous);
    std::vector<sim::ClientId> none;
    const bool anonymous_silent = server.drain_departed(none) == 0;

    host.stop();
    std::printf("  [slots] first %d full %d reported %d freed %d cycles %d/%d anonymous %d\n",
                first ? 1 : 0, full ? 1 : 0, reported ? 1 : 0, freed ? 1 : 0, rejoined,
                cycles ? 1 : 0, anonymous_silent ? 1 : 0);
    if (!first || !full || !reported || !freed || rejoined != 3 || !cycles || !anonymous_silent) {
        std::printf("FAIL: a departed client does not release its room slot (AUD-27)\n");
        ++failures;
    }
    return failures;
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

    // Same pipeline, a blob the client must refuse.
    auto refuses = [&](const sim::ConfigBlob& candidate) {
        sim::NetServer s;
        if (!s.init("sim/data/maps/test_box.map", "sim/data/movement.toml", 1, error)) {
            return false;
        }
        s.set_configstring(sim::encode_config(candidate));
        sim::LoopbackTransport s3c(cond, 71);
        sim::LoopbackTransport c3s(cond, 81);
        sim::ReliableTransport rs(&s3c, &c3s);
        sim::ReliableTransport rc(&c3s, &s3c);
        s.add_client(&rs, &rs, 0, &rs);
        sim::NetClient c;
        if (!c.init(&rc, &rc, 0, "sim/data/maps/test_box.map", "sim/data/movement.toml", error,
                    &rc)) {
            return false;
        }
        for (int t = 0; t < 200 && !c.config_ready(); ++t) {
            s.receive();
            s.step();
            s.send();
            s3c.advance(static_cast<uint32_t>(t));
            c3s.advance(static_cast<uint32_t>(t));
            c.receive();
        }
        return c.config_ready() && c.config_mismatch();
    };

    sim::ConfigBlob wrong = config;
    wrong.params_hash ^= 1u;
    const bool refused = refuses(wrong);

    // R8: the combat half is part of the join hash. A different combat.toml must
    // be refused exactly like a different movement.toml.
    sim::CombatParams other_combat = server.world().combat;
    other_combat.max_health += 1;
    sim::ConfigBlob combat_wrong = config;
    combat_wrong.params_hash = sim::combined_params_hash(server.world().params, other_combat);
    const bool combat_refused = refuses(combat_wrong);

    std::printf("  [config] chunks %u same %d matched %d refused %d combat %d full_state %d ticks %d retrans %d\n",
                static_cast<unsigned>(client.config_chunks()), same ? 1 : 0, matched ? 1 : 0,
                refused ? 1 : 0, combat_refused ? 1 : 0, joined ? 1 : 0, ticks,
                srv.retransmits());
    if (!ready || !same || !matched || !refused || !combat_refused || !joined ||
        !retransmitted) {
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
    match.game_port = sim::game_port_of(sim::kGateSignalPort);
    match_ok = peer->send_message(match, error) && client.recv_message(in, 1000, error) &&
               in.type == sim::SignalType::MatchStart && in.map == "playground" &&
               in.seed == 7 && in.players == 2 &&
               in.game_port == sim::game_port_of(sim::kGateSignalPort);

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

// R1: a correction must repair the rolled-back tick itself, not only the ticks
// replayed after it. Otherwise the hitbox history keeps the pre-correction
// prediction (M6 would rewind into it) and a duplicate snapshot at the same tick
// fails same_state and rolls back again, inflating the N3 counters.
// AUD-20: a client that misses 300 ticks (4.7 s) used to stay behind forever:
// its commands are stamped for ticks the server already executed, so they are all
// Late and nothing ever moves the axis forward. The gate freezes the client, then
// requires the late counter to settle and the player to keep moving - without a
// rejoin (full_states must stay 1).
int run_catch_up_check(const std::string& data_path) {
    const std::string map_path = "sim/data/maps/metrics_flat.map";
    std::string error;
    sim::NetServer server;
    if (!server.init(map_path, data_path, 1, error)) {
        std::printf("FAIL: catch-up server init: %s\n", error.c_str());
        return 1;
    }
    sim::NetConditions conditions;
    conditions.latency_ms = 20.0f;
    sim::LoopbackTransport to_server(conditions, 43);
    sim::LoopbackTransport to_client(conditions, 44);
    server.add_client(&to_server, &to_client, 0);
    sim::NetClient client;
    if (!client.init(&to_server, &to_client, 0, map_path, data_path, error)) {
        std::printf("FAIL: catch-up client init: %s\n", error.c_str());
        return 1;
    }

    sim::MoveInput in;
    in.forward = 1.0f;
    auto tick_once = [&](bool client_runs) {
        if (client_runs) {
            client.send_input(in);
            client.step_local();
        }
        server.receive();
        server.step();
        server.send();
        to_server.advance(0);
        to_client.advance(0);
        if (client_runs) client.receive();
    };

    for (int i = 0; i < 200; ++i) tick_once(true);
    const int accepted_before = server.accepted_commands();
    const uint32_t tick_before = client.tick();

    // The freeze: the server keeps running, the client neither sends nor polls.
    constexpr int kFreeze = 300;
    for (int i = 0; i < kFreeze; ++i) tick_once(false);
    const int late_frozen = server.late_commands();
    // A real socket buffer does not hold 4.7 s of datagrams: the stale ones are
    // gone by the time the client resumes. Draining here reproduces that, and it
    // is exactly what makes the first fresh snapshot land far ahead of the
    // client's own axis.
    {
        std::vector<uint8_t> stale;
        while (to_client.recv(stale)) {
        }
    }

    // Resume. The first snapshot has to re-anchor the axis; after that the
    // commands must be accepted again and the late counter has to stop moving.
    for (int i = 0; i < 8; ++i) tick_once(true);
    const int late_early = server.late_commands();
    const int accepted_early = server.accepted_commands();
    for (int i = 0; i < 56; ++i) tick_once(true);
    const int late_late = server.late_commands();
    const int accepted_late = server.accepted_commands();

    const bool caught = client.catch_ups() >= 1;
    // The exact deficit depends on how many snapshots were still buffered, so
    // the gate asks for "most of the freeze", not for a magic number.
    const bool behind = client.max_behind() >= static_cast<uint32_t>(kFreeze) - 32;
    const bool advanced = client.tick() > tick_before + static_cast<uint32_t>(kFreeze);
    const bool resumed = accepted_late > accepted_early && accepted_early > accepted_before;
    const bool settled = late_late == late_early;
    const bool moving = client.predicted().pos.x > 0.5f;
    const bool no_rejoin = client.full_states() == 1 && late_frozen >= 0;

    std::printf("  [catchup] freeze %d behind %u catchups %d accepted %d->%d->%d late "
                "%d->%d settled %d tick %u->%u moved %d\n",
                kFreeze, client.max_behind(), client.catch_ups(), accepted_before, accepted_early,
                accepted_late, late_early, late_late, settled ? 1 : 0, tick_before, client.tick(),
                moving ? 1 : 0);
    int failures = 0;
    if (!caught || !behind || !advanced || !resumed || !settled || !moving || !no_rejoin) {
        std::printf("FAIL: a client that fell behind never catches up (AUD-20)\n");
        ++failures;
    }
    return failures;
}

int run_rollback_repair_check(const std::string& data_path) {
    int failures = 0;
    const std::string map_path = "sim/data/maps/metrics_flat.map";
    std::string error;

    sim::NetServer server;
    if (!server.init(map_path, data_path, 1, error)) {
        std::printf("FAIL: rollback server init: %s\n", error.c_str());
        return 1;
    }
    sim::LoopbackTransport to_server(sim::NetConditions{}, 91);
    sim::LoopbackTransport to_client(sim::NetConditions{}, 92);
    server.add_client(&to_server, &to_client, 0);

    sim::NetClient client;
    if (!client.init(&to_server, &to_client, 0, map_path, data_path, error)) {
        std::printf("FAIL: rollback client init: %s\n", error.c_str());
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
    for (int i = 0; i < 80; ++i) tick_once(input);
    if (!client.joined()) {
        std::printf("FAIL: rollback client did not join\n");
        return 1;
    }

    // Hand-feed a snapshot for a tick the client has already predicted, with a
    // position its prediction cannot match. This is one real correction, using
    // the same wire format and receive path as the server's own snapshots.
    const uint32_t corrected_tick = client.tick();
    sim::Snapshot auth;
    auth.tick = corrected_tick;
    auth.players.push_back(sim::to_snap(client.predicted()));
    auth.players[0].pos.x += 0.25f;
    const sim::Vec3 corrected_pos = auth.players[0].pos;
    sim::PacketHeader header;
    header.type = sim::PacketType::Snapshot;
    header.seq = 1;
    const std::vector<uint8_t> packet =
        sim::wrap_packet(header, sim::encode_snapshot(auth, nullptr));

    auto deliver = [&](const std::vector<uint8_t>& pkt) {
        to_client.send(pkt);
        // The loopback queues with a one-tick delay; two advances deliver now.
        to_client.advance(0);
        to_client.advance(0);
        client.receive();
    };

    const int rollbacks_before = client.rollbacks();
    deliver(packet);
    const bool corrected_once = client.rollbacks() == rollbacks_before + 1;
    // (c) the corrected tick's hitbox sample must be the corrected position.
    const sim::HitboxSample sample = client.hitbox().at(corrected_tick);
    const bool state_ok = sim::length(sample.pos - corrected_pos) <= 1e-4f;

    // (b) a duplicate of that snapshot must compare equal to the repaired
    // history entry, so it must not roll back a second time.
    const int rollbacks_mid = client.rollbacks();
    deliver(packet);
    const bool dup_ok = client.rollbacks() == rollbacks_mid;

    // Keep playing: (a) the client hitbox history still tracks the local tick.
    for (int i = 0; i < 20; ++i) tick_once(input);
    const bool hitbox_ok = client.hitbox().newest_tick() == client.tick();

    std::printf("  [rollback] hitbox %d dup %d state %d\n", hitbox_ok ? 1 : 0, dup_ok ? 1 : 0,
                state_ok ? 1 : 0);
    if (!corrected_once) {
        std::printf("FAIL: the hand-fed snapshot did not force exactly one rollback\n");
        ++failures;
    }
    if (!hitbox_ok) {
        std::printf("FAIL: the client hitbox history no longer ends at the local tick\n");
        ++failures;
    }
    if (!dup_ok) {
        std::printf("FAIL: a duplicate snapshot at the same tick rolled back again (R1)\n");
        ++failures;
    }
    if (!state_ok) {
        std::printf("FAIL: the hitbox at the corrected tick kept the old prediction (R1)\n");
        ++failures;
    }
    return failures;
}

// R6 (iron law E): the network tick must not allocate in steady state. Only the
// two per-tick simulation calls are counted; packet serialisation and the loopback
// queue are outside the 4 ms sim budget, so counting them would measure the test
// transport instead of the session hot path.
//
// The threshold is zero, not "close to zero": averaging over 100 ticks used to
// turn one 100-allocation tick into 1.0 per tick, which a "> 1" check passed.
int run_net_alloc_check(const std::string& data_path) {
    const std::string map_path = "sim/data/maps/metrics_flat.map";
    std::string error;
    sim::NetServer server;
    if (!server.init(map_path, data_path, 1, error)) {
        std::printf("FAIL: netalloc server init: %s\n", error.c_str());
        return 1;
    }
    sim::NetConditions conditions;
    sim::LoopbackTransport to_server(conditions, 1234);
    sim::LoopbackTransport to_client(conditions, 5678);
    server.add_client(&to_server, &to_client, 0);

    sim::NetClient client;
    if (!client.init(&to_server, &to_client, 0, map_path, data_path, error)) {
        std::printf("FAIL: netalloc client init: %s\n", error.c_str());
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

    // Warm up past one-time growth: the 128-tick history, the 64-tick snapshot
    // rings and the loopback queues.
    sim::MoveInput in;
    in.forward = 1.0f;
    for (int i = 0; i < 200; ++i) tick_once(in);

    constexpr int kTicks = 100;
    unsigned long long allocs = 0;
    for (int i = 0; i < kTicks; ++i) {
        client.send_input(in);
        const unsigned long long before_local = allocation_count();
        client.step_local();
        const unsigned long long after_local = allocation_count();
        server.receive();
        const unsigned long long before_server = allocation_count();
        server.step();
        const unsigned long long after_server = allocation_count();
        server.send();
        to_server.advance(0);
        to_client.advance(0);
        client.receive();
        allocs += (after_local - before_local) + (after_server - before_server);
    }

    std::printf("  [netalloc] per_tick %g\n",
                static_cast<double>(allocs) / static_cast<double>(kTicks));
    if (allocs != 0) {
        std::printf("FAIL: the net tick allocated %llu times over %d ticks (iron law E)\n", allocs,
                    kTicks);
        return 1;
    }
    return 0;
}

int run_net_test(const Options& opt) {
    int failures = 0;

    // The real transport is exercised first: the deterministic loopback tests
    // above it are meaningless if a datagram cannot leave the process.
    failures += run_udp_check();
    failures += run_udp_fragment_check();
    failures += run_fragment_check();
    failures += run_udp_session_check(opt.data_path);
    failures += run_clock_check(opt.data_path);
    failures += run_catch_up_check(opt.data_path);
    failures += run_signal_test();
    failures += run_match_test();
    failures += run_slot_release_test();
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
        // Size alone is not the test: a push that *replaced* the frame would keep
        // the size and still corrupt the sample.
        std::vector<sim::PlayerSnap> after_dup;
        const bool duplicate_ignored =
            interp.size() == before_dup && interp.sample(11, 0.5f, after_dup) &&
            std::fabs(after_dup[0].pos.x - 1.5f) < 1e-5f;

        // AUD-05: the bridge rule must pick a tick inside the buffered range and
        // a real fraction, so sample() blends the two bracketing frames instead
        // of falling back to nearest. One frame clamps to that frame.
        bridge::RemoteSample rule;
        const bool rule_ok = bridge::remote_sample(interp.oldest_tick(), interp.newest_tick(),
                                                   0.5 * bridge::kTickSeconds, rule);
        const bool rule_in_range = rule_ok && rule.tick >= interp.oldest_tick() &&
                                   rule.tick <= interp.newest_tick();
        std::vector<sim::PlayerSnap> bridged;
        const bool rule_blends =
            rule_in_range && interp.sample(rule.tick, rule.frac, bridged) &&
            bridged.size() == 2 && bridged[0].pos.x > 1.0f && bridged[0].pos.x < 2.0f;

        sim::SnapshotInterpolator single;
        sim::Snapshot only;
        only.tick = 10;
        only.players = {a, b};
        single.push(only);
        bridge::RemoteSample one;
        const bool single_clamped =
            bridge::remote_sample(single.oldest_tick(), single.newest_tick(),
                                  0.5 * bridge::kTickSeconds, one) &&
            one.tick == 10;

        std::printf("  interp: midpoint %d exact %d clamp %d duplicate-ignored %d\n",
                    midpoint, exact, clamped, duplicate_ignored);
        std::printf("  [remotesample] rule %d in-range %d blends %d single %d "
                    "(tick %u frac %.2f)\n",
                    rule_ok, rule_in_range, rule_blends, single_clamped, rule.tick,
                    static_cast<double>(rule.frac));
        if (!midpoint || !exact || !clamped || !duplicate_ignored) {
            std::printf("FAIL: snapshot interpolation is wrong\n");
            ++failures;
        }
        if (!rule_in_range || !rule_blends || !single_clamped) {
            std::printf("FAIL: the bridge samples outside the snapshot axis (AUD-05)\n");
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

    // Every movement field must survive the wire: rollback rebuilds the player from
    // a snapshot, so a missing edge bit replays a held slide/dash/vault as a fresh
    // press, and a missing charge/speed pair replays at the wrong speed.
    {
        sim::PlayerState src;
        src.pos = sim::Vec3(1.0f, 2.0f, 3.0f);
        src.vel = sim::Vec3(4.0f, 5.0f, 6.0f);
        src.yaw = 0.5f;
        src.on_ground = true;
        src.crouched = true;
        src.active = false;
        src.jump_was_down = true;
        src.slide_was_down = true;
        src.dash_was_down = true;
        src.vault_was_down = true;
        src.impulses = 3;
        src.recharge_progress = 17;
        src.speed_coefficient = 0.75f;
        src.slow_ticks = 12;
        src.slow_factor = 0.85f;
        src.state = sim::ParkourState::WallRun;
        src.state_ticks = 5;
        src.dash_cooldown = 9;
        src.wall_normal = sim::Vec3(1.0f, 0.0f, 0.0f);
        src.vault_dir = sim::Vec3(0.0f, 1.0f, 0.0f);
        src.vault_rise = 0.4f;
        src.vitals.health = 42;
        src.vitals.armor = 25;

        sim::Snapshot one;
        one.tick = 42;
        one.players = {sim::to_snap(src)};
        const std::vector<uint8_t> bytes = sim::encode_snapshot(one, nullptr);
        sim::Snapshot decoded;
        const bool decoded_ok = sim::decode_snapshot(bytes.data(), bytes.size(), nullptr, decoded) &&
                                decoded.players.size() == 1;
        sim::PlayerState round;
        if (decoded_ok) sim::from_snap(decoded.players[0], round);
        const bool fields_ok =
            decoded_ok && round.pos == src.pos && round.vel == src.vel && round.yaw == src.yaw &&
            round.on_ground == src.on_ground && round.crouched == src.crouched &&
            round.active == src.active && round.jump_was_down == src.jump_was_down &&
            round.slide_was_down == src.slide_was_down &&
            round.dash_was_down == src.dash_was_down &&
            round.vault_was_down == src.vault_was_down && round.impulses == src.impulses &&
            round.recharge_progress == src.recharge_progress &&
            round.speed_coefficient == src.speed_coefficient && round.slow_ticks == src.slow_ticks &&
            // slow_factor is derived from slow_ticks by pmove, not carried state.
            round.state == src.state &&
            round.state_ticks == src.state_ticks && round.dash_cooldown == src.dash_cooldown &&
            round.wall_normal == src.wall_normal && round.vault_dir == src.vault_dir &&
            round.vault_rise == src.vault_rise &&
            round.vitals.health == src.vitals.health && round.vitals.armor == src.vitals.armor;
        std::printf("  [snapfields] %d\n", fields_ok ? 1 : 0);
        if (!fields_ok) {
            std::printf("FAIL: the snapshot loses a field (protocol 6)\n");
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

    // AUD-01: jump/land events survive the local prediction in network play.
    failures += run_net_events_check(opt.data_path);

    // R1: the rolled-back tick itself must be repaired, not only the replay.
    failures += run_rollback_repair_check(opt.data_path);

    // R6 (iron law E): a steady network tick allocates nothing.
    failures += run_net_alloc_check(opt.data_path);

    // Gate 8c (plan 7.7): mid-match join and reconnect both go through FULL_STATE.
    failures += run_join_phase(opt.data_path);

    if (failures != 0) {
        std::printf("FAIL: %d network checks failed\n", failures);
        return 1;
    }
    std::printf("PASS: cmd.tick aligned, input dedup, and <3 corrections/min on a 5%% loss link\n");
    return 0;
}

