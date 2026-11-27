# Game tests: unattended runs of both editions

A repeatable test of the whole stack in the game: RS Re-ARMed (the Mac edition and Learn & Play), rearmed-mods, Song
Bridge and the companion (`librscompanion.dylib`), on test installs that never touch the player's own game, saves or
settings. Each case is a scripted route through the game's menus, driven by keys, with the game's own log, the
companion's log and events and snapshots collected, and checks run over them as they come. The same cases run on
both editions.

Run it with the player's screen free: the game takes keys only while its window has the focus, and pauses when it
loses it.

    tests/game/fixtures.py              the test charts, both platforms' forms, and the test sounds, into <root>/fixtures
    tests/game/setup.py [--edition E] [--builds main|installed] [--fresh-saves] [--warm-shaders]
                                        the test installs (made once, refreshed from the builds each run)
    tests/game/run.py [--edition mac|lp|both] [--suite smoke|regression|extended|all] [--case NAME ...]
                      [--builds main|installed] [--root R] [--no-setup] [--stop-on-fail] [--list]
                                        the cases; a report in <root>/runs/<stamp>/report.md
    tests/game/test_runner.py           the runner's own tests, without a game (python3 -I tests/game/test_runner.py)

The runner is `run.py` and its modules: `gt_game.py` (the lock, a game started and always ended, the companion's
socket, snapshots, the route engine), `gt_routes.py` (the routes' building blocks), `gt_cases.py` (the cases and
their checks, each pattern citing the line that prints it), `gt_log.py` (the log and the check matcher) and
`gt_report.py`.

## Where things are (the contract between the three scripts)

