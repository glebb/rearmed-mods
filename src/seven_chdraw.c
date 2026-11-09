// Seven-String Guitar (seven.h): the chdraw part, a chord's note on the seventh string drawn as the game draws a
// chord's notes. Ported from rs7string (github.com/SomeeName/rs7string, MIT, by SomeeName): src/features/nslot (its
// N-slot render sites), src/features/chords (the chord loops over eight strings, the chord frame's span) and the chord
// frame's fit in src/features/frets; its ARCHITECTURE section 7.
//
// The game's own chord code draws the seventh string of a chord. The chords part hands that code a T8 record for
// every chord template and a C8 record for every ChordNotes record (the game's record, then arrays of eight strings:
// seven_chdraw.h says which the edits read), and the edits of seven_string make the code read those arrays over 7
// strings, 8 where rs7string's chord loops do: the gems on the seventh lane (the game's open-string bar for an open
// string 6), sustain tails, slides, bends and their notation, vibrato, the fret label and the chord's lowest fret,
// finger numbers on the neck, the next-note neck targets (on the grid part's next-note grid), the section string mask
// and the section dimming, the double stop of a stock string and string 6 (the chords part marks it at load), and the
// seventh string's vibration when the chord is hit. All of that is edits (no callback): what rs7string runs as callbacks there (the string mask test over
// eight, TM1-TM6; the chord pluck of string 6, PL1; the chord loops' bend bit, bend data and stop-pluck, CLBM, CLBA,
// CLVB) are instructions here, in place.
//
// What runs here is the chord frame on a guitar built with 7 strings (rs7string's B1 and B2). The frame model has the
// height of six lanes, and no game data sizes it: every chord frame of such a guitar is fitted to the lanes the chord
// uses. seven_b1 marks a frame when the chord box builder has made its geometry, with the highest string its chord
// uses (5 for strings 0-5 only); seven_b2, when Rnd_GemGeom_Build applies that geometry's span, scales the frame along
// the string axis so it covers lanes 0 to that string, its edge on the highway floor (the grid part moves the floor of
// inverted strings one spacing out). The game's own Rnd_Gem_SetScaleTranslate then sets what seven_b2 left in its
// arguments. Guitars built with 6 strings and bass keep the game's frames.
#include <math.h>
#include <stdarg.h>
#include <stdio.h>

#include "seven.h"

#define FRAME_AXIS 1  // the frame model's string axis (rs7string's [chords] frame_axis default: y)
#define MARKS 32      // frames marked and not yet built

#define PTR(f, name) ((uint8_t *)(uintptr_t)SEVEN_GET(f, name))
#define FIELD(p, name) ((uint8_t *)(p) + RC_OFF(name))

// ---- the log: at most this many lines of this part a run
static int g_logs = 16;

static void clog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void clog(const char *fmt, ...) {
  if (__atomic_sub_fetch(&g_logs, 1, __ATOMIC_RELAXED) < 0) return;
  char buf[320];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  rsm_log("seven-string: chord drawing: %s", buf);
}

// ---- guest memory
static uint8_t *ptr_at(const void *base, int64_t off) { return base ? rc_mod_ptr((const uint8_t *)base + off) : NULL; }
static void ld3(float v[3], const uint8_t *p) {
  for (int i = 0; i < 3; i++) v[i] = RC_LD(float, p + 4 * i);
}

// The count of strings a guitar (Rnd_Guitar) was built with when it is 7 or 8, else 0
static int seven_count(const uint8_t *guitar) {
  int n = guitar ? seven_strings_guitar_count(guitar) : 0;
  return n == 7 || n == 8 ? n : 0;
}

// ---- the frame's fit (rs7string's frets.cpp ChordFrame)
typedef struct {
  float scale;    // along the string axis, on the model's extent
  float shift;    // the origin edge moved, from it towards the lanes
  float origin;   // the origin edge as the game places it (0 at string 0, string i at -i spacings)
  float margin;   // the game's margin beyond the outer lane
  float spacing;  // the string spacing
} frame_fit;

// The highway floor: b = a fret wire's bottom end (guitar-local). Beyond string 0 on string 6's side (inverted strings)
// the grid part moves it one string spacing further out (its seven_f1_floor): 1 then.
static int floor_moved(const float a0[3], const float a1[3], const float b[3]) {
  float len2 = 0.0f, side = 0.0f;
  for (int i = 0; i < 3; i++) {
    const float u = a0[i] - a1[i];
    len2 += u * u;
    side += (b[i] - a0[i]) * u;
  }
  return len2 > 1e-6f && len2 < 1e6f && side > 0.0f;
}

