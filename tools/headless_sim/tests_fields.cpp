#include "tests_fields.h"

#include <cstdio>
#include <string>

#include "options.h"
#include "sim/core/fields.h"
#include "sim/core/world.h"
#include "test_util.h"

int run_fields_test(const Options& opt) {
    sim::World world;
    std::string error;
    if (!load_world(opt, world, error)) {
        std::printf("FAIL: fields world: %s\n", error.c_str());
        return 1;
    }
    if (world.players.empty()) {
        std::printf("FAIL: fields world has no players\n");
        return 1;
    }

    int failures = 0;
    auto check = [&](const char* label, bool ok) {
        if (!ok) ++failures;
        std::printf("  %-58s %s\n", label, ok ? "ok" : "FAIL");
    };

    const sim::PlayerState base = world.players[0];
    const uint64_t base_hash = world.state_hash();
    check("the same state hashes the same twice", world.state_hash() == base_hash);

    std::size_t count = 0;
    const sim::FieldProbe* probes = sim::player_field_probes(count);
    int hashed = 0;
    int excluded = 0;
    for (std::size_t i = 0; i < count; ++i) {
        world.players[0] = base;
        probes[i].mutate(world.players[0]);
        const bool changed = world.state_hash() != base_hash;
        if (probes[i].hashed) {
            ++hashed;
            if (!changed) {
                std::printf("FAIL: %s is authoritative but not in state_hash\n", probes[i].name);
                ++failures;
            }
        } else {
            ++excluded;
            if (changed) {
                std::printf("FAIL: %s must not be hashed (it is derived or presentation-only)\n",
                            probes[i].name);
                ++failures;
            }
        }
    }
    world.players[0] = base;

    std::printf("  fields: %zu registered, %d must hash, %d must not\n", count, hashed, excluded);
    check("every authoritative field is covered", failures == 0);
    std::printf("    sizeof(PlayerState)=%zu sizeof(PlayerVitals)=%zu\n",
                sizeof(sim::PlayerState), sizeof(sim::PlayerVitals));

    if (failures != 0) {
        std::printf("FAIL: %d field checks failed\n", failures);
        return 1;
    }
    std::printf("PASS: every authoritative field is hashed, and the excluded ones are not\n");
    return 0;
}
