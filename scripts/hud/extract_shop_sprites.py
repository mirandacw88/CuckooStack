#!/usr/bin/env python3
"""Cut the coin-shop sprites out of the artist's sheet (assets/hud/source/coin_shop_sheet.webp, transparent).

    python3 -m pip install pillow numpy
    python3 scripts/hud/extract_shop_sprites.py
    python3 scripts/hud/make_hud_icons.py          (packs assets/hud/shop/*.png into the HUD atlas)

The sheet holds most sprites at 1x, 2x and 3x; each one here is taken from its largest clean copy and the atlas mip
chain covers the smaller screen densities. assets/hud/source/coin_shop_mockup.jpg is the layout reference.

Clean-ups applied to the art:
  - price buttons: the baked-in price is removed and the body is rebuilt as a 2:1 slab (left cap, plain middle,
    right cap), so the game can print store-localised prices and stretch it sideways (3-slice);
  - the FREE frame: recoloured from yellow to the mockup's green and re-proportioned to 2:1 for the same reason,
    plus a tab-less white copy (frame_neon) that the game tints for the gold and BEST VALUE tiles; both get a
    see-through interior (the sheet's is solid black) so they read as glass like the other tiles;
  - the Missions icon: the baked "1" badge is replaced by the frame's mirrored corner (the game draws the badge);
  - a generic menu frame (the Replay frame with its icon painted out) for menu buttons the sheet has no art for.
"""
import colorsys
import os

import numpy as np
from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SHEET = os.path.join(ROOT, "assets", "hud", "source", "coin_shop_sheet.webp")
OUT = os.path.join(ROOT, "assets", "hud", "shop")

# Sprites the artist delivers as their own files (not cut from the sheet); left alone here.
ARTIST = {"pile_9000"}  # the 9,000 coin hoard, 2624 x 1632

# name: (left, top, right, bottom) on the sheet; 3x copies unless noted
BOXES = {
    "coin_green": (55, 1392, 286, 1638),
    "pile_300": (333, 1500, 547, 1630),
    "pile_1000": (549, 792, 755, 959),       # 2x (the 3x copy is a single stack)
    "pile_1800": (800, 759, 1132, 982),      # 2x
    "pile_4000": (786, 1401, 1185, 1658),
    "frame_free": (1176, 744, 1529, 1027),   # 2x
    "close": (1210, 1372, 1368, 1529),
    "tv_ad": (372, 1014, 500, 1137),         # 2x (the 3x copy has "+50" baked on)
    "tv_noads": (1409, 1558, 1544, 1696),
    "chest": (1652, 1387, 1939, 1669),
    "menu_replay": (1208, 1780, 1345, 1914),
    "menu_shop": (1396, 1767, 1556, 1925),
    "menu_locker": (1598, 1772, 1746, 1919),
    "menu_missions": (1804, 1766, 1954, 1914),
    "btn_blue": (33, 1699, 309, 1813),
    "btn_gold": (317, 1698, 594, 1813),
    "btn_dark": (830, 1845, 1150, 1959),
    "ribbon_best": (829, 1686, 1172, 1804),
}


def trim(a, pad=2):
    ys, xs = np.nonzero(a[..., 3] > 4)
    return a[max(ys.min() - pad, 0):ys.max() + pad + 1, max(xs.min() - pad, 0):xs.max() + pad + 1]


def resize(a, w, h):
    return np.asarray(Image.fromarray(a).resize((int(w), int(h)), Image.LANCZOS))


def three_slice(a, cap, width):
    """Keep `cap` columns at each end, fill the middle by blending the two columns just inside the caps."""
    h, w = a.shape[:2]
    left, right = a[:, :cap].astype(float), a[:, w - cap:].astype(float)
    mid_n = width - 2 * cap
    t = np.linspace(0, 1, mid_n)[None, :, None]
    mid = left[:, -1:, :] * (1 - t) + right[:, :1, :] * t
    return np.concatenate([left, mid, right], axis=1).clip(0, 255).astype(np.uint8)


def text_span(a):
    """Columns holding the white price text (bright, unsaturated, opaque) in the middle band."""
    h = a.shape[0]
    band = a[int(h * 0.25):int(h * 0.75)].astype(int)
    white = (band[..., :3].min(2) > 185) & (band[..., 3] > 200)
    cols = np.nonzero(white.any(0))[0]
    return cols.min(), cols.max()


