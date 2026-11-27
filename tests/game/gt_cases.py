"""The cases: what each one puts in place, its route and its checks (tests/game/DESIGN.md, the tables).

A case's checks are built for the edition and the charts it gets (build_checks(ctx)). Each pattern cites the line
that prints it. scope "case": the case's own lines; "session": the game's whole log so far (lines printed at the
start or at the DLC mounting, such as a native package accepted).
"""
import re
import time

import gt_log as L
import gt_routes as R
from gt_game import HIGHWAY, MAIN, PAUSE, RIFF, SONG_PAGE, SONGS, TUNER, Stuck
from gt_log import Check

TARGET = {"mac": "mac-x64", "lp": "win-x86"}
OWN_FORM = {"mac": "mac", "lp": "pc"}
OTHER_FORM = {"mac": "pc", "lp": "mac"}
EXPECTED_MODS = 38  # rearmed-mods' MODS[] (src/rsmods.c), as DESIGN.md says

# The stock song of the stock-flow cases where fixtures.json has none (its stock_songs: per edition, a disc song)
STOCK = {"key": "CherubRock", "search": "cher"}

# What a player has, less what reaches outside the test install (DESIGN.md, How a run goes 2)
TEST_BASE = {("Toggle Switches", "ToneHost"): "off", ("Toggle Switches", "DiscordRichPresence"): "off",
             ("Toggle Switches", "SongStems"): "off",
             # the lines some checks read: on, as in the player's own settings
             ("Toggle Switches", "ShowCurrentNoteOnScreen"): "on", ("Toggle Switches", "DisplayCurrentAccuracy"): "on",
             ("Toggle Switches", "SevenString"): "on", ("Toggle Switches", "PlainPackages"): "on"}


class Case:
    def __init__(self, name, suites, doc, route=None, checks=None, charts=(), charts_other=(), ini=None,
                 start_ini=None, env=None, needs=(), manual=None, ends_game=False, timeout=240, extra=None):
        self.name, self.suites, self.doc = name, set(suites), doc
        self.route, self.checks = route, checks or (lambda ctx: [])
        self.charts, self.charts_other = tuple(charts), tuple(charts_other)
        self.ini, self.start_ini, self.env = dict(ini or {}), dict(start_ini or {}), dict(env or {})
        self.needs, self.manual, self.ends_game, self.timeout = tuple(needs), manual, ends_game, timeout
        self.extra = extra

    def group_key(self):
        return (tuple(sorted(self.env.items())), tuple(sorted(self.start_ini.items())))


def C(name, pattern, **kw):
    return Check(name, pattern, **kw)


# ---------------------------------------------------------------------------------------------------------------
# Checks every case has, and the ones of a game's start

def fault_checks():
    out = [C("no " + n, p, forbid=True, src="rs-rearmed win-x86/app/lp_main.c:364-375, tools/ws7/recomp/runtime/"
             "rc32_hook.c:39,224") for n, p in L.FAULTS]
    return out


def start_checks(ed):
    """The start of a game (boot, and every case on both editions: DESIGN.md, the extended-range cases)."""
    t = TARGET[ed]
    out = [
        C("rearmed-mods loaded, %d mods" % EXPECTED_MODS, L.MODS_LOADED.pattern, groups={2: t, 3: str(EXPECTED_MODS)},
          src="rearmed-mods src/rsmods.c:473"),
        C("no mod stays off", L.STAYS_OFF, forbid=True,
          src="rearmed-mods src/rsmods.c:445, mod_camera.c:152, mod_loft.c:230, mod_extended_range.c:205, "
              "mod_song_stems.c:267"),
        C("Song Bridge loaded", r"^\[rc\] mod loaded: songbridge\.dylib", src="rs-rearmed tools/ws7/recomp/runtime/rc_mods.c:66"),
        C("Song Bridge's target", r"^\[songbridge\] %s: _[pm]\.psarc archives in dlc are read as _[pm]\.psarc" % t,
          src="rearmed-songbridge src/songbridge.c:551"),
        C("companion loaded", r"^\[rc\] mod loaded: librscompanion\.dylib", src="rs-rearmed tools/ws7/recomp/runtime/rc_mods.c:66"),
        C("companion's own log", r"^rscompanion prototype loaded \(pid \d+, RS Re-ARMed", source="companion",
          src="rs-mac-companion native/rscompanion.c:1941,1943"),
        C("built-in fixes on", r"^\[rs-mods\] fixes on, bypass on, overlay on, intro skip on, autosave on",
          src="rs-rearmed tools/ws7/recomp/overrides/mods/mods_shared.c:1499"),
        C("intro skipped", r"^\[rs-mods\] intro: skipped", src="rs-rearmed .../mods/mods_shared.c:161"),
        C("frame rate fixes on", r"^\[rs-mods\] frame rate: on, (\d+) of \1 points",
          src="rs-rearmed .../mods/mods_shared.c:1504"),
        C("Guitarcade Skip Tuner on", r"^\[rs-mods\] Guitarcade Skip Tuner: on", src="rs-rearmed .../mods/mods_shared.c:1507"),
        C("white loading screen fix on", r"^\[rs-mods\] menu movie request again: on", src="rs-rearmed .../mods/mods_shared.c:1510"),
        C("white loading screen: no fallback", r"^\[rs-mods\] menu load: WARNING", forbid=True,
          src="rs-rearmed .../mods/mods_shared.c:880"),
        C("alternate arrangements fix on", r"^\[rs-mods\] Learn a Song alternate arrangements: on",
          src="rs-rearmed .../mods/mods_shared.c:1518"),
        C("auto save warning skipped", r"^\[rs-mods\] auto save warning skipped", src="rs-rearmed .../mods/mods.c:731"),
        C("DLC mounted", L.DLC_MOUNTED.pattern, groups={1: lambda n: int(n) > 0},
          src="rs-rearmed .../mods/mods_shared.c:544,547"),
        C("the profile read", r"^\[steam-shim\] FileRead\(\w+_PRFLDB, \d+\) -> [1-9]\d*",
          src="rs-rearmed shims/steam/src/interfaces.cpp:191, win-x86/shims/steam/steam32.c:336"),
    ]
    if ed == "mac":
        out += [C("co-op arrangement list on", r"^\[rs-mods\] co-op arrangement list: on",
                  src="rs-rearmed .../mods/mods_shared.c:1523"),
                C("ADV DISPLAY rows", r"^\[rs-mods\] menu options: (\d+) in Adv Display Settings, 8 of 8 vtable slots set",
                  groups={1: lambda n: int(n) >= 10}, src="rs-rearmed .../mods/menu_options.c:380")]
    return out


