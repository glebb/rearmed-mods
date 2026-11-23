// Pitch Shifter's shift for a song, from the song's tuning and the guitar's own (PitchShifterAuto). Nothing here
// touches the game (tests/test_pitch_auto.c).
#ifndef PITCH_AUTO_H
#define PITCH_AUTO_H

#include <math.h>
#include <stdlib.h>
#include <strings.h>

#include "rsmods_tuning.h"

// The semitones from standard an arrangement's guitar as a whole is at: where most of its strings are (a bass's
// first four), so a drop tuning counts as the tuning it drops from (Eb Drop Db as Eb). Of two as common, the one
// nearer `guitar`, then the higher: DADGAD is a guitar in E with three strings down.
static inline int pitch_auto_tuning(const rsm_arrangement *a, int guitar) {
  int strings = rsm_is_bass(a) ? 4 : 6, best = 0, most = 0;
  for (int i = 0; i < strings; i++) {
    int t = a->tuning[i], count = 0;
    for (int k = 0; k < strings; k++) count += a->tuning[k] == t;
    int nearer = abs(t - guitar) < abs(best - guitar) || (abs(t - guitar) == abs(best - guitar) && t > best);
    if (count > most || (count == most && nearer)) best = t, most = count;
  }
  return best;
}

// The arrangement the shift follows: the instrument's (bass or not) lead, or its first; any, when it has none
static inline const rsm_arrangement *pitch_auto_arrangement(const rsm_arrangement *arr, int n, int bass) {
  const rsm_arrangement *first = NULL;
  for (int i = 0; i < n; i++) {
    if (rsm_is_bass(&arr[i]) != (bass != 0)) continue;
    if (!strncasecmp(arr[i].name, "Lead", 4)) return &arr[i];
    if (!first) first = &arr[i];
  }
  return first ? first : n > 0 ? &arr[0] : NULL;
}

// The arrangement with these six strings' tuning (the instrument's first, then any's), or NULL
static inline const rsm_arrangement *pitch_auto_with(const rsm_arrangement *arr, int n, int bass, const int tuning[6]) {
  for (int pass = 0; pass < 2; pass++)
    for (int i = 0; i < n; i++) {
      if (!pass && rsm_is_bass(&arr[i]) != (bass != 0)) continue;
      int same = 1;
      for (int s = 0; s < 6; s++) same &= arr[i].tuning[s] == tuning[s];
      if (same) return &arr[i];
    }
  return NULL;
}

// The shift that plays the song on a guitar tuned `guitar` semitones from standard: whole semitones (within `range`
// either way) and the A the guitar is to sound at, in hertz. The song's CentOffset is in both: its whole semitones
// in the first (a song in E at -100 cents is in E flat), the rest in the A (-50 cents is A427), and octaves in
// neither (a bass an octave down is at -1200). 0 without an arrangement.
static inline int pitch_auto_shift(const rsm_arrangement *arr, int n, int bass, int guitar, int range, int *semitones,
                                   int *a4) {
  const rsm_arrangement *a = pitch_auto_arrangement(arr, n, bass);
  if (!a) return 0;
  double off = a->cents / 100;
  off -= 12 * round(off / 12);
  long whole = lround(off);
  long s = pitch_auto_tuning(a, guitar) - guitar + whole;
  *semitones = (int)(s < -range ? -range : s > range ? range : s);
  *a4 = (int)lround(440 * pow(2, (off - (double)whole) / 12));
  return 1;
}

#endif
