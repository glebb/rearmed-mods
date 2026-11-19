# rs-tonehost for Helix Native

What makes Helix Native work with [rs-tonehost](../README.md), and what was learned about its files, none of it documented by
Line 6. Run [`setup.sh`](setup.sh) with the game closed; run it again after a Helix update or a change to `gear.json`.
It needs Helix Native installed and authorized, and nothing done by hand.

| File | What it does |
|---|---|
| `gear.json` | The game's gear onto Helix models. **Edit this one** |
| `make_presets.py` | Writes `presets/`, the pieces of a preset as the plug-in's engine takes one: a frame, a block per Helix amp and a block per effect. And `tones.json`: the chain's order, where each knob is in a block, and the rules from game knobs to parameters |
| `base.hlx` | An empty preset, the frame of Helix's file: a preset is described as Helix's own file has it |
| `engine.c` | The plug-in's engine as the host reaches it |
| `../config.default.json` | The config of a new install: `make_config.py` and `../install.sh` start from it |
| `make_config.py` | Writes the profile, `plugins/helix-native/profile.json`, and `config.json` with it chosen, keeping the player's settings |
| `setup.sh` | All of it |
| `verify.sh` | Tells whether the presets are the plug-in's own, with one import by hand. Installs nothing |
| `capture_states.m` | For `verify.sh`: selects each imported preset by MIDI and saves its state |
| `check_states.m` | For `verify.sh`: tells whether a state has the chain just made, by toggling the last block and listening |

## Correcting the gear

An amp that sounds wrong: change its line in `gear.json`, `"Amp_GB38": "AmpBritJ45Nrm"`, to another Helix amp. Helix's amps are
in `HX_ModelCatalog.json` in `/Library/Application Support/Line 6/Helix Native/res`; drop `HD2_` from the id. Then run
`setup.sh`, which takes a minute and keeps your settings. For a Helix amp not used here before, run `verify.sh` once
too: it tells whether the preset built for it is the one Helix itself makes. The names of disguised amps are a best
guess, and the cab is always Helix's default for the amp, whatever the song has. A Helix preamp, `Preamp...`, has no cab, and
`amp_set` sets its parameters, as `MicLine` for Studio Tube Pre.

`high_gain` lists Helix amps that get less input and drive for the same knob, and a tighter gate.

`tone_db` lists the game's amps whose Bass, Mid and Treble are in dB, from -12 to 12, as nine bass amps' are; the others
go from 1 to 100. On these, 0 dB is the middle of Helix's knob. The game's `gears.psarc` has every knob's unit and range,
in `manifests/gears`.

## The chain

The blocks a tone can have, in the chain's order. A tone gets its amp and the blocks it has gear for, each in the next
place: with no pedals that is the amp alone. The numbers name a knob or a switch in the rules; with every block there
they are Helix's own Audio Unit parameters, which is how `verify.sh` checks them.

| Path | Block | Model | Switch | Knobs |
|---|---|---|---|---|
| 1 | Octave up | Simple Pitch, +12 | 16 | 9 Mix |
| 1 | Octave down | Simple Pitch, -12, mix 50% | 17 | |
| 1 | Wah | Auto Filter: there is no pedal to rock | 18 | |
| 1 | Fuzz | Triangle Fuzz | 19 | 8 Sustain |
| 1 | Metal | Swedish Chainsaw | 20 | 14 Drive |
| 1 | Drive | Minotaur | 21 | 5 Gain, 6 Tone |
| 1 | Amp and cab | from `gear.json`, Helix's default cab; a preamp has none | | 0 the amp's gain, 1 Bass, 2 Mid, 3 Treble, 4 Presence |
| 1 | Compressor | Deluxe Comp, 4:1 | 22 | 7 Threshold |
| 2 | Acoustic | Acoustic Sim, mode 2 | 23 | 1 Body, 3 Top, with Studio Tube Pre only |
| 2 | Chorus | Chorus | 24 | 10 Mix |
| 2 | Flanger | Gray Flanger, rate 0.25 | 25 | |
| 2 | Phaser | Script Mod Phase | 26 | |
| 2 | Tremolo | Optical Trem | 27 | |
| 2 | Delay | Simple Delay | 28 | 11 Time, 12 Feedback, 13 Mix |
| 2 | Reverb | Plate | 29 | 15 Mix |

All sixteen knobs are in use, so a new one has to replace another. The octave up's mix took the flanger's rate: the game's
octave mixes go from 1 to 100, a faint shimmer on an acoustic to a whole octave lead, and a fixed 50% made the first kind
a loud second voice; the flanger is on fewer tones, and its rate stays at their median. The octave down still mixes 50%.
Parameters 32 and 33 are Master Input and Output Level, in dB; 32 is how hard a tone hits the amp. It follows a rule from fits
on a few tones, not a measurement of each amp. With a drive pedal it rises faster; with fuzz or metal it doesn't, as those
distort plenty themselves. That hasn't been compared with the game.