def menu_load_pairs(lines):
    """The white loading screen fix: every refused menu request is accepted later (mods_shared.c:813,815)."""
    rej = sum(1 for t in lines if re.search(r"^\[rs-mods\] menu load: '.*' was rejected", t))
    acc = sum(1 for t in lines if re.search(r"^\[rs-mods\] menu load: '.*' accepted after \d+ requests", t))
    return {"name": "white loading screen: refused menus accepted again", "status": L.PASS if acc >= rej else L.FAIL,
            "count": rej, "why": "" if acc >= rej else "%d refused, %d accepted after" % (rej, acc),
            "src": "rs-rearmed .../mods/mods_shared.c:813,815", "source": "game", "pattern": "menu load:"}


def seven_start_checks():
    return [
        C("Seven-String edits on", r"^\[rc\] edits seven_string: on \(\d+\)",
          src="rs-rearmed tools/ws7/recomp/runtime/rc_mid.c:38"),
        C("Seven-String on", r"^\[rsmods\] seven-string: on, \d+ mid points", src="rearmed-mods src/mod_seven_string.c:127,129"),
        C("Plain Packages on", r"^\[rsmods\] plain packages: on", src="rearmed-mods src/mod_plain_packages.c:90"),
        C("native charts on", r"^\[rsmods\] seven-string: native N-string charts: on", src="rearmed-mods src/seven_native.c:176"),
    ]


def scoped(checks, scope):
    for c in checks:
        c.scope = scope
    return checks


# ---------------------------------------------------------------------------------------------------------------
# Song checks

def playing_checks(key):
    return [C("the song plays", r"^\[rsmods\] Play_%s: playing id \d+" % re.escape(key), src="rearmed-mods src/rsmods.c:255")]


def position_checks():
    """The accuracy mod's line when it starts showing (once a song plays). It is logged when its state changes, and
    the state is kept across songs (mod_accuracy.c say()), so only a game's first song is sure to have it."""
    return [C("the song's position moves", r"^\[rsmods\] accuracy: \w+ \(LearnASong_Game, position ([\d.]+),",
              groups={1: lambda p: float(p) > 0.5}, src="rearmed-mods src/mod_accuracy.c:21")]


def heard_checks(midi=None):
    g = {1: str(midi)} if midi is not None else {1: lambda n: int(n) > 0}
    return [C("the test input on", r"^\[audio\] fake input: \d+ channel", needs=("fake_input",),
              src="rs-rearmed shims/coreaudio/rs_audio_fake.c:332,333").at("session"),
            C("the test input's channels usable", r"^\[audio\] fake input: .*silence in its place", forbid=True,
              needs=("fake_input",), src="rs-rearmed shims/coreaudio/rs_audio_fake.c:88,92,323,324").at("session"),
            C("the game reads the test input", r'^\[rs-audio\] guitar input "RS Re-ARMed Test Input": peak -\d',
              needs=("fake_input",), src="rs-rearmed shims/coreaudio/rs_audio_input.c:362 (RS_AUDIO_LOG=1)").at("session"),
            C("notes heard%s" % (" (MIDI %d)" % midi if midi else ""), r"^\[rsmods\] game note: (-?\d+) \(instrument",
              groups=g, needs=("fake_input",), src="rearmed-mods src/rsmods_game_note.c:54")]


def companion_song_checks(key):
    return [
        # Learn & Play's companion names the song by its file (lower case)
        C("companion: song loaded", r"(?i)^song %s loaded by the game from" % re.escape(key), source="companion",
          src="rs-mac-companion native/rscompanion.c:282"),
        C("companion: arrangement selected", r"^selected arrangement \S+", source="companion",
          src="rs-mac-companion native/rscompanion.c:1296"),
    ]


def freed_checks():
    return [C("the song freed on exit", r"^\[rsmods\] seven-string: chords: SNG 0x[0-9a-f]+ freed \(had",
              src="rearmed-mods src/seven_chords.c:835")]


def seven_song_checks(chart):
    n, tuning, low = chart["strings"], chart.get("tuning"), chart.get("low_midi")
    low_string = n - 1
    out = [
        C("native package accepted", r"^\[rsmods\] seven-string: native: package with the PSXR magic accepted",
          src="rearmed-mods src/seven_native.c:86").at("session"),
        C("manifests rewritten", r"^\[rsmods\] seven-string: native: manifest text of \d+ bytes: ([1-9]\d*) native "
          r"arrangement", src="rearmed-mods src/seven_native.c:123").at("session"),
        C("guitar built with %d strings" % n, r"^\[rsmods\] seven-string: strings: player 0's guitar rebuilt: %d strings" % n,
          src="rearmed-mods src/seven_strings.c:188"),
        C("tuner on string %d" % low_string + (" at MIDI %d" % low if low else ""),
          r"^\[rsmods\] seven-string: tuner: session 0x[0-9a-f]+ selects string %d, target MIDI (\d+)" % low_string,
          groups={1: str(low)} if low else None, src="rearmed-mods src/seven_tuner.c:474"),
        C("tuner's low range", r"^\[rsmods\] seven-string: tuner: low range: detection 0x[0-9a-f]+ lowest open note \d+ -> %s"
          % (low or r"\d+"), src="rearmed-mods src/seven_tuner.c:483"),
        C("SNXR converted (%d strings)" % n, r"^\[rsmods\] seven-string: native: SNXR body converted in place: guitar, %d strings" % n,
          src="rearmed-mods src/seven_native.c:149"),
        C("RS7X block read", r"^\[rsmods\] seven-string: chords: SNG 0x[0-9a-f]+: body \d+ bytes, .*RS7X v\d+ block \d+ "
          r"bytes: guitar, %d strings" % n, src="rearmed-mods src/seven_chords.c:713"),
        C("detection widened", r"^\[rsmods\] seven-string: pitch: song detection widened \(strings, lowest open note\) "
          r"\(%d, %s\)" % (n, low or r"\d+"), src="rearmed-mods src/seven_pitch.c:570"),
        C("headstock model shown", r"^\[rsmods\] seven-string: headstock: guitar 0x[0-9a-f]+ shows the %d-string headstock "
          r"model" % n, src="rearmed-mods src/seven_headstock.c:1092"),
        C("chord frames", r"^\[rsmods\] seven-string: chord drawing: chord frame up to string \d+: string axis scale",
          src="rearmed-mods src/seven_chdraw.c:147",
          optional="logged when a chord's frame is built: the part played may have had no chord"),
    ]
    if tuning:
        out.append(C("tuning named %s" % tuning, r"^\[rsmods\] seven-string: names: .* -> \"%s\"" % re.escape(tuning),
                     src="rearmed-mods src/seven_names.c:414,451,477"))
    return out + freed_checks()


