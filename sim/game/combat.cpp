#include "sim/game/combat.h"

#include "sim/core/hash.h"
#include "sim/data/config.h"

namespace sim {
namespace {

struct ArmorRow {
    ArmorType type;
    const char* section;
};

const ArmorRow kRows[] = {
    {ArmorType::Light, "armor.light"},
    {ArmorType::Regen, "armor.regen"},
    {ArmorType::Heavy, "armor.heavy"},
};

// R17: armour_type is data/network input, so it must never index the table out
// of bounds. -1 means "not a real armour type".
int armor_index(ArmorType type) {
    const int index = static_cast<int>(type);
    return index >= 0 && index < kArmorTypeCount ? index : -1;
}

// absorb_percent is a percentage: 0..100. Outside that it either heals the
// player (negative damage) or makes armour eat more than the hit.
int absorb_percent(const ArmorDef& def) {
    if (def.absorb_percent < 0) return 0;
    if (def.absorb_percent > 100) return 100;
    return def.absorb_percent;
}

}  // namespace

uint64_t CombatParams::hash() const {
    Hasher h;
    h.u32(static_cast<uint32_t>(max_health));
    for (int i = 0; i < kArmorTypeCount; ++i) {
        h.u32(static_cast<uint32_t>(armor[i].max_armor));
        h.u32(static_cast<uint32_t>(armor[i].absorb_percent));
        h.u32(armor[i].regenerates ? 1u : 0u);
        h.u32(static_cast<uint32_t>(armor[i].regen_delay_ticks));
        h.u32(static_cast<uint32_t>(armor[i].regen_ticks_per_point));
    }
    return h.value();
}

bool load_combat_params(const std::string& path, CombatParams& out,
                        std::vector<std::string>& warnings, std::string& error) {
    TomlTable table;
    if (!load_toml_file(path, table, error)) return false;

    CombatParams p;
    if (const TomlValue* v = table.find("health.max_health")) {
        if (v->type == TomlValue::Type::Number) p.max_health = static_cast<int>(v->number);
    }

    for (const ArmorRow& row : kRows) {
        const std::string base = row.section;
        ArmorDef& def = p.armor[static_cast<int>(row.type)];
        if (const TomlValue* v = table.find(base + ".max_armor")) {
            if (v->type == TomlValue::Type::Number) def.max_armor = static_cast<int>(v->number);
        }
        if (const TomlValue* v = table.find(base + ".absorb_percent")) {
            if (v->type == TomlValue::Type::Number) def.absorb_percent = static_cast<int>(v->number);
        }
        if (const TomlValue* v = table.find(base + ".regenerates")) {
            if (v->type == TomlValue::Type::Boolean) def.regenerates = v->boolean;
        }
        if (const TomlValue* v = table.find(base + ".regen_delay_seconds")) {
            if (v->type == TomlValue::Type::Number) {
                def.regen_delay_ticks = static_cast<int>(v->number * 64.0 + 0.5);
            }
        }
        if (const TomlValue* v = table.find(base + ".regen_seconds")) {
            if (v->type == TomlValue::Type::Number && v->number > 0.0) {
                def.regen_ticks_per_point = static_cast<int>(
                    v->number * 64.0 / static_cast<double>(def.max_armor > 0 ? def.max_armor : 1) + 0.5);
            }
        }
        // R17: clamp here, at the one place the file is read, so no consumer has
        // to cope with >100% absorption (which would turn a hit into healing).
        def.absorb_percent = absorb_percent(def);
    }
    (void)warnings;

    out = p;
    return true;
}

void equip_armor(PlayerVitals& vitals, ArmorType type, const CombatParams& params) {
    const int index = armor_index(type);
    vitals.armor_type = index < 0 ? ArmorType::None : type;
    vitals.armor = params.armor[index < 0 ? 0 : index].max_armor;
    vitals.ticks_since_damage = 0;
    vitals.regen_accumulator = 0;
}

int apply_damage(PlayerVitals& vitals, int damage, const CombatParams& params) {
    if (damage <= 0 || vitals.health <= 0) return 0;

    int remaining = damage;
    const int type_index = armor_index(vitals.armor_type);
    if (vitals.armor > 0 && type_index >= 0) {
        const ArmorDef& def = params.armor[type_index];
        // Re-clamped at use as well: a caller may hand us params that never went
        // through load_combat_params (tests, future hot reload).
        const int absorbed = remaining * absorb_percent(def) / 100;
        const int consumed = absorbed < vitals.armor ? absorbed : vitals.armor;
        vitals.armor -= consumed;
        remaining -= consumed;
    }

    if (remaining > vitals.health) remaining = vitals.health;
    vitals.health -= remaining;
    vitals.ticks_since_damage = 0;
    vitals.regen_accumulator = 0;
    return remaining;
}

void tick_vitals(PlayerVitals& vitals, const CombatParams& params) {
    const int type_index = armor_index(vitals.armor_type);
    if (type_index < 0 || vitals.armor_type == ArmorType::None) return;
    const ArmorDef& def = params.armor[type_index];
    if (!def.regenerates) return;
    if (vitals.health <= 0) return;
    if (vitals.armor >= def.max_armor) return;

    if (vitals.ticks_since_damage < def.regen_delay_ticks) {
        ++vitals.ticks_since_damage;
        return;
    }
    if (def.regen_ticks_per_point <= 0) return;
    if (++vitals.regen_accumulator >= def.regen_ticks_per_point) {
        vitals.regen_accumulator = 0;
        ++vitals.armor;
        if (vitals.armor > def.max_armor) vitals.armor = def.max_armor;
    }
}

}  // namespace sim
