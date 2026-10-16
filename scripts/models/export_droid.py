"""Export the artist's jet droid from Blender into the game: assets/models/droid.mesh + droid.jpg + droid_n.jpg.

Run inside Blender with the droid scene open (the object names below), e.g. through the Blender MCP, or:

    blender --background droid.blend --python scripts/models/export_droid.py

It only reads the scene: the body is decimated on a temporary copy that is deleted afterwards.

What it writes (all in the game's space: metres, y up, the droid's nose facing -x, centred on its body):
  parts   body (textured), eye (the body's eye-glow faces, textured + glowing), fan_l / fan_r (textured, each around its
          own pivot with the spin axis and rate from the Blender animation), shroud / backplate (the jets' plain dark
          metal), flame (one exhaust flame, unit length along +x from the nozzle, v = 0 at the nozzle .. 1 at the tip)
  anchors eye, exhaust_l, exhaust_r (position, direction, the flame's length and radius, the core's)
The Blender animation is procedural (cycled fan spins, noise on the flames' scale and the glow lights), so the game
replays it (Game::emitDroid) rather than reading keyframes: the fan rates and flame sizes come from the actions here.

Mesh format (src/core/DroidModel.cpp), little-endian: "CSDR", u32 1, u32 parts, then per part: u8 name length, name,
u32 flags (1 textured, 2 glowing), f32 colour rgb, metal, rough, glow, pivot xyz, axis xyz, spin rad/s, u32 vertex
count, u32 index count, vertices (pos xyz, normal xyz, uv: 8 f32), u32 indices; then u32 anchors, per anchor: u8 name
length, name, f32 position xyz, direction xyz, size abcd.
"""
import math
import os
import struct

import bmesh
import bpy
import mathutils

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")) if "__file__" in globals() else ""
OUT_DIR = os.path.join(ROOT, "assets", "models")
BODY_TRIS = 24000        # the body is ~100k triangles in Blender: a mobile-friendly reduction
TEX = 1024               # texture size in the game (from the 2048 px originals)


def out_dir():
    return OUT_DIR


def world_to_game():
    """Blender world (z up, nose towards the eye) -> game (y up, nose -x), centred on the body."""
    body = bpy.data.objects["geometry_0"]
    fwd = -(body.matrix_world.to_3x3() @ mathutils.Vector((0, 1, 0)))  # the body's -y: from the jets to the eye
    fwd.z = 0
    fwd.normalize()
    side = mathutils.Vector((0, 0, 1)).cross(fwd)  # the droid's left
    rot = mathutils.Matrix(((-fwd.x, -fwd.y, 0), (0, 0, 1), (side.x, side.y, 0)))
    dg = bpy.context.evaluated_depsgraph_get()
    ev = body.evaluated_get(dg)
    pts = [body.matrix_world @ mathutils.Vector(c) for c in ev.bound_box]
    centre = sum(pts, mathutils.Vector()) / len(pts)
    return rot, centre


def to_game(rot, centre, p):
    return rot @ (mathutils.Vector(p) - centre)


def mesh_triangles(obj, rot, centre, origin=None, material=None, uv_fn=None):
    """Triangles of `obj`'s evaluated mesh in game space (relative to `origin` if given), welded."""
    dg = bpy.context.evaluated_depsgraph_get()
    ev = obj.evaluated_get(dg)
    me = ev.to_mesh()
    try:
        me.calc_loop_triangles()
        mw = obj.matrix_world
        nmat = mw.to_3x3().inverted_safe().transposed()
        uvl = me.uv_layers.active.data if me.uv_layers.active else None
        normals = me.corner_normals if hasattr(me, "corner_normals") else None
        verts, idx, seen = [], [], {}
        for tri in me.loop_triangles:
            if material is not None and tri.material_index != material:
                continue
            for li in tri.loops:
                v = me.vertices[me.loops[li].vertex_index]
                p = to_game(rot, centre, mw @ v.co)
                if origin is not None:
                    p = p - origin
                n = normals[li].vector if normals is not None else v.normal
                n = (rot @ (nmat @ n)).normalized()
                uv = tuple(uvl[li].uv) if uvl else (uv_fn(v.co) if uv_fn else (0.0, 0.0))
                key = (round(p.x, 5), round(p.y, 5), round(p.z, 5), round(n.x, 3), round(n.y, 3), round(n.z, 3),
                       round(uv[0], 5), round(uv[1], 5))
                i = seen.get(key)
                if i is None:
                    i = seen[key] = len(verts)
                    verts.append((p.x, p.y, p.z, n.x, n.y, n.z, uv[0], 1.0 - uv[1]))
                idx.append(i)
        return verts, idx
    finally:
        ev.to_mesh_clear()