# ---------------------------------------------------------------------------------------------------------------
# Routes (ctx: the case's context, run.py's CaseRun: ctx.d the driver, ctx.chart(name), ctx.until(names, s), ...)

def r_boot(ctx):
    """The start itself is the route (run.py drives it before the first case): here only the main menu."""
    R.main_menu_ready(ctx.d)


def song_route(key_of, search_of, hold=25, arrangement=None, heard=False):
    def route(ctx):
        d = ctx.d
        key, search = key_of(ctx), search_of(ctx)

        def at_tuner(d):
            ctx.snap("tuner")
            if heard and "fake_input" not in ctx.missing:
                ctx.until(["notes heard"], 10)

        R.learn_song(d, key, search, tuner=at_tuner, arrangement=arrangement(ctx) if arrangement else None)
        ctx.until(ctx.in_song_checks(), hold)
        ctx.snap("highway")
        R.exit_song(d)
        R.home(d)
    return route


def stock_key(ctx):
    return ctx.stock()["key"]


def stock_search(ctx):
    return ctx.stock()["search"]


def r_pause_menu(ctx):
    d = ctx.d
    R.learn_song(d, stock_key(ctx), stock_search(ctx))
    ctx.until(["the song plays"], 20)
    # pause, RESUME
    R.pause(d)
    R.pause_menu_top(d)
    d.press("return", HIGHWAY, settle=0.3, retry=3, tries=2)
    # pause, RESTART (the second entry)
    R.pause(d)
    R.pause_menu_top(d)
    d.key("down")
    mark = d.mark()
    d.press("return", lambda: d.screen != PAUSE, settle=0.3, retry=3, tries=2)
    d.wait_screen(HIGHWAY, 30, what="the song after RESTART")
    ctx.until(["restarted"], 15)
    # pause, Riff Repeater (Space), the speed key (RRSpeedKey R with RRSpeedAboveOneHundred), back
    R.pause(d)
    d.press("space", RIFF, settle=0.5, retry=3, tries=3)
    ctx.snap("riff_repeater")
    d.poll(1.0)
    d.key("r")
    ctx.until(["riff repeater speed"], 5)
    for k in ("down", "down", "left"):  # SPEED, one step down
        d.key(k)
        d.poll(0.3)
    d.press("fwddelete" if ctx.edition == "lp" else "delete", lambda: d.screen != RIFF, settle=0.5, retry=3, tries=2)
    R.exit_song(d)
    R.home(d)


def r_exit_save(ctx):
    """EXIT GAME from the main menu (the profile saved), and a start again (run.py: ctx.restart())."""
    d = ctx.d
    R.exit_game(d)
    ctx.wait_exit(20)
    ctx.restart()


def exit_save_extra(ctx):
    """The profile as the game last wrote it in the first run is what the next start reads: the same file, the same
    size (the game writes it when something changed: leaving a song, a setting; at EXIT GAME only if still unsaved)."""
    first, second = ctx.lines("first_game"), ctx.lines("restart")
    wrote = [m for m in (re.search(r"^\[steam-shim\] FileWrite\((\w+_PRFLDB), (\d+)\) -> 1", t) for t in first) if m]
    read = [m for m in (re.search(r"^\[steam-shim\] FileRead\((\w+_PRFLDB), (\d+)\) -> (\d+)", t)
                        for t in second or []) if m]
    r = {"name": "the profile last saved is the one read at the next start", "source": "game",
         "src": "rs-rearmed shims/steam/src/interfaces.cpp:184,191, win-x86/shims/steam/steam32.c:329,336",
         "pattern": "FileWrite/FileRead(*_PRFLDB)"}
    if second is None:
        r.update(status=L.FAIL, why="the game did not start again")
    elif not read:
        r.update(status=L.FAIL, why="no FileRead of a profile at the next start")
    elif not wrote:
        r.update(status=L.SKIP, why="the first run wrote no profile (nothing changed in it)")
    else:
        w, rd = wrote[-1], read[-1]
        same = w.group(1) == rd.group(1) and w.group(2) == rd.group(3)
        r.update(status=L.PASS if same else L.FAIL,
                 why="%s, %s bytes" % (w.group(1), w.group(2)) if same else "wrote %s (%s bytes), read %s (%s bytes)" % (
                     w.group(1), w.group(2), rd.group(1), rd.group(3)))
    q = {"name": "a clean quit", "source": "game", "src": "the game's exit status", "pattern": ""}
    q.update(status=L.PASS if ctx.quit_status == 0 else L.FAIL, why="exit status %s (%s)" % (ctx.quit_status,
                                                                                          ctx.quit_how))
    return [r, q]


def r_chart_song(chart_name, hold=25):
    def route(ctx):
        ch = ctx.chart(chart_name)
        song_route(lambda c: ch["key"], lambda c: ch["search"], hold=hold)(ctx)
    return route


