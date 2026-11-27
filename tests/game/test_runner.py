#!/usr/bin/env python3
"""Tests of the game tests' runner without a game: the log matcher and the checks against recorded logs of real
runs, the route engine against a simulated game that answers keys with the mods' log lines, the settings file and the
plan of game starts.

    python3 -I -m pytest tests/game/test_runner.py      or      python3 -I tests/game/test_runner.py

The recorded logs (Learn & Play's scripted runs of the 7- and 8-string songs, a Mac session with the 7-string song)
are on this Mac's drives; a test whose log is not there is skipped.
"""
import json
import os
import re
import sys
import tempfile
import threading
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.dont_write_bytecode = True  # no __pycache__ in the repository

import gt_cases as C  # noqa: E402
import gt_game as G  # noqa: E402
import gt_log as L  # noqa: E402
import gt_routes as R  # noqa: E402
import run  # noqa: E402

LP_RUNS = "/Volumes/Storage/Rocksmith/build/seven-lp/run/runs"
LP_UP1 = os.path.join(LP_RUNS, "up1.log")        # Nickelback 7-string, Learn & Play, to the highway
LP_NEXT2 = os.path.join(LP_RUNS, "next2.log")    # Fear Factory 8-string, Learn & Play
MAC_RUN = "/Volumes/Storage/Rocksmith/build/seven-up-m4/mac-run1.log"  # the Mac, the 7-string song, paused

NICKELBACK = {"key": "DellNickelbackBurnItToTheGround7StringV23", "strings": 7, "tuning": "B STANDARD 7STR",
              "low_midi": 35}
FEARFACTORY = {"key": "DellFearFactorySoulHacker8StringT2", "strings": 8, "tuning": "F♯ STANDARD 8STR",
               "low_midi": 30}


def need(path):
    return unittest.skipUnless(os.path.exists(path), "%s is not here" % path)


def results(checks, lines, missing=()):
    return {r["name"]: r for r in L.evaluate(checks, {"game": lines, "companion": [], "events": []}, missing)}


# ---------------------------------------------------------------------------------------------------------------

class TestLog(unittest.TestCase):
    def test_partial_lines_wait_for_their_end(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "log")
            log = L.Log(p)
            self.assertEqual(log.poll(), 0)  # no file yet
            with open(p, "ab") as f:
                f.write(b"[rsmods] menu: TitleScreen\n[rsmods] men")
            self.assertEqual(log.poll(), 1)
            with open(p, "ab") as f:
                f.write(b"u: MainMenu\n")
            self.assertEqual(log.poll(), 1)
            self.assertEqual([s for _, s in L.screens(log.text())], ["TitleScreen", "MainMenu"])

    def test_companion_time_stripped(self):
        self.assertEqual(L.strip_companion_time("09:12:13.456 selected arrangement Lead"), "selected arrangement Lead")

    @need(LP_UP1)
    def test_screens_of_a_learn_and_play_run(self):
        s = [x for _, x in L.screens(L.lines_of(LP_UP1))]
        want = ["MainOverlay", "TitleScreen", "ProfileSelect", "SelectionListDialog", "ProfileSelect", "MainMenu",
                "LearnASong_Songs", "LearnASong_SongOptions", "LearnASong_PreSongTuner", "SelectionListDialog",
                "LearnASong_PreSongTuner", "LearnASong_Game"]
        self.assertEqual(s, want)
        self.assertEqual(L.songs(L.lines_of(LP_UP1))[-1][1], NICKELBACK["key"])

    @need(MAC_RUN)
    def test_screens_of_a_mac_run(self):
        s = [x for _, x in L.screens(L.lines_of(MAC_RUN))]
        self.assertEqual(s[-3:], ["LearnASong_PreSongTuner", "LearnASong_Game", "LearnASong_Pause"])


