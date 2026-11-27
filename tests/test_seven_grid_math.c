// The fret geometry of Seven-String Guitar's grid part (src/seven_grid_math.h): the bar extension, the highway floor,
// and the fret range glow's ends and scale, for 7 and 8 strings in either string order
#include <math.h>

#include "check.h"
#include "seven_grid_math.h"

static int near(float a, float b) { return fabsf(a - b) < 1e-3f; }

int main(void) {
  const float d = 1.608f;
  // Normal order along x: string i at x = -i d, so u = a0 - a1 = (+d, 0, 0) points towards string 6
  const float a0[3] = {0, 0, 0}, a1[3] = {-d, 0, 0};
  // A bar with the stock margin 1.97 beyond string 5 and 0.32 beyond string 0, centred at 0.27 (as seen on a stock guitar)
  {
    const float c = 0.27f, mid[3] = {c, 0, 0};
    float shift[3], k, len;
    CHECK(seven_bar_extend(a0, a1, mid, 1, shift, &k, &len));
    CHECK(near(len, d));
    // the bar's string-5 end (c - L) stays: new centre - new half = c - L
    CHECK(near(c + shift[0] - k * SEVEN_HALF_LEN, c - SEVEN_HALF_LEN));
    // its other end reaches 1 spacing beyond string 0 plus the wider margin (c + L)
    CHECK(near(c + shift[0] + k * SEVEN_HALF_LEN, 1.0f * d + c + SEVEN_HALF_LEN));
    CHECK(seven_bar_extend(a0, a1, mid, 2, shift, &k, &len));
    CHECK(near(c + shift[0] + k * SEVEN_HALF_LEN, 2.0f * d + c + SEVEN_HALF_LEN));  // 8 strings: one more spacing
    CHECK(!seven_bar_extend(a0, a1, mid, 0, shift, &k, &len));
    CHECK(!seven_bar_extend(a0, a1, mid, 3, shift, &k, &len));
    CHECK(!seven_bar_extend(a0, a0, mid, 1, shift, &k, &len));  // a degenerate spacing
  }
  // The floor: b on string 6's side beyond string 0 (inverted strings) moves by lanes spacings; beyond string 5 never
  {
    float out[3] = {9, 9, 9};
    const float inverted[3] = {1.97f, 0.0f, 0.0f}, normal[3] = {-5 * d - 1.97f, 0.0f, 0.0f};
    CHECK(seven_floor_shift(a0, a1, inverted, 1, out) && near(out[0], d) && near(out[1], 0));
    CHECK(seven_floor_shift(a0, a1, inverted, 2, out) && near(out[0], 2 * d));
    CHECK(!seven_floor_shift(a0, a1, normal, 1, out));
    CHECK(!seven_floor_shift(a0, a1, inverted, 0, out));
    CHECK(!seven_floor_shift(a0, a0, inverted, 1, out));
  }
  // The glow's ends: beyond string 0 they move by the lanes, the others stay
  {
    float p[3] = {0.32f, 0.1f, 0.2f}, q[3] = {-5 * d - 1.97f, 0.1f, 0.2f};
    CHECK(seven_glow_end(a0, a1, 1, p) && near(p[0], 0.32f + d) && near(p[1], 0.1f) && near(p[2], 0.2f));
    CHECK(!seven_glow_end(a0, a1, 1, q) && near(q[0], -5 * d - 1.97f));
    float r[3] = {0.32f, 0, 0};
    CHECK(seven_glow_end(a0, a1, 2, r) && near(r[0], 0.32f + 2 * d));
    CHECK(!seven_glow_end(a0, a1, 3, r));
  }
  // GlowEndAt puts an end exactly `beyond` spacings past string 0, wherever it was; and says so only when it moved
  {
    float p[3] = {1.97f, 0, 0};  // the floor's end, not yet moved: 1.97 / d spacings beyond string 0
    const float margin = 1.97f / d;
    CHECK(seven_glow_end_at(a0, a1, margin + 1.0f, p) && near(p[0], (margin + 1.0f) * d));
    CHECK(!seven_glow_end_at(a0, a1, margin + 1.0f, p));  // already there
    float far[3] = {-3.0f, 0, 0};                         // not beyond string 0
    CHECK(!seven_glow_end_at(a0, a1, 2.0f, far) && near(far[0], -3.0f));
    float moved[3] = {1.97f + d, 0, 0};  // the floor's end after the floor moved (8 strings: two): already there, or lands there
    CHECK(!seven_glow_end_at(a0, a1, margin + 1.0f, moved) && near(moved[0], 1.97f + d));
    float eight[3] = {1.97f + d, 0, 0};
    CHECK(seven_glow_end_at(a0, a1, margin + 2.0f, eight) && near(eight[0], 1.97f + 2 * d));
    CHECK(!seven_glow_end_at(a0, a1, 9.0f, p));           // out of range
  }
  // A bass: the stock far string is the G (3); a 5-string bass has the low B beyond string 0, a 6-string the high C beyond the G
  {
    const float D = 2.68f, b0[3] = {0, 0, 0}, b1[3] = {-D, 0, 0}, c = 0.4f, mid[3] = {c, 0, 0};
    float shift[3], k, len;
    CHECK(seven_bar_extend_far(b0, b1, mid, 1, 3, 0, shift, &k, &len) && near(len, D));
    CHECK(near(c + shift[0] - k * SEVEN_HALF_LEN, c - SEVEN_HALF_LEN));  // the end at the G stays
    CHECK(near(c + shift[0] + k * SEVEN_HALF_LEN, D + c + SEVEN_HALF_LEN));
    float k6;
    CHECK(seven_bar_extend_far(b0, b1, mid, 1, 3, 1, shift, &k6, &len));
    CHECK(near(c + shift[0] - k6 * SEVEN_HALF_LEN, c - SEVEN_HALF_LEN - D));  // the C: one spacing further out
    CHECK(k6 > k);
    CHECK(!seven_bar_extend_far(b0, b1, mid, 1, 3, 2, shift, &k, &len));
    CHECK(!seven_bar_extend_far(b0, b1, mid, 1, 6, 0, shift, &k, &len));
    // the floor: beyond string 0 it moves by the near lanes; beyond the G (b on the far side) by the far lanes
    float out[3];
    const float near_b[3] = {0.5f, 0, 0}, far_b[3] = {-3 * D - 0.5f, 0, 0};
    CHECK(seven_floor_move(b0, b1, near_b, 1, 3, 1, out) == 1 && near(out[0], D));
    CHECK(seven_floor_move(b0, b1, far_b, 1, 3, 1, out) == 1 && near(out[0], -D));
    CHECK(seven_floor_move(b0, b1, far_b, 1, 3, 0, out) == 0);  // a 5-string bass: nothing beyond the G
    CHECK(seven_floor_move(b0, b0, near_b, 1, 3, 1, out) == -1);
    // the chord frame: a 6-string bass, floor beyond string 0 (moved one lane): spans the C and the B
    seven_frame_fit fit;
    CHECK(seven_frame_fit_for(b0, b1, near_b, 1, 6, 3, 1, &fit) && fit.scale > 1.0f && near(fit.spacing, D));
    CHECK(near(fit.margin, 0.5f));
    CHECK(!seven_frame_fit_for(b0, b1, near_b, 0, 6, 6, 0, &fit));
  }
  // A guitar's chord frame (the chord part's B2): its origin edge on the floor, covering lanes 0 to the last string
  {
    const float m = 0.5f, inverted[3] = {m, 0, 0}, normal[3] = {-5 * d - m, 0, 0};
    const float h = 5 * d + 2 * m;
    seven_frame_fit fit;
    float out[3];
    // inverted strings: the floor moved by the added lanes (7 strings one, 8 two): no shift, the far edge out past string 5
    CHECK(seven_floor_move(a0, a1, inverted, 2, 5, 0, out) == 2 && near(out[0], 2 * d));
    CHECK(seven_frame_fit_for(a0, a1, inverted, 1, 6, 5, 0, &fit) && near(fit.shift, 0) && near(fit.scale, (6 * d + 2 * m) / h));
    CHECK(seven_frame_fit_for(a0, a1, inverted, 2, 7, 5, 0, &fit) && near(fit.shift, 0) && near(fit.scale, (7 * d + 2 * m) / h));
    CHECK(near(fit.origin, m + 2 * d) && near(fit.margin, m) && near(fit.spacing, d));
    // an 8-string frame taken as moved by one spacing (the old chord part's guess) is a spacing off: what B2 must not do
    CHECK(seven_frame_fit_for(a0, a1, inverted, 1, 7, 5, 0, &fit) && near(fit.shift, -d));
    // the floor not moved yet (pending): the origin edge moves out to cover the added lanes
    CHECK(seven_frame_fit_for(a0, a1, inverted, 0, 6, 5, 0, &fit) && near(fit.shift, -d) && near(fit.scale, (6 * d + 2 * m) / h));
    // normal order: the floor is beyond string 5 and stays; the far edge reaches past string 0 by the added lanes
    CHECK(seven_floor_move(a0, a1, normal, 1, 5, 0, out) == 0);
    for (int top = 6; top <= 7; top++)
      CHECK(seven_frame_fit_for(a0, a1, normal, 0, top, 5, 0, &fit) && near(fit.shift, 0) &&
            near(fit.scale, ((float)top * d + 2 * m) / h));
    // a 5-string bass, inverted: the low B's lane (the floor moved one), nothing beyond the G
    const float D = 2.68f, b0[3] = {0, 0, 0}, b1[3] = {-D, 0, 0}, bb[3] = {m, 0, 0};
    CHECK(seven_frame_fit_for(b0, b1, bb, 1, 6, 3, 0, &fit) && near(fit.shift, 0) &&
          near(fit.scale, (4 * D + 2 * m) / (3 * D + 2 * m)));
    // a 6-string bass in normal order: the floor beyond the G moved one (the C), the far edge past the low B
    const float bf[3] = {-3 * D - m, 0, 0};
    CHECK(seven_floor_move(b0, b1, bf, 1, 3, 1, out) == 1 && near(out[0], -D));
    CHECK(seven_frame_fit_for(b0, b1, bf, 1, 6, 3, 1, &fit) && near(fit.shift, 0) &&
          near(fit.scale, (5 * D + 2 * m) / (3 * D + 2 * m)));
  }
  // The glow's scale: 1 for what the stock width covers, wider strings stretch it, never past 3
  {
    CHECK(seven_glow_stretch(5, d, 0.0f) == 1.0f);  // 5 spacings of 1.608 = the stock width 8.04
    CHECK(near(seven_glow_stretch(6, d, 0.0f), 6 * d / SEVEN_STOCK_WIDTH));
    const float m = 0.32f * d;
    CHECK(near(seven_glow_stretch(7, d, m), (7 * d + 2 * m) / (SEVEN_STOCK_WIDTH + 2 * m)));
    CHECK(seven_glow_stretch(7, d, m) > seven_glow_stretch(6, d, m));
    CHECK(seven_glow_stretch(40, d, 0.0f) == 3.0f);
    CHECK(seven_glow_stretch(6, NAN, 0.0f) == 1.0f);
  }
  return check_done("seven_grid_math");
}