def decimated_body():
    """A temporary, decimated copy of the body (deleted by the caller)."""
    body = bpy.data.objects["geometry_0"]
    tmp = body.copy()
    tmp.data = body.data.copy()
    tmp.name = "_droid_export_tmp"
    bpy.context.scene.collection.objects.link(tmp)
    tris = sum(len(p.vertices) - 2 for p in tmp.data.polygons)
    mod = tmp.modifiers.new("decimate", "DECIMATE")
    mod.ratio = min(1.0, BODY_TRIS / max(1, tris))
    bpy.context.view_layer.update()
    return tmp


def principled(mat_name):
    b = bpy.data.materials[mat_name].node_tree.nodes.get("Principled BSDF")
    c = b.inputs["Base Color"].default_value
    return (c[0], c[1], c[2]), b.inputs["Metallic"].default_value, b.inputs["Roughness"].default_value


def texture_images(mat_name):
    """(albedo, normal map) images of the body's material, found by what they feed."""
    nt = bpy.data.materials[mat_name].node_tree
    albedo = normal = None
    for link in nt.links:
        if link.from_node.type != "TEX_IMAGE":
            continue
        if link.to_node.type == "BSDF_PRINCIPLED" and link.to_socket.name == "Base Color":
            albedo = link.from_node.image
        elif link.to_node.type == "NORMAL_MAP":
            normal = link.from_node.image
    return albedo, normal


def save_jpeg(img, path):
    copy = img.copy()
    try:
        copy.scale(TEX, TEX)
        scene = bpy.context.scene
        fmt, view = scene.render.image_settings, scene.view_settings
        old = (fmt.file_format, fmt.quality, fmt.color_mode, view.view_transform, view.look)
        # the texture's own pixels: the Standard view (AgX or Filmic would tone-map and desaturate them)
        fmt.file_format, fmt.quality, fmt.color_mode = "JPEG", 90, "RGB"
        view.view_transform, view.look = "Standard", "None"
        try:
            copy.save_render(path, scene=scene)
        finally:
            fmt.file_format, fmt.quality, fmt.color_mode, view.view_transform, view.look = old
    finally:
        bpy.data.images.remove(copy)


def spin_rate(obj_name):
    """Radians per second of a cycled rotation_euler[1] action (one turn per N frames)."""
    act = bpy.data.objects[obj_name].animation_data.action
    for layer in act.layers:
        for strip in layer.strips:
            for cb in strip.channelbags:
                for f in cb.fcurves:
                    if f.data_path == "rotation_euler" and f.array_index == 1 and len(f.keyframe_points) >= 2:
                        k0, k1 = f.keyframe_points[0].co, f.keyframe_points[-1].co
                        return (k1[1] - k0[1]) / (k1[0] - k0[0]) * bpy.context.scene.render.fps
    return 0.0


def rest_scale(obj_name):
    """The flame's base scale (the action's keyed value before its noise)."""
    act = bpy.data.objects[obj_name].animation_data.action
    s = [1.0, 1.0, 1.0]
    for layer in act.layers:
        for strip in layer.strips:
            for cb in strip.channelbags:
                for f in cb.fcurves:
                    if f.data_path == "scale" and f.keyframe_points:
                        s[f.array_index] = f.keyframe_points[0].co[1]
    return s