class TestChecks(unittest.TestCase):
    def test_counts_groups_and_forbid(self):
        lines = ["[rsmods] game note: 40 (instrument 1)", "[rsmods] game note: -1 (instrument 1)",
                 "[rsmods] game note: 40 (instrument 1)"]
        c = L.Check("heard 40", r"^\[rsmods\] game note: (-?\d+) \(", groups={1: "40"}, min=2)
        self.assertEqual(c.evaluate({"game": lines})["status"], L.PASS)
        c = L.Check("heard 35", r"^\[rsmods\] game note: (-?\d+) \(", groups={1: "35"})
        self.assertEqual(c.evaluate({"game": lines})["status"], L.FAIL)
        self.assertEqual(c.evaluate({"game": lines}, final=False)["status"], L.PENDING)
        c = L.Check("at most one", r"game note: 40", max=1)
        self.assertEqual(c.evaluate({"game": lines})["status"], L.FAIL)
        c = L.Check("any positive", r"game note: (-?\d+)", groups={1: lambda n: int(n) > 0})
        self.assertEqual(c.evaluate({"game": lines})["count"], 2)

    def test_needs_and_missing_sources_skip(self):
        c = L.Check("heard", r"game note", needs=("fake_input",))
        self.assertEqual(c.evaluate({"game": []}, missing={"fake_input"})["status"], L.SKIP)
        c = L.Check("companion", r"x", source="companion")
        self.assertEqual(c.evaluate({"game": []})["status"], L.SKIP)
        self.assertEqual(L.summary([{"status": L.SKIP}, {"status": L.PASS}]), L.PASS)
        self.assertEqual(L.summary([{"status": L.SKIP}]), L.SKIP)

    def test_faults_are_caught(self):
        lines = ["lp: signal 11: guest address 0x10", "lp: stack (atos -l 0x100000000): 1 2",
                 "[rc] hook x: 0x5 is not in guest memory: the game gets a null pointer", "[rc] a fault in F_1234"]
        r = results(C.fault_checks(), lines)
        self.assertTrue(all(x["status"] == L.FAIL for x in r.values()), r)
        r = results(C.fault_checks(), ["[rsmods] menu: MainMenu", "lp: the window is visible"])
        self.assertTrue(all(x["status"] == L.PASS for x in r.values()), r)

    def test_stays_off(self):
        c = [x for x in C.start_checks("mac") if x.name == "no mod stays off"][0]
        line = "[rsmods] no hook point si_io on mac-x64: its mod stays off"
        self.assertEqual(c.evaluate({"game": [line]})["status"], L.FAIL)

    def test_menu_load_pairs(self):
        ok = ["[rs-mods] menu load: 'x' was rejected (not indexed yet); waiting for the indexer",
              "[rs-mods] menu load: 'x' accepted after 2 requests"]
        self.assertEqual(C.menu_load_pairs(ok)["status"], L.PASS)
        self.assertEqual(C.menu_load_pairs(ok[:1])["status"], L.FAIL)
        self.assertEqual(C.menu_load_pairs([])["status"], L.PASS)

    @need(MAC_RUN)
    def test_mac_start_checks(self):
        lines = L.lines_of(MAC_RUN)
        r = results(C.start_checks("mac"), lines)
        # that session ran without RS_STEAM_LOG (no profile lines) and before the companion had its own log here
        for name, x in r.items():
            if name == "the profile read" or x["source"] == "companion":
                continue
            self.assertIn(x["status"], (L.PASS, L.SKIP), (name, x))
        # the Learn & Play target is not this log's
        r = results(C.start_checks("lp"), lines)
        self.assertEqual(r["rearmed-mods loaded, 38 mods"]["status"], L.FAIL)

    @need(MAC_RUN)
    def test_mac_guitar7_checks(self):
        lines = L.lines_of(MAC_RUN)
        r = results(C.seven_song_checks(NICKELBACK) + C.playing_checks(NICKELBACK["key"]), lines)
        for name, x in r.items():
            if name == "chord frames":  # paused 4 s in, before the first chord
                continue
            self.assertEqual(x["status"], L.PASS, (name, x))
        self.assertEqual(r["tuner on string 6 at MIDI 35"]["status"], L.PASS)
        # the same checks for the 8-string chart do not pass on it
        r = results(C.seven_song_checks(FEARFACTORY), lines)
        self.assertEqual(r["guitar built with 8 strings"]["status"], L.FAIL)
        self.assertEqual(r["tuner on string 7 at MIDI 30"]["status"], L.FAIL)

    @need(LP_NEXT2)
    def test_lp_guitar8_checks(self):
        lines = L.lines_of(LP_NEXT2)
        r = results(C.seven_song_checks(FEARFACTORY), lines)
        # that run stopped on the highway: nothing freed yet
        for name, x in r.items():
            if name == "the song freed on exit":
                self.assertEqual(x["status"], L.FAIL)
            else:
                self.assertEqual(x["status"], L.PASS, (name, x))
        self.assertIn("F♯ STANDARD 8STR", r["tuning named F♯ STANDARD 8STR"]["name"])

    @need(LP_UP1)
    def test_lp_up1_seven_string_lines(self):
        lines = L.lines_of(LP_UP1)
        r = results(C.seven_song_checks(NICKELBACK) + C.scoped(C.seven_start_checks(), "session"), lines)
        # an older build's headstock line ("shows the seven-peg headstock"): the check names today's
        self.assertEqual(r["headstock model shown"]["status"], L.FAIL)
        for name in ("guitar built with 7 strings", "tuner on string 6 at MIDI 35", "SNXR converted (7 strings)",
                     "RS7X block read", "detection widened", "tuning named B STANDARD 7STR", "native package accepted",
                     "manifests rewritten", "Seven-String edits on", "Seven-String on", "Plain Packages on",
                     "native charts on"):
            self.assertEqual(r[name]["status"], L.PASS, (name, r[name]))

    def test_events_source(self):
        ev = [json.dumps({"event": "song", "song_key": "SixteenSaltines", "tones": []}),
              json.dumps({"event": "state", "song_key": "SixteenSaltines"})]
        r = {x["name"]: x for x in L.evaluate(C.checks_companion(FakeCtx()), {"game": [], "companion": [],
                                                                               "events": ev})}
        self.assertEqual(r["companion: song event"]["status"], L.PASS)
        self.assertEqual(r["companion: state with the song"]["status"], L.PASS)
        self.assertEqual(r["companion: tone event"]["status"], L.FAIL)


