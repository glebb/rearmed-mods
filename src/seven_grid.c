// Seven-String Guitar (seven.h): the grid part. After rs7string's native FretGrid, native next-note grid and fret
// bars (github.com/SomeeName/rs7string, MIT, by SomeeName: src/features/fretgrid, nextgrid, frets).
//
// The grids grow by the edits of seven_string alone. Every FretGrid the game makes has cell rows of 8 strings (0x264
// bytes, not 0x1cc) and fret-wire rows of 8 records, so the game's own code (its build, ApplyOffset, GetCellPos, the
// readers of the cells for gems, sustains, slides and sparks) covers strings 0-7; the next-note grid on the neck
// targets' stack has 8 time rows behind its time-set tree. The edits are on before the game makes its first grid, so
// there is never a grid of the six-string size to replace. What runs here is the fret bars:
//
//   * On a guitar built with 7 or 8 strings every fret bar and the nut move half a string spacing towards string 6 and
//     scale by k = (L + d/2) / L (L = 4.61, the bar mesh's half length; d = the string spacing): their end at string 5
//     stays, the other reaches beyond string 6 by the wider of the game's two margins. Rnd_FretBars_ApplyTransforms
//     sets each bar's translation to the middle of its locators every time it runs; seven_f1 works out the shift and
//     k for its run, seven_f1_bar moves each translation before the game sets it, and the edit after it calls the
//     game's Entity_SetScale with k (seven_f1_scale gives k, or 0 for no call).
//   * The highway floor: the game puts the time axis, the fret numbers, the note stems and the chord frames on the
//     fret wires' bottom ends. With inverted strings those lie on string 6's side, so seven_f1_floor moves them one
//     string spacing further out after the game wrote them.
//   * The inlay dots, authored centred on strings 0-5, move to the centre of seven strings (seven_in1).
//
// Guitars built with 6 strings and bass are never touched. Only the game's own setters are used; the locators and the
// span data are never written, so gems, highway, fret numbers and wrong-fret effects stay the game's.
#include <math.h>
#include <string.h>

#include "seven.h"

// The strings part: the string count the guitar (Rnd_Guitar) was built with, InstrumentInfo +0x0b (4, 6, 7, 8); 0
// for none
extern int seven_strings_guitar_count(const void *guitar);

#define HALF_LEN 4.61f  // L: half the length of a fret bar's mesh (its bound radius is 4.638)
#define MAX_BARS 26     // fret-bar records of a FretGrid

// --- guest memory -----------------------------------------------------------------------------------------------
static void ld3(float v[3], const uint8_t *p) {
  for (int i = 0; i < 3; i++) v[i] = RC_LD(float, p + 4 * i);
}
static void st3(uint8_t *p, const float v[3]) {
  for (int i = 0; i < 3; i++) RC_ST(float, p + 4 * i, v[i]);
}
static uint8_t *host(uint64_t v) { return (uint8_t *)(uintptr_t)v; }
static uint64_t arg(const void *p) { return (uint64_t)(uintptr_t)p; }

static int once(int *flag) { return !__atomic_exchange_n(flag, 1, __ATOMIC_RELAXED); }

static int wants_seven(const uint8_t *guitar) {
  int n = guitar ? seven_strings_guitar_count(guitar) : 0;
  return n == 7 || n == 8;
}

// The guitar-local attach points of strings 0 and 1: 0 while its strings are not loaded
static int attach(const uint8_t *guitar, float a0[3], float a1[3]) {
  const uint8_t *gs = rc_mod_ptr(guitar + RC_OFF(guitar_strings));
  if (!gs) return 0;
  const uint8_t *s0 = rc_mod_ptr(gs + RC_OFF(guitar_strings_string));
  const uint8_t *s1 = rc_mod_ptr(gs + RC_OFF(guitar_strings_string) + rc_guest_ptr_size);
  if (!s0 || !s1) return 0;
  ld3(a0, s0 + RC_OFF(guitar_string_attach));
  ld3(a1, s1 + RC_OFF(guitar_string_attach));
  return 1;
}

