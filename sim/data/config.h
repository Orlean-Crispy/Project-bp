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

    // slide
    float slide_entry_window = 0.15f;
    float slide_min_speed = 5.08f;
    float slide_friction_scale = 0.35f;
    float slide_jump_boost = 1.15f;

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
    float vault_forward_boost = 1.2f;

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

    // FNV-1a over every field in declaration order.
    uint64_t hash() const;
};

// Minimal TOML subset: [section] headers, key = number|string|bool, # comments.
// Deliberately not a general TOML parser: the data files are ours.
struct TomlValue {
    enum class Type { Number, String, Boolean } type = Type::Number;
    double number = 0.0;
    std::string string;
    bool boolean = false;
};

struct TomlTable {
    std::vector<std::pair<std::string, TomlValue>> entries;  // "section.key", insertion order

    const TomlValue* find(const std::string& key) const;
};

bool parse_toml(const std::string& text, TomlTable& out, std::string& error);
bool load_toml_file(const std::string& path, TomlTable& out, std::string& error);

// Applies a parsed table on top of defaults. Unknown keys are collected into
// warnings instead of being silently ignored.
bool load_movement_params(const std::string& path, MovementParams& out,
                          std::vector<std::string>& warnings, std::string& error);

}  // namespace sim