class TestExitSave(unittest.TestCase):
    def ctx(self, first, second, status=0):
        class X:
            quit_status, quit_how = status, "EXIT GAME"

            def lines(self, scope):
                return {"first_game": first, "restart": second}[scope]
        return X()

    def test_the_profile_written_is_read(self):
        w = "[steam-shim] FileWrite(5C61_PRFLDB, 86244) -> 1"
        r = "[steam-shim] FileRead(5C61_PRFLDB, 86244) -> 86244"
        got = {x["name"]: x for x in C.exit_save_extra(self.ctx([w], ["x", r]))}
        self.assertEqual(got["the profile last saved is the one read at the next start"]["status"], L.PASS)
        self.assertEqual(got["a clean quit"]["status"], L.PASS)
        got = C.exit_save_extra(self.ctx([w], ["[steam-shim] FileRead(5C61_PRFLDB, 90000) -> 90000"], status=-9))
        self.assertEqual([x["status"] for x in got], [L.FAIL, L.FAIL])
        got = C.exit_save_extra(self.ctx([], [r]))
        self.assertEqual(got[0]["status"], L.SKIP)
        got = C.exit_save_extra(self.ctx([w], None))
        self.assertEqual(got[0]["status"], L.FAIL)


class FakeCtx:
    edition = "mac"
    missing = set()

    def stock(self):
        return {"key": "SixteenSaltines", "search": "sixt"}


