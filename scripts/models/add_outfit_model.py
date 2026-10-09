#!/usr/bin/env python3
"""Give an outfit its own 3D hen model: one command from the artist's .glb to an in-game model.

    python3 -m pip install numpy scipy pillow fast-simplification xatlas open3d
    python3 scripts/models/add_outfit_model.py <model.glb> <outfit id> [look]   e.g.  magmaHen.glb hen_lava --glow 1
    python3 scripts/models/add_outfit_model.py --all                             rebuild every registered model
    python3 scripts/models/add_outfit_model.py --remove <outfit id>

then rebuild the game (re-run cmake / platforms/ios/generate_xcode.sh once after adding a new outfit, so the new files
are picked up). The outfit ids are the cosmetic ids in src/core/Economy.cpp (hen_lava = Magma, hen_ice = Cryo, ...).
The game finds the model by outfit id; nothing else needs editing.

What it does with the .glb (one mesh, one base-colour texture; any skeleton it has is ignored):
  1. copies it to assets/models/source/<outfit id>.glb (the source of truth for --all);
  2. reduces it to ~40k triangles (quadric simplification of the welded surface);
  3. lays out new UVs for the reduced mesh (xatlas) and bakes two 1024 px textures from the full-detail model: the
     colour (every texel averages 2 x 2 samples of the nearest points on the original surface) and a normal map (that
     point's surface direction, relative to the reduced surface), so sculpted detail - panel lines, raised circuit
     traces, cracks - still catches the light although the reduced mesh is lighter. Both are 2048 px;
  4. places it in the hen's body space (front turned to +x, scaled to ~1.25 tall, standing on the robot legs at y 0.2)
     and auto-rigs it to the game's body / head / wing / tail nodes, by position relative to its bounding box;
  5. pre-cuts it into 40 glass shards for the crash shatter;
  6. writes assets/models/outfits/<outfit id>.mesh + .jpg + _n.jpg (normal map), regenerates src/core/OutfitModels.inc (the list the game
     embeds), and renders a check image, assets/models/outfits/<outfit id>_preview.png: four views with the texture,
     and two showing the rig (body grey, head yellow, wings green / blue, tail red).
How it looks in the game (saved with the model, so --all keeps it; defaults suit a painted, matte model):
  --glow  0..2   how strongly the bright, warm parts of the texture glow (lava cracks, flames). 0 = no glow.
  --metal 0..1   metalness (gold, chrome: ~0.85). --rough 0..1  roughness (lower = shinier). e.g. Gold Rush:
                 --glow 0 --metal 0.85 --rough 0.3
The current values for every model are kept in assets/models/outfits/looks.json.

The artist's models so far face +z with y up. If one faces another way, pass --front=-z / +x / -x.

Mesh format version 4 (src/core/HenModel.cpp): "CSHN", u32 4, u32 regions (1), u32 shards, f32 glow, metal, rough,
then per region: u32 vertex count, u32 index count, vertices (pos xyz, normal xyz, weights body/head/wing0/wing1/tail,
shard id, uv: 14 floats), u16 indices.
"""
import argparse
import io
import json
import os
import shutil
import struct
import sys

import numpy as np
from PIL import Image
from scipy import ndimage as nd
from scipy.spatial import cKDTree

sys.path.insert(0, os.path.dirname(__file__))
from build_hen_mesh import accessor, load, smooth  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SOURCES = os.path.join(ROOT, "assets", "models", "source")
OUT = os.path.join(ROOT, "assets", "models", "outfits")
LIST = os.path.join(ROOT, "src", "core", "OutfitModels.inc")
# ~40k triangles keeps curves (lenses, combs) smooth and stays under the 16-bit vertex limit after the shard cuts;
# textures are baked at 2 x 2 samples per texel and averaged down to 2048 px so thin design lines stay clean
TRIS, TEX_SIZE, SUPERSAMPLE, SHARDS = 40000, 2048, 2, 40
LOOKS = os.path.join(OUT, "looks.json")
DEFAULT_LOOK = {"glow": 1.0, "metal": 0.05, "rough": 0.55}


