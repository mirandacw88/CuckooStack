#!/usr/bin/env python3
"""The level editor: a web app, on this machine, to tune the campaign's levels (assets/levels/level_NN.txt: each course
wall by wall, its saws, disco balls, finish line, difficulty and raids, and which maze gauntlet it plays, where) and the
gauntlets' grids (assets/gauntlets/<name>.txt), the game's own checkers judging every edit (docs/LEVEL_DESIGN.md, "The
level editor").

    python3 tools/level_editor/server.py [--port 8765] [--open] [--no-build]

then open http://127.0.0.1:8765. It builds what it runs first (cmake --build build: build/gauntlet_check and
build/level_check, the checkers, and build/cuckoo_stack, the desktop game) unless --no-build. Its API (JSON):

    GET  /api/levels                        every level as saved: number, text
    GET  /api/gauntlets                     every grid as saved: name, description, rows
    POST /api/levels/check {level, text, grids?, game?}
                                            build/level_check --json on the text (grids: unsaved grids standing in
                                            for theirs): the course's walls, saws and gauntlets, the egg budget, notes;
                                            game: also the real game's bot playing it (headless)
    POST /api/check {name, rows, level?, levelText?, game?}
                                            build/gauntlet_check --json on the rows, as that level plays it (its text:
                                            the level as edited): the solution, the bot's way, the meter runs, near
                                            misses, its design's signature, where it is on the course; game: also the
                                            real game's bot through it; plus whether it's unlike every other saved grid
    POST /api/save {levels: {N: text}, gauntlets: {name: {description, rows}}}
                                            saves them together (a gauntlet moved from one level to another changes
                                            both): each checked as the game will read it, a saved level whose gauntlet
                                            grew or shrank refitted, no gauntlet in two levels; then the files, and the
                                            game's src/core/LevelLayouts.inc and GauntletLayouts.inc
    PUT  /api/levels/<N> {text}, PUT /api/gauntlets/<name> {description, rows}   one of them saved, the same way
    POST /api/play {level, levelText?, grids?, bot?, atGauntlet?, start?}
                                            plays the level in the desktop game, as edited (atGauntlet: every run from
                                            its gauntlet's door; start: {x} metres along, or {col, row, dir} a cell of its
                                            maze: every run from there; bot: the bot plays)
    GET  /wiki/                             the editor's documentation (tools/level_editor/wiki)
    POST /api/simulator                     builds the iOS app with the saved levels and grids (build-ios) and installs
                                            it on every booted iOS Simulator; GET /api/simulator: how that's going

Python 3.9's standard library only. It listens on 127.0.0.1 alone.
"""
import argparse
import json
import os
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import webbrowser
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import unquote, urlparse

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "scripts", "gauntlets"))
sys.path.insert(0, os.path.join(ROOT, "scripts", "levels"))
import build_gauntlets  # noqa: E402  (the grids' format, and the .inc)
import build_levels  # noqa: E402  (the levels' files, and the .inc)

STATIC = os.path.join(HERE, "static")
WIKI = os.path.join(HERE, "wiki")
FONTS = os.path.join(ROOT, "assets", "fonts")
FONT_FILES = {"CuckooDisplay-Black.ttf", "ChakraPetch-Medium.ttf", "ChakraPetch-Bold.ttf"}
CELLS = set("#HBRLSC.>X")
NAME = re.compile(r"^[a-z][a-z0-9_]{0,31}$")
MAX_COLS, MAX_ROWS, MAX_LEVEL, MAX_TEXT = 800, 64, 999, 400_000
TYPES = {".html": "text/html; charset=utf-8", ".js": "text/javascript; charset=utf-8", ".css": "text/css; charset=utf-8",
         ".svg": "image/svg+xml", ".png": "image/png", ".ttf": "font/ttf", ".json": "application/json"}