def r_songbridge(ctx):
    ch = ctx.chart(ctx.case.charts_other[0])
    song_route(lambda c: ch["key"], lambda c: ch["search"], hold=15)(ctx)


def r_companion(ctx):
    song_route(stock_key, stock_search, hold=20)(ctx)


def main_menu_entry(d, downs):
    """The main menu's entry `downs` below LEARN A SONG: Up to the top first (the menu keeps its cursor), keys a
    moment apart (the main menu drops keys sent quickly: docs/mac/TESTING.md)."""
    R.main_menu_ready(d)
    R.main_menu_top(d)
    for _ in range(downs):
        d.key("down")
        d.poll(0.5)


def r_guitarcade(ctx):
    """GUITARCADE (Down five), TECHNIQUE GAMES, the second game, its tuner: Skip Tuner (the built-in fix), Return on
    the warning, the game (docs/mac/TESTING.md, The other modes)."""
    d = ctx.d
    main_menu_entry(d, 5)
    d.press("return", lambda: d.screen != MAIN, settle=0.5, retry=5, tries=2)
    d.poll(1.5)
    d.key("return")  # TECHNIQUE GAMES: the list of games (the screen's name stays: no menu: line to wait for)
    d.poll(2.0)
    d.key("down")
    d.poll(0.5)
    d.press("return", "Guitarcade_Tuner", settle=0.5, retry=6, tries=2, timeout=60)
    ctx.snap("guitarcade_tuner")
    d.press("fwddelete", "SelectionListDialog", settle=2.0, retry=5, tries=3)
    d.press("return", ("Guitarcade_Game", "Guitarcade_Tuner"), settle=1.0, retry=6, tries=2)
    d.wait_screen("Guitarcade_Game", 40, what="the Guitarcade game after Skip Tuner")
    ctx.snap("guitarcade_game")
    R.home(d)


def checks_guitarcade(ctx):
    return [C("Guitarcade's tuner", r"^\[rsmods\] menu: Guitarcade_Tuner", src="rearmed-mods src/rsmods.c:327"),
            C("Skip Tuner's question", r"^\[rsmods\] menu: SelectionListDialog", src="rearmed-mods src/rsmods.c:327"),
            C("the game started without tuning", r"^\[rsmods\] menu: Guitarcade_Game", src="rearmed-mods src/rsmods.c:327")]


def r_tools(ctx):
    """Space on the main menu (Tools), OPTIONS (Down three), ADV DISPLAY SETTINGS (Down three), RS Re-ARMed's first
    row (after the game's six), Right and Left (back as it was), out by Esc (docs/*/TESTING.md, ADV DISPLAY
    SETTINGS)."""
    d = ctx.d
    R.main_menu_ready(d)
    d.press("space", lambda: d.screen != MAIN, settle=1.0, retry=4, tries=3)  # Tools (MainMenu_Pause)
    d.poll(1.0)
    for k in ["up"] * 9 + ["down"] * 3:  # the list keeps its cursor: from the top, OPTIONS is the fourth
        d.key(k)
        d.poll(0.3)
    d.press("return", "OptionsMenu", settle=0.5, retry=4, tries=2)
    d.poll(1.0)
    for k in ["up"] * 9 + ["down"] * 3:
        d.key(k)
        d.poll(0.3)
    d.press("return", "AdvancedDisplaySettings", settle=1.0, retry=4, tries=2)
    ctx.snap("adv_display")
    d.poll(1.0)
    # RS Re-ARMed's first row, FRAME RATE: after the game's six rows in Learn & Play, four on the Mac
    for k in ["up"] * 9 + ["down"] * (6 if ctx.edition == "lp" else 4):
        d.key(k)
        d.poll(0.3)
    d.press("return", "EditSetting", settle=0.5, retry=4, tries=2)
    back = "delete" if ctx.edition == "mac" else "esc"  # Edit Setting's way back (docs/*/TESTING.md)

    def change(k):
        """k on the open row -> whether a menu options: line came (at once, or when Edit Setting closed)."""
        if d.screen != "EditSetting":
            d.press("return", "EditSetting", settle=0.5, retry=4, tries=2)
        d.hold(max(0.0, 1.5 - (time.time() - d.screen_at)))  # a key while Edit Setting moves in is lost
        mark = d.mark()
        d.key(k)
        if d.hold(3, until=lambda: d.find(r"^\[rs-mods\] menu options: ", mark) is not None):
            return True
        d.press(back, "AdvancedDisplaySettings", settle=0.3, retry=3, tries=2)  # a row that takes it on closing
        return d.hold(4, until=lambda: d.find(r"^\[rs-mods\] menu options: ", mark) is not None)
    # a slider at one end does not move one way: the other way first then, and back
    for there, back_again in (("right", "left"), ("left", "right")):
        if change(there):
            ctx.snap("adv_display_changed")
            if not change(back_again):
                raise Stuck("the row did not go back by %s" % back_again, d.screen)
            break
    else:
        raise Stuck("no menu options: line for Right or Left on the row", d.screen)
    if d.screen == "EditSetting":
        d.press(back, "AdvancedDisplaySettings", settle=0.3, retry=3, tries=2)
    R.home(d)


def checks_tools(ctx):
    out = [C("Options", r"^\[rsmods\] menu: OptionsMenu", src="rearmed-mods src/rsmods.c:327"),
           C("ADV DISPLAY SETTINGS", r"^\[rsmods\] menu: AdvancedDisplaySettings", src="rearmed-mods src/rsmods.c:327"),
           C("a row opened", r"^\[rsmods\] menu: EditSetting", src="rearmed-mods src/rsmods.c:327"),
           C("the row changed and back", r"^\[rs-mods\] menu options: ", min=2,
             src="rs-rearmed tools/ws7/recomp/overrides/mods/menu_options_common.c:223")]
    if ctx.edition == "lp":
        out.append(C("RS Re-ARMed's rows", r"^\[rs-mods\] menu options: (\d+) in Adv Display Settings",
                     groups={1: lambda n: int(n) >= 10}, src="rs-rearmed win-x86/shims/win32_menu_options.c:242")
                   .at("session"))
    return out


