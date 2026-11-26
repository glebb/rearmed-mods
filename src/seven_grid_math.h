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
// string 0, the game's bar spans c -+ L; the new one keeps its end beyond the stock far string and reaches `lanes`
// spacings plus the wider of the game's margins beyond string 0. shift: the middle's move, scale: k, d: the string
// spacing. `far` is the stock far string (5 on a guitar, 3, the G, on a bass) and `far_lanes` the strings beyond it (1:
// a 6-string bass's high C): then the far end moves that many spacings out. 0 for a degenerate spacing or a lane
// count outside 1-2 (far_lanes 0-1, far 1-5). rs7string's rsxrfrets::Compute.
static inline int seven_bar_extend_far(const float a0[3], const float a1[3], const float mid[3], int lanes, int far,
                                       int far_lanes, float shift[3], float *scale, float *d) {
  float u[3], len2 = 0.0f;
  for (int i = 0; i < 3; i++) {
    u[i] = a0[i] - a1[i];
    len2 += u[i] * u[i];
  }
  const float len = sqrtf(len2);
  if (!(len > 1e-3f) || !(len < 1e3f) || lanes < 1 || lanes > 2 || far < 1 || far > 5 || far_lanes < 0 ||
      far_lanes > 1)
    return 0;
  float c = 0.0f;
  for (int i = 0; i < 3; i++) c += (mid[i] - a0[i]) * u[i] / len;
  const float m0 = c + SEVEN_HALF_LEN;                                    // margin beyond string 0
  const float m5 = (c - SEVEN_HALF_LEN) * -1.0f - (float)far * len;      // margin beyond the far string (-far spacings)
  const float margin = m0 > m5 ? m0 : m5;                                 // the wider, now beyond the outer string
  const float top = (float)lanes * len + margin, bottom = c - SEVEN_HALF_LEN - (float)far_lanes * len;
  const float along = 0.5f * (top + bottom) - c;
  *scale = 0.5f * (top - bottom) / SEVEN_HALF_LEN;
  for (int i = 0; i < 3; i++) shift[i] = along * u[i] / len;
  *d = len;
  return *scale > 0.0f && isfinite(*scale);
}
// A guitar's (the far string 5, none beyond it)
static inline int seven_bar_extend(const float a0[3], const float a1[3], const float mid[3], int lanes, float shift[3],
                                   float *scale, float *d) {
  return seven_bar_extend_far(a0, a1, mid, lanes, 5, 0, shift, scale, d);
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

// The floor of an instrument with lanes beyond either stock outer string (rs7string's FloorMove): `near_lanes` beyond
// string 0, `far_lanes` beyond the stock far string `far` (a 6-string bass's high C). The floor moves out on
// whichever side b lies: the lanes on that side. Returns -1 for a degenerate spacing, 0 when the floor stays, else the
// lanes it moves by, with out = that many spacings (towards string 6 on the near side, away from it on the far side).
static inline int seven_floor_move(const float a0[3], const float a1[3], const float b[3], int near_lanes, int far,
                                   int far_lanes, float out[3]) {
  float len2 = 0.0f;
  for (int i = 0; i < 3; i++) len2 += (a0[i] - a1[i]) * (a0[i] - a1[i]);
  if (!(len2 > 1e-6f) || !(len2 < 1e6f)) return -1;
  if (seven_floor_shift(a0, a1, b, near_lanes, out)) return near_lanes;
  if (far_lanes < 1 || far < 1 || far > 5) return 0;
  float f0[3], f1[3];  // the stock far string and its neighbour (string i at a0 + i (a1 - a0))
  for (int i = 0; i < 3; i++) {
    f0[i] = a0[i] + (float)far * (a1[i] - a0[i]);
    f1[i] = a0[i] + (float)(far - 1) * (a1[i] - a0[i]);
  }
  return seven_floor_shift(f0, f1, b, far_lanes, out) ? far_lanes : 0;
}

// A chord frame (rs7string's rsxrfrets::ChordFrame): one model whose origin edge sits on the fret wires' bottom end b
// (the highway floor) and which extends towards the lanes by H = far d + 2m (d the string spacing, m the game's margin
// beyond the outer lane of the stock strings 0..far; the model is symmetric about them). It must cover them and the
// strings beyond: lanes 0..`top` (top > 5: top - 5 lanes beyond string 0; a 5/6-string bass passes 6) and `far_lanes`
// beyond the far string, each with margin m. The origin edge stays on the floor (floor_lanes = the spacings the floor
// was moved by, on b's side, 0 for not moved); only the far edge moves (scale, on the model's extent), except where
// the origin edge lies inside the outermost lane and its margin: then `shift` moves it out. 0 for a degenerate
// geometry (out untouched). a0 / a1 / b are guitar-local.
typedef struct {
  float scale;    // along the string axis, on the model's extent
  float shift;    // the origin edge moved, from it towards the lanes
  float origin;   // the origin edge as placed now (0 at string 0, string i at -i spacings)
  float margin;   // the game's margin beyond the outer lane
  float spacing;  // the string spacing
} seven_frame_fit;
static inline int seven_frame_fit_for(const float a0[3], const float a1[3], const float b[3], int floor_lanes, int top,
                                      int far, int far_lanes, seven_frame_fit *out) {
  if (far < 1 || far > 5 || far_lanes < 0 || far_lanes > 1) return 0;
  float u[3], len2 = 0.0f;
  for (int i = 0; i < 3; i++) {
    u[i] = a0[i] - a1[i];
    len2 += u[i] * u[i];
  }
  const float d = sqrtf(len2);
  if (!(d > 1e-3f) || !(d < 1e3f)) return 0;
  float pb = 0.0f;  // along u (string 1 -> string 0 -> string 6), 0 at string 0: string i lies at -i d
  for (int i = 0; i < 3; i++) pb += (b[i] - a0[i]) * u[i] / d;
  const float fd = (float)far * d;
  const int six = pb > 0.0f;  // the origin edge lies beyond string 0, on string 6's side
  const float m = six ? pb : -pb - fd;
  if (!(m > 0.05f * d) || !(m < 4.0f * d) || !isfinite(m)) return 0;
  const float h = fd + 2.0f * m;
  const float po = pb + (six ? 1.0f : -1.0f) * (float)floor_lanes * d;
  const float lo = -fd - (float)far_lanes * d - m;
  const float hi = (top > 5 ? (float)(top - 5) * d : 0.0f) + m;
  float origin, far_edge;
  if (six) {
    origin = po + (hi > po ? hi - po : 0.0f);
    far_edge = lo;
  } else {
    origin = po - (po > lo ? po - lo : 0.0f);
    far_edge = hi;
  }
  out->scale = fabsf(far_edge - origin) / h;
  out->shift = six ? po - origin : origin - po;
  out->origin = po;
  out->margin = m;
  out->spacing = d;
  return isfinite(out->scale) && isfinite(out->shift) && out->scale > 0.0f;
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
