#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sim {

// Plan section 6.10 / M6: HP and the three armour types decided on 2026-10-18.
// Everything here is data driven so the numbers can be tuned without a rebuild.
enum class ArmorType : uint8_t {
    None = 0,
    Light = 1,   // 25, no regeneration
    Regen = 2,   // 25, regenerates after a delay without damage
    Heavy = 3,   // 50, no regeneration
};

constexpr int kArmorTypeCount = 4;

struct ArmorDef {
    int max_armor = 0;
    int absorb_percent = 66;   // share of incoming damage taken by armour
    bool regenerates = false;
    int regen_delay_ticks = 320;      // 5 s at 64 Hz
    int regen_ticks_per_point = 26;   // 25 points over ~10 s
};

struct CombatParams {
    int max_health = 100;
    ArmorDef armor[kArmorTypeCount];

    uint64_t hash() const;
};

struct PlayerVitals {
    int health = 100;
    int armor = 0;
    ArmorType armor_type = ArmorType::None;
    int ticks_since_damage = 0;
    int regen_accumulator = 0;
};

bool load_combat_params(const std::string& path, CombatParams& out,
                        std::vector<std::string>& warnings, std::string& error);

void equip_armor(PlayerVitals& vitals, ArmorType type, const CombatParams& params);

// Applies damage through armour. Returns the health damage actually dealt.
// Integer maths only: damage must never depend on float rounding.
int apply_damage(PlayerVitals& vitals, int damage, const CombatParams& params);

// Advances regeneration by one tick.
void tick_vitals(PlayerVitals& vitals, const CombatParams& params);

}  // namespace sim