def looks():
    return json.load(open(LOOKS)) if os.path.exists(LOOKS) else {}


# ---------------------------------------------------------------- glTF in
def read_glb(path):
    j, bin_ = load(path)
    prims = [(m, p) for m in j["meshes"] for p in m["primitives"]]
    if len(prims) != 1:
        sys.exit(f"{path}: expected one mesh primitive, found {len(prims)} (merge the meshes before export)")
    prim = prims[0][1]
    att = prim["attributes"]
    if "TEXCOORD_0" not in att:
        sys.exit(f"{path}: the mesh has no texture coordinates")
    P = accessor(j, bin_, att["POSITION"]).astype(np.float64)
    UV = accessor(j, bin_, att["TEXCOORD_0"]).astype(np.float64)
    T = accessor(j, bin_, prim["indices"]).reshape(-1, 3).astype(np.int64)
    if "NORMAL" in att:
        N = accessor(j, bin_, att["NORMAL"]).astype(np.float64)
    else:  # smooth normals from the faces
        fn = np.cross(P[T[:, 1]] - P[T[:, 0]], P[T[:, 2]] - P[T[:, 0]])
        N = np.zeros_like(P)
        for k in range(3):
            np.add.at(N, T[:, k], fn)
    N /= np.linalg.norm(N, axis=1, keepdims=True) + 1e-12
    mat = j["materials"][prim.get("material", 0)]
    pbr = mat.get("pbrMetallicRoughness", {})
    if "baseColorTexture" not in pbr:
        sys.exit(f"{path}: the material has no base colour texture")
    img = j["images"][j["textures"][pbr["baseColorTexture"]["index"]]["source"]]
    bv = j["bufferViews"][img["bufferView"]]
    tex = Image.open(io.BytesIO(bin_[bv.get("byteOffset", 0):bv.get("byteOffset", 0) + bv["byteLength"]])).convert("RGB")
    return P, N, UV, T, np.asarray(tex).astype(np.float32)


def orient(P, front):
    """Rotate so the model faces +z (the artist convention the rest of this script assumes). Works for normals too."""
    x, y, z = P[:, 0], P[:, 1], P[:, 2]
    return {"+z": P, "-z": np.stack([-x, y, -z], 1), "+x": np.stack([-z, y, x], 1), "-x": np.stack([z, y, -x], 1)}[front]


# ---------------------------------------------------------------- texture bake
def bary_clamped(A, X):
    e1, e2, d = A[:, 1] - A[:, 0], A[:, 2] - A[:, 0], X - A[:, 0]
    d00, d01, d11 = (e1 * e1).sum(1), (e1 * e2).sum(1), (e2 * e2).sum(1)
    d20, d21 = (d * e1).sum(1), (d * e2).sum(1)
    den = d00 * d11 - d01 * d01
    den[np.abs(den) < 1e-20] = 1e-20
    v, w = (d11 * d20 - d01 * d21) / den, (d00 * d21 - d01 * d20) / den
    u, v, w = np.clip(1 - v - w, 0, None), np.clip(v, 0, None), np.clip(w, 0, None)
    s = u + v + w
    s[s == 0] = 1
    return u / s, v / s, w / s


