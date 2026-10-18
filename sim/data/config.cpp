#include "sim/data/config.h"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unordered_set>

#include "sim/core/hash.h"

namespace sim {
namespace {

std::string trim(const std::string& s) {
    size_t a = 0;
    size_t b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// Removes a trailing # comment that is not inside a quoted string.
std::string strip_comment(const std::string& s) {
    bool in_string = false;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '"') in_string = !in_string;
        if (c == '#' && !in_string) return s.substr(0, i);
    }
    return s;
}

bool parse_value(const std::string& raw, TomlValue& out) {
    const std::string v = trim(raw);
    if (v.empty()) return false;

    if (v.front() == '"') {
        if (v.size() < 2 || v.back() != '"') return false;
        out.type = TomlValue::Type::String;
        out.string = v.substr(1, v.size() - 2);
        return true;
    }
    if (v == "true" || v == "false") {
        out.type = TomlValue::Type::Boolean;
        out.boolean = (v == "true");
        return true;
    }

    errno = 0;
    char* end = nullptr;
    const double d = std::strtod(v.c_str(), &end);
    if (end == v.c_str() || *end != '\0' || errno == ERANGE) return false;
    out.type = TomlValue::Type::Number;
    out.number = d;
    return true;
}

}  // namespace

const TomlValue* TomlTable::find(const std::string& key) const {
    for (const auto& e : entries) {
        if (e.first == key) return &e.second;
    }
    return nullptr;
}

bool parse_toml(const std::string& text, TomlTable& out, std::string& error) {
    std::istringstream in(text);
    std::string line;
    std::string section;
    int line_no = 0;

    while (std::getline(in, line)) {
        ++line_no;
        std::string s = trim(strip_comment(line));
        if (s.empty()) continue;

        if (s.front() == '[') {
            if (s.back() != ']') {
                error = "line " + std::to_string(line_no) + ": malformed section header";
                return false;
            }
            section = trim(s.substr(1, s.size() - 2));
            if (section.empty()) {
                error = "line " + std::to_string(line_no) + ": empty section name";
                return false;
            }
            continue;
        }

        const size_t eq = s.find('=');
        if (eq == std::string::npos) {
            error = "line " + std::to_string(line_no) + ": expected key = value";
            return false;
        }
        const std::string key = trim(s.substr(0, eq));
        if (key.empty()) {
            error = "line " + std::to_string(line_no) + ": empty key";
            return false;
        }

        TomlValue value;
        if (!parse_value(s.substr(eq + 1), value)) {
            error = "line " + std::to_string(line_no) + ": cannot parse value";
            return false;
        }
        out.entries.emplace_back(section.empty() ? key : section + "." + key, value);
    }
    return true;
}

bool load_toml_file(const std::string& path, TomlTable& out, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot open " + path;
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return parse_toml(buffer.str(), out, error);
}

uint64_t MovementParams::hash() const {
    Hasher h;
    h.f32(base_speed).f32(accel).f32(air_accel).f32(friction).f32(stop_speed)
     .f32(gravity).f32(jump_velocity).f32(step_height).f32(crouch_height)
     .f32(stand_height).f32(player_radius).f32(crouch_speed_scale);
    h.u32(static_cast<uint32_t>(max_impulses)).u32(recharge_on_ground ? 1u : 0u);
    h.f32(slide_entry_window).f32(slide_min_speed).f32(slide_friction_scale).f32(slide_jump_boost)
     .f32(slide_entry_boost).f32(slide_max_duration);
    h.f32(wallrun_min_approach_speed).f32(wallrun_max_duration)
     .f32(wallrun_gravity_scale).f32(wallrun_normal_dot);
    h.f32(wallclimb_max_duration).f32(wallclimb_speed);
    h.f32(vault_max_height).f32(vault_duration).f32(vault_forward_boost);
    h.f32(dash_speed).f32(dash_cooldown);
    h.f32(min_wall_thickness).u32(static_cast<uint32_t>(max_slide_steps))
     .u32(static_cast<uint32_t>(max_bumps));
    h.f32(damage_slow_factor).u32(static_cast<uint32_t>(damage_slow_ticks)).f32(headshot_mult);
    return h.value();
}