The order above is for a tone whose chain isn't known: one the game says with no song's plan behind it, as in the menus.
A song's tones come with their chain, the game's gear in the signal's order, and their blocks go in that order: a
compressor ahead of the amp where the game has it there, a chorus after it. Each block goes where the first of its gear
is. The amp's block has its cab in it, so what the game puts in the amp's loop, between amp and cab, comes after the
cab here.

An acoustic tone in the game is its Acoustic Emulator pedal, nearly always into the Tube Pre DI and a full-range PA cab. That
became Helix's bass amp and its 1x15, with nothing for the emulator: a dull electric. Now the DI preamps are Studio Tube Pre
with no cab, and the emulator switches on Acoustic Sim, in mode 2 as Line 6's own acoustic presets have it. Studio Tube Pre has
no tone knobs, so its preset gives knobs 1 and 3 to the sim's Body and Top, from the emulator's Body and Tone: a spare knob,
`spare_knobs` in `gear.json`, with the rule in that template alone. With a guitar amp, as about one acoustic tone in eight
has, the sim keeps Helix's default Body and Top and sits after the cab. None of it has been compared with the game, mode and
shimmer included, and the DI Tube Pre's own EQ is left out.

## More blocks

Helix's own preset has room for the chain above and no more, but a tone's preset is put together by the host, so a tone
can have blocks beside it: `more` in `gear.json`. A parameter there comes from a game knob in the Helix parameter's own
unit, dB to dB and Hz to Hz, kept within what that parameter takes. In the songs of one library, 1,199 tones, four in
ten of all pedals and racks were EQs.

| Block | Model | The game's gear |
|---|---|---|
| eq5 | Cali Q Graphic | 5-Band Graphic EQ: 63, 250, 750, 2.2k and 5.7k onto its 80, 240, 750, 2.2k and 6.6k |
| eq10 | 10 Band Graphic | 8-Band Graphic EQ and 8-Band Bass EQ: each band on the nearest of ten, a third of an octave off at most |
| lofi | Low and High Cut, 400 Hz to 3 kHz | Lo-Fi Filter, its knobs not followed |
| shelves | Low/High Shelf | Studio Parametric EQ and Studio ParaGraphic EQ: Bass and Treble with their frequencies |
| mids | Parametric | The same two: Low-Mid and High-Mid with their frequencies, a wide Q as 0.7 and a narrow one as 2 |

Sent a quiet sine at twenty-one frequencies, every band of these moved the level where it should and by what it
should. Whether a game's Bass is a shelf, and what its wide and narrow are, isn't known: both are guesses. `verify.sh`
doesn't cover these blocks, as Helix's own preset has none of them.

`least_gain` keeps an amp's gain from going under a value, for the amps that go silent near the bottom of it: a Helix
amp's gain is its volume too, and the game's clean amps and bass amps have theirs near nothing. 56 of those tones came
out under -60 dB, and the host then turned them up by as much as 90 dB. An amp that still plays down there, as Brit
2204 does, has no such value and keeps its gain as the game has it.

`moving` lists the blocks a tone's level is measured without: with a slow flanger a tone plays ten dB louder and
quieter as it sweeps, and a level taken over a second of it was anything in between.

Not mapped: the game's noise gate, which sets the host's own, pitch shifters, Bit Cruncher, Ring Mod, the Eden D.I.
and the Amp EQ pedal: 16 uses in those 1,199 tones.

An amp's gain isn't always called Drive: Fullerton and Grammatico have DriveNorm, Matchstick Ch1Drive and Ch2Drive, US Dripman
Norm Drive, Mandarin Bass and Agua Sledge Gain. An amp's model lacks some knobs too, and those stay as they are.

## Straight to the engine

Helix Native is a layer over an engine, `HelixCoreLib.framework` in `/Library/Frameworks`, and Line 6 installs the engine's
headers in it: `HelixCore.h` has its functions, `HelixCorePreset.h` a preset, `symbolTableMasterList.h` every model by
number. When a state loads, the plug-in hands the engine one plain preset of 18,332 bytes with `hc_loadPreset`, and
nothing else. The host hands it one too: it puts a tone's preset together from the pieces in `presets/`, the frame, the
amp's block and a block for each effect the tone has, with the tone's knobs written in at their places, and gives it to
the engine of the instance nobody hears. No state is loaded and no knob moved, so
there is nothing to import and nothing of Helix's library is used. The comment in `make_presets.py` has the layout.

