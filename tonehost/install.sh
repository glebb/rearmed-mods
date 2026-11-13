#!/bin/bash
# Installs rs-tonehost with no plug-in set up: builds it, puts the host in RS Re-ARMed/tonehost and writes a config if
# there is none. Then Re-ARMed Mods' Tone Host settings choose the interface and add a plug-in; helix/setup.sh does all
# of this and Helix Native's sounds. Run it again after a change to the host: a config and plug-ins there are kept.
# RS_NATIVE_HOME: the install, by default ~/Library/Application Support/RS Re-ARMed.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
home=${RS_NATIVE_HOME:-"$HOME/Library/Application Support/RS Re-ARMed"}
out=${RS_BUILD_DIR:-$here/../build}/tonehost
dest=$home/tonehost
pgrep -xq Rocksmith2014 && { echo "Quit the game first." >&2; exit 1; }
[ -f "$home/libnetguard.dylib" ] || { echo "No network guard at $home/libnetguard.dylib: install RS Re-ARMed first." >&2; exit 1; }

"$here/build.sh"
mkdir -p "$dest"
pgrep -f "RS Tone Host.app" > /dev/null && echo quit > "$dest/commands"  # nobody reading: a write would wait for ever
rm -rf "$dest/RS Tone Host.app"
cp -R "$out/RS Tone Host.app" "$dest/"
cp "$out/start.sh" "$out/stop.sh" "$dest/"
[ -f "$dest/config.json" ] || python3 - "$here/config.default.json" "$dest" <<'PY'
import json, os, sys
c = json.load(open(sys.argv[1]))
c["fifo"] = os.path.join(sys.argv[2], "commands")
json.dump(c, open(os.path.join(sys.argv[2], "config.json"), "w"), indent=1)
PY
echo "Installed in $dest. In Re-ARMed Mods, turn on Your Own Plug-in, choose the interface and add a plug-in."
