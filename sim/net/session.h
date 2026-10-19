#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "sim/core/world.h"
#include "sim/game/hitbox.h"
#include "sim/net/snapshot.h"
#include "sim/net/transport.h"

namespace sim {

struct InputCmd {
    uint32_t tick = 0;
    MoveInput input;
};

// Plan 7.0: the client simulates on the server's tick axis. At join the clock is
// aligned to the server tick and shifted forward by kInputLead, so a command
// reaches the server before the tick it belongs to is executed; every local step
// then advances the axis by exactly one tick. Lead = one-way latency (2 ticks at
// 30 ms) + 2 ticks of lookahead, matching 7.0's "1-2 ticks ahead".
constexpr uint32_t kInputLead = 4;

class ServerClock {
public:
    void align(uint32_t server_tick);
    void advance() { ++m_command_tick; }
    bool initialized() const { return m_initialized; }
    uint32_t command_tick() const { return m_command_tick; }

private:
    bool m_initialized = false;
    uint32_t m_command_tick = kInputLead;
};

// v0.4 5.2: a duplicated, stale or far-future command must never be executed.
// The bitmap covers ticks last_executed+1 .. last_executed+8 (the 5.2 window).
struct InputWindow {
    enum class Result { Accepted, Duplicate, Late, TooFar, Overflow };
    Result push(uint32_t tick, const MoveInput& input);
    bool take(uint32_t tick, MoveInput& out);
    std::size_t depth() const { return m_queue.size(); }
    uint32_t last_executed() const { return m_last_executed; }

private:
    uint32_t m_last_executed = 0;
    uint16_t m_seen = 0;          // queued ticks, last_executed+1 .. +8
    uint16_t m_executed_seen = 0; // bit0 = last tick had input, bit1 = previous...
    std::map<uint32_t, MoveInput> m_queue;
};

class NetServer {
public:
    bool init(const std::string& map_path, const std::string& params_path, int player_count,
              std::string& error);

    int add_client(Transport* incoming, Transport* outgoing, int player_index);
    void remove_client(int link_index);
    void receive();
    void step();
    void send();

    const World& world() const { return m_world; }
    uint32_t tick() const { return m_world.tick; }
    int full_snapshots() const { return m_full; }
    int delta_snapshots() const { return m_delta; }
    int dropped_commands() const { return m_late_cmds; }
    int duplicate_commands() const { return m_dup_cmds; }
    int late_commands() const { return m_late_cmds; }
    int far_commands() const { return m_far_cmds; }
    int overflow_commands() const { return m_overflow_cmds; }
    int max_input_depth() const { return m_max_depth; }
    const HitboxHistory& hitboxes(int player) const {
        return m_hitboxes[static_cast<std::size_t>(player)];
    }

private:
    struct Link {
        Transport* rx = nullptr;
        Transport* tx = nullptr;
        int player = 0;
        // Plan 7.7: a fresh link starts with the full state, whether it is a
        // cold start or a reconnect. Normal snapshots resume after the first
        // command proves the client has joined.
        bool pending_full_state = true;
        uint32_t join_id = 0;  // changes on every admission, so repeats can be ignored
        uint16_t last_cmd_seq = 0;
        bool has_command = false;
        MoveInput last_input;
        // Inputs keyed by the tick they belong to (v0.4 5.2): duplicates and late
        // arrivals are dropped, never replayed as the newest input.
        InputWindow inputs;
        uint16_t out_seq = 0;
        int unacked = 0;
        Snapshot baseline;
        bool has_baseline = false;
        std::map<uint16_t, Snapshot> sent;  // seq -> snapshot, for ack lookup
        uint32_t last_acked_snapshot = 0;
    };

    World m_world;
    std::vector<Link> m_links;
    int m_full = 0;
    int m_delta = 0;
    int m_dup_cmds = 0;
    int m_late_cmds = 0;
    int m_far_cmds = 0;
    int m_overflow_cmds = 0;
    int m_max_depth = 0;
    uint32_t m_next_join_id = 1;
    std::vector<HitboxHistory> m_hitboxes;
};

class NetClient {
public:
    bool init(Transport* outgoing, Transport* incoming, int player_index,
              const std::string& map_path, const std::string& params_path, std::string& error);

    void send_input(const MoveInput& input);
    void step_local();
    void receive();

    const PlayerState& predicted() const;
    uint32_t tick() const { return m_local.tick; }
    int rollbacks() const { return m_rollbacks; }
    int snapshots_received() const { return m_snapshots; }
    int decode_failures() const { return m_decode_fail; }
    int stale_skips() const { return m_stale; }
    uint32_t last_snapshot_tick() const { return m_last_snapshot_tick; }
    int corrections() const { return m_corrections; }
    float max_correction() const { return m_max_correction; }
    bool joined() const { return m_joined; }
    int full_states() const { return m_full_states; }
    uint32_t join_tick() const { return m_join_tick; }
    bool join_failed() const { return m_join_failed; }
    const HitboxHistory& hitbox() const { return m_hitbox; }
    void reset_stats();
    ServerClock& clock() { return m_clock; }

private:
    struct HistoryEntry {
        uint32_t tick = 0;
        PlayerState state;
        MoveInput input;
    };

    void apply_snapshot(const Snapshot& snap);
    void apply_full_state(const Snapshot& snap);

    Transport* m_tx = nullptr;
    Transport* m_rx = nullptr;
    int m_player = 0;
    World m_local;
    std::vector<HistoryEntry> m_history;
    MoveInput m_last_input;
    std::deque<InputCmd> m_pending;  // last few inputs, resent every packet
    uint16_t m_seq = 0;
    uint16_t m_ack = 0;
    uint32_t m_ack_bits = 0;
    // Recent snapshots by tick, so a delta can be decoded against the exact
    // baseline the server used (which lags our newest snapshot by an ack).
    std::map<uint32_t, Snapshot> m_recent;
    uint32_t m_last_snapshot_tick = 0;
    int m_rollbacks = 0;
    int m_snapshots = 0;
    int m_decode_fail = 0;
    int m_stale = 0;
    int m_corrections = 0;
    float m_max_correction = 0.0f;
    bool m_joined = false;
    bool m_join_failed = false;
    int m_full_states = 0;
    uint32_t m_join_tick = 0;
    uint32_t m_join_id = 0;
    HitboxHistory m_hitbox;
    ServerClock m_clock;
};

}  // namespace sim
