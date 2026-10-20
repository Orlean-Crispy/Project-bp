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

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "mesh_builder.h"
#include "mesh_common.h"
#include "net_gateway.h"
#include "remote_sampling.h"
#include "sim/collision/map_parse.h"
#include "sim/core/event.h"
#include "sim/core/world.h"
#include "sim/input/bindings.h"
#include "sim/net/session.h"

using namespace godot;

namespace {

// Shared with the mesh builder; see bridge/mesh_common.h.
using bridge::to_godot;

}  // namespace

class SimWorld : public RefCounted {
    GDCLASS(SimWorld, RefCounted)

public:
    SimWorld() = default;
    ~SimWorld() override = default;

    bool load_map(const String& map_path, const String& data_path) {
        sim::MapData map;
        std::string error;
        m_map_path = map_path.utf8().get_data();
        m_data_path = data_path.utf8().get_data();
        if (!sim::parse_map_file(m_map_path, map, error)) {
            ERR_PRINT(String("breakpoint: ") + String(error.c_str()));
            return false;
        }

        std::vector<std::string> warnings;
        if (!sim::load_movement_params(m_data_path, m_world.params, warnings, error)) {
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
        // AUD-06: a network session never falls back to the local map world, even
        // when it is disconnected; only step() from solo play may advance it.
        if (m_net.active()) return;
        if (m_world.players.empty()) return;
        m_world.step({m_input}, 1.0f / 64.0f, &m_events);
    }

    // ---- networking (host = listen server + local client, join = client) ----
    bool host(int port, int players) {
        std::string error;
        if (m_net.start_host(m_map_path, m_data_path, port, players, error)) return true;
        if (!error.empty()) ERR_PRINT(String(error.c_str()));
        return false;
    }

    bool join(const String& host, int port) {
        std::string error;
        if (m_net.join(host.utf8().get_data(), port, m_map_path, m_data_path, error)) return true;
        if (!error.empty()) ERR_PRINT(String(error.c_str()));
        return false;
    }

    bool reconnect() { return m_net.reconnect(); }

    void step_network() {
        m_net.pump(m_input, &m_events);
        // AUD-05: remember when the newest snapshot arrived; get_remote_players()
        // turns the elapsed time into the interpolation fraction.
        const sim::NetClient* net = m_net.client();
        const uint32_t newest = net == nullptr ? 0 : net->remote().newest_tick();
        if (newest == 0) {
            m_remote_newest = 0;
        } else if (newest != m_remote_newest) {
            m_remote_newest = newest;
            m_remote_arrived = std::chrono::steady_clock::now();
        }
    }

    bool is_networked() const { return m_net.active(); }
    bool get_joined() const { return m_net.joined(); }
    int get_player_slot() const { return m_net.slot(); }
    int get_peer_count() const { return m_net.peers(); }
    String get_role() const {
        if (!m_net.active()) return "solo";
        return m_net.hosting() ? "host" : "client";
    }

    Array get_remote_players() const {
        Array out;
        const sim::NetClient* net = m_net.client();
        if (net == nullptr) return out;
        const sim::SnapshotInterpolator& interp = net->remote();
        const double since_arrival = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - m_remote_arrived).count();
        bridge::RemoteSample sample;
        if (!bridge::remote_sample(interp.oldest_tick(), interp.newest_tick(), since_arrival,
                                   sample)) {
            return out;
        }
        std::vector<sim::PlayerSnap> players;
        if (!interp.sample(sample.tick, sample.frac, players)) return out;
        const int self = net->player_index();
        for (size_t i = 0; i < players.size(); ++i) {
            if (static_cast<int>(i) == self) continue;
            if ((players[i].flags & 4) == 0) continue;  // reserved slot, not joined
            Dictionary entry;
            entry["slot"] = static_cast<int>(i);
            entry["pos"] = to_godot(players[i].pos);
            entry["crouched"] = (players[i].flags & 2) != 0;
            entry["state"] = players[i].state;
            entry["yaw"] = players[i].yaw;
            out.push_back(entry);
        }
        return out;
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

    const sim::PlayerState* local_player() const {
        if (const sim::NetClient* net = m_net.client()) return &net->predicted();
        return m_world.players.empty() ? nullptr : &m_world.players[0];
    }

    uint32_t get_tick() const {
        const sim::NetClient* net = m_net.client();
        return net ? net->tick() : m_world.tick;
    }

    Vector3 get_position() const {
        const sim::PlayerState* p = local_player();
        return p == nullptr ? Vector3() : to_godot(p->pos);
    }

    Vector3 get_velocity() const {
        const sim::PlayerState* p = local_player();
        return p == nullptr ? Vector3() : to_godot(p->vel);
    }

    int get_state() const {
        const sim::PlayerState* p = local_player();
        return p == nullptr ? 0 : static_cast<int>(p->state);
    }

    int get_impulses() const {
        const sim::PlayerState* p = local_player();
        return p == nullptr ? 0 : p->impulses;
    }

    // HUD reads the parameter, never a literal: the cap is still undecided
    // (plan 5.2 is "TBD", ADR 0005 open item 1).
    int get_max_impulses() const { return m_world.params.max_impulses; }

    // Placeholder until hp/armor are networked (M6).
    int get_snapshot_age() const { return m_net.snapshot_age(); }

    int get_hp() const { return 100; }
    int get_armor() const { return 0; }

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
        ClassDB::bind_method(D_METHOD("host", "port", "players"), &SimWorld::host);
        ClassDB::bind_method(D_METHOD("join", "host", "port"), &SimWorld::join);
        ClassDB::bind_method(D_METHOD("reconnect"), &SimWorld::reconnect);
        ClassDB::bind_method(D_METHOD("step_network"), &SimWorld::step_network);
        ClassDB::bind_method(D_METHOD("is_networked"), &SimWorld::is_networked);
        ClassDB::bind_method(D_METHOD("get_joined"), &SimWorld::get_joined);
        ClassDB::bind_method(D_METHOD("get_player_slot"), &SimWorld::get_player_slot);
        ClassDB::bind_method(D_METHOD("get_peer_count"), &SimWorld::get_peer_count);
        ClassDB::bind_method(D_METHOD("get_role"), &SimWorld::get_role);
        ClassDB::bind_method(D_METHOD("get_remote_players"), &SimWorld::get_remote_players);
        ClassDB::bind_method(D_METHOD("get_tick"), &SimWorld::get_tick);
        ClassDB::bind_method(D_METHOD("get_position"), &SimWorld::get_position);
        ClassDB::bind_method(D_METHOD("get_velocity"), &SimWorld::get_velocity);
        ClassDB::bind_method(D_METHOD("get_state"), &SimWorld::get_state);
        ClassDB::bind_method(D_METHOD("get_impulses"), &SimWorld::get_impulses);
        ClassDB::bind_method(D_METHOD("get_max_impulses"), &SimWorld::get_max_impulses);
        ClassDB::bind_method(D_METHOD("get_snapshot_age"), &SimWorld::get_snapshot_age);
        ClassDB::bind_method(D_METHOD("get_hp"), &SimWorld::get_hp);
        ClassDB::bind_method(D_METHOD("get_armor"), &SimWorld::get_armor);
        ClassDB::bind_method(D_METHOD("get_eye_height"), &SimWorld::get_eye_height);
        ClassDB::bind_method(D_METHOD("get_vertices"), &SimWorld::get_vertices);
        ClassDB::bind_method(D_METHOD("get_normals"), &SimWorld::get_normals);
        ClassDB::bind_method(D_METHOD("get_colors"), &SimWorld::get_colors);
        ClassDB::bind_method(D_METHOD("get_indices"), &SimWorld::get_indices);
    }

private:
    void build_mesh() {
        bridge::build_map_mesh(m_world.collision, m_vertices, m_normals, m_colors, m_indices);
    }

    sim::World m_world;
    sim::Bindings m_bindings;
    sim::MoveInput m_input;
    sim::EventQueue m_events;

    std::string m_map_path;
    std::string m_data_path;
    bridge::NetGateway m_net;
    uint32_t m_remote_newest = 0;
    std::chrono::steady_clock::time_point m_remote_arrived{};
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