def bake(P, T, UV, tex, P3, T3, UV3, N=None, N3=None):
    """Rasterise the new layout; each texel copies the colour of the nearest point on the original surface. With the
    normals (N: original, N3: reduced) it also bakes a tangent-space normal map: the original surface direction there,
    in the reduced triangle's frame (T = dP/du, B = dP/dv, N), the frame the game's shader rebuilds per pixel."""
    th, tw = tex.shape[:2]
    S = TEX_SIZE * SUPERSAMPLE
    q_all = UV3 * S
    pts, pix, tid, bw = [], [], [], []
    for ti, tri in enumerate(T3):
        q = q_all[tri]
        x0, x1 = int(np.floor(q[:, 0].min())), int(np.ceil(q[:, 0].max()))
        y0, y1 = int(np.floor(q[:, 1].min())), int(np.ceil(q[:, 1].max()))
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        d = (q[1, 1] - q[2, 1]) * (q[0, 0] - q[2, 0]) + (q[2, 0] - q[1, 0]) * (q[0, 1] - q[2, 1])
        if abs(d) < 1e-12:
            continue
        w0 = ((q[1, 1] - q[2, 1]) * (gx - q[2, 0]) + (q[2, 0] - q[1, 0]) * (gy - q[2, 1])) / d
        w1 = ((q[2, 1] - q[0, 1]) * (gx - q[2, 0]) + (q[0, 0] - q[2, 0]) * (gy - q[2, 1])) / d
        w2 = 1 - w0 - w1
        m = (w0 >= -0.02) & (w1 >= -0.02) & (w2 >= -0.02)  # a hair of overlap: no gaps along edges
        if not m.any():
            continue
        pts.append(w0[m, None] * P3[tri[0]] + w1[m, None] * P3[tri[1]] + w2[m, None] * P3[tri[2]])
        pix.append(np.stack([np.clip(gy[m].astype(int), 0, S - 1), np.clip(gx[m].astype(int), 0, S - 1)], 1))
        tid.append(np.full(int(m.sum()), ti))
        bw.append(np.stack([w0[m], w1[m], w2[m]], 1))
    pts, pix, tid, bw = np.concatenate(pts), np.concatenate(pix), np.concatenate(tid), np.concatenate(bw)
    # the reduced surface's frame per texel: interpolated normal, T / B from each triangle's position-uv mapping
    if True:
        A3, U3 = P3[T3], UV3[T3]
        e1, e2 = A3[:, 1] - A3[:, 0], A3[:, 2] - A3[:, 0]
        d1, d2 = U3[:, 1] - U3[:, 0], U3[:, 2] - U3[:, 0]
        det = d1[:, 0] * d2[:, 1] - d2[:, 0] * d1[:, 1]
        det[np.abs(det) < 1e-12] = 1e-12
        tanU = (e1 * d2[:, 1:2] - e2 * d1[:, 1:2]) / det[:, None]
        tanV = (e2 * d1[:, 0:1] - e1 * d2[:, 0:1]) / det[:, None]
        nl = (bw[:, :, None] * N3[T3[tid]]).sum(1)
        nl /= np.linalg.norm(nl, axis=1, keepdims=True) + 1e-12
        tu = tanU[tid] - nl * (tanU[tid] * nl).sum(1, keepdims=True)
        tu /= np.linalg.norm(tu, axis=1, keepdims=True) + 1e-12
        tv = tanV[tid] - nl * (tanV[tid] * nl).sum(1, keepdims=True) - tu * (tanV[tid] * tu).sum(1, keepdims=True)
        tv /= np.linalg.norm(tv, axis=1, keepdims=True) + 1e-12
        nout = np.zeros((len(pts), 3), np.float32)
    # find the full-detail surface under each texel by ray casting, the way texture bakers do: a ray from just
    # outside the reduced surface (a "cage" along its normal) straight back through it; the first hit is the detailed
    # surface there. Texels whose ray misses fall back to the exact closest point.
    import open3d as o3d
    scene = o3d.t.geometry.RaycastingScene()
    scene.add_triangles(o3d.core.Tensor(P.astype(np.float32)), o3d.core.Tensor(T.astype(np.uint32)))
    cage = 0.015 * float(np.linalg.norm(P.max(0) - P.min(0)))
    out = np.zeros((len(pts), 3), np.float32)
    for s0 in range(0, len(pts), 1000000):
        sl = slice(s0, s0 + 1000000)
        X, nX = pts[sl], nl[sl]
        rays = np.hstack([X + nX * cage, -nX]).astype(np.float32)
        hit = scene.cast_rays(o3d.core.Tensor(rays))
        th_ = hit["t_hit"].numpy()
        prim = hit["primitive_ids"].numpy().astype(np.int64)
        buv = hit["primitive_uvs"].numpy().astype(np.float64)
        miss = ~np.isfinite(th_) | (th_ > 2 * cage)
        if miss.any():
            cp = scene.compute_closest_points(o3d.core.Tensor(X[miss].astype(np.float32)))
            prim[miss] = cp["primitive_ids"].numpy().astype(np.int64)
            buv[miss] = cp["primitive_uvs"].numpy()
        w1, w2 = buv[:, 0], buv[:, 1]
        w0 = 1 - w1 - w2
        tri = T[prim]
        U = UV[tri]
        uv = U[:, 0] * w0[:, None] + U[:, 1] * w1[:, None] + U[:, 2] * w2[:, None]
        # bilinear colour lookup (smooth edges on the painted lines)
        fx, fy = uv[:, 0] * tw - 0.5, uv[:, 1] * th - 0.5
        x0, y0 = np.floor(fx).astype(int), np.floor(fy).astype(int)
        ax, ay = (fx - x0)[:, None], (fy - y0)[:, None]
        at = lambda xx, yy: tex[np.clip(yy, 0, th - 1), np.clip(xx, 0, tw - 1)]
        out[sl] = (at(x0, y0) * (1 - ax) + at(x0 + 1, y0) * ax) * (1 - ay) + (at(x0, y0 + 1) * (1 - ax) + at(x0 + 1, y0 + 1) * ax) * ay
        Nn = N[tri]
        bn = Nn[:, 0] * w0[:, None] + Nn[:, 1] * w1[:, None] + Nn[:, 2] * w2[:, None]
        bn /= np.linalg.norm(bn, axis=1, keepdims=True) + 1e-12
        ts = np.stack([(bn * tu[sl]).sum(1), (bn * tv[sl]).sum(1), (bn * nl[sl]).sum(1)], 1)
        ts[:, 2] = np.maximum(ts[:, 2], 0.05)  # never point into the surface
        nout[sl] = ts / np.linalg.norm(ts, axis=1, keepdims=True)
    img = np.zeros((S, S, 3), np.float32)
    cov = np.zeros((S, S), bool)
    img[pix[:, 0], pix[:, 1]] = out
    cov[pix[:, 0], pix[:, 1]] = True
    nimg = np.zeros((S, S, 3), np.float32)
    nimg[..., 2] = 255
    nimg[..., :2] = 127.5
    nimg[pix[:, 0], pix[:, 1]] = (nout * 0.5 + 0.5) * 255
    ncov = cov.copy()
    img = pad(img, cov)
    nimg = pad(nimg, ncov)
    if SUPERSAMPLE > 1:  # average each block of samples into one texel (anti-aliased lines)
        k = SUPERSAMPLE
        img = img.reshape(TEX_SIZE, k, TEX_SIZE, k, 3).mean((1, 3))
        n = nimg.reshape(TEX_SIZE, k, TEX_SIZE, k, 3).mean((1, 3)) / 127.5 - 1.0
        n /= np.linalg.norm(n, axis=2, keepdims=True) + 1e-6
        nimg = (n * 0.5 + 0.5) * 255
    return img, nimg


