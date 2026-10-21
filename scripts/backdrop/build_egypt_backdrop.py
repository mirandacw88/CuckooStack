#!/usr/bin/env python3
"""Neon Egypt's backdrop art: cuts the artist's sprites out of assets/backdrop/egypt/source/egypt_sheet.webp
(transparent), cleans them, upscales the ones shown large, and packs them into one atlas for the world's sprite pass.

    python3 -m pip install pillow numpy
    python3 scripts/backdrop/build_egypt_backdrop.py

Writes assets/backdrop/egypt_backdrop.png (straight alpha; the game premultiplies on load) and src/core/EgyptBackdrop.h
(each sprite's atlas rect, its aspect, and the feature points the game lights: a pyramid's capstone, the pylon's
doorway, the watchtower's beacon). assets/backdrop/egypt/source/egypt_mockup.jpg is the layout reference.

The art: each sprite is the sheet's connected shape inside its box (a neighbour's edge never comes along), with the
soft glow round it kept; the big pieces are upscaled 2x (Lanczos in premultiplied space, then a light unsharp mask)
so they stay sharp when the backdrop shows them a phone-screen tall; every sprite sits in a gutter of 16 px of clear
alpha, aligned to 16, so the atlas's mips never bleed one sprite into another down to 1/16 size.
Clean-ups: Anubis's sheet copy has a dark smudge filling the gap between his leg and his staff; it's cleared.
The atlas also holds a few white light shapes drawn here, which the game tints and blends additively for the backdrop's
lights in their right place among the sprites (a soft dot, a soft line, a beam fading upward, a ring, a solid square),
and the moon's face, drawn here too (a pale disc darker toward its rim, its seas and its craters).
"""
import os

import numpy as np
from PIL import Image, ImageFilter

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SHEET = os.path.join(ROOT, "assets", "backdrop", "egypt", "source", "egypt_sheet.webp")
ATLAS = os.path.join(ROOT, "assets", "backdrop", "egypt_backdrop.png")
HEADER = os.path.join(ROOT, "src", "core", "EgyptBackdrop.h")

# id: (box on the sheet: left, top, right, bottom), upscale, what it is
SPRITES = [
    ("PyramidCircuit", (18, 24, 594, 444), 2.0, "the great pyramid, its faces traced with gold circuitry"),
    ("PyramidPlain", (582, 42, 1050, 360), 2.0, "a plain dark pyramid, cyan-edged, its capstone lit"),
    ("PylonGate", (1086, 72, 1470, 324), 2.0, "a temple's pylon gate: Anubis and a pharaoh carved, the doorway glowing"),
    ("Temple", (1512, 78, 1968, 336), 2.0, "a columned temple"),
    ("ObeliskGlyphs", (648, 384, 756, 912), 2.0, "an obelisk carved with cyan hieroglyphs"),
    ("Sphinx", (1062, 438, 1344, 624), 2.0, "the sphinx"),
    ("Watchtower", (1374, 342, 1488, 642), 2.0, "a watchtower, a signal on top"),
    ("Anubis", (60, 492, 234, 918), 2.0, "Anubis with his staff, facing right"),
    ("Horus", (258, 456, 432, 918), 2.0, "Horus with his staff, facing left"),
    ("Pharaoh", (504, 588, 606, 912), 2.0, "a pharaoh's statue"),
    ("ObeliskCyan", (786, 486, 876, 912), 2.0, "a plain cyan obelisk"),
    ("ObeliskGold", (912, 576, 1002, 912), 2.0, "a gold obelisk carved with hieroglyphs"),
    ("PharaohViolet", (1044, 660, 1122, 912), 2.0, "a pharaoh's statue in violet"),
    ("PalmA", (1158, 678, 1362, 912), 2.0, "a palm"),
    ("PalmB", (1380, 672, 1554, 912), 2.0, "a palm"),
    ("Bamboo", (1572, 672, 1698, 918), 2.0, "a clump of bamboo"),
    ("PalmSmall", (1716, 720, 1824, 912), 2.0, "a small palm"),
    ("PalmTall", (1830, 666, 1974, 912), 2.0, "a tall palm"),
    ("TileA", (42, 942, 186, 1092), 1.0, "a floor tile"),
    ("TileB", (204, 942, 348, 1092), 1.0, "a floor tile"),
    ("TileC", (366, 942, 510, 1092), 1.0, "a floor tile, cracked"),
    ("TileD", (528, 942, 672, 1092), 1.0, "a floor tile, cracked"),
    ("TileE", (690, 942, 834, 1092), 1.0, "a floor tile, cracked"),
    ("StelaA", (1020, 948, 1068, 1086), 1.5, "a stela, hieroglyphs in a cartouche"),
    ("StelaB", (1092, 948, 1140, 1086), 1.5, "a stela, hieroglyphs in a cartouche"),
    ("ScarabGold", (1344, 942, 1410, 1002), 1.5, "a gold scarab"),
    ("ScarabDark", (1344, 1014, 1410, 1092), 1.5, "a dark scarab"),
    ("ScarabGoldBig", (1440, 936, 1500, 1008), 1.5, "a gold scarab, wings out"),
    ("ScarabCyan", (1440, 1020, 1500, 1092), 1.5, "a cyan scarab"),
    ("ScarabCyanB", (1530, 1026, 1590, 1086), 1.5, "a cyan scarab"),
    ("ScarabGoldSmall", (1536, 942, 1590, 1008), 1.5, "a small gold scarab"),
    ("ObeliskSmallCyan", (1626, 924, 1674, 1098), 1.5, "a small cyan obelisk"),
    ("ObeliskSmallGold", (1704, 924, 1758, 1098), 1.5, "a small gold obelisk"),
    ("SphinxBust", (1788, 954, 1860, 1092), 1.5, "a sphinx's bust on a plinth"),
    ("HorusBust", (1890, 924, 1962, 1092), 1.5, "Horus's bust on a plinth"),
]
CELL = 6        # the coarse grid a sprite's connected shape is found on (its parts that nearly touch join up)
GUTTER = 16     # clear alpha round every sprite in the atlas, and its alignment
ATLAS_W = 4096


