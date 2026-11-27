# Game tests: unattended runs of both editions

A repeatable test of the whole stack in the game: RS Re-ARMed (the Mac edition and Learn & Play), rearmed-mods, Song
Bridge and the companion (`librscompanion.dylib`), on test installs that never touch the player's own game, saves or
settings. Each case is a scripted route through the game's menus, driven by keys, with the game's own log, the
companion's log and snapshots collected, and checks run over them afterwards. The same cases run on both editions.

Run it with the player's screen free: the game takes keys only while its window has the focus, and pauses when it
loses it.

    tests/game/fixtures.py              the test charts, both platforms' forms, into <root>/fixtures
    tests/game/setup.py [--edition E]   the test installs (made once, refreshed from the builds each run)
    tests/game/run.py [--edition mac|lp|both] [--case NAME ...] [--builds main|installed]
                                        the cases; a report in <root>/runs/<stamp>/report.md

## Where things are (the contract between the three scripts)

`RS_GAMETEST_ROOT`, default `~/rs-gametest` (the internal drive: the Mac test install is APFS clones of the Mac game,
which take no room only on the game's own volume; both drives are nearly full, so nothing here copies a game).

    <root>/fixtures/pc/*.psarc          the charts as Learn & Play reads them (_p)
    <root>/fixtures/mac/*.psarc         the same, as the Mac reads them (_m)
    <root>/fixtures/fixtures.json       per chart: file names, song key, arrangements (name, instrument, strings,
                                        tuning as the game names it, e.g. "B STANDARD 7STR"), what it is for
    <root>/mac/home/                    the Mac test install (RS_NATIVE_HOME): game/ (clones), steam/, mods/, logs/
    <root>/lp/                          the Learn & Play test folder: game/ (clones or links of the player's copy),
                                        userdata/, steam/, mods/, out/ (the build's rs2014_lp, lp_game.exe, dlls,
                                        libnetguard.dylib)
    <root>/tools/libkeyfifo.dylib       rs-rearmed's tools/ws8/keyfifo, built
    <root>/builds.json                  what setup.py put in place: each part's source (repo, commit, path)
    <root>/runs/<stamp>/<edition>/<case>/
        game.log, companion.log, keyfifo.log, snap/*.png, RSMods.ini (as run), result.json
    <root>/runs/<stamp>/report.md       per edition and case: PASS / FAIL / MANUAL, each check, the snapshots to look at

Each case's charts go in `game/dlc/gametest/` (emptied before every case), in the edition's own form unless the case
says otherwise (Song Bridge's cases put the other platform's form there).

## Parts under test and where they come from

| Part | `--builds main` (default) | `--builds installed` |
|---|---|---|
| Mac game | `~/src/rs-rearmed/build/m4/app/Rocksmith2014.app` (+ `rsmsl-corpus.bin`, icon, `en.lproj` from the player's app) | the player's `~/Library/Application Support/RS Re-ARMed/game/Rocksmith2014.app` |
| Learn & Play | `~/src/rs-rearmed/build/win-x86/lp/out` | `~/Applications/RS Re-ARMed Learn & Play.app` |
| rearmed-mods | `~/src/rearmed-mods/build/rearmed-mods.dylib` | the player's `mods/` |
| Song Bridge | `~/src/rearmed-songbridge/build/songbridge.dylib` | the player's `mods/` |
| companion | `~/rs-mac-companion` build of `librscompanion.dylib` | the player's `mods/` |

## Suites

The set is a smoke and regression test to run before a release of any of the parts, not only of the newest
features: the game's normal flows on both editions first, with the mods as players have them, then each feature.

- `smoke` (the release gate, fastest): `boot`, `song`, `bass`, `pause_menu`, `exit_save`, `songbridge`,
  `companion`, and one extended-range song (`guitar7`). Minutes per edition.
- `regression`: `smoke` plus every other stock-flow and mod case below.
- `extended`: the extended-range cases (`guitar7`, `guitar8`, `bass5`, `line_off`, `alternates`, `coop`,
  `hearing`).
- `all`.

`run.py --suite NAME` (default `smoke`); `--case` picks single cases.

## The game's normal flows (stock songs, stock instruments)

| Case | Route | Checks |
|---|---|---|
| `boot` | start, intro skipped, title, profile, main menu | the white loading screen fix (title in time, `menu load` retries if any), DLC enumerated (`DLC: N archives mounted`), every mod and the built-in fixes start (their lines), RS Re-ARMed's ADV DISPLAY rows |
| `song` | Learn a Song, a stock 6-string song (disc), Lead, tuner (fake input in tune), play 30 s | the song loads and plays, notes heard (`game note:` from the fake input), the accuracy/note counts move, snapshots of the highway |
| `bass` | the same for a stock 4-string Bass arrangement | as `song` |
| `pause_menu` | in a song: pause, Resume, pause, Restart, pause, Riff Repeater (a phrase, speed), back, Exit | each screen reached, the Riff Repeater mods' lines, the song freed on exit |
| `score_attack` | Score Attack, a stock song, 20 s, exit | its screens, the Score Attack accuracy line |
| `guitarcade` | Guitarcade, a game, Skip Tuner (the built-in fix), 15 s, exit | the game starts without tuning |
| `tools` | Tuner, Tone Designer or Amp, Options and ADV DISPLAY SETTINGS (each row changes) | each screen, no fault |
| `song_lists` | the song list's sort and filter, previews | the Song Lists / Song List Titles / previews lines |
| `exit_save` | EXIT GAME from the main menu, start again | the profile saved and loaded again (the same profile, a setting changed in one run is kept), a clean quit |
| `songbridge` | the other platform's form of a stock CDLC and of a native chart in dlc | `[songbridge]` serves them, listed, one plays |
| `companion` | a song | the companion's log: song loaded, arrangement selected, tone and state events (its socket) |
| `tonehost` | a song with Tone Host on and the fake input playing | the host starts, the song's tones reach it (its log), stops on exit (optional: needs Helix Native or another plug-in) |

## The extended-range cases

Every case also checks, on both editions: the mods load (`rearmed-mods ... loaded on <target>, 38 mods`, no
`stays off`, no `[rc]` fault or `lp: stack`), Song Bridge and the companion load, the title comes within its time
(the white loading screen fix), and the game quits by its own menu or Command-Q, not a kill.

| Case | Charts | Route | Checks (log lines, then snapshots for a look) |
|---|---|---|---|
| `boot` | none | title, profile, main menu, quit | the above; the basic mods' start lines (see below) |
| `guitar7` | Nickelback (7-string Lead) | Learn a Song, the song, Lead, tuner, play 40 s, pause, exit the song, quit | native package accepted, manifests rewritten, B STANDARD 7STR, 7 strings built, tuner on string 6 at MIDI 35, SNXR converted (7 strings), detection widened, headstock model shown, chord frames, freed on exit; companion: song loaded, arrangement selected |
| `guitar8` | Fear Factory (8-string Lead) | as guitar7 | as guitar7 for 8 strings: F♯ STANDARD 8STR, MIDI 30 |
| `bass5` | Jinjer (5-string Bass, low B down a tone), Nickelback (5-string Bass) | Learn a Song, each bass | names (5STR), bass built with 5 strings, tuner's low string, SNXR converted as a bass, detection widened; the low B colour |
| `line_off` | Nickelback | as guitar7 with `string7_line = 0` | the added string's lines hidden, detection still 7 strings |
| `alternates` | the made-up package with 7 arrangements | Learn a Song, the song, the arrangement list, down past the fifth and back | no fault, the list scrolls (its log lines), the last arrangement can be picked and loads |
| `songbridge` | the other platform's form of Nickelback and Fear Factory | as guitar7 for one of them | `[songbridge]` serves them, the song is listed and plays as in guitar7 |
| `mods` | Nickelback + a stock song | a song with the basic mods' settings on | the basic mods' lines while a song plays (see below) |
| `coop` | Nickelback | Multiplayer with the test's two profiles, each on a channel of the fake input; player 1 the 7-string Lead, player 2 the 5-string Bass; play 20 s, pause, exit | `player 2 on "..."`, both instruments built (guitar 7, bass 5: `seven_gkr`), both tuners, no "cable is unplugged", the co-op arrangement list |
| `hearing` | Nickelback, Fear Factory, Jinjer | the tuner of each with the fake input playing a low note (B1 35, F♯1 30, A0 21/B0 23) | the game hears it: the tuner's detection and `game note:` lines at the expected MIDI note |

The basic mods' checks are each mod's own log line where it has one; a mod with no log line is listed as unchecked
in the report rather than passed.

## Fake inputs (no person in the loop)

RS Re-ARMed's audio layer (both editions, `shims/coreaudio/`) gets a test device, on only with
`RS_AUDIO_FAKE_INPUT`: a capture device named "RS Re-ARMed Test Input" listed with the real ones, of as many
channels as the switch names, each fed from a WAV file (looped), a generated tone (`tone:MIDI[:SECONDS]`, a plucked
string's decay, repeated) or silence (`silence`), e.g. `RS_AUDIO_FAKE_INPUT=tone:35,silence`. The test
profiles have their input on its channels (player 1 channel 1, player 2 channel 2), so the tuner, the songs and
co-op run with nothing plugged in, and what the game hears is known.

## Fast

- One game start per edition and settings group: the cases run one after another in the same game (back to the
  menu between them), not a start each. A case that needs different start-time settings (e.g. `string7_line`,
  which is read at start) is grouped with others that need the same; RSMods.ini is reread by the mods while the
  game runs (`RSMods.ini (re)loaded`), so a key read live needs no restart.
- The route moves as soon as the log shows the next screen (no fixed waits); intro and splash are skipped; a song
  is played only as long as its checks need (the checks' lines come in the first seconds), then left by the pause
  menu, which also tests the song's free path.
- The checks run as the log comes (a case is done as soon as its lines are in), not after a fixed hold.
- Snapshots only where they are worth a look (the highway, the tuner), not on a timer.
- Learn & Play can run without a window (`LP_OFFSCREEN=1`) once its keys can be given without one: then it runs
  at the same time as the Mac edition.

## How a run goes

1. `setup.py` (re)places the parts above in the test installs, writes `builds.json`, builds keyfifo if missing.
2. For each case: empty `dlc/gametest/`, copy its charts, write `RSMods.ini` (rearmed-mods' defaults + the test base +
   the case's keys; the test base turns off what reaches outside the test install: Tone Host, Discord, Song Stems
   unless a case wants them), start the edition with the logs on (`RS_MODS_LOG=1 RS_STEAM_LOG=1
   RS_SONGBRIDGE_LOG=1`, the companion's log to the case folder), keyfifo and snapshots every 10 s.
3. The route is driven by the log: each `[rsmods] menu: SCREEN` line decides the next keys (as rs-rearmed's
   `tools/perf/run.py` does), a step that sees no new screen in time is retried, then the case fails as stuck.
4. The case ends by the game's own way out; the log is then checked, `result.json` written.
5. `report.md` collects every case of every edition, with the snapshots worth a look named.