// A chord frame is one model whose origin edge sits on the fret wires' bottom end b (the highway floor) and which
// extends towards the lanes by H = 5d + 2m (d the string spacing, m the game's margin beyond the outer lane of lanes
// 0-5; rs7string's HYPOTHESIS: the model is symmetric about lanes 0-5). It must cover lanes 0-5, or 0-top (top 6/7: the
// highest string the chord uses), each with margin m. The origin edge stays on the floor; only the far edge moves
// (scale), except where the origin edge lies inside the outermost lane and its margin (a floor that was not moved): then
// it moves out too (shift). a0 / a1 = the string 0 / 1 attach points, b = fret bar 0's bottom end, all guitar-local;
// moved = the floor was moved by a spacing. 0 for a degenerate geometry (out untouched).
static int chord_frame(const float a0[3], const float a1[3], const float b[3], int moved, int top, frame_fit *out) {
  float u[3], len2 = 0.0f;
  for (int i = 0; i < 3; i++) {
    u[i] = a0[i] - a1[i];
    len2 += u[i] * u[i];
  }
  const float d = sqrtf(len2);
  if (!(d > 1e-3f) || !(d < 1e3f)) return 0;
  float pb = 0.0f;  // along u (string 1 -> string 0 -> string 6), 0 at string 0: string i lies at -i d
  for (int i = 0; i < 3; i++) pb += (b[i] - a0[i]) * u[i] / d;
  const int six = pb > 0.0f;                  // the origin edge lies beyond string 0, on string 6's side
  const float m = six ? pb : -pb - 5.0f * d;  // the game's margin beyond lane 0 / lane 5
  if (!(m > 0.05f * d) || !(m < 4.0f * d) || !isfinite(m)) return 0;
  const float h = 5.0f * d + 2.0f * m;                         // the model's extent
  const float po = pb + (six && moved ? d : 0.0f);             // the origin edge as placed now
  const float lo = -5.0f * d - m;                              // string 5's end, margin m
  const float hi = (top > 5 ? (float)(top - 5) * d : 0.0f) + m;  // string `top`'s (or string 0's) end, margin m
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

// The fit of a chord frame up to string `top` on this guitar, from its string 0 / 1 attach points and fret bar 0's
// bottom end (rs7string's Rs7Frets_ChordFrame). 0 while its strings or bars are not there, or for a degenerate geometry.
static int guitar_frame(const uint8_t *guitar, int top, frame_fit *out, float b_out[3]) {
  const uint8_t *strings = ptr_at(guitar, RC_OFF(guitar_strings));
  const uint8_t *grid = ptr_at(guitar, RC_OFF(guitar_fretgrid));
  const uint8_t *s0 = ptr_at(strings, RC_OFF(guitar_strings_string));
  const uint8_t *s1 = ptr_at(strings, RC_OFF(guitar_strings_string) + rc_guest_ptr_size);
  if (!grid || !s0 || !s1) return 0;
  float a0[3], a1[3], b[3];
  const uint8_t *bar0 = FIELD(grid, fretgrid_bars);
  ld3(a0, s0 + RC_OFF(guitar_string_attach));
  ld3(a1, s1 + RC_OFF(guitar_string_attach));
  ld3(b, bar0 + RC_OFF(fretbar_b_local));
  for (int i = 0; i < 3; i++) b_out[i] = b[i];
  const int moved = RC_LD(uint8_t, bar0 + RC_OFF(fretbar_valid)) && floor_moved(a0, a1, b);
  return chord_frame(a0, a1, b, moved, top, out);
}

// ---- the frames marked (B1) and not yet built (B2). A spin lock: the builder and the build may be other threads.
static struct {
  const uint8_t *geom;  // NULL: a free entry
  const uint8_t *guitar;
  int top;
} g_marks[MARKS];
static int g_next, g_live, g_lock;

static void lock(void) {
  while (__atomic_exchange_n(&g_lock, 1, __ATOMIC_ACQUIRE)) {
  }
}
static void unlock(void) { __atomic_store_n(&g_lock, 0, __ATOMIC_RELEASE); }

// The highest string 6 / 7 of a chord on a guitar built with `count` strings, else 5 (strings 0-5 only). rs7string's
// Rs7Chord_FrameTop over the song's strings: here the guitar's, which a 7-string song builds (the strings part).
static int frame_top(const void *tpl, int count) {
  const uint8_t *t8 = tpl ? seven_chords_as_t8(tpl) : NULL;  // the chords part's (seven_chords.h)
  for (int s = (count > SEVEN_CAPACITY ? SEVEN_CAPACITY : count) - 1; t8 && s >= SEVEN_STOCK; s--)
    if (seven_chords_t8_fret(t8, s) >= 0) return s;
  return SEVEN_STOCK - 1;
}

// B1: the chord box builder has made the chord frame's geometry (geom, 0 when its allocation failed) for the chord
// render object `chord` (its template at chord_render_template) on the play session `session`. On a guitar built with
// 7 or 8 strings it is marked, every frame (rs7string's Rs7Chord_OnBoxGeom): a chord of strings 0-5 covers lanes 0-5
// from a floor that moved (else it is the game's frame), a chord with string 6 reaches its lane.
static void on_b1(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  const uint8_t *geom = PTR(f, "geom"), *chord = PTR(f, "chord"), *session = PTR(f, "session");
  const uint8_t *guitar = geom && chord ? ptr_at(session, RC_OFF(session_guitar)) : NULL;
  const int count = seven_count(guitar);
  if (!count) return;
  const int top = frame_top(ptr_at(chord, RC_OFF(chord_render_template)), count);
  lock();
  for (int i = 0; i < MARKS; i++)
    if (g_marks[i].geom == geom) {  // a geometry made again at the same address: its new mark only
      g_marks[i].geom = NULL;
      g_live--;
    }
  if (g_marks[g_next].geom) g_live--;  // the oldest mark goes
  g_marks[g_next].geom = geom;
  g_marks[g_next].guitar = guitar;
  g_marks[g_next].top = top;
  g_next = (g_next + 1) % MARKS;
  g_live++;
  unlock();
}

// B2: Rnd_GemGeom_ApplySpan has computed the span of a geometry (scale (1, 1, its height), translation (0, 0, its
// middle)) and is about to set them on its entity. Called from Rnd_GemGeom_Build for a component B1 marked (once: Build
// runs once a component), the scale and translation get the frame's fit along the string axis (rs7string's
// Rs7Chord_BoxScale and its B2 stub, which set the same through the same game function).
static void on_b2(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on || !__atomic_load_n(&g_live, __ATOMIC_RELAXED)) return;
  if ((uint32_t)SEVEN_GET(f, "from") != (uint32_t)RC_ADDR(gemgeom_build_span_ret)) return;
  const uint8_t *component = PTR(f, "component");
  const uint8_t *guitar = NULL;
  int top = 0;
  lock();
  for (int i = 0; i < MARKS && component; i++)
    if (g_marks[i].geom == component) {
      guitar = g_marks[i].guitar;
      top = g_marks[i].top;
      g_marks[i].geom = NULL;
      g_live--;
      break;
    }
  unlock();
  uint8_t *scale = PTR(f, "scale"), *trans = PTR(f, "translate");
  if (!guitar || !scale || !trans) return;
  frame_fit fit;
  float b[3];
  if (!guitar_frame(guitar, top, &fit, b)) {
    clog("chord frame up to string %d: no lane geometry on its guitar: the game's frame", top);
    return;
  }
  if (fabsf(fit.scale - 1.0f) < 1e-4f && fabsf(fit.shift) < 1e-3f) return;  // exactly the game's frame
  const int64_t at = 4 * FRAME_AXIS;
  RC_ST(float, scale + at, RC_LD(float, scale + at) * fit.scale);
  RC_ST(float, trans + at, RC_LD(float, trans + at) + fit.shift);
  clog("chord frame up to string %d: string axis scale %.4f, origin edge moved %.4f (spacing %.4f, margin %.4f, "
       "origin %.4f; floor end (%.3f %.3f %.3f))",
       top, fit.scale, fit.shift, fit.spacing, fit.margin, fit.origin, b[0], b[1], b[2]);
}

int seven_chdraw_init(void) {
  if (seven_mid("seven_b1", on_b1) || seven_mid("seven_b2", on_b2)) return -1;
  return 0;
}
