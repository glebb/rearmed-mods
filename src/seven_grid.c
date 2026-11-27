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
//     string spacing (two on 8 strings) further out after the game wrote them. It is done per run of the function and
//     per guitar: until the guitar's strings and bars are there it is pending, and the per-frame hook (seven_n5, via
//     seven_grid_frame) tries again. seven_grid_floor_lanes tells the others how far it moved.
//   * The song camera (seven_cam): where the guitar sends its camera markers the four z values get the floor's shift,
//     so the camera stays lined up with the moved floor and the fret numbers stay on screen.
//   * The fret range glow: the glowing box and bars that show where the hand goes for a chord or note. Where the
//     game builds one (seven_frg1) its four wire ends move to the floor and past string 6 as the bars did, and its
//     box and bars are recorded to be scaled to span all strings once their entities exist (seven_frg2, 2b and 3, the
//     scale by the game's Entity_SetScale: seven_frg*_scale injects the call on Learn & Play, seven_frg*_setscale
//     calls it on the Mac).
//   * The inlay dots, authored centred on strings 0-5, move to the centre of the guitar's strings (seven_in1).
//
// An extended bass (built with 5 or 6 strings, SEVEN_F_BASS) is the same with its own geometry: its stock far string is
// the G (3), not string 5; the low B is one lane beyond string 0 (like a 7th string) and a 6-string's high C one lane
// beyond the G, at the bars' other end (seven_span: near lanes, far string, far lanes). The bars keep the end at the
// G, or reach one spacing past it for the C; the floor moves out on whichever side it lies; the glow, the camera and
// the inlays (centred on the net lanes: a 6-string bass's stay put) follow. The strings of a bass of 5 or 6 are placed
// by the strings part's attach points (and spaced by seven_spacing.c's); the stock grid build covers its cells.
//
// Guitars built with 6 strings and 4-string basses are never touched. Only the game's own setters are used; the locators and the
// span data are never written, so gems, highway, fret numbers and wrong-fret effects stay the game's.
#include <math.h>
#include <string.h>

#include "seven.h"
#include "seven_grid_math.h"

// The strings part: the string count the guitar (Rnd_Guitar) was built with, InstrumentInfo +0x0b (4, 6, 7, 8); 0
// for none
extern int seven_strings_guitar_count(const void *guitar);

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

static int g_log_bars, g_log_strings, g_log_degenerate, g_log_floor, g_log_retry, g_log_inlays;

static int lanes_of(const uint8_t *guitar);

static int once(int *flag) { return !__atomic_exchange_n(flag, 1, __ATOMIC_RELAXED); }

static int wants_seven(const uint8_t *guitar) { return lanes_of(guitar) > 0; }

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

// --- per-guitar floor state ---------------------------------------------------------------------------------------
// rs7string's floorMoved: the string spacings the highway floor of a guitar was moved by in the game's last run of
// Rnd_FretBars_ApplyTransforms (which writes the world ends anew): FLOOR_PENDING until it is done (the strings or
// bars were not there yet), 0 when the floor needed no move, else the lanes. A run resets it, seven_f1_floor and the
// per-frame retry set it.
#define FLOOR_PENDING (-1)
#define MAX_GUITARS 4  // players with guitars in a song
static struct {
  const uint8_t *guitar;
  int moved;
} g_floor[MAX_GUITARS];
static int g_floor_next;

static int *floor_slot(const uint8_t *guitar, int add) {
  for (int i = 0; i < MAX_GUITARS; i++)
    if (g_floor[i].guitar == guitar) return &g_floor[i].moved;
  if (!add) return NULL;
  int i = g_floor_next++ % MAX_GUITARS;
  g_floor[i].guitar = guitar;
  g_floor[i].moved = FLOOR_PENDING;
  return &g_floor[i].moved;
}

