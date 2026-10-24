#!/usr/bin/env python3
"""The asset scripts (ctest "scripts"; python3 -m unittest discover -s scripts/tests):

  - scripts/models/export_droid.py: the droid.mesh writer, without Blender: the committed mesh reads back to exactly
    what the game expects (DroidModel.cpp's parts, flags, anchors) and writes back byte for byte;
  - scripts/hud/extract_drop_sprites.py: re-running it reproduces the committed Daily Drop PNGs exactly, and the
    labels baked into the sheet (DAY n, CLAIM, CLAIM X2) are gone from them;
  - scripts/hud/make_hud_icons.py: re-running it (raster-only, without Chrome) reproduces the committed atlas and
    HudIcons.h exactly, with every cell no sprite owns left empty;
  - scripts/gauntlets/build_gauntlets.py: the committed GauntletLayouts.inc is what the grids in assets/gauntlets
    build (no hand edits), every grid described and well formed, the first drafts still building, and a grid reading
    back as it was saved (the level editor saves through it);
  - scripts/levels/build_levels.py: the committed LevelLayouts.inc is what the level files in assets/levels build,
    levels 1-30 each with its file, and a level refitted to its gauntlet's grid grown or shrunk (the level editor);
  - scripts/chunks/build_chunks.py: the committed ChunkLayouts.inc is what the chunk files in assets/chunks build, only
    chunk files read (ids by Chunk.h's rule), and a chunk built into the .inc the way LevelCatalog.cpp includes it.

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


class BuildGauntlets(unittest.TestCase):
    CELLS = set("#HBRLSC.>X")

    def setUp(self):
        sys.path.insert(0, os.path.join(ROOT, "scripts", "gauntlets"))
        self.mod = load("scripts/gauntlets/build_gauntlets.py", "build_gauntlets")

    def well_formed(self, name, rows):
        self.assertTrue(rows and all(len(r) == len(rows[0]) for r in rows), "%s: rows of one width" % name)
        self.assertTrue(all(set(r) <= self.CELLS for r in rows), "%s: only Gauntlet.h's cells" % name)
        self.assertEqual([r[0] for r in rows].count(">"), 1, "%s: one entry, on the left edge" % name)
        self.assertEqual([r[-1] for r in rows].count("X"), 1, "%s: one exit, on the right edge" % name)
        self.assertEqual(sum(r.count(">") + r.count("X") for r in rows), 2, "%s: no other doors" % name)

    def test_committed_layouts_are_what_the_grids_build(self):
        self.assertEqual(self.mod.render(), read(self.mod.OUT), "GauntletLayouts.inc is stale (re-run scripts/gauntlets/build_gauntlets.py)")

    def test_every_grid_is_described_and_well_formed(self):
        grids = self.mod.grids()
        self.assertTrue(set(self.mod.DRAFTS) <= {name for name, _, _ in grids}, "a grid for every draft")
        for name, description, rows in grids:
            self.assertTrue(any(line.strip() for line in description), "%s has no description" % name)
            self.well_formed(name, rows)

    def test_drafts_still_build(self):
        for name in self.mod.DRAFTS:
            description, rows = self.mod.draft(name)
            self.assertTrue(description, "%s's draft has no description" % name)
            self.well_formed(name + " (draft)", rows)

    def test_a_grid_saves_and_reads_back_as_it_was(self):
        description, rows = ["Level 99, a test", "", "- two lines"], ["#####", "#...#", ">...X", "#####"]
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "test.txt")
            self.mod.write_grid(path, description, rows)
            self.assertEqual(self.mod.read_grid(path), (description, rows))
            self.assertEqual(read(path), "; Level 99, a test\n;\n; - two lines\n#####\n#...#\n>...X\n#####\n")


class BuildLevels(unittest.TestCase):
    def setUp(self):
        self.mod = load("scripts/levels/build_levels.py", "build_levels")

    def test_committed_levels_are_what_the_files_build(self):
        self.assertEqual(self.mod.render(), read(self.mod.OUT), "LevelLayouts.inc is stale (re-run scripts/levels/build_levels.py)")

    def test_every_campaign_level_has_its_file(self):
        numbers = [n for n, _ in self.mod.levels()]
        self.assertTrue(set(range(1, 31)) <= set(numbers), "levels 1-30 each have a file in assets/levels")
        for n, text in self.mod.levels():
            self.assertIn("\nlength ", "\n" + text, "level %d: a length" % n)
            self.assertIn("\ncourse\n", "\n" + text, "level %d: a course" % n)

    def test_a_level_refits_its_gauntlet(self):
        # (the level editor's save: a grid grown or shrunk, what's after its span in its level moves on or back)
        text = "length 30\ncourse\n10 0 crate\n20 0 gauntlet tomb\n30 0 crate\nsaws\n3 19.5 24.75\ndiscos\n21 1.5\n"
        self.assertEqual(self.mod.gauntlets_in(text), ["tomb"])
        wider = self.mod.refit(text, "tomb", 22)
        self.assertEqual(wider, "length 31.2\ncourse\n10 0 crate\n22 0 gauntlet tomb\n30 0 crate\nsaws\n3 20.7 25.95\ndiscos\n22.2 1.5\n")
        self.assertEqual(self.mod.refit(wider, "tomb", 20), text, "and back")
        self.assertEqual(self.mod.refit(text, "wells", 30), text, "another gauntlet's: as it was")
        self.assertEqual([self.mod.fmt(v) for v in (420.0, 31.2000001, -0.0001, 2.5)], ["420", "31.2", "0", "2.5"])
        for n, level in self.mod.levels():
            for name in self.mod.gauntlets_in(level):
                width = int(next(line for line in level.split("\n") if line.endswith(" 0 gauntlet " + name)).split()[0])
                self.assertEqual(self.mod.refit(self.mod.refit(level, name, width + 3), name, width), level, "level %d: refitted and back" % n)


class BuildChunks(unittest.TestCase):
    def setUp(self):
        self.mod = load("scripts/chunks/build_chunks.py", "build_chunks")

    def test_committed_chunks_are_what_the_files_build(self):
        self.assertEqual(self.mod.render(), read(self.mod.OUT), "ChunkLayouts.inc is stale (re-run scripts/chunks/build_chunks.py)")

    def test_ids_follow_the_chunk_files_rule(self):
        for good in ("a", "sacred-wells", "hurdles_2", "x" * 60):
            self.assertTrue(self.mod.valid_id(good), good)
        for bad in ("", "Sacred", "two words", "../x", "a.b", "x" * 61, None, 3):
            self.assertFalse(self.mod.valid_id(bad), repr(bad))

    def test_only_chunk_files_are_built_in_order(self):
        with tempfile.TemporaryDirectory() as tmp:
            for name, text in (("wells.txt", "chunk course\nranges 1-5\n---\nlength 30\n"), ("arch.txt", "chunk course\nranges 2\n---\nlength 31\n"),
                               ("Bad Name.txt", "x"), (".gitkeep", ""), ("notes.md", "x")):
                with open(os.path.join(tmp, name), "w") as f:
                    f.write(text)
            self.assertEqual([c for c, _ in self.mod.chunks(tmp)], ["arch", "wells"], "by id; a name that isn't an id, or isn't a .txt, is left out")
            out = self.mod.render(tmp)
            self.assertIn('{"arch", R"CHUNK(chunk course\nranges 2\n---\nlength 31\n)CHUNK"},\n{"wells", R"CHUNK(', out)
            self.assertNotIn("Bad", out)
            target = os.path.join(tmp, "out.inc")
            self.mod.write(target, tmp)
            self.assertEqual(read(target), out)
            with open(os.path.join(tmp, "evil.txt"), "w") as f:
                f.write("x)CHUNK\"y")
            with self.assertRaises(ValueError):
                self.mod.render(tmp)

    def test_none_built_in_when_the_folder_is_empty(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(self.mod.chunks(tmp), [])
            self.assertNotIn("R\"CHUNK", self.mod.render(tmp))


class UITestScheme(unittest.TestCase):
    """platforms/ios/make_ui_test_scheme.py: the editor's own scheme is written beside the UI tests' (Xcode makes no
    scheme for a target a shared scheme already names, so without it the project had no CuckooEditor scheme)"""

    PBX = ("\t\tAAAAAAAAAAAAAAAAAAAAAAAA /* CuckooEditor */ = {\n\t\t\tisa = PBXNativeTarget;\n"
           "\t\tBBBBBBBBBBBBBBBBBBBBBBBB /* CuckooEditorUITests */ = {\n\t\t\tisa = PBXNativeTarget;\n")

    def setUp(self):
        self.mod = load("platforms/ios/make_ui_test_scheme.py", "make_ui_test_scheme")

    def build_dir(self, tmp, pbx):
        proj = os.path.join(tmp, "CuckooStack.xcodeproj")
        os.makedirs(proj)
        with open(os.path.join(proj, "project.pbxproj"), "w") as f:
            f.write(pbx)
        return proj

    def test_writes_the_editor_scheme_and_the_ui_test_scheme(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as tmp:
            proj = self.build_dir(tmp, self.PBX)
            self.assertEqual(self.mod.main(__import__("pathlib").Path(tmp)), 0)
            out = os.path.join(proj, "xcshareddata", "xcschemes")
            self.assertEqual(sorted(os.listdir(out)), ["CuckooEditor.xcscheme", "CuckooEditorUITests.xcscheme"])
            for name in ("CuckooEditor", "CuckooEditorUITests"):
                text = read(os.path.join(out, name + ".xcscheme"))
                self.assertIn('BlueprintIdentifier = "AAAAAAAAAAAAAAAAAAAAAAAA"', text, name + ": builds and runs the editor")
                self.assertIn("<LaunchAction", text, name + ": Run launches the editor")
                self.assertIn('<TestableReference skipped = "NO">', text, name + ": Test runs the UI tests")

    def test_no_test_target_writes_nothing(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as tmp:
            proj = self.build_dir(tmp, self.PBX.split("\t\tBBBB")[0])
            self.assertEqual(self.mod.main(__import__("pathlib").Path(tmp)), 0)
            self.assertFalse(os.path.exists(os.path.join(proj, "xcshareddata")))


if __name__ == "__main__":
    unittest.main()
