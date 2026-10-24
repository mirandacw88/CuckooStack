#!/usr/bin/env python3
"""The level editor's sync with the iPhone and iPad editor (server.py --lan; ctest "level_editor"): only with the
pairing code, only /api/sync/* on the network; every level's, grid's and chunk's hash; their files pulled; a level or a grid pushed
written only if the Mac's file is still what the device edited from (compare-and-swap), through the editor's own
checks (the files and the game's .inc; a grid that grew refits its level); a stale one handed back as a conflict, one that doesn't read turned away, one
taken out moved to the trash; nothing written during a git merge. A chunk (assets/chunks/<id>.txt) the same: hashed, pulled,
pushed against its base, read by level_check --chunk (one that doesn't is turned away, the others written), taken out to the
trash, and src/core/ChunkLayouts.inc built after each change. On a scratch copy of the levels, grids and chunks.
"""
import hashlib
import http.client
import json
import os
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

BUILD = os.environ.get("LEVEL_EDITOR_BUILD") or os.path.join(ROOT, "build")
BUILT = os.path.exists(os.path.join(BUILD, "level_check")) and os.path.exists(os.path.join(BUILD, "gauntlet_check"))


CHUNK = ("; Test chunk\nchunk course\nranges 31-35 2-3 7\nweight 4\nuses 2\nmirror yes\n---\n"
         "length 26\ndifficulty 1085 0.35\nraids auto\ncourse\n13 0 crate\n8 3 hay\n10 1 crate\n13 1 crate\nsaws\n9.6\ndiscos\nlasers\n12.6 1 1\nlava\n14.4 3\n")


def sha(text):
    return hashlib.sha256(text.encode()).hexdigest()


