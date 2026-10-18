#include "sim/core/world.h"

#include "sim/core/hash.h"

namespace sim {

void World::step(const std::vector<MoveInput>& inputs, float dt) {
    const size_t count = players.size() < inputs.size() ? players.size() : inputs.size();
    for (size_t i = 0; i < count; ++i) {
        pmove(players[i], inputs[i], collision, params, dt);
    }
    ++tick;
}

uint64_t World::state_hash() const {
    Hasher h;
    h.u32(tick);
    h.u64(params_hash);
    h.u64(collision.geometry_hash());
    h.u32(static_cast<uint32_t>(players.size()));
    for (const PlayerState& p : players) {
        h.vec3(p.pos).vec3(p.vel);
        h.u32(p.on_ground ? 1u : 0u).u32(p.crouched ? 1u : 0u);
        h.u32(static_cast<uint32_t>(p.impulses));
        h.f32(p.speed_coefficient).f32(p.slow_factor);
    }
    return h.value();
}

}  // namespace sim
