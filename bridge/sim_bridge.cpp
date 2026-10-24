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
#include "sim/core/tick.h"
#include "sim/core/version.h"
#include "sim/data/config.h"
#include "sim/core/world.h"
#include "sim/input/bindings.h"
#include "sim/input/buttons.h"
#include "sim/net/ports.h"
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

        // R8: the join hash covers movement *and* combat, exactly like the server
        // and client do; the movement-only hash would accept a different combat.toml.
        if (!sim::load_combat_beside(m_data_path, m_world.combat, error)) {
            ERR_PRINT(String("breakpoint: ") + String(error.c_str()));
            return false;
        }
        // M6.2: the weapon table is part of the join hash, so the bridge loads it
        // exactly like the server and the client do.
        if (!sim::load_weapons_beside(m_data_path, m_world.weapons, error)) {
            ERR_PRINT(String("breakpoint: ") + String(error.c_str()));
            return false;
        }
        m_world.collision.build(map);
        m_world.params_hash =
            sim::combined_params_hash(m_world.params, m_world.combat, m_world.weapons);

        sim::PlayerState player;
        player.pos = sim::Vec3(-16.0f, 0.0f, 0.05f);
        player.impulses = m_world.params.max_impulses;

        // Iron law A: the bridge must not convert units. map_parse does it once for
        // everything, including entity origins.
        for (const auto& entity : map.entities) {
            const auto it = entity.keys.find("classname");
            if (it == entity.keys.end() || it->second != "info_player_start") continue;
            sim::Vec3 spawn;
            if (sim::entity_origin_metres(entity, spawn)) player.pos = spawn;
        }

        if (const sim::WeaponDef* def = m_world.weapons.find_by_slot(0)) {
            sim::equip_weapon(player, *def);
        }
        m_world.players.clear();
        m_world.players.push_back(player);
        m_world.tick = 0;
        // The interpolation history starts at the spawn, not at (0,0,0): before
        // the first step the camera would otherwise blend in from the world origin.
        remember_previous();

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

    // M6: buttons travel as the sim's own bitmask (sim/input/buttons.h) instead of
    // one positional bool per action. Adding fire/reload/aim would otherwise have
    // grown this signature to thirteen arguments, and their order is invisible at
    // the GDScript call site.
    void set_input(float forward, float right, float yaw, float pitch, int buttons) {
        m_input = sim::input_from_buttons(static_cast<uint16_t>(buttons));
        m_input.forward = forward;
        m_input.right = right;
        m_input.yaw = yaw;
        m_input.pitch = pitch;
    }

    // Action name -> button bit, for the GDScript side to build the mask from the
    // same InputMap it registered. Actions the simulation never sees are omitted.
    Dictionary get_action_bits() const {
        Dictionary out;
        for (size_t i = 0; i < sim::kActionCount; ++i) {
            const sim::Action action = static_cast<sim::Action>(i);
            const uint16_t bit = sim::button_for_action(action);
            if (bit != 0) out[String(sim::action_name(action))] = static_cast<int>(bit);
        }
        return out;
    }

    void step() {
        // AUD-06: a network session never falls back to the local map world, even
        // when it is disconnected; only step() from solo play may advance it.
        if (m_net.active()) return;
        if (m_world.players.empty()) return;
        remember_previous();
        // step_single exists exactly for this: step({input}) would build a
        // one-element vector 64 times a second (iron law E).
        m_world.step_single(m_input, static_cast<float>(sim::tick_seconds()), &m_events);
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
        remember_previous();
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

    int get_snapshot_age() const { return m_net.snapshot_age(); }

    // Protocol v6: the real values now ride in PlayerState (and in the snapshot),
    // so the HUD shows replica data instead of a constant. Nothing damages them
    // yet; the weapon that does is M6 work.
    int get_hp() const {
        const sim::PlayerState* p = local_player();
        return p == nullptr ? 0 : p->vitals.health;
    }

    // Render interpolation: the simulation advances 64 times a second, a display
    // does not. The camera used to snap to the newest tick position and stay
    // there for two or three frames, which reads as a staircase while strafing
    // (and got louder once recoil started moving the view). These two give the
    // presentation layer the tick before the newest one to blend from.
    Vector3 get_prev_position() const { return m_prev_pos; }
    float get_prev_eye_height() const { return m_prev_eye; }
    int get_armor() const {
        const sim::PlayerState* p = local_player();
        return p == nullptr ? 0 : p->vitals.armor;
    }

    // 90% of the hull height, so crouching lowers the camera with the body. The
    // old constant 1.55 m was above the 1.42 m standing hull and did not move when
    // crouched, which let a crouched player see over cover.
    float get_eye_height() const {
        const sim::PlayerState* p = local_player();
        return eye_height_for(p != nullptr && p->crouched);
    }

    // AUD-08: the remote capsule has to match the collision hull, so it reads
    // these from movement.toml through the bridge instead of hard-coding them.
    float get_player_radius() const { return m_world.params.player_radius; }
    float get_stand_height() const { return m_world.params.stand_height; }
    float get_crouch_height() const { return m_world.params.crouch_height; }

    // AUD-13: the DLL carries the same build id CMake bakes into headless_sim,
    // so a log line says which commit actually loaded. The protocol version has
    // to match the other machine or HELLO is refused, so show it too.
    String get_build_id() const { return String(sim::kBuildId); }
    int get_protocol_version() const { return static_cast<int>(sim::kProtocolVersion); }

    // AUD-14: the LAN menu starts on the port sim/net/ports.h defines; GDScript
    // must not carry its own copy of the number.
    int get_default_signal_port() const { return static_cast<int>(sim::kDefaultSignalPort); }

    PackedVector3Array get_vertices() const { return m_vertices; }
    PackedVector3Array get_normals() const { return m_normals; }
    PackedColorArray get_colors() const { return m_colors; }
    PackedInt32Array get_indices() const { return m_indices; }

protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("load_map", "map_path", "data_path"), &SimWorld::load_map);
        ClassDB::bind_method(D_METHOD("load_bindings", "path"), &SimWorld::load_bindings);
        ClassDB::bind_method(D_METHOD("get_bindings"), &SimWorld::get_bindings);
        ClassDB::bind_method(D_METHOD("set_input", "forward", "right", "yaw", "pitch", "buttons"),
                             &SimWorld::set_input);
        ClassDB::bind_method(D_METHOD("get_action_bits"), &SimWorld::get_action_bits);
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
    ClassDB::bind_method(D_METHOD("get_prev_position"), &SimWorld::get_prev_position);
    ClassDB::bind_method(D_METHOD("get_prev_eye_height"), &SimWorld::get_prev_eye_height);
        ClassDB::bind_method(D_METHOD("get_player_radius"), &SimWorld::get_player_radius);
        ClassDB::bind_method(D_METHOD("get_stand_height"), &SimWorld::get_stand_height);
        ClassDB::bind_method(D_METHOD("get_crouch_height"), &SimWorld::get_crouch_height);
        ClassDB::bind_method(D_METHOD("get_build_id"), &SimWorld::get_build_id);
        ClassDB::bind_method(D_METHOD("get_protocol_version"), &SimWorld::get_protocol_version);
        ClassDB::bind_method(D_METHOD("get_default_signal_port"), &SimWorld::get_default_signal_port);
        ClassDB::bind_method(D_METHOD("get_vertices"), &SimWorld::get_vertices);
        ClassDB::bind_method(D_METHOD("get_normals"), &SimWorld::get_normals);
        ClassDB::bind_method(D_METHOD("get_colors"), &SimWorld::get_colors);
        ClassDB::bind_method(D_METHOD("get_indices"), &SimWorld::get_indices);
    }

private:
    // 90% of the hull height, so crouching lowers the camera with the body.
    float eye_height_for(bool crouched) const {
        // One source for "where the eye is" (pmove.h): shots start there too.
        const float height = crouched ? m_world.params.crouch_height : m_world.params.stand_height;
        return height * sim::kEyeHeightScale;
    }

    // Called before every step: the tick that is about to be simulated becomes
    // the "previous" one for interpolation.
    void remember_previous() {
        const sim::PlayerState* p = local_player();
        if (p == nullptr) return;
        m_prev_pos = bridge::to_godot(p->pos);
        m_prev_eye = eye_height_for(p->crouched);
    }

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
    Vector3 m_prev_pos;
    float m_prev_eye = 0.0f;
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
