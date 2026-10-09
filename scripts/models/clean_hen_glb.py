#!/usr/bin/env python3
"""Clean the AI-generated rigged hen (three.js GLTFExporter output) into assets/models/hen_classic.glb.

    python3 -m pip install numpy
    python3 scripts/models/clean_hen_glb.py [in.glb] [out.glb]

What was wrong with the source:
  - the generator turned the background of its source image into geometry: a flat two-sided panel (~32k of the 48k
    triangles) about 0.17 units behind the hen, textured with the noisy grey/confetti part of the atlas. That is
    the "wall of gibberish". Every triangle lying entirely in that sheet (z < -0.163) is removed;
  - the hen was glued to that panel, so removing it leaves one small hole low on its back: it is closed with a
    slightly domed fan (body-coloured UV, normals facing out, skin weights copied from the rim);
  - unused vertices are dropped and every buffer is rebuilt; meshes, skin, bones and textures are otherwise unchanged.
"""
import json
import struct
import sys
from collections import defaultdict

import numpy as np

SRC = sys.argv[1] if len(sys.argv) > 1 else "assets/models/model-rigged.glb"
DST = sys.argv[2] if len(sys.argv) > 2 else "assets/models/hen_classic.glb"
WALL_Z = -0.163

CT = {5121: np.uint8, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}
NC = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def load(path):
    b = open(path, "rb").read()
    jl = struct.unpack("<I", b[12:16])[0]
    j = json.loads(b[20:20 + jl])
    bl = struct.unpack("<I", b[20 + jl:24 + jl])[0]
    return j, b[28 + jl:28 + jl + bl]


def accessor(j, bin_, i):
    a = j["accessors"][i]
    bv = j["bufferViews"][a["bufferView"]]
    dt = np.dtype(CT[a["componentType"]])
    n = NC[a["type"]]
    off = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
    stride = bv.get("byteStride", n * dt.itemsize)
    raw = np.frombuffer(bin_, np.uint8, a["count"] * stride, off).reshape(a["count"], stride)[:, :n * dt.itemsize]
    return np.ascontiguousarray(raw).view(dt).reshape(a["count"], n)


def boundary_loops(P, T):
    """Open edges (welded by position, so UV seams don't count), chained into loops of original vertex ids."""
    key = np.round(P / 1e-5).astype(np.int64)
    weld = np.unique(key, axis=0, return_inverse=True)[1].ravel()
    count, directed = defaultdict(int), {}
    for t in T:
        for k in range(3):
            a, b = t[k], t[(k + 1) % 3]
            u, v = weld[a], weld[b]
            count[(min(u, v), max(u, v))] += 1
            directed[(u, v)] = (a, b)
    nxt = {u: (v, a) for (u, v), (a, b) in directed.items() if count[(min(u, v), max(u, v))] == 1}
    loops, seen = [], set()
    for s in nxt:
        loop, u = [], s
        while u not in seen and u in nxt:
            seen.add(u)
            v, a = nxt[u]
            loop.append(a)
            u = v
        if len(loop) >= 8:  # real holes; 1-2 edge slivers are left alone
            loops.append(loop)
    return loops


