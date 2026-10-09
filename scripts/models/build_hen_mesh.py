#!/usr/bin/env python3
"""Turn the artist's hen model (assets/models/source/newClassicHen.glb) into the game's runtime mesh,
assets/models/hen.mesh, embedded into the build and read by src/core/HenModel.cpp.

    python3 -m pip install numpy pillow
    python3 scripts/models/build_hen_mesh.py

What it does:
  - places the model in the game hen's body space (Hen.cpp): front (+z in the model) turned to +x, scaled so it
    stands from the top of the robot legs (y 0.2) to just above where the old comb reached;
  - splits it into colour regions read from its base-colour texture (per triangle, at the triangle's centre), so
    every outfit can recolour it with its own materials: Body, Accent (comb, wattle, goggle frame), Lens (glowing
    goggle glass), Shade (the lens's dark band) and Beak;
  - auto-rigs it: weights for the game's own animated nodes (body, head, the two wings, tail), with soft falloffs
    so the mesh bends instead of cracking. The source rig's 5 bones are ignored (one bone carried 86% of the mesh and
    the others only drive the tail).

  - pre-cuts it into glass shards for the Glass Shatter crash effect: ~SHARDS chunky pieces (surface Voronoi cells
    around farthest-point seeds); vertices on a cut are duplicated so every triangle belongs to exactly one shard.
    The rest pose is unchanged (duplicates coincide); on a crash each shard flies off as a rigid piece (Hen::shatter).

Format (little endian): "CSHN", u32 version (2), u32 region count, u32 shard count, then per region: u32 vertex
count, u32 index count, vertices (pos xyz, normal xyz, weights body/head/wing0/wing1/tail, shard id: 12 floats),
u16 indices.
"""
import colorsys
import io
import json
import os
import struct

import numpy as np
from PIL import Image


ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SRC = os.path.join(ROOT, "assets", "models", "source", "newClassicHen.glb")
DST = os.path.join(ROOT, "assets", "models", "hen.mesh")
REGIONS = ["Body", "Accent", "Lens", "Shade", "Beak"]  # order = HenModel::Region
SHARDS = 40


CT = {5121: np.uint8, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}
NC = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def load(path):
    """A .glb's JSON and binary chunk."""
    b = open(path, "rb").read()
    jl = struct.unpack("<I", b[12:16])[0]
    j = json.loads(b[20:20 + jl])
    bl = struct.unpack("<I", b[20 + jl:24 + jl])[0]
    return j, b[28 + jl:28 + jl + bl]


def accessor(j, bin_, i):
    """A glTF accessor as an (count, components) array, honouring byte strides."""
    a = j["accessors"][i]
    bv = j["bufferViews"][a["bufferView"]]
    dt = np.dtype(CT[a["componentType"]])
    n = NC[a["type"]]
    off = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
    stride = bv.get("byteStride", n * dt.itemsize)
    raw = np.frombuffer(bin_, np.uint8, a["count"] * stride, off).reshape(a["count"], stride)[:, :n * dt.itemsize]
    return np.ascontiguousarray(raw).view(dt).reshape(a["count"], n)


