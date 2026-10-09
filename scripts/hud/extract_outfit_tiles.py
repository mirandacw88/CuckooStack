#!/usr/bin/env python3
"""Cut the Locker outfit tiles out of the artist's sheet (assets/hud/source/outfit_tiles_sheet.webp, transparent).

    python3 -m pip install pillow numpy scipy
    python3 scripts/hud/extract_outfit_tiles.py
    python3 scripts/hud/make_hud_icons.py          (packs assets/hud/outfits/*.png into the HUD atlas)

The sheet is a 3 x 4 grid of finished tiles (background, frame, hen portrait, name), in catalog order
(src/core/Economy.cpp). Each tile is ~403 x 270 px, which is 3x for the Locker's ~125 pt tiles; the atlas mip chain
serves 2x and 1x screens.

The padlock, the equipped check mark and the WEEK tag are painted on some tiles, but they depend on the player, so
they are removed here and the game draws them. They sit at fixed spots inside the frame:
  - padlock / check (top-right): covered with the tile's top-left corner, mirrored (plain background on every tile
    but Neon Tiger, whose top-left holds the WEEK tag; there the patch comes from just left of the lock instead);
  - WEEK tag (Neon Tiger, top-left): covered with the cleaned top-right corner, mirrored.
Patches are feathered so they blend into the background.
"""
import os

import numpy as np
from PIL import Image
from scipy import ndimage as nd

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SHEET = os.path.join(ROOT, "assets", "hud", "source", "outfit_tiles_sheet.webp")
OUT = os.path.join(ROOT, "assets", "hud", "outfits")

IDS = ["hen_classic", "hen_midnight", "hen_vapor", "hen_toxic", "hen_ice", "hen_lava",
       "hen_gold", "hen_chrome", "hen_tiger", "hen_holo", "hen_sakura", "hen_glitch"]
NO_BADGE = {"hen_midnight"}  # drawn without a lock or check on the sheet

# patch rectangles, as fractions of the tile (x0, y0, x1, y1): inside the frame's border and glow
BADGE = (0.815, 0.06, 0.965, 0.28)
WEEK = (0.0, 0.0, 0.34, 0.26)


def tiles(sheet):
    """The 12 tiles, found as the sheet's opaque blobs, in reading order."""
    lab, _ = nd.label(sheet[..., 3] > 20)
    boxes = [s for s in nd.find_objects(lab) if (s[0].stop - s[0].start) * (s[1].stop - s[1].start) > 5000]
    boxes.sort(key=lambda s: (round(s[0].start / 100), s[1].start))
    assert len(boxes) == 12, f"expected 12 tiles, found {len(boxes)}"
    return [sheet[s].copy() for s in boxes]


def patch(t, box, source):
    """Blend `source` (same shape as t) over the box, with a soft edge so the seam disappears."""
    h, w = t.shape[:2]
    x0, y0, x1, y1 = int(box[0] * w), int(box[1] * h), int(box[2] * w), int(box[3] * h)
    m = np.zeros((h, w))
    m[y0:y1, x0:x1] = 1
    m = np.clip(nd.gaussian_filter(m, 3.0) * 1.5, 0, 1)[..., None]
    return (t * (1 - m) + source * m).round().astype(np.uint8)


def clean(t, name):
    t = t.astype(float)
    if name not in NO_BADGE:
        if name == "hen_tiger":  # top-left is the WEEK tag: take the background just left of the lock
            src = np.roll(t, int((BADGE[2] - BADGE[0]) * t.shape[1]) + 8, axis=1)
        else:
            src = t[:, ::-1]      # mirrored: the top-left corner lands on the top-right
        t = patch(t, BADGE, src).astype(float)
    if name == "hen_tiger":
        t = patch(t, WEEK, t[:, ::-1]).astype(float)
    return t.round().astype(np.uint8)


def main():
    sheet = np.asarray(Image.open(SHEET).convert("RGBA"))
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        if f.endswith(".png"): os.remove(os.path.join(OUT, f))
    for name, t in zip(IDS, tiles(sheet)):
        Image.fromarray(clean(t, name)).save(os.path.join(OUT, name + ".png"))
        print(f"{name:14s} {t.shape[1]}x{t.shape[0]}")


if __name__ == "__main__":
    main()
