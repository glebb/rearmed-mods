#!/bin/bash
# rs-stems' tests. Each needs files from outside the repository (game data, the model, reference decodes) and is
# skipped, with a message, without them:
#   RS_STEMS_TEST_DATA  the test songs: songs/audio/mac/<file id>.wem and vgmstream-cli's decodes songs/<song>.wav, the
#                       Python Core ML pipeline's songs/<song>.coreml.no_guitar.wav and .no_bass.wav, a custom song in
#                       probe/song/ and a Learn & Play song in probe/song-lp/ (the SONGS table below)
#   RS_STEMS_CODEBOOKS  codebooks.bin, made from the Mac game by tools/codebooks_from_mac.py
#   RS_STEMS_HOME       an installed helper and model (stems/install.sh), for (b) and (c)
#   RS_STEMS_TEST_DIR   where the tests write, about 400 MB (default build/stems/test)
#
# (a) the decode of each .wem against vgmstream's: the same length, at most 2 LSB apart
# (b) the song without the guitar and without the bass, three songs, against the Python pipeline: the part taken out
#     within 40 dB
# (c) the protocol: serve, a separate, ready frames that only grow, the .m4a and the index, the same song again from
#     the cache, a budget that removes the lowest score but not the song being played, the end 10 s after the heartbeat
# (d) a song at 48 kHz: one of (b)'s, resampled to 48 kHz as if the game had it so (RS_STEMS_TEST_RATE), against the
#     same reference after resampling back (four resamplings in all: within 30 dB)
#
# usage: stems/test.sh [abcd]   (the tests to run, default all)
set -u -o pipefail
only=${1:-abcd}
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)
build=${RS_BUILD_DIR:-$root/build}/stems
data=${RS_STEMS_TEST_DATA:-}
cb=${RS_STEMS_CODEBOOKS:-}
home=${RS_STEMS_HOME:-$HOME/Library/Application Support/RS Re-ARMed/stems}
out=${RS_STEMS_TEST_DIR:-$build/test}
export RS_STEMS_HOME="$home"

# song, its .wem, vgmstream's decode (in RS_STEMS_TEST_DATA)
SONGS="youreallygotme songs/audio/mac/231314867.wem songs/youreallygotme.wav
whitewedding songs/audio/mac/288176903.wem songs/whitewedding.wav
yyz songs/audio/mac/151875785.wem songs/yyz.wav
custom probe/song/audio/mac/123732889.wem probe/song/123732889.wav
learnplay probe/song-lp/audio/windows/525916663.wem probe/song-lp/525916663.wav"

"$here/build.sh" >/dev/null || exit 1
clang -O2 -Wall -o "$build/pcmtool" "$here/tools/pcmtool.c" "$here/stems_dsp.c" -framework Accelerate || exit 1
tool=$build/pcmtool
fails=0
skip() { echo "  skipped: $*"; }
fail() {
  echo "  FAILED: $*"
  fails=$((fails + 1))
}
wem_of() { echo "$SONGS" | awk -v s="$1" '$1 == s { print $2 }'; }

mkdir -p "$out/cache"
have_data=0
if [ -n "$data" ] && [ -n "$cb" ] && [ -f "$cb" ]; then
  have_data=1
  cp "$cb" "$out/cache/codebooks.bin"
fi
have_model=0
[ -d "$home/htdemucs_6s.mlmodelc" ] && have_model=1

echo "(a) decoding, against vgmstream-cli's"
if [ "${only#*a}" = "$only" ]; then
  skip "not asked for"
elif [ $have_data = 0 ]; then
  skip "set RS_STEMS_TEST_DATA and RS_STEMS_CODEBOOKS"
else
  while read -r name wem wav; do
    if [ ! -f "$data/$wem" ] || [ ! -f "$data/$wav" ]; then
      skip "$name: no $wem or $wav"
      continue
    fi
    echo "  $name: $(RS_STEMS_CACHE="$out/cache" "$build/rs-stems" decode "$data/$wem" "$out/$name.wav")"
    printf '    '
    "$tool" compare "$data/$wav" "$out/$name.wav" || fail "$name"
  done <<EOF
$SONGS
EOF
fi