def pad(img, cov):
    S = img.shape[0]
    cov = cov.copy()
    for _ in range(16):  # pad islands outward so mipmaps never pull in the empty background
        grow = nd.binary_dilation(cov) & ~cov
        if not grow.any():
            break
        acc, n = np.zeros_like(img), np.zeros((S, S), np.float32)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            sh = np.roll(np.roll(cov, dy, 0), dx, 1)
            acc += np.roll(np.roll(img, dy, 0), dx, 1) * sh[..., None]
            n += sh
        img[grow] = acc[grow] / np.maximum(n[grow], 1)[:, None]
        cov |= grow
    return img


# ---------------------------------------------------------------- rig, placement, shards
def rig(P):
    """Weights for body / head / wing0 / wing1 / tail, by position relative to the bounding box (front +z, up +y)."""
    c = (P.max(0) + P.min(0)) / 2
    h = (P.max(0) - P.min(0)) / 2
    x, y, z = ((P - c) / h).T  # -1 .. 1 across the box
    wing = smooth(0.79, 0.9, np.abs(x)) * smooth(-0.78, -0.63, y) * (1 - smooth(-0.19, -0.06, y))
    tail = smooth(-0.58, -0.74, z) * (1 - smooth(-0.13, 0.0, y))
    head = smooth(-0.06, 0.15, y) * (1 - wing) * (1 - tail)
    wing0, wing1 = np.where(x > 0, wing, 0.0), np.where(x <= 0, wing, 0.0)
    body = np.clip(1 - head - wing0 - wing1 - tail, 0, 1)
    W = np.stack([body, head, wing0, wing1, tail], 1)
    return W / W.sum(1, keepdims=True)


