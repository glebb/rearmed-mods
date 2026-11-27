#!/usr/bin/env python3
"""Unattended game tests of both editions: the cases of tests/game/DESIGN.md, run back to back in one game a start-time
settings group, with a report.

  tests/game/run.py                         the smoke suite on both editions (setup.py first)
  tests/game/run.py --edition mac --suite regression
  tests/game/run.py --case guitar7 --case songbridge --edition lp --no-setup
  tests/game/run.py --list                  the cases and their suites

The game takes keys only while its window has the focus: keep the screen free while it runs. One game on this Mac
at a time: the runner takes ~/rs-gametest/.game-lock (waiting up to 30 min for another run) and always quits its game
(keyfifo's quit, then SIGTERM, then SIGKILL), on Ctrl-C too.

With RS Re-ARMed's test input in the build (RS_AUDIO_FAKE_INPUT), every game gets FAKE_INPUT (RS_GAMETEST_FAKE_INPUT
overrides it) and the cases' "heard" checks run; without it they are SKIP. A case whose route is not driven yet is
MANUAL in the report, with its steps. The charts of a game start's cases are put in dlc/gametest/ before it starts
(the game lists dlc at its start).

Where things are: RS_GAMETEST_ROOT (default ~/rs-gametest), DESIGN.md "Where things are". A run's results:
<root>/runs/<stamp>/<edition>/<case>/ (game.log, companion.log, events.jsonl, keyfifo.log, snap/, RSMods.ini,
result.json) and <root>/runs/<stamp>/report.md.
"""
import argparse
import datetime
import glob
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import time
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.dont_write_bytecode = True  # no __pycache__ in the repository (python3 -I leaves the script folder out of the path)

import gt_cases as C  # noqa: E402
import gt_log as L  # noqa: E402
import gt_report  # noqa: E402
import gt_routes as R  # noqa: E402
from gt_game import (Driver, EventSink, Game, GameEnded, Interrupted, Lock, MAIN, Snaps, Stuck)  # noqa: E402

PLAYER = os.path.expanduser("~/Library/Application Support/RS Re-ARMed")
PLAYER_APPS = os.path.expanduser("~/Applications")
INI_MARK = "; gametest: written by tests/game/run.py"
SNAP_SECS = 3
# The test input (rs-rearmed shims/coreaudio/rs_audio_fake.h): player 1's channel an E2 that the tuner reads in tune
# (an exact E2 reads 12-14 cents sharp in both editions), player 2's silent. The test saves' INPUT hears channel 1.
FAKE_INPUT = "tone:39.88,silence"
# Capabilities a case may need that this runner or the builds do not have yet (gt_cases: needs=...)
NOT_DRIVEN = {"tonehost_plugin", "coop_route"}


def say(*a):
    print(*a, flush=True)


def stamp():
    return datetime.datetime.now().strftime("%Y%m%d-%H%M%S")


# ---------------------------------------------------------------------------------------------------------------
# The test installs (setup.py's work, DESIGN.md "Where things are")