class Config:
    """where it all is (the tests point it at a scratch copy)"""

    def __init__(self, root=ROOT, grids=None, inc=None, build=None, verbose=True, levels=None, levels_inc=None):
        self.root = root
        self.verbose = verbose
        self.grids = grids or os.path.join(root, "assets", "gauntlets")
        self.inc = inc or os.path.join(root, "src", "core", "GauntletLayouts.inc")
        self.levels = levels or os.path.join(root, "assets", "levels")
        self.levels_inc = levels_inc or os.path.join(root, "src", "core", "LevelLayouts.inc")
        self.build = build or os.path.join(root, "build")
        self.checker = os.path.join(self.build, "gauntlet_check")
        self.level_checker = os.path.join(self.build, "level_check")
        self.game = os.path.join(self.build, "cuckoo_stack")
        self.lock = threading.Lock()   # (saves, and the game runs below)
        self.games = {}                # what a game run checks (a gauntlet, a level) -> its run (a newer one stops it)
        self.playing = None            # the desktop game playing a level
        self.scratch = tempfile.mkdtemp(prefix="cuckoo-level-editor-")
        self.simulator = SimulatorJob(self)


class SaveError(Exception):
    """what's wrong with a save (nothing written)"""


def fingerprint(rows):
    """Gauntlet::fingerprint: a grid and its mirror image (doors and gates turned round) the same"""
    swap = {">": "X", "X": ">", "R": "L", "L": "R"}
    a = "".join(r + "/" for r in rows)
    m = "".join("".join(swap.get(ch, ch) for ch in reversed(r)) + "/" for r in rows)
    return min(a, m)


def valid_rows(rows):
    """rows as the editor sends them: a list of strings of one width, Gauntlet.h's cells (the checker judges the rest)"""
    if not isinstance(rows, list) or not rows or len(rows) > MAX_ROWS:
        return "the grid needs 1 to %d rows" % MAX_ROWS
    width = len(rows[0]) if isinstance(rows[0], str) else 0
    if not 0 < width <= MAX_COLS:
        return "the grid needs 1 to %d columns" % MAX_COLS
    for i, r in enumerate(rows):
        if not isinstance(r, str) or len(r) != width:
            return "row %d isn't %d wide" % (i, width)
        bad = set(r) - CELLS
        if bad:
            return "row %d has an unknown cell %r" % (i, sorted(bad)[0])
    return None


def valid_grids(grids):
    """{name: rows}: unsaved grids standing in for saved ones"""
    if grids is None:
        return None
    if not isinstance(grids, dict):
        return "grids: {name: rows}"
    for name, rows in grids.items():
        if not NAME.match(str(name)):
            return "a gauntlet's name: lower case letters, digits, _"
        problem = valid_rows(rows)
        if problem:
            return "%s: %s" % (name, problem)
    return None


def valid_level(level, text=None):
    if not isinstance(level, int) or isinstance(level, bool) or not 0 < level <= MAX_LEVEL:
        return "a level's number: 1 to %d" % MAX_LEVEL
    if text is not None and (not isinstance(text, str) or len(text) > MAX_TEXT):
        return "a level's text: a string"
    return None


def last_json(text):
    """the checker's JSON (the game may print a line or two first)"""
    for line in reversed(text.splitlines()):
        if line.startswith("{"):
            return json.loads(line)
    return None


class Scratch:
    """a folder of a request's own for the files it hands the checker or the game (they name a layout for its file)"""

    def __init__(self, cfg, keep=None):
        self.dir = os.path.join(cfg.scratch, keep) if keep else tempfile.mkdtemp(dir=cfg.scratch)
        if keep:
            shutil.rmtree(self.dir, ignore_errors=True)
            os.makedirs(self.dir)
        self.keep = keep

    def grid(self, name, rows):
        path = os.path.join(self.dir, name + ".txt")
        build_gauntlets.write_grid(path, [], rows)
        return path

    def level(self, n, text):
        path = build_levels.path_of(n, self.dir)
        with open(path, "w") as f:
            f.write(text)
        return path

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        if not self.keep:
            shutil.rmtree(self.dir, ignore_errors=True)


def grid_files(cfg, scratch, unsaved=None):
    """every grid as saved (the game may have been built before it was), the unsaved ones over them: their files"""
    unsaved = unsaved or {}
    files = [os.path.join(cfg.grids, n + ".txt") for n, _, _ in build_gauntlets.grids(cfg.grids) if n not in unsaved]
    return files + [scratch.grid(n, rows) for n, rows in sorted(unsaved.items())]