def r_bass(ctx):
    song_route(stock_key, stock_search, hold=20, arrangement=lambda c: R.pick_bass)(ctx)


def checks_bass(ctx):
    k = stock_key(ctx)
    return playing_checks(k) + freed_checks() + [
        C("the bass's tuner (4 strings)", r"^\[rsmods\] seven-string: tuner: session 0x[0-9a-f]+: 4 strings",
          src="rearmed-mods src/seven_tuner.c:122"),
        C("companion: the bass arrangement", r"(?i)^selected arrangement \S*bass", source="companion",
          src="rs-mac-companion native/rscompanion.c:1296")] + heard_checks()


def r_score_attack(ctx):
    """The song's page, Score Attack (its last entry), EASY, Skip Tuner, play, pause, EXIT SONG (the second entry of
    Score Attack's pause menu), out."""
    d = ctx.d
    R.open_song(d, stock_key(ctx), stock_search(ctx))
    R.page_bottom(d)
    d.press("return", "ScoreAttack_SongOptions", settle=0.3, retry=5, tries=2)
    d.press("return", ("ScoreAttack_PreSongTuner", "SelectionListDialog"), settle=1.5, retry=8, tries=2,
            started=R.PLAY_STARTED, started_wait=90)
    if d.screen == "ScoreAttack_PreSongTuner":
        d.press("fwddelete", "SelectionListDialog", settle=3.0, retry=6, tries=3)
    d.press("return", lambda: d.screen != "SelectionListDialog", settle=1.0, retry=9, tries=2)
    d.wait_screen("ScoreAttack_Game", 60, what="Score Attack's song")
    ctx.until(["Score Attack's note counts", "Score Attack accuracy"], 20)
    ctx.snap("score_attack")
    d.press("esc", "ScoreAttack_Pause", settle=1.0, retry=3, tries=3)
    d.poll(0.5)
    R.pause_menu_top(d)
    d.key("down")
    d.poll(0.3)
    d.press("return", "ScoreAttack_SongOptions", settle=0.3, retry=5, tries=2)
    R.home(d)


def checks_score_attack(ctx):
    k = stock_key(ctx)
    # Learn & Play reads Score Attack's counts by the UI's path, and says where once (rsmods_note_stats.c:170-192); the
    # Mac has no such path (its ui_note_stats_0 offset is not on mac-x64) and reads them another way, with no line
    counts = [C("Score Attack's note counts", r"^\[rsmods\] note counts: Score Attack's at 0x",
                src="rearmed-mods src/rsmods_note_stats.c:192 (once a game)").at("session")] if ctx.edition == "lp" else []
    return playing_checks(k) + freed_checks() + counts + [
        C("Score Attack's screens", r"^\[rsmods\] menu: ScoreAttack_Game", src="rearmed-mods src/rsmods.c:327"),
        C("Score Attack accuracy", r"^\[rsmods\] accuracy: \w+ \(ScoreAttack_Game, position", optional=
          "logged when the accuracy's state changes (kept across songs)", src="rearmed-mods src/mod_accuracy.c:21")]


def r_alternates(ctx):
    """The made-up song with seven arrangements: the page's Downs go past the fourth alternate (the window scrolls,
    rs-rearmed's fix), to Score Attack; Up is the last alternate (the Bass, the seventh arrangement), Return plays it;
    then out."""
    ch = ctx.chart("alternates")
    d = ctx.d
    R.open_song(d, ch["key"], ch["search"])
    ctx.snap("page")
    R.page_bottom(d)
    ctx.until(["the list scrolls down"], 3)
    ctx.snap("page_scrolled")
    d.key("up")
    d.poll(0.3)
    R.play_from_page(d)
    ctx.until(["the song plays"], 20)
    ctx.snap("highway")
    R.exit_song(d)
    # the page again: from the top (the window back at 0)
    R.home(d)


def checks_alternates(ctx):
    ch = ctx.chart("alternates")
    n = len([a for a in (ch.get("entry") or {}).get("arrangements", []) if a.get("instrument") != "vocals"]) or 7
    mods = "rs-rearmed tools/ws7/recomp/overrides/mods/mods_shared.c"
    return playing_checks(ch["key"]) + freed_checks() + [
        C("the fix says it scrolls", r"^\[rs-mods\] Learn a Song: a song with more than five arrangements shows four "
          r"alternates at a time", src=mods + ":1156").at("session"),
        C("the list scrolls down", r"^\[rs-mods\] Learn a Song: Down, window (\d+) -> (\d+) of %d alternates" % (n - 1),
          groups={2: lambda x: int(x) >= 1}, src=mods + ":1122"),
        C("the last arrangement (the Bass) played", r"^\[rsmods\] seven-string: tuner: session 0x[0-9a-f]+: 4 strings",
          src="rearmed-mods src/seven_tuner.c:122"),
    ]


