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
    FILE* m_file = nullptr;
    DemoHeader m_header;
    std::vector<DemoTick> m_ticks;
    std::string m_path;
};

class DemoReader {
public:
    bool open(const std::string& path, DemoHeader& header, std::string& error);
    bool next(DemoTick& tick, bool& has_tick, std::string& error);

private:
    FILE* m_file = nullptr;
    DemoHeader m_header;
};

// Human readable summary used by --play and tests.
std::string demo_mismatch_reason(const DemoHeader& demo, uint32_t sim_version,
                                 uint32_t protocol_version, uint64_t params_hash,
                                 uint64_t geometry_hash);

}  // namespace sim