// The strings beyond the stock ones of a guitar, as the bars see them: near = the lanes beyond string 0 (1 or 2 on a
// guitar built with 7 or 8 strings, 1 on a bass built with 5 or 6 for its low B), far = its stock far string (5; a
// bass's 3, the G) and far_lanes = the lanes beyond that (1 on a 6-string bass, for the high C). 0 with near 0 for any
// other guitar (6 strings, a 4-string bass, a bass without SEVEN_F_BASS). rs7string's ExtraLanes, StockFar, FarLanes.
typedef struct {
  int near, far, far_lanes;
} seven_span;
static seven_span span_of(const uint8_t *guitar) {
  seven_span sp = {0, 5, 0};
  const int n = guitar ? seven_strings_guitar_count(guitar) : 0;
  if (!n) return sp;
  const void *info = rc_mod_ptr(guitar + RC_OFF(guitar_info));
  if (info && seven_strings_info_is_bass(info)) {
    sp.far = SEVEN_BASS_STOCK - 1;
    if (seven_feature(SEVEN_F_BASS) && seven_is_ext_bass(n)) sp.near = 1, sp.far_lanes = n == 6;
  } else if (seven_is_ext_guitar(n)) {
    sp.near = n - 6;
  }
  return sp;
}
static int lanes_of(const uint8_t *guitar) { return span_of(guitar).near; }

// --- the floor --------------------------------------------------------------------------------------------------
// What the floor of this guitar needs (rs7string's FloorOffset): -1 while its geometry is not ready (the strings or
// fret bars are not loaded, bar 0 is not placed, a degenerate spacing), 0 when the floor stays where the game has it,
// else the lanes it moves by, with out = lanes * (a0 - a1) towards string 6 (a bass's floor on the far side of its G:
// lanes * (a1 - a0), away from it; guitar-local, as the attach points are).
static int floor_offset(const uint8_t *guitar, float out[3]) {
  const int lanes = lanes_of(guitar);
  if (!lanes) return 0;
  const uint8_t *grid = rc_mod_ptr(guitar + RC_OFF(guitar_fretgrid));
  float a0[3], a1[3], b[3];
  if (!grid || !attach(guitar, a0, a1)) return -1;
  const uint8_t *b0 = bar((uint8_t *)grid, 0);
  if (!RC_LD(uint8_t, b0 + RC_OFF(fretbar_valid))) return -1;
  ld3(b, b0 + RC_OFF(fretbar_b_local));
  const seven_span sp = span_of(guitar);
  return seven_floor_move(a0, a1, b, lanes, sp.far, sp.far_lanes, out);
}

// The game has just written the world bottom ends of the guitar's fret bars (local + the guitar's offset): move
// them, records 0..n-1 as the game wrote them; record n, which the time axis and the fret numbers read too, is put at
// its local end by the same offset, so it moves once however often the function runs.
static void move_floor(uint8_t *guitar, const float u[3], int lanes) {
  uint8_t *grid = rc_mod_ptr(guitar + RC_OFF(guitar_fretgrid));
  const int n = bar_count(guitar);
  float off[3] = {0.0f, 0.0f, 0.0f}, w[3], l[3];
  for (int k = 0; k < n; k++) {
    uint8_t *r = bar(grid, k);
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
    uint8_t *r = bar(grid, n);
    ld3(l, r + RC_OFF(fretbar_b_local));
    for (int i = 0; i < 3; i++) w[i] = l[i] + off[i] + u[i];
    st3(r + RC_OFF(fretbar_b_world), w);
    moved++;
  }
  if (once(&g_log_floor))
    rsm_log("seven-string: frets: inverted strings: the highway floor (%d fret-wire ends) moves by %d string "
            "spacing(s), (%.4f, %.4f, %.4f)", moved, lanes, u[0], u[1], u[2]);
}

// The floor of one guitar, after the game wrote its world ends: moved now, or left pending for the frame hook
static void recover_floor(uint8_t *guitar) {
  float u[3];
  const int moved = floor_offset(guitar, u);
  if (moved > 0) move_floor(guitar, u, moved);
  int *slot = floor_slot(guitar, 1);
  *slot = moved < 0 ? FLOOR_PENDING : moved;
}