class Install:
    """One edition's test install: paths, the launch, the settings file and the test charts' folder."""

    def __init__(self, root, edition, builds):
        self.root, self.edition, self.builds = root, edition, builds
        if edition == "mac":
            self.home = os.path.join(root, "mac", "home")
            self.game_dir = os.path.join(self.home, "game")
            self.exe = os.path.join(self.game_dir, "Rocksmith2014.app", "Contents", "MacOS", "Rocksmith2014")
            self.mods = os.path.join(self.home, "mods")
            self.steam = os.path.join(self.home, "steam")
        else:
            self.home = os.path.join(root, "lp")
            self.game_dir = os.path.join(self.home, "game")
            self.exe = os.path.join(self.home, "out", "rs2014_lp")
            self.mods = os.path.join(self.home, "mods")
            self.steam = os.path.join(self.home, "steam")
            self.userdata = os.path.join(self.home, "userdata")
        # builds.json "editions": how setup.py says a run starts each (exe, cwd, env, insert, rsmods_ini, dlc_test,
        # steam_env); the layout above where it says nothing
        launch = ((builds or {}).get("editions") or {}).get(edition) or {}
        self.launch = launch
        self.home = launch.get("home", self.home)
        self.exe = launch.get("exe", self.exe)
        self.game_dir = launch.get("game", self.game_dir)
        self.cwd = launch.get("cwd", self.game_dir)
        self.mods = launch.get("mods", self.mods)
        self.steam = launch.get("steam", self.steam)
        if edition == "lp":
            self.userdata = launch.get("userdata", self.userdata)
        self.ini = launch.get("rsmods_ini", os.path.join(self.cwd, "RSMods.ini"))
        self.dlc = launch.get("dlc_test", os.path.join(self.game_dir, "dlc", "gametest"))
        self.steam_env = launch.get("steam_env", os.path.join(self.game_dir, ".rearmed", "steam.env"))
        self.companion_data = os.path.join(root, "companion", edition)

    def problems(self):
        out = []
        if not os.access(self.exe, os.X_OK):
            out.append("no game program at %s (setup.py)" % self.exe)
        for p in (self.game_dir, self.mods):
            if not os.path.isdir(p):
                out.append("no %s (setup.py)" % p)
        # never the player's game: every place written must be the test install's
        for p in (self.cwd, os.path.dirname(self.dlc), self.steam):
            rp = os.path.realpath(p)
            if rp.startswith(os.path.realpath(PLAYER) + os.sep) or rp.startswith(os.path.realpath(PLAYER_APPS)):
                out.append("%s is the player's (%s): refusing" % (p, rp))
        return out

    def env(self, run_dir, fifo, snap_dir, keyfifo, extra_env):
        e = {k: os.environ[k] for k in ("HOME", "USER", "LOGNAME", "PATH", "TMPDIR", "LANG") if k in os.environ}
        e.update(RS_MODS_LOG="1", RS_STEAM_LOG="1", RS_SONGBRIDGE_LOG="1", RS_FULLSCREEN="0",
                 RS_KEY_FIFO=fifo, RS_GL_SNAPSHOT_DIR=snap_dir, RS_GL_SNAPSHOT_SECS=str(SNAP_SECS),
                 RS_COMPANION_DATA=self.companion_data, RS_MODS_PATH=self.mods,
                 RS_NETGUARD_LOG=os.path.join(run_dir, "netguard.log"))
        insert = [keyfifo]
        # the copy's Steam facts (RS_STEAM_DLC, _OWNED, _LANGUAGE, _ID), as the launchers pass them
        e.update(read_env_file(self.steam_env))
        if self.edition == "mac":
            e.update(RS_STEAM_MODE="stub", RS_STEAM_DATA_DIR=self.steam, RS_NATIVE_HOME=self.home)
            insert.insert(0, os.path.join(self.home, "libnetguard.dylib"))
        else:
            # LP_LOG_TO: an app bundle's game sends its log to LP_HOME/logs otherwise (win-x86/app/lp_main.c:345)
            e.update(LP_GAME_DIR=self.game_dir, LP_USER_DIR=self.userdata, RS_STEAM_DATA_DIR=self.steam,
                     LP_HOME=self.home, LP_LOG_TO=os.path.join(run_dir, "game.log"))
        e.update({k: v for k, v in self.launch.get("env", {}).items() if k != "RS_NETGUARD_LOG"})
        e.update(extra_env)
        e["DYLD_INSERT_LIBRARIES"] = ":".join(self.launch.get("insert") or insert)
        for k in ("RS_NETGUARD", "RS_STEAM_KEEP_CREDENTIALS"):  # never weakened (rs-rearmed AGENTS.md, Offline)
            e.pop(k, None)
        return e


def read_env_file(path):
    out = {}
    try:
        with open(path) as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith("#") and "=" in line:
                    k, v = line.split("=", 1)
                    out[k.strip()] = v.strip()
    except OSError:
        pass
    return out


# ---------------------------------------------------------------------------------------------------------------
# RSMods.ini: the base (the install's own, as setup.py left it), the test base, the case's keys

PLAYER_INI = {"mac": os.path.join(PLAYER, "game", "RSMods.ini"),
              "lp": os.path.join(PLAYER, "learn-play", "game", "RSMods.ini")}


def base_ini(inst):
    """The settings as a player has them: the install's RSMods.ini as setup.py left it (kept aside the first time the
    runner sees it: later ones are the runner's own), else the one kept, else the player's own (only read)."""
    keep = os.path.join(inst.root, inst.edition, "RSMods.base.ini")

    def read(p):
        try:
            with open(p, errors="replace") as f:
                return f.read()
        except OSError:
            return None
    text = read(inst.ini)
    if text is None or INI_MARK in text:
        text = read(keep)
        if text is not None:
            return text
        text = read(PLAYER_INI[inst.edition])
    if text is None:
        return ""
    os.makedirs(os.path.dirname(keep), exist_ok=True)
    with open(keep, "w") as f:
        f.write(text)
    return text


def ini_text(base, *overrides):
    """The base, then the overrides as sections at the end: the reader takes a key's last line (rearmed-mods
    src/rsmods_ini.c rsm_get)."""
    keys = {}
    for o in overrides:
        keys.update(o)
    by_section = {}
    for (sec, key), v in keys.items():
        by_section.setdefault(sec, []).append((key, v))
    out = [INI_MARK, base.rstrip("\n"), "", "; gametest: the test base and the case's keys"]
    for sec, kv in by_section.items():
        out.append("[%s]" % sec)
        out += ["%s = %s" % (k, v) for k, v in kv]
    return "\n".join(out) + "\n"


def write_ini(path, text):
    try:
        with open(path) as f:
            if f.read() == text:
                return False
    except OSError:
        pass
    tmp = path + ".gametest-new"
    with open(tmp, "w") as f:
        f.write(text)
    os.replace(tmp, path)
    return True


# ---------------------------------------------------------------------------------------------------------------
# The test charts (fixtures.py's work: <root>/fixtures/{pc,mac}/*.psarc and fixtures.json)

