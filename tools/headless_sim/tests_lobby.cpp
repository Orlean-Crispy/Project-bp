// Lobby gates: admission, tickets, the reliable stream and the matchmaking
// handshake. Split out of tests_net.cpp for the same reason as tests_transport:
// these are all "getting connected", not "staying in sync".

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "tests_lobby.h"

#include "sim/net/admission.h"
#include "sim/net/configstring.h"
#include "sim/net/matchmaking.h"
#include "sim/net/ports.h"
#include "sim/net/reliable.h"
#include "sim/net/session.h"
#include "sim/net/signaling.h"
#include "sim/net/ticket.h"
#include "sim/net/transport.h"


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

    // The handler is the contract: whoever admitted the client frees the slot.
    // Exactly what the bridge and net_loop install.
    std::vector<sim::ClientId> departed;
    server.set_departure_handler([&](const sim::ClientId& id) {
        departed.push_back(id);
        host.release(id);
    });

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
            reported = departed.size() == 1 && departed[0] == first_id;
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
        departed.clear();
        server.remove_client(link);
        if (departed.size() != 1 || departed[0] != fresh) {
            cycles = false;
            break;
        }
    }

    // A link with no id (the deterministic gates add those) must not report a
    // departure: releasing an all-zero id would free whichever slot holds zeros.
    departed.clear();
    const int anonymous = server.add_client(&down, &up, 0);
    if (anonymous >= 0) server.remove_client(anonymous);
    const bool anonymous_silent = departed.empty();

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
