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
$cc "$here/tests/test_gain.c" -o "$out/test_gain"
$cc -O1 "$here/tests/test_tuning.c" "$here/src/rsmods_tuning.c" -lz -o "$out/test_tuning"
$cc "$here/tests/test_profiles.c" "$here/src/rsmods_profiles.c" -lz -o "$out/test_profiles"
$cc "$here/tests/test_song_lists.c" "$here/src/rsmods_profiles.c" -lz -o "$out/test_song_lists"
$cc "$here/tests/test_song_list_titles.c" -o "$out/test_song_list_titles"
$cc -DGUEST32 "$here/tests/test_song_list_titles.c" -o "$out/test_song_list_titles32"
$cc -O1 "$here/tests/test_7z.c" "$here/src/rsmods_7z.c" -lz -o "$out/test_7z"
$cc "$here/tests/test_cache.c" "$here/src/rsmods_cache.c" -o "$out/test_cache"
"$out/test_ini"
"$out/test_colors"
"$out/test_gate"
"$out/test_gain"
"$out/test_sunlight"
"$out/test_tuning"
"$out/test_profiles"
"$out/test_song_lists"
"$out/test_song_list_titles"
"$out/test_song_list_titles32"
"$out/test_7z"
"$out/test_cache"

cd "$here/gui"
swift test
