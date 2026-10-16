#!/usr/bin/env python3
"""The asset scripts (ctest "scripts"; python3 -m unittest discover -s scripts/tests):

  - scripts/models/export_droid.py: the droid.mesh writer, without Blender: the committed mesh reads back to exactly
    what the game expects (DroidModel.cpp's parts, flags, anchors) and writes back byte for byte;
  - scripts/hud/extract_drop_sprites.py: re-running it reproduces the committed Daily Drop PNGs exactly, and the
    labels baked into the sheet (DAY n, CLAIM, CLAIM X2) are gone from them;
  - scripts/hud/make_hud_icons.py: re-running it (raster-only, without Chrome) reproduces the committed atlas and
    HudIcons.h exactly, with every cell no sprite owns left empty.

The image tests need Pillow and numpy (python3 -m pip install pillow numpy); without them they're skipped.
"""
import importlib.util
import os
import shutil
import sys
import tempfile
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

try:
    import numpy as np
    from PIL import Image
    HAVE_IMAGING = True
except ImportError:
    HAVE_IMAGING = False


def read(path, mode="r"):
    with open(path, mode) as f:
        return f.read()


def write(path, data):
    with open(path, "wb") as f:
        f.write(data)


def load(rel, name):
    spec = importlib.util.spec_from_file_location(name, os.path.join(ROOT, rel))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class ExportDroid(unittest.TestCase):
    def setUp(self):
        self.mod = load("scripts/models/export_droid.py", "export_droid")
        self.path = os.path.join(ROOT, "assets", "models", "droid.mesh")

    def test_committed_mesh_is_what_the_game_reads(self):
        parts, anchors = self.mod.read_mesh(self.path)
        # DroidModel.cpp's part names (in its Part order) and anchors
        self.assertEqual([p["name"] for p in parts], ["body", "eye", "fan_l", "fan_r", "shroud", "backplate", "flame"])
        self.assertEqual([a[0] for a in anchors], ["eye", "exhaust_l", "exhaust_r"])
        flags = {p["name"]: p["flags"] for p in parts}
        self.assertEqual(flags["body"] & 1, 1)                    # textured
        self.assertEqual(flags["eye"], 3)                         # textured + glowing
        self.assertEqual(flags["shroud"], 0)                      # plain metal
        for p in parts:
            self.assertEqual(len(p["idx"]) % 3, 0, p["name"])     # whole triangles
            self.assertTrue(all(i < len(p["verts"]) for i in p["idx"]), p["name"])
        self.assertLess(len(parts[0]["verts"]), 65536)            # the body fits 16-bit indices
        fans = [p for p in parts if p["name"].startswith("fan")]
        self.assertAlmostEqual(abs(fans[0]["spin"]), 15.08, places=1)
        self.assertAlmostEqual(fans[0]["spin"], -fans[1]["spin"], places=4)  # opposite directions

    def test_round_trip_byte_for_byte(self):
        parts, anchors = self.mod.read_mesh(self.path)
        with tempfile.TemporaryDirectory() as tmp:
            out = os.path.join(tmp, "droid.mesh")
            self.mod.write_mesh(out, parts, anchors)
            self.assertEqual(read(out, "rb"), read(self.path, "rb"))

    def test_rejects_damaged_files(self):
        data = read(self.path, "rb")
        with tempfile.TemporaryDirectory() as tmp:
            for name, bad in (("magic", b"XSDR" + data[4:]), ("version", data[:4] + b"\x02" + data[5:]), ("truncated", data[:-7]),
                              ("trailing", data + b"\x00")):
                path = os.path.join(tmp, name)
                write(path, bad)
                with self.assertRaises(Exception, msg=name):
                    self.mod.read_mesh(path)