# The basic mods in a song: each mod's switch on (read live), its key, and the line it logs (rearmed-mods src/)
BASIC_MODS = [
    # (mod, ini keys, key in the song, check name, pattern, src)
    ("Song Timer", {("Toggle Switches", "ShowSongTimer"): "on", ("Toggle Switches", "ShowSongTimerWhen"): "manual"},
     "i", "song timer", r"^\[rsmods\] song timer (shown|hidden)", "mod_song_timer.c:29"),
    ("Lyrics", {("Toggle Switches", "Lyrics"): "on", ("Toggle Switches", "RemoveLyricsWhen"): "manual"},
     "l", "lyrics", r"^\[rsmods\] lyrics (hidden|shown)", "mod_lyrics.c:27"),
    ("Volume Control", {("Toggle Switches", "VolumeControl"): "on"},
     "5", "volume", r"^\[rsmods\] volume: \S+ \d+", "mod_volume.c:117"),
    ("Mute", {}, ("x", "x"), "mute player 1", r"^\[rsmods\] volume: player 1 (muted|unmuted)", "mod_volume.c:84"),
    ("Sunlight", {("Toggle Switches", "Sunlight"): "on"}, "g", "sunlight", r"^\[rsmods\] sunlight: ",
     "mod_sunlight.c:156,164,175"),
    ("Looping", {("Toggle Switches", "AllowLooping"): "on"}, "y", "looping start", r"^\[rsmods\] looping: start",
     "mod_looping.c:73"),
    ("Rewind", {("Toggle Switches", "AllowRewind"): "on"}, "z", "rewind", r"^\[rsmods\] rewind: [\d.]+ s ->",
     "mod_looping.c:98"),
    ("Camera Director", {("Toggle Switches", "CameraDirector"): "on"}, "k", "camera", r"^\[rsmods\] camera: zoom",
     "mod_camera.c:147"),
    ("Nashville Numbers", {("Toggle Switches", "NashvilleNumbers"): "on"}, None, "nashville numbers",
     r"^\[rsmods\] nashville numbers: \S+: \d+ arrangements", "mod_nashville.c:135"),
]
# Mods with no line of their own to check: listed in the report as unchecked
UNCHECKED_MODS = ["Fretless/Inlays/Lane Markers (mod_fretboard.c)", "Headstock (mod_headstock.c)",
                  "Skyline (mod_skyline.c)", "Screenshot Scores (mod_screenshot_scores.c)",
                  "colors (mod_colors.c: lines only when its render API is missing)"]


def mods_ini():
    out = {}
    for m in BASIC_MODS:
        out.update(m[1])
    return out


def r_mods(ctx):
    """A song with the basic mods on: each one's key once the song plays, its line."""
    d = ctx.d
    R.learn_song(d, stock_key(ctx), stock_search(ctx))
    ctx.until(["the song plays"], 20)
    d.poll(2.0)
    for mod, _, key, name, _, _ in BASIC_MODS:
        if not key:
            continue
        if d.screen != "LearnASong_Game":
            raise Stuck("left the song while the mods' keys were pressed (on %s)" % d.screen, d.screen)
        for k in (key,) if isinstance(key, str) else key:  # a toggle twice: as it was
            d.key(k)
            ctx.until([name], 3)
    ctx.snap("mods")
    R.exit_song(d)
    R.home(d)


def checks_mods(ctx):
    out = playing_checks(stock_key(ctx))
    for mod, _, _, name, pat, src in BASIC_MODS:
        out.append(C(name, pat, src="rearmed-mods src/" + src))
    return out


def mods_extra(ctx):
    return [{"name": "unchecked (no line of their own): " + ", ".join(UNCHECKED_MODS), "status": L.SKIP,
             "why": "no log line", "source": "game", "src": "", "pattern": ""}]


def r_song_lists(ctx):
    """The song list with Custom Song List Titles and the previews switch changed (read live), SORT BY (Tab) and back,
    a few rows down (previews play), out."""
    d = ctx.d
    R.open_song_list(d)
    ctx.until(["song list titles on", "song previews switch"], 5)
    for _ in range(3):
        d.key("down")
        d.poll(0.8)
    d.key("tab")
    d.poll(1.5)
    ctx.snap("sort_by")
    d.key("esc")
    d.poll(1.0)
    R.home(d)


def checks_song_lists(ctx):
    return [C("song list titles on", r"^\[rsmods\] song list titles on", src="rearmed-mods src/mod_song_list_titles.c:41"),
            C("song previews switch", r"^\[rsmods\] song previews (on|off)", src="rearmed-mods src/mod_song_previews.c:13"),
            C("songs highlighted", r"^\[rsmods\] song: \S+", min=2, src="rearmed-mods src/rsmods.c:336")]


def r_hearing(ctx):
    """Nickelback's 7-string Lead with the test input on its low B (B1, MIDI 35): the tuner and the song hear it."""
    ch = ctx.chart("nickelback")
    d = ctx.d

    def at_tuner(d):
        ctx.until(["notes heard (MIDI %d)" % ch["low_midi"]], 15)
        ctx.snap("tuner")
    R.learn_song(d, ch["key"], ch["search"], tuner=at_tuner)
    ctx.until(["the song plays"], 15)
    ctx.snap("highway")
    R.exit_song(d)
    R.home(d)


def checks_hearing(ctx):
    ch = ctx.chart("nickelback")
    return heard_checks(ch["low_midi"]) + playing_checks(ch["key"]) + [
        C("the tuner's low range", r"^\[rsmods\] seven-string: tuner: low range: detection 0x[0-9a-f]+ lowest open note "
          r"\d+ -> %d" % ch["low_midi"], src="rearmed-mods src/seven_tuner.c:483")]


def r_bass5(ctx):
    """Nickelback's 5-string Bass (the page's last other arrangement), then Jinjer's (its only arrangement: Play
    Song), each to the highway and out."""
    d = ctx.d
    nb, jj = ctx.chart("nickelback"), ctx.chart("jinjer")
    R.learn_song(d, nb["key"], nb["search"], tuner=lambda d: ctx.snap("nickelback_tuner"), arrangement=R.pick_bass)
    ctx.until(["the song plays"], 15)
    ctx.snap("nickelback_bass")
    R.exit_song(d)
    R.home(d)
    R.learn_song(d, jj["key"], jj["search"], tuner=lambda d: ctx.snap("jinjer_tuner"))
    ctx.until(["SNXR converted as a bass (5 strings)"], 20)
    ctx.snap("jinjer_bass")
    R.exit_song(d)
    R.home(d)


