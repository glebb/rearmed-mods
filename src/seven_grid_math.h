// Seven-String Guitar (seven.h): the fret geometry of the grid part, values in and values out (no game memory), so a
// test can run it. After rs7string's frets.cpp (github.com/SomeeName/rs7string, MIT, by SomeeName): Compute,
// FloorShift, GlowEnd, GlowEndAt and GlowStretch. seven_grid.c has the game's side.
//
// Every vector is guitar-local or world as the caller passes it; only differences count. a0 / a1 are the attach
// points of strings 0 and 1, u = a0 - a1 one string spacing towards string 6 (7, 8). `lanes` is how many strings the
// guitar has beyond the stock six (1 on 7 strings, 2 on 8).
#ifndef RS_SEVEN_GRID_MATH_H
#define RS_SEVEN_GRID_MATH_H
#include <math.h>

#define SEVEN_HALF_LEN 4.61f       // L: half the length of a fret bar's mesh (its bound radius is 4.638)
#define SEVEN_STOCK_WIDTH 8.04f    // five string spacings (5 x 1.608) and a margin on both sides, what the glow was made for

// The bars' extension. mid: bar 0's middle as the game places it. Along u (string 1 -> string 0 -> string 6), from
// string 0, the game's bar spans c -+ L; the new one keeps its end beyond string 5 and reaches `lanes` spacings plus
// the wider of the game's margins beyond string 0. shift: the middle's move, scale: k, d: the string spacing. 0 for
// a degenerate spacing or a lane count outside 1-2.
static inline int seven_bar_extend(const float a0[3], const float a1[3], const float mid[3], int lanes, float shift[3],
                                   float *scale, float *d) {
  float u[3], len2 = 0.0f;
  for (int i = 0; i < 3; i++) {
    u[i] = a0[i] - a1[i];
    len2 += u[i] * u[i];
  }
  const float len = sqrtf(len2);
  if (!(len > 1e-3f) || !(len < 1e3f) || lanes < 1 || lanes > 2) return 0;
  float c = 0.0f;
  for (int i = 0; i < 3; i++) c += (mid[i] - a0[i]) * u[i] / len;
  const float m0 = c + SEVEN_HALF_LEN;                       // margin beyond string 0
  const float m5 = (c - SEVEN_HALF_LEN) * -1.0f - 5.0f * len;  // margin beyond string 5 (at -5 spacings)
  const float margin = m0 > m5 ? m0 : m5;                    // the wider, now beyond the outer string
  const float top = (float)lanes * len + margin, bottom = c - SEVEN_HALF_LEN;
  const float along = 0.5f * (top + bottom) - c;
  *scale = 0.5f * (top - bottom) / SEVEN_HALF_LEN;
  for (int i = 0; i < 3; i++) shift[i] = along * u[i] / len;
  *d = len;
  return *scale > 0.0f && isfinite(*scale);
}

// The highway floor: b = a fret wire's bottom end. Beyond string 0 on string 6's side (inverted strings) it moves
// `lanes` string spacings further out: 1 and out = lanes * (a0 - a1); else 0 (the game's floor beyond string 5).
static inline int seven_floor_shift(const float a0[3], const float a1[3], const float b[3], int lanes, float out[3]) {
  float u[3], len2 = 0.0f, side = 0.0f;
  for (int i = 0; i < 3; i++) {
    u[i] = a0[i] - a1[i];
    len2 += u[i] * u[i];
    side += (b[i] - a0[i]) * u[i];
  }
  if (!(len2 > 1e-6f) || !(len2 < 1e6f) || !(side > 0.0f) || lanes < 1 || lanes > 2) return 0;
  for (int i = 0; i < 3; i++) out[i] = (float)lanes * u[i];
  return 1;
}

// The fret range glow: p = one end of a fret wire (world), w0 / w1 = strings 0 and 1 (world). An end beyond string
// 0 (string 6's side) moves `lanes` spacings further out, like the extended bar it lies on; the other end stays.
// 0 when p did not move.
static inline int seven_glow_end(const float w0[3], const float w1[3], int lanes, float p[3]) {
  float u[3], side = 0.0f;
  for (int i = 0; i < 3; i++) {
    u[i] = w0[i] - w1[i];
    side += (p[i] - w0[i]) * u[i];
  }
  if (lanes < 1 || lanes > 2 || !(side > 0.0f) || !isfinite(side)) return 0;
  for (int i = 0; i < 3; i++) p[i] += (float)lanes * u[i];
  return 1;
}

// The same on the floor's side, where the floor may or may not have been moved yet: an end beyond string 0 goes
// exactly `beyond` spacings past it (the stock margin plus the lanes), whatever it was before. 0 when p is not beyond
// string 0 or was already there.
static inline int seven_glow_end_at(const float w0[3], const float w1[3], float beyond, float p[3]) {
  float u[3], len2 = 0.0f, side = 0.0f;
  for (int i = 0; i < 3; i++) {
    u[i] = w0[i] - w1[i];
    len2 += u[i] * u[i];
    side += (p[i] - w0[i]) * u[i];
  }
  if (!(len2 > 1e-6f) || !(len2 < 1e6f) || !(side > 0.0f) || !isfinite(side) || !(beyond > 0.0f) || !(beyond < 8.0f))
    return 0;
  const float delta = beyond - side / len2;  // in spacings, along u
  if (fabsf(delta) < 1e-3f) return 0;
  for (int i = 0; i < 3; i++) p[i] += delta * u[i];
  return 1;
}

// The glow's box and bars are fixed-size effects made for the stock strings (SEVEN_STOCK_WIDTH plus the margin beyond
// them). The uniform scale that makes one span `spacings` string spacings of `spacing` plus `margin` (world units) on
// both sides: 1 when that is no wider (a piece too long is harmless), at most 3.
static inline float seven_glow_stretch(int spacings, float spacing, float margin) {
  const float m = isfinite(margin) && margin > 0.0f ? 2.0f * margin : 0.0f;
  const float k = ((float)spacings * spacing + m) / (SEVEN_STOCK_WIDTH + m);
  return isfinite(k) && k > 1.0f ? fminf(k, 3.0f) : 1.0f;
}
#endif
