#!/bin/bash
# For an rs-tonehost set up before plug-ins had profiles: installs this build's host and moves the plug-in in config.json
# into plugins/ID, with its states and measured levels. Nothing is imported or captured again. Quit the game first.
#   tonehost/migrate.sh ID NAME        e.g. tonehost/migrate.sh helix-native "Helix Native"
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
home=${RS_NATIVE_HOME:-"$HOME/Library/Application Support/RS Re-ARMed"}
out=${RS_BUILD_DIR:-$here/../build}/tonehost
dest=$home/tonehost
[ $# -eq 2 ] || { echo "usage: $0 ID NAME" >&2; exit 2; }
pgrep -xq Rocksmith2014 && { echo "Quit the game first." >&2; exit 1; }
[ -f "$dest/config.json" ] || { echo "No rs-tonehost in $dest." >&2; exit 1; }

"$here/build.sh"
pgrep -f "RS Tone Host.app" > /dev/null && echo quit > "$dest/commands"  # nobody reading: a write would wait for ever
host="$out/RS Tone Host.app/Contents/MacOS/rs-tonehost"
python3 "$here/migrate.py" "$dest" --id "$1" --name "$2" --host "$host"
# The host before this one can't read a profile
rm -rf "$dest/RS Tone Host.app"
cp -R "$out/RS Tone Host.app" "$dest/"
cp "$out/start.sh" "$out/stop.sh" "$dest/"
"$dest/RS Tone Host.app/Contents/MacOS/rs-tonehost" --config "$dest/config.json" --check
echo "Done. macOS asks for the microphone again the first time this host runs."
