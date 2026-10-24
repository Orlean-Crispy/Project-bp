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
    // Stable numeric id that goes on the wire and into save/demo data. Reordering
    // the file must not renumber anybody's gun, so it is explicit, not the index.
    int slot = 0;

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

    // M6.10: the spray pattern, one step per shot, in radians of view offset.
    // Shot 1 is straight; step k joins when the (k+1)-th round leaves the barrel,
    // and the offsets accumulate. A table, not a formula: "like CS" means
    // hand-authored, not procedural.
    std::vector<float> recoil_pitch;
    std::vector<float> recoil_yaw;
    // Silence for this many ticks and the spray resets to straight.
    int recoil_recovery_ticks = 32;
};

struct WeaponParams {
    std::vector<WeaponDef> weapons;  // file order; ids are unique

    const WeaponDef* find(const std::string& id) const;
    const WeaponDef* find_by_slot(int slot) const;

    // FNV-1a over the whole table, ids and order included. Kept separate from
    // the movement hash: tuning a gun must not look like a movement change.
    uint64_t hash() const;
};

// Applies a parsed file on top of the compiled defaults. A missing key warns and
// keeps the default; a value that cannot be simulated (rpm 0, empty magazine) is
// rejected here, at the one place the file is read.
bool load_weapon_params(const std::string& path, WeaponParams& out,
                        std::vector<std::string>& warnings, std::string& error);

// Ticks between shots and ticks to reload, from the data. Integer on purpose:
// the fire cadence is part of the simulation, so it cannot depend on rounding.
int fire_interval_ticks(const WeaponDef& def);
int reload_ticks(const WeaponDef& def);

// Damage before zone and penetration, linearly between the two falloff
// distances. Float only for the interpolation; the caller rounds once.
float damage_at_range(const WeaponDef& def, float distance);

// M6.10: the cumulative view offset after `shot` shots (0 = straight). The
// pattern is data, so this is only the accumulator; both sides run it and get
// the same floats.
struct RecoilOffset {
    float pitch = 0.0f;
    float yaw = 0.0f;
};
RecoilOffset recoil_offset(const WeaponDef& def, int shot);
int recoil_cap(const WeaponDef& def);

struct PlayerState;
// Gives a player this gun: id, a full magazine and a full reserve. Called when a
// slot joins and when the default kit is handed out.
void equip_weapon(PlayerState& player, const WeaponDef& def);

// weapons.toml sits next to movement.toml. A missing file leaves the table empty
// instead of failing: the movement gates and move_metrics run from data
// directories that have no weapons, and M6.3 refuses to fire without a weapon
// rather than inventing one.
bool load_weapons_beside(const std::string& movement_path, WeaponParams& out,
                         std::string& error);

}  // namespace sim
