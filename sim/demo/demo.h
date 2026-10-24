#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "sim/move/pmove.h"

namespace sim {

// Demo = inputs + seed + initial state, replayed tick by tick. Every tick stores
// the world hash too, so a replay can point at the exact tick where two builds
// diverge instead of just saying "something differs" (plan section 10.3 / B3).
struct DemoPlayerInit {
    Vec3 pos;
    Vec3 vel;
    float speed_coefficient = 1.0f;
    float slow_factor = 1.0f;
    bool crouched = false;
    int impulses = 4;
    // M6.7: the gun and its timers are part of the initial state. A replay that
    // starts with an empty magazine (or a full one when the demo was recorded
    // empty) diverges on tick 0 and blames the wrong thing.
    uint8_t weapon_id = 0;
    uint16_t ammo = 0;
    uint16_t reserve = 0;
    uint16_t fire_cooldown = 0;
    uint16_t reload_left = 0;
};

struct DemoHeader {
    std::string build_id;
    uint32_t sim_version = 0;
    uint32_t protocol_version = 0;
    uint32_t tick_rate = 64;
    uint64_t params_hash = 0;
    uint64_t geometry_hash = 0;
    std::string map;
    std::vector<DemoPlayerInit> players;
    uint32_t tick_count = 0;
};

struct DemoTick {
    uint32_t tick = 0;
    uint64_t state_hash = 0;
    std::vector<MoveInput> inputs;
};

class DemoWriter {
public:
    ~DemoWriter();

    bool open(const std::string& path, const DemoHeader& header, std::string& error);
    bool add(const DemoTick& tick);
    bool close(std::string& error);

    const DemoHeader& header() const { return m_header; }

private:
    DemoHeader m_header;
    std::vector<DemoTick> m_ticks;
    std::string m_path;
};

// Owns a FILE*: the previous version had neither a destructor nor a close(), so
// every --play leaked a handle and kept the file locked on Windows.
class DemoReader {
public:
    DemoReader() = default;
    ~DemoReader();
    DemoReader(const DemoReader&) = delete;
    DemoReader& operator=(const DemoReader&) = delete;

    bool open(const std::string& path, DemoHeader& header, std::string& error);
    // has_tick is false only at a clean end of file. A truncated record is an
    // error: treating a short read as EOF let a damaged demo replay as PASS.
    bool next(DemoTick& tick, bool& has_tick, std::string& error);
    void close();

private:
    FILE* m_file = nullptr;
    DemoHeader m_header;
};

// Human readable summary used by --play and tests.
std::string demo_mismatch_reason(const DemoHeader& demo, uint32_t sim_version,
                                 uint32_t protocol_version, uint64_t params_hash,
                                 uint64_t geometry_hash, const char* build_id);

}  // namespace sim