def to_body_space(P, N):
    y = P[:, 1]
    s = 1.25 / (y.max() - y.min())
    Pg = np.stack([P[:, 2] * s + 0.05, y * s + (0.2 - y.min() * s), -P[:, 0] * s], 1)
    Ng = np.stack([N[:, 2], N[:, 1], -N[:, 0]], 1)
    return Pg, Ng


def shards(Pg, T):
    cen = Pg[T].mean(1)
    rng = np.random.default_rng(7)
    seeds = [cen[rng.integers(len(cen))]]
    d = np.linalg.norm(cen - seeds[0], axis=1)
    for _ in range(SHARDS - 1):
        seeds.append(cen[int(np.argmax(d))])
        d = np.minimum(d, np.linalg.norm(cen - seeds[-1], axis=1))
    seeds = np.array(seeds)
    jit = cen + rng.normal(0, 0.02, cen.shape)
    return np.argmin(np.linalg.norm(jit[:, None, :] - seeds[None, :, :], axis=2), axis=1)


# ---------------------------------------------------------------- preview
def preview(path, P, T, UV, img, W):
    """Four textured views (front, side, back, 3/4) and two rig views, flat-shaded with a z-buffer."""
    tex = img
    th, tw = tex.shape[:2]
    S = 360
    rigc = np.array([[150, 150, 150], [255, 220, 60], [60, 255, 120], [60, 160, 255], [255, 80, 80]], float)
    views = [(np.pi / 2, 0, "tex"), (0, 0, "tex"), (-np.pi / 2, 0, "tex"), (np.pi / 4, -0.3, "tex"), (np.pi / 2, 0, "rig"), (0, 0, "rig")]
    c0 = (P.max(0) + P.min(0)) / 2
    sc = S * 0.85 / (np.abs(P - c0).max() * 2)
    out = []
    for yaw, pitch, mode in views:
        cy, sy, cp, sp = np.cos(yaw), np.sin(yaw), np.cos(pitch), np.sin(pitch)
        R = np.array([[1, 0, 0], [0, cp, -sp], [0, sp, cp]]) @ np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
        Q = (P - c0) @ R.T
        x, y, z = Q[:, 0] * sc + S / 2, S / 2 - Q[:, 1] * sc, Q[:, 2]
        im = np.full((S, S, 3), 28.0)
        zb = np.full((S, S), -1e9)
        for a, b, c in T:
            xs, ys = x[[a, b, c]], y[[a, b, c]]
            x0, x1 = int(max(xs.min(), 0)), int(min(xs.max() + 1, S))
            y0, y1 = int(max(ys.min(), 0)), int(min(ys.max() + 1, S))
            if x1 <= x0 or y1 <= y0:
                continue
            d = (ys[1] - ys[2]) * (xs[0] - xs[2]) + (xs[2] - xs[1]) * (ys[0] - ys[2])
            if abs(d) < 1e-9:
                continue
            gx, gy = np.meshgrid(np.arange(x0, x1) + 0.5, np.arange(y0, y1) + 0.5)
            w0 = ((ys[1] - ys[2]) * (gx - xs[2]) + (xs[2] - xs[1]) * (gy - ys[2])) / d
            w1 = ((ys[2] - ys[0]) * (gx - xs[2]) + (xs[0] - xs[2]) * (gy - ys[2])) / d
            w2 = 1 - w0 - w1
            m = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
            if not m.any():
                continue
            zz = w0 * z[a] + w1 * z[b] + w2 * z[c]
            sub = zb[y0:y1, x0:x1]
            up = m & (zz > sub)
            sub[up] = zz[up]
            if mode == "tex":
                uv = w0[..., None] * UV[a] + w1[..., None] * UV[b] + w2[..., None] * UV[c]
                col = tex[np.clip((uv[..., 1] * th).astype(int), 0, th - 1), np.clip((uv[..., 0] * tw).astype(int), 0, tw - 1)]
            else:
                col = (w0[..., None] * (W[a] @ rigc) + w1[..., None] * (W[b] @ rigc) + w2[..., None] * (W[c] @ rigc))
            n = np.cross(Q[b] - Q[a], Q[c] - Q[a])
            n /= np.linalg.norm(n) + 1e-12
            im[y0:y1, x0:x1][up] = col[up] * (0.45 + 0.55 * abs(n[2]))
        out.append(im)
    Image.fromarray(np.concatenate(out, 1).clip(0, 255).astype(np.uint8)).save(path)


