#!/bin/sh
# Runs the tests: the library's (tests/, no game needed) and the app's (gui/Tests).
set -eu
here=$(cd "$(dirname "$0")" && pwd)
out=$here/build/tests
mkdir -p "$out"
cc="clang -arch arm64 -g -Wall -Wno-unused-function -I$here/src -I$here/include/rs-rearmed -I$here/tests -DREARMED_MODS_VERSION=\"test\""

$cc "$here/tests/test_ini.c" "$here/src/rsmods_ini.c" -o "$out/test_ini"
$cc "$here/tests/test_sunlight.c" -lobjc -framework CoreFoundation -framework CoreGraphics -o "$out/test_sunlight"
$cc "$here/tests/test_colors.c" -o "$out/test_colors"
$cc "$here/tests/test_gate.c" -o "$out/test_gate"
"$out/test_ini"
"$out/test_colors"
"$out/test_gate"
"$out/test_sunlight"

cd "$here/gui"
swift test