# ---------------------------------------------------------------------------------------------------------------
# A simulated game: the mods' lines for each key (menu:, song:, key down), screens that come after a delay, keys
# lost while a screen moves in

class FakeGame:
    def __init__(self, path, edition="mac", drop_first=(), songs=None):
        self.log = L.Log(path)
        self.f = open(path, "a")
        self.t_start = time.time()
        self.exit_code = None
        self.screen = None
        self.drop_first = set(drop_first)  # (screen, key): the first such key is lost
        self.songs = songs or ["AAA", "SixteenSaltines", "ZZZ"]
        self.song_i = 0
        self.typing = ""
        self.pause_i = 0
        self.ended = False
        self.timers = []
        self.sent = []
        self.edition = edition

    def emit(self, line):
        self.f.write(line + "\n")
        self.f.flush()

    def goto(self, s, after=0.0):
        def go():
            self.screen = s
            self.emit("[rsmods] menu: " + s)
            if s == "LearnASong_Game":
                self.emit("[rsmods] Play_SixteenSaltines: playing id 7")
                self.emit("[rsmods] accuracy: shown (LearnASong_Game, position 3.5, 0 hit, 0 missed)")
        if after:
            t = threading.Timer(after, go)
            self.timers.append(t)
            t.start()
        else:
            go()

    def alive(self):
        return not self.ended

    def send(self, *cmds):
        for c in cmds:
            self.sent.append(c)
            if not c.startswith("key "):
                continue
            k = c.split()[1]
            self.emit("[rsmods] key down 0x%x" % G.KEYCODES.get(k, 0))
            if (self.screen, k) in self.drop_first:
                self.drop_first.discard((self.screen, k))
                continue
            self.on_key(k)

    def on_key(self, k):
        s = self.screen
        nav = {("TitleScreen", "return"): "ProfileSelect", ("ProfileSelect", "return"): "MainMenu",
               ("MainMenu", "return"): "LearnASong_Songs", ("LearnASong_SongOptions", "esc"): "LearnASong_Songs",
               ("LearnASong_Songs", "esc"): "MainMenu", ("LearnASong_Game", "esc"): "LearnASong_Pause",
               ("MainMenu", "esc"): "SelectionListDialog"}
        if s == "LearnASong_Songs" and k == "/":
            self.typing = "/"
            return
        if s == "LearnASong_Songs" and self.typing and len(k) == 1 and k.isalpha():
            self.typing += k
            if len(self.typing) == 4:
                self.song_i = 0
                self.emit("[rsmods] song: " + self.songs[0])
            return
        if s == "LearnASong_Songs" and k == "down":
            if self.song_i + 1 < len(self.songs):
                self.song_i += 1
                self.emit("[rsmods] song: " + self.songs[self.song_i])
            return
        if s == "LearnASong_Songs" and k == "return":
            self.goto("LearnASong_SongOptions", 0.2)
            return
        if s == "LearnASong_Songs" and k == "esc" and self.typing:
            self.typing = ""
            return
        if s == "LearnASong_SongOptions" and k == "return":
            for i in range(5):  # loading lines, then the tuner
                threading.Timer(0.2 * i, self.emit, ["[rsmods] seven-string: strings: loading %d" % i]).start()
            self.emit("[rsmods] seven-string: strings: target tuning stored (player 0, song selection): 6 strings")
            self.goto("LearnASong_PreSongTuner", 1.2)
            return
        if s == "LearnASong_PreSongTuner" and k == "fwddelete":
            self.goto("SelectionListDialog", 0.2)
            self.after_dialog = "tuner"
            return
        if s == "SelectionListDialog" and k == "return" and getattr(self, "after_dialog", "") == "tuner":
            self.after_dialog = ""
            self.goto("LearnASong_PreSongTuner")
            self.goto("LearnASong_Game", 0.5)
            return
        if s == "LearnASong_Pause":
            if k == "up":
                self.pause_i = max(0, self.pause_i - 1)
            elif k == "down":
                self.pause_i = min(5, self.pause_i + 1)
            elif k == "return":
                if self.pause_i == 5:
                    self.emit("[rsmods] seven-string: chords: SNG 0x1 freed (had 0 T8, 0 C8)")
                    self.goto("LearnASong_SongOptions", 0.3)
                elif self.pause_i == 0:
                    self.goto("LearnASong_Game")
            elif k == "esc":
                self.goto("LearnASong_Game")
            return
        if s == "SelectionListDialog" and getattr(self, "quit_q", False):
            if k == "down":
                self.quit_no = True
            elif k == "return":
                self.quit_q = False
                self.goto("MainMenu" if getattr(self, "quit_no", False) else "TitleScreen", 0.1)
            return
        if s == "MainMenu" and k == "esc":
            self.quit_q, self.quit_no = True, False
        if (s, k) in nav:
            self.goto(nav[(s, k)], 0.1)

    def quit(self):
        self.ended = True
        for t in self.timers:
            t.cancel()
        self.f.close()


