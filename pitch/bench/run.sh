#!/bin/sh
# Builds and runs the pitch shifters' bench (bench.c has what it measures); arguments go to the bench.
# RAB_DIR=<a checkout of github.com/Cheesewizard/RocksmithAudioBridge> adds its shifter as the baseline.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
out=$here/../../build/pitch
mkdir -p "$out"
flags="-arch arm64 -O2 -g -Wall"
extra=""
if [ -n "${RAB_DIR:-}" ]; then
  src=$RAB_DIR/DLL/DropPedal
  clang++ $flags -std=c++17 -I"$here/rab_shim" -I"$src" -c "$src/DelayLinePitchShifter.cpp" -o "$out/rab_shifter.o"
  clang++ $flags -std=c++17 -I"$here/rab_shim" -I"$src" -c "$here/rab_baseline.cpp" -o "$out/rab_baseline.o"
  extra="-DHAVE_RAB $out/rab_shifter.o $out/rab_baseline.o -lc++"
fi
clang $flags $extra "$here/bench.c" "$here/../pitch_shift.c" "$here/../splice_shift.c" -o "$out/bench"
exec "$out/bench" "$@"
