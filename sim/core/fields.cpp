#include "sim/core/fields.h"

namespace sim {
namespace {

const FieldProbe kProbes[] = {
    {"pos.x", [](PlayerState& p) { p.pos.x += 0.25f; }, true},
    {"pos.z", [](PlayerState& p) { p.pos.z += 0.25f; }, true},
    {"vel.y", [](PlayerState& p) { p.vel.y += 0.5f; }, true},
    {"yaw", [](PlayerState& p) { p.yaw += 0.1f; }, true},
    {"active", [](PlayerState& p) { p.active = !p.active; }, true},
    {"on_ground", [](PlayerState& p) { p.on_ground = !p.on_ground; }, true},
    {"crouched", [](PlayerState& p) { p.crouched = !p.crouched; }, true},
    {"impulses", [](PlayerState& p) { p.impulses += 1; }, true},
    {"recharge_progress", [](PlayerState& p) { p.recharge_progress += 1; }, true},
    {"state", [](PlayerState& p) { p.state = ParkourState::Slide; }, true},
    {"state_ticks", [](PlayerState& p) { p.state_ticks += 1; }, true},
    {"dash_cooldown", [](PlayerState& p) { p.dash_cooldown += 1; }, true},
    {"wall_normal", [](PlayerState& p) { p.wall_normal.x += 0.5f; }, true},
    {"jump_was_down", [](PlayerState& p) { p.jump_was_down = true; }, true},
    {"slide_was_down", [](PlayerState& p) { p.slide_was_down = true; }, true},
    {"dash_was_down", [](PlayerState& p) { p.dash_was_down = true; }, true},
    {"vault_was_down", [](PlayerState& p) { p.vault_was_down = true; }, true},
    {"firing", [](PlayerState& p) { p.firing = true; }, true},
    {"reload_held", [](PlayerState& p) { p.reload_held = true; }, true},
    {"ads", [](PlayerState& p) { p.ads = true; }, true},
    {"weapon_id", [](PlayerState& p) { p.weapon_id = 1; }, true},
    {"ammo", [](PlayerState& p) { p.ammo += 1; }, true},
    {"reserve", [](PlayerState& p) { p.reserve += 1; }, true},
    {"fire_cooldown", [](PlayerState& p) { p.fire_cooldown += 1; }, true},
    {"reload_left", [](PlayerState& p) { p.reload_left += 1; }, true},
    {"recoil_index", [](PlayerState& p) { p.recoil_index += 1; }, true},
    {"recoil_recovery", [](PlayerState& p) { p.recoil_recovery += 1; }, true},
    {"vault_dir", [](PlayerState& p) { p.vault_dir.z += 0.5f; }, true},
    {"vault_rise", [](PlayerState& p) { p.vault_rise += 0.1f; }, true},
    {"speed_coefficient", [](PlayerState& p) { p.speed_coefficient += 0.1f; }, true},
    {"slow_factor", [](PlayerState& p) { p.slow_factor += 0.1f; }, true},
    {"slow_ticks", [](PlayerState& p) { p.slow_ticks += 1; }, true},
    {"health", [](PlayerState& p) { p.vitals.health -= 1; }, true},
    {"armor", [](PlayerState& p) { p.vitals.armor += 1; }, true},
    // S85: these three were missing from state_hash (external review AUD-39).
    {"armor_type", [](PlayerState& p) { p.vitals.armor_type = ArmorType::Heavy; }, true},
    {"ticks_since_damage", [](PlayerState& p) { p.vitals.ticks_since_damage += 1; }, true},
    {"regen_accumulator", [](PlayerState& p) { p.vitals.regen_accumulator += 1; }, true},
    // Deliberately NOT hashed. Derived from (input, previous held); hashing it
    // would make two identical states differ.
    {"reload_edge", [](PlayerState& p) { p.reload_edge = true; }, false},
    // Presentation only: the hitmarker rides the snapshot, never the hash.
    {"hit_serial", [](PlayerState& p) { p.hit_serial = 7; }, false},
    {"hit_amount", [](PlayerState& p) { p.hit_amount = 42; }, false},
};

}  // namespace

const FieldProbe* player_field_probes(std::size_t& count) {
    count = sizeof(kProbes) / sizeof(kProbes[0]);
    return kProbes;
}

}  // namespace sim