class Charts:
    """fixtures.json (tests/game/fixtures.py): charts {id: {files {pc, mac}, song_key, title, artist, search,
    arrangements [{name, instrument, strings, tuning_name, lowest_midi, ...}]}}, stock_songs {edition: {song_key,
    search, lead, bass}}."""

    def __init__(self, root):
        self.root = root
        try:
            with open(os.path.join(root, "fixtures", "fixtures.json")) as f:
                self.data = json.load(f)
        except (OSError, ValueError):
            self.data = {}
        self.charts = self.data.get("charts") or {}

    def resolve(self, name):
        """-> {key, search, strings, tuning, low_midi, files: {form: path}, entry}."""
        d = dict(C.CHART_DEFAULTS.get(name, {}))
        d.pop("words", None)
        entry = self.charts.get(name)
        files = {}
        if entry:
            d["entry"] = entry
            d["key"] = entry.get("song_key") or d.get("key")
            d["search"] = entry.get("search") or d.get("search")
            want = d.get("instrument", "guitar")
            arrs = [a for a in entry.get("arrangements") or [] if a.get("instrument") == want]
            # the arrangement the case plays: the extended one with the most strings
            arrs.sort(key=lambda a: (-(a.get("strings") or 0), a.get("name") != ("Bass" if want == "bass" else "Lead")))
            if arrs:
                a = arrs[0]
                d["strings"] = int(a.get("strings") or d.get("strings") or 6)
                d["arrangement"] = a.get("name")
                if a.get("tuning_name"):
                    d["tuning"] = a["tuning_name"]
                if a.get("lowest_midi") is not None:
                    d["low_midi"] = int(a["lowest_midi"])
            for form, fn in (entry.get("files") or {}).items():
                files[form] = fn if os.path.isabs(fn) else os.path.join(self.root, "fixtures", form, fn)
        d["files"] = files
        if not d.get("search") and d.get("key"):
            d["search"] = re.sub(r"[^a-z]", "", d["key"].lower())[:4]
        return d

    def stock(self, edition):
        s = (self.data.get("stock_songs") or {}).get(edition) or {}
        if s.get("song_key"):
            return {"key": s["song_key"], "search": s.get("search") or s["song_key"][:4].lower(), "entry": s}
        return dict(C.STOCK)


# ---------------------------------------------------------------------------------------------------------------
# A game start: the lock, the game, the companion's socket, the start to the main menu

class Session:
    def __init__(self, runner, inst, label, env, group_dir):
        self.runner, self.inst, self.label, self.extra_env = runner, inst, label, env
        self.group_dir = group_dir
        self.game = self.d = self.sink = None
        self.starts = []  # each start: {log, t_start, t_title, t_main, t_dlc, quit_how, exit_code, error}
        self.n = 0

    def start(self):
        self.n += 1
        n = self.n
        gd = self.group_dir
        os.makedirs(gd, exist_ok=True)
        log = os.path.join(gd, "game.log" if n == 1 else "game-%d.log" % n)
        fifo = os.path.join(gd, "keys")
        snap_dir = os.path.join(gd, "snaps-%d" % n)
        os.makedirs(snap_dir, exist_ok=True)
        os.makedirs(self.inst.companion_data, exist_ok=True)
        env = self.inst.env(gd, fifo, snap_dir, self.runner.keyfifo, self.extra_env)
        if self.inst.edition == "lp":
            env["LP_LOG_TO"] = log
        spec = {"exe": self.inst.exe, "args": self.inst.launch.get("args", []), "cwd": self.inst.cwd, "env": env,
                "fifo": fifo, "log": log}
        with open(os.path.join(gd, "launch-%d.json" % n), "w") as f:
            json.dump({k: v for k, v in spec.items()}, f, indent=1)
        self.comp_log = L.Log(os.path.join(self.inst.companion_data, "rscompanion.log"), strip=L.strip_companion_time)
        self.comp_log.poll()  # what an earlier start wrote: skipped
        self.comp_start = len(self.comp_log.lines)
        self.events_path = os.path.join(gd, "events.jsonl")
        if self.sink is None:
            self.sink = EventSink(os.path.join(self.inst.companion_data, "events.sock"), self.events_path)
            self.sink.start()
        self.game = Game(spec, say=say)
        self.snaps = Snaps(snap_dir, SNAP_SECS)
        self.d = Driver(self.game, self.inst.edition, self.snaps, say=say, on_poll=self.runner.on_poll)
        rec = {"log": log, "n": n}
        self.starts.append(rec)
        say("  start %d of %s (%s)" % (n, self.inst.edition, self.label))
        self.game.start()
        rec["t_start"] = self.game.t_start
        try:
            R.wait_netguard(self.d, env["RS_NETGUARD_LOG"])
            self.d.wait_screen(("TitleScreen", "ProfileSelect", MAIN), 90, what="the title")
            rec["t_title"] = next((t for t, s in self.d.history if s == "TitleScreen"), time.time())
            R.to_main_menu_from_start(self.d, autoload=self.runner.autoload(self.inst))
            rec["t_main"] = time.time()
            R.wait_dlc(self.d)
            rec["t_dlc"] = time.time()
            say("    main menu %.1f s after the start (title %.1f s)" % (rec["t_main"] - rec["t_start"],
                                                                       rec["t_title"] - rec["t_start"]))
        except Stuck as e:
            rec["error"] = "%s (on %s)" % (e, e.where)
            raise

    def lines(self, start=0):
        self.game.log.poll()
        return self.game.log.text(start)

    def comp_lines(self, start=None):
        self.comp_log.poll()
        return self.comp_log.text(self.comp_start if start is None else start)

    def quit(self):
        if self.game is None:
            return
        how = self.game.quit()
        rec = self.starts[-1] if self.starts else {}
        rec["quit_how"] = how
        rec["exit_code"] = self.game.exit_code
        say("  quit: %s, exit status %s" % (how, self.game.exit_code))

    def close(self):
        try:
            self.quit()
        finally:
            if self.sink:
                self.sink.close()
                self.sink = None