def checks_bass5(ctx):
    nb, jj = ctx.chart("nickelback"), ctx.chart("jinjer")
    nbb = [a for a in (nb.get("entry") or {}).get("arrangements", []) if a.get("instrument") == "bass"]
    names = [a.get("tuning_name") for a in nbb[:1]] + [jj.get("tuning")]
    out = playing_checks(nb["key"]) + playing_checks(jj["key"]) + freed_checks() + [
        C("bass tuning stored with 5 strings", r"^\[rsmods\] seven-string: strings: target tuning stored \(player 0, "
          r"song selection\): \d+ strings \(bass 5\)", min=2, src="rearmed-mods src/seven_strings.c:372"),
        C("the bass's tuner with 5 strings", r"^\[rsmods\] seven-string: tuner: session 0x[0-9a-f]+: 5 strings", min=2,
          src="rearmed-mods src/seven_tuner.c:122"),
        C("SNXR converted as a bass (5 strings)", r"^\[rsmods\] seven-string: native: SNXR body converted in place: bass, "
          r"5 strings", min=2, src="rearmed-mods src/seven_native.c:149"),
        # a bass's detection: the bass part names the song's strings, and the floor goes below its low string
        C("the bass's detection: 5-string extended bass", r"^\[rsmods\] seven-string: bass: song 0x[0-9a-f]+ on detector "
          r"0x[0-9a-f]+: 5-string extended bass", min=2, src="rearmed-mods src/seven_bass.c:59"),
        C("the bass's detection floor below its low string", r"^\[rsmods\] seven-string: pitch: detection 0x[0-9a-f]+ "
          r"floor \d+ -> \d+ \(lowest open (21|23),", src="rearmed-mods src/seven_pitch.c:276"),
        C("the bass's tuner on its low string", r"^\[rsmods\] seven-string: tuner: session 0x[0-9a-f]+ selects string 4, "
          r"target MIDI (21|23)", min=2, src="rearmed-mods src/seven_tuner.c:474"),
    ]
    for n in names:
        if n:
            out.append(C("tuning named %s" % n, r"^\[rsmods\] seven-string: names: .* -> \"%s\"" % re.escape(n),
                         src="rearmed-mods src/seven_names.c:414,451,477"))
    return out


# ---------------------------------------------------------------------------------------------------------------
# The cases

def checks_boot(ctx):
    return scoped(start_checks(ctx.edition), "session")


def checks_song(ctx, key=None):
    key = key or stock_key(ctx)
    # the test input's first channel: E2 (tone:39.88, which the tuner reads in tune: run.py FAKE_INPUT)
    return playing_checks(key) + position_checks() + heard_checks(40) + freed_checks() + scoped(
        [C("the highway reached", r"^\[rsmods\] menu: LearnASong_Game", src="rearmed-mods src/rsmods.c:327")], "case")


def checks_pause_menu(ctx):
    k = stock_key(ctx)
    return [
        C("pause menu each time", r"^\[rsmods\] menu: LearnASong_Pause", min=3, src="rearmed-mods src/rsmods.c:327"),
        C("Riff Repeater reached", r"^\[rsmods\] menu: LearnASong_RiffRepeater", src="rearmed-mods src/rsmods.c:327"),
        C("the song plays", r"^\[rsmods\] Play_%s: playing id \d+" % k, src="rearmed-mods src/rsmods.c:255"),
        C("restarted", r"^\[rsmods\] Play_%s: playing id \d+" % k, min=2, src="rearmed-mods src/rsmods.c:255"),
        # once a game a song (its node is cached): the game's log
        C("riff repeater's song node", r"^\[rsmods\] riff repeater: Play_%s -> node 0x" % k,
          src="rearmed-mods src/mod_riff_repeater.c:84").at("session"),
        C("riff repeater speed", r"^\[rsmods\] riff repeater: song speed [\d.]+%", src="rearmed-mods src/mod_riff_repeater.c:142"),
        C("riff repeater speed reset on leaving", r"^\[rsmods\] riff repeater: speed reset to 100%",
          src="rearmed-mods src/mod_riff_repeater.c:129"),
    ] + freed_checks()


def checks_exit_save(ctx):
    return [C("EXIT GAME? asked", r"^\[rsmods\] menu: SelectionListDialog", min=2, src="rearmed-mods src/rsmods.c:327")]


def checks_songbridge(ctx):
    ch = ctx.chart(ctx.case.charts_other[0])
    other = "_p" if ctx.edition == "mac" else "_m"
    return [
        C("Song Bridge serves the other platform's form", r"^\[songbridge\] .*: (\d+) %s\.psarc archives and feedpaks "
          r"of \d+ served as" % other, groups={1: lambda n: int(n) >= len(ctx.case.charts_other)},
          src="rearmed-songbridge src/songbridge.c:404").at("session"),
        C("no archive not served", r"^\[songbridge\] .* is not served: ", forbid=True,
          src="rearmed-songbridge src/songbridge.c:208").at("session"),
        C("the song's archive opened through it", r"^\[songbridge\] open .* \(from .*gametest", src="rearmed-songbridge "
          "src/songbridge.c:468 (RS_SONGBRIDGE_LOG=1)"),
    ] + playing_checks(ch["key"])


def checks_companion(ctx):
    k = stock_key(ctx)
    return companion_song_checks(k) + [
        C("companion: song event", r'(?i)"event": "song".*"song_key": "%s"' % k, source="events",
          src="rs-mac-companion native/rscore.c:352 (song event)"),
        C("companion: tone event", r'"event": "tone"', source="events", src="rs-mac-companion native/rscore.c:330"),
        C("companion: state with the song", r'(?i)"event": "state".*"song_key": "%s"' % k, source="events",
          src="rs-mac-companion native/rscore.c:365"),
    ] + playing_checks(k)


def checks_chart(chart_name, extra=()):
    def f(ctx):
        ch = ctx.chart(chart_name)
        return scoped(seven_start_checks(), "session") + seven_song_checks(ch) + playing_checks(ch["key"]) + \
            companion_song_checks(ch["key"]) + list(extra)
    return f


def checks_line_off(ctx):
    ch = ctx.chart("nickelback")
    # string7_line = 0: the section mask's edits (group seven_line) stay off, read at the start
    # (rearmed-mods src/seven_chdraw.c:299-300 seven_edits_after; the runtime logs a group it turns on, rc_mid.c:38)
    return seven_song_checks(ch) + [
        C("the added strings' lines off (no seven_line edits)", r"^\[rc\] edits seven_line: on", forbid=True,
          src="rearmed-mods src/seven_chdraw.c:300, rs-rearmed tools/ws7/recomp/runtime/rc_mid.c:38").at("session")]


