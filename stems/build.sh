#!/bin/sh
# Builds rs-stems into $RS_BUILD_DIR/stems (default build/stems). Needs only the Xcode tools. The helper runs on
# macOS 15 or later, the oldest the model is converted for (its attention op is macOS 15's).
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)
out=${RS_BUILD_DIR:-$root/build}/stems
mkdir -p "$out"
flags="-arch arm64 -O2 -Wall -mmacosx-version-min=15.0"
clang $flags -c "$here/wwise_vorbis.c" -o "$out/wwise_vorbis.o"
clang $flags -c "$here/stems_dsp.c" -o "$out/stems_dsp.o"
clang $flags -fobjc-arc "$here/rs_stems.m" "$out/wwise_vorbis.o" "$out/stems_dsp.o" -framework Foundation \
  -framework CoreML -framework Accelerate -framework AudioToolbox -o "$out/rs-stems"
codesign --force -s - "$out/rs-stems"
echo "built: $out/rs-stems"