# ---------------------------------------------------------------------------------------------------------------
# One case in a running game

class CaseRun:
    def __init__(self, runner, session, case, edition, case_dir):
        self.runner, self.s, self.case, self.edition, self.dir = runner, session, case, edition, case_dir
        self.missing = runner.missing[edition]
        self.snaps = []
        self.restart_lines = None
        self.quit_status = self.quit_how = None
        self._checks = None
        self.start_index = 0
        self.comp_index = 0
        self.events_offset = 0
        self.game_n = session.n

    @property
    def d(self):
        return self.s.d

    def chart(self, name):
        return self.runner.charts.resolve(name)

    def stock(self):
        return self.runner.charts.stock(self.edition)

    def begin(self):
        self.s.game.log.poll()
        self.start_index = len(self.s.game.log.lines)
        self.s.comp_log.poll()
        self.comp_index = len(self.s.comp_log.lines)
        try:
            self.events_offset = os.path.getsize(self.s.events_path)
        except OSError:
            self.events_offset = 0
        self.t0 = time.time()

    def checks(self):
        if self._checks is None:
            self._checks = C.fault_checks() + self.case.checks(self)
        return self._checks

    def sources(self, scope):
        game = self.s.game
        game.log.poll()
        if scope == "session":
            lines = game.log.text(0)
            comp = self.s.comp_lines(self.s.comp_start)
        else:
            lines = game.log.text(self.start_index) if self.game_n == self.s.n else self.lines("case")
            comp = self.s.comp_lines(self.comp_index)
        events, _ = L.read_events(self.s.events_path, 0 if scope == "session" else self.events_offset)
        return {"game": lines, "companion": comp, "events": [json.dumps(e) for e in events]}

    def lines(self, scope):
        if scope == "restart":
            return self.restart_lines
        if scope == "first_game":  # the whole log of the game the case started in
            with open(self.s.starts[self.game_n - 1]["log"], errors="replace") as f:
                return f.read().splitlines()
        if scope == "session":
            return self.sources("session")["game"]
        if scope == "case" and self.game_n != self.s.n:  # the game was started again: the first one's log
            with open(self.s.starts[self.game_n - 1]["log"], errors="replace") as f:
                return f.read().splitlines()[self.start_index:]
        return self.sources("case")["game"]

    def evaluate(self, final=True, names=None):
        out = []
        cache = {}
        for c in self.checks():
            if names is not None and c.name not in names:
                continue
            scope = getattr(c, "scope", "case")
            if scope not in cache:
                cache[scope] = self.sources(scope)
            out.append(c.evaluate(cache[scope], self.missing, final))
        return out

    def until(self, names, seconds):
        """Holds until the named checks pass (or are skipped), at most `seconds` -> True if they did."""
        names = [n for n in names if any(c.name == n for c in self.checks())]
        if not names:
            return True

        def ok():
            rs = self.evaluate(final=False, names=names)
            return all(r["status"] in (L.PASS, L.SKIP) for r in rs)
        got = self.d.hold(seconds, until=ok)
        self.d.note("checks %s: %s" % (", ".join(names), "in" if got else "not all in %d s" % seconds))
        return got

    def in_song_checks(self):
        return [c.name for c in self.checks() if not c.forbid and getattr(c, "scope", "case") == "case"
                and not re.search(r"freed|reset|on exit|on leaving", c.name)]

    def snap(self, label):
        p = self.d.snap(os.path.join(self.dir, "snap"), label)
        if p:
            self.snaps.append(os.path.relpath(p, self.dir))
        return p

    def wait_exit(self, seconds):
        end = time.time() + seconds
        g = self.s.game
        while time.time() < end and g.alive():
            time.sleep(0.2)
        if g.alive():
            raise Stuck("the game is still up %d s after EXIT GAME" % seconds)
        self.quit_status = g.exit_code
        self.quit_how = "EXIT GAME (the game's own menu)"
        g.quit_how = self.quit_how
        self.s.starts[-1].update(quit_how=self.quit_how, exit_code=g.exit_code)

    def restart(self):
        self.s.start()
        self.restart_lines = self.s.lines(0)

    def start_timing(self):
        rec = self.s.starts[self.game_n - 1]
        out = []
        t = rec.get("t_title")
        ok = t is not None and t - rec["t_start"] <= 30
        out.append({"name": "the title within 30 s (the white loading screen fix, the intro skip)",
                    "status": L.PASS if ok else L.FAIL, "source": "game", "src": "the menu: TitleScreen line's time",
                    "pattern": "", "why": "" if ok else ("no title" if t is None else "%.1f s" % (t - rec["t_start"])),
                    "value": None if t is None else round(t - rec["t_start"], 1)})
        t = rec.get("t_main")
        ok = t is not None and t - rec["t_start"] <= 120
        out.append({"name": "the main menu within 120 s", "status": L.PASS if ok else L.FAIL, "source": "game",
                    "src": "the menu: MainMenu line's time", "pattern": "",
                    "why": "" if ok else ("no main menu" if t is None else "%.1f s" % (t - rec["t_start"])),
                    "value": None if t is None else round(t - rec["t_start"], 1)})
        return out