// N5, every frame of a guitar (seven_strings.c): a floor that was not ready when the bars were placed is done as soon
// as it is (rs7string's RsxrFrets_Frame)
void seven_grid_frame(void *guitar) {
  if (!seven_on || !guitar) return;
  const int *slot = floor_slot(guitar, 0);
  if (slot && *slot == FLOOR_PENDING && wants_seven(guitar)) {
    recover_floor(guitar);
    if (*slot != FLOOR_PENDING && once(&g_log_retry))
      rsm_log("seven-string: frets: the highway floor of an extended guitar or bass found its place after its fret "
              "bars were placed");
  }
}

int seven_grid_floor_lanes(const void *guitar) {
  const int *slot = floor_slot(guitar, 0);
  return slot && *slot > 0 ? *slot : 0;
}

// --- F1: Rnd_FretBars_ApplyTransforms ---------------------------------------------------------------------------
// One run of the function on this thread, from seven_f1 to seven_f1_floor
static __thread struct {
  uint8_t *grid, *guitar;
  int bars;          // the bars move and scale
  int floor;         // a 7/8-string guitar: the floor is done at the function's end
  float a0[3], a1[3];
  float shift[3], k, d;
  uint32_t k_bits;
} f1;

// After the bars' world ends are written, before their translations are set
static void on_f1(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  memset(&f1, 0, sizeof f1);
  uint8_t *grid = host(SEVEN_GET(f, "grid"));
  uint8_t *guitar = grid ? rc_mod_ptr(grid + RC_OFF(fretgrid_guitar)) : NULL;
  if (!wants_seven(guitar)) return;
  // The game has written the world ends anew: the floor is the game's again until seven_f1_floor (or the frame hook)
  // moves it
  *floor_slot(guitar, 1) = FLOOR_PENDING;
  f1.grid = grid;
  f1.guitar = guitar;
  f1.floor = 1;
  if (!attach(guitar, f1.a0, f1.a1)) {
    if (once(&g_log_strings))
      rsm_log("seven-string: frets: an extended guitar's (7/8 strings, 5/6-string bass) strings were not loaded when its fret bars were placed: "
              "the bars stay as the game has them until it places them again");
    return;
  }
  float a[3], b[3], mid[3];
  ld3(a, bar(grid, 0) + RC_OFF(fretbar_a_local));
  ld3(b, bar(grid, 0) + RC_OFF(fretbar_b_local));
  for (int i = 0; i < 3; i++) mid[i] = 0.5f * (a[i] + b[i]);
  // A spaced guitar (seven_spacing.h): its strings' spacing over the stock's, from the attach points the strings were
  // loaded with; the bars follow the spaced strings, not the stock locators
  float ratio = 1.0f;
  if (seven_feature(SEVEN_F_SPACING)) {
    float s0[3], s1[3], len0 = 0.0f, len = 0.0f;
    if (seven_spacing_stock(rc_mod_ptr(guitar + RC_OFF(guitar_info)), s0, s1))
      for (int i = 0; i < 3; i++) {
        len0 += (s0[i] - s1[i]) * (s0[i] - s1[i]);
        len += (f1.a0[i] - f1.a1[i]) * (f1.a0[i] - f1.a1[i]);
      }
    if (len0 > 1e-6f) ratio = sqrtf(len / len0);
  }
  const int spaced = fabsf(ratio - 1.0f) > 1e-3f;
  const seven_span sp = span_of(guitar);
  if (!(spaced ? seven_spacing_bar_far(f1.a0, f1.a1, mid, sp.near, sp.far, sp.far_lanes, ratio, SEVEN_HALF_LEN, f1.shift,
                                       &f1.k, &f1.d)
               : seven_bar_extend_far(f1.a0, f1.a1, mid, sp.near, sp.far, sp.far_lanes, f1.shift, &f1.k, &f1.d))) {
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

// At the function's end: the floor
static void on_f1_floor(rc_mid_frame *f, void *user) {
  (void)f;
  (void)user;
  if (!seven_on || !f1.floor) return;
  f1.floor = 0;
  if (f1.bars && once(&g_log_bars))
    rsm_log("seven-string: frets: the fret bars of a %d-string %s span all its strings: shift (%.4f, %.4f, %.4f), "
            "scale %.4f, string spacing %.4f", seven_strings_guitar_count(f1.guitar),
            seven_strings_info_is_bass(rc_mod_ptr(f1.guitar + RC_OFF(guitar_info))) ? "bass" : "guitar", f1.shift[0],
            f1.shift[1], f1.shift[2], f1.k, f1.d);
  f1.bars = 0;
  recover_floor(f1.guitar);
}

// After it, where the game lets the C call Entity_SetScale (the Mac: seven_f1_scale is none there, and this point
// is none on Learn & Play, whose Entity_SetScale takes its own registers)
static void on_f1_setscale(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on || !f1.bars) return;
  const uint64_t entity = SEVEN_GET(f, "entity");
  if (!entity) return;
  const uint64_t a[3] = {entity, 1, f1.k_bits};  // (entity, 1 as the game's own setters pass it, scale)
  rc_call_name_f("entity_set_scale", a, 3, 1u << 2);
}

// --- SC1: the song camera ---------------------------------------------------------------------------------------
// Rnd_Guitar_SendCameraMarkers builds four vectors for the song camera (three from the guitar's camera data, the
// fourth the guitar's own offset) and sends them. Their z values (three in memory on the stack, the fourth the
// guitar's offset z in a register, as float bits) get the floor's shift, so the camera follows the moved floor and
// the fret numbers stay on screen. Not the guitar's data itself: every send starts from fresh stock values.
static int g_cam_on;  // every point of the fix is taken
static int g_log_cam;

static float bits_f(uint64_t v) {
  float x;
  const uint32_t b = (uint32_t)v;
  memcpy(&x, &b, sizeof x);
  return x;
}
static uint64_t f_bits(float x) {
  uint32_t b;
  memcpy(&b, &x, sizeof b);
  return b;
}

static void on_cam(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on || !g_cam_on) return;
  const uint8_t *guitar = host(SEVEN_GET(f, "guitar"));
  float shift[3];
  if (!guitar || floor_offset(guitar, shift) <= 0 || shift[2] == 0.0f) return;
  static const char *const names[4] = {"z_a", "z_b", "z_c", "z_guitar"};
  static int idx[4] = {-2, -2, -2, -2};
  for (int i = 0; i < 4; i++) {
    if (idx[i] == -2) idx[i] = rc_mid_value(f->point, names[i]);
    rc_mid_set(f, idx[i], f_bits(bits_f(rc_mid_get(f, idx[i])) + shift[2]));
  }
  if (once(&g_log_cam))
    rsm_log("seven-string: frets: the song camera of an inverted extended guitar follows its floor: z %+.4f", shift[2]);
}