// The guitar's fret bars (InstrumentInfo), at most MAX_BARS
static int bar_count(const uint8_t *guitar) {
  const uint8_t *info = rc_mod_ptr(guitar + RC_OFF(guitar_info));
  int n = info ? RC_LD(uint8_t, info + RC_OFF(instrument_fret_bars)) : 0;
  return n > MAX_BARS ? MAX_BARS : n;
}

static uint8_t *bar(uint8_t *grid, int k) { return grid + RC_OFF(fretgrid_bars) + k * RC_OFF(fretbar_size); }

// --- the geometry (rs7string's frets.cpp) -----------------------------------------------------------------------
// The bars' extension. a0 / a1: the string 0 / 1 attach points, mid: bar 0's middle as the game places it (all
// guitar-local; only differences count, so a lefty or inverted guitar comes with its attach points). Along u (string
// 1 -> string 0 -> string 6), from string 0, the game's bar spans c -+ L; the new one keeps its end beyond string 5
// and reaches beyond string 6 by the wider of the game's margins. 0 for a degenerate spacing.
static int extend(const float a0[3], const float a1[3], const float mid[3], float shift[3], float *scale, float *d) {
  float u[3], len2 = 0.0f;
  for (int i = 0; i < 3; i++) {
    u[i] = a0[i] - a1[i];
    len2 += u[i] * u[i];
  }
  const float len = sqrtf(len2);
  if (!(len > 1e-3f) || !(len < 1e3f)) return 0;
  float c = 0.0f;
  for (int i = 0; i < 3; i++) c += (mid[i] - a0[i]) * u[i] / len;
  const float m0 = c + HALF_LEN;                       // margin beyond string 0
  const float m5 = (c - HALF_LEN) * -1.0f - 5.0f * len;  // margin beyond string 5 (at -5 spacings)
  const float margin = m0 > m5 ? m0 : m5;              // the wider, now beyond string 6
  const float top = len + margin, bottom = c - HALF_LEN;
  const float along = 0.5f * (top + bottom) - c;
  *scale = 0.5f * (top - bottom) / HALF_LEN;
  for (int i = 0; i < 3; i++) shift[i] = along * u[i] / len;
  *d = len;
  return *scale > 0.0f && isfinite(*scale);
}

// The highway floor: b = a fret wire's bottom end (guitar-local). Beyond string 0 on string 6's side (inverted
// strings) it moves one string spacing further out: 1 and out = a0 - a1; else 0 (the game's floor beyond string 5).
static int floor_shift(const float a0[3], const float a1[3], const float b[3], float out[3]) {
  float len2 = 0.0f, side = 0.0f;
  for (int i = 0; i < 3; i++) {
    out[i] = a0[i] - a1[i];
    len2 += out[i] * out[i];
    side += (b[i] - a0[i]) * out[i];
  }
  return len2 > 1e-6f && len2 < 1e6f && side > 0.0f;
}

// --- F1: Rnd_FretBars_ApplyTransforms ---------------------------------------------------------------------------
// One run of the function on this thread, from seven_f1 to seven_f1_floor
static __thread struct {
  uint8_t *grid, *guitar;
  int bars;          // the bars move and scale
  int floor;         // a 7/8-string guitar with its strings loaded: the floor may move
  float a0[3], a1[3];
  float shift[3], k, d;
  uint32_t k_bits;
} f1;
static int g_log_bars, g_log_strings, g_log_degenerate, g_log_floor, g_log_inlays;

