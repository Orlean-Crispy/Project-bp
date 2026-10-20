#include "sim/net/session.h"

#include "sim/core/tick.h"
#include "sim/net/bytes.h"
#include "sim/net/netchan.h"
#include "sim/net/reliable_transport.h"

namespace sim {
namespace {

constexpr float kDt = tick_seconds();

enum InputFlag : uint16_t {
    kJump = 1u << 0,
    kCrouch = 1u << 1,
    kSlide = 1u << 2,
    kDash = 1u << 3,
    kWallRun = 1u << 4,
    kWallClimb = 1u << 5,
    kVault = 1u << 6,
};

uint16_t encode_flags(const MoveInput& in) {
    uint16_t f = 0;
    if (in.jump) f |= kJump;
    if (in.crouch) f |= kCrouch;
    if (in.slide) f |= kSlide;
    if (in.dash) f |= kDash;
    if (in.wallrun) f |= kWallRun;
    if (in.wallclimb) f |= kWallClimb;
    if (in.vault) f |= kVault;
    return f;
}

MoveInput decode_flags(uint16_t f) {
    MoveInput in;
    in.jump = (f & kJump) != 0;
    in.crouch = (f & kCrouch) != 0;
    in.slide = (f & kSlide) != 0;
    in.dash = (f & kDash) != 0;
    in.wallrun = (f & kWallRun) != 0;
    in.wallclimb = (f & kWallClimb) != 0;
    in.vault = (f & kVault) != 0;
    return in;
}

// Plan 7.9: every packet repeats the last few inputs, so one lost packet does
// not turn into a lost tick on the server.
constexpr size_t kInputRedundancy = 4;

std::vector<uint8_t> encode_commands(const std::deque<InputCmd>& cmds) {
    ByteWriter w;
    w.u8(static_cast<uint8_t>(cmds.size()));
    for (const InputCmd& cmd : cmds) {
        w.u32(cmd.tick);
        w.f32(cmd.input.forward);
        w.f32(cmd.input.right);
        w.f32(cmd.input.yaw);
        w.u16(encode_flags(cmd.input));
    }
    return w.data();
}

bool decode_commands(const uint8_t* data, size_t size, std::vector<InputCmd>& out) {
    ByteReader r(data, size);
    const uint8_t count = r.u8();
    for (uint8_t i = 0; i < count; ++i) {
        InputCmd cmd;
        cmd.tick = r.u32();
        cmd.input.forward = r.f32();
        cmd.input.right = r.f32();
        cmd.input.yaw = r.f32();
        const uint16_t flags = r.u16();
        if (!r.ok()) return false;
        const MoveInput decoded = decode_flags(flags);
        cmd.input.jump = decoded.jump;
        cmd.input.crouch = decoded.crouch;
        cmd.input.slide = decoded.slide;
        cmd.input.dash = decoded.dash;
        cmd.input.wallrun = decoded.wallrun;
        cmd.input.wallclimb = decoded.wallclimb;
        cmd.input.vault = decoded.vault;
        out.push_back(cmd);
    }
    return r.ok();
}

bool same_state(const PlayerSnap& a, const PlayerState& b) {
    const PlayerSnap bs = to_snap(b);
    // The same field set World::state_hash() covers (audit R9), in the same
    // order. Comparing only pos/vel/state would call a replayed prediction equal
    // even when its timers or its wall/vault state differs, so the replay that
    // would have fixed it never happens. jump_was_down rides in the flags.
    return a.pos == bs.pos && a.vel == bs.vel && a.yaw == bs.yaw && a.flags == bs.flags &&
           a.impulses == bs.impulses && a.state == bs.state && a.state_ticks == bs.state_ticks &&
           a.dash_cooldown == bs.dash_cooldown && a.slow_ticks == bs.slow_ticks &&
           a.wall_normal == bs.wall_normal &&
           a.vault_dir == bs.vault_dir && a.vault_rise == bs.vault_rise;
}

}  // namespace

void ServerClock::align(uint32_t server_tick) {
    m_command_tick = server_tick + kInputLead;
    m_initialized = true;
}

// The window is an earliness window, not a latency buffer: a command labelled T
// is applied at tick T no matter how early it arrives. Clients run kInputLead (4)
// plus join slack (3) ahead, so the window must comfortably exceed 7 or the
// oldest entry would be evicted before its tick is executed.
InputWindow::Result InputWindow::push(uint32_t tick, const MoveInput& input) {
    if (tick <= m_last_executed) {
        // Already executed. A resend of a tick that was applied is the normal
        // redundancy path; one that arrives after a tick executed without input
        // is a genuine late command and is counted once.
        const uint32_t age = m_last_executed - tick;
        if (age >= kWindow) return Result::Duplicate;
        const uint16_t bit = static_cast<uint16_t>(1u << age);
        if ((m_executed_seen & bit) != 0) return Result::Duplicate;
        m_executed_seen = static_cast<uint16_t>(m_executed_seen | bit);
        return Result::Late;
    }
    const uint32_t offset = tick - m_last_executed - 1;
    if (offset >= kWindow) return Result::TooFar;
    const uint16_t bit = static_cast<uint16_t>(1u << offset);
    if ((m_seen & bit) != 0) return Result::Duplicate;

    // Window == depth and duplicates are filtered above, so the array can never
    // overflow here; a slot is only ever reused after its tick was executed.
    Entry& slot = m_slots[tick % kWindow];
    slot.tick = tick;
    slot.input = input;
    slot.valid = true;
    ++m_count;
    m_seen = static_cast<uint16_t>(m_seen | bit);
    return Result::Accepted;
}

bool InputWindow::take(uint32_t tick, MoveInput& out) {
    if (tick > m_last_executed) {
        const uint32_t steps = tick - m_last_executed;
        m_seen = steps >= kWindow ? 0 : static_cast<uint16_t>(m_seen >> steps);
        m_executed_seen = steps >= kWindow ? 0 : static_cast<uint16_t>(m_executed_seen << steps);
        m_last_executed = tick;
    }
    Entry& slot = m_slots[tick % kWindow];
    const bool found = slot.valid && slot.tick == tick;
    if (found) {
        out = slot.input;
        slot.valid = false;
        --m_count;
    }
    // Bit 0 records whether the tick just executed had a real command; the
    // history lets a late arrival be told apart from a normal resend.
    m_executed_seen =
        static_cast<uint16_t>((m_executed_seen & ~1u) | (found ? 1u : 0u));
    return found;
}

bool NetServer::init(const std::string& map_path, const std::string& params_path,
                     int player_count, std::string& error) {
    MapData map;
    if (!parse_map_file(map_path, map, error)) return false;
    std::vector<std::string> warnings;
    if (!load_movement_params(params_path, m_world.params, warnings, error)) return false;
    if (!load_combat_beside(params_path, m_world.combat, error)) return false;
    m_world.collision.build(map);
    // R8: the join hash covers movement *and* combat, so a host and a client
    // on different combat rules refuse to play instead of silently diverging.
    m_world.params_hash = combined_params_hash(m_world.params, m_world.combat);

    m_world.players.clear();
    for (int i = 0; i < player_count; ++i) {
        PlayerState p;
        p.pos = Vec3(-16.0f + static_cast<float>(i) * 2.0f, 0.0f, 0.05f);
        p.impulses = m_world.params.max_impulses;
        p.active = false;  // reserved until a link joins this slot
        m_world.players.push_back(p);
    }
    m_world.tick = 0;
    m_hitboxes.assign(static_cast<std::size_t>(player_count), HitboxHistory{});
    for (int i = 0; i < player_count; ++i) {
        m_hitboxes[static_cast<std::size_t>(i)].record(
            m_world.players[static_cast<std::size_t>(i)], m_world.params, 0);
    }
    return true;
}

void NetServer::set_configstring(std::vector<uint8_t> blob) {
    m_config_chunks = chunk_config(blob, 512);
}

int NetServer::add_client(Transport* incoming, Transport* outgoing, int player_index,
                          ReliableTransport* reliable) {
    if (player_index < 0) {
        // Reuse the first free slot: after a timeout a reconnect must land on
        // the same player the server kept in the world.
        for (size_t i = 0; i < m_world.players.size(); ++i) {
            if (!m_world.players[i].active) {
                player_index = static_cast<int>(i);
                break;
            }
        }
        if (player_index < 0) return -1;
    }
    if (player_index >= static_cast<int>(m_world.players.size())) return -1;
    // A reconnect can arrive before the 5 s timeout retires the old link. The
    // slot is the identity, so the newcomer replaces the stale link instead of
    // adding a second writer for the same player.
    for (std::size_t i = 0; i < m_links.size(); ++i) {
        if (m_links[i].player == player_index) {
            m_links.erase(m_links.begin() + static_cast<long>(i));
            break;
        }
    }
    Link link;
    link.rx = incoming;
    link.tx = outgoing;
    link.player = player_index;
    link.rel = reliable;
    if (reliable != nullptr) link.config_chunks = m_config_chunks;
    link.join_id = m_next_join_id++;
    if (player_index >= 0 && player_index < static_cast<int>(m_world.players.size())) {
        m_world.players[static_cast<std::size_t>(player_index)].active = true;
    }
    m_links.push_back(link);
    return static_cast<int>(m_links.size()) - 1;
}

void NetServer::remove_client(int link_index) {
    if (link_index < 0 || link_index >= static_cast<int>(m_links.size())) return;
    // The player stays in the world; a reconnect re-attaches a link to it and
    // receives the full state again (plan 7.7). Until then it is not rendered
    // by anyone (active=false).
    if (m_links[static_cast<std::size_t>(link_index)].player <
        static_cast<int>(m_world.players.size())) {
        m_world.players[static_cast<std::size_t>(
            m_links[static_cast<std::size_t>(link_index)].player)].active = false;
    }
    m_links.erase(m_links.begin() + link_index);
}

void NetServer::debug_advance_seq(uint16_t delta) {
    for (Link& link : m_links) link.out_seq = static_cast<uint16_t>(link.out_seq + delta);
}

void NetServer::receive() {
    for (Link& link : m_links) {
        std::vector<uint8_t> packet;
        while (link.rx->recv(packet)) {
            PacketHeader header;
            const uint8_t* payload = nullptr;
            size_t size = 0;
            if (!unwrap_packet(packet, header, payload, size)) continue;
            link.last_rx_tick = m_world.tick;
            link.ever_seen = true;
            if (header.type != PacketType::Command) continue;

            std::vector<InputCmd> cmds;
            if (!decode_commands(payload, size, cmds)) continue;

            // Acknowledge the snapshot the client is using as its baseline. The
            // queue is ordered by send time and only ever holds the last 64
            // sends, so at most one entry can carry this seq (an ack older than
            // 64 snapshots is not queued at all) and the newest match is it.
            if (header.ack != 0) {
                for (auto it = link.sent.rbegin(); it != link.sent.rend(); ++it) {
                    if (it->seq != header.ack) continue;
                    link.baseline = it->snapshot;
                    link.has_baseline = true;
                    link.unacked = 0;
                    link.last_acked_snapshot = header.ack;
                    break;
                }
            }

            for (const InputCmd& cmd : cmds) {
                link.has_command = true;
                link.last_cmd_seq = header.seq;
                link.last_seen_tick = cmd.tick;
                switch (link.inputs.push(cmd.tick, cmd.input)) {
                    case InputWindow::Result::Accepted:
                        // A command for a future tick proves the client has
                        // received the full state; stale packets cannot, because
                        // they resolve to Late below.
                        link.pending_full_state = false;
                        link.last_cmd_tick = cmd.tick;
                        ++m_accepted_cmds;
                        break;
                    case InputWindow::Result::Duplicate:
                        ++m_dup_cmds;  // normal under redundancy, do not log
                        break;
                    case InputWindow::Result::Late:
                        // v0.4 5.3: the tick has already been executed. Drop it
                        // instead of replaying it as the newest input, otherwise
                        // the whole input stream shifts by one tick and the
                        // client can never converge.
                        ++m_late_cmds;
                        break;
                    case InputWindow::Result::TooFar:
                        ++m_far_cmds;  // clock skew or cheating
                        break;
                }
                if (link.inputs.depth() > static_cast<std::size_t>(m_max_depth)) {
                    m_max_depth = static_cast<int>(link.inputs.depth());
                }
            }
        }
    }
}

void NetServer::step() {
    // A client silent for 5 s is gone; free its slot so nobody keeps rendering
    // a frozen capsule. A reconnect re-attaches through FULL_STATE.
    constexpr uint32_t kTimeoutTicks = seconds_to_ticks(5.0f);
    for (int i = static_cast<int>(m_links.size()) - 1; i >= 0; --i) {
        const Link& link = m_links[static_cast<std::size_t>(i)];
        if (link.ever_seen && m_world.tick - link.last_rx_tick > kTimeoutTicks) {
            ++m_timed_out;
            remove_client(i);
        }
    }

    std::vector<MoveInput> inputs(m_world.players.size());
    for (Link& link : m_links) {
        MoveInput in;
        if (link.inputs.take(m_world.tick, in)) {
            link.last_input = in;
            link.has_command = true;
        }
        // If no command arrived for this tick, repeat the last one (Q3 behaviour).
        if (link.player >= 0 && link.player < static_cast<int>(inputs.size())) {
            inputs[static_cast<size_t>(link.player)] = link.has_command ? link.last_input : MoveInput{};
        }
    }
    m_world.step(inputs, kDt);
    for (std::size_t i = 0; i < m_world.players.size() && i < m_hitboxes.size(); ++i) {
        m_hitboxes[i].record(m_world.players[i], m_world.params, m_world.tick);
    }
}

void NetServer::send() {
    for (Link& link : m_links) {
        if (link.rel != nullptr) {
            // A2: the join configstring rides the reliable half of the socket and
            // never blocks a snapshot. Window 32 is deeper than any blob we ship.
            while (link.config_next < link.config_chunks.size() &&
                   link.rel->send_reliable(link.config_chunks[link.config_next])) {
                ++link.config_next;
            }
            link.rel->advance(m_world.tick);
        }
        if (link.pending_full_state) {
            Snapshot full;
            full.tick = m_world.tick;
            full.acked_cmd = link.last_cmd_seq;
            for (const PlayerState& p : m_world.players) full.players.push_back(to_snap(p));
            const std::vector<uint8_t> body = encode_snapshot(full, nullptr);

            ByteWriter w;
            w.u32(link.join_id);
            w.u8(static_cast<uint8_t>(link.player));
            w.u64(m_world.params_hash);
            w.u64(m_world.collision.geometry_hash());
            w.raw(body.data(), body.size());

            PacketHeader header;
            header.type = PacketType::FullState;
            header.seq = link.out_seq++;
            header.ack = link.last_cmd_seq;
            header.ack_bits = 0;
            link.tx->send(wrap_packet(header, w.data()));
            continue;
        }

        Snapshot snap;
        snap.tick = m_world.tick;
        snap.acked_cmd = link.last_cmd_seq;
        for (const PlayerState& p : m_world.players) snap.players.push_back(to_snap(p));

        // Plan 7.1: delta against the last acknowledged snapshot, but fall back to
        // a full snapshot after three unacknowledged sends.
        const bool use_delta = link.has_baseline && link.unacked < 3;
        const Snapshot* baseline = use_delta ? &link.baseline : nullptr;
        const std::vector<uint8_t> payload = encode_snapshot(snap, baseline);

        PacketHeader header;
        header.type = PacketType::Snapshot;
        header.seq = link.out_seq++;
        header.ack = link.last_cmd_seq;
        header.ack_bits = 0;
        link.tx->send(wrap_packet(header, payload));

        link.sent.push_back(Link::SentSnapshot{header.seq, snap});
        while (link.sent.size() > 64) link.sent.pop_front();

        ++link.unacked;
        if (use_delta) ++m_delta; else ++m_full;
    }
}

bool NetClient::init(Transport* outgoing, Transport* incoming, int player_index,
                     const std::string& map_path, const std::string& params_path,
                     std::string& error, ReliableTransport* reliable) {
    m_tx = outgoing;
    m_rx = incoming;
    m_player = player_index;
    m_rel = reliable;

    MapData map;
    if (!parse_map_file(map_path, map, error)) return false;
    std::vector<std::string> warnings;
    if (!load_movement_params(params_path, m_local.params, warnings, error)) return false;
    if (!load_combat_beside(params_path, m_local.combat, error)) return false;
    m_local.collision.build(map);
    m_local.params_hash = combined_params_hash(m_local.params, m_local.combat);

    m_local.players.clear();
    PlayerState p;
    p.pos = Vec3(-16.0f + static_cast<float>(player_index) * 2.0f, 0.0f, 0.05f);
    p.impulses = m_local.params.max_impulses;
    m_local.players.push_back(p);
    m_local.tick = 0;

    // Plan 7.7: wait for FULL_STATE instead of assuming both sides start at
    // tick 0. Cold start, mid-match join and reconnect all use the same path.
    m_joined = false;
    return true;
}

int NetClient::snapshot_age() const {
    if (!m_joined) return 0;
    return m_local.tick > m_last_snapshot_tick
               ? static_cast<int>(m_local.tick - m_last_snapshot_tick)
               : 0;
}

const PlayerState& NetClient::predicted() const { return m_local.players[0]; }

void NetClient::reset_stats() {
    m_rollbacks = 0;
    m_snapshots = 0;
    m_decode_fail = 0;
    m_stale = 0;
    m_corrections = 0;
    m_max_correction = 0.0f;
}

void NetClient::apply_full_state(const Snapshot& snap) {
    ++m_full_states;
    m_join_tick = snap.tick;
    m_join_failed = false;
    m_ahead_min = 1000000;
    m_skip_cooldown = 0;
    m_skip_next = false;

    // Everything predicted before the join/reconnect refers to a different
    // history; drop it rather than trying to reconcile it.
    m_history.clear();
    m_recent.clear();
    m_pending.clear();
    m_last_snapshot_tick = snap.tick;
    m_ack = 0;
    m_ack_bits = 0;

    m_remote.reset();
    m_remote.push(snap);

    m_local.players.resize(1);
    from_snap(snap.players[static_cast<size_t>(m_player)], m_local.players[0]);
    m_local.tick = snap.tick;

    // Plan 7.0/7.7: start on the server tick axis one lead ahead, mirroring the
    // empty ticks the server runs while our first command is in flight.
    // The snapshot is already up to three ticks stale when it arrives. Estimate
    // the server's current tick before adding the normal command lead; the extra
    // slack only makes the first command arrive early, never late.
    constexpr uint32_t kJoinSlack = 3;
    m_clock.align(snap.tick + kJoinSlack);
    m_hitbox.reset();
    m_hitbox.record(m_local.players[0], m_local.params, snap.tick);
    while (m_local.tick < m_clock.command_tick()) {
        const MoveInput none;
        m_local.step({none}, kDt);
        HistoryEntry entry;
        entry.tick = m_local.tick;
        entry.state = m_local.players[0];
        entry.input = none;
        m_history.push_back(entry);
        m_hitbox.record(m_local.players[0], m_local.params, m_local.tick);
    }
    m_joined = true;
}

void NetClient::send_input(const MoveInput& input) {
    if (!m_joined) return;  // no tick axis until FULL_STATE arrives
    if (m_skip_next) return;  // clock correction: hold this loop, send nothing
    m_last_input = input;
    InputCmd cmd;
    cmd.tick = m_clock.command_tick();
    cmd.input = input;
    m_pending.push_back(cmd);
    while (m_pending.size() > kInputRedundancy) m_pending.pop_front();

    PacketHeader header;
    header.type = PacketType::Command;
    header.seq = m_seq++;
    header.ack = m_ack;
    header.ack_bits = m_ack_bits;
    m_tx->send(wrap_packet(header, encode_commands(m_pending)));
}

void NetClient::step_local() {
    if (!m_joined) return;
    if (m_skip_next) {
        // Plan 7.0: when the client has drifted ahead, skip one loop. The world
        // simply does not advance this iteration; commands stay labelled by
        // their own tick, so there is no gap and no history repair.
        m_skip_next = false;
        return;
    }
    if (m_skip_cooldown > 0) --m_skip_cooldown;
    // Prediction uses exactly the input that was just sent, on the server's tick
    // axis; the clock advances with the world so the two never drift apart.
    m_local.step({m_last_input}, kDt);
    m_clock.advance();
    HistoryEntry entry;
    entry.tick = m_local.tick;
    entry.state = m_local.players[0];
    entry.input = m_last_input;
    m_history.push_back(entry);
    m_hitbox.record(m_local.players[0], m_local.params, m_local.tick);
    if (m_history.size() > 128) m_history.erase(m_history.begin());
}

void NetClient::receive() {
    if (m_rel != nullptr) {
        std::vector<uint8_t> piece;
        while (m_rel->recv_reliable(piece)) {
            std::string error;
            m_config.add(piece.data(), piece.size(), error);
        }
        m_rel->advance(m_local.tick);
        if (!m_config_checked && m_config.complete()) {
            m_config_checked = true;
            ConfigBlob blob;
            if (!decode_config(m_config.assemble(), blob) || blob.version != kConfigVersion ||
                blob.params_hash != m_local.params_hash ||
                blob.geometry_hash != m_local.collision.geometry_hash()) {
                // v0.4 3.5: never run half a configuration, and never run a
                // different one. Refusing beats a silent divergence.
                m_config_bad = true;
                m_join_failed = true;
            }
        }
    }
    std::vector<uint8_t> packet;
    while (m_rx->recv(packet)) {
        PacketHeader header;
        const uint8_t* payload = nullptr;
        size_t size = 0;
        if (!unwrap_packet(packet, header, payload, size)) continue;

        if (header.type == PacketType::FullState) {
            constexpr size_t kFullHeader = 4 + 1 + 8 + 8;
            if (size < kFullHeader) continue;
            ByteReader r(payload, size);
            const uint32_t join_id = r.u32();
            const uint8_t player_index = r.u8();
            const uint64_t params_hash = r.u64();
            const uint64_t geometry_hash = r.u64();
            if (!r.ok() || params_hash != m_local.params_hash ||
                geometry_hash != m_local.collision.geometry_hash()) {
                m_join_failed = true;  // wrong build or map: refuse to join
                continue;
            }
            Snapshot full;
            if (!decode_snapshot(payload + kFullHeader, size - kFullHeader, nullptr, full)) {
                ++m_decode_fail;
                continue;
            }
            if (player_index >= full.players.size()) {
                m_join_failed = true;
                continue;
            }
            // The server repeats the same full state every tick until our first
            // command lands; a new join_id means a reconnect.
            if (m_joined && join_id == m_join_id) continue;
            m_join_id = join_id;
            m_player = player_index;
            apply_full_state(full);
            continue;
        }

        if (header.type != PacketType::Snapshot) continue;
        if (!m_joined) continue;

        Snapshot snap;
        const uint32_t baseline_tick = peek_baseline_tick(payload, size);
        const Snapshot* baseline = nullptr;
        if (baseline_tick != 0) {
            const auto it = m_recent.find(baseline_tick);
            if (it == m_recent.end()) {
                ++m_decode_fail;  // baseline already trimmed; server will go full
                continue;
            }
            baseline = &it->second;
        }
        if (!decode_snapshot(payload, size, baseline, snap)) {
            ++m_decode_fail;
            continue;
        }

        if (snap.tick < m_last_snapshot_tick) {
            ++m_stale;  // reordered older snapshot
            continue;
        }
        m_snapshots++;
        m_ack = header.seq;
        m_ack_bits = 0;
        m_last_snapshot_tick = snap.tick;
        m_recent[snap.tick] = snap;
        while (m_recent.size() > 64) m_recent.erase(m_recent.begin());
        m_remote.push(snap);

        if (m_local.tick > snap.tick) {
            const int ahead = static_cast<int>(m_local.tick - snap.tick);
            if (ahead < m_ahead_min) m_ahead_min = ahead;
            if (ahead > m_ahead_min + 3 && m_skip_cooldown == 0) {
                m_skip_next = true;
                m_skip_cooldown = 32;
                ++m_clock_skips;
            }
        }

        if (m_player < 0 || m_player >= static_cast<int>(snap.players.size())) continue;
        apply_snapshot(snap);
    }
}

void NetClient::apply_snapshot(const Snapshot& snap) {
    const PlayerSnap& auth = snap.players[static_cast<size_t>(m_player)];

    // Find the predicted state for this tick.
    size_t index = m_history.size();
    for (size_t i = 0; i < m_history.size(); ++i) {
        if (m_history[i].tick == snap.tick) { index = i; break; }
    }
    if (index == m_history.size()) {
        // Snapshots older than the first unconfirmed prediction are already
        // applied; rewinding to them would teleport the view backwards. Only a
        // forward gap (mid-match join) is allowed to snap.
        if (!m_history.empty() && snap.tick < m_history.front().tick) return;
        from_snap(auth, m_local.players[0]);
        m_hitbox.record(m_local.players[0], m_local.params, snap.tick);
        return;
    }

    if (same_state(auth, m_history[index].state)) {
        m_history.erase(m_history.begin(), m_history.begin() + static_cast<long>(index));
        return;
    }

    // Rollback: adopt the authoritative state and replay every later input.
    ++m_rollbacks;
    const float deviation = length(auth.pos - m_history[index].state.pos);
    if (deviation > 1e-4f) {
        ++m_corrections;  // a visible position correction (v0.4 N3)
        if (deviation > m_max_correction) m_max_correction = deviation;
    }
    PlayerState corrected;
    from_snap(auth, corrected);
    m_local.players[0] = corrected;
    m_local.tick = snap.tick;

    for (size_t i = index + 1; i < m_history.size(); ++i) {
        m_local.step({m_history[i].input}, kDt);
        m_history[i].state = m_local.players[0];
        m_hitbox.record(m_local.players[0], m_local.params, m_local.tick);
    }
    m_history.erase(m_history.begin(), m_history.begin() + static_cast<long>(index));
}

}  // namespace sim