# ---------------------------------------------------------------------------------------------------------------
# The runner

class Runner:
    def __init__(self, args):
        self.args = args
        self.root = os.path.abspath(os.path.expanduser(args.root))
        self.stamp = stamp()
        self.run_dir = os.path.join(self.root, "runs", self.stamp)
        self.lock = Lock(say=say)
        self.session = None
        self.case_run = None
        self.case_deadline = None
        self.results = {}
        self.sessions = {}
        self.builds = None
        self.charts = Charts(self.root)
        self.missing = {}
        self.keyfifo = os.path.join(self.root, "tools", "libkeyfifo.dylib")
        self._forbid_at = 0
        self._calibration_seen = None

    # -- what a run has
    def load(self):
        p = os.path.join(self.root, "builds.json")
        if not os.path.exists(p):
            raise Stuck("%s is not there: setup.py makes it (the test installs)" % p)
        with open(p) as f:
            self.builds = json.load(f)
        if not os.path.exists(self.keyfifo):
            raise Stuck("no %s (setup.py builds rs-rearmed's tools/ws8/keyfifo)" % self.keyfifo)
        self.charts = Charts(self.root)
        try:
            with open(os.path.join(self.root, "profiles.json")) as f:
                self.profiles = json.load(f)
        except (OSError, ValueError):
            self.profiles = None

    def capabilities(self, inst):
        missing = set(NOT_DRIVEN)
        if not has_fake_input(inst):
            missing.add("fake_input")
        pr = (self.profiles or {}).get(inst.edition) or {}
        if not pr.get("coop"):  # setup.py: two profiles of the test saves, else the runner must make one
            missing.add("two_profiles")
        return missing

    def autoload(self, inst):
        text = base_ini(inst)
        m = re.findall(r"(?im)^\s*ForceProfileLoad\s*=\s*(\S+)", text)
        return bool(m) and m[-1].lower() in ("on", "1", "true")

    # -- while a case runs: its time limit, and a fault ends it at once
    def on_poll(self):
        if self.case_deadline and time.time() > self.case_deadline:
            self.case_deadline = None
            raise Stuck("the case's time is up")
        s = self.session
        if s and s.d and s.d.screen and "Calibration" in s.d.screen and s.d.screen != self._calibration_seen:
            # no route calibrates (the tests skip it): a key that landed elsewhere; Esc cancels it (recover)
            self._calibration_seen = s.d.screen
            raise Stuck("the game's calibration started (%s): a key landed on a tuner's Calibrate" % s.d.screen,
                        s.d.screen)
        if s and s.d and s.d.screen and "Calibration" not in s.d.screen:
            self._calibration_seen = None
        cr = self.case_run
        if cr and time.time() - self._forbid_at > 1.0:
            self._forbid_at = time.time()
            lines = cr.s.game.log.text(cr.start_index) if cr.game_n == cr.s.n else []
            for name, pat in L.FAULTS:
                if any(re.search(pat, t) for t in lines[-400:]):
                    raise Stuck("a fault in the log (%s)" % name)

    # -- the plan
    def plan(self, cases, edition):
        """Cases -> game starts: [(label, env, start_ini, [cases], charts {form: [names]})]. One start a settings
        group; a group whose charts would put a song there twice (the native form and Song Bridge's other form of
        the same chart) is split."""
        groups = []
        for c in cases:
            if self.skip_reason(c, edition):
                key = ("skipped",)
            else:
                key = c.group_key()
            g = next((g for g in groups if g["key"] == key and not conflicts(g, c)), None)
            if g is None:
                g = {"key": key, "env": dict(c.env), "start_ini": dict(c.start_ini), "cases": [], "own": set(),
                     "other": set()}
                groups.append(g)
            g["cases"].append(c)
            g["own"] |= set(c.charts)
            g["other"] |= set(c.charts_other)
        for g in groups:  # boot first, a case that ends the game last
            g["cases"].sort(key=lambda c: (c.name != "boot", c.ends_game))
        groups.sort(key=lambda g: (g["key"] == ("skipped",), g["key"] != ((), ())))
        return groups

    def skip_reason(self, case, edition):
        missing = [n for n in case.needs if n in self.missing.get(edition, set())]
        if missing:
            if case.manual and set(missing) <= NOT_DRIVEN:
                return "MANUAL"
            return "needs " + ", ".join(missing)
        if case.route is None:
            return "MANUAL"
        return None

    def place_charts(self, inst, group):
        """dlc/gametest emptied, then the group's charts in the edition's own form and Song Bridge's in the other's."""
        d = inst.dlc
        if os.path.realpath(d).startswith(os.path.realpath(PLAYER)):
            raise Stuck("%s is the player's dlc folder: refusing" % d)
        if os.path.isdir(d):
            for f in os.listdir(d):
                p = os.path.join(d, f)
                if os.path.isdir(p) and not os.path.islink(p):
                    shutil.rmtree(p)
                else:
                    os.remove(p)
        os.makedirs(d, exist_ok=True)
        placed = []
        for names, form in ((group["own"], C.OWN_FORM[inst.edition]), (group["other"], C.OTHER_FORM[inst.edition])):
            for n in sorted(names):
                ch = self.charts.resolve(n)
                src = ch["files"].get(form)
                if not src or not os.path.exists(src):
                    raise Stuck("no %s form of the chart %s in %s/fixtures (fixtures.py)" % (form, n, self.root))
                dst = os.path.join(d, os.path.basename(src))
                if subprocess.run(["cp", "-c", src, dst], capture_output=True).returncode:
                    shutil.copyfile(src, dst)
                placed.append(os.path.relpath(dst, inst.game_dir))
        return placed

    # -- running
    def run_edition(self, edition, cases):
        inst = Install(self.root, edition, self.builds)
        bad = inst.problems()
        self.missing[edition] = self.capabilities(inst)
        res = self.results.setdefault(edition, [])
        self.sessions[edition] = []
        say("%s: %d case(s); without: %s" % (edition, len(cases), ", ".join(sorted(self.missing[edition] - NOT_DRIVEN))
                                              or "nothing"))
        groups = self.plan(cases, edition)
        for gi, g in enumerate(groups):
            if g["key"] == ("skipped",):
                for c in g["cases"]:
                    res.append(self.not_run(c, edition, self.skip_reason(c, edition)))
                continue
            if bad:
                for c in g["cases"]:
                    res.append(self.not_run(c, edition, "FAIL", why="; ".join(bad)))
                continue
            self.run_group(inst, g, gi + 1, res)

    def not_run(self, case, edition, reason, why=None):
        status = "MANUAL" if reason == "MANUAL" else "FAIL" if reason == "FAIL" else "SKIP"
        r = {"case": case.name, "edition": edition, "status": status, "time_s": 0.0, "doc": case.doc,
             "why": why or (case.manual if status == "MANUAL" else reason), "checks": []}
        d = os.path.join(self.run_dir, edition, case.name)
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, "result.json"), "w") as f:
            json.dump(r, f, indent=1)
        say("  %-12s %s  %s" % (case.name, status, r["why"]))
        return r

    def run_group(self, inst, g, gi, res):
        label = "g%d" % gi
        group_dir = os.path.join(self.run_dir, inst.edition, "_" + label)
        env = dict(g["env"])
        if "fake_input" not in self.missing[inst.edition]:
            env.setdefault("RS_AUDIO_FAKE_INPUT", os.environ.get("RS_GAMETEST_FAKE_INPUT", FAKE_INPUT))
            env.setdefault("RS_AUDIO_LOG", "1")  # the input's peak line (rs_audio_input.c:362)
        base = base_ini(inst)
        placed = self.place_charts(inst, g)
        write_ini(inst.ini, ini_text(base, C.TEST_BASE, g["start_ini"]))
        s = Session(self, inst, label, env, group_dir)
        self.session = s
        rec_sess = {"label": label, "edition": inst.edition, "env": env, "start_ini": {"%s/%s" % k: v for k, v in
                                                                                         g["start_ini"].items()},
                    "charts": placed, "starts": s.starts, "cases": [c.name for c in g["cases"]]}
        self.sessions[inst.edition].append(rec_sess)
        self.lock.acquire()
        try:
            try:
                s.start()
            except Stuck as e:
                say("  the game did not reach the main menu: %s" % e)
                s.close()
                try:  # once more
                    s.start()
                except Stuck as e2:
                    for c in g["cases"]:
                        res.append(self.case_result(c, inst.edition, None, "FAIL", 0.0,
                                                    why="the game did not reach the main menu: %s" % e2))
                    return
            for c in g["cases"]:
                if s.game is None or not s.game.alive():
                    s.close()
                    s.start()
                res.append(self.run_case(inst, s, c, base, g))
                if not self.args.keep_going and res[-1]["status"] == "FAIL":
                    break
        finally:
            self.case_run = None
            s.close()
            self.lock.release()
            self.session = None

    def run_case(self, inst, s, case, base, g):
        case_dir = os.path.join(self.run_dir, inst.edition, case.name)
        os.makedirs(case_dir, exist_ok=True)
        cr = CaseRun(self, s, case, inst.edition, case_dir)
        say("  %s: %s" % (case.name, case.doc))
        text = ini_text(base, C.TEST_BASE, g["start_ini"], case.ini)
        cr.begin()  # before the settings change: the mods' lines about it are the case's
        mark = s.d.mark()
        if write_ini(inst.ini, text):
            try:
                s.d.wait_for(L.INI_RELOADED, 6, mark=mark, what="RSMods.ini reread")
            except Stuck:
                s.d.note("no `RSMods.ini (re)loaded` line in 6 s")
        with open(os.path.join(case_dir, "RSMods.ini"), "w") as f:
            f.write(text)
        cr.t0 = time.time()
        self.case_run = cr
        self.case_deadline = time.time() + case.timeout
        error = where = None
        try:
            case.route(cr)
        except GameEnded as e:
            error, where = str(e), e.where
        except Stuck as e:
            error, where = str(e), e.where
        except Exception as e:  # a bug of a route: the case fails, the run goes on
            error, where = "%s: %s" % (type(e).__name__, e), cr.d.screen if s.d else None
            traceback.print_exc()
        self.case_deadline = None
        self.case_run = None
        elapsed = time.time() - cr.t0
        if error:
            say("    stuck: %s (on %s)" % (error, where))
            if case.ends_game and cr.quit_status is not None:
                pass
            elif s.game is not None and s.game.alive():
                cr.snap("stuck")
                if not R.recover(s.d):
                    say("    not back to the main menu: the game is started again")
                    s.close()
            # a game that ended (a crash, or a route's quit) is started again by run_group for the next case
        results = cr.evaluate(final=True)
        if case.extra:
            try:
                results += case.extra(cr)
            except Exception as e:
                results.append({"name": "the case's own checks", "status": L.FAIL, "why": "%s: %s" % (
                    type(e).__name__, e), "source": "game", "src": "", "pattern": ""})
        status = L.summary(results)
        if error:
            status = L.FAIL
        if status == L.PENDING:
            status = L.FAIL
        r = self.case_result(case, inst.edition, cr, status, elapsed, why=error, where=where, checks=results)
        self.write_case(cr, r)
        failed = [x["name"] for x in results if x["status"] == L.FAIL]
        skipped = [x["name"] for x in results if x["status"] == L.SKIP]
        say("    %s in %.1f s%s%s" % (status, elapsed, "; failed: " + ", ".join(failed) if failed else "",
                                     "; skipped: " + ", ".join(skipped) if skipped else ""))
        return r

    def case_result(self, case, edition, cr, status, elapsed, why=None, where=None, checks=()):
        return {"case": case.name, "edition": edition, "status": status, "time_s": round(elapsed, 1),
                "doc": case.doc, "why": why, "where": where, "checks": list(checks),
                "snapshots": cr.snaps if cr else [], "game": cr.s.label if cr else None,
                "trace": [(round(t - cr.t0, 1), x) for t, x in cr.d.trace if t >= cr.t0] if cr and cr.s.d else []}

    def write_case(self, cr, r):
        d = cr.dir
        src = cr.sources("case")
        with open(os.path.join(d, "game.log"), "w") as f:  # boot's lines are the start's
            f.write("\n".join(cr.lines("session" if cr.case.name == "boot" else "case")) + "\n")
        if cr.restart_lines is not None:
            with open(os.path.join(d, "game-restart.log"), "w") as f:
                f.write("\n".join(cr.restart_lines) + "\n")
        with open(os.path.join(d, "companion.log"), "w") as f:
            f.write("\n".join(src["companion"]) + "\n")
        with open(os.path.join(d, "events.jsonl"), "w") as f:
            f.write("\n".join(src["events"]) + ("\n" if src["events"] else ""))
        with open(os.path.join(d, "keyfifo.log"), "w") as f:
            f.write("\n".join(t for t in cr.lines("case") if t.startswith("keyfifo:")) + "\n")
        # each failed check's lines, for the report
        lines = {"game": cr.lines("case"), "companion": src["companion"], "events": src["events"]}
        sess = None
        for x in r["checks"]:
            if x["status"] != L.FAIL:
                continue
            ls = lines.get(x.get("source"), [])
            if x.get("scope") == "session":
                sess = sess or cr.sources("session")
                ls = sess.get(x.get("source"), [])
            if x.get("lines"):
                x["context"] = [[i, t] for i, t in L.context(ls, x["lines"][0])]
            else:  # no line: the lines that name what the pattern names (its longest word), else the last ones
                words = sorted(re.findall(r"[A-Za-z][A-Za-z0-9_]{5,}", x.get("pattern") or ""), key=len)
                near = [[i, t] for i, t in enumerate(ls) if words and words[-1].lower() in t.lower()][:6]
                x["context"] = near or [[i, t] for i, t in enumerate(ls)][-8:]
        with open(os.path.join(d, "result.json"), "w") as f:
            json.dump(r, f, indent=1, default=str)