`RS_GAMETEST_ROOT`, default `~/rs-gametest` (the internal drive). The game copies are APFS clones on the drive the
games are on (`<games>`, `/Volumes/Storage/rs-gametest` here), linked from the installs: clones take no room only on
the game's own volume, and both drives are nearly full, so nothing here copies a game.

    <root>/fixtures/pc/*.psarc          the charts as Learn & Play reads them (_p)
    <root>/fixtures/mac/*.psarc         the same, as the Mac reads them (_m)
    <root>/fixtures/audio/*.wav         plucked notes (MIDI 40, 35, 30, 28, 23, 21) and a strum, for the test input
    <root>/fixtures/fixtures.json       charts {id: files {pc, mac}, song_key, title, artist, search, arrangements
                                        (name, instrument, strings, tuning_name, open_midi, lowest_midi), for};
                                        stock_songs {edition: song_key, search, lead, bass}; audio {name: file, midi}
    <root>/mac/home/                    the Mac test install (RS_NATIVE_HOME): game -> <games>/mac/game, steam/,
                                        mods/, logs/, libnetguard.dylib
    <root>/lp/                          the Learn & Play test folder: game -> <games>/lp/game, userdata/, steam/,
                                        mods/, out/ (the build's rs2014_lp, lp_game.exe, dlls, libnetguard.dylib)
    <root>/tools/libkeyfifo.dylib       rs-rearmed's tools/ws8/keyfifo, built
    <root>/builds.json                  each part's source (repo, commit, sha256, path) and per edition how a run
                                        starts it ("editions": exe, cwd, env, insert, rsmods_ini, dlc_test, steam_env)
    <root>/profiles.json                per edition the test saves' account and profiles, the co-op pair
    <root>/<edition>/RSMods.base.ini    the settings base (below), kept by the runner
    <root>/companion/<edition>/         the companion's data folder in runs (RS_COMPANION_DATA): rscompanion.log,
                                        events.sock (bound by the runner), what it learned
    <root>/runner/fake-cycle.wav        the test input's channel 1 (below), made by the runner from fixtures/audio
    <root>/runs/<stamp>/<edition>/_gN/  each game start: game.log (game-2.log after a restart), launch-N.json,
                                        netguard.log, events.jsonl, snaps-N/ (pruned as they age)
    <root>/runs/<stamp>/<edition>/<case>/
        game.log, companion.log, events.jsonl, keyfifo.log (the case's slices), snap/*.png, RSMods.ini (as run),
        result.json
    <root>/runs/<stamp>/report.md       per edition a table (case, PASS / FAIL / SKIP / MANUAL, time), the game
                                        starts and how each ended, each failed check with its lines, the snapshots
                                        to look at, the builds

The runner takes `~/rs-gametest/.game-lock` (a folder, `owner.json` in it) before each game start, waits for it
(every 30 s, up to 30 min) when another run holds it, and removes it when the game ends, on Ctrl-C and SIGTERM too.
A game outside the lock (the player's) is waited for the same way. setup.py refuses to run while any live process
holds the lock (the installs are in use), so run.py waits for the lock to be free, runs setup.py, then takes the
lock for its games; another run that takes it in between makes setup.py refuse, and it is tried again. (setup.py
could take an owner pid to run under its caller's lock and close that gap.)

Never the player's game: every place a run writes (the settings file, dlc/gametest/, the saves) is checked to be
the test install's. The companion's data and log go to `<root>/companion/`, not the player's.

## Parts under test and where they come from

| Part | `--builds main` (default) | `--builds installed` |
|---|---|---|
| Mac game | `~/src/rs-rearmed/build/m4/app/Rocksmith2014.app` (+ `rsmsl-corpus.bin` from the player's app, re-signed) | the player's `~/Library/Application Support/RS Re-ARMed/game/Rocksmith2014.app` |
| Learn & Play | `~/src/rs-rearmed/build/win-x86/lp/out` | `~/Applications/RS Re-ARMed Learn & Play.app` |
| rearmed-mods | `~/src/rearmed-mods/build/rearmed-mods.dylib` | the player's `mods/` |
| Song Bridge | `~/src/rearmed-songbridge/build/songbridge.dylib` | the player's `mods/` |
| companion | `~/rs-mac-companion` built into `<root>/build/librscompanion.dylib` | the player's `mods/` |

## Suites

The set is a smoke and regression test to run before a release of any of the parts, not only of the newest
features: the game's normal flows on both editions first, with the mods as players have them, then each feature.

- `smoke` (the release gate, fastest): `boot`, `song`, `bass`, `pause_menu`, `songbridge`, `guitar7`, `exit_save`.
  About 6 minutes an edition, one game start (and the restart of `exit_save`).
- `regression`: `smoke` plus `guitar8`, `bass5`, `alternates`, `coop`, `score_attack`, `guitarcade`, `tools`,
  `song_lists`, `mods`.
- `extended`: the extended-range cases (`guitar7`, `guitar8`, `bass5`, `alternates`, `songbridge`, `coop`).
- `all`.

`run.py --suite NAME` (default `smoke`); `--case` picks single cases; `--list` lists them.

A case is PASS when every check passes or is skipped, FAIL when a check fails or its route got stuck, SKIP when the
run lacks what it needs (the test input), MANUAL when its route is not driven yet (the report gives its steps). A
check may be SKIP on its own: it needs the test input, or its line need not come in the part played (`chord frames`).

## The cases

The companion's checks run in every song case: its log (`song KEY loaded by the game`, `selected arrangement`) and
its events on the socket (`song` with the song's key, `tone`, `state` with the song). The test input's checks run in
every song case too: the device on (`[audio] fake input: N channels`), the game reading it (`guitar input "RS
Re-ARMed Test Input": peak`) and `game note: N` at the case's lowest note while the song plays.

| Case | Route | Checks |
|---|---|---|
| `boot` | start, intro skipped, title, profile (ForceProfileLoad enters it), main menu | the mods load (`rearmed-mods ... loaded on <target>, 38 mods`, no `stays off`), Song Bridge and the companion load, the built-in fixes' start lines, the intro skipped, auto save warning skipped, DLC mounted, the profile read, the white loading screen fix (no fallback, every refused menu accepted again), the title within 30 s and the main menu within 120 s; on the Mac RS Re-ARMed's ADV DISPLAY rows counted at the start and the co-op list fix |
| `song` | Learn a Song, the edition's stock song (Mac Sixteen Saltines, Learn & Play Brannan Street Blues), Lead, the tuner (Skip Tuner), play until the checks are in, out by the pause menu | the song plays (`Play_KEY: playing id`), the accuracy mod's position, E2 (40) heard, the companion, the song freed on exit |
| `bass` | the same, the song page's Bass (twelve Downs to Score Attack, one Up) | the bass's tuner (4 strings), the companion's bass arrangement, a note heard |
| `pause_menu` | in a song: pause, Resume, pause, Restart, pause, Riff Repeater (Space), the speed key (R, RRSpeedAboveOneHundred on), back, Exit | the pause menu each time, Riff Repeater, the song played again after Restart, Riff Repeater's song node, speed, speed reset on leaving, the song freed |
| `songbridge` | the other platform's form of the stock CDLC (Territorial Pissings) and of Fear Factory in dlc; the CDLC played | `[songbridge]` serves both, none left unserved, the song's archive opened through it, the song plays |
| `exit_save` | EXIT GAME from the main menu (Esc, Return, Esc, Return on EXIT GAME?), start again | the profile last saved is the one read at the next start (same file, same size), a clean quit |
| `score_attack` (regression) | the song page's last entry, EASY, Skip Tuner, play, pause, EXIT SONG | Score Attack's screens, the accuracy line, Learn & Play's note counts line (the Mac reads them another way, with no line) |
| `guitarcade` (regression) | GUITARCADE (Down five from the top), TECHNIQUE GAMES, the second game, Skip Tuner (the built-in fix), Return | the tuner, Skip Tuner's question, the game started without tuning |
| `tools` (regression) | Space (Tools), OPTIONS, ADV DISPLAY SETTINGS, a look, out | each screen; RS Re-ARMed's rows listed (`menu options: N in Adv Display Settings`, N >= 10). No row is changed |
| `song_lists` (regression) | the song list with Custom Song List Titles and the previews switch changed (read live), Down, SORT BY | the switches' lines, songs highlighted |
| `mods` (regression) | the stock song with the basic mods on (read live), each mod's key once | each mod's own line: song timer, lyrics, volume, mute (twice), sunlight, looping, rewind, camera, nashville numbers; mods with no line of their own are listed as unchecked |

### The extended-range cases

| Case | Charts | Route | Checks |
|---|---|---|---|
| `guitar7` | Nickelback (7-string Lead) | Learn a Song, the song, Lead, the tuner, play until the checks are in (at most 45 s), out | native package accepted, manifests rewritten, B STANDARD 7STR, 7 strings built, the tuner on string 6 at MIDI 35 and its low range, SNXR converted (7 strings), the RS7X block read, detection widened (7, 35), the 7-string headstock model, chord frames (optional), the low B (35) heard, the companion, freed on exit; the Seven-String and Plain Packages start lines |
| `guitar8` | Fear Factory (8-string Lead) | as guitar7 | as guitar7 for 8 strings: F♯ STANDARD 8STR, string 7 at MIDI 30, (8, 30), the low F♯ (30) heard |
| `bass5` | Nickelback (5-string Bass), Jinjer (5-string Bass, low B down a tone) | Nickelback's Bass (the page's last other arrangement), then Jinjer's (Play Song), each until its low string is heard | names (B STANDARD 5STR, DROP A 5STR), the tuning stored with 5 strings, the bass's tuner with 5 strings on string 4 at MIDI 23 / 21, SNXR converted as a bass, the bass part's `5-string extended bass` and the detection floor below the low string, B0 (23) and A0 (21) heard, the companion |
| `alternates` | the made-up package with 7 arrangements | Learn a Song, the song, twelve Downs (past the fourth alternate: the window scrolls), Up (the last arrangement, the Bass), play, out | the fix's start line, `Learn a Song: Down, window N -> M of 6 alternates`, the last arrangement plays (4-string tuner) |
| `coop` | Nickelback | MANUAL until its route exists: Multiplayer with the test's two profiles (profiles.json's pair), player 2's INPUT set to the test input's channel 2 in the menus; player 1 the 7-string Lead, player 2 the 5-string Bass; play 20 s, pause, exit | `player 2 on "..."`, both instruments built (guitar 7, bass 5), both tuners, no "cable is unplugged", the co-op arrangement list |

The companion on the Mac edition looks for a song's archive in the player's game folder, not the test install's
(rs-mac-companion `native/rscompanion.c` takes `$HOME/Library/Application Support/RS Re-ARMed/game`, ignoring
`RS_NATIVE_HOME`): a chart the player also has (Nickelback) is found there, one the player lacks (Jinjer) is not,
so `bass5`'s companion check fails on the Mac until it follows `RS_NATIVE_HOME`. Learn & Play's follows
`LP_GAME_DIR`.

Every case also checks that no line says a fault (`lp: signal`, `lp: stack`, an `[rc]` fault or a null pointer
given to the game); a fault ends the case at once. Every game start's end is in the report: a quit by the game's own
menu or Command-Q with exit status 0 is clean, anything else (a signal, SIGTERM, a kill) is NOT A CLEAN QUIT and the
run exits 1.

By hand, not in the set: **Tone Host** with a plug-in (Helix Native or another): a song with `ToneHost = on` and the
test input playing; the host starts, the song's tones reach it (its log), it stops when the song is left.

## Fake inputs (no person in the loop)

RS Re-ARMed's audio layer (both editions, `shims/coreaudio/`) has a test device, on only with
`RS_AUDIO_FAKE_INPUT=entry,entry,...` (up to 8 channels): a WAV path (looped), `tone:MIDI[:SECONDS]` (a plucked
note; a fraction is cents), `calibrate[:MIDI]`, or `silence`. It is "RS Re-ARMed Test Input", listed after a cable
and before other interfaces. The test saves' INPUT already hears its channel 1 in both editions, so no Path/Input
route is needed for player 1.

Every game a run starts gets channel 1 `<root>/runner/fake-cycle.wav` and channel 2 `silence` (with
`RS_AUDIO_LOG=1` for the input's peak line). The WAV is the fixtures' plucked notes one after another, 4 s each and
looped: E2 (40), B1 (35), F♯1 (30), B0 (23), A0 (21), the lowest note of every song case. So one game start serves
every case, and each case waits (within its hold) for its own note in `game note:`; the cycle is 20 s. Without the
fixtures' sounds the input is `tone:39.88` (an exact E2 reads 12-14 cents sharp on the tuner in both editions).
`RS_GAMETEST_FAKE_INPUT` overrides it all. A build without the test input (its program lacks the switch's name)
makes the heard checks SKIP.

No route goes through calibration: tuners are passed by Skip Tuner (`fwddelete`) and Return on "continue without
tuning". A `*Calibration*` screen stops the case at once (a key that landed on a tuner's Calibrate) and Esc
recovers.

## Fast

- One game start per edition and start-time settings group (a case's `start_ini` and `env`): the cases run one
  after another in the same game, back to the main menu between them. No case of the set needs a start of its own
  now. RSMods.ini is reread by the mods while the game runs (`RSMods.ini (re)loaded`): a case's live keys are
  written before it, and the case's lines start before the write, so the mods' answers are its own.
- **Charts per game start, not per case**: the game lists dlc/ only when it starts, so `dlc/gametest/` is emptied and
  filled with the union of the group's charts before the start, each in the edition's own form (Song Bridge's in the
  other platform's). A case whose chart's other form would sit beside its own form (Song Bridge leaves out a song
  that is there already) goes to a start of its own.
- The route moves as soon as the log shows the next screen; each key is confirmed by the mods' `key down 0x..` line
  (sent again after `front` when it does not come in 6 s); a screen that drops a key moving in gets the key again
  after a few seconds; a key whose effect has begun (`target tuning stored` after Play Song) is not sent again.
  keyfifo's keys reach both editions, `front` with each.
- A song is played only as long as its checks need (they are evaluated as the log comes), then left by the pause
  menu, which also tests the song's free path.
- **Snapshots**: the renderer has no on-demand snapshot, so a game runs with `RS_GL_SNAPSHOT_SECS=3`, the runner
  keeps the first complete PNG after a moment it names (the tuner, the highway, a page) and prunes the rest after
  30 s. A `<dir>/now` file that `mtl_context.m` looks for at present would make them exact.
- Learn & Play could run without a window (`LP_OFFSCREEN=1`) once its keys can be given without one: then beside the
  Mac edition. Not done: both run in turn under the lock.

## How a run goes

1. `setup.py` (re)places the parts in the test installs and writes `builds.json` and `profiles.json` (quick when
   nothing changed); `--no-setup` skips it.
2. The cases are planned into game starts (above). For each start: the lock taken, then `dlc/gametest/` filled and
   `RSMods.ini` written (another run may use the installs until then: setup.py empties `dlc/gametest/`), the game
   started as builds.json says (the executable directly, so `DYLD_INSERT_LIBRARIES` keeps the
   network guard and keyfifo), with `RS_MODS_LOG=1 RS_STEAM_LOG=1 RS_SONGBRIDGE_LOG=1 RS_FULLSCREEN=0`, the
   companion's data folder, the test input and the snapshots; the network guard's `mode=block` awaited; the title,
   the profile and the main menu; the DLC mounted.
3. **The settings base**: setup.py puts no RSMods.ini in the installs, so the base is the player's own (only read)
   or the install's when it has one not written by the runner; it is kept as `<root>/<edition>/RSMods.base.ini`.
   Each case's file is the base, then the test base, then the case's keys, as sections at the end (the mods take a
   key's last line). The test base turns off what reaches outside the test install (Tone Host, Discord, Song Stems)
   and turns on what the checks read (Show Current Note, Display Current Accuracy, Seven-String, Plain Packages).
4. Each case: its route, its checks, its slices of the logs and its snapshots in its folder, `result.json`. A stuck
   route fails the case, says where it was, and goes back to the main menu by Esc steps (a song by its pause menu;
   the quit question answered No), or the game is started again; the run goes on.
5. The game is quit (keyfifo's Command-Q, then SIGTERM, then SIGKILL) and the lock removed; `report.md` collects
   every case of every edition.

## What the checks learned in the game

- Some lines come once a game process, not once a song: Riff Repeater's song node, Score Attack's note counts, the
  accuracy mod's state (logged when it changes, kept across songs). They are checked over the game's whole log
  (`scope="session"`), or only in a game's first song.
- The game writes the profile when something in it changes (leaving a song, a setting), not at EXIT GAME:
  `exit_save` checks that the last write is what the next start reads.
- `chord frames` is logged when a chord's frame is built: Nickelback has no chord in its first 45 s, so the check is
  optional. fixtures.json could record each chart's first chord time.
- A 5-string bass logs its own detection lines (`seven-string: bass: ... 5-string extended bass`, the detection
  floor), not the guitar's `song detection widened`.
- The song page: Play Song at the top (Up does nothing), Rocksmith Recommends, OTHER ARRANGEMENTS (Lead, Rhythm,
  Bass), Score Attack last; Down does not wrap. Score Attack's pause menu: RESUME SONG, EXIT SONG, ... The main menu,
  the Tools list and Options keep their cursor: routes go Up to the top first. ADV DISPLAY SETTINGS lists RS
  Re-ARMed's rows after the game's six in Learn & Play and four on the Mac.