// After the bars' world ends are written, before their translations are set
static void on_f1(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  memset(&f1, 0, sizeof f1);
  uint8_t *grid = host(SEVEN_GET(f, "grid"));
  uint8_t *guitar = grid ? rc_mod_ptr(grid + RC_OFF(fretgrid_guitar)) : NULL;
  if (!wants_seven(guitar)) return;
  if (!attach(guitar, f1.a0, f1.a1)) {
    if (once(&g_log_strings))
      rsm_log("seven-string: frets: a 7-string guitar's strings were not loaded when its fret bars were placed: "
              "the bars stay as the game has them until it places them again");
    return;
  }
  f1.grid = grid;
  f1.guitar = guitar;
  f1.floor = 1;
  float a[3], b[3], mid[3];
  ld3(a, bar(grid, 0) + RC_OFF(fretbar_a_local));
  ld3(b, bar(grid, 0) + RC_OFF(fretbar_b_local));
  for (int i = 0; i < 3; i++) mid[i] = 0.5f * (a[i] + b[i]);
  if (!extend(f1.a0, f1.a1, mid, f1.shift, &f1.k, &f1.d)) {
    if (once(&g_log_degenerate))
      rsm_log("seven-string: frets: the string spacing is degenerate: the bars stay as the game has them");
    return;
  }
  memcpy(&f1.k_bits, &f1.k, sizeof f1.k_bits);
  f1.bars = 1;
}

// Before Entity_SetTranslation(bar's entity, pos): pos is the middle of the bar's world ends
static void on_f1_bar(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on || !f1.bars) return;
  uint8_t *pos = host(SEVEN_GET(f, "pos"));
  if (!pos) return;
  float p[3];
  ld3(p, pos);
  for (int i = 0; i < 3; i++) p[i] += f1.shift[i];
  st3(pos, p);
}

// After it: the edit calls Entity_SetScale(entity, scale) unless scale is 0
static void on_f1_scale(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  SEVEN_SET(f, "scale_bits", f1.bars && SEVEN_GET(f, "entity") ? f1.k_bits : 0);
}

// At the function's end: the floor. The game has just written the bottom ends of records 0..n-1 (local + the guitar's
// offset); record n, which the time axis and the fret numbers read too, is put at its local end by the same offset,
// so it moves once however often the function runs.
static void on_f1_floor(rc_mid_frame *f, void *user) {
  (void)f;
  (void)user;
  if (!seven_on || !f1.floor) return;
  f1.floor = 0;
  if (f1.bars && once(&g_log_bars))
    rsm_log("seven-string: frets: the fret bars of a 7-string guitar span the 7th string: shift (%.4f, %.4f, %.4f), "
            "scale %.4f, string spacing %.4f", f1.shift[0], f1.shift[1], f1.shift[2], f1.k, f1.d);
  f1.bars = 0;
  uint8_t *b0 = bar(f1.grid, 0);
  float b[3], u[3];
  ld3(b, b0 + RC_OFF(fretbar_b_local));
  if (!RC_LD(uint8_t, b0 + RC_OFF(fretbar_valid)) || !floor_shift(f1.a0, f1.a1, b, u)) return;
  const int n = bar_count(f1.guitar);
  float off[3] = {0.0f, 0.0f, 0.0f}, w[3], l[3];
  for (int k = 0; k < n; k++) {
    uint8_t *r = bar(f1.grid, k);
    ld3(w, r + RC_OFF(fretbar_b_world));
    ld3(l, r + RC_OFF(fretbar_b_local));
    for (int i = 0; i < 3; i++) {
      off[i] = w[i] - l[i];
      w[i] += u[i];
    }
    st3(r + RC_OFF(fretbar_b_world), w);
  }
  int moved = n;
  if (n > 0 && n < MAX_BARS) {
    uint8_t *r = bar(f1.grid, n);
    ld3(l, r + RC_OFF(fretbar_b_local));
    for (int i = 0; i < 3; i++) w[i] = l[i] + off[i] + u[i];
    st3(r + RC_OFF(fretbar_b_world), w);
    moved++;
  }
  if (once(&g_log_floor))
    rsm_log("seven-string: frets: inverted strings: the highway floor (%d fret-wire ends) moves by (%.4f, %.4f, %.4f)",
            moved, u[0], u[1], u[2]);
}