// --- FRG1 - FRG3: the fret range glow ----------------------------------------------------------------------------
// The glow that lights the fret wires at both ends of a hand position, and the box between them, are built for six
// strings: the wire ends the game reads (the effect's four, world) stop at the stock floor and string 0, and its
// box and four bars are fixed-size models. FRG1 (the end of the effect's setup) moves the ends like the extended
// bars, and records the five pieces with the uniform scale that makes each span all strings; the scale is the
// game's Entity_SetScale, called once a piece's entity exists: where the game places a bar (FRG2, and FRG2B on its
// other path) or the box (FRG3). The game derives the glow's centres from the ends afterwards.
#define GLOW_PIECES 5   // the box, then four bars
#define GLOW_PENDING 20  // pieces waiting for their entity (a few glows at once)
static struct {
  const uint8_t *obj;
  float k;
} g_glow[GLOW_PENDING];
static int g_glow_next, g_glow_on;
static int g_log_glow, g_log_glow_piece;

static void on_frg1(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on || !g_glow_on) return;
  uint8_t *fx = host(SEVEN_GET(f, "fx"));
  const uint8_t *player = host(SEVEN_GET(f, "player"));
  const uint8_t *guitar = player ? rc_mod_ptr(player + RC_OFF(session_guitar)) : NULL;
  const seven_span sp = span_of(guitar);
  const int lanes = sp.near;
  if (!fx || !lanes) return;
  const uint8_t *grid = rc_mod_ptr(guitar + RC_OFF(guitar_fretgrid));
  float a0[3], a1[3], b[3], off[3], w0[3], w1[3], u[3], f0[3], f1[3], unused[3];
  if (!grid || !attach(guitar, a0, a1)) return;
  const uint8_t *b0 = bar((uint8_t *)grid, 0);
  if (!RC_LD(uint8_t, b0)) return;
  ld3(b, b0 + RC_OFF(fretbar_b_local));
  ld3(off, guitar + RC_OFF(guitar_offset));  // world = local + the guitar's offset
  float len2 = 0.0f, pb = 0.0f;
  for (int i = 0; i < 3; i++) {
    w0[i] = a0[i] + off[i];
    w1[i] = a1[i] + off[i];
    f0[i] = w0[i] + (float)sp.far * (w1[i] - w0[i]);  // the stock far string and its neighbour (world)
    f1[i] = w0[i] + (float)(sp.far - 1) * (w1[i] - w0[i]);
    u[i] = a0[i] - a1[i];
    len2 += u[i] * u[i];
    pb += (b[i] - a0[i]) * u[i];
  }
  // The floor's side (inverted strings): the end goes exactly to the floor, the stock margin beyond string 0 plus the
  // lanes, whether or not the floor has been moved yet. The other side: the ends beyond string 0 move by the lanes.
  const int near_floor = seven_floor_shift(a0, a1, b, lanes, unused);
  const float margin = len2 > 1e-6f ? pb / len2 : 0.0f;  // the stock floor end beyond string 0, in spacings
  // A far floor (a 6-string bass's high C beyond the G): ends the floor's move already took are not moved twice
  const int shifted = seven_grid_floor_lanes(guitar) > 0;
  int moved = 0;
  for (int i = 0; i < 4; i++) {  // the low and high fret's two wire ends
    uint8_t *p = fx + RC_OFF(rangefx_end) + 12 * i;
    float end[3];
    ld3(end, p);
    const int m = near_floor ? margin > 0.0f && seven_glow_end_at(w0, w1, margin + (float)lanes, end)
                             : seven_glow_end(w0, w1, lanes, end);
    int m2 = m;
    if (!shifted || near_floor) m2 += seven_glow_end(f0, f1, sp.far_lanes, end);  // beyond the far string: its lanes
    if (m2) st3(p, end);
    moved += m2 > 0;
  }
  const float d = sqrtf(len2);
  const float k = seven_glow_stretch(sp.far + lanes + sp.far_lanes, d, margin * d);
  if (k > 1.0f)
    for (int i = 0; i < GLOW_PIECES; i++) {
      const uint8_t *obj = rc_mod_ptr(fx + RC_OFF(rangefx_pieces) + i * rc_guest_ptr_size);
      if (!obj) continue;
      g_glow[g_glow_next].obj = obj;
      g_glow[g_glow_next].k = k;
      g_glow_next = (g_glow_next + 1) % GLOW_PENDING;
    }
  if (once(&g_log_glow))
    rsm_log("seven-string: frets: fret range glow %p of guitar %p: %d of 4 wire ends moved, pieces x%.3f",
            (void *)fx, (void *)guitar, moved, (double)k);
}

