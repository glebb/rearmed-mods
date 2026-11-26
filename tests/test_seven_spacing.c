// The string spacing's math (src/seven_spacing.h): the scale of a wanted spacing, the scaled attach points and the fret
// bars over spaced strings; and the strings part's pure rules (src/seven_strings.h): which counts a song asks for and
// when a guitar is rebuilt.
#include <math.h>

#include "check.h"
#include "seven_grid_math.h"
#include "seven_model.h"
#include "seven_spacing.h"
#include "seven_strings.h"

static int near(float a, float b) { return fabsf(a - b) < 2e-3f; }

int main(void) {
  // the scale
  CHECK(seven_spacing_scale(0.0f, 1.608f) == 1.0f);   // stock
  CHECK(near(seven_spacing_scale(1.34f, 1.608f), 1.34f / 1.608f));
  CHECK(seven_spacing_scale(1.34f, 0.0f) == 1.0f);    // no measure
  CHECK(seven_spacing_scale(NAN, 1.608f) == 1.0f);
  CHECK(seven_spacing_scale(100.0f, 1.608f) == SEVEN_SPACING_KMAX);
  CHECK(seven_spacing_scale(0.01f, 1.608f) == SEVEN_SPACING_KMIN);

  // a0 = 0, a1 = -d: u = +x, the stock six at 0, -d ... -5d, so the middle is at -2.5 d
  const float d = 1.608f, a0[3] = {0, 0, 0}, a1[3] = {-d, 0, 0};
  float c[3], p[3], q[3];
  seven_spacing_centre(a0, a1, c);
  CHECK(near(c[0], -2.5f * d) && c[1] == 0 && c[2] == 0);
  // k = 1 is the identity; the middle stays; the ends move by k
  const float k = 1.34f / d;
  p[0] = -5 * d, p[1] = 0, p[2] = 0;  // string 5
  seven_spacing_point(p, c, 1.0f, q);
  CHECK(near(q[0], p[0]));
  seven_spacing_point(c, c, k, q);
  CHECK(near(q[0], c[0]));
  seven_spacing_point(a0, c, k, q);  // string 0
  CHECK(near(q[0], c[0] + k * 2.5f * d));
  seven_spacing_point(p, c, k, q);   // string 5
  CHECK(near(q[0], c[0] - k * 2.5f * d));
  // the spaced distance of two strings is k d, and the extra string continues the row: 2 a0' - a1' = c + k (6th at +d)
  float q0[3], q1[3];
  seven_spacing_point(a0, c, k, q0);
  seven_spacing_point(a1, c, k, q1);
  CHECK(near(q0[0] - q1[0], 1.34f));
  const float x6 = q0[0] + (q0[0] - q1[0]);
  float s6[3] = {d, 0, 0};
  seven_spacing_point(s6, c, k, q);
  CHECK(near(q[0], x6));  // scaling the stock sixth lane == extending the scaled row

  // the bars
  {
    const float xm = 0.27f, mid[3] = {xm, 0, 0};
    float shift[3], scale, len;
    // k = 1: the same bar as the unspaced code
    float s1[3], sc1, l1;
    CHECK(seven_bar_extend(a0, a1, mid, 1, s1, &sc1, &l1));
    CHECK(seven_spacing_bar(a0, a1, mid, 1, 1.0f, SEVEN_HALF_LEN, shift, &scale, &len));
    CHECK(near(shift[0], s1[0]) && near(scale, sc1) && near(len, l1));
    CHECK(seven_bar_extend(a0, a1, mid, 2, s1, &sc1, &l1));
    CHECK(seven_spacing_bar(a0, a1, mid, 2, 1.0f, SEVEN_HALF_LEN, shift, &scale, &len));
    CHECK(near(shift[0], s1[0]) && near(scale, sc1));
    // spaced: strings at q0 (string 0) ... the bar keeps its margin beyond string 5 and reaches the extra lane + margin
    const float b0[3] = {q0[0], 0, 0}, b1[3] = {q1[0], 0, 0};
    CHECK(seven_spacing_bar(b0, b1, mid, 1, k, SEVEN_HALF_LEN, shift, &scale, &len));
    CHECK(near(len, 1.34f));
    const float x5 = q0[0] - 5 * 1.34f;  // string 5, spaced
    // the stock margins: beyond string 0 and beyond string 5 (measured on the stock spacing)
    const float m0 = xm + SEVEN_HALF_LEN - 0.0f, m5 = -5 * d - (xm - SEVEN_HALF_LEN);  // xm relative to string 0 here
    (void)m0;
    // the bar's end beyond string 5 sits m5 from string 5
    const float bottom = (xm + shift[0]) - scale * SEVEN_HALF_LEN, top = (xm + shift[0]) + scale * SEVEN_HALF_LEN;
    CHECK(near(bottom, x5 - m5));
    // and its top reaches one spaced string past string 0 and the wider margin
    const float wider = (xm + SEVEN_HALF_LEN) > m5 ? (xm + SEVEN_HALF_LEN) : m5;
    CHECK(near(top, q0[0] + 1.34f + wider));
    // degenerate
    CHECK(!seven_spacing_bar(a0, a0, mid, 1, k, SEVEN_HALF_LEN, shift, &scale, &len));
    CHECK(!seven_spacing_bar(a0, a1, mid, 3, k, SEVEN_HALF_LEN, shift, &scale, &len));
    CHECK(!seven_spacing_bar(a0, a1, mid, 1, 0.0f, SEVEN_HALF_LEN, shift, &scale, &len));
  }

  // the counts a song asks for (seven_strings.h)
  CHECK(seven_wanted_guitar(0, 0, 1, 1) == 6);     // no keys
  CHECK(seven_wanted_guitar(1, 0, 1, 1) == 7);     // Rs7String6
  CHECK(seven_wanted_guitar(1, 1, 1, 1) == 8);     // Rs7String7
  CHECK(seven_wanted_guitar(1, 1, 0, 1) == 7);     // eight strings not on: seven
  CHECK(seven_wanted_guitar(1, 1, 0, 0) == 6);
  CHECK(seven_wanted_guitar(0, 1, 1, 1) == 6);     // Rs7String7 alone is no chart
  CHECK(seven_wanted_bass(0, 0, 1) == 4);
  CHECK(seven_wanted_bass(1, 0, 1) == 5);
  CHECK(seven_wanted_bass(1, 1, 1) == 6);
  CHECK(seven_wanted_bass(1, 1, 0) == 4);          // bass not on
  CHECK(seven_wanted_bass(0, 1, 1) == 4);
  // the string count of an InstrumentInfo's loops: the build count, 4-8
  CHECK(seven_build_count(4) == 4 && seven_build_count(5) == 5 && seven_build_count(6) == 6);
  CHECK(seven_build_count(7) == 7 && seven_build_count(8) == 8);
  CHECK(seven_build_count(0) == 6 && seven_build_count(3) == 6 && seven_build_count(9) == 8);
  // the rebuild: song selection of player 0 (or 1 in co-op) with another count; a tuner holding a guitar never
  CHECK(seven_rebuild_target(SEVEN_ROLE_SONG, 0, 7, 6, 0, 0) == 7);
  CHECK(seven_rebuild_target(SEVEN_ROLE_SONG, 0, 6, 6, 0, 0) == 0);
  CHECK(seven_rebuild_target(SEVEN_ROLE_SONG, 0, 7, 6, 1, 0) == 0);
  CHECK(seven_rebuild_target(SEVEN_ROLE_SONG, 2, 7, 6, 0, 0) == 0);
  CHECK(seven_rebuild_target(SEVEN_ROLE_TUNER, 0, 7, 6, 0, 1) == 0);   // a song runs
  CHECK(seven_rebuild_target(SEVEN_ROLE_TUNER, 0, 7, 6, 0, 0) == 7);
  CHECK(seven_rebuild_target(SEVEN_ROLE_MODE, 0, 6, 8, 0, 0) == 6);    // a mode entry: back to stock
  CHECK(seven_rebuild_target(SEVEN_ROLE_MODE, 0, 7, 6, 0, 0) == 0);
  CHECK(seven_rebuild_target(SEVEN_ROLE_OTHER, 0, 7, 6, 0, 0) == 0);
  // a bass (stock far string 3): the centre is the middle of its four strings; the bar keeps the end at the G
  {
    const float a0[3] = {0, 0, 0}, a1[3] = {-2.68f, 0, 0}, mid[3] = {0.4f, 0, 0};
    float c[3], shift[3], scale, len;
    seven_spacing_centre_far(a0, a1, 3, c);
    CHECK(fabsf(c[0] + 1.5f * 2.68f) < 1e-3f);
    seven_spacing_centre_far(a0, a1, 5, c);
    CHECK(fabsf(c[0] + 2.5f * 2.68f) < 1e-3f);
    CHECK(seven_spacing_bar_far(a0, a1, mid, 1, 3, 0, 1.0f, SEVEN_HALF_LEN, shift, &scale, &len));
    CHECK(fabsf(0.4f + shift[0] - scale * SEVEN_HALF_LEN - (0.4f - SEVEN_HALF_LEN)) < 1e-3f);
    float scale6;
    CHECK(seven_spacing_bar_far(a0, a1, mid, 1, 3, 1, 1.0f, SEVEN_HALF_LEN, shift, &scale6, &len) && scale6 > scale);
    CHECK(!seven_spacing_bar_far(a0, a1, mid, 1, 3, 2, 1.0f, SEVEN_HALF_LEN, shift, &scale, &len));
  }
  // local co-op: player 1 rebuilds too (the song selection's, a mode entry back to stock); a third player never
  CHECK(seven_rebuild_target(SEVEN_ROLE_SONG, 1, 7, 6, 0, 0) == 7);
  CHECK(seven_rebuild_target(SEVEN_ROLE_SONG, 1, 6, 8, 0, 0) == 6);
  CHECK(seven_rebuild_target(SEVEN_ROLE_SONG, 1, 7, 7, 0, 0) == 0);
  CHECK(seven_rebuild_target(SEVEN_ROLE_SONG, 1, 7, 6, 1, 0) == 0);    // a tuner holds a guitar
  CHECK(seven_rebuild_target(SEVEN_ROLE_MODE, 1, 6, 8, 0, 0) == 6);
  CHECK(seven_rebuild_target_for(SEVEN_ROLE_SONG, 1, 5, 4, 0, 0, 4) == 5);  // player 1's bass
  CHECK(seven_rebuild_target_for(SEVEN_ROLE_MODE, 1, 4, 6, 0, 0, 4) == 4);
  CHECK(seven_rebuild_target_for(SEVEN_ROLE_SONG, 2, 5, 4, 0, 0, 4) == 0);
  // NormaliseSlot1: only a mode entry of player 0, with player 1 wide, and no tuner holding a guitar
  CHECK(seven_slot1_normalise(SEVEN_ROLE_MODE, 0, 1, 0) == 1);
  CHECK(seven_slot1_normalise(SEVEN_ROLE_MODE, 0, 0, 0) == 0);         // player 1 is stock already
  CHECK(seven_slot1_normalise(SEVEN_ROLE_MODE, 0, 1, 1) == 0);         // a tuner holds a guitar
  CHECK(seven_slot1_normalise(SEVEN_ROLE_MODE, 1, 1, 0) == 0);         // player 1's own store is its own rebuild
  CHECK(seven_slot1_normalise(SEVEN_ROLE_SONG, 0, 1, 0) == 0);
  CHECK(seven_slot1_normalise(SEVEN_ROLE_TUNER, 0, 1, 0) == 0);
  // string7_line: the strings an InstrumentInfo loads and shows; off, the stock ones only (the build count stays elsewhere)
  CHECK(seven_shown_count(7, 0, 1) == 7 && seven_shown_count(8, 0, 1) == 8 && seven_shown_count(6, 0, 1) == 6);
  CHECK(seven_shown_count(7, 0, 0) == 6 && seven_shown_count(8, 0, 0) == 6 && seven_shown_count(6, 0, 0) == 6);
  CHECK(seven_shown_count(5, 1, 1) == 5 && seven_shown_count(6, 1, 1) == 6 && seven_shown_count(4, 1, 1) == 4);
  CHECK(seven_shown_count(5, 1, 0) == 4 && seven_shown_count(6, 1, 0) == 4 && seven_shown_count(4, 1, 0) == 4);
  CHECK(seven_shown_count(0, 0, 0) == 6);  // no build count yet: the game's six
  return check_done("test_seven_spacing");
}