namespace {

struct Binder {
    const TomlTable& table;
    std::unordered_set<std::string> used;
    std::vector<std::string>& warnings;

    bool number(const std::string& key, float& target) {
        const TomlValue* v = table.find(key);
        if (!v) return false;
        used.insert(key);
        if (v->type != TomlValue::Type::Number) {
            warnings.push_back(key + ": expected a number, kept default");
            return false;
        }
        target = static_cast<float>(v->number);
        return true;
    }

    bool integer(const std::string& key, int& target) {
        const TomlValue* v = table.find(key);
        if (!v) return false;
        used.insert(key);
        if (v->type != TomlValue::Type::Number) {
            warnings.push_back(key + ": expected a number, kept default");
            return false;
        }
        target = static_cast<int>(v->number);
        return true;
    }

    bool boolean(const std::string& key, bool& target) {
        const TomlValue* v = table.find(key);
        if (!v) return false;
        used.insert(key);
        if (v->type != TomlValue::Type::Boolean) {
            warnings.push_back(key + ": expected true/false, kept default");
            return false;
        }
        target = v->boolean;
        return true;
    }
};

}  // namespace

bool load_movement_params(const std::string& path, MovementParams& out,
                          std::vector<std::string>& warnings, std::string& error) {
    TomlTable table;
    if (!load_toml_file(path, table, error)) return false;

    MovementParams p;
    Binder b{table, {}, warnings};

    b.number("base.base_speed", p.base_speed);
    b.number("base.accel", p.accel);
    b.number("base.air_accel", p.air_accel);
    b.number("base.friction", p.friction);
    b.number("base.stop_speed", p.stop_speed);
    b.number("base.gravity", p.gravity);
    b.number("base.jump_velocity", p.jump_velocity);
    b.number("base.step_height", p.step_height);
    b.number("base.crouch_height", p.crouch_height);
    b.number("base.stand_height", p.stand_height);
    b.number("base.player_radius", p.player_radius);
    b.number("base.crouch_speed_scale", p.crouch_speed_scale);

    b.integer("parkour.max_impulses", p.max_impulses);
    b.boolean("parkour.recharge_on_ground", p.recharge_on_ground);

    b.number("slide.entry_window", p.slide_entry_window);
    b.number("slide.min_speed", p.slide_min_speed);
    b.number("slide.friction_scale", p.slide_friction_scale);
    b.number("slide.jump_boost", p.slide_jump_boost);
    b.number("slide.entry_boost", p.slide_entry_boost);
    b.number("slide.max_duration", p.slide_max_duration);

    b.number("wallrun.min_approach_speed", p.wallrun_min_approach_speed);
    b.number("wallrun.max_duration", p.wallrun_max_duration);
    b.number("wallrun.gravity_scale", p.wallrun_gravity_scale);
    b.number("wallrun.normal_dot", p.wallrun_normal_dot);

    b.number("wallclimb.max_duration", p.wallclimb_max_duration);
    b.number("wallclimb.climb_speed", p.wallclimb_speed);

    b.number("vault.max_height", p.vault_max_height);
    b.number("vault.duration", p.vault_duration);
    b.number("vault.forward_boost", p.vault_forward_boost);

    b.number("dash.speed", p.dash_speed);
    b.number("dash.cooldown", p.dash_cooldown);

    b.number("collision.min_wall_thickness", p.min_wall_thickness);
    b.integer("collision.max_slide_steps", p.max_slide_steps);
    b.integer("collision.max_bumps", p.max_bumps);

    b.number("damage.slow_factor", p.damage_slow_factor);
    b.integer("damage.slow_ticks", p.damage_slow_ticks);
    b.number("damage.headshot_mult", p.headshot_mult);

    for (const auto& e : table.entries) {
        if (b.used.find(e.first) == b.used.end()) {
            warnings.push_back(e.first + ": unknown key");
        }
    }

    out = p;
    return true;
}

}  // namespace sim