# ---------------------------------------------------------------- build one model
def build(outfit, front="+z", look=None):
    look = {**DEFAULT_LOOK, **(look or {})}
    import fast_simplification
    import xatlas
    src = os.path.join(SOURCES, outfit + ".glb")
    P, N, UV, T, tex = read_glb(src)
    P, N = orient(P, front), orient(N, front)
    key = np.round(P / 1e-6).astype(np.int64)
    _, first, weld = np.unique(key, axis=0, return_index=True, return_inverse=True)
    Tw = weld.ravel()[T]
    Tw = Tw[(Tw[:, 0] != Tw[:, 1]) & (Tw[:, 1] != Tw[:, 2]) & (Tw[:, 0] != Tw[:, 2])]
    if len(Tw) > TRIS:
        P2, T2 = fast_simplification.simplify(P[first].astype(np.float32), Tw.astype(np.int32), target_count=TRIS)
    else:
        P2, T2 = P[first], Tw
    P2, T2 = P2.astype(np.float64), T2.astype(np.int64)
    fn = np.cross(P2[T2[:, 1]] - P2[T2[:, 0]], P2[T2[:, 2]] - P2[T2[:, 0]])
    N2 = np.zeros_like(P2)
    for k in range(3):
        np.add.at(N2, T2[:, k], fn)
    N2 /= np.linalg.norm(N2, axis=1, keepdims=True) + 1e-12
    vmap, T3, UV3 = xatlas.parametrize(P2.astype(np.float32), T2.astype(np.uint32))
    T3 = T3.astype(np.int64)
    P3, N3 = P2[vmap], N2[vmap]
    img, nimg = bake(P, T, UV, tex, P3, T3, UV3, N, N3)
    W = rig(P3)
    Pg, Ng = to_body_space(P3, N3)
    shard = shards(Pg, T3)
    keyv = T3 * SHARDS + shard[:, None]
    used, remap = np.unique(keyv, return_inverse=True)
    vsrc, sid = used // SHARDS, used % SHARDS
    verts = np.hstack([Pg[vsrc], Ng[vsrc], W[vsrc], sid[:, None], UV3[vsrc]]).astype(np.float32)
    idx = remap.reshape(-1).astype(np.uint16)
    if len(verts) >= 65536:
        sys.exit(f"{outfit}: {len(verts)} vertices after the shard cuts (max 65535); lower TRIS")
    os.makedirs(OUT, exist_ok=True)
    mesh_path, tex_path = os.path.join(OUT, outfit + ".mesh"), os.path.join(OUT, outfit + ".jpg")
    data = bytearray(b"CSHN" + struct.pack("<III", 4, 1, SHARDS) + struct.pack("<fff", look["glow"], look["metal"], look["rough"])
                     + struct.pack("<II", len(verts), idx.size))
    data += verts.tobytes() + idx.tobytes()
    open(mesh_path, "wb").write(data)
    Image.fromarray(img.clip(0, 255).astype(np.uint8)).save(tex_path, quality=92, subsampling=0)
    Image.fromarray(nimg.clip(0, 255).astype(np.uint8)).save(os.path.join(OUT, outfit + "_n.jpg"), quality=97, subsampling=0)
    preview(os.path.join(OUT, outfit + "_preview.png"), P3, T3, UV3, img, W)
    all_looks = looks()
    all_looks[outfit] = look
    json.dump(all_looks, open(LOOKS, "w"), indent=2, sort_keys=True)
    print(f"{outfit}: {len(T)} -> {len(T3)} triangles, {len(verts)} vertices; mesh {len(data) / 1e6:.2f} MB, "
          f"textures {(os.path.getsize(tex_path) + os.path.getsize(os.path.join(OUT, outfit + '_n.jpg'))) / 1e3:.0f} KB; "
          f"check {outfit}_preview.png")


