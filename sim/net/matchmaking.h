#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "sim/net/signaling.h"
#include "sim/net/ticket.h"

namespace sim {

// A5 landing: the ticket now travels in the TCP control plane, so the UDP side
// only has to admit a client the host already vetted.
constexpr uint8_t kMatchNoSlot = 0xFF;
constexpr uint32_t kTicketTtlSeconds = 120;

enum class MatchStatus : uint8_t {
    Accepted = 0,
    BadProtocol = 1,
    BadTicket = 2,
    RoomFull = 3,
};

// What the sim thread is allowed to see. The TCP thread never touches the world.
struct MatchEvent {
    ClientId client_id{};
    MatchStatus status = MatchStatus::Accepted;
    uint8_t slot = kMatchNoSlot;
    bool resumed = false;
    uint32_t seed = 0;
    uint8_t players = 0;
    std::string map;
    uint16_t game_port = 0;
};

struct MatchReply {
    bool ok = false;              // the request itself completed
    MatchStatus status = MatchStatus::BadProtocol;
    uint8_t slot = kMatchNoSlot;
    bool resumed = false;
    Ticket ticket{};
    bool has_ticket = false;
    uint32_t seed = 0;
    uint8_t players = 0;
    std::string map;
    uint16_t game_port = 0;
    std::string error;
};

// A1 host side. Owns the TCP listener and one thread; v0.4 2.2 forbids blocking
// IO on the sim thread, so every result is handed over through drain().
class MatchHost {
public:
    MatchHost();
    ~MatchHost();
    MatchHost(const MatchHost&) = delete;
    MatchHost& operator=(const MatchHost&) = delete;

    // port 0 = ephemeral (the gate uses it so parallel runs cannot collide).
    bool start(uint16_t port, uint8_t players, std::string map, uint32_t seed, std::string& error);
    void stop();
    std::vector<MatchEvent> drain();
    uint16_t port() const { return m_port; }
    void set_server_tick(uint32_t tick) { m_server_tick.store(tick); }
    int handshakes() const { return m_handshakes.load(); }
    int slot_of(const ClientId& id) const;

private:
    void run();
    void serve(TcpStream& stream);
    int slot_of_locked(const ClientId& id) const;
    int reserve_slot_locked(const ClientId& id);
    void push(const MatchEvent& event);

    TicketIssuer m_issuer;
    TcpListener m_listener;
    std::thread m_thread;
    mutable std::mutex m_mutex;
    std::deque<MatchEvent> m_events;
    std::vector<ClientId> m_slots;
    std::atomic<bool> m_stop{false};
    std::atomic<uint32_t> m_server_tick{0};
    std::atomic<int> m_handshakes{0};
    uint8_t m_players = 2;
    uint32_t m_seed = 0;
    std::string m_map;
    uint16_t m_port = 0;
};

// A1 client side. Blocking on purpose: the caller in the bridge runs it on its
// own thread, and the gate calls it directly.
bool match_request(const std::string& host, uint16_t port, const ClientId& id,
                   const Ticket* resume_ticket, MatchReply& out);

}  // namespace sim
