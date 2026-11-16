#!/bin/bash
# Sets up rs-tonehost for Helix Native, from nothing or again after a Helix update or a change to gear.json: builds it,
# makes one preset per Helix amp as the plug-in's engine takes it, and plays each through the plug-in, heard by nobody.
# Nothing is done by hand and Helix's own library isn't touched: Helix Native has to be installed and authorized, no
# more. Quit the game first.
#   tonehost/helix/setup.sh [--input DEVICE --channel N] [--output DEVICE]
# The interface can be chosen in Re-ARMed Mods' Tone Host settings instead, and a later run keeps your settings.
# RS_NATIVE_HOME: the install, by default ~/Library/Application Support/RS Re-ARMed. Then turn on ToneHost in
# rearmed-mods: the game starts the host from then on.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
home=${RS_NATIVE_HOME:-"$HOME/Library/Application Support/RS Re-ARMed"}
out=${RS_BUILD_DIR:-$root/build}/tonehost
dest=$home/tonehost
guard=$home/libnetguard.dylib

pgrep -xq Rocksmith2014 && { echo "Quit the game first." >&2; exit 1; }
[ -d "/Library/Application Support/Line 6/Helix Native/res" ] || { echo "Helix Native isn't installed here." >&2; exit 1; }
[ -f "$guard" ] || { echo "No network guard at $guard: install RS Re-ARMed first." >&2; exit 1; }

"$here/../build.sh"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
python3 "$here/make_presets.py" "$work"

# The profile whole, or not at all: Re-ARMed Mods offers a plug-in whose folder is there
id=helix-native
host="$out/RS Tone Host.app/Contents/MacOS/rs-tonehost"
mkdir -p "$work/new" "$dest"
python3 "$here/make_config.py" "$work/tones.json" "$dest/config.json" --out "$work/new" --id "$id" --name "Helix Native" \
  --version "$("$host" --plugin aumf 6HPA LSIX)" "$@"
stage=$dest/plugins/.$id.new
rm -rf "$stage"
mkdir -p "$stage"
cp -R "$work/presets" "$stage/presets"
cp "$work/new/profile.json" "$stage/"

pgrep -xq rs-tonehost && echo quit > "$dest/commands"  # nobody reading: a write would wait for ever
rm -rf "$dest/RS Tone Host.app"
cp -R "$out/RS Tone Host.app" "$dest/"
cp "$out/start.sh" "$out/stop.sh" "$dest/"
rm -rf "$dest/plugins/$id"  # and its levels.plist, measured with the presets before these
mv "$stage" "$dest/plugins/$id"
cp "$work/new/config.json" "$dest/config.json.new"
mv "$dest/config.json.new" "$dest/config.json"
rm -rf "$dest/states" "$dest/levels.plist"  # of a setup from before profiles
# Every preset through the plug-in, heard by nobody: only then do Re-ARMed Mods' settings offer it and the host play it
DYLD_INSERT_LIBRARIES=$guard "$dest/RS Tone Host.app/Contents/MacOS/rs-tonehost" --config "$dest/config.json" --check-sound 2>&1 | tail -2 ||
  { echo "The presets didn't play. If Helix Native isn't authorized yet, open it once in a host and sign in, then run this again." >&2; exit 1; }
echo "Done. Turn on ToneHost in rearmed-mods; macOS asks for the microphone the first time the host runs."
