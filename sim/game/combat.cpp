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
    }
    (void)warnings;

    out = p;
    return true;
}

void equip_armor(PlayerVitals& vitals, ArmorType type, const CombatParams& params) {
    vitals.armor_type = type;
    vitals.armor = params.armor[static_cast<int>(type)].max_armor;
    vitals.ticks_since_damage = 0;
    vitals.regen_accumulator = 0;
}

int apply_damage(PlayerVitals& vitals, int damage, const CombatParams& params) {
    if (damage <= 0 || vitals.health <= 0) return 0;

    int remaining = damage;
    if (vitals.armor > 0) {
        const ArmorDef& def = params.armor[static_cast<int>(vitals.armor_type)];
        const int absorbed = remaining * def.absorb_percent / 100;
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
    if (vitals.armor_type == ArmorType::None) return;
    const ArmorDef& def = params.armor[static_cast<int>(vitals.armor_type)];
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