def run_checker(cfg, args, stdin, key=None, timeout=60):
    """a checker run: (its JSON, or an error). With a key (a game run: seconds of the real game), one at a time per key:
    the newest wins, an older one stopped"""
    if key is None:
        done = subprocess.run(args, input=stdin, capture_output=True, text=True, timeout=timeout, cwd=cfg.build)
        out, err = done.stdout, done.stderr
    else:
        proc = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=cfg.build)
        with cfg.lock:
            old = cfg.games.get(key)
            cfg.games[key] = proc
        if old and old.poll() is None:
            old.kill()
        try:
            out, err = proc.communicate(stdin, timeout=max(timeout, 240))
        except subprocess.TimeoutExpired:
            proc.kill()
            return {"ok": False, "error": "the game run took too long"}
        if proc.returncode < 0:
            return {"ok": False, "cancelled": True, "error": "a newer run took over"}
    result = last_json(out)
    if result is None:
        return {"ok": False, "error": (err or out or "no answer from the checker").strip()[-400:]}
    return result


def check(cfg, name, rows, game=False, level=0, level_text=None):
    """a grid, as its level plays it (the level as edited, if its text comes too)"""
    if not os.path.exists(cfg.checker):
        return {"ok": False, "error": "build/gauntlet_check isn't built (cmake --build build --target gauntlet_check)"}
    with Scratch(cfg) as sc:
        args = [cfg.checker, "--file", "-", "--name", name, "--json"] + (["--game"] if game else [])
        if level:
            args += ["--level", str(level)]
            if level_text:
                args += ["--level-file", sc.level(level, level_text)]
        result = run_checker(cfg, args, "\n".join(rows) + "\n", key=("gauntlet", name) if game else None)
    if result.get("ok") or "solution" in result:
        mine = fingerprint(rows)
        result["sameAs"] = [other for other, _, other_rows in build_gauntlets.grids(cfg.grids) if other != name and fingerprint(other_rows) == mine]
    return result


def check_level(cfg, level, text, grids=None, game=False):
    """a level's text, its gauntlet's grid as given (unsaved) or as saved"""
    if not os.path.exists(cfg.level_checker):
        return {"ok": False, "error": "build/level_check isn't built (cmake --build build --target level_check)"}
    with Scratch(cfg) as sc:
        args = [cfg.level_checker, "--file", "-", "--level", str(level), "--json"] + (["--game"] if game else [])
        for path in grid_files(cfg, sc, grids):
            args += ["--gauntlet-file", path]
        return run_checker(cfg, args, text, key=("level", level) if game else None)


