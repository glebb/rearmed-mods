#!/bin/bash
# Tells whether the presets make_presets.py builds are the plug-in's own: for whoever changes how they are built, maps
# an amp to a Helix model not used before, or meets a new Helix Native. Helix imports the same presets as a setlist,
# each one's state is saved, and the host compares state and preset through the plug-in. One import by hand; your Helix
# library is put back afterwards, and nothing is installed. Quit the game first.
#   tonehost/helix/verify.sh
# RS_NATIVE_HOME: the install, for its network guard. RS_HELIX_SETLIST: which setlist the import goes to, counted as
# MIDI does (6 is USER 5).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
home=${RS_NATIVE_HOME:-"$HOME/Library/Application Support/RS Re-ARMed"}
out=${RS_BUILD_DIR:-$root/build}/tonehost
native="$HOME/Library/Application Support/Line 6/Helix Native"
setlist=${RS_HELIX_SETLIST:-6}
guard=$home/libnetguard.dylib

pgrep -xq Rocksmith2014 && { echo "Quit the game first." >&2; exit 1; }
[ -d "$native/#PRESET_LIBRARY" ] || { echo "Helix Native isn't set up here: open it in a host once." >&2; exit 1; }
[ -f "$guard" ] || { echo "No network guard at $guard: install RS Re-ARMed first." >&2; exit 1; }

"$here/../build.sh"
work=$(mktemp -d)
python3 "$here/make_presets.py" "$work" --setlist

# The import overwrites a setlist, so the library is put back afterwards
backup=$work/library
mkdir "$backup"
cp -R "$native/#PRESET_LIBRARY" "$backup/"
cp "$native/Helix Native.prefs" "$backup/"
restore() {
  rm -rf "$native/#PRESET_LIBRARY"
  cp -R "$backup/#PRESET_LIBRARY" "$native/"
  cp "$backup/Helix Native.prefs" "$native/Helix Native.prefs"
  echo "Your Helix library is back as it was."
  rm -rf "$work"  # also so that nobody can import this setlist by mistake next time
}
trap restore EXIT

cat <<EOT

One step for you, in the window that opens:
  1. Choose the setlist USER $((setlist - 1)) at the top left. It is put back afterwards.
  2. Click IMPORT next to SETLISTS, press Command-Shift-G and paste this path, then press Return and Open:
     $work/setlist.hls
  3. Answer Yes, then close the window.

EOT
export RS_NETGUARD_LOG=$work/netguard.log
DYLD_INSERT_LIBRARIES=$guard "$out/rs-tone-editor" aumf 6HPA LSIX

tried=$work/tonehost
mkdir -p "$tried/plugins/helix-native/states"
names=$(python3 -c "
import json, sys
t = json.load(open(sys.argv[1]))['templates']
print(' '.join(v['state'] for k, v in sorted(t.items(), key=lambda kv: kv[1]['slot'])))" "$work/tones.json")
# shellcheck disable=SC2086
DYLD_INSERT_LIBRARIES=$guard "$out/capture_states" "$setlist" "$tried/plugins/helix-native/states" $names | tail -1

[ "$(md5 -q "$tried"/plugins/helix-native/states/* | sort -u | wc -l)" -gt 1 ] || { echo "The presets all came out the same: was the import in the right setlist?" >&2; exit 1; }
# A state of an older setlist looks the same from outside: the reverb, the last block, has to answer
first=${names%% *}
read -r reverb_switch reverb_mix < <(python3 -c "
import json, sys
f = json.load(open(sys.argv[1]))['follow']
print(max(r['param'] for r in f if 'present' in r), [r['param'] for r in f if r.get('prefix') == 'Rack_StudioPlate_' and r['from'] == '_Mix'][0])" "$work/tones.json")
DYLD_INSERT_LIBRARIES=$guard "$out/check_states" "$tried/plugins/helix-native/states/$first" "$reverb_switch" "$reverb_mix" ||
  { echo "These presets aren't the ones just made: was the right setlist.hls imported?" >&2; exit 1; }

# A profile with both, in a folder of its own: the host's sound check then holds each preset against its state
host="$out/RS Tone Host.app/Contents/MacOS/rs-tonehost"
python3 "$here/make_config.py" "$work/tones.json" "$tried/config.json" --out "$tried" --id helix-native --states \
  --version "$("$host" --plugin aumf 6HPA LSIX)" > /dev/null
mv "$tried/profile.json" "$tried/plugins/helix-native/"
cp -R "$work/presets" "$tried/plugins/helix-native/presets"
DYLD_INSERT_LIBRARIES=$guard "$host" --config "$tried/config.json" --check-sound 2>&1 | tail -2
