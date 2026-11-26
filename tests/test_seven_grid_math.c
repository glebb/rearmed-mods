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
