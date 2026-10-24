# bp_export_map.py - export Blender objects as Project Bp .map brushes.
#
# Why this exists: the simulation reads .map brush geometry directly (no BSP, no
# mesh collider). Blender can model boxes and convex prisms quickly, so this adds
# the one missing arrow: Blender -> .map.
#
# Conventions that matter (they are not negotiable):
#   * Blender is Z-up and so is the simulation, so no axis swap.
#   * .map stores Quake units; the parser multiplies by 0.0254. Blender works in
#     metres, so every coordinate is divided by 0.0254 on the way out.
#   * A brush is a CONVEX solid. Each object in the collection becomes one brush;
#     non-convex objects are refused loudly instead of producing a broken map.
#   * Quake lists three points per plane, clockwise seen from outside, because
#     make_plane() computes cross(a - b, c - b) and expects it to point out.
#
# Usage (headless):
#   blender --background --python tools/blender/bp_export_map.py -- out.map
#   blender --background --python tools/blender/bp_export_map.py -- --selftest out.map
#
# Usage (inside Blender): Text editor -> Run Script, or install as an addon and
# use File > Export > Project Bp Map.

import math
import os
import sys

try:
    import bpy
    from mathutils import Vector
except ImportError:  # pragma: no cover - only when run outside Blender
    bpy = None

UNITS_PER_METRE = 1.0 / 0.0254

# The nine material names the renderer knows (bridge/mesh_common.h). Anything
# else would render as the default grey, so unknown names are rejected.
KNOWN_SURFACES = [
    "FLOOR", "CEILING", "WALL", "THINWALL", "PLATFORM",
    "STEP", "LOWCEIL", "METAL", "CONCRETE",
]


def surface_of(obj):
    name = obj.get("surface")
    if not name and obj.data.materials:
        name = obj.data.materials[0].name
    name = (name or "CONCRETE").upper()
    if name not in KNOWN_SURFACES:
        raise ValueError(
            "object '%s': unknown surface '%s' (use one of %s)"
            % (obj.name, name, ", ".join(KNOWN_SURFACES))
        )
    return name


def flags_of(obj):
    def flag(key):
        return 1 if int(obj.get(key, 0) or 0) else 0

    return (flag("climbable"), flag("wallrunnable"), flag("penetrable"))


def world_triangles(obj):
    """World-space triangles with their loop order preserved."""
    depsgraph = bpy.context.evaluated_depsgraph_get()
    evaluated = obj.evaluated_get(depsgraph)
    mesh = evaluated.to_mesh()
    matrix = obj.matrix_world
    try:
        verts = [matrix @ v.co for v in mesh.vertices]
        tris = []
        for poly in mesh.polygons:
            loop = list(poly.loop_indices)
            idx = [mesh.loops[i].vertex_index for i in loop]
            # Fan-triangulate: brushes are convex, so this stays inside the face.
            for k in range(1, len(idx) - 1):
                tris.append((idx[0], idx[k], idx[k + 1], poly.normal.copy()))
        return verts, tris
    finally:
        evaluated.to_mesh_clear()


def convex_planes(verts, tris, name):
    """One outward plane per distinct face; refuse non-convex input."""
    planes = []
    for a, b, c, normal in tris:
        pa, pb, pc = verts[a], verts[b], verts[c]
        edge1 = pb - pa
        edge2 = pc - pa
        cross = edge1.cross(edge2)
        if cross.length < 1e-9:
            continue
        n = cross.normalized()
        if n.dot(normal) < 0.0:
            n = -n
        d = n.dot(pa)
        # Deduplicate: a face split into several triangles shares one plane.
        duplicate = False
        for other_n, other_d in planes:
            if other_n.dot(n) > 0.999999 and abs(other_d - d) < 1e-4:
                duplicate = True
                break
        if not duplicate:
            planes.append((n, d))

    # Convexity: every vertex must be on or behind every plane.
    for n, d in planes:
        for v in verts:
            if n.dot(v) - d > 1e-3:
                raise ValueError(
                    "object '%s' is not convex: vertex %s is %.4f m outside a face plane"
                    % (name, tuple(round(x, 3) for x in v), n.dot(v) - d)
                )
    if len(planes) < 4:
        raise ValueError("object '%s' has fewer than 4 planes" % name)
    return planes


