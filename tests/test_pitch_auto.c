// Pitch Shifter's shift for a song (src/pitch_auto.h)
#include <stdio.h>

#include "check.h"
#include "pitch_auto.h"

static rsm_arrangement arr(const char *name, int a, int b, int c, int d, int e, int f, double cents) {
  rsm_arrangement r = {.tuning = {a, b, c, d, e, f}, .cents = cents};
  snprintf(r.name, sizeof r.name, "%s", name);
  return r;
}

static int g_semitones, g_a4;
static int shift(rsm_arrangement a, int bass, int guitar) {
  g_semitones = g_a4 = -999;
  return pitch_auto_shift(&a, 1, bass, guitar, 12, &g_semitones, &g_a4);
}

int main(void) {
  // a guitar in E: nothing for E standard, a semitone down for E flat, two for D
  CHECK(shift(arr("Lead", 0, 0, 0, 0, 0, 0, 0), 0, 0) && g_semitones == 0 && g_a4 == 440);
  CHECK(shift(arr("Lead", -1, -1, -1, -1, -1, -1, 0), 0, 0) && g_semitones == -1 && g_a4 == 440);
  CHECK(shift(arr("Rhythm", -2, -2, -2, -2, -2, -2, 0), 0, 0) && g_semitones == -2);
  // a guitar in E flat: nothing for E flat, a semitone up for E
  CHECK(shift(arr("Lead", -1, -1, -1, -1, -1, -1, 0), 0, -1) && g_semitones == 0);
  CHECK(shift(arr("Lead", 0, 0, 0, 0, 0, 0, 0), 0, -1) && g_semitones == 1);

  // a drop tuning is the tuning it drops from: Drop D is E, Eb Drop Db is E flat
  CHECK(shift(arr("Lead", -2, 0, 0, 0, 0, 0, 0), 0, 0) && g_semitones == 0);
  CHECK(shift(arr("Lead", -3, -1, -1, -1, -1, -1, 0), 0, 0) && g_semitones == -1);
  // open tunings: where most strings are (Open G: three at E), and of two as common the guitar's own (DADGAD)
  CHECK(shift(arr("Lead", -2, -2, 0, 0, 0, -2, 0), 0, 0) && g_semitones == 0);
  CHECK(shift(arr("Lead", -2, 0, 0, 0, -2, -2, 0), 0, 0) && g_semitones == 0);
  CHECK(shift(arr("Lead", -2, 0, 0, 0, -2, -2, 0), 0, -2) && g_semitones == 0);
  // Open E has three strings up: still a guitar in E
  CHECK(shift(arr("Lead", 0, 2, 2, 1, 0, 0, 0), 0, 0) && g_semitones == 0);

  // a bass counts by its four strings: the top two of the six stay at E in its manifest
  CHECK(shift(arr("Bass", -1, -1, -1, -1, 0, 0, 0), 1, 0) && g_semitones == -1);
  CHECK(shift(arr("Bass", -2, 0, 0, 0, 0, 0, 0), 1, 0) && g_semitones == 0);
  CHECK(shift(arr("Bass", -2, -2, 0, 0, 0, 0, 0), 1, 0) && g_semitones == 0);

  // an extended bass (Rs7String4 / 5: its low B and high C in slots 4 / 5, from their own standard) by its four stock
  // strings too: a 5-string in standard, one with its low B dropped, a 6-string a tone down throughout
  rsm_arrangement ext = arr("Bass", 0, 0, 0, 0, 0, 0, 0);
  ext.extra = 1;
  CHECK(pitch_auto_shift(&ext, 1, 1, 0, 12, &g_semitones, &g_a4) && g_semitones == 0);
  ext = arr("Bass", 0, 0, 0, 0, -5, 0, 0), ext.extra = 1;
  CHECK(pitch_auto_shift(&ext, 1, 1, 0, 12, &g_semitones, &g_a4) && g_semitones == 0);
  ext = arr("Bass", -2, -2, -2, -2, -2, -2, 0), ext.extra = 2;
  CHECK(pitch_auto_shift(&ext, 1, 1, 0, 12, &g_semitones, &g_a4) && g_semitones == -2);
  // a 7- or 8-string guitar's added strings are not in the six (Rs7String6 / 7): a B standard 7-string and an F#
  // standard 8-string are a guitar in E, an 8-string a semitone down is at -1
  rsm_arrangement g = arr("Lead", 0, 0, 0, 0, 0, 0, 0);
  g.extra = 1;
  CHECK(pitch_auto_shift(&g, 1, 0, 0, 12, &g_semitones, &g_a4) && g_semitones == 0);
  g = arr("Lead", -1, -1, -1, -1, -1, -1, 0), g.extra = 2;
  CHECK(pitch_auto_shift(&g, 1, 0, 0, 12, &g_semitones, &g_a4) && g_semitones == -1);

  // off concert pitch: the A, by the cents (Castles Made of Sand, E Standard at A428)
  CHECK(shift(arr("Lead", 0, 0, 0, 0, 0, 0, -48), 0, 0) && g_semitones == 0 && g_a4 == 428);
  CHECK(shift(arr("Lead", -1, -1, -1, -1, -1, -1, 20), 0, 0) && g_semitones == -1 && g_a4 == 445);
  // whole semitones of the cents are semitones: E at -100 cents is E flat, and at -130 E flat with the A at 432
  CHECK(shift(arr("Lead", 0, 0, 0, 0, 0, 0, -100), 0, 0) && g_semitones == -1 && g_a4 == 440);
  CHECK(shift(arr("Lead", 0, 0, 0, 0, 0, 0, -130), 0, 0) && g_semitones == -1 && g_a4 == 432);
  // an octave of cents is none (a bass an octave down), with what is over it
  CHECK(shift(arr("Bass", 0, 0, 0, 0, 0, 0, -1200), 1, 0) && g_semitones == 0 && g_a4 == 440);
  CHECK(shift(arr("Lead", 0, 0, 0, 0, 0, 0, -1300), 0, 0) && g_semitones == -1 && g_a4 == 440);

  // no further than the shifter goes
  CHECK(shift(arr("Lead", -14, -14, -14, -14, -14, -14, 0), 0, 0) && g_semitones == -12);
  CHECK(shift(arr("Lead", 5, 5, 5, 5, 5, 5, 0), 0, -12) && g_semitones == 12);

  // which arrangement: the instrument's lead, or its first; any when it has none
  rsm_arrangement song[] = {arr("Bass", -2, -2, -2, -2, 0, 0, 0), arr("Rhythm", -3, -1, -1, -1, -1, -1, 0),
                            arr("Lead", 0, 0, 0, 0, 0, 0, 0)};
  CHECK(pitch_auto_arrangement(song, 3, 0) == &song[2]);
  CHECK(pitch_auto_arrangement(song, 3, 1) == &song[0]);
  CHECK(pitch_auto_arrangement(song, 2, 0) == &song[1]);
  CHECK(pitch_auto_arrangement(song, 1, 0) == &song[0]);
  CHECK(pitch_auto_arrangement(song, 0, 0) == NULL);
  CHECK(pitch_auto_shift(song, 3, 1, 0, 12, &g_semitones, &g_a4) && g_semitones == -2);
  CHECK(pitch_auto_shift(song, 2, 0, 0, 12, &g_semitones, &g_a4) && g_semitones == -1);
  CHECK(!pitch_auto_shift(song, 0, 0, 0, 12, &g_semitones, &g_a4));

  // the arrangement a tuner's six strings are: the instrument's first, then any's
  int d[6] = {-2, -2, -2, -2, 0, 0}, drop[6] = {-3, -1, -1, -1, -1, -1}, none[6] = {1, 1, 1, 1, 1, 1};
  CHECK(pitch_auto_with(song, 3, 1, d) == &song[0]);
  CHECK(pitch_auto_with(song, 3, 0, d) == &song[0]);
  CHECK(pitch_auto_with(song, 3, 0, drop) == &song[1]);
  CHECK(pitch_auto_with(song, 3, 0, none) == NULL);
  rsm_arrangement twins[] = {arr("Bass", 0, 0, 0, 0, 0, 0, 0), arr("Lead", 0, 0, 0, 0, 0, 0, 0)};
  int e[6] = {0};
  CHECK(pitch_auto_with(twins, 2, 0, e) == &twins[1]);
  CHECK(pitch_auto_with(twins, 2, 1, e) == &twins[0]);

  return check_done("pitch_auto");
}