// --- IN1: after Rnd_Inlays_ApplyTransforms ----------------------------------------------------------------------
// The entity's root scene node, as the game's entity setters find it: NULL while the world has no scene
static uint8_t *entity_root(uint8_t *entity) {
  uint8_t *world = RC_HOST(OV_GUESTV(render_world, NULL, 0));
  if (!world || !RC_LD(uint32_t, world + RC_OFF(render_world_scene))) return NULL;
  if (RC_LD(uint32_t, entity + RC_OFF(entity_flags)) & 1) return rc_mod_ptr(entity + RC_OFF(entity_root_node));
  return RC_HOST(OV_GUEST(world_entity_root, arg(entity), arg(world)));
}

// The game has put the inlay entity where the guitar's frame is; on a guitar built with 7/8 strings it moves half a
// string spacing towards string 6, either string order (the attach points say which way)
static void on_in1(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  uint8_t *guitar = host(SEVEN_GET(f, "guitar")), *head = host(SEVEN_GET(f, "head"));
  uint8_t *inlays = host(SEVEN_GET(f, "inlays"));
  if (!head || !inlays || !wants_seven(guitar)) return;
  uint8_t *entity = rc_mod_ptr(inlays + RC_OFF(inlays_entity));
  uint8_t *frame = rc_mod_ptr(head + RC_OFF(guitar_head_frame));
  uint8_t *root = entity && frame ? entity_root(frame) : NULL;
  float a0[3], a1[3], p[3];
  if (!root || !attach(guitar, a0, a1)) return;
  static __thread uint8_t *pos;  // the game's memory for the position it is given
  if (!pos && !(pos = rc_guest_alloc(12))) return;
  ld3(p, root + RC_OFF(ni_world_translate));
  for (int i = 0; i < 3; i++) p[i] += 0.5f * (a0[i] - a1[i]);
  st3(pos, p);
  OV_GUEST(entity_set_translation, arg(entity), arg(pos));
  if (once(&g_log_inlays))
    rsm_log("seven-string: frets: the inlays of a 7-string guitar are at the centre of its strings (%.3f, %.3f, %.3f)",
            p[0], p[1], p[2]);
}

// --- what the others use (seven_grid.h) -------------------------------------------------------------------------
void *seven_grid_cell(void *grid, unsigned row, unsigned string) {
  if (!grid || row >= RC_OFF(seven_fretgrid_rows) || string >= 8) return NULL;
  return (uint8_t *)grid + RC_OFF(seven_fretgrid_cells) + row * RC_OFF(seven_fretgrid_row) +
         string * RC_OFF(seven_fretgrid_cell);
}

int seven_grid_fill_extra_cells(void *grid, unsigned frets) {
  if (!grid) return 0;
  if (frets > RC_OFF(seven_fretgrid_rows)) frets = (unsigned)RC_OFF(seven_fretgrid_rows);
  const int64_t local = RC_OFF(seven_fretgrid_cell_local);
  int done = 0;
  for (unsigned r = 0; r < frets; r++) {
    uint8_t *c0 = seven_grid_cell(grid, r, 0), *c1 = seven_grid_cell(grid, r, 1);
    uint8_t *c6 = seven_grid_cell(grid, r, 6), *c7 = seven_grid_cell(grid, r, 7);
    if (!RC_LD(uint8_t, c0) || !RC_LD(uint8_t, c1)) {
      RC_ST(uint8_t, c6, 0);
      RC_ST(uint8_t, c7, 0);
      continue;
    }
    for (int i = 0; i < 9; i++) {  // three points of three coordinates
      const float a = RC_LD(float, c0 + local + 4 * i), b = RC_LD(float, c1 + local + 4 * i);
      RC_ST(float, c6 + local + 4 * i, 2.0f * a - b);
      RC_ST(float, c7 + local + 4 * i, 3.0f * a - 2.0f * b);
    }
    RC_ST(uint8_t, c6, 1);
    RC_ST(uint8_t, c7, 1);
    done++;
  }
  return done;
}

int seven_grid_init(void) {
  if (seven_mid("seven_f1", on_f1) || seven_mid("seven_f1_bar", on_f1_bar) ||
      seven_mid("seven_f1_scale", on_f1_scale) || seven_mid("seven_f1_floor", on_f1_floor) ||
      seven_mid("seven_in1", on_in1))
    return -1;
  return 0;
}
