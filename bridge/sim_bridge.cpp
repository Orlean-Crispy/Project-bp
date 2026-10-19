// GDExtension bridge: the only place the simulation meets Godot (plan 3.1 / 3.2).
// Everything above this file is presentation; everything below is deterministic
// C++ with no engine dependency.

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <string>
#include <vector>

#include "sim/collision/map_parse.h"
#include "sim/core/event.h"
#include "sim/core/world.h"
#include "sim/input/bindings.h"

using namespace godot;

namespace {

// Godot is Y-up, the simulation is Z-up (Quake convention).
Vector3 to_godot(const sim::Vec3& v) { return Vector3(v.x, v.z, -v.y); }

int material_color(const std::string& name) {
    if (name == "FLOOR") return 0x7e7c76;
    if (name == "CEILING") return 0x4a4e58;
    if (name == "WALL") return 0x5c6474;
    if (name == "THINWALL") return 0xa8885c;
    if (name == "PLATFORM") return 0x609468;
    if (name == "STEP") return 0x9c7a52;
    if (name == "LOWCEIL") return 0x7c5c94;
    if (name == "METAL") return 0x84888e;
    if (name == "CONCRETE") return 0x787872;
    return 0x6e6e72;
}

}  // namespace

class SimWorld : public RefCounted {
    GDCLASS(SimWorld, RefCounted)

public:
    SimWorld() = default;
    ~SimWorld() override = default;

    bool load_map(const String& map_path, const String& data_path) {
        sim::MapData map;
        std::string error;
        if (!sim::parse_map_file(map_path.utf8().get_data(), map, error)) {
            ERR_PRINT(String("breakpoint: ") + String(error.c_str()));
            return false;
        }

        std::vector<std::string> warnings;
        if (!sim::load_movement_params(data_path.utf8().get_data(), m_world.params, warnings, error)) {
            ERR_PRINT(String("breakpoint: ") + String(error.c_str()));
            return false;
        }

        m_world.collision.build(map);
        m_world.params_hash = m_world.params.hash();

        sim::PlayerState player;
        player.pos = sim::Vec3(-16.0f, 0.0f, 0.05f);
        player.impulses = m_world.params.max_impulses;

        for (const auto& entity : map.entities) {
            const auto it = entity.keys.find("classname");
            if (it == entity.keys.end() || it->second != "info_player_start") continue;
            const auto origin = entity.keys.find("origin");
            if (origin == entity.keys.end()) continue;
            float x = 0.0f, y = 0.0f, z = 0.0f;
            if (std::sscanf(origin->second.c_str(), "%f %f %f", &x, &y, &z) == 3) {
                player.pos = sim::Vec3(x * 0.0254f, y * 0.0254f, z * 0.0254f);
            }
        }

        m_world.players.clear();
        m_world.players.push_back(player);
        m_world.tick = 0;

        build_mesh();
        return true;
    }

    bool load_bindings(const String& path) {
        std::string error;
        std::vector<std::string> warnings;
        return m_bindings.load(path.utf8().get_data(), error, warnings);
    }

    Dictionary get_bindings() const {
        Dictionary out;
        for (size_t i = 0; i < sim::kActionCount; ++i) {
            const sim::Action action = static_cast<sim::Action>(i);
            out[String(sim::action_name(action))] = String(sim::key_name(m_bindings.key(action)));
        }
        return out;
    }

    void set_input(float forward, float right, float yaw, bool jump, bool crouch, bool slide,
                   bool dash, bool wallrun, bool wallclimb, bool vault) {
        m_input.forward = forward;
        m_input.right = right;
        m_input.yaw = yaw;
        m_input.jump = jump;
        m_input.crouch = crouch;
        m_input.slide = slide;
        m_input.dash = dash;
        m_input.wallrun = wallrun;
        m_input.wallclimb = wallclimb;
        m_input.vault = vault;
    }

    void step() {
        if (m_world.players.empty()) return;
        m_world.step({m_input}, 1.0f / 64.0f, &m_events);
    }

    // Plan 3.4: the presentation layer drains the event channel once per frame.
    // Events carry a tick so a future rollback can discard stale predictions.
    Array drain_events() {
        Array out;
        sim::SimEvent event;
        while (m_events.pop(event)) {
            Dictionary entry;
            entry["tick"] = event.tick;
            entry["type"] = static_cast<int>(event.type);
            entry["entity"] = event.entity_id;
            entry["a"] = to_godot(event.a);
            entry["b"] = to_godot(event.b);
            entry["param"] = event.param;
            out.push_back(entry);
        }
        return out;
    }

    uint32_t get_tick() const { return m_world.tick; }

    Vector3 get_position() const {
        return m_world.players.empty() ? Vector3() : to_godot(m_world.players[0].pos);
    }

    Vector3 get_velocity() const {
        return m_world.players.empty() ? Vector3() : to_godot(m_world.players[0].vel);
    }