echo "(b) separation, against the Python Core ML pipeline (SNR of the part taken out, 40 dB or better)"
if [ "${only#*b}" = "$only" ]; then
  skip "not asked for"
elif [ $have_data = 0 ] || [ $have_model = 0 ]; then
  skip "set RS_STEMS_TEST_DATA and RS_STEMS_CODEBOOKS, and install the helper and model (RS_STEMS_HOME)"
else
  for name in youreallygotme whitewedding yyz; do
    wem=$(wem_of $name)
    id=$(basename "$wem" .wem)
    for part in guitar bass; do
      ref="$data/songs/$name.coreml.no_$part.wav"
      if [ ! -f "$data/$wem" ] || [ ! -f "$ref" ]; then
        skip "$name $part: no $wem or $ref"
        continue
      fi
      echo "  $name without the $part:"
      RS_STEMS_CACHE="$out/cache" "$build/rs-stems" separate "$data/$wem" "$id" $part "$out/$name.$part.pcm" |
        sed 's/^/    /' || fail "$name $part"
      printf '    '
      "$tool" stems "$data/songs/$name.wav" "$ref" "$out/$name.$part.pcm" 40 || fail "$name $part"
    done
  done
fi

echo "(c) the protocol"
wa=$(wem_of youreallygotme)
wc=$(wem_of custom)
if [ "${only#*c}" = "$only" ]; then
  skip "not asked for"
elif [ $have_data = 0 ] || [ $have_model = 0 ] || [ ! -f "$data/$wa" ] || [ ! -f "$data/$wc" ]; then
  skip "needs RS_STEMS_TEST_DATA, RS_STEMS_CODEBOOKS, the helper and model, and the songs youreallygotme and custom"