// A guitar was rebuilt: nothing of the old one carries over (its floor state, glow pieces not yet scaled)
void seven_grid_forget_all(void) {
  memset(g_floor, 0, sizeof g_floor);
  memset(g_glow, 0, sizeof g_glow);
}

// The scale a glow piece waits for, once its entity exists (0: none): the piece is dropped from the list either way
static uint64_t glow_piece_scale(const uint8_t *obj, uint8_t **entity) {
  *entity = NULL;
  if (!obj) return 0;
  for (int i = 0; i < GLOW_PENDING; i++) {
    if (g_glow[i].obj != obj) continue;
    const float k = g_glow[i].k;
    g_glow[i].obj = NULL;
    const uint32_t hash = RC_LD(uint32_t, obj + RC_OFF(fxpiece_hash));
    uint8_t *e = rc_mod_ptr(obj + RC_OFF(fxpiece_entity));
    // The glow's two bar effects and its box
    if (!e || (hash != 0xAF25DCE0u && hash != 0x68058637u && hash != 0xFA400094u)) return 0;
    if (once(&g_log_glow_piece))
      rsm_log("seven-string: frets: a fret range glow piece (effect %08X) is scaled x%.3f", hash, (double)k);
    *entity = e;
    return f_bits(k);
  }
  return 0;
}

