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
    GET  /api/rules                         build/level_check --registry-json: the course's rules the editor lays by
                                            (the cell, a gauntlet's forecourt, lintel and porch, the meter) as the game
                                            lays them, and its hazards' registry (Hazards.h: each one's section, numbers,
                                            tool and look) and wall kinds
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
    POST /api/gauntlet/rename {from, to}    a maze renamed: its grid file moved and every level that lays it
                                            rewritten (nothing else in them changes); {ok, levels:{N:text}} (the levels
                                            that lay it now)
    POST /api/play {level, levelText?, grids?, bot?, atGauntlet?, start?}
                                            plays the level in the desktop game, as edited (atGauntlet: every run from
                                            its gauntlet's door; start: {x} metres along, or {col, row, dir} a cell of its
                                            maze: every run from there; bot: the bot plays)
    GET  /wiki/                             the editor's documentation (tools/level_editor/wiki)
    POST /api/simulator                     builds the iOS app with the saved levels and grids (build-ios) and installs
                                            it on every booted iOS Simulator; GET /api/simulator: how that's going

    python3 tools/level_editor/server.py --lan   ...and the iPhone and iPad editor's sync (CuckooEditor), on the same
                                            Wi-Fi: a second listener on port 8766 that serves /api/sync/* alone, behind
                                            the pairing code it prints (a bearer token), advertised by Bonjour
                                            (_cuckooedit._tcp):
    GET  /api/sync/manifest                 every level's, grid's and chunk's hash (sha256 of its file), the checkout's git
                                            head, whether a git merge or rebase is under way (then nothing is written: 423)
    POST /api/sync/pull {levels:[N], grids:[name], chunks:[id]}
                                            their files: {levels:{N:text}, grids:{name:{description,rows,text}}, chunks:{id:text}}
    POST /api/sync/push {levels:{N:{base,text}}, deleted:{N:base}, grids:{name:{base,text}}, chunks:{id:{base,text}},
                         deletedChunks:{id:base}}
                                            each one written only if the file's hash is still `base` (what the device
                                            edited from; "" for a new one), through /api/save's checks (save_all);
                                            {applied:[N], conflicts:{N:{text,hash}}, rejected:{N:why}}. A level taken out
                                            goes to build/editor-trash; every sync is logged to build/editor-sync.log;
                                            nothing is committed. A chunk (assets/chunks/<id>.txt: Chunk.h) is written the
                                            same way once `level_check --chunk` reads it (a course chunk's body judged, a maze
                                            chunk's played in a plain corridor and turned away if it can't be, flipped too if
                                            Mirror is on), then src/core/ChunkLayouts.inc built (scripts/chunks): also
                                            {chunksApplied:[id] (taken out ones too), chunkConflicts:{id:{text,hash}}}, a
                                            chunk turned away in rejected as "chunk <id>"; one taken out goes to the trash
    POST /api/sync/rename {from, to}        /api/gauntlet/rename, for a maze renamed on the device (before its push;
                                            renamed already: the same answer)

Python 3.9's standard library only. It listens on 127.0.0.1 alone (and, with --lan, its sync on the network).
"""
import argparse
import hashlib
import json
import os
import plistlib
import re
import secrets
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import webbrowser
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import unquote, urlparse

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "scripts", "gauntlets"))
sys.path.insert(0, os.path.join(ROOT, "scripts", "levels"))
sys.path.insert(0, os.path.join(ROOT, "scripts", "chunks"))
import build_chunks  # noqa: E402  (the chunks' files, and the .inc)
import build_gauntlets  # noqa: E402  (the grids' format, and the .inc)
import build_levels  # noqa: E402  (the levels' files, and the .inc)

STATIC = os.path.join(HERE, "static")
WIKI = os.path.join(HERE, "wiki")
FONTS = os.path.join(ROOT, "assets", "fonts")
FONT_FILES = {"CuckooDisplay-Black.ttf", "ChakraPetch-Medium.ttf", "ChakraPetch-Bold.ttf"}
CELLS = set("#HBRLSC()~^.>X")
NAME = re.compile(r"^[a-z][a-z0-9_]{0,31}$")
MAX_COLS, MAX_ROWS, MAX_LEVEL, MAX_TEXT = 800, 64, 999, 400_000
TYPES = {".html": "text/html; charset=utf-8", ".js": "text/javascript; charset=utf-8", ".css": "text/css; charset=utf-8",
         ".svg": "image/svg+xml", ".png": "image/png", ".ttf": "font/ttf", ".json": "application/json"}


class Config:
    """where it all is (the tests point it at a scratch copy)"""

    def __init__(self, root=ROOT, grids=None, inc=None, build=None, verbose=True, levels=None, levels_inc=None, chunks=None, chunks_inc=None):
        self.root = root
        self.verbose = verbose
        self.grids = grids or os.path.join(root, "assets", "gauntlets")
        self.inc = inc or os.path.join(root, "src", "core", "GauntletLayouts.inc")
        self.levels = levels or os.path.join(root, "assets", "levels")
        self.levels_inc = levels_inc or os.path.join(root, "src", "core", "LevelLayouts.inc")
        self.chunks = chunks or os.path.join(root, "assets", "chunks")
        self.chunks_inc = chunks_inc or os.path.join(root, "src", "core", "ChunkLayouts.inc")
        self.build = build or os.path.join(root, "build")
        self.checker = os.path.join(self.build, "gauntlet_check")
        self.level_checker = os.path.join(self.build, "level_check")
        self.game = os.path.join(self.build, "cuckoo_stack")
        self.lock = threading.Lock()   # (saves, and the game runs below)
        self.games = {}                # what a game run checks (a gauntlet, a level) -> its run (a newer one stops it)
        self.playing = None            # the desktop game playing a level
        self.scratch = tempfile.mkdtemp(prefix="cuckoo-level-editor-")
        self.simulator = SimulatorJob(self)
        self.sync_token = None          # (--lan: the pairing code the devices send)


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


def course_rules(cfg):
    """the course's rules as the game lays them (level_check --registry-json)"""
    if not os.path.exists(cfg.level_checker):
        return {"ok": False, "error": "build/level_check isn't built (cmake --build build --target level_check)"}
    done = subprocess.run([cfg.level_checker, "--registry-json"], capture_output=True, text=True, timeout=60)
    rules = last_json(done.stdout)
    return {"ok": True, **rules} if done.returncode == 0 and rules else {"ok": False, "error": done.stderr.strip() or "level_check --registry-json failed"}


def check_level(cfg, level, text, grids=None, game=False):
    """a level's text, its gauntlet's grid as given (unsaved) or as saved"""
    if not os.path.exists(cfg.level_checker):
        return {"ok": False, "error": "build/level_check isn't built (cmake --build build --target level_check)"}
    with Scratch(cfg) as sc:
        args = [cfg.level_checker, "--file", "-", "--level", str(level), "--json"] + (["--game"] if game else [])
        for path in grid_files(cfg, sc, grids):
            args += ["--gauntlet-file", path]
        return run_checker(cfg, args, text, key=("level", level) if game else None)


CHUNK_KIND = re.compile(r"^chunk[ \t]+(course|maze)[ \t]*$", re.M)
CHUNK_RULE = re.compile(r"^---[ \t]*$", re.M)


def valid_chunk(chunk_id, text=None):
    """a chunk's id (Chunk.h: lower case letters, digits, - and _) and, if given, its text's shape (the checker reads the rest)"""
    if not build_chunks.valid_id(chunk_id):
        return "a chunk's name: lower case letters, digits, - and _ (1 to 60)"
    if text is not None:
        if not isinstance(text, str) or len(text) > MAX_TEXT:
            return "a chunk's text: a string"
        if not CHUNK_KIND.search(text) or not CHUNK_RULE.search(text):
            return "the %s isn't a chunk file (a 'chunk course' or 'chunk maze' line, then a --- line)" % chunk_id
    return None


def check_chunk(cfg, chunk_id, text, game=False):
    """a chunk's text read as the generators read it, its body judged (flipped too, if Mirror is on): level_check --chunk"""
    if not os.path.exists(cfg.level_checker):
        return {"ok": False, "error": "build/level_check isn't built (cmake --build build --target level_check)"}
    args = [cfg.level_checker, "--chunk", "-", "--id", chunk_id] + (["--game"] if game else [])
    return run_checker(cfg, args, text, key=("chunk", chunk_id) if game else None)


def save_chunks(cfg, chunks):
    """chunks checked (level_check --chunk) and written, with the game's ChunkLayouts.inc built from the files:
    ({id: its verdict}, the ones turned away {id: why}); the others are written even so"""
    rejected = {}
    for chunk_id, text in sorted(chunks.items()):
        verdict = check_chunk(cfg, chunk_id, text) if os.path.exists(cfg.level_checker) else {"ok": True}
        if not verdict.get("ok"):
            rejected[chunk_id] = verdict.get("error", "it doesn't read")
    good = {i: t for i, t in chunks.items() if i not in rejected}
    if good:
        with cfg.lock:
            os.makedirs(cfg.chunks, exist_ok=True)
            for chunk_id, text in sorted(good.items()):
                with open(build_chunks.path_of(chunk_id, cfg.chunks), "w") as f:
                    f.write(text)
            build_chunks.write(cfg.chunks_inc, cfg.chunks)
    return good, rejected


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
                    t = build_levels.refit(text, name, len(g["rows"][0]), cfg.build)
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


def rename_in(text, old, new):
    """a level's text laying the gauntlet `old` as `new` (its course line; the rest as it was)"""
    out = []
    for line in text.split("\n"):
        m = build_levels.COURSE_LINE.match(line.split(";")[0])
        if m and m.group(6) == old:
            line = line[:m.start(6)] + new + line[m.end(6):]
        out.append(line)
    return "\n".join(out)


def rename_grid(cfg, old, new):
    """a maze renamed: its grid file moved, every level that lays it rewritten, the game's two .inc files written.
    Renamed already (`old` gone, `new` there): nothing to do. {ok, levels: {N: text}}: the levels that lay `new`"""
    if not NAME.match(str(old)) or not NAME.match(str(new)):
        raise SaveError("a gauntlet's name: lower case letters, digits, _")
    if old == new:
        raise SaveError("that's its name already")
    with cfg.lock:
        old_path, new_path = os.path.join(cfg.grids, old + ".txt"), os.path.join(cfg.grids, new + ".txt")
        if os.path.exists(old_path):
            if os.path.exists(new_path):
                raise SaveError("there's already a gauntlet called %s" % new)
            rewrite = {}
            for n, text in build_levels.levels(cfg.levels):
                t = rename_in(text, old, new)
                if t != text:
                    rewrite[n] = t
            os.rename(old_path, new_path)
            for n, t in sorted(rewrite.items()):
                with open(build_levels.path_of(n, cfg.levels), "w") as f:
                    f.write(t)
            with open(cfg.inc, "w") as f:
                f.write(build_gauntlets.render(cfg.grids))
            if rewrite:
                build_levels.write(cfg.levels_inc, cfg.levels)
        elif not os.path.exists(new_path):
            raise SaveError("there's no gauntlet called %s" % old)
        lay = {str(n): t for n, t in build_levels.levels(cfg.levels) if new in build_levels.gauntlets_in(t)}
    sync_log(cfg, "rename", old=old, new=new, levels=sorted(lay))
    return {"ok": True, "levels": lay}


# ---- sync with the iPhone and iPad editor (--lan)

def text_hash(text):
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def sync_locked(cfg):
    """a git merge or rebase under way in the checkout: no writes till it's done"""
    git = os.path.join(cfg.root, ".git")
    return any(os.path.exists(os.path.join(git, p)) for p in ("MERGE_HEAD", "rebase-merge", "rebase-apply", "CHERRY_PICK_HEAD"))


def sync_log(cfg, what, **detail):
    try:
        os.makedirs(cfg.build, exist_ok=True)
        with open(os.path.join(cfg.build, "editor-sync.log"), "a") as f:
            f.write(json.dumps({"at": time.strftime("%Y-%m-%dT%H:%M:%S"), "what": what, **detail}) + "\n")
    except OSError:
        pass


def git_head(cfg):
    try:
        done = subprocess.run(["git", "-C", cfg.root, "rev-parse", "--short", "HEAD"], capture_output=True, text=True, timeout=10)
        return done.stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return ""


def sync_manifest(cfg):
    return {"levels": {str(n): text_hash(t) for n, t in build_levels.levels(cfg.levels)},
            "grids": {name: text_hash(build_gauntlets.grid_text(d, rows)) for name, d, rows in build_gauntlets.grids(cfg.grids)},
            "chunks": {i: text_hash(t) for i, t in build_chunks.chunks(cfg.chunks)},
            "head": git_head(cfg), "locked": sync_locked(cfg)}


def sync_pull(cfg, levels=(), grids=(), chunks=()):
    saved = dict(build_levels.levels(cfg.levels))
    out = {"levels": {}, "grids": {}, "chunks": {}}
    have_chunks = dict(build_chunks.chunks(cfg.chunks))
    for chunk_id in chunks:
        if isinstance(chunk_id, str) and chunk_id in have_chunks:
            out["chunks"][chunk_id] = have_chunks[chunk_id]
    for n in levels:
        if isinstance(n, int) and n in saved:
            out["levels"][str(n)] = saved[n]
    have = {name: (d, rows) for name, d, rows in build_gauntlets.grids(cfg.grids)}
    for name in grids:
        if name in have:
            d, rows = have[name]
            out["grids"][name] = {"description": "\n".join(d), "rows": rows, "text": build_gauntlets.grid_text(d, rows)}
    return out


def grid_of(text):
    """a grid file's text: (its description's lines, its rows), as build_gauntlets.read_grid reads a file"""
    description, rows = [], []
    for line in text.splitlines():
        if line.startswith(";"):
            description.append(line[1:][1:] if line[1:2] == " " else line[1:])
        elif line.strip():
            rows.append(line.rstrip())
    return description, rows


def sync_push_chunks(cfg, chunks, deleted):
    """the chunks of a push: each written only if its file is still at the base the device edited from (a new one: base ""),
    once the checker reads it; one taken out, if it's still at its base, goes to the trash. The game's ChunkLayouts.inc built
    after. (applied ids, taken out ids, {id: the Mac's file and hash}, {id: why not})"""
    saved = dict(build_chunks.chunks(cfg.chunks))
    applied, gone, conflicts, rejected, accept = [], [], {}, {}, {}
    for chunk_id, item in sorted(chunks.items()):
        current = saved.get(chunk_id)
        base = item.get("base", "")
        if (current is None and base == "") or (current is not None and text_hash(current) == base):
            accept[chunk_id] = item["text"]
        else:
            conflicts[chunk_id] = {"text": current or "", "hash": text_hash(current) if current is not None else ""}
    written, turned_away = save_chunks(cfg, accept)
    applied += sorted(written)
    rejected.update({"chunk " + i: why for i, why in turned_away.items()})
    for chunk_id, base in sorted(deleted.items()):
        current = saved.get(chunk_id)
        if current is None:
            applied.append(chunk_id)
        elif text_hash(current) != base:
            conflicts[chunk_id] = {"text": current, "hash": text_hash(current)}
        else:
            with cfg.lock:
                trash = os.path.join(cfg.build, "editor-trash")
                os.makedirs(trash, exist_ok=True)
                shutil.move(build_chunks.path_of(chunk_id, cfg.chunks), os.path.join(trash, "chunk_%s-%s.txt" % (chunk_id, time.strftime("%Y%m%d-%H%M%S"))))
            gone.append(chunk_id)
            applied.append(chunk_id)
    if gone:
        with cfg.lock:
            build_chunks.write(cfg.chunks_inc, cfg.chunks)
    return applied, gone, conflicts, rejected


def sync_push(cfg, levels=None, deleted=None, grids=None, chunks=None, deleted_chunks=None):
    """each level and grid written only if its file is still at the base the device edited from (compare-and-swap),
    all of them through save_all's checks together (a grid that grew or shrank refits the other levels that lay it;
    one that fails the checks alone: rejected, the others still written); chunks the same way (sync_push_chunks)"""
    levels, deleted, grids = dict(levels or {}), dict(deleted or {}), dict(grids or {})
    saved = dict(build_levels.levels(cfg.levels))
    saved_grids = {name: build_gauntlets.grid_text(d, rows) for name, d, rows in build_gauntlets.grids(cfg.grids)}
    applied, conflicts, rejected, accept = [], {}, {}, {}
    grids_applied, grid_conflicts, accept_grids, refit = [], {}, {}, {}
    for name, item in sorted(grids.items()):
        current = saved_grids.get(name)
        base = item.get("base", "")
        if (current is None and base == "") or (current is not None and text_hash(current) == base):
            d, rows = grid_of(item["text"])
            accept_grids[name] = {"description": d, "rows": rows}
        else:
            grid_conflicts[name] = {"text": current or "", "hash": text_hash(current) if current is not None else ""}
    for key, item in sorted(levels.items()):
        n = int(key)
        current = saved.get(n)
        base = item.get("base", "")
        if (current is None and base == "") or (current is not None and text_hash(current) == base):
            accept[n] = item["text"]
        else:
            conflicts[key] = {"text": current or "", "hash": text_hash(current) if current is not None else ""}
    if accept or accept_grids:
        try:
            refit.update(save_all(cfg, accept, accept_grids)["refit"])
            applied += [str(n) for n in accept]
            grids_applied += sorted(accept_grids)
        except SaveError:
            for name, g in sorted(accept_grids.items()): # (which: one by one, the grids first, each with the levels laying it)
                lay = {n: t for n, t in accept.items() if name in build_levels.gauntlets_in(t)}
                try:
                    refit.update(save_all(cfg, lay, {name: g})["refit"])
                    grids_applied.append(name)
                    applied += [str(n) for n in lay]
                except SaveError as e:
                    rejected["grid " + name] = str(e)
            for n, text in sorted(accept.items()):
                if str(n) in applied:
                    continue
                try:
                    save_all(cfg, {n: text})
                    applied.append(str(n))
                except SaveError as e:
                    rejected[str(n)] = str(e)
    gone = []
    for key, base in sorted(deleted.items()):
        n = int(key)
        current = saved.get(n)
        if current is None:
            applied.append(key)
        elif text_hash(current) != base:
            conflicts[key] = {"text": current, "hash": text_hash(current)}
        else:
            trash = os.path.join(cfg.build, "editor-trash")
            os.makedirs(trash, exist_ok=True)
            shutil.move(build_levels.path_of(n, cfg.levels), os.path.join(trash, "level_%02d-%s.txt" % (n, time.strftime("%Y%m%d-%H%M%S"))))
            gone.append(n)
            applied.append(key)
    if gone:
        with cfg.lock:
            build_levels.write(cfg.levels_inc, cfg.levels)
    chunks_applied, chunks_gone, chunk_conflicts, chunk_rejected = sync_push_chunks(cfg, dict(chunks or {}), dict(deleted_chunks or {}))
    rejected.update(chunk_rejected)
    sync_log(cfg, "push", applied=applied, conflicts=sorted(conflicts), rejected=rejected, deleted=gone,
             grids=grids_applied, gridConflicts=sorted(grid_conflicts), refit=sorted(refit),
             chunks=chunks_applied, chunksDeleted=chunks_gone, chunkConflicts=sorted(chunk_conflicts))
    return {"applied": applied, "conflicts": conflicts, "rejected": rejected,
            "gridsApplied": grids_applied, "gridConflicts": grid_conflicts, "refit": refit,
            "chunksApplied": chunks_applied, "chunkConflicts": chunk_conflicts}


def pairing_code(cfg):
    """the code the devices pair with: made once, kept in the build folder (pair again: delete it)"""
    path = os.path.join(cfg.build, "editor-sync.token")
    try:
        with open(path) as f:
            code = f.read().strip()
            if code:
                return code
    except OSError:
        pass
    letters = "ABCDEFGHJKMNPQRSTUVWXYZ23456789" # (none that read alike: no I, L, O, 0, 1)
    code = "-".join("".join(secrets.choice(letters) for _ in range(4)) for _ in range(2))
    os.makedirs(cfg.build, exist_ok=True)
    with open(path, "w") as f:
        f.write(code + "\n")
    os.chmod(path, 0o600)
    return code


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
        if url == "/api/rules":
            return self.reply(HTTPStatus.OK, course_rules(self.cfg))
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
        if url == "/api/gauntlet/rename":
            return self.rename(req)
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

    def rename(self, req):
        try:
            return self.reply(HTTPStatus.OK, rename_grid(self.cfg, req.get("from"), req.get("to")))
        except SaveError as e:
            return self.reply(HTTPStatus.CONFLICT, {"error": "not renamed: %s" % e})

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


class SyncHandler(Handler):
    """the network's listener (--lan): /api/sync/* alone, the pairing code its key"""
    cfg = None
    server_version = "CuckooLevelEditorSync/1"

    def log_message(self, fmt, *args):
        if self.cfg.verbose:
            sys.stderr.write("  sync %s: %s\n" % (self.client_address[0], fmt % args))

    def authorized(self):
        want = "Bearer " + (self.cfg.sync_token or "")
        got = self.headers.get("Authorization", "")
        if self.cfg.sync_token and secrets.compare_digest(got.encode(), want.encode()):
            return True
        self.reply(HTTPStatus.UNAUTHORIZED, {"error": "pair with the code the server printed"})
        return False

    def do_GET(self):
        url = urlparse(self.path).path
        if not url.startswith("/api/sync/"):
            return self.reply(HTTPStatus.NOT_FOUND, {"error": "not found"})
        if not self.authorized():
            return None
        if url == "/api/sync/manifest":
            return self.reply(HTTPStatus.OK, sync_manifest(self.cfg))
        self.reply(HTTPStatus.NOT_FOUND, {"error": "not found"})

    def do_POST(self):
        url = urlparse(self.path).path
        if not url.startswith("/api/sync/"):
            return self.reply(HTTPStatus.NOT_FOUND, {"error": "not found"})
        if not self.authorized():
            return None
        try:
            req = self.body()
        except ValueError:
            return self.reply(HTTPStatus.BAD_REQUEST, {"error": "not JSON"})
        if url == "/api/sync/pull":
            levels, grids, chunks = req.get("levels", []), req.get("grids", []), req.get("chunks", [])
            if not isinstance(levels, list) or not isinstance(grids, list) or not isinstance(chunks, list):
                return self.reply(HTTPStatus.BAD_REQUEST, {"error": "levels, grids and chunks: lists"})
            return self.reply(HTTPStatus.OK, sync_pull(self.cfg, levels, grids, chunks))
        if url == "/api/sync/push":
            if sync_locked(self.cfg):
                return self.reply(HTTPStatus.LOCKED, {"error": "A git merge or rebase is under way on the Mac: finish it, then sync"})
            levels, deleted = req.get("levels", {}), req.get("deleted", {})
            if not isinstance(levels, dict) or not isinstance(deleted, dict):
                return self.reply(HTTPStatus.BAD_REQUEST, {"error": "levels and deleted: objects"})
            for key, item in levels.items():
                problem = (None if key.isdigit() and isinstance(item, dict) and isinstance(item.get("text"), str) and isinstance(item.get("base", ""), str)
                           else "level %s: {base, text}" % key) or valid_level(int(key), item["text"])
                if problem:
                    return self.reply(HTTPStatus.BAD_REQUEST, {"error": problem})
            if any(not k.isdigit() or not isinstance(v, str) for k, v in deleted.items()):
                return self.reply(HTTPStatus.BAD_REQUEST, {"error": "deleted: {N: base}"})
            grids = req.get("grids", {})
            if not isinstance(grids, dict):
                return self.reply(HTTPStatus.BAD_REQUEST, {"error": "grids: an object"})
            for name, item in grids.items():
                if not (NAME.match(name) and isinstance(item, dict) and isinstance(item.get("text"), str) and isinstance(item.get("base", ""), str)):
                    return self.reply(HTTPStatus.BAD_REQUEST, {"error": "grid %s: {base, text}" % name})
                problem = valid_rows(grid_of(item["text"])[1])
                if problem:
                    return self.reply(HTTPStatus.BAD_REQUEST, {"error": "the %s: %s" % (name, problem)})
            chunks, deleted_chunks = req.get("chunks", {}), req.get("deletedChunks", {})
            if not isinstance(chunks, dict) or not isinstance(deleted_chunks, dict):
                return self.reply(HTTPStatus.BAD_REQUEST, {"error": "chunks and deletedChunks: objects"})
            for chunk_id, item in chunks.items():
                if not (isinstance(item, dict) and isinstance(item.get("text"), str) and isinstance(item.get("base", ""), str)):
                    return self.reply(HTTPStatus.BAD_REQUEST, {"error": "chunk %s: {base, text}" % chunk_id})
                problem = valid_chunk(chunk_id, item["text"])
                if problem:
                    return self.reply(HTTPStatus.BAD_REQUEST, {"error": problem})
            for chunk_id, base in deleted_chunks.items():
                problem = valid_chunk(chunk_id)
                if problem or not isinstance(base, str):
                    return self.reply(HTTPStatus.BAD_REQUEST, {"error": problem or "deletedChunks: {id: base}"})
            return self.reply(HTTPStatus.OK, sync_push(self.cfg, levels, deleted, grids, chunks, deleted_chunks))
        if url == "/api/sync/rename":
            if sync_locked(self.cfg):
                return self.reply(HTTPStatus.LOCKED, {"error": "A git merge or rebase is under way on the Mac: finish it, then sync"})
            return self.rename(req)
        self.reply(HTTPStatus.NOT_FOUND, {"error": "not found"})


def serve_lan(cfg, port=8766):
    """the sync listener on the network, and its Bonjour advert (dns-sd: macOS's own)"""
    cfg.sync_token = pairing_code(cfg)
    SyncHandler.cfg = cfg
    server = ThreadingHTTPServer(("0.0.0.0", port), SyncHandler)
    server.daemon_threads = True
    threading.Thread(target=server.serve_forever, daemon=True).start()
    name = "Cuckoo Editor on %s" % socket.gethostname().split(".")[0]
    advert = None
    if shutil.which("dns-sd"):
        advert = subprocess.Popen(["dns-sd", "-R", name, "_cuckooedit._tcp", "local", str(port)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print("Sync for the iPhone and iPad editor: port %d, as \"%s\". Pairing code: %s" % (port, name, cfg.sync_token))
    return server, advert


def build(cfg):
    if not shutil.which("cmake") or not os.path.isdir(cfg.build):
        print("  (no build/ to build in: run cmake -S . -B build first)")
        return
    print("Building the checkers and the desktop game (cmake --build build)...")
    done = subprocess.run(["cmake", "--build", cfg.build, "--target", "gauntlet_check", "level_check", "cuckoo_stack", "-j8"], capture_output=True, text=True)
    if done.returncode:
        print(done.stdout[-2000:] + done.stderr[-2000:])
        print("  the build failed: the editor still runs, with what's built")


def serve(cfg, port, open_browser=False, lan=False):
    Handler.cfg = cfg
    lan_server = serve_lan(cfg) if lan else None
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
        if lan_server:
            lan_server[0].shutdown()
            if lan_server[1]:
                lan_server[1].terminate()
        if cfg.playing and cfg.playing.poll() is None:
            cfg.playing.terminate()
        shutil.rmtree(cfg.scratch, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--open", action="store_true", help="open it in the browser")
    ap.add_argument("--no-build", action="store_true", help="don't build the checkers and the game first")
    ap.add_argument("--lan", action="store_true", help="sync with the iPhone and iPad editor on this network (port 8766)")
    args = ap.parse_args()
    cfg = Config()
    if not args.no_build:
        build(cfg)
    serve(cfg, args.port, args.open, args.lan)


if __name__ == "__main__":
    main()
