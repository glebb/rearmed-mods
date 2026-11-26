// Seven-String Guitar (seven.h): the string spacing. After rs7string's spacing feature (github.com/SomeeName/rs7string,
// MIT, by SomeeName: src/features/spacing, [neck] spacing_guitar7 / spacing_guitar8).
//
// A guitar built with 7 or 8 strings can have its own string spacing (the settings' spacing_guitar7 / spacing_guitar8,
// the distance between two strings in the game's units; stock is 1.608). The game's attach points of strings 0-5
// (Rnd_GetStringAttachPoint, which the string load, the fret grid's cells and the next-note grid ask) are scaled
// about the middle of the stock six by k = wanted / stock spacing, and the extra strings follow from them (the strings
// part's AttachExt on the scaled rows). The fret bars follow it (seven_grid.c's F1). Only guitars built with 7 or 8
// strings; a bass keeps its spacing (its bars would need the bass grid, which isn't here).
//
// The pure math is below (no game memory, tests/test_seven_spacing.c); seven_spacing.c has the game's side.
#ifndef RS_SEVEN_SPACING_H
#define RS_SEVEN_SPACING_H
#include <math.h>

#define SEVEN_SPACING_STOCK 1.608f  // the game's string spacing
#define SEVEN_SPACING_KMIN 0.25f    // k stays within these (a wanted value is 0.5 to 4.0 in 1.608)
#define SEVEN_SPACING_KMAX 4.0f

// k for a wanted spacing (0 = stock) and the stock spacing measured; 1 for no change or a value that makes no sense
static inline float seven_spacing_scale(float wanted, float stock) {
  if (!(wanted > 0.0f) || !(stock > 1e-3f) || !isfinite(wanted) || !isfinite(stock)) return 1.0f;
  const float k = wanted / stock;
  return k < SEVEN_SPACING_KMIN ? SEVEN_SPACING_KMIN : k > SEVEN_SPACING_KMAX ? SEVEN_SPACING_KMAX : k;
}

// The middle of the stock six strings from the stock attach points of strings 0 and 1 (string 5 lies five spacings
// from string 0): a0 - 2.5 * (a0 - a1)
static inline void seven_spacing_centre(const float a0[3], const float a1[3], float c[3]) {
  for (int i = 0; i < 3; i++) c[i] = a0[i] - 2.5f * (a0[i] - a1[i]);
}

// A stock attach point scaled about the centre: c + k * (p - c)
static inline void seven_spacing_point(const float p[3], const float c[3], float k, float out[3]) {
  for (int i = 0; i < 3; i++) out[i] = c[i] + k * (p[i] - c[i]);
}

// The fret bars on a guitar whose strings are spaced k times the stock (a0, a1: the spaced attach points of strings 0
// and 1, mid: bar 0's middle as the game places it from its locators, half: the bar mesh's half length). Along
// u = a0 - a1 from the middle of the six strings, the game's bar spans [xm - half, xm + half]; its margins beyond
// string 5 (m5) and string 0 (m0) are measured on the stock spacing. The new bar keeps m5 beyond string 5 and reaches
// `lanes` spacings and the wider margin beyond string 0. Returns 1, with shift (the middle's move), scale (the mesh
// scale) and d (the spaced string spacing); 0 for a degenerate spacing, k <= 0 or lanes outside 0-2.
static inline int seven_spacing_bar(const float a0[3], const float a1[3], const float mid[3], int lanes, float k,
                                    float half, float shift[3], float *scale, float *d) {
  float u[3], len2 = 0.0f, xm = 0.0f;
  for (int i = 0; i < 3; i++) {
    u[i] = a0[i] - a1[i];
    len2 += u[i] * u[i];
  }
  const float len = sqrtf(len2);
  if (!(len > 1e-3f) || !(len < 1e3f) || !(k > 0.0f) || !isfinite(k) || lanes < 0 || lanes > 2 || !(half > 0.0f)) return 0;
  for (int i = 0; i < 3; i++) xm += (mid[i] - (a0[i] - 2.5f * u[i])) * u[i] / len;
  const float len0 = len / k;
  const float m0 = xm + half - 2.5f * len0;      // margin beyond string 0 (stock)
  const float m5 = -2.5f * len0 - (xm - half);   // margin beyond string 5 (stock)
  const float margin = m0 > m5 ? m0 : m5;
  const float top = (2.5f + (float)lanes) * len + margin, bottom = -2.5f * len - m5;
  const float along = 0.5f * (top + bottom) - xm;
  *scale = 0.5f * (top - bottom) / half;
  for (int i = 0; i < 3; i++) shift[i] = along * u[i] / len;
  *d = len;
  return *scale > 0.0f && isfinite(*scale);
}

// ---- the game's side (seven_spacing.c)
// The stock attach points of strings 0 and 1 of this InstrumentInfo (the game's own answer, whatever the spacing): 1 and
// a0, a1 (cached for the guitar's life), or 0 while the headstock is not there to answer
int seven_spacing_stock(const void *info, float a0[3], float a1[3]);
// k of the guitar (Rnd_Guitar, a host pointer): its loaded strings' spacing over the stock's; 1 for stock spacing, a
// guitar without strings loaded, or a feature that is off. P4's headstock width reads it.
float seven_spacing_k(const void *guitar);
// The attach point of string s (0-5) of an InstrumentInfo with the spacing applied: 1 or 0 as the game's query answers
// (the point written to out, the caller's in the game's memory), or -1 when the spacing doesn't apply (feature off,
// not a guitar of 7 or 8 strings, stock spacing, or a nested query of its own): the game's query is then to run as it is.
int seven_spacing_attach(void *info, unsigned s, float *out);
// A new guitar: its cached stock geometry is dropped (the InstrumentInfo's address may be reused)
void seven_spacing_forget(void);
#endif