def main():
    j, bin_ = load(SRC)
    prim = j["meshes"][0]["primitives"][0]
    att = prim["attributes"]
    P, N, UV = (accessor(j, bin_, att[k]).copy() for k in ("POSITION", "NORMAL", "TEXCOORD_0"))
    J, W = accessor(j, bin_, att["JOINTS_0"]).copy(), accessor(j, bin_, att["WEIGHTS_0"]).copy()
    T = accessor(j, bin_, prim["indices"]).reshape(-1, 3).astype(np.int64)

    wall = (P[T][:, :, 2] < WALL_Z).all(1)
    T = T[~wall]
    print(f"removed {wall.sum()} background triangles, kept {len(T)}")

    for loop in boundary_loops(P, T):
        rim = P[loop]
        c = rim.mean(0)
        c[2] -= 0.35 * (rim[:, :2].max(0) - rim[:, :2].min(0)).mean() / 2  # a gentle dome outward (-z)
        uv_c = UV[loop][np.argmin(np.linalg.norm(UV[loop] - UV[loop].mean(0), axis=1))]
        near = loop[int(np.argmin(np.linalg.norm(rim - c, axis=1)))]
        ci = len(P)
        P = np.vstack([P, c]); N = np.vstack([N, [0, 0, -1]]); UV = np.vstack([UV, uv_c])
        J = np.vstack([J, J[near]]); W = np.vstack([W, W[near]])
        cap = [(loop[(k + 1) % len(loop)], loop[k], ci) for k in range(len(loop))]  # reversed rim winding
        # rim vertices keep their own UVs; the centre takes the most central rim UV, so the cap stays body-coloured
        T = np.vstack([T, cap])
        print(f"closed a {len(loop)}-edge hole on the back")

    used, remap = np.unique(T, return_inverse=True)
    T = remap.reshape(-1, 3).astype(np.uint32)
    P, N, UV, J, W = P[used], N[used], UV[used], J[used], W[used]
    W = W / W.sum(1, keepdims=True)

    # rebuild the binary chunk: vertex streams, indices, then the untouched inverse-bind matrices and images
    out, views = bytearray(), []

    def add(data, target=None, stride=None):
        while len(out) % 4: out.append(0)
        v = {"buffer": 0, "byteOffset": len(out), "byteLength": len(data)}
        if target: v["target"] = target
        if stride: v["byteStride"] = stride
        out.extend(data)
        views.append(v)
        return len(views) - 1

    def blob(bv_index):
        bv = j["bufferViews"][bv_index]
        return bin_[bv.get("byteOffset", 0):bv.get("byteOffset", 0) + bv["byteLength"]]

    acc = []

    def accessor_for(arr, ctype, typ, target, stride=None, minmax=False):
        a = {"bufferView": add(arr.tobytes(), target, stride), "componentType": ctype, "count": len(arr), "type": typ}
        if minmax:
            a["min"], a["max"] = arr.min(0).tolist(), arr.max(0).tolist()
        acc.append(a)
        return len(acc) - 1

    attrs = {
        "POSITION": accessor_for(P.astype(np.float32), 5126, "VEC3", 34962, 12, True),
        "NORMAL": accessor_for(N.astype(np.float32), 5126, "VEC3", 34962, 12),
        "TEXCOORD_0": accessor_for(UV.astype(np.float32), 5126, "VEC2", 34962, 8),
        "JOINTS_0": accessor_for(J.astype(np.uint16), 5123, "VEC4", 34962, 8),
        "WEIGHTS_0": accessor_for(W.astype(np.float32), 5126, "VEC4", 34962, 16),
    }
    idx = accessor_for(T.reshape(-1, 1), 5125, "SCALAR", 34963)
    acc[idx]["min"], acc[idx]["max"] = [int(T.min())], [int(T.max())]
    ibm_old = j["accessors"][j["skins"][0]["inverseBindMatrices"]]
    acc.append({**{k: v for k, v in ibm_old.items() if k != "bufferView"}, "bufferView": add(blob(ibm_old["bufferView"]))})
    j["skins"][0]["inverseBindMatrices"] = len(acc) - 1
    for im in j["images"]:
        im["bufferView"] = add(blob(im["bufferView"]))
    while len(out) % 4: out.append(0)

    prim["attributes"], prim["indices"] = attrs, idx
    j["accessors"], j["bufferViews"] = acc, views
    j["buffers"] = [{"byteLength": len(out)}]
    j["asset"] = {"version": "2.0", "generator": "CuckooStack clean_hen_glb.py (from THREE.GLTFExporter r172)"}

    js = json.dumps(j, separators=(",", ":")).encode()
    js += b" " * (-len(js) % 4)
    total = 12 + 8 + len(js) + 8 + len(out)
    with open(DST, "wb") as f:
        f.write(struct.pack("<4sII", b"glTF", 2, total))
        f.write(struct.pack("<I4s", len(js), b"JSON") + js)
        f.write(struct.pack("<I4s", len(out), b"BIN\0") + bytes(out))
    print(f"wrote {DST}: {len(P)} vertices, {len(T)} triangles, {total / 1e6:.1f} MB")


if __name__ == "__main__":
    main()
