#include "sim/net/matchmaking.h"

#include <chrono>
#include <utility>

namespace sim {
namespace {

uint32_t wall_seconds() {
    using namespace std::chrono;
    return static_cast<uint32_t>(duration_cast<seconds>(
        system_clock::now().time_since_epoch()).count());
}

bool send_reject(TcpStream& stream, MatchStatus status, uint32_t server_tick, std::string& error) {
    SignalMessage ack;
    ack.type = SignalType::HelloAck;
    ack.accepted = false;
    ack.reason = static_cast<uint8_t>(status);
    ack.server_tick = server_tick;
    return stream.send_message(ack, error);
}

}  // namespace

MatchHost::MatchHost() = default;
MatchHost::~MatchHost() { stop(); }

bool MatchHost::start(uint16_t port, uint8_t players, std::string map, uint32_t seed,
                      uint8_t reserved, std::string& error) {
    m_players = players == 0 ? 1 : players;
    m_reserved = reserved > m_players ? m_players : reserved;
    m_map = std::move(map);
    m_seed = seed;
    m_slots.assign(m_players, ClientId{});
    if (!m_listener.listen(port, error)) return false;
    m_port = m_listener.local_port();
    m_stop.store(false);
    m_thread = std::thread([this] { run(); });
    return true;
}

void MatchHost::stop() {
    m_stop.store(true);
    if (m_thread.joinable()) m_thread.join();
    m_listener.close();
}

void MatchHost::run() {
    while (!m_stop.load()) {
        std::string error;
        std::unique_ptr<TcpStream> stream = m_listener.accept(50, error);
        if (!stream) continue;
        serve(*stream);
    }
}

int MatchHost::slot_of_locked(const ClientId& id) const {
    for (std::size_t i = 0; i < m_slots.size(); ++i) {
        if (m_slots[i] == id) return static_cast<int>(i);
    }
    return -1;
}

int MatchHost::slot_of(const ClientId& id) const {
    const std::lock_guard<std::mutex> lock(m_mutex);
    return slot_of_locked(id);
}

// A reconnect keeps its old slot; a fresh client takes the first free one.
int MatchHost::reserve_slot_locked(const ClientId& id) {
    const int existing = slot_of_locked(id);
    if (existing >= 0) return existing;
    for (std::size_t i = m_reserved; i < m_slots.size(); ++i) {
        if (m_slots[i] == ClientId{}) {
            m_slots[i] = id;
            return static_cast<int>(i);
        }
    }
    return -1;
}

void MatchHost::push(const MatchEvent& event) {
    const std::lock_guard<std::mutex> lock(m_mutex);
    m_events.push_back(event);
}

std::vector<MatchEvent> MatchHost::drain() {
    const std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<MatchEvent> out;
    out.reserve(m_events.size());
    while (!m_events.empty()) {
        out.push_back(std::move(m_events.front()));
        m_events.pop_front();
    }
    return out;
}

void MatchHost::serve(TcpStream& stream) {
    std::string error;
    SignalMessage hello;
    if (!stream.recv_message(hello, 2000, error)) return;

    MatchEvent event;
    event.players = m_players;
    event.map = m_map;
    event.seed = m_seed;
    event.game_port = static_cast<uint16_t>(m_port + 1);

    if (hello.type != SignalType::Hello || hello.protocol != kSignalProtocol) {
        send_reject(stream, MatchStatus::BadProtocol, m_server_tick.load(), error);
        event.status = MatchStatus::BadProtocol;
        push(event);
        return;
    }

    event.client_id = hello.client_id;
    const uint32_t now = wall_seconds();
    Ticket ticket;
    bool resumed = false;
    int slot = -1;

    if (hello.has_ticket) {
        // A5: a resume is admitted only if the MAC holds and the id matches.
        if (!m_issuer.verify(hello.ticket, now) || hello.ticket.client_id != hello.client_id) {
            send_reject(stream, MatchStatus::BadTicket, m_server_tick.load(), error);
            event.status = MatchStatus::BadTicket;
            push(event);
            return;
        }
        {
            const std::lock_guard<std::mutex> lock(m_mutex);
            slot = slot_of_locked(hello.client_id);
        }
        if (slot < 0) {
            send_reject(stream, MatchStatus::BadTicket, m_server_tick.load(), error);
            event.status = MatchStatus::BadTicket;
            push(event);
            return;
        }
        resumed = true;
        ticket = hello.ticket;
    } else {
        {
            const std::lock_guard<std::mutex> lock(m_mutex);
            slot = reserve_slot_locked(hello.client_id);
        }
        if (slot < 0) {
            send_reject(stream, MatchStatus::RoomFull, m_server_tick.load(), error);
            event.status = MatchStatus::RoomFull;
            push(event);
            return;
        }
        ticket = m_issuer.issue(hello.client_id, now, kTicketTtlSeconds);
    }

    SignalMessage ack;
    ack.type = SignalType::HelloAck;
    ack.accepted = true;
    ack.reason = 0;
    ack.server_tick = m_server_tick.load();
    ack.slot = static_cast<uint8_t>(slot);
    ack.has_ticket = true;
    ack.ticket = ticket;
    if (!stream.send_message(ack, error)) return;

    SignalMessage manifest;
    manifest.type = SignalType::AssetManifest;  // v0.1 ships no assets over TCP
    if (!stream.send_message(manifest, error)) return;

    SignalMessage ready;
    if (!stream.recv_message(ready, 2000, error) || ready.type != SignalType::AssetReady) return;

    SignalMessage start;
    start.type = SignalType::MatchStart;
    start.map = m_map;
    start.seed = m_seed;
    start.players = m_players;
    start.game_port = event.game_port;
    if (!stream.send_message(start, error)) return;

    event.status = MatchStatus::Accepted;
    event.slot = static_cast<uint8_t>(slot);
    event.resumed = resumed;
    push(event);
    ++m_handshakes;
}

bool match_request(const std::string& host, uint16_t port, const ClientId& id,
                   const Ticket* resume_ticket, MatchReply& out) {
    TcpStream stream;
    if (!stream.connect(host, port, 2000, out.error)) return false;

    SignalMessage hello;
    hello.type = SignalType::Hello;
    hello.protocol = kSignalProtocol;
    hello.client_id = id;
    if (resume_ticket != nullptr) {
        hello.has_ticket = true;
        hello.ticket = *resume_ticket;
    }
    if (!stream.send_message(hello, out.error)) return false;

    SignalMessage ack;
    if (!stream.recv_message(ack, 3000, out.error)) return false;
    if (ack.type != SignalType::HelloAck) {
        out.error = "expected hello ack";
        return false;
    }
    out.ok = true;
    if (!ack.accepted) {
        out.status = static_cast<MatchStatus>(ack.reason);
        return true;
    }

    out.status = MatchStatus::Accepted;
    out.slot = ack.slot;
    out.has_ticket = ack.has_ticket;
    out.ticket = ack.ticket;

    SignalMessage manifest;
    if (!stream.recv_message(manifest, 3000, out.error)) return false;
    if (manifest.type != SignalType::AssetManifest) {
        out.error = "expected asset manifest";
        return false;
    }
    SignalMessage ready;
    ready.type = SignalType::AssetReady;
    if (!stream.send_message(ready, out.error)) return false;

    SignalMessage start;
    if (!stream.recv_message(start, 3000, out.error)) return false;
    if (start.type != SignalType::MatchStart) {
        out.error = "expected match start";
        return false;
    }
    out.map = start.map;
    out.seed = start.seed;
    out.players = start.players;
    out.game_port = start.game_port;
    return true;
}

}  // namespace sim