CASES = [
    Case("boot", ["smoke", "regression"], "start, intro skipped, title, profile, main menu", route=r_boot,
         checks=checks_boot, extra=lambda ctx: [menu_load_pairs(ctx.lines("session"))] + ctx.start_timing()),
    Case("song", ["smoke", "regression"], "Learn a Song, a stock 6-string song, Lead, the tuner, play, out",
         route=song_route(stock_key, stock_search, hold=25, heard=True), checks=checks_song),
    Case("bass", ["smoke", "regression"], "the same for the stock song's Bass arrangement (the page's other "
         "arrangements)", route=r_bass, checks=checks_bass),
    Case("pause_menu", ["smoke", "regression"], "in a song: pause, Resume, pause, Restart, pause, Riff Repeater "
         "(speed), back, Exit", route=r_pause_menu, checks=checks_pause_menu,
         ini={("Toggle Switches", "RRSpeedAboveOneHundred"): "on"}),
    Case("songbridge", ["smoke", "regression", "extended"], "the other platform's form of a native chart in dlc: "
         "served, listed, played", route=r_songbridge, checks=checks_songbridge,
         charts_other=("stock_cdlc", "fearfactory")),
    Case("companion", ["smoke", "regression"], "a song: the companion's log and events", route=r_companion,
         checks=checks_companion),
    Case("guitar7", ["smoke", "regression", "extended"], "Nickelback (7-string Lead): Learn a Song, the tuner, play, "
         "pause, exit", route=r_chart_song("nickelback", hold=45), checks=checks_chart("nickelback"),
         charts=("nickelback",)),
    Case("exit_save", ["smoke", "regression"], "EXIT GAME from the main menu, start again: the profile saved and read",
         route=r_exit_save, checks=checks_exit_save, extra=exit_save_extra, ends_game=True),
    # regression and extended
    Case("guitar8", ["regression", "extended"], "Fear Factory (8-string Lead): as guitar7",
         route=r_chart_song("fearfactory", hold=45), checks=checks_chart("fearfactory"), charts=("fearfactory",)),
    Case("bass5", ["regression", "extended"], "Nickelback's and Jinjer's 5-string Bass: names, the bass built with 5 "
         "strings, the tuner, SNXR, detection", route=r_bass5, checks=checks_bass5, charts=("jinjer", "nickelback")),
    Case("line_off", ["regression", "extended"], "Nickelback with string7_line = 0 (read at the start)",
         route=r_chart_song("nickelback", hold=20), checks=checks_line_off, charts=("nickelback",),
         start_ini={("Seven String", "string7_line"): "0"}),
    Case("alternates", ["regression", "extended"], "the made-up package with 7 arrangements: the list scrolls, the "
         "last one plays", route=r_alternates, checks=checks_alternates, charts=("alternates",)),
    Case("coop", ["regression", "extended"], "Multiplayer with two profiles on the fake input's channels",
         charts=("nickelback",), needs=("fake_input", "two_profiles", "coop_route"),
         manual="Multiplayer, the second profile, Learn a Song, P1 7-string Lead, P2 5-string Bass, 20 s, pause, exit"),
    Case("hearing", ["regression", "extended"], "Nickelback's tuner and song with the test input on the low B (MIDI 35)",
         route=r_hearing, checks=checks_hearing, charts=("nickelback",), needs=("fake_input",),
         env={"RS_AUDIO_FAKE_INPUT": "tone:35,silence"}),
    Case("score_attack", ["regression"], "Score Attack (the song page's last entry), EASY, Skip Tuner, play, exit",
         route=r_score_attack, checks=checks_score_attack),
    Case("guitarcade", ["regression"], "Guitarcade, a game, Skip Tuner (the built-in fix)", route=r_guitarcade,
         checks=checks_guitarcade),
    Case("tools", ["regression"], "Tools, Options, ADV DISPLAY SETTINGS: RS Re-ARMed's first row changed and back",
         route=r_tools, checks=checks_tools),
    Case("song_lists", ["regression"], "the song list with Song List Titles and the previews switch changed, SORT BY",
         route=r_song_lists, checks=checks_song_lists,
         ini={("Toggle Switches", "CustomSongListTitles"): "on", ("Toggle Switches", "SongPreviews"): "on"}),
    Case("tonehost", ["regression"], "Tone Host on with the fake input", needs=("fake_input", "tonehost_plugin"),
         start_ini={("Toggle Switches", "ToneHost"): "on"},
         manual="a song with Tone Host on: the host starts, the song's tones reach it, it stops on exit"),
    Case("mods", ["regression"], "the basic mods' settings on during a song: each one's key and line",
         route=r_mods, checks=checks_mods, ini=mods_ini(), extra=mods_extra),
]

SUITES = ("smoke", "regression", "extended", "all")
BY_NAME = {c.name: c for c in CASES}


def select(suite=None, names=None):
    if names:
        unknown = [n for n in names if n not in BY_NAME]
        if unknown:
            raise KeyError("no case %s (the cases: %s)" % (", ".join(unknown), ", ".join(BY_NAME)))
        return [BY_NAME[n] for n in names]
    suite = suite or "smoke"
    return [c for c in CASES if suite == "all" or suite in c.suites]


# The extended charts' facts where fixtures.json lacks them: the low string's open note in MIDI (B1 35, F#1 30;
# the bass's low B0 23, Jinjer's low B down a tone A0 21) and the tuning's name as the game shows it
CHART_DEFAULTS = {
    "nickelback": {"words": ("nickelback",), "strings": 7, "tuning": "B STANDARD 7STR", "low_midi": 35,
                    "key": "DellNickelbackBurnItToTheGround7StringV23", "search": "burn"},
    "fearfactory": {"words": ("fear factory", "fearfactory", "soul hacker", "soulhacker"), "strings": 8,
                     "tuning": "F♯ STANDARD 8STR", "low_midi": 30,
                     "key": "DellFearFactorySoulHacker8StringT2", "search": "soul"},
    "jinjer": {"words": ("jinjer",), "strings": 5, "low_midi": 21, "instrument": "bass", "search": "jinj"},
    "alternates": {"words": ("alternate", "arrangements"), "strings": 7},
}
