#pragma once

// Turns the collision world into a render mesh. Pure: it reads brushes and
// writes godot arrays, never touches the network or the simulation state.

#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

namespace sim {
class CollisionWorld;
}  // namespace sim

namespace bridge {

void build_map_mesh(const sim::CollisionWorld& world, godot::PackedVector3Array& vertices,
                    godot::PackedVector3Array& normals, godot::PackedColorArray& colors,
                    godot::PackedInt32Array& indices);

}  // namespace bridge
