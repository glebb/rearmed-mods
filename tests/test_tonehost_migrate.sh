#!/bin/sh
# tonehost/migrate.py: a config with the plug-in in itself becomes a profile, and its states and levels go along
set -eu
here=$(cd "$(dirname "$0")" && pwd)
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
mkdir "$d/states"
touch "$d/states/a.plist" "$d/b.plist" "$d/levels.plist" "$d/changes.plist"
cat > "$d/config.json" <<JSON
{"plugin": {"type": "aufx", "subtype": "SND1", "manufacturer": "Shed"}, "input": {"device": "X"}, "buffer": 64,
 "templates": {"a": {"state": "states/a.plist"}, "b": {"state": "b.plist"}, "c": {"state": "/elsewhere/c.plist"}},
 "map": {"clean": "a", "driven": "b"}}
JSON
python3 "$here/../tonehost/migrate.py" "$d" --id shed --name Shed > /dev/null
p=$d/plugins/shed
for f in "$p/profile.json" "$p/states/a.plist" "$p/b.plist" "$p/levels.plist" "$d/changes.plist"; do
  [ -f "$f" ] || { echo "FAIL: no $f"; exit 1; }
done
for f in "$d/states" "$d/b.plist" "$d/levels.plist" "$d/plugins/.shed.new"; do
  [ ! -e "$f" ] || { echo "FAIL: $f is still there"; exit 1; }
done
python3 - "$d/config.json" "$p/profile.json" <<PY
import json, sys
c, p = (json.load(open(f)) for f in sys.argv[1:])
assert sorted(c) == ["buffer", "input", "profile"] and c["profile"] == "shed", c
assert sorted(p) == ["map", "name", "plugin", "templates"] and p["name"] == "Shed" and len(p["templates"]) == 3, p
PY
python3 "$here/../tonehost/migrate.py" "$d" --id again > /dev/null 2>&1 && { echo "FAIL: moved twice"; exit 1; }
echo "tonehost migrate: ok"
