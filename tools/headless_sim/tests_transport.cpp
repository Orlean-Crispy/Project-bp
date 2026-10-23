// UDP transport and fragmentation gates. Split out of tests_net.cpp when that
// file passed the 70 KB mark: everything here talks to a real socket, so it is
// the layer that a deterministic loopback test cannot cover.

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "tests_transport.h"

#include "sim/net/fragment.h"
#include "sim/net/handshake.h"
#include "sim/net/ports.h"
#include "sim/net/session.h"
#include "sim/net/transport.h"
#include "sim/net/udp_hub.h"
#include "sim/net/udp_socket.h"
#include "sim/net/udp_transport.h"

namespace {

// Real sockets need wall-clock waiting. Polls fn() for up to the given number of
// milliseconds. The deterministic gates never call this: they advance a virtual
// clock instead.
template <typename Fn>
bool wait_for(Fn&& fn, int ms) {
    for (int i = 0; i < ms; ++i) {
        if (fn()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

}  // namespace


int run_udp_check() {
    int failures = 0;
    std::string error;
    sim::UdpTransport server;
    sim::UdpTransport client;
    if (!server.open(0, error) || !client.open(0, error)) {
        std::printf("FAIL: udp open: %s\n", error.c_str());
        return 1;
    }
    // Both ends connect: the production host path is UdpPeerHub (which demuxes
    // by source address), so a connected endpoint is all UdpTransport has to be.
    if (!client.connect_peer("127.0.0.1", server.local_port(), error) ||
        !server.connect_peer("127.0.0.1", client.local_port(), error)) {
        std::printf("FAIL: udp connect: %s\n", error.c_str());
        return 1;
    }

    const std::vector<uint8_t> hello = {0x42, 0x50, 0x01, 0x02};
    std::vector<uint8_t> got;
    std::vector<uint8_t> pong;

    client.send(hello);
    const bool hello_ok = wait_for([&] { return server.recv(got); }, 200) && got == hello;

    pong = {0x50, 0x4f, 0x4e, 0x47};
    server.send(pong);
    const bool pong_ok = wait_for([&] { return client.recv(got); }, 200) && got == pong;

    std::vector<uint8_t> large(1200);
    for (size_t i = 0; i < large.size(); ++i) {
        large[i] = static_cast<uint8_t>((i * 7 + 3) & 0xFF);
    }
    client.send(large);
    const bool large_ok = wait_for([&] { return server.recv(got); }, 200) && got == large;
    const bool empty_ok = !client.recv(got);

    std::printf("  [udp] hello %d pong %d 1200B %d empty-poll %d\n", hello_ok, pong_ok ? 1 : 0,
                large_ok ? 1 : 0, empty_ok ? 1 : 0);
    if (!hello_ok || !pong_ok || !large_ok || !empty_ok) {
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
        legit_after = wait_for([&] { return client.recv(sink); }, 200) && sink == pong2;
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
    const bool ten_arrived = wait_for([&] { return fb.recv(got); }, 2000) && got == bytes;
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
    const bool big_arrived = wait_for([&] { return fb.recv(got); }, 2000) && got == big;
    const bool split = fa.fragments_sent() == 3 && fb.reassembled() == 1;
    // Nothing may be abandoned on a clean link: a non-zero counter here would mean
    // fragments are being lost somewhere the loss model does not cover.
    const bool no_abandon = fa.dropped_packets() == 0 && fb.dropped_packets() == 0;

    const std::vector<uint8_t> small = {1, 2, 3, 4};
    fa.send(small);
    const bool small_ok = wait_for([&] { return fb.recv(got); }, 2000) && got == small &&
                          fa.fragments_sent() == 3 && fb.reassembled() == 1;

    std::printf("  [frag] real-link 10p %zu B ten %d decode %d nofrag %d 3000B %d split %d "
                "small %d abandon %d\n",
                bytes.size(), ten_arrived ? 1 : 0, ten_decoded ? 1 : 0, no_frag ? 1 : 0,
                big_arrived ? 1 : 0, split ? 1 : 0, small_ok ? 1 : 0, no_abandon ? 1 : 0);
    if (!ten_arrived || !ten_decoded || !no_frag || !big_arrived || !split || !small_ok ||
        !no_abandon) {
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
