# rs-tonehost

Plays the guitar through an Audio Unit of your own in place of the game's tones. It is an add-on: it uses the network guard
that RS Re-ARMed installs, and nothing in RS Re-ARMed needs it. It works for one player, in both editions, and has only
been tried with Helix Native.

Two pieces:

- **The mod** is `ToneHost` ([`src/mod_tone_host.c`](../src/mod_tone_host.c), on with `ToneHost = on` in `RSMods.ini`). On a
  tone change the game sets every knob of the gear as a Wwise RTPC by name, then the tone's balance, about two seconds
  before the change in the song. The mod sends these as one line. While the host runs, the game's guitar is silent
  (`Mixer_Player1` at 0; notes still score). With no host the mod does nothing, and the game plays its own tones; a host
  that stops gives the game its guitar back within about a second, and a host that starts later gets the tone in play.
- **The host** (`tonehost.m`, `RS Tone Host.app`). A separate process, so a plug-in's licence check, window or crash
  stays out of the game. It has two plug-in instances, or up to four with `slots`: one plays, another takes the next tone's saved state, and a
  crossfade joins them. A load is audible; an instance standing by is not.

The game's effects still run: this mutes them, it doesn't remove them.

## Build and run

For Helix Native, with the game closed:

```bash
tonehost/helix/setup.sh --input "HX Stomp" --channel 5 --output "HX Stomp"
```