def export():
    rot, centre = world_to_game()
    parts, anchors = [], []

    tmp = decimated_body()
    try:
        body_mat = bpy.data.objects["geometry_0"].data.materials
        eye_index = [i for i, m in enumerate(body_mat) if m and m.name == "Droid_Eye_Glow"]
        for name, mi, flags, glow in (("body", 0, 1, 0.6), ("eye", eye_index[0] if eye_index else -1, 3, 2.5)):
            if mi < 0:
                continue
            v, i = mesh_triangles(tmp, rot, centre, material=mi)
            parts.append(dict(name=name, flags=flags, color=(1, 1, 1), metal=0.35, rough=0.45, glow=glow,
                              pivot=(0, 0, 0), axis=(1, 0, 0), spin=0.0, verts=v, idx=i))
    finally:
        mesh = tmp.data
        bpy.data.objects.remove(tmp)
        bpy.data.meshes.remove(mesh)

    for side, obj in (("l", "Jet_Fan_L"), ("r", "Jet_Fan_R")):
        o = bpy.data.objects[obj]
        pivot = to_game(rot, centre, o.matrix_world.translation)
        axis = (rot @ (bpy.data.objects["Jet_Pivot_" + side.upper()].matrix_world.to_3x3() @ mathutils.Vector((0, 1, 0)))).normalized()
        v, i = mesh_triangles(o, rot, centre, origin=pivot)
        parts.append(dict(name="fan_" + side, flags=1, color=(1, 1, 1), metal=0.5, rough=0.4, glow=0.4,
                          pivot=tuple(pivot), axis=tuple(axis), spin=spin_rate(obj), verts=v, idx=i))

    for name, objs in (("shroud", ("Jet_Shroud_L", "Jet_Shroud_R")), ("backplate", ("Jet_Backplate_L", "Jet_Backplate_R"))):
        col, metal, rough = principled(objs[0])
        v, i = [], []
        for obj in objs:
            vv, ii = mesh_triangles(bpy.data.objects[obj], rot, centre)
            i += [k + len(v) for k in ii]
            v += vv
        parts.append(dict(name=name, flags=0, color=col, metal=metal, rough=rough, glow=0.0,
                          pivot=(0, 0, 0), axis=(1, 0, 0), spin=0.0, verts=v, idx=i))

    # one flame, unit length along +x from the nozzle, unit radius: the game scales and flickers it per exhaust
    flame = bpy.data.objects["Jet_Flame_L"]
    me = flame.data
    ys = [v.co.y for v in me.vertices]
    y0, y1 = min(ys), max(ys)
    rs = max(max(abs(v.co.x), abs(v.co.z)) for v in me.vertices) or 1.0
    me.calc_loop_triangles()
    fv, fi, seen = [], [], {}
    for tri in me.loop_triangles:
        for li in tri.loops:
            v = me.vertices[me.loops[li].vertex_index]
            t = (v.co.y - y0) / (y1 - y0)
            n = mathutils.Vector((v.normal.y, v.normal.z, v.normal.x)).normalized()
            key = (round(t, 5), round(v.co.z / rs, 5), round(v.co.x / rs, 5))
            k = seen.get(key)
            if k is None:
                k = seen[key] = len(fv)
                fv.append((t, v.co.z / rs, v.co.x / rs, n.x, n.y, n.z, 0.5, t))
            fi.append(k)
    parts.append(dict(name="flame", flags=2, color=(1, 0.03, 0.7), metal=0, rough=1, glow=2.6,
                      pivot=(0, 0, 0), axis=(1, 0, 0), spin=0.0, verts=fv, idx=fi))

    # anchors: the eye, and each exhaust with its flame's and core's length and radius (unit flame * base scale)
    eye = bpy.data.objects["Droid_Eye_Light"]
    anchors.append(("eye", to_game(rot, centre, eye.matrix_world.translation), mathutils.Vector((-1, 0, 0)), (0.12, 0, 0, 0)))
    fs, cs = rest_scale("Jet_Flame_L"), rest_scale("Jet_FlameCore_L")
    span = y1 - y0
    for side in ("l", "r"):
        ex = bpy.data.objects["Jet_Exhaust_" + side.upper()]
        pos = to_game(rot, centre, ex.matrix_world.translation)
        d = (rot @ (ex.matrix_world.to_3x3() @ mathutils.Vector((0, 1, 0)))).normalized()
        anchors.append(("exhaust_" + side, pos, d, (fs[1] * span, fs[0] * rs, cs[1] * span, cs[0] * rs)))

    os.makedirs(out_dir(), exist_ok=True)
    path = os.path.join(out_dir(), "droid.mesh")
    with open(path, "wb") as f:
        f.write(b"CSDR" + struct.pack("<II", 1, len(parts)))
        for p in parts:
            nb = p["name"].encode()
            f.write(struct.pack("<B", len(nb)) + nb)
            f.write(struct.pack("<I", p["flags"]))
            f.write(struct.pack("<6f", *p["color"], p["metal"], p["rough"], p["glow"]))
            f.write(struct.pack("<7f", *p["pivot"], *p["axis"], p["spin"]))
            f.write(struct.pack("<II", len(p["verts"]), len(p["idx"])))
            for v in p["verts"]:
                f.write(struct.pack("<8f", *v))
            f.write(struct.pack("<%dI" % len(p["idx"]), *p["idx"]))
        f.write(struct.pack("<I", len(anchors)))
        for name, pos, d, size in anchors:
            nb = name.encode()
            f.write(struct.pack("<B", len(nb)) + nb)
            f.write(struct.pack("<10f", *pos, *d, *size))

    albedo, normal = texture_images(bpy.data.objects["geometry_0"].data.materials[0].name)
    save_jpeg(albedo, os.path.join(out_dir(), "droid.jpg"))
    if normal:
        save_jpeg(normal, os.path.join(out_dir(), "droid_n.jpg"))
    return {"parts": {p["name"]: [len(p["verts"]), len(p["idx"]) // 3] for p in parts},
            "anchors": {a[0]: [round(c, 3) for c in a[1]] + [round(c, 3) for c in a[3]] for a in anchors},
            "spin": [p["spin"] for p in parts if p["name"].startswith("fan")], "file": path}


if __name__ == "__main__":
    print(export())