def save_all(cfg, levels=None, gauntlets=None):
    """Levels and grids saved together: checked as the game will read them (every grid parses; every level saved
    parses with the grids as they'll be), a saved level whose gauntlet grew or shrank refitted (what's after it moved
    on or back), no gauntlet in two levels; then written, with the game's two .inc files. Nothing is written if
    anything's wrong (SaveError says what)."""
    levels, gauntlets = dict(levels or {}), dict(gauntlets or {})
    with cfg.lock:
        saved_grids = {n: rows for n, _, rows in build_gauntlets.grids(cfg.grids)}
        saved_levels = dict(build_levels.levels(cfg.levels))
        grids_after = dict(saved_grids)
        grids_after.update({n: g["rows"] for n, g in gauntlets.items()})
        levels_after = dict(saved_levels)
        levels_after.update(levels)
        refit = {}
        for name, g in gauntlets.items():
            for n, text in levels_after.items():
                if n not in levels:
                    t = build_levels.refit(text, name, len(g["rows"][0]))
                    if t != text:
                        refit[n] = t
        levels_after.update(refit)
        unsaved_grids = {n: g["rows"] for n, g in gauntlets.items()}
    # (the checks run outside the lock: a check of its own takes it for its game run)
    for name, g in sorted(gauntlets.items()):
        if os.path.exists(cfg.checker):
            verdict = check(cfg, name, g["rows"])
            if not verdict.get("ok"):
                raise SaveError("the %s's grid: %s" % (name, verdict.get("error", "it doesn't parse")))
    for n in sorted(set(levels) | set(refit)):
        if os.path.exists(cfg.level_checker):
            verdict = check_level(cfg, n, levels_after[n], unsaved_grids)
            if not verdict.get("ok"):
                raise SaveError("level %d: %s" % (n, verdict.get("error", "it doesn't parse")))
    where = {}
    for n in sorted(levels_after):
        for name in build_levels.gauntlets_in(levels_after[n]):
            if name in where and where[name] != n:
                raise SaveError("the %s would be in levels %d and %d: a gauntlet plays in one level (take it out of one)" % (name, where[name], n))
            if name not in grids_after:
                raise SaveError("level %d lays the %s, and there's no such gauntlet" % (n, name))
            where[name] = n
    wrote = []
    with cfg.lock:
        for name, g in sorted(gauntlets.items()):
            path = os.path.join(cfg.grids, name + ".txt")
            build_gauntlets.write_grid(path, g.get("description", []), g["rows"])
            wrote.append(os.path.relpath(path, cfg.root))
        for n in sorted(set(levels) | set(refit)):
            path = build_levels.path_of(n, cfg.levels)
            with open(path, "w") as f:
                f.write(levels_after[n])
            wrote.append(os.path.relpath(path, cfg.root))
        incs = []
        if gauntlets:
            with open(cfg.inc, "w") as f:
                f.write(build_gauntlets.render(cfg.grids))
            incs.append(os.path.relpath(cfg.inc, cfg.root))
        if levels or refit:
            build_levels.write(cfg.levels_inc, cfg.levels)
            incs.append(os.path.relpath(cfg.levels_inc, cfg.root))
    return {"ok": True, "wrote": wrote, "incs": incs, "refit": {str(n): t for n, t in refit.items()}}


def vulkan_env():
    """the desktop game's Vulkan on a Mac without the SDK: the Android emulator's MoltenVK"""
    env = dict(os.environ)
    vk = os.environ.get("LEVEL_EDITOR_VULKAN") or os.path.expanduser("~/Library/Android/sdk/emulator/lib64/vulkan")
    if "VK_ICD_FILENAMES" not in env and os.path.exists(os.path.join(vk, "MoltenVK_icd.json")):
        env["DYLD_LIBRARY_PATH"] = vk + (":" + env["DYLD_LIBRARY_PATH"] if env.get("DYLD_LIBRARY_PATH") else "")
        env["VK_ICD_FILENAMES"] = os.path.join(vk, "MoltenVK_icd.json")
    return env


def valid_start(start):
    """where the play-test starts: {x} metres along, or {col, row, dir} a cell of its maze (None: its beginning)"""
    if start is None:
        return None
    if not isinstance(start, dict):
        return "a start: {x} or {col, row, dir}"
    num = lambda v: isinstance(v, (int, float)) and not isinstance(v, bool)
    if "x" in start:
        return None if num(start["x"]) and 0 <= start["x"] <= 100000 else "a start's x: metres along"
    ok = all(isinstance(start.get(k), int) and not isinstance(start.get(k), bool) and 0 <= start[k] < 1000 for k in ("col", "row"))
    return None if ok and start.get("dir", 1) in (1, -1) else "a start's cell: col, row (and dir 1 or -1)"


def play_args(cfg, files, level, level_file=None, bot=False, at_gauntlet=False, start=None):
    """the desktop game's command line: the grids standing in for the built ones (files), the level as edited, where
    every run starts (Game::debugPlayTest), whether the bot plays"""
    args = [cfg.game]
    for path in files:
        args += ["--gauntlet-file", path]
    if level_file:
        args += ["--level-file", level_file]
    if level:
        args += ["--level", str(int(level))]
    if start and "x" in start:
        args += ["--start-at", "%g" % start["x"]]
    elif start:
        args += ["--start-cell", str(start["col"]), str(start["row"])] + (["left"] if start.get("dir", 1) < 0 else [])
    elif at_gauntlet:
        args += ["--gauntlet"]
    if bot:
        args += ["--bot", "--tap-once"]
    return args


