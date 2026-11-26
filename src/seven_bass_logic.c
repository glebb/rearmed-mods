// Seven-String Guitar (seven.h): extended bass, the pure logic (seven_bass.h). After rs7string's src/features/bass/bass.cpp
// (github.com/SomeeName/rs7string, MIT, by SomeeName).
#include "seven_bass.h"

int seven_bass_extra_base(unsigned string, unsigned strings) {
  if (strings > 6 || string < 4 || string >= strings) return SEVEN_BASS_NO_BASE;
  return string == 4 ? 23 : 48;
}

unsigned seven_bass_song_strings(int has_block, int block_instrument, int block_strings, int det_instrument) {
  if (!has_block || block_instrument != SEVEN_BASS_BLOCK_INSTRUMENT || det_instrument != SEVEN_DET_BASS) return 0;
  if (block_strings < 5 || block_strings > 6) return 0;
  return (unsigned)block_strings;
}

int seven_bass_lowest(int stock, const int16_t tuning[6], unsigned strings) {
  int low = stock;
  for (unsigned s = 4; s < 6; s++) {
    const int base = seven_bass_extra_base(s, strings);
    if (base != SEVEN_BASS_NO_BASE && base + tuning[s] < low) low = base + tuning[s];
  }
  return low;
}

static float maxf(float a, float b) { return a > b ? a : b; }

int seven_bass_hold_bend(seven_bend_hold *h, const void *note, int ok, float *semis, float now) {
  if (h->note != note || (h->valid && now < h->time)) {  // a new note, or the clock went back (restart)
    *h = (seven_bend_hold){0};
    h->note = note;
    h->low_since = -1.0f;
    h->gap_since = -1.0f;
  }
  h->reads++;
  h->time = now;
  if (!(ok && *semis >= 0.0f)) {  // no pitch this frame: keep the held bend
    h->low_since = -1.0f;         // a missing reading cannot establish a sustained release
    if (!h->valid) return ok;
    h->missed++;
    h->gap_since = h->gap_since < 0.0f ? now : h->gap_since;
    h->longest_gap = maxf(h->longest_gap, now - h->gap_since);
    *semis = h->semis;
    return 1;
  }
  h->gap_since = -1.0f;
  if (h->valid && *semis < h->semis - SEVEN_BEND_DROP) {  // well below the held bend: a dropout until it lasts
    h->low_since = h->low_since < 0.0f ? now : h->low_since;
    if (now - h->low_since < SEVEN_BEND_RELEASE_SEC) {
      h->low++;
      *semis = h->semis;
      return 1;
    }
  }
  h->semis = *semis;
  h->peak = maxf(h->peak, *semis);
  h->low_since = -1.0f;
  h->valid = 1;
  return 1;
}