def conflicts(group, case):
    """The same chart in both forms in one game: Song Bridge leaves the other form out (songbridge.c:404)."""
    return bool(set(case.charts_other) & group["own"] or set(case.charts) & group["other"])


def has_fake_input(inst):
    """RS Re-ARMed's test input (RS_AUDIO_FAKE_INPUT) is in the build: the switch's name is in its program."""
    paths = [inst.exe]
    if inst.edition == "mac":
        paths += glob.glob(os.path.join(os.path.dirname(inst.exe), "*.dylib"))
        paths += glob.glob(os.path.join(os.path.dirname(os.path.dirname(inst.exe)), "Frameworks", "*.dylib"))
    else:
        paths += glob.glob(os.path.join(os.path.dirname(inst.exe), "*.dylib"))
    for p in paths:
        try:
            if subprocess.run(["grep", "-qa", "RS_AUDIO_FAKE_INPUT", p]).returncode == 0:
                return True
        except OSError:
            pass
    return False


def call_setup(args, editions):
    setup = os.path.join(HERE, "setup.py")
    if not os.path.exists(setup):
        say("no tests/game/setup.py yet: the installs are taken as they are")
        return 0
    try:
        helptext = subprocess.run([sys.executable, "-I", setup, "--help"], capture_output=True, text=True,
                                  timeout=60).stdout
    except (OSError, subprocess.TimeoutExpired):
        helptext = ""
    cmd = [sys.executable, "-I", setup]
    if "--edition" in helptext:
        cmd += ["--edition", editions[0] if len(editions) == 1 else "both"]
    if "--builds" in helptext:
        cmd += ["--builds", args.builds]
    if "--root" in helptext:
        cmd += ["--root", args.root]
    say("setup: %s" % " ".join(cmd[2:]))
    env = dict(os.environ, RS_GAMETEST_ROOT=os.path.abspath(os.path.expanduser(args.root)))
    # setup.py refuses while a game run holds the lock (its game reads the installs): wait for the lock to be free,
    # then run it; another run that takes the lock meanwhile makes it refuse, and it is tried again
    lock = Lock(say=say)
    end = time.time() + 1800
    while True:
        lock.wait_free(end)
        p = subprocess.run(cmd, env=env, capture_output=True, text=True)
        sys.stdout.write(p.stdout + p.stderr)
        if p.returncode == 0 or ".game-lock" not in p.stdout + p.stderr or time.time() > end:
            return p.returncode
        say("setup: a game run took the lock meanwhile; waiting")