// Learn & Play: the edit after the point calls Entity_SetScale(entity, ecx) when ecx (scale_bits) is not 0
static void on_frg_scale(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  uint8_t *entity;
  const uint64_t bits = g_glow_on ? glow_piece_scale(host(SEVEN_GET(f, "obj")), &entity) : 0;
  SEVEN_SET(f, "scale_bits", bits);
}

// The Mac: the C calls the game's Entity_SetScale(entity, 1, scale) itself
static void on_frg_setscale(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on || !g_glow_on) return;
  uint8_t *entity;
  const uint64_t bits = glow_piece_scale(host(SEVEN_GET(f, "obj")), &entity);
  if (!bits) return;
  const uint64_t a[3] = {arg(entity), 1, bits};
  rc_call_name_f("entity_set_scale", a, 3, 1u << 2);
}

// --- IN1: after Rnd_Inlays_ApplyTransforms ----------------------------------------------------------------------
// The entity's root scene node, as the game's entity setters find it: NULL while the world has no scene. A game
// without a render-world getter of its own (the Mac inlines it) answers through its entity_model_root.
int seven_has_render_world(void) { return RC_ADDR(render_world) != 0; }  // one lookup (one log line) for the unit
static uint8_t *entity_root(uint8_t *entity) {
  if (!seven_has_render_world()) return RC_HOST(OV_GUEST(entity_model_root, arg(entity)));
  uint8_t *world = RC_HOST(OV_GUESTV(render_world, NULL, 0));
  if (!world || !RC_LD(uint32_t, world + RC_OFF(render_world_scene))) return NULL;
  if (RC_LD(uint32_t, entity + RC_OFF(entity_flags)) & 1) return rc_mod_ptr(entity + RC_OFF(entity_root_node));
  return RC_HOST(OV_GUEST(world_entity_root, arg(entity), arg(world)));
}

