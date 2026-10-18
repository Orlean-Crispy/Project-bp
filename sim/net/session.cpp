#include "sim/net/session.h"

#include "sim/net/bytes.h"
#include "sim/net/netchan.h"

namespace sim {
namespace {

constexpr float kDt = 1.0f / 64.0f;

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
    return a.pos == bs.pos && a.vel == bs.vel && a.flags == bs.flags &&
           a.impulses == bs.impulses && a.state == bs.state;
}

}  // namespace

void ServerClock::sample(uint32_t server_tick, uint32_t local_tick, float rtt_seconds) {
    // Plan 7.0: serverTime ~= snapshot tick + RTT/2, smoothed, never jumped.
    const float measured = static_cast<float>(server_tick) +
                           rtt_seconds * 32.0f - static_cast<float>(local_tick);
    if (!m_initialized) {
        m_offset = measured;
        m_initialized = true;
    } else {
        m_offset += 0.1f * (measured - m_offset);
    }
}

uint32_t ServerClock::server_tick_for(uint32_t local_tick) const {
    const float value = static_cast<float>(local_tick) + m_offset;
    return value <= 0.0f ? 0u : static_cast<uint32_t>(value + 0.5f);
}

bool NetServer::init(const std::string& map_path, const std::string& params_path,
                     int player_count, std::string& error) {
    MapData map;
    if (!parse_map_file(map_path, map, error)) return false;
    std::vector<std::string> warnings;
    if (!load_movement_params(params_path, m_world.params, warnings, error)) return false;
    m_world.collision.build(map);
    m_world.params_hash = m_world.params.hash();

    m_world.players.clear();
    for (int i = 0; i < player_count; ++i) {
        PlayerState p;
        p.pos = Vec3(-16.0f + static_cast<float>(i) * 2.0f, 0.0f, 0.05f);
        p.impulses = m_world.params.max_impulses;
        m_world.players.push_back(p);
    }
    m_world.tick = 0;
    return true;
}

int NetServer::add_client(Transport* incoming, Transport* outgoing, int player_index) {
    Link link;
    link.rx = incoming;
    link.tx = outgoing;
    link.player = player_index;
    m_links.push_back(link);
    return static_cast<int>(m_links.size()) - 1;
}

void NetServer::receive() {
    for (Link& link : m_links) {
        std::vector<uint8_t> packet;
        while (link.rx->recv(packet)) {
            PacketHeader header;
            const uint8_t* payload = nullptr;
            size_t size = 0;
            if (!unwrap_packet(packet, header, payload, size)) continue;
            if (header.type != PacketType::Command) continue;

            std::vector<InputCmd> cmds;
            if (!decode_commands(payload, size, cmds)) continue;

            // Acknowledge the snapshot the client is using as its baseline.
            if (header.ack != 0) {
                const auto it = link.sent.find(header.ack);
                if (it != link.sent.end()) {
                    link.baseline = it->second;
                    link.has_baseline = true;
                    link.unacked = 0;
                    link.last_acked_snapshot = header.ack;
                }
            }

            for (const InputCmd& cmd : cmds) {
                link.has_command = true;
                link.last_cmd_seq = header.seq;
                if (cmd.tick < m_world.tick) {
                    // Arrived after its tick (latency): apply it as the newest
                    // input instead of throwing it away, otherwise the server
                    // would stand still under jitter.
                    link.last_input = cmd.input;
                    ++m_dropped_cmds;
                    continue;
                }
                link.last_cmd_tick = cmd.tick;
                link.inputs[cmd.tick] = cmd.input;
            }
            // Plan 7.9: the server buffer targets ~2 ticks; keep it bounded.
            while (link.inputs.size() > 8) link.inputs.erase(link.inputs.begin());
        }
    }
}

void NetServer::step() {
    std::vector<MoveInput> inputs(m_world.players.size());
    for (Link& link : m_links) {
        const auto it = link.inputs.find(m_world.tick);
        if (it != link.inputs.end()) {
            link.last_input = it->second;
            link.inputs.erase(it);
        }
        // If no command arrived for this tick, repeat the last one (Q3 behaviour).
        if (link.player >= 0 && link.player < static_cast<int>(inputs.size())) {
            inputs[static_cast<size_t>(link.player)] = link.has_command ? link.last_input : MoveInput{};
        }
    }
    m_world.step(inputs, kDt);
}

void NetServer::send() {
    for (Link& link : m_links) {
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

        link.sent[header.seq] = snap;
        if (link.sent.size() > 64) link.sent.erase(link.sent.begin());

        ++link.unacked;
        if (use_delta) ++m_delta; else ++m_full;
    }
}

bool NetClient::init(Transport* outgoing, Transport* incoming, int player_index,
                     const std::string& map_path, const std::string& params_path,
                     std::string& error) {
    m_tx = outgoing;
    m_rx = incoming;
    m_player = player_index;

    MapData map;
    if (!parse_map_file(map_path, map, error)) return false;
    std::vector<std::string> warnings;
    if (!load_movement_params(params_path, m_local.params, warnings, error)) return false;
    m_local.collision.build(map);
    m_local.params_hash = m_local.params.hash();

    m_local.players.clear();
    PlayerState p;
    p.pos = Vec3(-16.0f + static_cast<float>(player_index) * 2.0f, 0.0f, 0.05f);
    p.impulses = m_local.params.max_impulses;
    m_local.players.push_back(p);
    m_local.tick = 0;
    return true;
}

const PlayerState& NetClient::predicted() const { return m_local.players[0]; }

void NetClient::send_input(const MoveInput& input) {
    m_last_input = input;
    InputCmd cmd;
    cmd.tick = m_local.tick;
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
    // Prediction uses exactly the input that was just sent.
    m_local.step({m_last_input}, kDt);
    HistoryEntry entry;
    entry.tick = m_local.tick;
    entry.state = m_local.players[0];
    entry.input = m_last_input;
    m_history.push_back(entry);
    if (m_history.size() > 128) m_history.erase(m_history.begin());
}

void NetClient::receive() {
    std::vector<uint8_t> packet;
    while (m_rx->recv(packet)) {
        PacketHeader header;
        const uint8_t* payload = nullptr;
        size_t size = 0;
        if (!unwrap_packet(packet, header, payload, size)) continue;
        if (header.type != PacketType::Snapshot) continue;

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
        // No matching prediction (older than our buffer or a fresh join): snap.
        from_snap(auth, m_local.players[0]);
        return;
    }

    if (same_state(auth, m_history[index].state)) {
        m_history.erase(m_history.begin(), m_history.begin() + static_cast<long>(index));
        return;
    }

    // Rollback: adopt the authoritative state and replay every later input.
    ++m_rollbacks;
    PlayerState corrected;
    from_snap(auth, corrected);
    m_local.players[0] = corrected;
    m_local.tick = snap.tick;

    for (size_t i = index + 1; i < m_history.size(); ++i) {
        m_local.step({m_history[i].input}, kDt);
        m_history[i].state = m_local.players[0];
    }
    m_history.erase(m_history.begin(), m_history.begin() + static_cast<long>(index));
}

}  // namespace sim