else
  cache=$out/serve
  rm -rf "$cache"
  mkdir -p "$cache/work"
  cp "$cb" "$cache/codebooks.bin"
  ida=$(basename "$wa" .wem)
  idc=$(basename "$wc" .wem)
  cp "$data/$wa" "$cache/work/$ida.wem"  # as the mod writes them
  cp "$data/$wc" "$cache/work/$idc.wem"
  RS_STEMS_CACHE="$cache" "$build/rs-stems" serve &
  pid=$!
  for _ in $(seq 50); do [ -p "$cache/commands" ] && break; sleep 0.1; done
  exec 3>"$cache/commands"
  (while printf '\n' >&3 2>/dev/null; do sleep 1; done) &
  beat=$!
  send() { printf '%s\n' "$1" >&3; }
  in_index() { grep -q "^$1	$2	" "$cache/index.tsv" 2>/dev/null; }
  wait_index() {  # $1 $2 in the index (yes) or not (no), within $3 s
    for _ in $(seq $(($3 * 10))); do
      if in_index "$1" "$2"; then [ "$4" = yes ] && return 0; else [ "$4" = no ] && return 0; fi
      sleep 0.1
    done
    return 1
  }
  pcm_a="$cache/work/$ida.guitar.pcm"

  echo "  separate youreallygotme without the guitar:"
  send "budget	500"
  send "separate	$cache/work/$ida.wem	$ida	guitar	youreallygotme"
  "$tool" watch "$pcm_a" 120 | sed 's/^/    /' || fail "the first separate"
  cp "$pcm_a" "$out/serve-first.pcm"
  if wait_index $ida guitar 60 yes && [ -f "$cache/$ida.guitar.m4a" ]; then
    echo "    cached: $(grep "^$ida	guitar	" "$cache/index.tsv" | tr '\t' ' ')"
  else
    fail "no $ida.guitar.m4a in the index"
  fi

  echo "  the same again, from the cache:"
  ino=$(stat -f %i "$pcm_a")
  send "separate	$cache/work/$ida.wem	$ida	guitar	youreallygotme"
  "$tool" watch "$pcm_a" 60 "$ino" | sed 's/^/    /' || fail "the cached separate"
  printf '    against the first: '
  "$tool" stems "$data/songs/youreallygotme.wav" "$out/serve-first.pcm" "$pcm_a" 0 20 || fail "the cached copy"
  grep -q "^$ida	guitar	[0-9]*	2	" "$cache/index.tsv" || fail "the second play isn't counted"

  echo "  youreallygotme without the bass, then the custom song without the guitar:"
  send "separate	$cache/work/$ida.wem	$ida	bass	youreallygotme"
  "$tool" watch "$cache/work/$ida.bass.pcm" 120 | sed 's/^/    /' || fail "the bass separate"
  wait_index $ida bass 60 yes || fail "no $ida bass in the index"
  send "separate	$cache/work/$idc.wem	$idc	guitar	custom"
  "$tool" watch "$cache/work/$idc.guitar.pcm" 120 | sed 's/^/    /' || fail "the custom separate"
  wait_index $idc guitar 60 yes || fail "no $idc guitar in the index"
  n=$(ls "$cache/work/"*.pcm 2>/dev/null | wc -l | tr -d ' ')
  [ "$n" = 1 ] && echo "    one .pcm left in work/" || fail "$n .pcm files in work/"

  echo "  a budget with room for two of the three:"
  ba=$(awk -v i=$ida '$1 == i && $2 == "guitar" { print $3 }' "$cache/index.tsv")
  bc=$(awk -v i=$idc '$1 == i && $2 == "guitar" { print $3 }' "$cache/index.tsv")
  mb=$(((ba + bc + 999999) / 1000000))
  send "budget	$mb"
  if wait_index $ida bass 10 no && in_index $ida guitar && in_index $idc guitar && [ ! -f "$cache/$ida.bass.m4a" ]; then
    echo "    $mb MB: removed $ida bass (1 play), kept $ida guitar (2 plays) and $idc guitar"
  else
    fail "budget $mb MB: $(cut -f1,2,4 "$cache/index.tsv" | tr '\t\n' ' ')"
  fi
  send "budget	1"
  if wait_index $ida guitar 10 no && in_index $idc guitar; then
    echo "    1 MB: removed $ida guitar, kept $idc guitar (the song being played)"
  else
    fail "budget 1 MB: $(cut -f1,2,4 "$cache/index.tsv" | tr '\t\n' ' ')"
  fi

  echo "  the heartbeat stops:"
  kill $beat 2>/dev/null
  wait $beat 2>/dev/null
  exec 3>&-
  t0=$(date +%s)
  while kill -0 $pid 2>/dev/null && [ $(($(date +%s) - t0)) -lt 30 ]; do sleep 0.2; done
  t=$(($(date +%s) - t0))
  if kill -0 $pid 2>/dev/null; then
    kill $pid
    fail "still running 30 s after the last heartbeat"
  elif [ $t -ge 9 ] && [ $t -le 12 ]; then
    echo "    ended $t s after the last heartbeat"
  else
    fail "ended $t s after the last heartbeat"
  fi
  echo "  the helper's log:"
  sed 's/^/    /' "$cache/rs-stems.log"
fi

echo "(d) a song at 48 kHz"
wem=$(wem_of youreallygotme)
ref="${data:-.}/songs/youreallygotme.coreml.no_guitar.wav"
if [ "${only#*d}" = "$only" ]; then
  skip "not asked for"
elif [ $have_data = 0 ] || [ $have_model = 0 ] || [ ! -f "$data/$wem" ] || [ ! -f "$ref" ]; then
  skip "needs RS_STEMS_TEST_DATA, RS_STEMS_CODEBOOKS, the helper and model, and youreallygotme"
else
  echo "  youreallygotme at 48 kHz without the guitar:"
  RS_STEMS_TEST_RATE=48000 RS_STEMS_CACHE="$out/cache" "$build/rs-stems" separate "$data/$wem" \
    "$(basename "$wem" .wem)" guitar "$out/youreallygotme.48k.pcm" | sed 's/^/    /' || fail "48 kHz"
  printf '    back to 44.1 kHz: '
  "$tool" resample "$out/youreallygotme.48k.pcm" 44100 "$out/youreallygotme.48k.wav" || fail "resampling back"
  printf '    '
  "$tool" stems "$data/songs/youreallygotme.wav" "$ref" "$out/youreallygotme.48k.wav" 30 || fail "48 kHz"
fi

[ $fails = 0 ] && echo "all run tests passed" || echo "$fails failed"
[ $fails = 0 ]
