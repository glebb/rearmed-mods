#!/bin/sh
# Installs rs-stems and the model into the helper folder ($RS_STEMS_HOME, default
# ~/Library/Application Support/RS Re-ARMed/stems) and loads the model once: the first load of a model file compiles it
# for the Neural Engine (20-50 s), which a song shouldn't have to wait for.
#
# The model isn't in the repository: RS_STEMS_MODEL is htdemucs_6s's Core ML package (stems/convert/README.md), default
# build/stems/htdemucs_6s.mlpackage; an .mlmodelc already compiled is taken as it is.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)
build=${RS_BUILD_DIR:-$root/build}/stems
home=${RS_STEMS_HOME:-$HOME/Library/Application Support/RS Re-ARMed/stems}
model=${RS_STEMS_MODEL:-$build/htdemucs_6s.mlpackage}

major=$(sw_vers -productVersion | cut -d. -f1)
if [ "$major" -lt 15 ]; then
  echo "rs-stems needs macOS 15 or later (this is $(sw_vers -productVersion))" >&2
  exit 1
fi
if [ ! -e "$model" ]; then
  echo "no model at $model: make it with stems/convert, or set RS_STEMS_MODEL" >&2
  exit 1
fi
[ -x "$build/rs-stems" ] || "$here/build.sh"

mkdir -p "$home"
cp "$build/rs-stems" "$home/rs-stems.new"
mv -f "$home/rs-stems.new" "$home/rs-stems"
stage="$home/.model.new"
rm -rf "$stage"
mkdir -p "$stage"
case "$model" in
  *.mlmodelc) cp -R "$model" "$stage/model.mlmodelc" ;;
  *) xcrun coremlcompiler compile "$model" "$stage" >/dev/null ;;
esac
rm -rf "$home/htdemucs_6s.mlmodelc"
mv "$stage"/*.mlmodelc "$home/htdemucs_6s.mlmodelc"
rm -rf "$stage"
echo "installed: $home"
"$home/rs-stems" warm
