#include "sim/game/weapon.h"

#include <cstddef>
#include <filesystem>
#include <utility>

#include "sim/core/hash.h"
#include "sim/core/tick.h"
#include "sim/data/config.h"
#include "sim/move/pmove.h"

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
    if (def.slot < 0 || def.slot > 63) return fail("slot must be in 0..63");
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

const WeaponDef* WeaponParams::find_by_slot(int slot) const {
    for (const WeaponDef& def : weapons) {
        if (def.slot == slot) return &def;
    }
    return nullptr;
}

int fire_interval_ticks(const WeaponDef& def) {
    if (!(def.rpm > 0.0f)) return 1;
    const float ticks = 60.0f * static_cast<float>(kDefaultTickRate) / def.rpm;
    const int rounded = static_cast<int>(ticks + 0.5f);
    return rounded > 0 ? rounded : 1;
}

int reload_ticks(const WeaponDef& def) {
    if (!(def.reload_seconds > 0.0f)) return 0;
    return static_cast<int>(def.reload_seconds * static_cast<float>(kDefaultTickRate) + 0.5f);
}

float damage_at_range(const WeaponDef& def, float distance) {
    if (distance <= def.falloff_start) return def.damage_near;
    if (distance >= def.falloff_end) return def.damage_far;
    const float span = def.falloff_end - def.falloff_start;
    if (!(span > 0.0f)) return def.damage_far;
    const float t = (distance - def.falloff_start) / span;
    return def.damage_near + (def.damage_far - def.damage_near) * t;
}

void equip_weapon(PlayerState& player, const WeaponDef& def) {
    player.weapon_id = static_cast<uint8_t>(def.slot & 0xFF);
    player.ammo = def.magazine;
    player.reserve = def.reserve_ammo;
    player.fire_cooldown = 0;
    player.reload_left = 0;
    player.recoil_index = 0;
    player.recoil_recovery = 0;
}

int recoil_cap(const WeaponDef& def) {
    return static_cast<int>(def.recoil_pitch.size());
}

RecoilOffset recoil_offset(const WeaponDef& def, int shot) {
    RecoilOffset out;
    const int cap = recoil_cap(def);
    const int count = shot < cap ? shot : cap;
    for (int i = 0; i < count; ++i) {
        out.pitch += def.recoil_pitch[static_cast<std::size_t>(i)];
        out.yaw += def.recoil_yaw[static_cast<std::size_t>(i)];
    }
    return out;
}

uint64_t WeaponParams::hash() const {
    Hasher h;
    h.u32(static_cast<uint32_t>(weapons.size()));
    for (const WeaponDef& def : weapons) {
        h.str(def.id);
        h.u32(static_cast<uint32_t>(def.slot));
        h.f32(def.damage_near).f32(def.damage_far);
        h.f32(def.falloff_start).f32(def.falloff_end);
        h.f32(def.rpm).f32(def.range);
        h.u32(static_cast<uint32_t>(def.magazine))
            .u32(static_cast<uint32_t>(def.reserve_ammo))
            .f32(def.reload_seconds);
        h.u32(static_cast<uint32_t>(def.penetration_count))
            .f32(def.penetration_damage_scale);
        h.u32(static_cast<uint32_t>(def.recoil_pitch.size()));
        for (std::size_t i = 0; i < def.recoil_pitch.size(); ++i) {
            h.f32(def.recoil_pitch[i]).f32(def.recoil_yaw[i]);
        }
        h.u32(static_cast<uint32_t>(def.recoil_recovery_ticks));
        h.f32(def.ads_recoil_scale);
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
        def.slot = static_cast<int>(params.weapons.size());  // file order unless the file says otherwise

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

        // slot is optional: without it the file order decides, but an explicit
        // one is what keeps ids stable when somebody inserts a gun above.
        if (table.find(prefix + "slot")) binder.integer(prefix + "slot", def.slot);
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
        // Recoil is optional whole-table: a gun without a pattern simply kicks
        // straight up by zero, which is what a pistol with one step looks like.
        // Optional with the rest of the recoil block: a gun without a pattern has
        // nothing for ADS to scale.
        if (table.find(prefix + "ads_recoil_scale")) {
            binder.number(prefix + "ads_recoil_scale", def.ads_recoil_scale);
        }
        binder.floats(prefix + "recoil_pitch", def.recoil_pitch);
        binder.floats(prefix + "recoil_yaw", def.recoil_yaw);
        // Optional like the pattern it belongs to: a gun without a table needs no
        // recovery timer, so a data file may omit all three together.
        if (table.find(prefix + "recoil_recovery_ticks")) {
            binder.integer(prefix + "recoil_recovery_ticks", def.recoil_recovery_ticks);
        }

        if (!validate(def, error)) return false;
        params.weapons.push_back(std::move(def));
    }

    for (const WeaponDef& def : params.weapons) {
        if (def.recoil_pitch.size() != def.recoil_yaw.size()) {
            error = "weapon " + def.id + ": recoil_pitch and recoil_yaw differ in length";
            return false;
        }
        if (!(def.ads_recoil_scale > 0.0f) || def.ads_recoil_scale > 1.0f) {
            error = "weapon " + def.id + ": ads_recoil_scale must be in (0, 1]";
            return false;
        }
        if (!def.recoil_pitch.empty() && def.recoil_recovery_ticks <= 0) {
            error = "weapon " + def.id + ": a recoil pattern needs recoil_recovery_ticks > 0";
            return false;
        }
        const RecoilOffset total = recoil_offset(def, recoil_cap(def));
        if (!(std::fabs(total.pitch) < 1.5708f) || !(std::fabs(total.yaw) < 1.5708f)) {
            error = "weapon " + def.id + ": recoil pattern sums past 90 degrees";
            return false;
        }
    }

    for (std::size_t i = 0; i < params.weapons.size(); ++i) {
        for (std::size_t j = i + 1; j < params.weapons.size(); ++j) {
            if (params.weapons[i].slot == params.weapons[j].slot) {
                error = "weapon " + params.weapons[i].id + " and " + params.weapons[j].id +
                        " share slot " + std::to_string(params.weapons[i].slot);
                return false;
            }
        }
    }

    if (params.weapons.empty()) {
        error = "no [weapon.<id>] sections in " + path;
        return false;
    }

    binder.report_unknown("");

    out = std::move(params);
    return true;
}

bool load_weapons_beside(const std::string& movement_path, WeaponParams& out,
                         std::string& error) {
    namespace fs = std::filesystem;
    const fs::path sibling = fs::u8path(movement_path).parent_path() / "weapons.toml";
    std::error_code ec;
    if (!fs::exists(sibling, ec)) {
        out = WeaponParams{};
        return true;
    }
    std::vector<std::string> warnings;
    return load_weapon_params(sibling.u8string(), out, warnings, error);
}

}  // namespace sim
