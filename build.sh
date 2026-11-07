#!/bin/sh
# Build rearmed-mods.dylib, RSMods' features for RS Re-ARMed.
#   ./build.sh              -> build/rearmed-mods.dylib
#   ./build.sh --install    also copy it into RS Re-ARMed's mods folders ($RS_NATIVE_HOME/mods, default
#                           ~/Library/Application Support/RS Re-ARMed/mods, and learn-play/mods under it if Learn
#                           & Play is installed). Quit the game first.
# One library for both editions, RS Re-ARMed 1.5.1 and later: it is built with rc_mod.h and has no game address. The
# runtime it uses (OV_GUEST, RC_OFF, rc_hook_add_name) is resolved when the game loads it; the declarations are in
# include/rs-rearmed (from RS Re-ARMed, MIT). It builds without the game.
set -eu
# The same sources give the same library: without this the linker records the object files' modification times,
# and the settings app, which compares the installed library with its own, would offer every rebuild as an update
export ZERO_AR_DATE=1
here=$(cd "$(dirname "$0")" && pwd)
install=0
[ "${1:-}" = --install ] && install=1
out=$here/build
mkdir -p "$out/obj"
version=$(cat "$here/VERSION")

CC="clang -arch arm64 -O2 -g -mmacosx-version-min=11.0 -Wall -Wno-unused-function -ffp-contract=off -fno-strict-aliasing -I$here/src -I$here/include/rs-rearmed -DREARMED_MODS_VERSION=\"$version\""
objs=""
for c in "$here"/src/*.c; do
  o="$out/obj/$(basename "$c" .c).o"
  $CC -c "$c" -o "$o"
  objs="$objs $o"
done
clang -arch arm64 -dynamiclib -mmacosx-version-min=11.0 -install_name @rpath/rearmed-mods.dylib \
  -Wl,-undefined,dynamic_lookup $objs -lobjc -framework OpenGL -framework CoreText -framework CoreGraphics \
  -framework CoreFoundation -framework CoreAudio -framework ImageIO -framework CoreImage -framework QuartzCore -o "$out/rearmed-mods.dylib"
codesign --force -s - "$out/rearmed-mods.dylib" 2>/dev/null || true
echo "rearmed-mods $version: $out/rearmed-mods.dylib"

if [ $install = 1 ]; then
  home=${RS_NATIVE_HOME:-"$HOME/Library/Application Support/RS Re-ARMed"}
  if pgrep -xq Rocksmith2014 || pgrep -xq rs2014_lp; then echo "quit the game first" >&2; exit 1; fi
  found=0
  for dir in "$home" "$home/learn-play"; do
    [ -d "$dir/game" ] || continue
    mkdir -p "$dir/mods"
    # a new file renamed over the old one: overwriting a loaded library in place breaks the running game and
    # leaves macOS with the old code signature cached for the file
    cp "$out/rearmed-mods.dylib" "$dir/mods/rearmed-mods.dylib.new"
    mv -f "$dir/mods/rearmed-mods.dylib.new" "$dir/mods/rearmed-mods.dylib"
    echo "installed: $dir/mods/rearmed-mods.dylib"
    found=1
  done
  [ $found = 1 ] || { echo "no RS Re-ARMed install at $home" >&2; exit 1; }
fi