def smooth(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def classify(rgb):
    """Colour region of each sample (N x 3, 0..1)."""
    hsv = np.array([colorsys.rgb_to_hsv(*c) for c in rgb])
    h, s, v = hsv[:, 0] * 360, hsv[:, 1], hsv[:, 2]
    out = np.zeros(len(rgb), np.int32)                                                  # Body
    out[((h > 290) | (h < 12)) & (s > 0.38) & (v > 0.3)] = 1                           # Accent
    out[(h > 15) & (h < 50) & (s > 0.45) & (v > 0.4)] = 4                               # Beak
    out[v < 0.32] = 3                                                                   # Shade
    out[(h > 165) & (h < 215) & (s > 0.35) & (v > 0.35)] = 2                            # Lens
    return out


def main():
    j, bin_ = load(SRC)
    prim = j["meshes"][0]["primitives"][0]
    att = prim["attributes"]
    P = accessor(j, bin_, att["POSITION"]).astype(np.float64)
    N = accessor(j, bin_, att["NORMAL"]).astype(np.float64)
    UV = accessor(j, bin_, att["TEXCOORD_0"]).astype(np.float64)
    T = accessor(j, bin_, prim["indices"]).reshape(-1, 3).astype(np.int64)
    bv = j["bufferViews"][j["images"][j["textures"][j["materials"][0]["pbrMetallicRoughness"]["baseColorTexture"]["index"]]["source"]]["bufferView"]]
    tex = np.asarray(Image.open(io.BytesIO(bin_[bv.get("byteOffset", 0):bv.get("byteOffset", 0) + bv["byteLength"]])).convert("RGB")) / 255.0
    th, tw = tex.shape[:2]

    # ---- auto-rig, in the model's own axes (x: hen's left/right, y: up, z: front)
    x, y, z = P[:, 0], P[:, 1], P[:, 2]
    # wings: the side lobes (|x| past the body's surface, low and toward the front); tail: everything behind the
    # body; head: the upper half of the round body, where the goggles, comb and beak are
    wing = smooth(0.185, 0.21, np.abs(x)) * smooth(-0.24, -0.2, y) * (1 - smooth(-0.02, 0.03, y)) * smooth(-0.03, 0.02, z) * (1 - smooth(0.3, 0.34, z))
    tail = smooth(-0.04, -0.13, z)
    head = smooth(-0.01, 0.09, y) * (1 - wing) * (1 - tail)
    wing0 = np.where(x > 0, wing, 0.0)  # game wing 0 sits at -z, which is the model's +x after the turn below
    wing1 = np.where(x <= 0, wing, 0.0)
    body = np.clip(1 - head - wing0 - wing1 - tail, 0, 1)
    Wt = np.stack([body, head, wing0, wing1, tail], 1)
    Wt /= Wt.sum(1, keepdims=True)

    # ---- into the game's body space: turn +90 deg about y (front +z -> +x), scale, stand on the legs
    # sized so the round body matches the old hen (~1.15 tall); the long tail streams out behind
    s = 2.2
    Pg = np.stack([(z - 0.2) * s + 0.05, y * s + (0.2 - y.min() * s), -x * s], 1)
    Ng = np.stack([N[:, 2], N[:, 1], -N[:, 0]], 1)

    # ---- colour regions per triangle, from the base colour at the triangle's UV centroid
    cuv = UV[T].mean(1)
    rgb = tex[np.clip((cuv[:, 1] * th).astype(int), 0, th - 1), np.clip((cuv[:, 0] * tw).astype(int), 0, tw - 1)]
    region = classify(rgb)

    # ---- glass shards: farthest-point seeds over the triangle centres, each triangle joins its nearest seed
    cen = Pg[T].mean(1)
    rng = np.random.default_rng(7)
    seeds = [cen[rng.integers(len(cen))]]
    d = np.linalg.norm(cen - seeds[0], axis=1)
    for _ in range(SHARDS - 1):
        seeds.append(cen[int(np.argmax(d))])
        d = np.minimum(d, np.linalg.norm(cen - seeds[-1], axis=1))
    seeds = np.array(seeds)
    jitter = cen + rng.normal(0, 0.02, cen.shape)  # slightly ragged cut lines, like broken glass
    shard = np.argmin(np.linalg.norm(jitter[:, None, :] - seeds[None, :, :], axis=2), axis=1)

    out = bytearray(b"CSHN" + struct.pack("<III", 2, len(REGIONS), SHARDS))
    for r, name in enumerate(REGIONS):
        sel = region == r
        tris, tshard = T[sel], shard[sel]
        # one vertex per (source vertex, shard): a vertex shared by two shards is split along the crack
        key = tris.astype(np.int64) * SHARDS + tshard[:, None]
        used, remap = np.unique(key, return_inverse=True)
        src, sid = used // SHARDS, used % SHARDS
        verts = np.hstack([Pg[src], Ng[src], Wt[src], sid[:, None]]).astype(np.float32)
        idx = remap.reshape(-1).astype(np.uint16)
        out += struct.pack("<II", len(verts), idx.size) + verts.tobytes() + idx.tobytes()
        print(f"{name:7s} {len(verts):6d} vertices {len(tris):6d} triangles")
    open(DST, "wb").write(out)
    lo, hi = Pg.min(0), Pg.max(0)
    print(f"wrote {DST} ({len(out) / 1e6:.2f} MB); body-space bounds {lo.round(2)} .. {hi.round(2)}")


if __name__ == "__main__":
    main()
