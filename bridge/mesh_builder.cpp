#include "mesh_builder.h"

#include <cmath>

#include "mesh_common.h"
#include "sim/core/world.h"

namespace bridge {

void build_map_mesh(const sim::CollisionWorld& world, godot::PackedVector3Array& vertices,
                    godot::PackedVector3Array& normals, godot::PackedColorArray& colors,
                    godot::PackedInt32Array& indices) {
    vertices.clear();
    normals.clear();
    colors.clear();
    indices.clear();

    for (const sim::Brush& brush : world.brushes()) {
        const int base = material_color(brush.material);
        const float r = static_cast<float>((base >> 16) & 0xFF) / 255.0f;
        const float g = static_cast<float>((base >> 8) & 0xFF) / 255.0f;
        const float b = static_cast<float>(base & 0xFF) / 255.0f;

        for (const sim::BrushSide& side : brush.sides) {
            if (side.polygon.size() < 3) continue;

            const sim::Vec3 light(-0.45f, -0.35f, 0.82f);
            const float len = std::sqrt(light.x * light.x + light.y * light.y + light.z * light.z);
            const float lambert = std::max(
                0.0f, sim::dot(side.plane.normal, sim::Vec3(light.x / len, light.y / len, light.z / len)));
            const float shade = 0.62f + 0.38f * lambert;
            const godot::Color color(r * shade, g * shade, b * shade);

            const int32_t start = static_cast<int32_t>(vertices.size());
            const godot::Vector3 normal = to_godot(side.plane.normal);
            for (const sim::Vec3& v : side.polygon) {
                vertices.push_back(to_godot(v));
                normals.push_back(normal);
                colors.push_back(color);
            }
            for (size_t i = 1; i + 1 < side.polygon.size(); ++i) {
                indices.push_back(start);
                indices.push_back(start + static_cast<int32_t>(i));
                indices.push_back(start + static_cast<int32_t>(i + 1));
            }
        }
    }
}

}  // namespace bridge