def registered():
    return sorted(f[:-5] for f in os.listdir(OUT) if f.endswith(".mesh")) if os.path.isdir(OUT) else []


def write_list():
    ids = registered()
    lines = ["// Generated by scripts/models/add_outfit_model.py; do not edit. One line per outfit with its own hen model:",
             "// assets/models/outfits/<id>.mesh + <id>.jpg, embedded by CMakeLists.txt, looked up by outfit id (HenModel.cpp)."]
    lines += [f"CS_OUTFIT_MODEL({i})" for i in ids]
    open(LIST, "w").write("\n".join(lines) + "\n")
    print(f"{LIST}: {', '.join(ids) or '(none)'}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("glb", nargs="?")
    ap.add_argument("outfit", nargs="?")
    ap.add_argument("--front", default="+z", choices=["+z", "-z", "+x", "-x"])
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--remove")
    ap.add_argument("--glow", type=float)
    ap.add_argument("--metal", type=float)
    ap.add_argument("--rough", type=float)
    a = ap.parse_args()
    if a.remove:
        for ext in (".mesh", ".jpg", "_n.jpg", "_preview.png"):
            p = os.path.join(OUT, a.remove + ext)
            if os.path.exists(p):
                os.remove(p)
        lk = looks()
        lk.pop(a.remove, None)
        json.dump(lk, open(LOOKS, "w"), indent=2, sort_keys=True)
    elif a.all:
        for outfit in sorted(f[:-4] for f in os.listdir(SOURCES) if f.endswith(".glb") and f[:-4].startswith("hen_")):
            build(outfit, a.front, looks().get(outfit))
    else:
        if not a.glb or not a.outfit:
            ap.error("give <model.glb> <outfit id>, or --all, or --remove <outfit id>")
        if not a.outfit.startswith("hen_") or not a.outfit.replace("_", "").isalnum():
            ap.error("the outfit id is the cosmetic id in Economy.cpp, e.g. hen_lava")
        os.makedirs(SOURCES, exist_ok=True)
        dst = os.path.join(SOURCES, a.outfit + ".glb")
        if os.path.abspath(a.glb) != os.path.abspath(dst):
            shutil.copyfile(a.glb, dst)
        given = {k: v for k, v in (("glow", a.glow), ("metal", a.metal), ("rough", a.rough)) if v is not None}
        build(a.outfit, a.front, {**looks().get(a.outfit, {}), **given})
    write_list()


if __name__ == "__main__":
    main()
