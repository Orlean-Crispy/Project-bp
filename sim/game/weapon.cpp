#include "sim/game/weapon.h"

#include <cstddef>
#include <utility>

#include "sim/core/hash.h"
#include "sim/data/config.h"

namespace sim {
namespace {

constexpr const char* kWeaponPrefix = "weapon.";
constexpr std::size_t kWeaponPrefixLen = sizeof("weapon.") - 1;

// Section ids in file order. The table is flat ("weapon.rifle.rpm"), so the id
// is whatever sits between the prefix and the next dot.
std::vector<std::string> section_ids(const TomlTable& table) {
    std::vector<std::string> ids;
    for (const auto& entry : table.entries) {
        if (entry.first.compare(0, kWeaponPrefixLen, kWeaponPrefix) != 0) continue;
        const std::string rest = entry.first.substr(kWeaponPrefixLen);
        const std::size_t dot = rest.find('.');
        if (dot == std::string::npos || dot == 0) continue;
        const std::string id = rest.substr(0, dot);
        bool seen = false;
        for (const auto& known : ids) {
            if (known == id) {
                seen = true;
                break;
            }
        }
        if (!seen) ids.push_back(id);
    }
    return ids;
}

// Rejects values the simulation cannot use. Everything here would otherwise turn
// into a divide by zero, an infinite magazine or healing through a wall.
bool validate(const WeaponDef& def, std::string& error) {
    const std::string who = std::string(kWeaponPrefix) + def.id + ": ";
    auto fail = [&](const char* what) {
        error = who + what;
        return false;
    };
    if (!(def.damage_near > 0.0f)) return fail("damage_near must be > 0");
    if (def.damage_far < 0.0f || def.damage_far > def.damage_near) {
        return fail("damage_far must be in [0, damage_near]");
    }
    if (def.falloff_start < 0.0f || def.falloff_end <= def.falloff_start) {
        return fail("falloff_end must be beyond falloff_start");
    }
    if (!(def.rpm > 0.0f)) return fail("rpm must be > 0");
    if (!(def.range > 0.0f)) return fail("range must be > 0");
    if (def.magazine < 1) return fail("magazine must be at least 1");
    if (def.reserve_ammo < 0) return fail("reserve_ammo cannot be negative");
    if (def.reload_seconds < 0.0f) return fail("reload_seconds cannot be negative");
    if (def.penetration_count < 0) return fail("penetration_count cannot be negative");
    if (!(def.penetration_damage_scale > 0.0f) || def.penetration_damage_scale > 1.0f) {
        return fail("penetration_damage_scale must be in (0, 1]");
    }
    return true;
}

}  // namespace

const WeaponDef* WeaponParams::find(const std::string& id) const {
    for (const WeaponDef& def : weapons) {
        if (def.id == id) return &def;
    }
    return nullptr;
}

uint64_t WeaponParams::hash() const {
    Hasher h;
    h.u32(static_cast<uint32_t>(weapons.size()));
    for (const WeaponDef& def : weapons) {
        h.str(def.id);
        h.f32(def.damage_near).f32(def.damage_far);
        h.f32(def.falloff_start).f32(def.falloff_end);
        h.f32(def.rpm).f32(def.range);
        h.u32(static_cast<uint32_t>(def.magazine))
            .u32(static_cast<uint32_t>(def.reserve_ammo))
            .f32(def.reload_seconds);
        h.u32(static_cast<uint32_t>(def.penetration_count))
            .f32(def.penetration_damage_scale);
    }
    return h.value();
}

bool load_weapon_params(const std::string& path, WeaponParams& out,
                        std::vector<std::string>& warnings, std::string& error) {
    TomlTable table;
    if (!load_toml_file(path, table, error)) return false;

    WeaponParams params;
    TomlBinder binder{table, warnings};

    for (const std::string& id : section_ids(table)) {
        const std::string prefix = std::string(kWeaponPrefix) + id + ".";
        WeaponDef def;
        def.id = id;

        // A weapon is a complete definition, not a set of overrides: a missing
        // key is a mistake in the file even though the default still applies.
        auto number = [&](const char* field, float& target) {
            const std::string key = prefix + field;
            if (!table.find(key)) {
                warnings.push_back(key + ": missing, kept default");
                return;
            }
            binder.number(key, target);
        };
        auto integer = [&](const char* field, int& target) {
            const std::string key = prefix + field;
            if (!table.find(key)) {
                warnings.push_back(key + ": missing, kept default");
                return;
            }
            binder.integer(key, target);
        };

        number("damage_near", def.damage_near);
        number("damage_far", def.damage_far);
        number("falloff_start", def.falloff_start);
        number("falloff_end", def.falloff_end);
        number("rpm", def.rpm);
        number("range", def.range);
        integer("magazine", def.magazine);
        integer("reserve_ammo", def.reserve_ammo);
        number("reload_seconds", def.reload_seconds);
        integer("penetration_count", def.penetration_count);
        number("penetration_damage_scale", def.penetration_damage_scale);

        if (!validate(def, error)) return false;
        params.weapons.push_back(std::move(def));
    }

    if (params.weapons.empty()) {
        error = "no [weapon.<id>] sections in " + path;
        return false;
    }

    binder.report_unknown("");

    out = std::move(params);
    return true;
}

}  // namespace sim
