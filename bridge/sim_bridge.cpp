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

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "sim/collision/map_parse.h"
#include "sim/core/event.h"
#include "sim/core/world.h"
#include "sim/input/bindings.h"
#include "sim/net/handshake.h"
#include "sim/net/session.h"
#include "sim/net/udp_hub.h"
#include "sim/net/udp_transport.h"

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
        if (m_world.players.empty()) return;
        m_world.step({m_input}, 1.0f / 64.0f, &m_events);
    }

    // ---- networking (host = listen server + local client, join = client) ----
    bool host(int port, int players) {
        if (m_net_client) return false;
        std::string error;
        m_server = std::make_unique<sim::NetServer>();
        if (!m_server->init(m_map_path, m_data_path, players, error)) {
            ERR_PRINT(String("breakpoint host: ") + String(error.c_str()));
            return false;
        }
        m_hub = std::make_unique<sim::UdpPeerHub>();
        if (!m_hub->open(static_cast<uint16_t>(port), error)) {
            ERR_PRINT(String("breakpoint host: ") + String(error.c_str()));
            return false;
        }
        m_local_to_server = std::make_unique<sim::LoopbackTransport>(sim::NetConditions{}, 1);
        m_local_to_client = std::make_unique<sim::LoopbackTransport>(sim::NetConditions{}, 2);
        m_server->add_client(m_local_to_server.get(), m_local_to_client.get(), 0);
        m_net_client = std::make_unique<sim::NetClient>();
        if (!m_net_client->init(m_local_to_server.get(), m_local_to_client.get(), 0, m_map_path,
                                m_data_path, error)) {
            ERR_PRINT(String("breakpoint host: ") + String(error.c_str()));
            m_net_client.reset();
            return false;
        }
        m_hosting = true;
        return true;
    }

    // The id the host vets over TCP (A5); the UDP hello repeats it. Until the
    // bridge runs the control plane, this stands in for it.
    static sim::ClientId make_client_id() {
        static uint64_t counter = 0;
        sim::ClientId id{};
        const uint64_t seed = static_cast<uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        for (std::size_t i = 0; i < id.size(); ++i) {
            id[i] = static_cast<uint8_t>((seed >> ((i % 8) * 8)) ^ (counter * 31u + i * 7u));
        }
        ++counter;
        return id;
    }

    bool join(const String& host, int port) {
        if (m_net_client) return false;
        std::string error;
        m_client_id = make_client_id();
        m_udp = std::make_unique<sim::UdpTransport>();
        if (!m_udp->open(0, error) ||
            !m_udp->connect_peer(host.utf8().get_data(), static_cast<uint16_t>(port), error)) {
            ERR_PRINT(String("breakpoint join: ") + String(error.c_str()));
            return false;
        }
        m_net_client = std::make_unique<sim::NetClient>();
        if (!m_net_client->init(m_udp.get(), m_udp.get(), 0, m_map_path, m_data_path, error)) {
            ERR_PRINT(String("breakpoint join: ") + String(error.c_str()));
            m_net_client.reset();
            return false;
        }
        m_udp->send(sim::make_hello(m_client_id));
        m_last_hello = std::chrono::steady_clock::now();
        return true;
    }

    void step_network() {
        if (!m_net_client) return;
        const auto now = std::chrono::steady_clock::now();
        if (m_hosting) {
            m_hub->poll();
            std::string peer_host;
            uint16_t peer_port = 0;
            std::vector<uint8_t> packet;
            while (m_hub->next_unknown(peer_host, peer_port, packet)) {
                if (!sim::is_hello(packet)) continue;
                sim::PeerTransport* peer = m_hub->add_peer(peer_host, peer_port);
                const int slot = m_server->add_client(peer, peer, -1);
                if (slot < 0) continue;  // all slots taken
                std::printf("breakpoint: peer joined slot %d\n", slot);
                std::fflush(stdout);
            }
            m_net_client->send_input(m_input);
            m_net_client->step_local();
            m_server->receive();
            m_server->step();
            m_server->send();
            m_local_to_server->advance(1);
            m_local_to_client->advance(1);
            m_net_client->receive();
        } else {
            if (!m_net_client->joined() &&
                std::chrono::duration<double>(now - m_last_hello).count() >= 0.25) {
                m_udp->send(sim::make_hello(m_client_id));
                m_last_hello = now;
            }
            m_net_client->send_input(m_input);
            m_net_client->step_local();
            m_net_client->receive();
        }
    }

    bool is_networked() const { return m_net_client != nullptr; }
    bool get_joined() const { return m_net_client != nullptr && m_net_client->joined(); }
    int get_player_slot() const { return m_net_client ? m_net_client->player_index() : 0; }
    int get_peer_count() const {
        if (m_hosting && m_server) return m_server->client_count();
        return (m_net_client && m_net_client->joined()) ? 1 : 0;
    }
    String get_role() const {
        if (!m_net_client) return "solo";
        return m_hosting ? "host" : "client";
    }

    Array get_remote_players() const {
        Array out;
        if (!m_net_client) return out;
        const uint32_t tick = m_net_client->tick();
        if (tick <= 1) return out;
        std::vector<sim::PlayerSnap> players;
        if (!m_net_client->remote().sample(tick - 1, 0.0f, players)) return out;
        const int self = m_net_client->player_index();
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
        if (m_net_client) return &m_net_client->predicted();
        return m_world.players.empty() ? nullptr : &m_world.players[0];
    }

    uint32_t get_tick() const {
        return m_net_client ? m_net_client->tick() : m_world.tick;
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

    std::string m_map_path;
    std::string m_data_path;
    std::unique_ptr<sim::NetServer> m_server;
    std::unique_ptr<sim::NetClient> m_net_client;
    std::unique_ptr<sim::UdpPeerHub> m_hub;
    std::unique_ptr<sim::UdpTransport> m_udp;
    std::unique_ptr<sim::LoopbackTransport> m_local_to_server;
    std::unique_ptr<sim::LoopbackTransport> m_local_to_client;
    bool m_hosting = false;
    sim::ClientId m_client_id{};
    std::chrono::steady_clock::time_point m_last_hello{};
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
