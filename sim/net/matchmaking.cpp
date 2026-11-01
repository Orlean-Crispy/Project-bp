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
    // Assigning over a joinable thread terminates the process; a host that is
    // started twice must stop the first listener first.
    if (m_thread.joinable()) stop();
    m_players = players == 0 ? 1 : players;
    m_reserved = reserved > m_players ? m_players : reserved;
    m_map = std::move(map);
    m_seed = seed;
    m_slots.assign(m_players, Slot{});
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
    // R4: occupancy belongs to the running host. Once stopped, no slot is
    // reserved, so a stale question about occupancy cannot answer RoomFull.
    const std::lock_guard<std::mutex> lock(m_mutex);
    m_slots.assign(m_slots.size(), Slot{});
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
    if (id == ClientId{}) return -1;
    for (std::size_t i = 0; i < m_slots.size(); ++i) {
        if (m_slots[i].id == id) return static_cast<int>(i);
    }
    return -1;
}

int MatchHost::slot_of(const ClientId& id) const {
    const std::lock_guard<std::mutex> lock(m_mutex);
    return slot_of_locked(id);
}

void MatchHost::release(const ClientId& id) {
    const std::lock_guard<std::mutex> lock(m_mutex);
    const int slot = slot_of_locked(id);
    if (slot < 0 || slot < static_cast<int>(m_reserved)) return;  // host-owned slots stay
    Slot& entry = m_slots[static_cast<std::size_t>(slot)];
    // Keep the id: the seat is a ghost, not free. The owner can resume into it
    // for as long as the ticket lives, and a stranger may take it - oldest
    // ghost first - so an empty room never answers RoomFull (AUD-41).
    entry.held_since = wall_seconds();
    if (entry.held_since == 0) entry.held_since = 1;  // 0 means "occupied"
}

// A reconnect keeps its old slot; a fresh client takes the first free one, and
// only if none is free, the oldest ghost (a seat whose owner is gone and whose
// ticket may already be expired).
int MatchHost::reserve_slot_locked(const ClientId& id, uint32_t now) {
    const int existing = slot_of_locked(id);
    if (existing >= 0) return existing;

    for (std::size_t i = m_reserved; i < m_slots.size(); ++i) {
        if (m_slots[i].id == ClientId{}) {
            m_slots[i].id = id;
            m_slots[i].held_since = 0;
            return static_cast<int>(i);
        }
    }

    int oldest = -1;
    uint32_t oldest_since = 0;
    for (std::size_t i = m_reserved; i < m_slots.size(); ++i) {
        const Slot& entry = m_slots[i];
        if (entry.held_since == 0) continue;  // still occupied: never steal it
        if (now >= entry.held_since + kTicketTtlSeconds) {
            // Expired ghost: reclaim it before considering fresher ones.
            oldest = static_cast<int>(i);
            oldest_since = entry.held_since;
            break;
        }
        if (oldest < 0 || entry.held_since < oldest_since) {
            oldest = static_cast<int>(i);
            oldest_since = entry.held_since;
        }
    }
    if (oldest < 0) return -1;
    m_slots[static_cast<std::size_t>(oldest)].id = id;
    m_slots[static_cast<std::size_t>(oldest)].held_since = 0;
    return oldest;
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

    // m_slots uses an all-zero ClientId as "free", so a zero id would look like a
    // resume of whatever slot holds zeros -- including the host's reserved slot 0.
    if (hello.client_id == ClientId{}) {
        send_reject(stream, MatchStatus::BadTicket, m_server_tick.load(), error);
        event.client_id = hello.client_id;
        event.status = MatchStatus::BadTicket;
        push(event);
        return;
    }

    event.client_id = hello.client_id;
    const uint32_t now = wall_seconds();
    Ticket ticket;
    bool resumed = false;
    int slot = -1;
    bool reserved_now = false;  // true only for a fresh reservation, not a resume

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
        bool known_id = false;
        {
            // AUD-40: a known id without a ticket is a takeover attempt, not a
            // join. ClientIds are visible on the wire, so "I know the id" must
            // never be enough to take over someone's slot - only a valid resume
            // ticket is. The decision is made under the lock, but the reply is
            // sent outside it: this thread must never block the sim thread's
            // slot_of() call behind a socket write.
            const std::lock_guard<std::mutex> lock(m_mutex);
            known_id = slot_of_locked(hello.client_id) >= 0;
            if (!known_id) slot = reserve_slot_locked(hello.client_id, now);
        }
        if (known_id) {
            send_reject(stream, MatchStatus::BadTicket, m_server_tick.load(), error);
            event.status = MatchStatus::BadTicket;
            push(event);
            return;
        }
        if (slot < 0) {
            send_reject(stream, MatchStatus::RoomFull, m_server_tick.load(), error);
            event.status = MatchStatus::RoomFull;
            push(event);
            return;
        }
        reserved_now = true;
        ticket = m_issuer.issue(hello.client_id, now, kTicketTtlSeconds);
    }

    // R4: a slot is reserved before the handshake completes, so every exit
    // between here and MATCH_START has to give it back (a client that aborts
    // before AssetReady would otherwise hold it forever). A resume owns no new
    // reservation, so a failed resume leaves the live client's slot alone.
    const auto abandon = [&] {
        if (reserved_now) release(hello.client_id);
    };

    SignalMessage ack;
    ack.type = SignalType::HelloAck;
    ack.accepted = true;
    ack.reason = 0;
    ack.server_tick = m_server_tick.load();
    ack.slot = static_cast<uint8_t>(slot);
    ack.has_ticket = true;
    ack.ticket = ticket;
    if (!stream.send_message(ack, error)) {
        abandon();
        return;
    }

    SignalMessage manifest;
    manifest.type = SignalType::AssetManifest;  // v0.1 ships no assets over TCP
    if (!stream.send_message(manifest, error)) {
        abandon();
        return;
    }

    SignalMessage ready;
    if (!stream.recv_message(ready, 2000, error) || ready.type != SignalType::AssetReady) {
        abandon();
        return;
    }

    SignalMessage start;
    start.type = SignalType::MatchStart;
    start.map = m_map;
    start.seed = m_seed;
    start.players = m_players;
    start.game_port = event.game_port;
    if (!stream.send_message(start, error)) {
        abandon();
        return;
    }

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
