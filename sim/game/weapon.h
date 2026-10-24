#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sim {

// One weapon's numbers (plan 6.2). The field set is staged on purpose (contract
// D5): only fields an M6 task consumes exist here, so no field sits unused
// waiting for a reader. Recoil joins at M6.10, ADS and tracers at M6.11.
struct WeaponDef {
    std::string id;  // section name: [weapon.<id>]

    // Damage (plan 6.2, the v0.2 addition): linear from damage_near down to
    // damage_far between falloff_start and falloff_end metres.
    float damage_near = 25.0f;
    float damage_far = 18.0f;
    float falloff_start = 25.0f;
    float falloff_end = 60.0f;

    float rpm = 600.0f;    // rounds per minute; the tick interval is derived by M6.7
    float range = 150.0f;  // metres; the shot ray stops here

    int magazine = 30;
    int reserve_ammo = 90;
    float reload_seconds = 1.8f;

    // ADR 0006 D1: the weapon decides whether a layer can be crossed, the map
    // can still veto it with _penetrable 0 (M6.5).
    int penetration_count = 0;
    float penetration_damage_scale = 0.7f;
};

struct WeaponParams {
    std::vector<WeaponDef> weapons;  // file order; ids are unique

    const WeaponDef* find(const std::string& id) const;

    // FNV-1a over the whole table, ids and order included. Kept separate from
    // the movement hash: tuning a gun must not look like a movement change.
    uint64_t hash() const;
};

// Applies a parsed file on top of the compiled defaults. A missing key warns and
// keeps the default; a value that cannot be simulated (rpm 0, empty magazine) is
// rejected here, at the one place the file is read.
bool load_weapon_params(const std::string& path, WeaponParams& out,
                        std::vector<std::string>& warnings, std::string& error);

}  // namespace sim