// The game has put the inlay entity where the guitar's frame is; on a guitar built with 7/8 strings it moves half a
// string spacing per string beyond six towards string 6, either string order (the attach points say which way)
static void on_in1(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  uint8_t *guitar = host(SEVEN_GET(f, "guitar")), *head = host(SEVEN_GET(f, "head"));
  uint8_t *inlays = host(SEVEN_GET(f, "inlays"));
  if (!head || !inlays || !wants_seven(guitar)) return;
  // The centre moves half a spacing per added lane beyond string 0 and back half a spacing per lane beyond the far
  // string (a 6-string bass: low B and high C, the centre stays; an 8-string guitar: one whole spacing)
  const seven_span sp = span_of(guitar);
  const int net = sp.near - sp.far_lanes;
  if (!net) return;
  uint8_t *entity = rc_mod_ptr(inlays + RC_OFF(inlays_entity));
  uint8_t *frame = rc_mod_ptr(head + RC_OFF(guitar_head_frame));
  uint8_t *root = entity && frame ? entity_root(frame) : NULL;
  float a0[3], a1[3], p[3];
  if (!root || !attach(guitar, a0, a1)) return;
  static __thread uint8_t *pos;  // the game's memory for the position it is given
  if (!pos && !(pos = rc_guest_alloc(12))) return;
  ld3(p, root + RC_OFF(ni_world_translate));
  for (int i = 0; i < 3; i++) p[i] += 0.5f * (float)net * (a0[i] - a1[i]);
  st3(pos, p);
  OV_GUEST(entity_set_translation, arg(entity), arg(pos), 1);  // the Mac's third argument as its callers pass it
  if (once(&g_log_inlays))
    rsm_log("seven-string: frets: the inlays of an extended guitar are at the centre of its strings (%.3f, %.3f, %.3f)",
            p[0], p[1], p[2]);
}

// --- the chord frame's fit (B2 reads it) ---------------------------------------------------------------------------
int seven_grid_is_extended(const void *guitar) { return span_of(guitar).near > 0; }

int seven_grid_chord_frame(const void *guitar, seven_frame_fit *out, float b_out[3]) {
  const seven_span sp = span_of(guitar);
  if (!sp.near || !out) return 0;
  const uint8_t *g = guitar;
  const uint8_t *grid = rc_mod_ptr(g + RC_OFF(guitar_fretgrid));
  float a0[3], a1[3], b[3];
  if (!grid || !attach(g, a0, a1)) return 0;
  const uint8_t *bar0 = bar((uint8_t *)grid, 0);
  if (!RC_LD(uint8_t, bar0 + RC_OFF(fretbar_valid))) return 0;
  ld3(b, bar0 + RC_OFF(fretbar_b_local));
  if (b_out) memcpy(b_out, b, sizeof b);
  // top: the last string beyond string 0 (a guitar's 6 / 7, a bass's 6); the floor as it was moved (floor_lanes)
  return seven_frame_fit_for(a0, a1, b, seven_grid_floor_lanes(guitar), 5 + sp.near, sp.far, sp.far_lanes, out);
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

// --- the unrolled row functions (the Mac: clang unrolled the six-loops, so there is no count to edit) -------------
// Each point runs where the game has done its six cells or records of a row, and does 7 and 8 the game's way. A
// cell row is a valid byte and 8 cells (seven_fretgrid_cell bytes from +4); a fret-wire row a header of
// seven_fretgrid_wire_head bytes and 8 records of seven_fretgrid_wire_rec. None of them on Learn & Play, whose loops
// count to 8 by its edits.
static uint8_t *extra(uint8_t *at6, int k, int64_t size) { return at6 + k * size; }

// The cell row's constructor before its tail call for cell 5 (cell5 = that cell): cells 6 and 7
static void on_cellrow_ctor(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  uint8_t *c5 = host(SEVEN_GET(f, "cell5"));
  const int64_t cell = RC_OFF(seven_fretgrid_cell);
  for (int k = 1; k <= 2; k++) OV_GUEST(fretgrid_cell_ctor, arg(extra(c5, k, cell)));
}

// A fret-wire row's record of string s
static uint8_t *wire_rec(uint8_t *row, int s) {
  return row + RC_OFF(seven_fretgrid_wire_head) + s * RC_OFF(seven_fretgrid_wire_rec);
}

// The wire row's constructor after record 5: records 6 and 7
static void on_wirerow_ctor(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  uint8_t *row = host(SEVEN_GET(f, "row"));
  for (int s = 6; s < 8; s++) OV_GUEST(fretgrid_wire_ctor, arg(wire_rec(row, s)));
}

// The wire row's reset (the FretGrid unload) and flush, before their tail calls for record 5 (rec5): 6 and 7
static void on_wirerow_reset(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  uint8_t *r5 = host(SEVEN_GET(f, "rec5"));
  for (int k = 1; k <= 2; k++) OV_GUEST(fretgrid_wire_reset, arg(extra(r5, k, RC_OFF(seven_fretgrid_wire_rec))));
}
static void on_wirerow_flush(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  uint8_t *r5 = host(SEVEN_GET(f, "rec5"));
  for (int k = 1; k <= 2; k++) OV_GUEST(fretgrid_wire_flush, arg(extra(r5, k, RC_OFF(seven_fretgrid_wire_rec))));
}

// The wire row's "mark" (event 13) at its exit: records 6 and 7 as the game marks 0-5, a record whose state is not
// 2 gets its dirty byte and state 2
static void on_wirerow_mark(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on || (uint32_t)SEVEN_GET(f, "event") != 13) return;
  uint8_t *row = host(SEVEN_GET(f, "row"));
  const int64_t state = RC_OFF(seven_fretgrid_wire_state);
  for (int s = 6; s < 8; s++) {
    uint8_t *r = wire_rec(row, s);
    if (RC_LD(uint32_t, r + state) == 2) continue;
    RC_ST(uint8_t, r, 1);
    RC_ST(uint32_t, r + state, 2);
  }
}