    int get_state() const {
        return m_world.players.empty() ? 0 : static_cast<int>(m_world.players[0].state);
    }

    int get_impulses() const {
        return m_world.players.empty() ? 0 : m_world.players[0].impulses;
    }

    float get_eye_height() const { return 1.55f; }

    PackedVector3Array get_vertices() const { return m_vertices; }
    PackedVector3Array get_normals() const { return m_normals; }
    PackedColorArray get_colors() const { return m_colors; }
    PackedInt32Array get_indices() const { return m_indices; }

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("load_map", "map_path", "data_path"), &SimWorld::load_map);
        ClassDB::bind_method(D_METHOD("load_bindings", "path"), &SimWorld::load_bindings);
        ClassDB::bind_method(D_METHOD("get_bindings"), &SimWorld::get_bindings);
        ClassDB::bind_method(D_METHOD("set_input", "forward", "right", "yaw", "jump", "crouch",
                                      "slide", "dash", "wallrun", "wallclimb", "vault"),
                             &SimWorld::set_input);
        ClassDB::bind_method(D_METHOD("step"), &SimWorld::step);
        ClassDB::bind_method(D_METHOD("drain_events"), &SimWorld::drain_events);
        ClassDB::bind_method(D_METHOD("get_tick"), &SimWorld::get_tick);
        ClassDB::bind_method(D_METHOD("get_position"), &SimWorld::get_position);
        ClassDB::bind_method(D_METHOD("get_velocity"), &SimWorld::get_velocity);
        ClassDB::bind_method(D_METHOD("get_state"), &SimWorld::get_state);
        ClassDB::bind_method(D_METHOD("get_impulses"), &SimWorld::get_impulses);
        ClassDB::bind_method(D_METHOD("get_eye_height"), &SimWorld::get_eye_height);
        ClassDB::bind_method(D_METHOD("get_vertices"), &SimWorld::get_vertices);
        ClassDB::bind_method(D_METHOD("get_normals"), &SimWorld::get_normals);
        ClassDB::bind_method(D_METHOD("get_colors"), &SimWorld::get_colors);
        ClassDB::bind_method(D_METHOD("get_indices"), &SimWorld::get_indices);
    }

private:
    void build_mesh() {
        m_vertices.clear();
        m_normals.clear();
        m_colors.clear();
        m_indices.clear();

        for (const sim::Brush& brush : m_world.collision.brushes()) {
            const int base = material_color(brush.material);
            const float r = static_cast<float>((base >> 16) & 0xFF) / 255.0f;
            const float g = static_cast<float>((base >> 8) & 0xFF) / 255.0f;
            const float b = static_cast<float>(base & 0xFF) / 255.0f;

            for (const sim::BrushSide& side : brush.sides) {
                if (side.polygon.size() < 3) continue;

                // Same cheap directional shade as the rest of the tooling, baked
                // into vertex colours so the demo does not need a light rig.
                const sim::Vec3 light(-0.45f, -0.35f, 0.82f);
                const float len = std::sqrt(light.x * light.x + light.y * light.y + light.z * light.z);
                const float lambert =
                    std::max(0.0f, sim::dot(side.plane.normal, sim::Vec3(light.x / len, light.y / len, light.z / len)));
                const float shade = 0.62f + 0.38f * lambert;
                const Color color(r * shade, g * shade, b * shade);

                const int32_t start = static_cast<int32_t>(m_vertices.size());
                const Vector3 normal = to_godot(side.plane.normal);
                for (const sim::Vec3& v : side.polygon) {
                    m_vertices.push_back(to_godot(v));
                    m_normals.push_back(normal);
                    m_colors.push_back(color);
                }
                for (size_t i = 1; i + 1 < side.polygon.size(); ++i) {
                    m_indices.push_back(start);
                    m_indices.push_back(start + static_cast<int32_t>(i));
                    m_indices.push_back(start + static_cast<int32_t>(i + 1));
                }
            }
        }
    }

    sim::World m_world;
    sim::Bindings m_bindings;
    sim::MoveInput m_input;
    sim::EventQueue m_events;
    PackedVector3Array m_vertices;
    PackedVector3Array m_normals;
    PackedColorArray m_colors;
    PackedInt32Array m_indices;
};

static void initialize_breakpoint(ModuleInitializationLevel level) {
    if (level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
    GDREGISTER_CLASS(SimWorld);
}

static void uninitialize_breakpoint(ModuleInitializationLevel level) {
    (void)level;
}

extern "C" {
GDExtensionBool GDE_EXPORT breakpoint_library_init(GDExtensionInterfaceGetProcAddress get_proc_address,
                                                  const GDExtensionClassLibraryPtr library,
                                                  GDExtensionInitialization* initialization) {
    godot::GDExtensionBinding::InitObject init_object(get_proc_address, library, initialization);
    init_object.register_initializer(initialize_breakpoint);
    init_object.register_terminator(uninitialize_breakpoint);
    init_object.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
    return init_object.init();
}
}
