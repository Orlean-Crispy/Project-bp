#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "sim/core/world.h"
#include "sim/game/hitbox.h"
#include "sim/net/client_id.h"
#include "sim/net/configstring.h"
#include "sim/net/interp.h"
#include "sim/net/reliable.h"
#include "sim/net/snapshot.h"
#include "sim/net/transport.h"

namespace sim {

class ReliableTransport;

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
    uint32_t command_tick() const { return m_command_tick; }

private:
    bool m_initialized = false;
    uint32_t m_command_tick = kInputLead;
};

// v0.4 5.2: a duplicated, stale or far-future command must never be executed.
// The bitmap covers ticks last_executed+1 .. last_executed+8 (the 5.2 window).
struct InputWindow {
    // The earliness window equals the queue depth (16), and duplicates are
    // filtered by the bitmap, so an entry can never be evicted: no Overflow state.
    static constexpr uint32_t kWindow = 16;

    enum class Result { Accepted, Duplicate, Late, TooFar };
    Result push(uint32_t tick, const MoveInput& input);
    bool take(uint32_t tick, MoveInput& out);
    std::size_t depth() const { return static_cast<std::size_t>(m_count); }
    uint32_t last_executed() const { return m_last_executed; }

private:
    // One slot per tick offset in the window. The server used to store at most 16
    // commands in a std::map and walk it on every packet; a fixed array keyed by
    // tick % kWindow (validated by the stored tick) keeps the same semantics with
    // no tree and no allocation (audit R19).
    struct Entry {
        uint32_t tick = 0;
        MoveInput input;
        bool valid = false;
    };

    uint32_t m_last_executed = 0;
    uint16_t m_seen = 0;          // queued ticks, last_executed+1 .. +8
    uint16_t m_executed_seen = 0; // bit0 = last tick had input, bit1 = previous...
    Entry m_slots[kWindow];
    int m_count = 0;
};

class NetServer {
public:
    bool init(const std::string& map_path, const std::string& params_path, int player_count,
              std::string& error);

    // A2: pass the shared-socket wrapper to deliver the join configstring over the
    // reliable stream; nullptr keeps the plain unreliable-only path (gates).
    // who (AUD-27): the client id the matchmaking layer reserved this slot for.
    // It is optional so the deterministic gates can add anonymous links, but when
    // present the server reports it back on removal so the host can release the
    // slot instead of leaving the room permanently full.
    int add_client(Transport* incoming, Transport* outgoing, int player_index,
                   ReliableTransport* reliable = nullptr, const ClientId* who = nullptr);
    void set_configstring(std::vector<uint8_t> blob);
    void remove_client(int link_index);
    // AUD-27: the room slot belongs to whoever admitted the client, so the server
    // only reports the departure. A queue nobody drains eventually leaks a slot
    // (net_loop forgot to drain it on day one); a handler cannot be forgotten.
    // Called from remove_client(); it must not call back into the server.
    void set_departure_handler(std::function<void(const ClientId&)> handler) {
        m_on_departure = std::move(handler);
    }
    void receive();
    void step();
    void send();

    const World& world() const { return m_world; }
    uint32_t tick() const { return m_world.tick; }
    int full_snapshots() const { return m_full; }
    int delta_snapshots() const { return m_delta; }
    int duplicate_commands() const { return m_dup_cmds; }
    int late_commands() const { return m_late_cmds; }
    int far_commands() const { return m_far_cmds; }
    int max_input_depth() const { return m_max_depth; }
    int accepted_commands() const { return m_accepted_cmds; }
    int client_count() const { return static_cast<int>(m_links.size()); }
    int timed_out_clients() const { return m_timed_out; }
    // R17: callers (tools, bridge) pass indices that may outlive a link or a
    // slot. An out-of-range index returns a safe default instead of reading
    // past the vector.
    uint32_t last_executed_tick(int link) const {
        return valid_link(link) ? m_links[static_cast<std::size_t>(link)].inputs.last_executed() : 0;
    }
    uint32_t last_cmd_tick(int link) const {
        return valid_link(link) ? m_links[static_cast<std::size_t>(link)].last_cmd_tick : 0;
    }
    uint32_t last_seen_tick(int link) const {
        return valid_link(link) ? m_links[static_cast<std::size_t>(link)].last_seen_tick : 0;
    }
    const HitboxHistory& hitboxes(int player) const {
        static const HitboxHistory kNoHitboxes;
        if (player < 0 || player >= static_cast<int>(m_hitboxes.size())) return kNoHitboxes;
        return m_hitboxes[static_cast<std::size_t>(player)];
    }

    // Gate hooks: drive the outgoing snapshot sequence around the u16 wrap
    // without simulating the 65536 ticks a real wrap needs.
    uint16_t debug_out_seq(int link) const {
        return m_links[static_cast<std::size_t>(link)].out_seq;
    }
    void debug_advance_seq(uint16_t delta);

private:
    bool valid_link(int link) const {
        return link >= 0 && link < static_cast<int>(m_links.size());
    }

