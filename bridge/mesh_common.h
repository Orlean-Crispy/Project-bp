#pragma once

// Presentation helpers shared by the GDExtension bridge and the mesh builder.
// Pure functions: no state, no socket, no world. Keeping them here means the
// builder can be extracted without dragging the whole bridge class along.

#include <string>

#include <godot_cpp/variant/vector3.hpp>

#include "sim/core/world.h"

namespace bridge {

// Godot is Y-up, the simulation is Z-up (Quake convention).
inline godot::Vector3 to_godot(const sim::Vec3& v) { return godot::Vector3(v.x, v.z, -v.y); }

inline int material_color(const std::string& name) {
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

}  // namespace bridge
