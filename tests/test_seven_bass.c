// Extended bass (src/seven_bass_logic.c, seven_model.c's setups): ports of rs7string's tests/bass
#include <math.h>
#include <string.h>

#include "../src/seven_bass.h"
#include "../src/seven_model.h"
#include "check.h"

static int hold(seven_bend_hold *h, const void *note, int ok, float *v, float now) {
  return seven_bass_hold_bend(h, note, ok, v, now);
}

int main(void) {
  // the song's strings need a bass block on a bass detector
  CHECK(seven_bass_song_strings(1, 1, 5, 2) == 5);
  CHECK(seven_bass_song_strings(1, 1, 6, 2) == 6);
  CHECK(seven_bass_song_strings(0, 1, 5, 2) == 0);  // no block: a stock 4-string bass
  CHECK(seven_bass_song_strings(1, 0, 7, 2) == 0);  // a guitar block
  CHECK(seven_bass_song_strings(1, 1, 5, 1) == 0);  // a guitar detector
  CHECK(seven_bass_song_strings(1, 1, 4, 2) == 0);
  CHECK(seven_bass_song_strings(1, 1, 7, 2) == 0);

  // extra bases and the lowest open note
  CHECK(seven_bass_extra_base(4, 5) == 23);
  CHECK(seven_bass_extra_base(5, 5) == SEVEN_BASS_NO_BASE);  // five strings: slot 5 is padding
  CHECK(seven_bass_extra_base(5, 6) == 48);
  CHECK(seven_bass_extra_base(4, 0) == SEVEN_BASS_NO_BASE);
  CHECK(seven_bass_extra_base(3, 6) == SEVEN_BASS_NO_BASE);
  CHECK(seven_bass_extra_base(6, 6) == SEVEN_BASS_NO_BASE);
  CHECK(seven_bass_extra_base(4, 7) == SEVEN_BASS_NO_BASE);
  // the model's table agrees with the bases
  CHECK(seven_bass_open[4] == seven_bass_extra_base(4, 5) && seven_bass_open[5] == seven_bass_extra_base(5, 6));
  {
    const int16_t standard[6] = {0, 0, 0, 0, 0, 0}, drop_a[6] = {-2, 0, 0, 0, -2, 0}, high[6] = {0, 0, 0, 0, 0, -30};
    CHECK(seven_bass_lowest(28, standard, 5) == 23);  // B0
    CHECK(seven_bass_lowest(26, drop_a, 6) == 21);    // A0
    CHECK(seven_bass_lowest(28, standard, 0) == 28);
    CHECK(seven_bass_lowest(28, high, 6) == 18);  // C tuned below B: the lowest of both
  }

  // the floor that follows the lowest open note (seven_pitch.h's rule, rsxrp::Floor): B0 and G0 tunings are heard
  {
    const int stock = 24, min = 18;
    CHECK((23 >= stock ? stock : (23 - 1 > min ? 23 - 1 : min)) == 22);
    CHECK((20 - 1 > min ? 20 - 1 : min) == 19);
    CHECK((18 - 1 > min ? 18 - 1 : min) == 18);
  }

  // the bend hold bridges dropouts of the low B
  {
    seven_bend_hold h;
    memset(&h, 0, sizeof h);
    int note = 0;
    float v = 1.6f;
    CHECK(hold(&h, &note, 1, &v, 10.00f) && v == 1.6f);
    v = -29.6f;  // the tracker lost the pitch (-1 - expected)
    CHECK(hold(&h, &note, 1, &v, 10.02f) && v == 1.6f);
    CHECK(hold(&h, &note, 0, &v, 10.04f) && v == 1.6f);  // no tracked note this frame
    v = 0.1f;                                            // back to the unbent pitch for one frame
    CHECK(hold(&h, &note, 1, &v, 10.06f) && v == 1.6f);
    v = 1.2f;  // a real move down within SEVEN_BEND_DROP
    CHECK(hold(&h, &note, 1, &v, 10.08f) && v == 1.2f);
    v = 2.0f;  // rises are taken at once
    CHECK(hold(&h, &note, 1, &v, 10.10f) && v == 2.0f);
  }
  // ... over a long dropout
  {
    seven_bend_hold h;
    memset(&h, 0, sizeof h);
    int note = 0;
    float v = 1.0f;
    CHECK(hold(&h, &note, 1, &v, 3.0f) && v == 1.0f);
    for (float t = 3.02f; t < 5.0f; t += 0.02f) {  // a decaying low B: two seconds without a pitch
      v = -1.0f;
      CHECK(hold(&h, &note, t < 4.0f, &v, t) && v == 1.0f);
    }
    CHECK(h.missed > 90 && h.longest_gap > 1.9f);
  }
  // ... lets a lasting release through
  {
    seven_bend_hold h;
    memset(&h, 0, sizeof h);
    int note = 0;
    float v = 2.0f;
    CHECK(hold(&h, &note, 1, &v, 5.0f));
    v = 0.0f;
    CHECK(hold(&h, &note, 1, &v, 5.1f) && v == 2.0f);
    v = 0.0f;
    CHECK(hold(&h, &note, 1, &v, 5.15f) && v == 2.0f && h.low == 2);
    v = 0.0f;  // still down SEVEN_BEND_RELEASE_SEC after it fell: the release is real
    CHECK(hold(&h, &note, 1, &v, 5.1f + SEVEN_BEND_RELEASE_SEC + 0.01f) && v == 0.0f);
    v = -1.0f;
    CHECK(hold(&h, &note, 0, &v, 6.0f) && v == 0.0f);  // a dropout now keeps the released string down
  }
  // ... starts over for a new note
  {
    seven_bend_hold h;
    memset(&h, 0, sizeof h);
    int a = 0, b = 0;
    float v = 2.0f;
    CHECK(hold(&h, &a, 1, &v, 1.0f));
    v = -1.0f;
    CHECK(!hold(&h, &b, 0, &v, 1.01f) && v == -1.0f);  // nothing of note a carries over
    v = 0.3f;
    CHECK(hold(&h, &b, 1, &v, 1.02f) && v == 0.3f);
    v = -1.0f;
    CHECK(hold(&h, &b, 1, &v, 0.5f) && v == -1.0f);  // the clock went back (restart): no hold
  }
  // a release needs continuous low readings (rs7string's test_bend_release)
  for (int ok = 0; ok <= 1; ok++) {
    seven_bend_hold h;
    memset(&h, 0, sizeof h);
    int note = 0;
    float v = 2.0f;
    CHECK(hold(&h, &note, 1, &v, 1.0f));
    v = 0.0f;
    CHECK(hold(&h, &note, 1, &v, 1.02f) && v == 2.0f);
    v = -1.0f;
    CHECK(hold(&h, &note, ok, &v, 2.0f) && v == 2.0f);
    v = 0.0f;
    CHECK(hold(&h, &note, 1, &v, 2.02f) && v == 2.0f);
    v = 0.0f;
    CHECK(hold(&h, &note, 1, &v, 2.10f) && v == 2.0f);
    v = 0.0f;
    CHECK(hold(&h, &note, 1, &v, 2.20f) && v == 0.0f);
  }

  // the tuner's order of an extended bass (seven_tuner.c's put_count: seven_slot_of by rank): the low B first, the high C last
  {
    static const int want5[5] = {4, 0, 1, 2, 3}, want6[6] = {4, 0, 1, 2, 3, 5};
    for (int k = 0; k < 5; k++) CHECK(seven_slot_of(SEVEN_BASS, 5, k) == want5[k]);
    for (int k = 0; k < 6; k++) CHECK(seven_slot_of(SEVEN_BASS, 6, k) == want6[k]);
    for (int k = 0; k < 4; k++) CHECK(seven_slot_of(SEVEN_BASS, 4, k) == k);
    // the guitar's: 6,0..5 (seven) and 7,6,0..5 (eight)
    CHECK(seven_slot_of(SEVEN_GUITAR, 7, 0) == 6 && seven_slot_of(SEVEN_GUITAR, 7, 1) == 0);
    CHECK(seven_slot_of(SEVEN_GUITAR, 8, 0) == 7 && seven_slot_of(SEVEN_GUITAR, 8, 1) == 6 && seven_slot_of(SEVEN_GUITAR, 8, 2) == 0);
  }

  // the players' setups, per detector (co-op)
  {
    int bass_det = 0, guitar_det = 0, third = 0;
    seven_bass_clear();
    seven_bass_setup(&bass_det, SEVEN_DET_BASS, 6, 1000);
    seven_bass_setup(&guitar_det, 1, 0, 1005);  // player 2's guitar right after: the bass keeps its mode
    CHECK(seven_bass_strings_of(&bass_det) == 6 && seven_bass_strings_of(&guitar_det) == 0);
    CHECK(seven_bass_max_strings() == 6 && seven_bass_any_guitar());
    seven_bass_setup(&bass_det, SEVEN_DET_BASS, 5, 9000);  // next song, the bass player alone: the old setup goes
    CHECK(seven_bass_strings_of(&bass_det) == 5 && !seven_bass_any_guitar());
    seven_bass_setup(&bass_det, SEVEN_DET_BASS, 0, 9500);  // a stock bass on the same detector clears it
    CHECK(seven_bass_max_strings() == 0);
    seven_bass_clear();
    (void)third;
  }
  return check_done("seven_bass");
}
