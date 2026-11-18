#!/bin/sh
# Makes recordings the bench can read (-r): 48 kHz, mono, 32-bit float .wav, one per input, into a folder.
#   prepare.sh OUT_DIR FILE...        any audio files afconvert reads
#   prepare.sh OUT_DIR ARCHIVE.zip... Guitar-TECHS archives (zenodo.org/records/14963133, CC BY 4.0): their dry
#                                     tracks, <pack>/audio/directinput/*.wav, and nothing else of them
set -eu
out=$1
shift
mkdir -p "$out"
for src in "$@"; do
  case $src in
    *.zip)
      tmp=$(mktemp -d "$out/.unpack.XXXXXX")
      unzip -q -j "$src" '*/audio/directinput/*.wav' -d "$tmp"
      pack=$(basename "$src" .zip)
      for f in "$tmp"/*.wav; do
        afconvert -f WAVE -d LEF32@48000 -c 1 "$f" "$out/$pack-$(basename "$f" .wav | sed 's/^directinput_//').wav"
      done
      rm -r "$tmp"
      ;;
    *)
      afconvert -f WAVE -d LEF32@48000 -c 1 "$src" "$out/$(basename "${src%.*}").wav"
      ;;
  esac
done
ls -l "$out"