    struct Link {
        Transport* rx = nullptr;
        Transport* tx = nullptr;
        int player = 0;
        // Plan 7.7: a fresh link starts with the full state, whether it is a
        // cold start or a reconnect. Normal snapshots resume after the first
        // command proves the client has joined.
        bool pending_full_state = true;
        uint32_t join_id = 0;  // changes on every admission, so repeats can be ignored
        uint32_t last_cmd_tick = 0;
        uint32_t last_seen_tick = 0;
        uint32_t last_rx_tick = 0;
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
        ReliableTransport* rel = nullptr;
        bool has_id = false;
        ClientId id{};
        std::vector<std::vector<uint8_t>> config_chunks;
        std::size_t config_next = 0;
        // Acks carry the u16 snapshot seq, but the queue is ordered by send time,
        // never by seq: after 65536 sends the seq wraps and a seq-keyed map would
        // sort fresh entries below stale ones, evicting them and losing the ack.
        // R6: a fixed-capacity ring instead of a deque, so a steady tick reuses
        // the same 64 slots (and their player vectors) instead of allocating a
        // node and copying the whole snapshot into it.
        struct SentSnapshot {
            uint16_t seq = 0;
            Snapshot snapshot;
        };
        struct SentRing {
            static constexpr std::size_t kCapacity = 64;

            std::vector<SentSnapshot> slots;
            std::size_t head = 0;  // index of the oldest entry
            std::size_t count = 0;

            void init() {
                slots.resize(kCapacity);
                head = 0;
                count = 0;
            }
            const SentSnapshot* find_seq(uint16_t seq) const {
                // Newest to oldest, like the old deque reverse scan.
                for (std::size_t k = 0; k < count; ++k) {
                    const SentSnapshot* entry = &slots[(head + count - 1 - k) % kCapacity];
                    if (entry->seq == seq) return entry;
                }
                return nullptr;
            }
            void push(uint16_t seq, const Snapshot& snapshot) {
                SentSnapshot* slot = nullptr;
                if (count < kCapacity) {
                    slot = &slots[(head + count) % kCapacity];
                    ++count;
                } else {
                    slot = &slots[head];
                    head = (head + 1) % kCapacity;
                }
                slot->seq = seq;
                slot->snapshot = snapshot;
            }
        };
        SentRing sent;
    };

    World m_world;
    std::vector<Link> m_links;
    // R6: per-tick input scratch, cleared with resize()+fill instead of built as
    // a fresh vector every step.
    std::vector<MoveInput> m_inputs;
    int m_full = 0;
    int m_delta = 0;
    int m_dup_cmds = 0;
    int m_late_cmds = 0;
    int m_far_cmds = 0;
    int m_max_depth = 0;
    int m_accepted_cmds = 0;
    int m_timed_out = 0;
    uint32_t m_next_join_id = 1;
    std::function<void(const ClientId&)> m_on_departure;
    std::vector<HitboxHistory> m_hitboxes;
    std::vector<std::vector<uint8_t>> m_config_chunks;
};

class NetClient {
public:
    bool init(Transport* outgoing, Transport* incoming, int player_index,
              const std::string& map_path, const std::string& params_path, std::string& error,
              ReliableTransport* reliable = nullptr);

    void send_input(const MoveInput& input);
    // AUD-01: the predicted world emits jump/land events too, so the caller that
    // owns an EventQueue passes it in. nullptr (gates, headless loops) stays free.
    void step_local(EventQueue* events = nullptr);
    void receive();

    const PlayerState& predicted() const;
    uint32_t tick() const { return m_local.tick; }
    int rollbacks() const { return m_rollbacks; }
    int snapshots_received() const { return m_snapshots; }
    // Malformed or reordered snapshots: a rising number means the baseline ring
    // is too shallow or the link reorders badly.
    int bad_snapshots() const { return m_decode_fail + m_stale; }
    uint32_t last_snapshot_tick() const { return m_last_snapshot_tick; }
    // Ticks since the last snapshot arrived; the only honest "server is gone"
    // signal a client has (a frozen world looks identical to a dead link).
    int snapshot_age() const;
    int corrections() const { return m_corrections; }
    int clock_skips() const { return m_clock_skips; }
    // AUD-20: how many times the local tick axis was jumped forward because it
    // had fallen behind the server's (a frame hitch longer than the input lead).
    int catch_ups() const { return m_catch_ups; }
    uint32_t max_behind() const { return m_behind_max; }
    float max_correction() const { return m_max_correction; }
    bool joined() const { return m_joined; }
    int player_index() const { return m_player; }
    uint32_t last_sent_tick() const {
        return m_pending.empty() ? 0 : m_pending.back().tick;
    }
    int full_states() const { return m_full_states; }
    uint32_t join_tick() const { return m_join_tick; }
    bool join_failed() const { return m_join_failed; }
    const SnapshotInterpolator& remote() const { return m_remote; }
    const HitboxHistory& hitbox() const { return m_hitbox; }
    bool config_ready() const { return m_config.complete(); }
    // True when the host's rules/map differ from ours: refuse to play.
    bool config_mismatch() const { return m_config_bad; }
    uint16_t config_chunks() const { return m_config.total(); }
    std::vector<uint8_t> config_string() const { return m_config.assemble(); }
    void reset_stats();
    ServerClock& clock() { return m_clock; }

private:
    struct HistoryEntry {
        uint32_t tick = 0;
        PlayerState state;
        MoveInput input;
    };