def list_cases():
    for c in C.CASES:
        flags = []
        if c.needs:
            flags.append("needs " + ", ".join(c.needs))
        if c.start_ini or c.env:
            flags.append("own start")
        say("%-12s %-28s %s%s" % (c.name, ",".join(sorted(c.suites)), c.doc, ("  [%s]" % "; ".join(flags)) if flags
                                   else ""))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--edition", choices=("mac", "lp", "both"), default="both")
    ap.add_argument("--suite", choices=C.SUITES, default="smoke")
    ap.add_argument("--case", action="append", metavar="NAME", help="one case (again for more); overrides --suite")
    ap.add_argument("--builds", choices=("main", "installed"), default="main")
    ap.add_argument("--root", default=os.environ.get("RS_GAMETEST_ROOT", "~/rs-gametest"))
    ap.add_argument("--keep-going", dest="keep_going", action="store_true", default=True)
    ap.add_argument("--stop-on-fail", dest="keep_going", action="store_false")
    ap.add_argument("--no-setup", action="store_true")
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args(argv)
    if a.list:
        list_cases()
        return 0
    try:
        cases = C.select(a.suite, a.case)
    except KeyError as e:
        say(str(e))
        return 2
    editions = ["mac", "lp"] if a.edition == "both" else [a.edition]
    if not a.no_setup and call_setup(a, editions):
        say("setup.py failed: no game run")
        return 1
    runner = Runner(a)

    def on_signal(sig, frame):
        raise Interrupted("signal %d" % sig)
    for sg in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
        signal.signal(sg, on_signal)
    t0 = time.time()
    code = 0
    try:
        runner.load()
        os.makedirs(runner.run_dir, exist_ok=True)
        say("run %s (%s, %s)" % (runner.run_dir, a.suite if not a.case else "cases", a.builds))
        for ed in editions:
            te = time.time()
            runner.run_edition(ed, cases)
            runner.sessions[ed + "_time"] = round(time.time() - te, 1)
    except Interrupted as e:
        say("stopped: %s" % e)
        code = 130
    except Stuck as e:
        say("stopped: %s" % e)
        code = 1
    finally:
        if runner.session:
            try:
                runner.session.close()
            except BaseException:
                pass
        runner.lock.release()
    if os.path.isdir(runner.run_dir):
        path = gt_report.write(runner, time.time() - t0)
        say("report: %s" % path)
    fails = sum(1 for ed in runner.results.values() for r in ed if r["status"] == "FAIL")
    unclean = [(ed, st["n"], st.get("quit_how"), st.get("exit_code")) for ed, ss in runner.sessions.items()
               if isinstance(ss, list) for g in ss for st in g["starts"]
               if st.get("exit_code") != 0 or st.get("quit_how") in ("SIGTERM", "SIGKILL")]
    for u in unclean:
        say("NOT A CLEAN QUIT: %s start %s: %s, exit status %s" % u)
    fails += len(unclean)
    return code or (1 if fails else 0)


if __name__ == "__main__":
    sys.exit(main())