def button(a):
    """Price button without its price, as a 2:1 slab whose caps are half its height."""
    a = trim(a)
    h, w = a.shape[:2]
    t0, t1 = text_span(a)
    cap = min(h // 2, t0 - 3, w - t1 - 4)
    if cap < h * 0.4:
        raise SystemExit(f"button text too close to the caps ({t0}..{t1} of {w})")
    out = three_slice(a, cap, 2 * h)
    # the caps only reach the half-height mark when cap == h/2; pad the rest from the plain middle so the 3-slice
    # in the game (corner = half the height) never samples text
    return out


def hue_shift(a, from_lo, from_hi, to):
    """Move saturated pixels whose hue lies in [from_lo, from_hi] degrees to hue `to`, keeping value/saturation."""
    rgb = a[..., :3].astype(float) / 255
    mx, mn = rgb.max(2), rgb.min(2)
    out = rgb.copy()
    sel = (mx - mn) > 0.05
    for y, x in zip(*np.nonzero(sel)):
        hh, ss, vv = colorsys.rgb_to_hsv(*rgb[y, x])
        deg = hh * 360
        if from_lo <= deg <= from_hi:
            out[y, x] = colorsys.hsv_to_rgb(to / 360, ss, vv)
    return np.dstack([(out * 255).round().astype(np.uint8), a[..., 3]])


def widen(a):
    """Scale a frame to the 2x cell height and widen it to 2:1 through its plain middle column."""
    h, w = a.shape[:2]
    target_h = 192 * 2  # 2x of the atlas cell, downscaled when packed
    a = resize(a, w * target_h / h, target_h)
    h, w = a.shape[:2]
    cap = int(h * 0.5)
    mid = np.repeat(a[:, w // 2:w // 2 + 1], 2 * h - 2 * cap, axis=1)  # plain top/bottom edge, dark interior
    return np.concatenate([a[:, :cap], mid, a[:, w - cap:]], axis=1)


def glassify(a, keep=0.38):
    """Make a frame's dark interior see-through (glass over the blurred scene): opacity follows brightness, so the
    neon line and its inner glow stay solid while the near-black fill drops to `keep`."""
    a = a.copy()
    v = a[..., :3].max(2).astype(float)
    t = np.clip((v - 35) / 70, 0, 1)
    a[..., 3] = (a[..., 3] * (keep + (1 - keep) * t)).astype(np.uint8)
    return a


def frame_free(a):
    """Green FREE frame (the sheet's is yellow), 2:1."""
    return glassify(widen(trim(hue_shift(a, 20, 100, 138))))


def frame_neon(a):
    """The FREE frame without its tab, in white, for the game to tint (gold 300 tile, pink BEST VALUE tile)."""
    a = trim(a).copy()
    h, w = a.shape[:2]
    q = int(h * 0.5)
    a[:q, :w // 2] = a[:q, w - 1:w - 1 - w // 2:-1]  # top-left quarter = top-right quarter, mirrored
    rgb = a[..., :3].astype(float)
    v = rgb.max(2)
    t = np.clip((v - 60) / 80, 0, 1)[..., None]  # the neon line goes white (colour dropped); the dark glass stays
    a[..., :3] = (rgb * (1 - t) + v[..., None] * t).astype(np.uint8)
    return glassify(widen(a))


def frame_box(a):
    """Bounds of a square menu frame, measured on its lower-left half (clear of any badge)."""
    h, w = a.shape[:2]
    on = a[..., 3] > 100
    rows = np.nonzero(on[:, :w // 2].any(1))[0]
    cols = np.nonzero(on[h // 2:].any(0))[0]
    return rows.min(), rows.max(), cols.min(), cols.max()


def missions(a):
    """Replace the baked "1" badge (top-right) with the frame's top-left corner, mirrored inside the frame."""
    a = trim(a).copy()
    h, w = a.shape[:2]
    top, bottom, left, right = frame_box(a)
    rgb = a[..., :3].astype(int)
    pink = (rgb[..., 0] > 190) & (rgb[..., 1] < 140) & (rgb[..., 2] > 90)
    white = rgb.min(2) > 200
    zone = np.zeros((h, w), bool)
    zone[:int(top + (bottom - top) * 0.4), int(left + (right - left) * 0.62):] = True
    ys, xs = np.nonzero(zone & (pink | white) & (a[..., 3] > 30))
    y1, x0 = ys.max() + 5, xs.min() - 5
    src = a.copy()
    # the target disc: leftmost and lowest strongly coloured pixels inside the frame give its centre and radius
    sat = (rgb.max(2) - rgb.min(2) > 90) & (a[..., 3] > 200)
    inner = np.zeros((h, w), bool)
    m = int((right - left) * 0.08)
    inner[top + m:bottom - m, left + m:right - m] = True
    disc = sat & inner
    # widest row of the disc below the badge gives its centre and radius
    rows = [y for y in range(y1, bottom) if disc[y].any()]
    spans = [(np.nonzero(disc[y])[0].max() - np.nonzero(disc[y])[0].min(), y) for y in rows]
    width, wy = max(spans)
    xs_w = np.nonzero(disc[wy])[0]
    cx, r = (xs_w.min() + xs_w.max()) / 2, width / 2
    cy = np.nonzero(disc.any(1))[0].max() - r
    # refine the centre column: the lower half of the disc is mirror-symmetric about it
    lo = src[int(cy + r * 0.35):int(cy + r * 0.9)].astype(float)

    def asym(c2):  # c2 = 2 * centre column
        xs = np.arange(int(c2 / 2 - r * 0.8), int(c2 / 2))
        return np.abs(lo[:, xs, :3] - lo[:, c2 - xs, :3]).mean()
    cx = min(range(int(2 * cx) - 24, int(2 * cx) + 25), key=asym) / 2
    for y in range(0, y1):
        for x in range(x0, w):
            mx = left + right - x
            if not (np.hypot(x - cx, y - cy) <= r + 3) and 0 <= mx < w and x <= right + 3:
                a[y, x] = src[y, mx]  # frame corner, mirrored
            elif not np.hypot(x - cx, y - cy) <= r + 3:
                a[y, x] = 0
    # the disc's top-right quarter (badge and dart) becomes its top-left quarter mirrored: the rings are concentric,
    # so they join up on both edges; the bullseye stays
    for y in range(0, int(cy + r * 0.3) + 1):
        for x in range(int(np.ceil(cx)), w):
            d = np.hypot(x - cx, y - cy)
            sx = int(2 * cx) - x
            if r * 0.2 < d <= r + 3 and 0 <= sx < w:
                a[y, x] = src[y, sx]
    a[:max(top - 3, 0)] = 0  # badge overhang above the frame
    return trim(a)


def laplace_fill(a, mask, iters=1500):
    """Fill `mask` smoothly from its surroundings (premultiplied, so edges don't darken)."""
    f = a.astype(float)
    f[..., :3] *= f[..., 3:4] / 255
    for _ in range(iters):
        avg = (np.roll(f, 1, 0) + np.roll(f, -1, 0) + np.roll(f, 1, 1) + np.roll(f, -1, 1)) / 4
        f[mask] = avg[mask]
    out = f.copy()
    al = np.maximum(f[..., 3:4], 1e-3)
    out[..., :3] = f[..., :3] * 255 / al
    return out.clip(0, 255).astype(np.uint8)


def menu_frame(a):
    """The Replay button's frame with its icon painted out: the icon area is refilled from the frame interior."""
    a = trim(a)
    h, w = a.shape[:2]
    top, bottom, left, right = frame_box(a)
    fh, fw = bottom - top, right - left
    yy, xx = np.mgrid[0:h, 0:w]
    cy, cx = top + fh / 2, left + fw / 2
    # rounded box around the icon (and its glow), well inside the frame's neon border
    rx, ry, r = fw * 0.36, fh * 0.34, fw * 0.12
    dx, dy = np.maximum(abs(xx - cx) - (rx - r), 0), np.maximum(abs(yy - cy) - (ry - r), 0)
    mask = np.hypot(dx, dy) < r
    return laplace_fill(a, mask)


def main():
    sheet = np.asarray(Image.open(SHEET).convert("RGBA"))
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        if f.endswith(".png") and f[:-4] not in ARTIST: os.remove(os.path.join(OUT, f))
    crops = {n: sheet[t:b, l:r] for n, (l, t, r, b) in BOXES.items()}
    out = {}
    for name, a in crops.items():
        if name.startswith("btn_"): out[name] = button(a)
        elif name == "frame_free":
            out[name] = frame_free(a)
            out["frame_neon"] = frame_neon(a)
        elif name == "menu_missions": out[name] = missions(a)
        else: out[name] = trim(a)
    out["menu_frame"] = menu_frame(crops["menu_replay"])
    for name, a in out.items():
        Image.fromarray(a).save(os.path.join(OUT, name + ".png"))
        print(f"{name:16s} {a.shape[1]}x{a.shape[0]}")


if __name__ == "__main__":
    main()
