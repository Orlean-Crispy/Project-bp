#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "sim/core/world.h"
#include "sim/net/snapshot.h"
#include "sim/net/transport.h"

namespace sim {

struct InputCmd {
    uint32_t tick = 0;
    MoveInput input;
};

// Server-side clock estimate (plan 7.0): serverTick = localTick + offset, with the
// offset smoothed so the client never teleports its simulation time.
class ServerClock {
public:
    void sample(uint32_t server_tick, uint32_t local_tick, float rtt_seconds);
    bool initialized() const { return m_initialized; }
    float offset() const { return m_offset; }
    uint32_t server_tick_for(uint32_t local_tick) const;

private:
    bool m_initialized = false;
    float m_offset = 0.0f;
};

class NetServer {
public:
    bool init(const std::string& map_path, const std::string& params_path, int player_count,
              std::string& error);

    int add_client(Transport* incoming, Transport* outgoing, int player_index);
    void receive();
    void step();
    void send();

    const World& world() const { return m_world; }
    uint32_t tick() const { return m_world.tick; }
    int full_snapshots() const { return m_full; }
    int delta_snapshots() const { return m_delta; }
    int dropped_commands() const { return m_dropped_cmds; }

private:
    struct Link {
        Transport* rx = nullptr;
        Transport* tx = nullptr;
        int player = 0;
        uint16_t last_cmd_seq = 0;
        uint32_t last_cmd_tick = 0;
        bool has_command = false;
        MoveInput last_input;
        // Inputs keyed by the tick they belong to. Applying them out of a queue
        // would shift every input by the network latency and force a rollback on
        // every snapshot.
        std::map<uint32_t, MoveInput> inputs;
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
    int m_dropped_cmds = 0;
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
    ServerClock& clock() { return m_clock; }

private:
    struct HistoryEntry {
        uint32_t tick = 0;
        PlayerState state;
        MoveInput input;
    };

    void apply_snapshot(const Snapshot& snap);

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
    ServerClock m_clock;
};

}  // namespace sim