    // R6: the recent-snapshot map allocated a node and copied the whole snapshot
    // (players vector included) for every received tick. Delta decoding only ever
    // needs the last 64 ticks, so a fixed-capacity ring holds them in place.
    struct RecentEntry {
        uint32_t tick = 0;
        Snapshot snapshot;
    };
    struct RecentRing {
        static constexpr std::size_t kCapacity = 64;

        std::vector<RecentEntry> slots;
        std::size_t head = 0;  // index of the oldest entry
        std::size_t count = 0;

        void init() {
            slots.resize(kCapacity);
            head = 0;
            count = 0;
        }
        void clear() {
            head = 0;
            count = 0;
        }
        const Snapshot* find(uint32_t tick) const {
            for (std::size_t k = 0; k < count; ++k) {
                const RecentEntry& entry = slots[(head + k) % kCapacity];
                if (entry.tick == tick) return &entry.snapshot;
            }
            return nullptr;
        }
        void push(uint32_t tick, const Snapshot& snapshot) {
            // Ticks arrive non-decreasing (older ones are rejected as stale), so a
            // repeated tick can only be the newest one: overwrite it in place,
            // exactly like the map's operator[] did.
            if (count > 0) {
                RecentEntry& newest = slots[(head + count - 1) % kCapacity];
                if (newest.tick == tick) {
                    newest.snapshot = snapshot;
                    return;
                }
            }
            RecentEntry* slot = nullptr;
            if (count < kCapacity) {
                slot = &slots[(head + count) % kCapacity];
                ++count;
            } else {
                slot = &slots[head];
                head = (head + 1) % kCapacity;
            }
            slot->tick = tick;
            slot->snapshot = snapshot;
        }
    };

    void apply_snapshot(const Snapshot& snap);
    void apply_full_state(const Snapshot& snap);
    // Ammo and fire timers advance on the predicted world too, or every snapshot
    // would disagree with it and roll the client back for a reload tick.
    void advance_weapon(const MoveInput& input);
    // Steps empty-input ticks until the local tick reaches the command tick,
    // recording each in the history and the hitbox ring. A join and a catch-up
    // must land on exactly the same axis, so they share this one loop.
    void advance_to_clock();
    // Re-anchors the tick axis ahead of server_tick and calls advance_to_clock().
    // Used only when the client fell behind (AUD-20).
    void catch_up(uint32_t server_tick);

    Transport* m_tx = nullptr;
    Transport* m_rx = nullptr;
    int m_player = 0;
    // AUD-01: kept so a rollback replay of already-predicted ticks feeds the same
    // channel; the event tick lets the presentation layer drop repeats.
    EventQueue* m_events = nullptr;
    World m_local;
    std::vector<HistoryEntry> m_history;
    MoveInput m_last_input;
    std::deque<InputCmd> m_pending;  // last few inputs, resent every packet
    uint16_t m_seq = 0;
    uint16_t m_ack = 0;
    uint32_t m_ack_bits = 0;
    // Recent snapshots by tick, so a delta can be decoded against the exact
    // baseline the server used (which lags our newest snapshot by an ack).
    RecentRing m_recent;
    uint32_t m_last_snapshot_tick = 0;
    int m_rollbacks = 0;
    int m_snapshots = 0;
    int m_decode_fail = 0;
    int m_stale = 0;
    int m_corrections = 0;
    float m_max_correction = 0.0f;
    bool m_joined = false;
    bool m_join_failed = false;
    int m_ahead_min = 1000000;
    int m_skip_cooldown = 0;
    bool m_skip_next = false;
    int m_clock_skips = 0;
    int m_catch_ups = 0;
    uint32_t m_behind_max = 0;
    int m_full_states = 0;
    uint32_t m_join_tick = 0;
    uint32_t m_join_id = 0;
    HitboxHistory m_hitbox;
    ReliableTransport* m_rel = nullptr;
    ConfigAssembler m_config;
    bool m_config_checked = false;
    bool m_config_bad = false;
    SnapshotInterpolator m_remote;
    ServerClock m_clock;
};

}  // namespace sim