@unittest.skipUnless(HAVE_IMAGING, "needs Pillow and numpy")
class ExtractDropSprites(unittest.TestCase):
    def setUp(self):
        self.mod = load("scripts/hud/extract_drop_sprites.py", "extract_drop_sprites")
        self.committed = os.path.join(ROOT, "assets", "hud", "shop")

    def test_reproduces_the_committed_sprites(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.mod.OUT = tmp
            self.mod.main()
            for name in self.mod.BOXES:
                made = np.asarray(Image.open(os.path.join(tmp, name + ".png")))
                kept = np.asarray(Image.open(os.path.join(self.committed, name + ".png")))
                self.assertEqual(made.shape, kept.shape, name)
                self.assertTrue((made == kept).all(), name + " differs from the committed PNG (re-run the script)")

    @staticmethod
    def lettering(a, tile, white_ok=True):
        """Lettering pixels where a label sits: on a tile, the header band just above its divider line (the brightest
        row in its upper third), pixels much brighter than the band; on a button, the middle band, dark lettering
        (the green CLAIM) or near-white unsaturated lettering right of the TV icon (CLAIM X2)."""
        h, w = a.shape[:2]
        rgb = a[..., :3].astype(int)
        lum = rgb @ np.array([0.299, 0.587, 0.114])
        alpha = a[..., 3]
        cols = slice(int(w * 0.3), int(w * 0.7))
        if tile:
            mid = lum[:, cols].mean(1)
            divider = int(h * 0.15) + int(np.argmax(mid[int(h * 0.15):int(h * 0.35)]))
            rows = slice(divider - int(h * 0.13), divider - 3)
            band = lum[rows, cols]
            return int(((band > np.median(band) + 45) & (alpha[rows, cols] > 150)).sum())
        rows = slice(int(h * 0.3), int(h * 0.7))
        if white_ok:  # CLAIM X2
            cols = slice(int(w * 0.38), int(w * 0.88))
            sat = rgb.max(-1) - rgb.min(-1)
            return int(((rgb[rows, cols].min(-1) > 190) & (sat[rows, cols] < 70) & (alpha[rows, cols] > 150)).sum())
        cols = slice(int(w * 0.2), int(w * 0.8))
        band = lum[rows, cols]
        return int(((band < np.median(band) - 60) & (alpha[rows, cols] > 150)).sum())

    def test_baked_labels_are_painted_out(self):
        sheet = np.asarray(Image.open(self.mod.SHEET).convert("RGBA"))
        for name, (l, t, r, b) in self.mod.BOXES.items():
            if not (name.startswith("drop_tile") or name.startswith("drop_claim")):
                continue
            before = self.mod.trim(sheet[t:b, l:r])
            after = np.asarray(Image.open(os.path.join(self.committed, name + ".png")).convert("RGBA"))
            tile, white = name.startswith("drop_tile"), name == "drop_claim2"
            had, has = self.lettering(before, tile, white), self.lettering(after, tile, white)
            self.assertGreater(had, 40, name + ": the sheet's label wasn't found (test band off?)")
            self.assertLess(has, had * 0.05, "%s: the label is still there (%d of %d pixels)" % (name, has, had))


@unittest.skipUnless(HAVE_IMAGING, "needs Pillow and numpy")
class MakeHudIcons(unittest.TestCase):
    def test_reproduces_the_committed_atlas_and_header(self):
        mod = load("scripts/hud/make_hud_icons.py", "make_hud_icons")
        with tempfile.TemporaryDirectory() as tmp:
            # raster-only mode (no Chrome): the vector sprites come from the committed atlas, the rest is re-packed
            png, header = os.path.join(tmp, "hud_icons.png"), os.path.join(tmp, "HudIcons.h")
            shutil.copy(mod.OUT_PNG, png)
            committed_png, committed_h = mod.OUT_PNG, mod.OUT_H
            mod.OUT_PNG, mod.OUT_H, mod.CHROME = png, header, os.path.join(tmp, "no-chrome")
            mod.main()
            self.assertEqual(read(header), read(committed_h), "HudIcons.h is stale (re-run the script)")
            made, kept = np.asarray(Image.open(png).convert("RGBA")), np.asarray(Image.open(committed_png).convert("RGBA"))
            self.assertEqual(made.shape, kept.shape)
            self.assertTrue((made == kept).all(), "the atlas is stale (re-run the script)")
            # leftover art in a cell no sprite owns (an old sprite moved away) is cleared on the next run
            F, cols = mod.F, mod.COLS
            sprite_cells = min(i for name, i, n, span in mod.ENUM if name.startswith("Outfit"))
            owned = {i + k for name, i, n, span in mod.ENUM if i < sprite_cells for k in range(n * span)}
            spare = [c for c in range(sprite_cells) if c not in owned]
            self.assertTrue(spare, "no spare cell to test with")
            x, y = (spare[0] % cols) * F, (spare[0] // cols) * F
            atlas = Image.open(png).convert("RGBA")
            atlas.paste(Image.new("RGBA", (F, F), (255, 0, 255, 255)), (x, y))
            atlas.save(png)
            mod.main()
            cleared = np.asarray(Image.open(png).convert("RGBA"))[y:y + F, x:x + F]
            self.assertEqual(int(cleared[..., 3].max()), 0, "stray art left in a cell no sprite owns")


if __name__ == "__main__":
    unittest.main()
