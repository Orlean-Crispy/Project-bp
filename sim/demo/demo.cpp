#include "sim/demo/demo.h"

#include <cstring>
#include <filesystem>

#include "sim/core/version.h"

namespace sim {
namespace {

constexpr char kMagic[4] = {'B', 'P', 'D', 'M'};

enum InputFlag : uint16_t {
    kFlagJump = 1u << 0,
    kFlagCrouch = 1u << 1,
    kFlagSlide = 1u << 2,
    kFlagDash = 1u << 3,
    kFlagWallRun = 1u << 4,
    kFlagWallClimb = 1u << 5,
    kFlagVault = 1u << 6,
};

uint16_t encode_input(const MoveInput& in) {
    uint16_t flags = 0;
    if (in.jump) flags |= kFlagJump;
    if (in.crouch) flags |= kFlagCrouch;
    if (in.slide) flags |= kFlagSlide;
    if (in.dash) flags |= kFlagDash;
    if (in.wallrun) flags |= kFlagWallRun;
    if (in.wallclimb) flags |= kFlagWallClimb;
    if (in.vault) flags |= kFlagVault;
    return flags;
}

MoveInput decode_input(float forward, float right, float yaw, uint16_t flags) {
    MoveInput in;
    in.forward = forward;
    in.right = right;
    in.yaw = yaw;
    in.jump = (flags & kFlagJump) != 0;
    in.crouch = (flags & kFlagCrouch) != 0;
    in.slide = (flags & kFlagSlide) != 0;
    in.dash = (flags & kFlagDash) != 0;
    in.wallrun = (flags & kFlagWallRun) != 0;
    in.wallclimb = (flags & kFlagWallClimb) != 0;
    in.vault = (flags & kFlagVault) != 0;
    return in;
}

// AGENTS 2.5: the C runtime decodes a path as ANSI, so a UTF-8 path (from
// Godot, or from this non-ASCII workspace) opens the wrong file. u8path does the
// one conversion to the native wide name that _wfopen expects.
FILE* open_u8(const std::string& path, const wchar_t* mode) {
    return _wfopen(std::filesystem::u8path(path).c_str(), mode);
}

bool put_u16(FILE* f, uint16_t v) { return std::fwrite(&v, sizeof(v), 1, f) == 1; }
bool put_u32(FILE* f, uint32_t v) { return std::fwrite(&v, sizeof(v), 1, f) == 1; }
bool put_u64(FILE* f, uint64_t v) { return std::fwrite(&v, sizeof(v), 1, f) == 1; }
bool put_f32(FILE* f, float v) { return std::fwrite(&v, sizeof(v), 1, f) == 1; }
bool put_u8(FILE* f, uint8_t v) { return std::fwrite(&v, sizeof(v), 1, f) == 1; }

bool put_string(FILE* f, const std::string& s) {
    const uint16_t len = static_cast<uint16_t>(s.size() > 0xFFFF ? 0xFFFF : s.size());
    if (!put_u16(f, len)) return false;
    return len == 0 || std::fwrite(s.data(), 1, len, f) == len;
}

bool get_u16(FILE* f, uint16_t& v) { return std::fread(&v, sizeof(v), 1, f) == 1; }
bool get_u32(FILE* f, uint32_t& v) { return std::fread(&v, sizeof(v), 1, f) == 1; }
bool get_u64(FILE* f, uint64_t& v) { return std::fread(&v, sizeof(v), 1, f) == 1; }
bool get_f32(FILE* f, float& v) { return std::fread(&v, sizeof(v), 1, f) == 1; }
bool get_u8(FILE* f, uint8_t& v) { return std::fread(&v, sizeof(v), 1, f) == 1; }

bool get_string(FILE* f, std::string& s) {
    uint16_t len = 0;
    if (!get_u16(f, len)) return false;
    s.assign(len, '\0');
    if (len == 0) return true;
    return std::fread(&s[0], 1, len, f) == len;
}

}  // namespace

DemoWriter::~DemoWriter() {
    if (m_file) std::fclose(m_file);
}

bool DemoWriter::open(const std::string& path, const DemoHeader& header, std::string& error) {
    FILE* probe = open_u8(path, L"wb");
    if (!probe) {
        error = "cannot write " + path;
        return false;
    }
    std::fclose(probe);
    m_path = path;
    m_header = header;
    m_ticks.clear();
    return true;
}

bool DemoWriter::add(const DemoTick& tick) {
    m_ticks.push_back(tick);
    return true;
}

bool DemoWriter::close(std::string& error) {
    FILE* f = open_u8(m_path, L"wb");
    if (!f) {
        error = "cannot write " + m_path;
        return false;
    }

    m_header.tick_count = static_cast<uint32_t>(m_ticks.size());

    bool ok = std::fwrite(kMagic, 1, sizeof(kMagic), f) == sizeof(kMagic);
    ok = ok && put_u16(f, static_cast<uint16_t>(kDemoFormatVersion));
    ok = ok && put_u16(f, 0);
    ok = ok && put_u32(f, m_header.sim_version);
    ok = ok && put_u32(f, m_header.protocol_version);
    ok = ok && put_u32(f, m_header.tick_rate);
    ok = ok && put_u64(f, m_header.params_hash);
    ok = ok && put_u64(f, m_header.geometry_hash);
    ok = ok && put_string(f, m_header.build_id);
    ok = ok && put_string(f, m_header.map);
    ok = ok && put_u16(f, static_cast<uint16_t>(m_header.players.size()));
    for (const DemoPlayerInit& p : m_header.players) {
        ok = ok && put_f32(f, p.pos.x) && put_f32(f, p.pos.y) && put_f32(f, p.pos.z);
        ok = ok && put_f32(f, p.vel.x) && put_f32(f, p.vel.y) && put_f32(f, p.vel.z);
        ok = ok && put_f32(f, p.speed_coefficient) && put_f32(f, p.slow_factor);
        ok = ok && put_u8(f, p.crouched ? 1 : 0);
        ok = ok && put_u8(f, static_cast<uint8_t>(p.impulses));
        ok = ok && put_u16(f, 0);
    }
    ok = ok && put_u32(f, m_header.tick_count);

    for (const DemoTick& t : m_ticks) {
        ok = ok && put_u32(f, t.tick);
        ok = ok && put_u64(f, t.state_hash);
        ok = ok && put_u16(f, static_cast<uint16_t>(t.inputs.size()));
        for (const MoveInput& in : t.inputs) {
            ok = ok && put_f32(f, in.forward) && put_f32(f, in.right) && put_f32(f, in.yaw);
            ok = ok && put_u16(f, encode_input(in));
        }
    }

    if (std::fclose(f) != 0) ok = false;
    if (!ok) {
        error = "failed while writing " + m_path;
        return false;
    }
    return true;
}

bool DemoReader::open(const std::string& path, DemoHeader& header, std::string& error) {
    FILE* f = open_u8(path, L"rb");
    if (!f) {
        error = "cannot open " + path;
        return false;
    }

    char magic[4] = {0, 0, 0, 0};
    if (std::fread(magic, 1, sizeof(magic), f) != sizeof(magic) ||
        std::memcmp(magic, kMagic, sizeof(magic)) != 0) {
        std::fclose(f);
        error = path + ": not a Breakpoint demo";
        return false;
    }

    uint16_t format = 0;
    uint16_t reserved = 0;
    if (!get_u16(f, format) || !get_u16(f, reserved)) {
        std::fclose(f);
        error = path + ": truncated header";
        return false;
    }
    if (format != kDemoFormatVersion) {
        std::fclose(f);
        error = path + ": unsupported demo format " + std::to_string(format);
        return false;
    }

    DemoHeader h;
    if (!get_u32(f, h.sim_version) || !get_u32(f, h.protocol_version) ||
        !get_u32(f, h.tick_rate) || !get_u64(f, h.params_hash) ||
        !get_u64(f, h.geometry_hash) || !get_string(f, h.build_id) || !get_string(f, h.map)) {
        std::fclose(f);
        error = path + ": truncated header";
        return false;
    }

    uint16_t player_count = 0;
    if (!get_u16(f, player_count) || player_count > 64) {
        std::fclose(f);
        error = path + ": bad player count";
        return false;
    }
    h.players.resize(player_count);
    for (DemoPlayerInit& p : h.players) {
        uint8_t crouched = 0;
        uint8_t impulses = 0;
        uint16_t pad = 0;
        bool ok = get_f32(f, p.pos.x) && get_f32(f, p.pos.y) && get_f32(f, p.pos.z);
        ok = ok && get_f32(f, p.vel.x) && get_f32(f, p.vel.y) && get_f32(f, p.vel.z);
        ok = ok && get_f32(f, p.speed_coefficient) && get_f32(f, p.slow_factor);
        ok = ok && get_u8(f, crouched) && get_u8(f, impulses) && get_u16(f, pad);
        if (!ok) {
            std::fclose(f);
            error = path + ": truncated player table";
            return false;
        }
        p.crouched = crouched != 0;
        p.impulses = impulses;
    }

    if (!get_u32(f, h.tick_count)) {
        std::fclose(f);
        error = path + ": truncated tick count";
        return false;
    }

    m_file = f;
    m_header = h;
    header = h;
    return true;
}

bool DemoReader::next(DemoTick& tick, bool& has_tick, std::string& error) {
    has_tick = false;
    if (!m_file) {
        error = "reader is not open";
        return false;
    }

    DemoTick t;
    uint32_t tick_index = 0;
    uint64_t hash = 0;
    uint16_t count = 0;
    if (!get_u32(m_file, tick_index)) return true;  // clean end of file
    if (!get_u64(m_file, hash) || !get_u16(m_file, count) || count > 64) {
        error = "truncated tick record";
        return false;
    }
    t.tick = tick_index;
    t.state_hash = hash;
    t.inputs.resize(count);
    for (MoveInput& in : t.inputs) {
        float forward = 0.0f;
        float right = 0.0f;
        float yaw = 0.0f;
        uint16_t flags = 0;
        if (!get_f32(m_file, forward) || !get_f32(m_file, right) || !get_f32(m_file, yaw) ||
            !get_u16(m_file, flags)) {
            error = "truncated input record";
            return false;
        }
        in = decode_input(forward, right, yaw, flags);
    }
    tick = t;
    has_tick = true;
    return true;
}

std::string demo_mismatch_reason(const DemoHeader& demo, uint32_t sim_version,
                                 uint32_t protocol_version, uint64_t params_hash,
                                 uint64_t geometry_hash) {
    std::string reason;
    if (demo.sim_version != sim_version) {
        reason += "sim_version " + std::to_string(demo.sim_version) + " != " +
                  std::to_string(sim_version) + "; ";
    }
    if (demo.protocol_version != protocol_version) {
        reason += "protocol_version differs; ";
    }
    if (demo.params_hash != params_hash) {
        reason += "movement.toml differs (params_hash); ";
    }
    if (demo.geometry_hash != geometry_hash) {
        reason += "map geometry differs; ";
    }
    if (!reason.empty()) reason.resize(reason.size() - 2);
    return reason;
}

}  // namespace sim