- **The same sound.** `verify.sh` has Helix import a whole preset per amp as a setlist and saves a state of each. The
  host's `--check-sound` then compares, for every amp: with the state loaded, every block switched on and every knob
  moved on the plug-in, the engine has the very preset the host puts together from the blocks; and the amp alone,
  handed to the engine, plays as loud as the state with its effects off. All 36 passed with Helix Native 3.8.2. Rendered offline, a preset handed to the
  engine came out sample for sample as the state with the same knobs set, and a slot that never had a state plays as
  one that had. `setup.sh` can't tell this much: there the engine has to take each preset unchanged, and it has to
  sound.
- **Knobs.** A knob of the plug-in is a value of one block, on a line from `@min` to `@max` and on beyond them: the plug-in
  doesn't stop at 0 or 1, and neither does a preset. Master Input and Output, parameters 32 and 33, are the plug-in's
  own, outside the engine, and are set as before.
- **Instances.** The engine numbers its instances as they are made, as the host does its slots. The host makes sure at
  its start, before any audio: a slot is silent with its instance muted in the engine.
- **Order of values.** A block's values are in the engine's order. That is the models file's, except that nine models
  have some values the other way round in `HX_ModelCatalog.json`; Matchstick Ch2, the one of them in use, is as the
  catalog has it. `verify.sh` would tell a wrong one, and nothing else here would.
- **What the plug-in doesn't know.** Its editor, its own knobs and the state it would save still have the preset it
  opened with. So a slot is never handed a preset and then moved by its knobs.
- **While it loads** a slot isn't rendered, as with a state. A load takes about 40 ms, as a state's does, and for some 60
  to 80 ms after it the engine plays under its level: the host waits 0.1 s before a tone is heard.

Not done: a chain of a tone's own, which is what this is for. The headers are Line 6's: they are read from the install,
as the models files are, and aren't kept here.

## Helix's files

- **Models.** The `.models` files in `res` are JSON: every model's parameters with `min`, `max` and `default`, and `ircablink`,
  the amp's default cab. A block with only some parameters isn't trusted: `make_presets.py` writes them all from the defaults.
- **`.hlx`** is JSON, and a preset for the plug-in has `device` 2162944, which is Helix compatibility mode. Other devices ask the
  user to translate.
- **`.hls`** is a setlist: JSON around `encoded_data`, base64 of zlib of `{"meta": ..., "presets": [...]}`, with `crc32` and
  `decompressed_size`. It takes 128 presets, and imports in one go over the setlist in view.
- **Automation.** `tone.automation["@assignments"]` is a list by position: 0-15 are Knob 1-16 and 16-31 Switch 1-16, unused ones
  `null`. An entry is `{"@param": "dsp0.block2.Drive", "@min": 0, "@max": 1}`, and for a switch `"@param": "dsp0.block2.@enabled"`
  with `@min` false and `@max` true. `@min` and `@max` set the Audio Unit parameter's range in the block's units. One entry
  per knob. An enum such as the compressor's Ratio is its index, 0-5.
- **Audio Unit parameters** 0-15 are the knobs, 16-31 the switches, 32 Master Input, 33 Master Output, 34 Master Bypass, 36 Snapshot
  Index. A knob's value from a state isn't reported: set every knob you rely on after loading one.
- **Paths.** A path holds eight blocks; an amp and its cab take one. An amp with its cab is `@type` 3 with `"@cab": "cab0"`,
  a preamp `@type` 1 with no cab, the rest 0. `dsp0.outputA["@output"] = 2` sends path 1 to path 2A. In the editor brackets
  round a block's name mean it has a bypass control; a dim block is off.
- **Snapshots** list `blocks` per `dsp`, which set what is on: keep them in step with `@enabled`.
- **Preset recall by MIDI** works in the Audio Unit only, not VST3: CC 32 picks the setlist, 6 being USER 5, then Program Change.
  A change takes up to half a second and dips, so never on the instance being heard.
- **State.** `kAudioUnitProperty_ClassInfo` has `Line6PresetData`, about 90 KB, and the library's `#PRESET` files are scrambled.
  They are encrypted, by Line 6's own scheme, and stay unread: states are saved and restored whole, and a state loads in about 34 ms. They come from one Helix version,
  so run `setup.sh` again after an update.
- **CPU.** A bypassed block costs the same as an active one: about 2.7% of a core for an amp alone, about 7% for the chain.
  So a tone's preset has only the blocks the tone has: about 3% on eleven tones, where the whole chain took 7.5%.
- **Network.** After authorization Helix runs without it. Its editor window tries a server and carries on when refused.

Line 6's files are read from the user's install and aren't kept here. This page is notes on formats, with no data of theirs.