class TestRoutes(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.path = os.path.join(self.tmp.name, "game.log")

    def tearDown(self):
        self.g.quit()
        self.tmp.cleanup()

    def driver(self, **kw):
        self.g = FakeGame(self.path, **kw)
        return G.Driver(self.g, "mac", say=lambda *a: None, key_gap=0.02)

    def test_start_to_main_menu_with_a_lost_return(self):
        d = self.driver(drop_first={("TitleScreen", "return")})
        self.g.goto("TitleScreen")
        self.g.emit("[rs-mods] DLC: 3 archives mounted in 2 frames, 0.10 s")
        # a fast settle for the test: the title's Return is lost once and sent again
        orig = d.press

        def press(key, to, settle=1.0, retry=4.0, tries=4, **kw):
            return orig(key, to, settle=0.05, retry=0.4, tries=tries, **kw)
        d.press = press
        R.to_main_menu_from_start(d, autoload=False, timeout=10)
        self.assertEqual(d.screen, "MainMenu")
        self.assertEqual(sum(1 for c in self.g.sent if c == "key return"), 3)  # title twice, the profile once

    def test_song_in_and_out(self):
        d = self.driver()
        self.g.goto("MainMenu")
        self.g.emit("[rs-mods] DLC: 3 archives mounted in 2 frames, 0.10 s")
        fast(d)
        R.learn_song(d, "SixteenSaltines", "sixt")
        self.assertEqual(d.screen, "LearnASong_Game")
        # the second match, by Down and the song: lines
        self.assertIn("key down", self.g.sent)
        R.exit_song(d)
        self.assertEqual(d.screen, "LearnASong_SongOptions")
        R.home(d)
        self.assertEqual(d.screen, "MainMenu")
        self.assertTrue(d.find(r"freed \(had"))

    def test_a_taken_key_is_not_sent_again(self):
        d = self.driver()
        self.g.goto("LearnASong_SongOptions")
        time.sleep(0.05)
        d.poll(0)
        d.press("return", ("LearnASong_PreSongTuner",), settle=0.0, retry=0.3, tries=3, started=R.PLAY_STARTED)
        self.assertEqual(d.screen, "LearnASong_PreSongTuner")
        self.assertEqual(sum(1 for c in self.g.sent if c == "key return"), 1)

    def test_progress_lines_hold_the_retry(self):
        d = self.driver()
        self.g.goto("LearnASong_SongOptions")
        time.sleep(0.05)
        d.poll(0)
        # retry 0.5 s, but the tuner comes after 1.2 s with loading lines in between: one Return only
        d.press("return", ("LearnASong_PreSongTuner",), settle=0.0, retry=0.5, tries=1, progress=R.LOADING)
        self.assertEqual(d.screen, "LearnASong_PreSongTuner")
        self.assertEqual(sum(1 for c in self.g.sent if c == "key return"), 1)

    def test_stuck_says_where(self):
        d = self.driver()
        self.g.goto("LearnASong_Songs")
        d.poll(0)
        with self.assertRaises(G.Stuck) as e:
            d.press("tab", "OptionsMenu", settle=0.0, retry=0.2, tries=2)
        self.assertEqual(e.exception.where, "LearnASong_Songs")

    def test_a_key_the_game_did_not_get_is_sent_again(self):
        d = self.driver()
        self.g.goto("MainMenu")
        d.poll(0)
        real = self.g.send
        lost = {"n": 1}

        def send(*cmds):  # the first key never reaches the game (no focus): no key down line
            if lost["n"] and any(c.startswith("key ") for c in cmds):
                lost["n"] = 0
                self.g.sent.extend(cmds)
                return
            real(*cmds)
        self.g.send = send
        d.key("return")
        self.assertEqual(sum(1 for c in self.g.sent if c == "key return"), 2)
        self.assertEqual(sum(1 for c in self.g.sent if c == "front"), 2)

    def test_a_game_that_ends_is_reported(self):
        d = self.driver()
        self.g.goto("MainMenu")
        self.g.ended = True
        with self.assertRaises(G.GameEnded):
            d.poll(0)


def fast(d):
    """The engine's waits shortened for the simulated game."""
    orig = d.press

    def press(key, to, settle=1.0, retry=4.0, tries=4, **kw):
        return orig(key, to, settle=min(settle, 0.05), retry=min(retry, 1.5), tries=tries, **kw)
    d.press = press
    orig_text = d.text
    d.text = lambda s, gap=1.0: orig_text(s, gap=0.02)
    orig_hold = d.hold
    d.hold = lambda seconds, until=None: orig_hold(min(seconds, 3.0), until)


# ---------------------------------------------------------------------------------------------------------------

class TestSettingsAndPlan(unittest.TestCase):
    def test_ini_last_key_wins(self):
        base = "[Toggle Switches]\nToneHost = on\nSevenString = on\n[Seven String]\nstring7_line = 1\n"
        text = run.ini_text(base, C.TEST_BASE, {("Seven String", "string7_line"): "0"})
        self.assertTrue(text.startswith(run.INI_MARK))
        # the reader: sections and keys, the last line of a key wins (rearmed-mods src/rsmods_ini.c rsm_get)
        sec, got = "", {}
        for line in text.splitlines():
            line = line.strip()
            if not line or line[0] in ";#":
                continue
            if line.startswith("["):
                sec = line[1:line.index("]")]
                continue
            k, v = line.split("=", 1)
            got[(sec, k.strip())] = v.strip()
        self.assertEqual(got[("Toggle Switches", "ToneHost")], "off")
        self.assertEqual(got[("Seven String", "string7_line")], "0")
        self.assertEqual(got[("Toggle Switches", "SevenString")], "on")

    def test_write_ini_only_when_changed(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "RSMods.ini")
            self.assertTrue(run.write_ini(p, "a\n"))
            self.assertFalse(run.write_ini(p, "a\n"))
            self.assertTrue(run.write_ini(p, "b\n"))

    def test_plan_groups(self):
        class A:
            pass
        a = A()
        a.root, a.suite, a.case, a.builds, a.keep_going = tempfile.gettempdir(), "all", None, "main", True
        r = run.Runner(a)
        r.missing["mac"] = set(run.NOT_DRIVEN)
        r.missing["mac"].add("fake_input")
        cases = C.select("smoke") + [C.BY_NAME["line_off"], C.BY_NAME["guitar8"], C.BY_NAME["coop"]]
        groups = r.plan(cases, "mac")
        first = groups[0]
        self.assertEqual(first["key"], ((), ()))
        self.assertEqual(first["cases"][0].name, "boot")
        self.assertEqual(first["cases"][-1].name, "exit_save")
        # guitar8 puts Fear Factory there in its own form: not in the game where Song Bridge serves its other form
        names = [[c.name for c in g["cases"]] for g in groups]
        g_sb = next(n for n in names if "songbridge" in n)
        self.assertNotIn("guitar8", g_sb)
        self.assertIn(["line_off"], names)
        skipped = next(g for g in groups if g["key"] == ("skipped",))
        self.assertIn("coop", [c.name for c in skipped["cases"]])

    def test_suites(self):
        smoke = [c.name for c in C.select("smoke")]
        self.assertEqual(smoke, ["boot", "song", "bass", "pause_menu", "songbridge", "companion", "guitar7", "exit_save"])
        ext = {c.name for c in C.select("extended")}
        self.assertEqual(ext, {"guitar7", "guitar8", "bass5", "line_off", "alternates", "coop", "hearing", "songbridge"})
        self.assertEqual(len(C.select("all")), len(C.CASES))

    def test_charts_from_fixtures_json(self):
        with tempfile.TemporaryDirectory() as root:
            os.makedirs(os.path.join(root, "fixtures"))
            data = {"charts": {"nickelback": {
                "files": {"pc": "N_p.psarc", "mac": "N_m.psarc"}, "song_key": NICKELBACK["key"], "search": "burn it",
                "arrangements": [{"name": "Rhythm", "instrument": "guitar", "strings": 6},
                                 {"name": "Lead", "instrument": "guitar", "strings": 7, "tuning_name": "B STANDARD 7STR",
                                  "lowest_midi": 35},
                                 {"name": "Bass", "instrument": "bass", "strings": 5, "lowest_midi": 23}]}},
                "stock_songs": {"mac": {"song_key": "SixteenSaltines", "search": "sixteen"}}}
            with open(os.path.join(root, "fixtures", "fixtures.json"), "w") as f:
                json.dump(data, f)
            ch = run.Charts(root).resolve("nickelback")
            self.assertEqual((ch["key"], ch["strings"], ch["low_midi"], ch["arrangement"]),
                             (NICKELBACK["key"], 7, 35, "Lead"))
            self.assertTrue(ch["files"]["mac"].endswith("fixtures/mac/N_m.psarc"))
            self.assertEqual(run.Charts(root).stock("mac")["key"], "SixteenSaltines")
            self.assertEqual(run.Charts(root).stock("lp")["key"], C.STOCK["key"])

    @unittest.skipUnless(os.path.exists(os.path.expanduser("~/rs-gametest/fixtures/fixtures.json")), "no fixtures")
    def test_the_real_fixtures_resolve(self):
        ch = run.Charts(os.path.expanduser("~/rs-gametest"))
        for name in ("nickelback", "fearfactory"):
            c = ch.resolve(name)
            self.assertTrue(c.get("key") and c.get("search"), c)
            for form in ("pc", "mac"):
                self.assertTrue(os.path.exists(c["files"][form]), (name, form, c["files"]))


class TestLock(unittest.TestCase):
    def test_lock_and_its_release(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, ".game-lock")
            lk = G.Lock(p, say=lambda *a: None)
            G.game_running_saved = G.game_running
            G.game_running = lambda: []
            try:
                lk.acquire(wait=1, every=0.1)
                self.assertTrue(os.path.isdir(p))
                other = G.Lock(p, say=lambda *a: None)
                with self.assertRaises(G.Stuck):
                    other.acquire(wait=0.3, every=0.1)
                lk.release()
                self.assertFalse(os.path.exists(p))
                # a lock of this runner whose process is gone is taken over
                os.mkdir(p)
                with open(os.path.join(p, "owner.json"), "w") as f:
                    json.dump({"pid": 999999, "by": "rearmed-mods tests/game/run.py"}, f)
                other.acquire(wait=1, every=0.1)
                other.release()
                # someone else's lock is not
                os.mkdir(p)
                with self.assertRaises(G.Stuck):
                    G.Lock(p, say=lambda *a: None).acquire(wait=0.3, every=0.1)
            finally:
                G.game_running = G.game_running_saved


if __name__ == "__main__":
    unittest.main()