def play(cfg, level, level_text=None, grids=None, bot=False, at_gauntlet=False, start=None):
    """the desktop game on the level as edited: every grid as saved or edited standing in for the one it was built
    with, then the level's text (it names its gauntlet); every run from its start point, if it has one"""
    if not os.path.exists(cfg.game):
        return {"ok": False, "error": "build/cuckoo_stack isn't built (cmake --build build --target cuckoo_stack)"}
    sc = Scratch(cfg, keep="play")  # (kept: the game reads them as it starts)
    args = play_args(cfg, grid_files(cfg, sc, grids), level, sc.level(level, level_text) if level_text else None, bot, at_gauntlet, start)
    with cfg.lock:
        if cfg.playing and cfg.playing.poll() is None:
            cfg.playing.terminate()
        cfg.playing = subprocess.Popen(args, cwd=cfg.build, env=vulkan_env(), stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        proc = cfg.playing
    try:  # (a layout it won't take, or no Vulkan: it quits at once)
        _, err = proc.communicate(timeout=1.5)
        return {"ok": False, "error": (err or "the game quit").strip()[-400:]}
    except subprocess.TimeoutExpired:
        return {"ok": True, "pid": proc.pid}


class SimulatorJob:
    """the iOS app built with the saved levels and grids, installed and launched on every booted simulator (minutes)"""

    def __init__(self, cfg):
        self.cfg = cfg
        self.state, self.step, self.log, self.devices = "idle", "", "", []
        self.lock = threading.Lock()

    def status(self):
        with self.lock:
            return {"state": self.state, "step": self.step, "log": self.log[-1500:], "devices": self.devices}

    def start(self):
        with self.lock:
            if self.state == "running":
                return False
            self.state, self.step, self.log, self.devices = "running", "Starting", "", []
        threading.Thread(target=self.run, daemon=True).start()
        return True

    def say(self, step, log=""):
        with self.lock:
            self.step = step
            self.log += log

    def sh(self, step, args):
        self.say(step)
        done = subprocess.run(args, cwd=self.cfg.root, capture_output=True, text=True)
        if done.returncode:
            raise RuntimeError("%s failed:\n%s" % (step, (done.stdout + done.stderr).strip()[-1200:]))
        return done.stdout

    def run(self):
        xc = os.path.join(self.cfg.root, "build-ios", "CuckooStack.xcodeproj")
        app = os.path.join(self.cfg.root, "build-ios", "Debug-iphonesimulator", "CuckooStack.app")
        try:
            if not os.path.isdir(xc):
                raise RuntimeError("no build-ios: generate the Xcode project first (platforms/ios/generate_xcode.sh)")
            booted = json.loads(subprocess.run(["xcrun", "simctl", "list", "devices", "booted", "-j"], capture_output=True, text=True).stdout or "{}")
            devices = [(d["udid"], d["name"]) for ds in booted.get("devices", {}).values() for d in ds if d.get("state") == "Booted"]
            if not devices:
                raise RuntimeError("no iOS Simulator is running: open one (Simulator.app), then try again")
            self.sh("Configuring", ["cmake", os.path.join(self.cfg.root, "build-ios")])
            for i, target in enumerate(["cs_fonts", "cs_core"]):
                self.sh("Building %s (%d of 3)" % (target, i + 1), ["xcodebuild", "-project", xc, "-target", target, "-configuration", "Debug",
                                                                  "-sdk", "iphonesimulator", "build", "-quiet"])
            self.sh("Building the app (3 of 3)", ["xcodebuild", "-project", xc, "-scheme", "CuckooStack", "-configuration", "Debug",
                                                  "-sdk", "iphonesimulator", "-destination", "generic/platform=iOS Simulator", "build", "-quiet"])
            with open(os.path.join(app, "Info.plist"), "rb") as f:
                bundle = plistlib.load(f)["CFBundleIdentifier"]
            for udid, name in devices:
                subprocess.run(["xcrun", "simctl", "terminate", udid, bundle], capture_output=True)
                self.sh("Installing on %s" % name, ["xcrun", "simctl", "install", udid, app])
                self.sh("Launching on %s" % name, ["xcrun", "simctl", "launch", udid, bundle])
            with self.lock:
                self.state, self.step, self.devices = "done", "Installed", [name for _, name in devices]
        except Exception as e:  # (the editor shows why)
            with self.lock:
                self.state, self.step = "failed", str(e)


class Handler(BaseHTTPRequestHandler):
    cfg = None  # (set by serve())
    server_version = "CuckooLevelEditor/2"

    def log_message(self, fmt, *args):
        if self.cfg.verbose and "/check" not in self.path:
            sys.stderr.write("  %s\n" % (fmt % args))

    def reply(self, status, body, ctype="application/json"):
        data = body if isinstance(body, bytes) else json.dumps(body).encode()
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def bad(self, why):
        return self.reply(HTTPStatus.BAD_REQUEST, {"error": why})

    def body(self):
        n = int(self.headers.get("Content-Length") or 0)
        if n > 8_000_000:
            raise ValueError("too big")
        data = json.loads(self.rfile.read(n) or b"{}")
        if not isinstance(data, dict):
            raise ValueError("not an object")
        return data

    def static(self, folder, rel):
        path = os.path.realpath(os.path.join(folder, rel))
        if not path.startswith(os.path.realpath(folder) + os.sep) or not os.path.isfile(path):
            return self.reply(HTTPStatus.NOT_FOUND, {"error": "not found"})
        with open(path, "rb") as f:
            self.reply(HTTPStatus.OK, f.read(), TYPES.get(os.path.splitext(path)[1], "application/octet-stream"))

    def do_GET(self):
        url = unquote(urlparse(self.path).path)
        if url in ("/", "/index.html"):
            return self.static(STATIC, "index.html")
        if url.startswith("/static/"):
            return self.static(STATIC, url[len("/static/"):])
        if url.startswith("/fonts/") and url[len("/fonts/"):] in FONT_FILES:
            return self.static(FONTS, url[len("/fonts/"):])
        if url in ("/wiki", "/wiki/"):
            return self.static(WIKI, "index.html")
        if url.startswith("/wiki/"):
            return self.static(WIKI, url[len("/wiki/"):])
        if url == "/api/gauntlets":
            return self.reply(HTTPStatus.OK, {"gauntlets": [{"name": n, "description": d, "rows": r} for n, d, r in build_gauntlets.grids(self.cfg.grids)]})
        if url == "/api/levels":
            return self.reply(HTTPStatus.OK, {"levels": [{"number": n, "text": t} for n, t in build_levels.levels(self.cfg.levels)]})
        if url == "/api/simulator":
            return self.reply(HTTPStatus.OK, self.cfg.simulator.status())
        self.reply(HTTPStatus.NOT_FOUND, {"error": "not found"})

    def do_POST(self):
        url = urlparse(self.path).path
        try:
            req = self.body()
        except ValueError:
            return self.bad("not JSON")
        if url == "/api/simulator":
            self.cfg.simulator.start()
            return self.reply(HTTPStatus.OK, self.cfg.simulator.status())
        if url == "/api/levels/check":
            level, text, grids = req.get("level"), req.get("text"), req.get("grids")
            problem = valid_level(level, text) or (None if isinstance(text, str) else "a level's text") or valid_grids(grids)
            if problem:
                return self.bad(problem)
            return self.reply(HTTPStatus.OK, check_level(self.cfg, level, text, grids, bool(req.get("game"))))
        if url == "/api/play":
            level, text, grids, start = req.get("level"), req.get("levelText"), req.get("grids"), req.get("start")
            problem = valid_level(level, text) or valid_grids(grids) or valid_start(start)
            if problem:
                return self.bad(problem)
            return self.reply(HTTPStatus.OK, play(self.cfg, level, text, grids, bool(req.get("bot")), bool(req.get("atGauntlet")), start))
        if url == "/api/save":
            return self.save(req.get("levels") or {}, req.get("gauntlets") or {})
        if url == "/api/check":
            name, rows, level = req.get("name", ""), req.get("rows"), req.get("level") or 0
            if not NAME.match(str(name)):
                return self.bad("a gauntlet's name: lower case letters, digits, _")
            problem = valid_rows(rows) or (valid_level(level, req.get("levelText")) if level else None)
            if problem:
                return self.bad(problem)
            return self.reply(HTTPStatus.OK, check(self.cfg, name, rows, bool(req.get("game")), level, req.get("levelText") if level else None))
        self.reply(HTTPStatus.NOT_FOUND, {"error": "not found"})

    def do_PUT(self):
        url = urlparse(self.path).path
        try:
            req = self.body()
        except ValueError:
            return self.bad("not JSON")
        if url.startswith("/api/gauntlets/"):
            name = url[len("/api/gauntlets/"):]
            res = self.save({}, {name: {"rows": req.get("rows"), "description": req.get("description", [])}}, reply=False)
            if res is None:
                return None
            path = os.path.relpath(os.path.join(self.cfg.grids, name + ".txt"), self.cfg.root)
            return self.reply(HTTPStatus.OK, dict(res, path=path, inc=os.path.relpath(self.cfg.inc, self.cfg.root)))
        if url.startswith("/api/levels/") and url[len("/api/levels/"):].isdigit():
            return self.save({url[len("/api/levels/"):]: req.get("text")}, {})
        self.reply(HTTPStatus.NOT_FOUND, {"error": "not found"})

    def save(self, levels, gauntlets, reply=True):
        if not isinstance(levels, dict) or not isinstance(gauntlets, dict):
            return self.bad("levels: {N: text}; gauntlets: {name: {description, rows}}")
        lv = {}
        for k, text in levels.items():
            n = int(k) if str(k).isdigit() else 0
            problem = valid_level(n, text) or (None if isinstance(text, str) else "a level's text")
            if problem:
                return self.bad("level %s: %s" % (k, problem))
            lv[n] = text
        for name, g in gauntlets.items():
            if not NAME.match(str(name)):
                return self.bad("a gauntlet's name: lower case letters, digits, _")
            if not isinstance(g, dict):
                return self.bad("a gauntlet: {description, rows}")
            problem = valid_rows(g.get("rows"))
            if problem:
                return self.bad("the %s: %s" % (name, problem))
            description = g.get("description", [])
            if not isinstance(description, list) or not all(isinstance(line, str) and "\n" not in line for line in description):
                return self.bad("a description is a list of lines")
        try:
            res = save_all(self.cfg, lv, gauntlets)
        except SaveError as e:
            # (a grid that doesn't parse would take its gauntlet out of its level; a level that doesn't, the level out
            # of the game: never saved)
            self.reply(HTTPStatus.CONFLICT, {"error": "not saved: %s" % e})
            return None
        if reply:
            return self.reply(HTTPStatus.OK, res)
        return res


def build(cfg):
    if not shutil.which("cmake") or not os.path.isdir(cfg.build):
        print("  (no build/ to build in: run cmake -S . -B build first)")
        return
    print("Building the checkers and the desktop game (cmake --build build)...")
    done = subprocess.run(["cmake", "--build", cfg.build, "--target", "gauntlet_check", "level_check", "cuckoo_stack", "-j8"], capture_output=True, text=True)
    if done.returncode:
        print(done.stdout[-2000:] + done.stderr[-2000:])
        print("  the build failed: the editor still runs, with what's built")


def serve(cfg, port, open_browser=False):
    Handler.cfg = cfg
    server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    server.daemon_threads = True
    url = "http://127.0.0.1:%d" % server.server_address[1]
    print("The level editor: %s  (Ctrl-C to stop)" % url)
    if open_browser:
        threading.Timer(0.5, lambda: webbrowser.open(url)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        if cfg.playing and cfg.playing.poll() is None:
            cfg.playing.terminate()
        shutil.rmtree(cfg.scratch, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--open", action="store_true", help="open it in the browser")
    ap.add_argument("--no-build", action="store_true", help="don't build the checkers and the game first")
    args = ap.parse_args()
    cfg = Config()
    if not args.no_build:
        build(cfg)
    serve(cfg, args.port, args.open)


if __name__ == "__main__":
    main()
