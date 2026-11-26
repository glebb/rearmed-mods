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
$cc -O1 "$here/tests/test_level.c" -o "$out/test_level"
$cc "$here/tests/test_places.c" -o "$out/test_places"
$cc "$here/tests/test_stems.c" -o "$out/test_stems"
$cc -O2 -I"$here/pitch" "$here/tests/test_splice_shift.c" "$here/pitch/splice_shift.c" -o "$out/test_splice_shift"
stems_index="$here/tests/test_stems_index.c $here/src/stems_index.c $here/src/song_info.c $here/src/rsmods_files.c $here/src/rsmods_tuning.c $here/src/seven_native_core.c"
$cc $stems_index "$here/tests/rc_file.c" -lz -o "$out/test_stems_index"
$cc -DNO_LAYER $stems_index -lz -o "$out/test_stems_index_plain"
$cc -O1 "$here/tests/test_tuning.c" "$here/src/rsmods_tuning.c" "$here/src/seven_native_core.c" -lz -o "$out/test_tuning"
$cc "$here/tests/test_pitch_auto.c" "$here/src/rsmods_tuning.c" "$here/src/seven_native_core.c" -lz -o "$out/test_pitch_auto"
$cc "$here/tests/test_profiles.c" "$here/src/rsmods_profiles.c" -lz -o "$out/test_profiles"
$cc "$here/tests/test_song_lists.c" "$here/src/rsmods_profiles.c" -lz -o "$out/test_song_lists"
$cc "$here/tests/test_song_list_titles.c" -o "$out/test_song_list_titles"
$cc -DGUEST32 "$here/tests/test_song_list_titles.c" -o "$out/test_song_list_titles32"
$cc -O1 "$here/tests/test_7z.c" "$here/src/rsmods_7z.c" -lz -o "$out/test_7z"
$cc "$here/tests/test_cache.c" "$here/src/rsmods_cache.c" -o "$out/test_cache"
$cc "$here/tests/test_song_info.c" "$here/src/song_info.c" "$here/src/rsmods_files.c" "$here/src/rsmods_tuning.c" "$here/src/seven_native_core.c" -lz -o "$out/test_song_info"
$cc "$here/tests/test_discord.c" "$here/src/discord_ipc.c" -o "$out/test_discord"
$cc "$here/tests/test_song_plan.c" "$here/src/song_plan.c" -o "$out/test_song_plan"
$cc "$here/tests/test_seven_grid_math.c" -lm -o "$out/test_seven_grid_math"
$cc "$here/tests/test_seven_barre.c" -o "$out/test_seven_barre"
$cc "$here/tests/test_seven_chmatch.c" -o "$out/test_seven_chmatch"
$cc "$here/tests/test_song_key.c" "$here/src/song_harmony.c" "$here/src/song_key.c" "$here/src/song_sng.c" "$here/src/rsmods_tuning.c" "$here/src/seven_native_core.c" -lz -o "$out/test_song_key"
$cc "$here/tests/test_seven_native.c" -o "$out/test_seven_native"
$cc -DGUEST32 "$here/tests/test_seven_native.c" -o "$out/test_seven_native32"
$cc "$here/tests/test_seven_pitch.c" -o "$out/test_seven_pitch"
$cc "$here/tests/test_seven_names.c" -o "$out/test_seven_names"
$cc "$here/tests/test_seven_model.c" "$here/src/seven_model.c" -o "$out/test_seven_model"
# Wave 2's tests of the packages: tests/test_<name>.link, if there, lists the extra sources and flags of its build
# (paths from the repository's root; a package writes it instead of editing this file)
for n in seven_bass seven_spacing seven_arrangements seven_colours seven_headstock; do
  link=
  if [ -f "$here/tests/test_$n.link" ]; then
    for w in $(cat "$here/tests/test_$n.link"); do
      case $w in
      -*) link="$link $w" ;;
      *) link="$link $here/$w" ;;
      esac
    done
  fi
  $cc "$here/tests/test_$n.c" $link -o "$out/test_$n"
done
"$out/test_seven_model"
for n in seven_bass seven_spacing seven_arrangements seven_colours seven_headstock; do "$out/test_$n"; done
"$out/test_ini"
"$out/test_colors"
"$out/test_gate"
"$out/test_gain"
"$out/test_level"
"$out/test_places"
"$out/test_stems"
"$out/test_splice_shift"
"$out/test_stems_index"
"$out/test_stems_index_plain"
"$out/test_sunlight"
"$out/test_tuning"
"$out/test_pitch_auto"
"$out/test_profiles"
"$out/test_song_lists"
"$out/test_song_list_titles"
"$out/test_song_list_titles32"
"$out/test_7z"
"$out/test_cache"
"$out/test_song_info"
"$out/test_discord"
"$out/test_song_plan"
"$out/test_song_key"
"$out/test_seven_native"
"$out/test_seven_native32"
"$out/test_seven_grid_math"
"$out/test_seven_barre"
"$out/test_seven_chmatch"
"$out/test_seven_pitch"
"$out/test_seven_names"
"$here/tests/test_tonehost_migrate.sh"

cd "$here/gui"
swift test
