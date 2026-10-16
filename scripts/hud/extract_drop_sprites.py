#!/usr/bin/env python3
"""Cut the Daily Drop sprites out of the artist's sheet (assets/hud/source/daily_drop_sheet.webp, transparent).

    python3 -m pip install pillow numpy
    python3 scripts/hud/extract_drop_sprites.py
    python3 scripts/hud/make_hud_icons.py          (packs them into the HUD atlas)

assets/hud/source/daily_drop_mockup.jpg is the layout reference. Writes assets/hud/shop/drop_*.png.

Clean-ups applied to the art:
  - the day tiles: the baked "DAY 1" / "DAY 2" / "DAY 3-6" / "DAY 7" labels are painted out (each row of the label
    is refilled by blending the plain glass on either side of it) so the game prints any day number in its font;
  - the CLAIM / CLAIM X2 buttons: their labels are painted out the same way (the TV icon stays on the x2 button);
  - the panel frame stays 4:1; the game 9-slices it with corners wide enough to keep its tech notches.
"""
import os

import numpy as np
from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SHEET = os.path.join(ROOT, "assets", "hud", "source", "daily_drop_sheet.webp")
OUT = os.path.join(ROOT, "assets", "hud", "shop")

# name: (left, top, right, bottom) on the sheet
BOXES = {
    "drop_frame": (59, 40, 769, 218),
    "drop_tile_green": (58, 242, 291, 499),   # today
    "drop_tile_purple": (299, 256, 506, 485), # tomorrow
    "drop_tile_blue": (526, 256, 733, 485),   # the other days
    "drop_tile_pink": (755, 255, 962, 487),   # day 7
    "drop_coin": (83, 568, 245, 731),
    "drop_chest": (300, 574, 483, 734),
    "drop_tv": (553, 614, 629, 688),
    "drop_claim": (70, 840, 355, 963),
    "drop_claim2": (398, 840, 717, 961),
}


def trim(a, pad=2):
    ys, xs = np.nonzero(a[..., 3] > 4)
    return a[max(ys.min() - pad, 0):ys.max() + pad + 1, max(xs.min() - pad, 0):xs.max() + pad + 1]


def lum(a):
    rgb = a[..., :3].astype(float)
    return rgb @ np.array([0.299, 0.587, 0.114])


def sat(a):
    rgb = a[..., :3].astype(float)
    return rgb.max(-1) - rgb.min(-1)


def paint_out(a, mask, margin=3, period=10, ymargin=1, texture=True):
    """Refill each row's masked run (plus `margin` columns, `ymargin` rows): its brightness blends from the plain pixels
    on the left to those on the right; with `texture`, the glass's dot grid is tiled in from the plain strip just left
    of it, mirrored (smooth surfaces like the buttons go without)."""
    a = a.copy()
    src = a.copy()
    rows = np.nonzero(mask.any(1))[0]
    if not len(rows):
        raise SystemExit("no text found to paint out")
    w = a.shape[1]
    for y in range(max(rows.min() - ymargin, 0), min(rows.max() + ymargin + 1, a.shape[0])):
        cols = np.nonzero(mask[max(y - ymargin - 1, 0):y + ymargin + 2].any(0))[0]
        if not len(cols):
            continue
        x0, x1 = max(cols.min() - margin, period + 2), min(cols.max() + margin, w - period - 3)
        left = src[y, x0 - period - 1:x0 - 1].astype(float)        # plain strip, left of the run
        right = src[y, x1 + 2:x1 + period + 2].astype(float)       # plain strip, right of the run
        lm, rm = left.mean(0), right.mean(0)
        n = x1 - x0 + 1
        t = np.linspace(0, 1, n)[:, None]
        base = lm * (1 - t) + rm * t
        k = np.arange(n)
        # mirrored tiling of the left strip's texture (its deviation from its own mean)
        ph = k % (2 * period)
        idx = np.where(ph < period, period - 1 - ph, ph - period)
        tex = left[idx] - lm if texture else 0.0
        a[y, x0:x1 + 1] = (base + tex).clip(0, 255).round().astype(np.uint8)
    return a


def tile(a):
    """A day tile without its label. The label sits between the frame's top edge and the header's divider line (the
    brightest rows near the top and in the upper third, measured across the middle)."""
    a = trim(a)
    h, w = a.shape[:2]
    L = lum(a)
    mid = L[:, int(w * 0.3):int(w * 0.7)].mean(1)
    lo, hi = int(h * 0.15), int(h * 0.35)
    divider = lo + int(np.argmax(mid[lo:hi]))
    top = int(np.argmax(mid[:int(h * 0.12)]))  # the frame's top edge
    band = np.zeros((h, w), bool)
    band[top + 8:divider - 3, int(w * 0.2):int(w * 0.8)] = True
    ref = np.median(L[band])
    text = band & (L > ref + 45) & (a[..., 3] > 150)
    # a row lit nearly all the way across is the frame's edge or its glow, not the label
    across = text[:, int(w * 0.2):int(w * 0.8)].mean(1)
    text[across > 0.6] = False
    # the label is the run of rows nearest the divider; anything above a gap (bits of the frame's inner line) isn't
    rows = np.nonzero(text.any(1))[0]
    keep = [rows.max()]
    for r in rows[::-1][1:]:
        if keep[-1] - r > 2:
            break
        keep.append(r)
    text[:min(keep)] = False
    # and the letters' dark drop shadow, just around them
    near = text.copy()
    for dy, dx in ((1, 0), (2, 0), (3, 1), (0, 1), (0, -1), (2, 2)):
        near |= np.roll(np.roll(text, dy, 0), dx, 1)
    text |= near & band & (L < ref - 15)
    return paint_out(a, text, margin=6)


def button(a, dark_text):
    """A button without its label (dark text on the green one, white text on the pink one; its TV icon stays)."""
    a = trim(a)
    h, w = a.shape[:2]
    band = np.zeros((h, w), bool)
    # (on the x2 button the label starts right of the TV icon)
    band[int(h * 0.25):int(h * 0.75), int(w * (0.12 if dark_text else 0.36)):int(w * 0.9)] = True
    L, S = lum(a), sat(a)
    ref = np.median(L[band])
    if dark_text:
        text = band & (L < ref - 60)
    else:
        text = band & (L > 215) & (S < 60)  # white, unsaturated (the purple TV is saturated)
    return paint_out(a, text, margin=7, ymargin=3, texture=False)


def main():
    sheet = np.asarray(Image.open(SHEET).convert("RGBA"))
    os.makedirs(OUT, exist_ok=True)
    for name, (l, t, r, b) in BOXES.items():
        a = sheet[t:b, l:r]
        if name.startswith("drop_tile"):
            a = tile(a)
        elif name == "drop_claim":
            a = button(a, dark_text=True)
        elif name == "drop_claim2":
            a = button(a, dark_text=False)
        else:
            a = trim(a)
        Image.fromarray(a).save(os.path.join(OUT, name + ".png"))
        print(f"{name}: {a.shape[1]} x {a.shape[0]}")


if __name__ == "__main__":
    main()
