#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sim {

// Every tunable that affects feel. Lives in sim/data/movement.toml so it can be
// reloaded without recompiling (iron law I), and its hash ships with the world
// state so two machines cannot silently run different numbers.
struct MovementParams {
    // base (section 5.6, all metres)
    float base_speed = 8.13f;
    float accel = 10.0f;
    float air_accel = 1.0f;
    float friction = 6.0f;
    float stop_speed = 2.54f;
    float gravity = 20.32f;
    float jump_velocity = 6.86f;
    float step_height = 0.46f;
    float crouch_height = 0.91f;
    float stand_height = 1.42f;
    float player_radius = 0.405f;
    float crouch_speed_scale = 0.4f;

    // parkour resource (section 5.2 / 8.3)
    int max_impulses = 4;
    bool recharge_on_ground = true;
    // Ground-contact ticks per impulse. One per tick refilled all four in 62 ms,
    // which made the parkour resource meaningless (2026-10-20).
    int recharge_ticks = 32;

    // slide
    float slide_min_speed = 5.08f;
    float slide_friction_scale = 0.35f;
    float slide_jump_boost = 1.15f;
    float slide_entry_boost = 1.15f;
    float slide_max_duration = 1.5f;

    // wallrun
    float wallrun_min_approach_speed = 6.35f;
    float wallrun_max_duration = 0.9f;
    float wallrun_gravity_scale = 0.35f;
    float wallrun_normal_dot = 0.7f;

    // wallclimb
    float wallclimb_max_duration = 0.7f;
    float wallclimb_speed = 4.57f;

    // vault
    float vault_max_height = 1.22f;
    float vault_duration = 0.35f;

    // dash
    float dash_speed = 11.43f;
    float dash_cooldown = 1.0f;

    // collision (section 5.9) - these two must stay consistent with map rules
    float min_wall_thickness = 0.15f;
    int max_slide_steps = 4;
    int max_bumps = 4;

    // damage / chase termination (section 6.10)
    float damage_slow_factor = 0.85f;
    int damage_slow_ticks = 96;
    float headshot_mult = 5.0f;
    // Leg multiplier. It was a literal 0.7 in hitbox.cpp, which is exactly the
    // kind of feel value iron law I says must live in the data file.
    float legshot_mult = 0.7f;
    // Eye height as a fraction of the hull (camera and shot origin), and the zone
    // bands as fractions of the hull height. DESIGN-09: a level shot must land in
    // the chest, so head_band stays above eye_height_scale.
    float eye_height_scale = 0.82f;
    float head_band = 0.88f;
    float chest_band = 0.45f;

    // FNV-1a over every field in declaration order.
    uint64_t hash() const;
};

// Minimal TOML subset: [section] headers, key = number|string|bool, # comments.
// Deliberately not a general TOML parser: the data files are ours.
struct TomlValue {
    enum class Type { Number, String, Boolean, Array } type = Type::Number;
    double number = 0.0;
    std::string string;
    bool boolean = false;
    // Recoil patterns are per-shot tables, so the subset needs one array type.
    // Numbers only: a table of strings would be a different feature.
    std::vector<double> array;
};

struct TomlTable {
    std::vector<std::pair<std::string, TomlValue>> entries;  // "section.key", insertion order

    const TomlValue* find(const std::string& key) const;
};

bool parse_toml(const std::string& text, TomlTable& out, std::string& error);
bool load_toml_file(const std::string& path, TomlTable& out, std::string& error);

// Binds values from a parsed table onto fields while remembering which keys were
// consumed, so a typo in a data file is reported instead of silently keeping the
// default. Every loader (movement, weapons) uses this one implementation: two
// copies of "read a number, warn on the wrong type" is how the warnings drift.
struct TomlBinder {
    TomlBinder(const TomlTable& t, std::vector<std::string>& w) : table(t), warnings(w) {}

    const TomlTable& table;
    std::vector<std::string>& warnings;
    std::vector<std::string> used;

    bool number(const std::string& key, float& target);
    bool integer(const std::string& key, int& target);
    bool boolean(const std::string& key, bool& target);
    bool floats(const std::string& key, std::vector<float>& target);

    // Reports every unconsumed key starting with 'prefix' as unknown. The prefix
    // keeps the report scoped when several sections share one flat table.
    void report_unknown(const std::string& prefix) const;
};

// R8: movement is not the only rule set. The combat rules (HP/armour) and the
// headshot multiplier also change what a client predicts, so the one params hash
// that the join handshake and the configstring carry folds both in.
struct CombatParams;
struct WeaponParams;
// M6.2: weapons join the handshake hash. Tuning a gun changes what a client
// predicts, so a host and a client with different weapons.toml must refuse to
// play instead of diverging silently.
uint64_t combined_params_hash(const MovementParams& movement, const CombatParams& combat,
                              const WeaponParams& weapons);

// Combat rules live next to movement.toml (same data directory). A missing file
// means "compiled defaults", so a bare movement.toml stays usable.
bool load_combat_beside(const std::string& movement_path, CombatParams& out,
                        std::string& error);

// Applies a parsed table on top of defaults. Unknown keys are collected into
// warnings instead of being silently ignored.
bool load_movement_params(const std::string& path, MovementParams& out,
                          std::vector<std::string>& warnings, std::string& error);

}  // namespace sim