def label_cells(alpha):
    """connected shapes on a coarse grid: (label per cell, cell count per label)"""
    h, w = alpha.shape[0] // CELL, alpha.shape[1] // CELL
    mask = (alpha[:h * CELL, :w * CELL].reshape(h, CELL, w, CELL) > 24).any(axis=(1, 3))
    lab = -np.ones((h, w), int)
    sizes = []
    for y in range(h):
        for x in range(w):
            if not mask[y, x] or lab[y, x] >= 0:
                continue
            stack, n = [(y, x)], 0
            lab[y, x] = len(sizes)
            while stack:
                cy, cx = stack.pop()
                n += 1
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        ny, nx = cy + dy, cx + dx
                        if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] and lab[ny, nx] < 0:
                            lab[ny, nx] = len(sizes)
                            stack.append((ny, nx))
            sizes.append(n)
    return lab, sizes


def cut(sheet, lab, box):
    """the largest connected shape in the box, with the faint glow round it, and nothing of its neighbours"""
    l, t, r, b = box
    cells = lab[t // CELL:(b + CELL - 1) // CELL, l // CELL:(r + CELL - 1) // CELL]
    ids, counts = np.unique(cells[cells >= 0], return_counts=True)
    mine = ids[np.argmax(counts)]
    own = lab == mine
    near = own.copy()  # the shape's cells grown by two, minus any other shape's
    for _ in range(2):
        g = near.copy()
        g[1:] |= near[:-1]; g[:-1] |= near[1:]; g[:, 1:] |= near[:, :-1]; g[:, :-1] |= near[:, 1:]
        near = g
    near &= (lab < 0) | own
    full = np.zeros(sheet.shape[:2], bool)
    full[:near.shape[0] * CELL, :near.shape[1] * CELL] = np.repeat(np.repeat(near, CELL, 0), CELL, 1)
    px = sheet.copy()
    px[~full, 3] = 0
    ys, xs = np.nonzero(px[:, :, 3] > 0)
    keep = (xs >= l - 8) & (xs < r + 8) & (ys >= t - 8) & (ys < b + 8)
    x0, x1, y0, y1 = xs[keep].min(), xs[keep].max() + 1, ys[keep].min(), ys[keep].max() + 1
    return px[y0:y1, x0:x1], (x0, y0)


def clean(name, px, origin):
    if name == "Anubis":  # the smudge between his right leg and the staff: dark fill, never a neon line
        ox, oy = origin
        h, w = px.shape[:2]
        yy, xx = np.mgrid[0:h, 0:w]
        sx, sy = xx + ox, yy + oy
        inside = (sy >= 796) & (sy <= 902) & (sx <= 212) & (sx >= 176 + (sy - 796) * 0.30)
        dark = px[:, :, :3].max(axis=2) < 70
        px[inside & dark, 3] = 0
    return px


def upscale(px, k):
    if k == 1.0:
        return px
    f = px.astype(np.float32) / 255.0
    pm = np.dstack([f[:, :, :3] * f[:, :, 3:4], f[:, :, 3:4]])  # premultiplied: edges filter without dark fringes
    w, h = int(round(px.shape[1] * k)), int(round(px.shape[0] * k))
    chans = [np.asarray(Image.fromarray(pm[:, :, c]).resize((w, h), Image.LANCZOS)) for c in range(4)]
    out = np.clip(np.dstack(chans), 0, 1)
    rgb = Image.fromarray((out[:, :, :3] * 255 + 0.5).astype(np.uint8)).filter(ImageFilter.UnsharpMask(radius=1.4, percent=55, threshold=2))
    a = out[:, :, 3]
    rgb = np.asarray(rgb).astype(np.float32) / 255.0
    straight = np.where(a[:, :, None] > 1e-4, rgb / np.maximum(a[:, :, None], 1e-4), 0)
    return (np.dstack([np.clip(straight, 0, 1), a]) * 255 + 0.5).astype(np.uint8)


def features(name, px):
    """points the game lights (fractions of the sprite: x from the left, y from the top)"""
    a = px[:, :, 3] > 128
    h, w = a.shape
    rows = np.nonzero(a.any(axis=1))[0]
    top = rows[0]
    xs = np.nonzero(a[top:top + max(2, h // 60)].any(axis=0))[0]
    out = {"top": ((xs.min() + xs.max() + 1) / 2 / w, top / h), "foot": 0.0}
    if name.startswith("Pyramid"):  # a 3/4 view: its base's outer corners stand above its front corner (the sprite's
        cols = np.nonzero(a.any(axis=0))[0]  # bottom); where they meet the ground is its foot line
        edge = max(2, w // 40)
        corner = lambda cs: max(np.nonzero(a[:, c])[0].max() for c in cs)
        out["foot"] = 1 - min(corner(cols[:edge]), corner(cols[-edge:])) / h
    if name == "PylonGate":  # the doorway's light: warm, bright
        r, g, b = (px[:, :, c].astype(int) for c in range(3))
        warm = (r > 200) & (g > 160) & (b < 150) & a
        ys, xs = np.nonzero(warm)
        out["door"] = (xs.mean() / w, ys.mean() / h)
    return out


def pack(sizes):
    """skyline packing (each sprite where its top ends lowest, then leftmost), tallest first, each in its gutter:
    {index: (x, y) of the sprite itself}, and the height used"""
    align = lambda v: (v + GUTTER - 1) // GUTTER * GUTTER
    sky = [(0, 0, ATLAS_W)]  # the skyline: (x, y, width) segments, left to right
    place = {}
    for i in sorted(range(len(sizes)), key=lambda i: (-sizes[i][0], -sizes[i][1])):
        h, w = sizes[i]
        cw, ch = align(w + 2 * GUTTER), align(h + 2 * GUTTER)
        best = None
        for j in range(len(sky)):
            x = sky[j][0]
            if x + cw > ATLAS_W:
                break
            y, k, span = 0, j, 0
            while span < cw:
                y = max(y, sky[k][1])
                span += sky[k][2]
                k += 1
            if best is None or (y, x) < best[:2]:
                best = (y, x)
        y, x = best
        place[i] = (x + GUTTER, y + GUTTER)
        segs = [s for s in sky if s[0] + s[2] <= x or s[0] >= x + cw]  # the skyline under it, cut away
        for s in sky:
            if s[0] < x < s[0] + s[2]:
                segs.append((s[0], s[1], x - s[0]))
            if s[0] < x + cw < s[0] + s[2]:
                segs.append((x + cw, s[1], s[0] + s[2] - x - cw))
        segs.append((x, y + ch, cw))
        sky = sorted(segs)
        merged = [sky[0]]
        for s in sky[1:]:
            if s[1] == merged[-1][1] and merged[-1][0] + merged[-1][2] == s[0]:
                merged[-1] = (merged[-1][0], s[1], merged[-1][2] + s[2])
            else:
                merged.append(s)
        sky = merged
    return place, max(s[1] for s in sky)


def lights():
    """white light shapes (alpha only): (name, pixels, what)"""
    def white(alpha):
        a = (np.clip(alpha, 0, 1) * 255 + 0.5).astype(np.uint8)
        return np.dstack([np.full(a.shape, 255, np.uint8)] * 3 + [a])
    n = 128
    y, x = (np.mgrid[0:n, 0:n] + 0.5) / n * 2 - 1
    r = np.hypot(x, y)
    dot = np.exp(-(r / 0.42) ** 2) * np.clip(1 - r, 0, 1) ** 0.5
    ring = np.exp(-((r - 0.8) / 0.07) ** 2) * (r < 1)
    ly, lx = np.mgrid[0:24, 0:256] + 0.5
    line = np.exp(-(((ly / 24) * 2 - 1) / 0.42) ** 2) * np.clip(np.minimum(lx, 256 - lx) / 40, 0, 1)
    by, bx = np.mgrid[0:512, 0:48] + 0.5
    beam = np.exp(-(((bx / 48) * 2 - 1) / 0.45) ** 2) * (by / 512) ** 1.6  # (bright at its foot, fading up to nothing)
    return [("GlowDot", white(dot), "light: a soft round glow"), ("GlowLine", white(line), "light: a soft line"),
            ("GlowBeam", white(beam), "light: a beam fading upward from its foot"), ("GlowRing", white(ring), "light: a thin ring"),
            ("Solid", white(np.ones((32, 32))), "a solid square (flat fills)"), ("MoonFace", moon_face(), "the moon's face")]


def blurred_noise(n, sigma, seed):
    """smooth noise, n x n, -1..1 (a float gaussian blur of white noise, wrapping round)"""
    g = np.random.default_rng(seed).normal(0, 1, (n, n))
    f = np.fft.fftfreq(n)
    k = np.exp(-2 * (np.pi * sigma) ** 2 * (f[:, None] ** 2 + f[None, :] ** 2))
    out = np.real(np.fft.ifft2(np.fft.fft2(g) * k))
    return out / (np.abs(out).max() + 1e-9)


def moon_face(n=512):
    """the moon as it looks from the Earth: a pale disc, cyan-white, darker toward its rim; its seas where the real ones
    are (dark, ragged shores); craters, each lit on its rim toward the light (up and to the left) and shadowed inside,
    and one bright young crater with rays (Tycho); a fine grain. Straight alpha, an antialiased edge"""
    rng = np.random.default_rng(11)
    y, x = (np.mgrid[0:n, 0:n] + 0.5) / n * 2 - 1
    r = np.hypot(x, y)
    R = 0.96
    alpha = np.clip((R - r) * n / 2 + 0.5, 0, 1)
    facing = np.sqrt(np.clip(1 - (r / R) ** 2, 0, 1))
    shade = 0.58 + 0.42 * facing ** 0.5                        # (darker toward the rim)
    seas = np.zeros_like(r)
    for cx, cy, sx, sy in [(-0.27, -0.33, 0.24, 0.22), (0.14, -0.32, 0.14, 0.13), (0.26, -0.04, 0.17, 0.15), (0.6, -0.24, 0.09, 0.08),
                           (0.47, 0.17, 0.11, 0.13), (-0.13, 0.34, 0.15, 0.12), (-0.55, -0.02, 0.17, 0.36), (-0.1, -0.62, 0.32, 0.07)]:
        seas = np.maximum(seas, np.exp(-(((x - cx) / sx) ** 2 + ((y - cy) / sy) ** 2)))
    seas += 0.3 * blurred_noise(n, n / 30, 3) + 0.1 * blurred_noise(n, n / 90, 4)  # (ragged shores)
    sea = np.clip((seas - 0.32) / 0.32, 0, 1) ** 1.3 * 0.78
    crater = np.zeros_like(r)
    lit = np.zeros_like(r)
    dark = np.zeros_like(r)
    for _ in range(60):
        cx, cy = rng.uniform(-0.86, 0.86, 2)
        if np.hypot(cx, cy) > 0.88:
            continue
        sz = rng.uniform(0.01, 0.035) if rng.uniform() < 0.88 else rng.uniform(0.045, 0.075)
        dx, dy = (x - cx) / sz, (y - cy) / sz
        d = np.hypot(dx, dy)
        inside = np.clip(1 - d, 0, 1)
        crater = np.maximum(crater, 0.4 * inside ** 0.7)
        side = np.clip(-(dx * 0.7 + dy * 0.7) / np.maximum(d, 1e-3), -1, 1)  # (+1: the side toward the light)
        lit = np.maximum(lit, 0.2 * np.exp(-((d - 1.0) / 0.25) ** 2) * np.clip(-side, 0, 1))  # (its far rim catches it)
        dark = np.maximum(dark, 0.22 * inside ** 0.5 * np.clip(side, 0, 1))                  # (its near wall in shadow)
    tx, ty = -0.12, 0.62                                        # Tycho: bright, with rays
    td = np.hypot(x - tx, y - ty)
    ang = np.arctan2(y - ty, x - tx)
    rays = np.clip(np.cos(ang * 7 + 0.4) * 0.5 + np.cos(ang * 11 + 1.3) * 0.5, 0, 1) ** 6 * np.exp(-td / 0.35) * (td > 0.04)
    bright = 0.45 * np.exp(-(td / 0.035) ** 2) + 0.18 * rays
    grain = blurred_noise(n, 0.8, 9) * 0.6 + blurred_noise(n, 2.5, 10) * 0.4
    high = np.array([228, 246, 252]) / 255.0
    maria = np.array([126, 168, 188]) / 255.0
    col = high[None, None, :] * (1 - sea[..., None]) + maria[None, None, :] * sea[..., None]
    col = col * (shade * (1 - 0.18 * crater) * (1 - dark) * (1 + 0.06 * grain))[..., None] + (lit + bright)[..., None] * 0.5
    rgb = (np.clip(col, 0, 1) * 255 + 0.5).astype(np.uint8)
    return np.dstack([rgb, (alpha * 255 + 0.5).astype(np.uint8)])


def main():
    sheet = np.array(Image.open(SHEET).convert("RGBA"))
    lab, _ = label_cells(sheet[:, :, 3])
    sprites = []
    for name, box, k, what in SPRITES:
        px, origin = cut(sheet, lab, box)
        px = upscale(clean(name, px, origin), k)
        sprites.append((name, px, features(name, px), what))
    for name, px, what in lights():
        sprites.append((name, px, {"top": (0.5, 0.0), "foot": 0.0}, what))
    place, top = pack([s[1].shape[:2] for s in sprites])
    height = 1 << (top - 1).bit_length()
    atlas = np.zeros((height, ATLAS_W, 4), np.uint8)
    for i, (name, px, _, _) in enumerate(sprites):
        ax, ay = place[i]
        atlas[ay:ay + px.shape[0], ax:ax + px.shape[1]] = px
    Image.fromarray(atlas).save(ATLAS, optimize=True)

    lines = [
        "// Generated by scripts/backdrop/build_egypt_backdrop.py from assets/backdrop/egypt/source/egypt_sheet.webp. Do not edit.",
        "// Neon Egypt's backdrop sprites in assets/backdrop/egypt_backdrop.png: each one's atlas rect (u0, v0, u1, v1; v down),",
        "// its width over its height, the middle of its top (a pyramid's capstone, an obelisk's tip, the watchtower's signal)",
        "// and the pylon's doorway, as fractions of the sprite (x from the left, y from the top); and its foot line, how far",
        "// up from its bottom it meets the ground (a pyramid seen 3/4 on: its base's outer corners, above its front one).",
        "#pragma once",
        "",
        "#include <cstdint>",
        "",
        "namespace cs::egypt {",
        "",
        "inline constexpr int kAtlasW = %d, kAtlasH = %d;" % (ATLAS_W, height),
        "",
        "enum Sprite : uint8_t {",
    ]
    for name, px, f, what in sprites:
        lines.append("    %s, // %s (%d x %d px)" % (name, what, px.shape[1], px.shape[0]))
    lines += ["    kSpriteCount", "};", "", "struct SpriteInfo { float u0, v0, u1, v1, aspect, topX, topY, doorX, doorY, footY; };", "",
              "inline constexpr SpriteInfo kSprites[kSpriteCount] = {"]
    for i, (name, px, f, _) in enumerate(sprites):
        ax, ay = place[i]
        h, w = px.shape[:2]
        dx, dy = f.get("door", (0, 0))
        lines.append("    {%.6ff, %.6ff, %.6ff, %.6ff, %.4ff, %.3ff, %.3ff, %.3ff, %.3ff, %.3ff}, // %s" % (
            ax / ATLAS_W, ay / height, (ax + w) / ATLAS_W, (ay + h) / height, w / h, f["top"][0], f["top"][1], dx, dy, f["foot"], name))
    lines += ["};", "", "} // namespace cs::egypt", ""]
    with open(HEADER, "w") as fh:
        fh.write("\n".join(lines))
    used = sum(p[1].shape[0] * p[1].shape[1] for p in sprites)
    print("%s: %d x %d, %d sprites (%.0f%% covered), %d KB" % (os.path.relpath(ATLAS, ROOT), ATLAS_W, height, len(sprites),
                                                              100.0 * used / (ATLAS_W * height), os.path.getsize(ATLAS) // 1024))


if __name__ == "__main__":
    main()