def plane_points(verts, plane_normal, surface):
    """Pick three vertices on the plane, wound clockwise seen from outside."""
    n, d = plane_normal
    on_plane = [v for v in verts if abs(n.dot(v) - d) < 1e-3]
    if len(on_plane) < 3:
        raise ValueError("plane has fewer than 3 vertices")
    a = on_plane[0]
    b = None
    c = None
    for candidate in on_plane[1:]:
        if (candidate - a).length > 1e-6:
            b = candidate
            break
    for candidate in on_plane[1:]:
        if b is None or candidate is b:
            continue
        if (candidate - a).cross(b - a).length > 1e-6:
            c = candidate
            break
    if b is None or c is None:
        raise ValueError("degenerate face")

    def wind(p, q, r):
        return (p - q).cross(r - q).dot(n)

    if wind(a, b, c) < 0.0:
        b, c = c, b

    def fmt(v):
        u = v * UNITS_PER_METRE
        return "( %.4f %.4f %.4f )" % (u.x + 0.0, u.y + 0.0, u.z + 0.0)

    return "%s %s %s %s 0 0 0 1 1" % (fmt(a), fmt(b), fmt(c), surface)


def brushes_of(objects):
    """Group brush text by flag tuple so shared keys go into one entity."""
    groups = {}
    for obj in objects:
        if obj.type != "MESH":
            continue
        surface = surface_of(obj)
        flags = flags_of(obj)
        verts, tris = world_triangles(obj)
        planes = convex_planes(verts, tris, obj.name)
        lines = [plane_points(verts, plane, surface) for plane in planes]
        groups.setdefault(flags, []).append((obj.name, lines))
    return groups


def emit_map(objects, message="blender"):
    groups = brushes_of(objects)
    out = []
    out.append("// Generated by tools/blender/bp_export_map.py - edit in Blender, not here.")
    out.append("{")
    out.append('"classname" "worldspawn"')
    out.append('"message" "%s"' % message)
    for _name, lines in groups.get((0, 0, 0), []):
        out.append("{")
        out.extend(lines)
        out.append("}")
    out.append("}")  # close worldspawn
    for flags, brushes in groups.items():
        if flags == (0, 0, 0):
            continue
        out.append("{")
        out.append('"classname" "func_brush"')
        if flags[0]:
            out.append('"_climbable" "1"')
        if flags[1]:
            out.append('"_wallrunnable" "1"')
        if flags[2]:
            out.append('"_penetrable" "1"')
        for _name, lines in brushes:
            out.append("{")
            out.extend(lines)
            out.append("}")
        out.append("}")
    return "\n".join(out) + "\n"


def map_objects():
    """The BP_MAP collection if it exists, otherwise every mesh in the scene."""
    collection = bpy.data.collections.get("BP_MAP")
    if collection is not None:
        return [obj for obj in collection.all_objects if obj.type == "MESH"]
    return [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]


def build_selftest_scene():
    """A box room with one platform: enough to prove the pipe, not a map."""
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj, do_unlink=True)

    def box(name, center, size, surface, **props):
        bpy.ops.mesh.primitive_cube_add(size=1.0, location=center)
        obj = bpy.context.active_object
        obj.name = name
        obj.scale = (size[0], size[1], size[2])
        bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
        obj["surface"] = surface
        for key, value in props.items():
            obj[key] = value
        return obj

    box("floor", (0, 0, -0.5), (40, 40, 1.0), "FLOOR")
    box("ceiling", (0, 0, 8.5), (40, 40, 1.0), "CEILING")
    box("wall_n", (0, 20.5, 4.0), (40, 1.0, 8.0), "WALL")
    box("wall_s", (0, -20.5, 4.0), (40, 1.0, 8.0), "WALL")
    box("wall_e", (20.5, 0, 4.0), (1.0, 40, 8.0), "WALL")
    box("wall_w", (-20.5, 0, 4.0), (1.0, 40, 8.0), "WALL")
    box("platform", (-6, 0, 0.75), (8, 6, 1.5), "PLATFORM")
    box("thin", (6, 0, 4.0), (0.17, 10, 8.0), "THINWALL", penetrable=1)
    box("climbbox", (12, 6, 4.0), (1.0, 8, 8.0), "WALL", climbable=1)


def main():
    argv = sys.argv
    args = argv[argv.index("--") + 1:] if "--" in argv else []
    if not args:
        raise SystemExit("usage: blender --background --python bp_export_map.py -- [--selftest] out.map")
    selftest = "--selftest" in args
    args = [a for a in args if a != "--selftest"]
    out_path = args[0]

    if selftest:
        build_selftest_scene()

    objects = map_objects()
    if not objects:
        raise SystemExit("no mesh objects to export")

    text = emit_map(objects)
    with open(out_path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)

    brushes = text.count("\n{\n(")
    print("bp_export: %d objects -> %s (%d brushes)" % (len(objects), out_path, brushes))


if __name__ == "__main__":
    main()