It asks you for one import and puts your Helix library back afterwards. Then set `ToneHost = on` in `RSMods.ini`: the
game starts the host (three times, 30 s apart, if it doesn't come; again when one that came has ended), and the host
ends a few seconds after the game does, a crash included. `start.sh` and `stop.sh` in
`RS Re-ARMed/tonehost` do it by hand; `"stay_after_game": true` keeps it running. Everything is in that folder.

For another Audio Unit, set a sound in its own window and save it as a template's state:

```bash
build/tonehost/rs-tone-editor aufx SND1 Shed clean.plist
```

The state is written when the window closes. `auval -a` lists the codes.

macOS asks for the microphone the first time, and again after every rebuild, as an ad-hoc signature changes with the
build. Until it is answered the host hangs in `AudioDeviceCreateIOProcID`, and killing it there loses the question: start
it with `open`, as `start.sh` does, so the prompt is the app's own, and answer it. Running it through `nohup` or the
system `perl` drops `DYLD_INSERT_LIBRARIES`, and the network guard with it.

The mod looks for the FIFO at `~/Library/Application Support/RS Re-ARMed/tonehost/commands`, or at `RS_TONEHOST_FIFO`.

## The config

```json
{
  "plugin": {"type": "aumf", "subtype": "6HPA", "manufacturer": "LSIX"},
  "input": {"device": "HX Stomp", "channel": 4, "gain_db": 0},
  "output": {"device": "HX Stomp"},
  "buffer": 64,
  "fade_ms": 20,
  "gate": {"margin_db": 12, "tight_db": 8, "min_db": -72, "max_db": -40, "hold_ms": [150, 40], "release_ms": [120, 30]},
  "fifo": "/path/to/commands",
  "templates": {
    "clean": {"state": "clean.plist", "output_db": 14.56, "params": {"0": 0.5, "32": -13.95}},
    "driven": {
      "state": "driven.plist", "output_db": 5.94, "params": {"32": 17.65},
      "follow": [
        {"param": 0, "from": "amp_gain", "scale": 0.93, "offset": 0},
        {"param": 3, "from": "_Gain", "prefix": "Pedal_", "scale": 0.0098, "offset": 0}
      ]
    }
  },
  "map": {
    "gain_knobs": ["_Gain", "_Loudness1", "_Loudness2", "_Drive"],
    "drive_pedals": ["Pedal_GermaniumDrive"],
    "clean": "clean", "driven": "driven", "driven_from_gain": 0.3,
    "by_prefix": {"Amp_MarshallJCM800": "driven"}
  }
}
```

- `channel` counts from 0. The host sets 48 kHz on its devices. With no `output` the sound follows the system's output,
  as the game's does.
  Input and output on one device give the lowest latency: one callback and nothing held between them. A virtual device
  such as Loopback adds a hop. `buffer` can go below the 128 the game's input works in: the game collects what comes,
  from RS Re-ARMed 1.9.0, and the host sets its size again if something changes it.
- For more than one interface, `"setups": [{"input": {…}, "output": {…}}, …]`: when the host starts with the devices of
  `input` and `output` not connected, the first of these that is connected takes their place. Re-ARMed Mods' Tone Host
  settings choose the interface, its channel, gain and output, and keep each one played in this list.
- `gate` works on the input after `gain_db`, ahead of the plug-in; leave it out for none. It learns the guitar's noise,
  the quietest 100 ms lately, rising only for a level that holds steady, so a note ringing out doesn't take the gate up
  with it. It opens `margin_db` above that, kept between `min_db` and `max_db`. A tone's gain moves it
  from loose to tight: `tight_db` more threshold, and `hold_ms` and `release_ms` from their first value to the second. A
  template's `tight` scales that. The game's Noise Gate pedal, where a tone has one, sets a lowest threshold.
- `state` is an Audio Unit's `ClassInfo` saved as a property list, relative to the config.
- `params` are Audio Unit parameter ids and values, set when the template starts. `follow` puts a game knob on a
  parameter: `amp_gain` is the amp's gain knob from 0 to 1, anything else a knob whose name ends in `from`.
- The game says a tone only as it changes, and a state's load and its knobs' ramps take half a second after that. So
  the mod sends a song's plan as the song loads: the changes the loaded song has (a time and a slot each) and each
  slot's tone from the arrangement's manifest, then where the song is, every other frame. The host has the next tone
  ready in the slot nobody hears, its level measured, and plays it `early_ms` (100) before its place. With no plan (the
  song's arrangements differ in their tones and have as many levels, or the game is one the mod can't read a song of)
  the host keeps where each tone came, in `changes.plist`, and is ahead from the song's second play.
  `slots`, 2 to 4, is how many instances there are, and an instance takes memory.
- `target_db` makes every tone equally loud: before a tone is heard, a reference pluck (`probe_peak_db`, at the input)
  goes through the instance nobody hears, and its gain is set from the level that comes out. `trim_db` in a template
  moves it from there.
  The pluck repeats until two agree, as a plug-in ramps a knob that was moved, and the level is kept in `levels.plist`
  until the config's templates or rules change: a tone measured once changes within 0.1 s where its slot still has it, else in 0.3 s and a load. Without `target_db` the template's `output_db` applies.
- The system's volume controls the guitar, wherever the sound goes, and so does the game's guitar fader, which the mod
  passes on. `"ignore_system_volume": true` turns the first off.
- `trim` in the map, `{"db": 1.5, "with": ["Pedal_X"], "clean": true}`, moves the level of a tone that has that gear, or of a
  clean one: no drive pedal, and the amp's gain under `driven_from_gain`.
- `follow` with `"present": ["Pedal_X"]` turns a switch parameter on when the tone has that gear; `with` and `without`
  apply a rule only if the tone has, or lacks, the gear. A template's rules come after the map's.
- `by_prefix` picks a template by a knob name's start; otherwise a drive pedal or an amp gain from `driven_from_gain`
  up picks `driven`, and anything else `clean`.

`helix/` makes all this for Helix Native: `gear.json` is the game's gear onto Helix models, `make_presets.py` writes a
setlist with one preset per Helix amp and the rules, from Line 6's model files in the install, and `capture_states.m`
saves each preset's state after a one-time import. The setlist isn't needed after that.

Commands on the FIFO: `gear KEY=VALUE ...`, `mixer VALUE`, `time SONG SECONDS` and a song's plan (`plan SONG`,
`plantone SLOT KEY=VALUE ...`, `planat TIME:SLOT ...`, `planend`) from the mod, `offset VALUE` from
[RS Companion](https://github.com/glebb/rs-mac-companion), `use TEMPLATE`, `mute` and `quit` by hand.

`offset` is how much too hot a song's tones are against its record, in thirds of the gain knob: an AI's answer, 0 if there
is none. It comes as a song loads, and lowers `amp_gain` for a tone at a third or more, so clean parts stay as they are. A
change takes the tone in play through the instance nobody hears, as any tone change does.

## What is measured, and what isn't

Offline, against the game with a test signal: on one song a template with the tone's amp, compressor and drive pedal
came within 0.29 dB RMS of the distorted tone's level and 1.41 dB of the clean one's, the clean amp being a stand-in.
Each Helix instance costs about 2.7% of a core and two take about 1.4 GB; the one standing by renders only around a switch. A bypassed amp saves nothing. Not done:
measuring each amp's drive against the game, latency measured end to end, two players.