class Sync(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix="level-editor-sync-")
        os.makedirs(os.path.join(cls.tmp, "src", "core"))
        os.makedirs(os.path.join(cls.tmp, ".git"))
        cls.cfg = server.Config(root=cls.tmp, grids=os.path.join(cls.tmp, "assets", "gauntlets"), inc=os.path.join(cls.tmp, "src", "core", "GauntletLayouts.inc"),
                                levels=os.path.join(cls.tmp, "assets", "levels"), levels_inc=os.path.join(cls.tmp, "src", "core", "LevelLayouts.inc"),
                                build=BUILD, verbose=False)
        cls.cfg.sync_token = "TEST-CODE"
        server.SyncHandler.cfg = cls.cfg
        cls.httpd = ThreadingHTTPServer(("127.0.0.1", 0), server.SyncHandler)
        cls.port = cls.httpd.server_address[1]
        threading.Thread(target=cls.httpd.serve_forever, daemon=True).start()

    @classmethod
    def tearDownClass(cls):
        cls.httpd.shutdown()
        cls.httpd.server_close()
        shutil.rmtree(cls.tmp, ignore_errors=True)
        shutil.rmtree(cls.cfg.scratch, ignore_errors=True)

    def setUp(self):
        for folder in ("gauntlets", "levels"):
            shutil.rmtree(os.path.join(self.tmp, "assets", folder), ignore_errors=True)
            shutil.copytree(os.path.join(ROOT, "assets", folder), os.path.join(self.tmp, "assets", folder))
        shutil.rmtree(self.cfg.chunks, ignore_errors=True)  # (none to start with: the chunks a test pushes are its own)
        os.makedirs(self.cfg.chunks)
        if os.path.exists(self.cfg.chunks_inc):
            os.remove(self.cfg.chunks_inc)
        merge = os.path.join(self.tmp, ".git", "MERGE_HEAD")
        if os.path.exists(merge):
            os.remove(merge)

    def call(self, method, path, body=None, token="TEST-CODE"):
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=240)
        headers = {"Content-Type": "application/json"}
        if token:
            headers["Authorization"] = "Bearer " + token
        conn.request(method, path, body=None if body is None else json.dumps(body), headers=headers)
        res = conn.getresponse()
        data = res.read()
        conn.close()
        return res.status, json.loads(data) if data else {}

    def level(self, n):
        with open(server.build_levels.path_of(n, self.cfg.levels)) as f:
            return f.read()

    def test_only_with_the_pairing_code_and_only_sync(self):
        self.assertEqual(self.call("GET", "/api/sync/manifest", token=None)[0], 401)
        self.assertEqual(self.call("GET", "/api/sync/manifest", token="WRONG")[0], 401)
        for path in ("/api/levels", "/api/gauntlets", "/", "/wiki/"):
            self.assertEqual(self.call("GET", path)[0], 404, path)
        for path in ("/api/play", "/api/save", "/api/simulator", "/api/levels/check"):
            self.assertEqual(self.call("POST", path, {"level": 1})[0], 404, path)

    def test_manifest_and_pull(self):
        status, m = self.call("GET", "/api/sync/manifest")
        self.assertEqual(status, 200)
        self.assertEqual(m["levels"]["12"], sha(self.level(12)))
        self.assertTrue({"tomb", "gallery"} <= set(m["grids"]))
        self.assertFalse(m["locked"])
        _, got = self.call("POST", "/api/sync/pull", {"levels": [12, 99], "grids": ["tomb"]})
        self.assertEqual(got["levels"], {"12": self.level(12)})
        self.assertEqual(sha(got["grids"]["tomb"]["text"]), m["grids"]["tomb"], "a grid's text: the hash the manifest has")
        self.assertTrue(got["grids"]["tomb"]["rows"])

    @unittest.skipUnless(BUILT, "the checkers aren't built")
    def test_push_compare_and_swap(self):
        before = self.level(3)
        mine = before.replace("raids auto", "raids 2")
        status, res = self.call("POST", "/api/sync/push", {"levels": {"3": {"base": sha(before), "text": mine}}})
        self.assertEqual(status, 200)
        self.assertEqual(res["applied"], ["3"])
        self.assertEqual(self.level(3), mine, "written")
        with open(self.cfg.levels_inc) as f:
            self.assertIn("raids 2", f.read(), "...the game's .inc with it")
        # a device that edited the old text: stale, a conflict handing back the Mac's
        _, res = self.call("POST", "/api/sync/push", {"levels": {"3": {"base": sha(before), "text": before.replace("raids auto", "raids none")}}})
        self.assertEqual(res["applied"], [])
        self.assertEqual(res["conflicts"]["3"], {"text": mine, "hash": sha(mine)})
        self.assertEqual(self.level(3), mine, "nothing written")
        # a new level: base "" (and one that does exist: a conflict)
        _, res = self.call("POST", "/api/sync/push", {"levels": {"31": {"base": "", "text": self.level(1)}, "4": {"base": "", "text": self.level(1)}}})
        self.assertEqual(res["applied"], ["31"])
        self.assertIn("4", res["conflicts"])
        self.assertTrue(os.path.exists(server.build_levels.path_of(31, self.cfg.levels)))

    def grid(self, name):
        with open(os.path.join(self.cfg.grids, name + ".txt")) as f:
            return f.read()

    @unittest.skipUnless(BUILT, "the checkers aren't built")
    def test_grid_pushed_refits_its_level(self):
        before = self.grid("tomb")
        rows = [line for line in before.splitlines() if line and not line.startswith(";")]
        notes = [line for line in before.splitlines() if line.startswith(";")]
        wider = "\n".join(notes + [r[:51] + r[50] + r[51:] for r in rows]) + "\n" # (a column copied in, as the device's maze editor does)
        level = next(n for n, t in server.build_levels.levels(self.cfg.levels) if "tomb" in server.build_levels.gauntlets_in(t))
        old_level = self.level(level)
        status, res = self.call("POST", "/api/sync/push", {"grids": {"tomb": {"base": sha(before), "text": wider}}})
        self.assertEqual(status, 200)
        self.assertEqual(res["gridsApplied"], ["tomb"])
        self.assertEqual(self.grid("tomb"), wider, "written, as its file has it (its description kept)")
        self.assertIn(str(level), res["refit"], "the level that lays it: refitted to its new width, and handed back")
        self.assertEqual(self.level(level), res["refit"][str(level)])
        self.assertNotEqual(self.level(level), old_level)
        with open(self.cfg.inc) as f:
            self.assertIn(rows[0][:51] + rows[0][50] + rows[0][51:], f.read(), "...the game's grids .inc with it")
        # a device that edited the old grid: stale, a conflict handing back the Mac's
        _, res = self.call("POST", "/api/sync/push", {"grids": {"tomb": {"base": sha(before), "text": before}}})
        self.assertEqual(res["gridsApplied"], [])
        self.assertEqual(res["gridConflicts"]["tomb"], {"text": wider, "hash": sha(wider)})
        self.assertEqual(self.grid("tomb"), wider, "nothing written")

    @unittest.skipUnless(BUILT, "the checkers aren't built")
    def test_one_that_doesnt_read_is_turned_away_the_others_written(self):
        five, six = self.level(5), self.level(6)
        status, res = self.call("POST", "/api/sync/push", {"levels": {"5": {"base": sha(five), "text": "length 30\ncourse\n10 0 crate\n"},
                                                                    "6": {"base": sha(six), "text": six.replace("raids auto", "raids 1")}}})
        self.assertEqual(status, 200)
        self.assertEqual(res["applied"], ["6"])
        self.assertIn("5", res["rejected"])
        self.assertEqual(self.level(5), five)

    def test_a_maze_renamed_on_the_device(self):
        # renamed first (its grid and the levels that lay it, by hash after), then the device's push finds them agreed
        grid_hash = self.call("GET", "/api/sync/manifest")[1]["grids"]["gallery"]
        status, res = self.call("POST", "/api/sync/rename", {"from": "gallery", "to": "hall_of_saws"})
        self.assertEqual(status, 200, res)
        manifest = self.call("GET", "/api/sync/manifest")[1]
        self.assertNotIn("gallery", manifest["grids"])
        self.assertEqual(manifest["grids"]["hall_of_saws"], grid_hash)  # (the same file: the device's base still holds)
        (n, text), = res["levels"].items()
        self.assertIn(" 0 gauntlet hall_of_saws\n", text)
        self.assertEqual(manifest["levels"][n], sha(text))
        # asked again (the answer lost on the way): the same; never during a git merge
        self.assertEqual(self.call("POST", "/api/sync/rename", {"from": "gallery", "to": "hall_of_saws"}), (200, res))
        open(os.path.join(self.tmp, ".git", "MERGE_HEAD"), "w").close()
        self.assertEqual(self.call("POST", "/api/sync/rename", {"from": "hall_of_saws", "to": "gallery"})[0], 423)
        self.assertEqual(self.call("POST", "/api/sync/rename", {"from": "wells", "to": "x"}, token="WRONG")[0], 401)

    def test_taken_out_to_the_trash(self):
        text = self.level(7)
        _, res = self.call("POST", "/api/sync/push", {"levels": {}, "deleted": {"7": sha(text)}})
        self.assertEqual(res["applied"], ["7"])
        self.assertFalse(os.path.exists(server.build_levels.path_of(7, self.cfg.levels)))
        trash = os.path.join(BUILD, "editor-trash")
        self.assertTrue(any(f.startswith("level_07-") for f in os.listdir(trash)))
        _, res = self.call("POST", "/api/sync/push", {"deleted": {"8": "stale"}})
        self.assertIn("8", res["conflicts"], "changed on the Mac since: not taken out")

    def test_nothing_written_during_a_git_merge(self):
        with open(os.path.join(self.tmp, ".git", "MERGE_HEAD"), "w") as f:
            f.write("x\n")
        self.assertTrue(self.call("GET", "/api/sync/manifest")[1]["locked"])
        status, res = self.call("POST", "/api/sync/push", {"levels": {"3": {"base": "", "text": "x"}}})
        self.assertEqual(status, 423)
        self.assertIn("merge", res["error"])

    def test_turns_away_bad_requests(self):
        for body in ({"levels": []}, {"levels": {"x": {"text": "a"}}}, {"levels": {"3": {"base": 1, "text": "a"}}}, {"deleted": {"3": 4}},
                     {"grids": []}, {"grids": {"Bad Name": {"base": "", "text": "##\n"}}}, {"grids": {"tomb": {"base": "", "text": "#?#\n"}}}):
            self.assertEqual(self.call("POST", "/api/sync/push", body)[0], 400, body)
        self.assertEqual(self.call("POST", "/api/sync/pull", {"levels": "12"})[0], 400)

    # ---- chunks (assets/chunks/<id>.txt)

    def chunk(self, chunk_id):
        with open(server.build_chunks.path_of(chunk_id, self.cfg.chunks)) as f:
            return f.read()

    def chunk_inc(self):
        with open(self.cfg.chunks_inc) as f:
            return f.read()

    def test_the_chunks_folder_is_the_scratch_one(self):
        self.assertTrue(self.cfg.chunks.startswith(self.tmp), "(never the real assets/chunks)")
        self.assertTrue(self.cfg.chunks_inc.startswith(self.tmp))
        self.assertEqual(server.Config(root="/x").chunks, "/x/assets/chunks", "by default a checkout's assets/chunks...")
        self.assertEqual(server.Config(root="/x").chunks_inc, "/x/src/core/ChunkLayouts.inc", "...and its ChunkLayouts.inc")

    def test_chunks_in_the_manifest_and_pulled(self):
        self.assertEqual(self.call("GET", "/api/sync/manifest")[1]["chunks"], {}, "none yet")
        with open(server.build_chunks.path_of("wells", self.cfg.chunks), "w") as f:
            f.write(CHUNK)
        with open(os.path.join(self.cfg.chunks, ".gitkeep"), "w"):
            pass
        with open(os.path.join(self.cfg.chunks, "Not A Chunk.txt"), "w") as f:
            f.write("x")
        _, m = self.call("GET", "/api/sync/manifest")
        self.assertEqual(m["chunks"], {"wells": sha(CHUNK)}, "a chunk file's hash (a file that isn't named like one is left out)")
        _, got = self.call("POST", "/api/sync/pull", {"chunks": ["wells", "nope", "Bad Id"], "levels": [12]})
        self.assertEqual(got["chunks"], {"wells": CHUNK}, "its text; the ones the Mac hasn't are left out")
        self.assertEqual(got["levels"], {"12": self.level(12)}, "(beside the levels)")
        self.assertEqual(self.call("POST", "/api/sync/pull", {})[1]["chunks"], {}, "a pull that asks for none gets none")

    @unittest.skipUnless(BUILT, "the checkers aren't built")
    def test_chunk_push_compare_and_swap_and_the_bake(self):
        self.assertFalse(os.path.exists(self.cfg.chunks_inc), "(none built before the first push)")
        status, res = self.call("POST", "/api/sync/push", {"chunks": {"wells": {"base": "", "text": CHUNK}}})
        self.assertEqual(status, 200, res)
        self.assertEqual(res["chunksApplied"], ["wells"])
        self.assertEqual(res["chunkConflicts"], {})
        self.assertEqual(res["rejected"], {})
        self.assertEqual(self.chunk("wells"), CHUNK, "written as sent")
        self.assertIn('{"wells", R"CHUNK(' + CHUNK + ')CHUNK"},', self.chunk_inc(), "...and the game's ChunkLayouts.inc built from the files")
        self.assertEqual(self.call("GET", "/api/sync/manifest")[1]["chunks"]["wells"], sha(CHUNK))
        # edited from that base
        mine = CHUNK.replace("weight 4", "weight 7")
        _, res = self.call("POST", "/api/sync/push", {"chunks": {"wells": {"base": sha(CHUNK), "text": mine}}})
        self.assertEqual(res["chunksApplied"], ["wells"])
        self.assertEqual(self.chunk("wells"), mine)
        self.assertIn("weight 7", self.chunk_inc(), "the .inc follows every push")
        # a device that edited the old text: stale, a conflict handing back the Mac's; nothing written
        _, res = self.call("POST", "/api/sync/push", {"chunks": {"wells": {"base": sha(CHUNK), "text": CHUNK.replace("weight 4", "weight 2")}}})
        self.assertEqual(res["chunksApplied"], [])
        self.assertEqual(res["chunkConflicts"]["wells"], {"text": mine, "hash": sha(mine)})
        self.assertEqual(self.chunk("wells"), mine)
        # a new chunk whose id the Mac has (base ""): a conflict too; a new id: written
        _, res = self.call("POST", "/api/sync/push", {"chunks": {"wells": {"base": "", "text": CHUNK}, "arch": {"base": "", "text": CHUNK.replace("ranges 31-35 2-3 7", "ranges 36-40")}}})
        self.assertEqual(res["chunksApplied"], ["arch"])
        self.assertIn("wells", res["chunkConflicts"])
        self.assertEqual(sorted(f for f in os.listdir(self.cfg.chunks) if f.endswith(".txt")), ["arch.txt", "wells.txt"])
        self.assertLess(self.chunk_inc().index('{"arch"'), self.chunk_inc().index('{"wells"'), "(the .inc by id)")
        # the base of one the Mac doesn't have: stale
        _, res = self.call("POST", "/api/sync/push", {"chunks": {"ghost": {"base": sha("x"), "text": CHUNK}}})
        self.assertIn("ghost", res["chunkConflicts"])
        self.assertEqual(res["chunkConflicts"]["ghost"], {"text": "", "hash": ""})
        self.assertFalse(os.path.exists(server.build_chunks.path_of("ghost", self.cfg.chunks)))

    @unittest.skipUnless(BUILT, "the checkers aren't built")
    def test_a_chunk_that_doesnt_read_is_turned_away_the_others_written(self):
        broken = CHUNK.replace("13 0 crate\n8 3 hay", "13 0 crate 4\n8 3 hay")  # (a barrier over the run-in)
        no_ranges = CHUNK.replace("ranges 31-35 2-3 7\n", "")
        status, res = self.call("POST", "/api/sync/push", {"chunks": {"good": {"base": "", "text": CHUNK}, "pads": {"base": "", "text": broken}, "bare": {"base": "", "text": no_ranges}},
                                                           "levels": {"6": {"base": sha(self.level(6)), "text": self.level(6).replace("raids auto", "raids 1")}}})
        self.assertEqual(status, 200, res)
        self.assertEqual(res["chunksApplied"], ["good"])
        self.assertIn("chunk pads", res["rejected"])
        self.assertIn("chunk bare", res["rejected"])
        self.assertIn("run-in", res["rejected"]["chunk pads"], "why, as the core says it")
        self.assertEqual(res["applied"], ["6"], "(a level in the same push is still written)")
        self.assertTrue(os.path.exists(server.build_chunks.path_of("good", self.cfg.chunks)))
        self.assertFalse(os.path.exists(server.build_chunks.path_of("pads", self.cfg.chunks)))
        self.assertNotIn("pads", self.chunk_inc())
        self.assertIn('{"good",', self.chunk_inc())

    @unittest.skipUnless(BUILT, "the checkers aren't built")
    def test_a_maze_chunk_that_cant_be_played_is_turned_away_with_why(self):
        def maze(rows, mirror="yes"):
            return "chunk maze\nranges 21-30\nweight 3\nuses 1\nmirror %s\n---\n%s\n" % (mirror, "\n".join(rows))
        well = maze([".....", ".....", "##...", "##S##"])
        pit = maze(["##.##", "#...#"])
        stone = maze(["#####", "#####", "#####"], "no")
        gates = maze(["..R.."] * 8 + ["#####"])  # (playable as drawn, a wall of one-way gates; flipped they face away from her)
        gates_as_drawn = gates.replace("mirror yes", "mirror no")
        pushed = {i: {"base": "", "text": t} for i, t in {"well": well, "pit": pit, "stone": stone, "gates": gates, "gates-drawn": gates_as_drawn}.items()}
        status, res = self.call("POST", "/api/sync/push", {"chunks": pushed})
        self.assertEqual(status, 200, res)
        self.assertEqual(sorted(res["chunksApplied"]), ["gates-drawn", "well"], "the playable ones (flipped too) are written")
        for chunk_id in ("pit", "stone", "gates"):
            self.assertIn("chunk " + chunk_id, res["rejected"])
            self.assertFalse(os.path.exists(server.build_chunks.path_of(chunk_id, self.cfg.chunks)), chunk_id + " isn't written")
        self.assertIn("unplayable in a corridor", res["rejected"]["chunk pit"])
        self.assertIn("no way to the exit", res["rejected"]["chunk stone"])
        self.assertIn("flipped: no way to the exit", res["rejected"]["chunk gates"], "(the reason says it is the flipped one)")
        self.assertIn('{"well",', self.chunk_inc())
        self.assertNotIn('{"pit",', self.chunk_inc())
        # the checker's own answer: the corridor's report in it
        verdict = server.check_chunk(self.cfg, "well", well)
        self.assertTrue(verdict["ok"] and verdict["report"]["solution"]["reachable"] and "flipped" in verdict)
        self.assertFalse(server.check_chunk(self.cfg, "pit", pit)["ok"])

    @unittest.skipUnless(BUILT, "the checkers aren't built")
    def test_a_chunk_taken_out_to_the_trash(self):
        self.call("POST", "/api/sync/push", {"chunks": {"wells": {"base": "", "text": CHUNK}, "arch": {"base": "", "text": CHUNK}}})
        _, res = self.call("POST", "/api/sync/push", {"deletedChunks": {"wells": sha(CHUNK)}})
        self.assertEqual(res["chunksApplied"], ["wells"])
        self.assertFalse(os.path.exists(server.build_chunks.path_of("wells", self.cfg.chunks)))
        self.assertTrue(os.path.exists(server.build_chunks.path_of("arch", self.cfg.chunks)))
        trash = os.path.join(BUILD, "editor-trash")
        mine = [f for f in os.listdir(trash) if f.startswith("chunk_wells-")]
        self.assertTrue(mine)
        with open(os.path.join(trash, mine[0])) as f:
            self.assertEqual(f.read(), CHUNK, "the file kept, not lost")
        for f in mine:
            os.remove(os.path.join(trash, f))
        self.assertNotIn('{"wells"', self.chunk_inc(), "taken out of the .inc")
        self.assertIn('{"arch"', self.chunk_inc())
        # changed on the Mac since: not taken out; already gone: agreed
        _, res = self.call("POST", "/api/sync/push", {"deletedChunks": {"arch": "stale"}})
        self.assertEqual(res["chunksApplied"], [])
        self.assertEqual(res["chunkConflicts"]["arch"], {"text": CHUNK, "hash": sha(CHUNK)})
        self.assertTrue(os.path.exists(server.build_chunks.path_of("arch", self.cfg.chunks)))
        _, res = self.call("POST", "/api/sync/push", {"deletedChunks": {"wells": sha(CHUNK)}})
        self.assertEqual(res["chunksApplied"], ["wells"], "gone already (the answer lost on the way): agreed")

    def test_nothing_of_a_chunk_written_during_a_git_merge(self):
        with open(os.path.join(self.tmp, ".git", "MERGE_HEAD"), "w") as f:
            f.write("x\n")
        self.assertEqual(self.call("POST", "/api/sync/push", {"chunks": {"wells": {"base": "", "text": CHUNK}}})[0], 423)
        self.assertFalse(os.path.exists(server.build_chunks.path_of("wells", self.cfg.chunks)))

    def test_turns_away_bad_chunk_requests(self):
        for body in ({"chunks": []}, {"chunks": {"wells": "x"}}, {"chunks": {"wells": {"base": 1, "text": CHUNK}}}, {"chunks": {"wells": {"base": "", "text": 4}}},
                     {"chunks": {"Bad Id": {"base": "", "text": CHUNK}}}, {"chunks": {"../x": {"base": "", "text": CHUNK}}}, {"chunks": {"x" * 61: {"base": "", "text": CHUNK}}},
                     {"chunks": {"wells": {"base": "", "text": "just some words"}}}, {"chunks": {"wells": {"base": "", "text": "chunk course\nranges 1\n"}}},
                     {"deletedChunks": []}, {"deletedChunks": {"Bad Id": "x"}}, {"deletedChunks": {"wells": 3}}):
            self.assertEqual(self.call("POST", "/api/sync/push", body)[0], 400, body)
        self.assertEqual(self.call("POST", "/api/sync/pull", {"chunks": "wells"})[0], 400)
        self.assertEqual(os.listdir(self.cfg.chunks), [], "nothing written")

    def test_chunk_checks_ids_and_shapes(self):
        self.assertIsNone(server.valid_chunk("sacred-wells_2"))
        self.assertIsNone(server.valid_chunk("a", "chunk maze\nranges 1\n---\n##\n"))
        self.assertIsNotNone(server.valid_chunk("A"))
        self.assertIsNotNone(server.valid_chunk("a", "chunk blob\n---\n"))
        self.assertIsNotNone(server.valid_chunk("a", "x" * (server.MAX_TEXT + 1)))
        self.assertIsNone(server.valid_chunk("a"), "no text: just the id")

    @unittest.skipUnless(BUILT, "the checkers aren't built")
    def test_check_chunk_is_level_checks(self):
        ok = server.check_chunk(self.cfg, "wells", CHUNK)
        self.assertTrue(ok["ok"])
        self.assertTrue(ok["mirrors"])
        self.assertIn("flipped", ok, "Mirror on: judged flipped too")
        self.assertNotIn("flipped", server.check_chunk(self.cfg, "wells", CHUNK.replace("mirror yes", "mirror no")))
        bad = server.check_chunk(self.cfg, "wells", "chunk course\nranges 1\n---\nlength 30\n")
        self.assertFalse(bad["ok"])
        self.assertTrue(bad["error"])
        self.assertFalse(server.check_chunk(self.cfg, "Bad Id", CHUNK)["ok"])

    def test_pairing_code_kept(self):
        cfg = server.Config(root=self.tmp, build=tempfile.mkdtemp(prefix="sync-build-"), verbose=False)
        try:
            a = server.pairing_code(cfg)
            self.assertRegex(a, r"^[A-Z2-9]{4}-[A-Z2-9]{4}$")
            self.assertEqual(server.pairing_code(cfg), a, "the same till it's deleted")
        finally:
            shutil.rmtree(cfg.build, ignore_errors=True)
            shutil.rmtree(cfg.scratch, ignore_errors=True)


if __name__ == "__main__":
    unittest.main()
