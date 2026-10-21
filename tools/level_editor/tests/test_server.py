#!/usr/bin/env python3
"""The level editor's server (ctest "level_editor"): it serves the app and the game's fonts, lists the levels and the
grids, checks a grid with the game's own solver and a level with its checker (build/gauntlet_check, build/level_check:
skipped if they aren't built), saves levels and grids together (the files and the game's two .inc files: never a grid
or a level that doesn't parse, never a gauntlet in two levels; a level refitted when its gauntlet grows), and turns
away what it shouldn't take. On a scratch copy of the levels and grids, fresh for every test.
"""
import http.client
import json
import os
import re
import shutil
import sys
import tempfile
import threading
import unittest
from http.server import ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "level_editor"))
import server  # noqa: E402

BUILD = os.environ.get("LEVEL_EDITOR_BUILD") or os.path.join(ROOT, "build")  # (ctest's build folder)
CHECKER = os.path.join(BUILD, "gauntlet_check")
LEVEL_CHECKER = os.path.join(BUILD, "level_check")
BOTH = os.path.exists(CHECKER) and os.path.exists(LEVEL_CHECKER)


class LevelEditorServer(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix="level-editor-test-")
        os.makedirs(os.path.join(cls.tmp, "src", "core"))
        cls.cfg = server.Config(root=cls.tmp, grids=os.path.join(cls.tmp, "assets", "gauntlets"), inc=os.path.join(cls.tmp, "src", "core", "GauntletLayouts.inc"),
                                levels=os.path.join(cls.tmp, "assets", "levels"), levels_inc=os.path.join(cls.tmp, "src", "core", "LevelLayouts.inc"),
                                build=BUILD, verbose=False)
        server.Handler.cfg = cls.cfg
        cls.httpd = ThreadingHTTPServer(("127.0.0.1", 0), server.Handler)
        cls.port = cls.httpd.server_address[1]
        threading.Thread(target=cls.httpd.serve_forever, daemon=True).start()

    @classmethod
    def tearDownClass(cls):
        cls.httpd.shutdown()
        cls.httpd.server_close()
        shutil.rmtree(cls.tmp, ignore_errors=True)
        shutil.rmtree(cls.cfg.scratch, ignore_errors=True)

    def setUp(self):  # (the game's levels and grids, fresh)
        for folder in ("gauntlets", "levels"):
            shutil.rmtree(os.path.join(self.tmp, "assets", folder), ignore_errors=True)
            shutil.copytree(os.path.join(ROOT, "assets", folder), os.path.join(self.tmp, "assets", folder))

    def call(self, method, path, body=None):
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=240)
        conn.request(method, path, body=None if body is None else json.dumps(body), headers={"Content-Type": "application/json"})
        res = conn.getresponse()
        data = res.read()
        conn.close()
        ctype = res.getheader("Content-Type", "")
        return res.status, (json.loads(data) if ctype.startswith("application/json") else data)

    def grid(self, name):
        return server.build_gauntlets.read_grid(os.path.join(self.cfg.grids, name + ".txt"))

    def level(self, n):
        with open(server.build_levels.path_of(n, self.cfg.levels)) as f:
            return f.read()

    def test_serves_the_app_and_the_fonts(self):
        status, page = self.call("GET", "/")
        self.assertEqual(status, 200)
        self.assertIn(b"Level editor", page)
        for path in ("/static/js/app.js", "/static/js/grid.js", "/static/js/view.js", "/static/js/course.js", "/static/js/courseview.js",
                     "/static/js/levels.js", "/static/js/gauntlets.js", "/static/js/store.js", "/static/js/dialog.js", "/static/js/util.js",
                     "/static/js/history.js", "/static/js/describe.js", "/static/js/historycard.js", "/static/js/tips.js", "/static/js/tooltip.js",
                     "/static/js/tools.js", "/static/editor.css", "/fonts/ChakraPetch-Bold.ttf"):
            self.assertEqual(self.call("GET", path)[0], 200, path)
        for path in ("/static/../server.py", "/fonts/../../README.md", "/fonts/NotoSansJP-Bold-signs.ttf", "/nothing"):
            self.assertEqual(self.call("GET", path)[0], 404, path)

    def test_serves_the_wiki(self):  # (its pages, as modules and styles the browser takes; nothing beside it)
        for path in ("/wiki", "/wiki/"):
            status, page = self.call("GET", path)
            self.assertEqual(status, 200, path)
            self.assertIn(b"Level Editor Wiki", page)
        for path, kind in (("/wiki/figures.js", "text/javascript"), ("/wiki/wiki.css", "text/css")):
            conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=30)
            conn.request("GET", path)
            res = conn.getresponse()
            res.read()
            conn.close()
            self.assertEqual((res.status, res.getheader("Content-Type", "").split(";")[0]), (200, kind), path)
        for path in ("/wiki/../server.py", "/wiki/../static/index.html", "/wiki/nothing.js", "/wikis/"):
            self.assertEqual(self.call("GET", path)[0], 404, path)

    def test_play_starts_where_asked(self):  # (the command line: Game::debugPlayTest's start; nothing is opened)
        game = self.cfg.game
        base = [game, "--gauntlet-file", "g.txt", "--level-file", "l.txt", "--level", "13"]
        self.assertEqual(server.play_args(self.cfg, ["g.txt"], 13, "l.txt"), base)
        self.assertEqual(server.play_args(self.cfg, ["g.txt"], 13, "l.txt", start={"x": 212.5}), base + ["--start-at", "212.5"])
        self.assertEqual(server.play_args(self.cfg, ["g.txt"], 13, "l.txt", bot=True, start={"col": 40, "row": 3, "dir": -1}),
                         base + ["--start-cell", "40", "3", "left", "--bot", "--tap-once"])
        self.assertEqual(server.play_args(self.cfg, [], 11, at_gauntlet=True, start={"col": 4, "row": 2, "dir": 1}), [game, "--level", "11", "--start-cell", "4", "2"])
        self.assertEqual(server.play_args(self.cfg, [], 11, at_gauntlet=True), [game, "--level", "11", "--gauntlet"])
        for start in (None, {"x": 0}, {"x": 99.5}, {"col": 0, "row": 0}, {"col": 3, "row": 4, "dir": -1}):
            self.assertIsNone(server.valid_start(start), start)
        for start in ("212", [1], {"x": "1"}, {"x": -1}, {"x": True}, {"col": 1}, {"col": 1.5, "row": 2}, {"col": 1, "row": 2, "dir": 0},
                      {"col": -1, "row": 2}, {"col": True, "row": 2}):
            self.assertIsNotNone(server.valid_start(start), start)
        for start in ({"x": "far"}, {"col": 2}, {"col": 2, "row": 3, "dir": 2}):
            self.assertEqual(self.call("POST", "/api/play", {"level": 13, "start": start})[0], 400, start)

    def test_lists_every_level_and_grid(self):
        status, data = self.call("GET", "/api/gauntlets")
        self.assertEqual(status, 200)
        names = [g["name"] for g in data["gauntlets"]]
        self.assertTrue({"tomb", "gallery", "wells", "labyrinth", "ascent"} <= set(names))
        wells = next(g for g in data["gauntlets"] if g["name"] == "wells")
        self.assertEqual((wells["description"], wells["rows"]), self.grid("wells"))
        status, data = self.call("GET", "/api/levels")
        self.assertEqual(status, 200)
        self.assertTrue(set(range(1, 31)) <= {lv["number"] for lv in data["levels"]})
        self.assertEqual(next(lv["text"] for lv in data["levels"] if lv["number"] == 13), self.level(13))

    def test_turns_away_bad_requests(self):
        rows = self.grid("wells")[1]
        for body, why in [({"name": "../x", "rows": rows}, "name"), ({"name": "wells", "rows": []}, "rows"),
                          ({"name": "wells", "rows": rows[:3] + [rows[3][:-1]] + rows[4:]}, "width"),
                          ({"name": "wells", "rows": [rows[0].replace("#", "?", 1)] + rows[1:]}, "cell"),
                          ({"name": "wells", "rows": rows, "level": "15"}, "level")]:
            self.assertEqual(self.call("POST", "/api/check", body)[0], 400, why)
        for body, why in [({"level": 0, "text": "length 30"}, "number"), ({"level": 3, "text": 4}, "text"),
                          ({"level": 3, "text": "", "grids": {"Bad!": rows}}, "grid name"), ({"level": 3, "text": "", "grids": {"wells": ["#?"]}}, "grid")]:
            self.assertEqual(self.call("POST", "/api/levels/check", body)[0], 400, why)
        self.assertEqual(self.call("PUT", "/api/gauntlets/Wells!", {"rows": rows, "description": []})[0], 400)
        self.assertEqual(self.call("PUT", "/api/gauntlets/wells", {"rows": rows, "description": "one string"})[0], 400)
        self.assertEqual(self.call("POST", "/api/save", {"levels": {"x": "length 9"}})[0], 400)
        self.assertEqual(self.call("POST", "/api/play", {"level": -2})[0], 400)

    def test_fingerprints_see_through_mirror_images(self):
        rows = ["#####", "#...#", ">.R.X", "#####"]
        mirrored = ["#####", "#...#", ">.L.X", "#####"]
        self.assertEqual(server.fingerprint(rows), server.fingerprint(mirrored))
        self.assertNotEqual(server.fingerprint(rows), server.fingerprint(["#####", "#.#.#", ">.R.X", "#####"]))

    @unittest.skipUnless(os.path.exists(CHECKER), "build/gauntlet_check isn't built")
    def test_checks_a_grid_with_the_games_solver(self):
        desc, rows = self.grid("wells")
        status, res = self.call("POST", "/api/check", {"name": "wells", "rows": rows})
        self.assertEqual(status, 200)
        self.assertTrue(res["ok"])
        self.assertEqual((res["level"], res["cols"], res["rows"]), (15, len(rows[0]), len(rows)))
        s = res["solution"]
        self.assertTrue(s["reachable"] and s["routes"] == 1 and s["softLocks"] == 0 and s["nearMisses"] >= 15)
        self.assertEqual(len(res["cellMap"]), len(rows))
        self.assertEqual([m["rate"] for m in res["meter"]], [1, 1.25, 1.4])
        self.assertTrue(res["meter"][1]["out"])
        self.assertEqual(len(res["meter"][0]["steps"][0]), 10)  # (c, b, n, d, lay, move, climbed, fell, flags, meter)
        self.assertEqual(res["sameAs"], [])
        self.assertTrue(res["course"]["placed"] and res["course"]["authored"])
        self.assertTrue(res["designs"] and all(d["ok"] for d in res["designs"]), "the wells' signature")
        # a copy of another gauntlet is seen for one, under any name
        _, copy = self.call("POST", "/api/check", {"name": "copy", "rows": self.grid("tomb")[1]})
        self.assertEqual(copy["sameAs"], ["tomb"])
        # a grid that doesn't parse: why
        _, bad = self.call("POST", "/api/check", {"name": "wells", "rows": [r.replace(">", "#") for r in rows]})
        self.assertFalse(bad["ok"])
        self.assertIn("entry", bad["error"])

    @unittest.skipUnless(BOTH, "the checkers aren't built")
    def test_checks_a_grid_in_the_level_as_edited(self):
        # the wells grown a column, its level refitted to match (as the editor keeps them): placed where its span is
        desc, rows = self.grid("wells")
        wider = [r[:5] + r[4] + r[5:] for r in rows]
        text = server.build_levels.refit(self.level(15), "wells", len(wider[0]))
        status, res = self.call("POST", "/api/check", {"name": "wells", "rows": wider, "level": 15, "levelText": text})
        self.assertEqual(status, 200)
        self.assertTrue(res["ok"] and res["course"]["placed"], res.get("error"))
        self.assertAlmostEqual(res["course"]["x1"] - res["course"]["x0"], len(wider[0]) * 0.6, places=3)
        # a level text that doesn't lay it (the span as wide as the old grid): why
        _, bad = self.call("POST", "/api/check", {"name": "wells", "rows": wider, "level": 15, "levelText": self.level(15)})
        self.assertFalse(bad["ok"])
        self.assertIn("cells wide", bad["error"])

    @unittest.skipUnless(BOTH, "the checkers aren't built")
    def test_checks_a_level(self):
        status, res = self.call("POST", "/api/levels/check", {"level": 13, "text": self.level(13)})
        self.assertEqual(status, 200)
        self.assertTrue(res["ok"], res.get("error"))
        self.assertEqual([g["name"] for g in res["gauntlets"]], ["gallery"])
        self.assertTrue(res["walk"] and res["short"] == [] and res["doors"] == [])
        # with an unsaved grid standing in for the saved one (its level refitted)
        desc, rows = self.grid("gallery")
        wider = [r[:5] + r[4] + r[5:] for r in rows]
        text = server.build_levels.refit(self.level(13), "gallery", len(wider[0]))
        _, res = self.call("POST", "/api/levels/check", {"level": 13, "text": text, "grids": {"gallery": wider}})
        self.assertTrue(res["ok"], res.get("error"))
        self.assertEqual(res["gauntlets"][0]["cols"], len(wider[0]))
        # a level that doesn't parse: why
        _, bad = self.call("POST", "/api/levels/check", {"level": 13, "text": "length 400\ncourse\n10 0 crate\n"})
        self.assertFalse(bad["ok"])
        self.assertIn("short of its finish line", bad["error"])
        # a door off the course's height: a door note (the tests fail on it)
        off = self.level(13).replace("3 1 crate 4\n212 0 gauntlet gallery", "3 2 crate 4\n212 0 gauntlet gallery")
        _, res = self.call("POST", "/api/levels/check", {"level": 13, "text": off})
        self.assertTrue(res["ok"] and res["doors"], res)

    @unittest.skipUnless(os.path.exists(CHECKER), "build/gauntlet_check isn't built")
    def test_saves_a_grid_and_the_games_layouts(self):
        desc, rows = self.grid("gallery")
        tuned = rows[:1] + [rows[1]] + rows[2:]
        status, res = self.call("PUT", "/api/gauntlets/gallery", {"rows": tuned, "description": desc + ["Tuned."]})
        self.assertEqual(status, 200, res)
        self.assertEqual(self.grid("gallery"), (desc + ["Tuned."], tuned))
        with open(self.cfg.inc) as f:
            inc = f.read()
        self.assertIn("// Tuned.", inc)
        self.assertEqual(inc, server.build_gauntlets.render(self.cfg.grids))
        # never one that doesn't parse (it would take the gauntlet out of its level)
        status, res = self.call("PUT", "/api/gauntlets/gallery", {"rows": [r.replace("X", "#") for r in rows], "description": desc})
        self.assertEqual(status, 409)
        self.assertIn("not saved", res["error"])
        self.assertEqual(self.grid("gallery")[1], tuned)

    @unittest.skipUnless(BOTH, "the checkers aren't built")
    def test_a_grid_grown_refits_its_level(self):
        desc, rows = self.grid("wells")
        before = self.level(15)
        wider = [r[:5] + r[4] + r[5:] for r in rows]
        status, res = self.call("PUT", "/api/gauntlets/wells", {"rows": wider, "description": desc})
        self.assertEqual(status, 200, res)
        after = self.level(15)
        self.assertEqual(res["refit"], {"15": after})
        self.assertIn("\n%d 0 gauntlet wells\n" % len(wider[0]), after)
        self.assertEqual(after, server.build_levels.refit(before, "wells", len(wider[0])))
        with open(self.cfg.levels_inc) as f:
            self.assertEqual(f.read(), server.build_levels.render(self.cfg.levels))

    @unittest.skipUnless(BOTH, "the checkers aren't built")
    def test_levels_and_grids_saved_together(self):
        # the tomb moved from level 11 to 12: one level at a time, it'd be in two (never saved); both together, saved
        eleven = re.sub(r"\n(\d+) 0 gauntlet tomb\n", lambda m: "\n%s 0 crate\n" % m.group(1), self.level(11))
        tomb_cols = len(self.grid("tomb")[1][0])
        twelve = self.level(12).replace("\ncourse\n", "\ncourse\n30 1 crate\n%d 0 gauntlet tomb\n" % tomb_cols)
        twelve = re.sub(r"\nlength ([\d.]+)\n", lambda m: "\nlength %s\n" % server.build_levels.fmt(float(m.group(1)) + (30 + tomb_cols) * 0.6), twelve)
        twelve = twelve.split("\nsaws\n")[0] + "\nsaws\ndiscos\n"
        status, res = self.call("PUT", "/api/levels/12", {"text": twelve})
        self.assertEqual(status, 409)
        self.assertIn("levels 11 and 12", res["error"])
        self.assertNotIn("gauntlet tomb", self.level(12))
        status, res = self.call("POST", "/api/save", {"levels": {"11": eleven, "12": twelve}})
        self.assertEqual(status, 200, res)
        self.assertEqual((self.level(11), self.level(12)), (eleven, twelve))
        with open(self.cfg.levels_inc) as f:
            self.assertEqual(f.read(), server.build_levels.render(self.cfg.levels))
        # a new gauntlet, saved with the level that lays it
        room = ["#" * 30] + ["#" + "." * 28 + "#"] * 4 + ["." * 30] * 2 + [">" + "." * 28 + "X", "#" * 30]
        three = self.level(3).replace("\ncourse\n", "\ncourse\n30 1 crate\n30 0 gauntlet room\n")
        three = re.sub(r"\nlength ([\d.]+)\n", lambda m: "\nlength %s\n" % server.build_levels.fmt(float(m.group(1)) + 36), three)
        three = three.split("\nsaws\n")[0] + "\nsaws\ndiscos\n"
        status, res = self.call("POST", "/api/save", {"levels": {"3": three}, "gauntlets": {"room": {"rows": room, "description": ["A test room."]}}})
        self.assertEqual(status, 200, res)
        self.assertEqual(self.grid("room"), (["A test room."], room))
        self.assertIn("gauntlet room", self.level(3))
        # a level laying a gauntlet there's none of: never saved
        status, res = self.call("PUT", "/api/levels/4", {"text": self.level(4).replace("\ncourse\n", "\ncourse\n30 0 gauntlet nope\n")})
        self.assertEqual(status, 409)


if __name__ == "__main__":
    unittest.main()