// The FretGrid's destructor, in its loop over the wire rows from the last: at = the offset of the row's last record's
// second reference (record 7, +0x30 on the Mac); the game releases records 7..2, this releases 1 and 0 the same way
static void on_wires_dtor(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  uint8_t *grid = host(SEVEN_GET(f, "grid"));
  const int64_t at = (int64_t)SEVEN_GET(f, "at"), rec = RC_OFF(seven_fretgrid_wire_rec);
  for (int k = 6; k <= 7; k++)                 // records 1 and 0: k records below record 7
    for (int r = 0; r < 2; r++)                // its second reference, then its first
      OV_GUEST(ref_release, arg(grid + at - k * rec - r * rc_guest_ptr_size));
}

int seven_grid_init(void) {
  if (seven_mid("seven_f1", on_f1) || seven_mid("seven_f1_bar", on_f1_bar) ||
      seven_mid("seven_f1_scale", on_f1_scale) || seven_mid("seven_f1_setscale", on_f1_setscale) ||
      seven_mid("seven_f1_floor", on_f1_floor) || seven_mid("seven_in1", on_in1) ||
      seven_mid("seven_grid_cellrow_ctor", on_cellrow_ctor) || seven_mid("seven_grid_wirerow_ctor", on_wirerow_ctor) ||
      seven_mid("seven_grid_wirerow_reset", on_wirerow_reset) || seven_mid("seven_grid_wirerow_mark", on_wirerow_mark) ||
      seven_mid("seven_grid_wirerow_flush", on_wirerow_flush) || seven_mid("seven_grid_wires_dtor", on_wires_dtor))
    return -1;
  // The fixes of rs7string 1.0.0-beta.4, after the unit first shipped: each runs only when all its points are taken
  g_cam_on = seven_mid_optional("seven_cam", on_cam, "the song camera following the highway floor");
  static const struct {
    const char *name;
    rc_mid_fn fn;
  } glow[] = {
      {"seven_frg1", on_frg1},
      {"seven_frg2_scale", on_frg_scale},
      {"seven_frg2b_scale", on_frg_scale},
      {"seven_frg3_scale", on_frg_scale},
      {"seven_frg2_setscale", on_frg_setscale},
      {"seven_frg2b_setscale", on_frg_setscale},
      {"seven_frg3_setscale", on_frg_setscale},
  };
  int taken = 0;
  for (size_t i = 0; i < sizeof glow / sizeof *glow; i++)
    taken += seven_mid_optional(glow[i].name, glow[i].fn, "the fret range glow reaching all strings");
  g_glow_on = taken == (int)(sizeof glow / sizeof *glow);
  seven_edits_after("seven_glow", &g_glow_on);
  return 0;
}
