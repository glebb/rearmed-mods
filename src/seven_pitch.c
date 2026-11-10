// Seven-String Guitar (seven.h): the pitch part, the low B detected and scored. After rs7string's native pitch
// storage, the song's 7-string mode and its publication ports (github.com/SomeeName/rs7string, MIT, by SomeeName:
// src/features/pitch, src/features/publish).
//
// The detection (the game's pitch detection, rs7string's "detector") and the matcher grow by the edits of seven_string
// (PA1 0x13b8 -> 0x13e0 bytes, PA2 0x58 -> 0x6c): Play_DetectInput_Init is the only place either is made, and the edits
// are on before the game runs, so every one has room for the tails (rs7string asked _msize for objects made before
// its patch; there are none here). The tails are kept here:
//   seven_pm0 / seven_pr0     a new matcher / a reset detection: tuning 0, the detection's open notes and count
//   seven_s3 / seven_pt1 / seven_pm1   each SetTuning: the vector's first eight entries; the tuner's re-apply of the
//                             profile's six strings (Play_Tuning_ApplyToDetection) keeps a song's strings 6/7
//   seven_s4                  song setup: a 7/8-entry guitar tuning on a guitar built with that many strings widens
//                             the detection to them (its count and lowest open note)
// and read by the game's own code (edits: PUBNL, PUBAN read openMidi8 and the count) and here:
//   seven_s1                  the expected pitch of a tuning pointer (0x13f3d60) past six strings: its eleven callers
//                             (rs7string's PC1-PC4, PUBZ0-PUBZ6) pass a matcher's, a detection's or the standard
//                             tuning, found here by its address (the registry below)
//   seven_i1                  the inverse (MIDI -> fret) for strings 6/7
//   seven_pubcl(_base)        a played note's string and fret over 7/8 strings
// The vector-tuning expected pitch (rs7string's PE0) is an edit. The FretGrid follows the guitar's built count:
// seven_pubgb builds cells 6/7 once the strings 6/7 are loaded, seven_pubga their attach points, seven_pubwu and
// seven_pubedge its loops; the bounds are edits (PUBGS, PUBGE1/2, PUBGC, PUBGO, R1).
#include <string.h>

#include "seven.h"

// The strings part: the count the guitar (Rnd_Guitar) was built with, InstrumentInfo +0xb as C-lite sets it (4, 6, 7,
// 8), 0 for none or an InstrumentInfo without the native layout
extern int seven_strings_guitar_count(const void *guitar);
// 0 when the guitar's string objects 6 and 7 (main and ghost) exist and their models are loaded, else the first
// failing check (rs7string's Rs7Gs_ExtraWhy)
extern int seven_strings_extra_why(void *guitar);
// The guitar's main string object of string 0-7 (a host pointer), NULL without one (rs7gs::Child(gs, s, false))
extern void *seven_strings_child(void *guitar, int string);
// The attach point of string 6 or 7 (three floats to out), extrapolated from strings 0 and 1; 1 when written
// (rs7string's Rs7Gs_AttachExt)
extern int seven_strings_attach_ext(void *info, unsigned string, float *out);

// MIDI note of each open string at tuning offset 0: the game's six, then B1 and F#1; a bass's four
static const int32_t g_gbase[8] = {40, 45, 50, 55, 59, 64, 35, 30};
static const int32_t g_bbase[4] = {28, 33, 38, 43};

#define MAGIC 0x38503752u  // "R7P8": a tail the writers below have set
enum { MATCHER, DETECTION };

static uint8_t *P(uint64_t v) { return (uint8_t *)(uintptr_t)v; }
static uint64_t arg(const void *p) { return (uint64_t)(uintptr_t)p; }
static int once(int *flag) { return !__atomic_exchange_n(flag, 1, __ATOMIC_RELAXED); }

static int bass(const uint8_t *det) { return RC_LD(int32_t, det + RC_OFF(detection_instrument)) == 2; }

// A std::vector<int16_t> of the game: its entries (host pointer) and count, 0 for an odd or empty one
static size_t vec(const uint8_t *v, const int16_t **at) {
  const uint8_t *b = v ? rc_mod_ptr(v + RC_OFF(std_vector_begin)) : NULL;
  const uint8_t *e = v ? rc_mod_ptr(v + RC_OFF(std_vector_end)) : NULL;
  if (!b || e < b || (e - b) % 2) return 0;
  *at = (const int16_t *)b;
  return (size_t)(e - b) / 2;
}

// --- the registry of tails ------------------------------------------------------------------------------------
// Every matcher and detection whose tail was written, by its guest address (bit 0: a detection). seven_s1 and seven_i1
// get only a tuning pointer and find its object here: nothing is read from an address this has not seen written.
// Writers (song setup, the tuner, a new detection) take a spin lock; readers compare words. Full: the least recently
// written goes (a live object is written at every tuning change; rs7string saw 2 at most), logged once.
#define TAILED_MAX 32
static uint64_t g_tailed[TAILED_MAX];
static uint32_t g_tailed_age[TAILED_MAX], g_tailed_clock;
static int g_tailed_lock, g_log_full;

static uint64_t key(const void *obj, int kind) { return RC_GUEST(obj) << 1 | (uint64_t)kind; }

static int64_t magic_off(int kind) { return kind ? RC_OFF(seven_detection_magic) : RC_OFF(seven_matcher_magic); }

static void lock(void) {
  while (__atomic_exchange_n(&g_tailed_lock, 1, __ATOMIC_ACQUIRE)) {
  }
}
static void unlock(void) { __atomic_store_n(&g_tailed_lock, 0, __ATOMIC_RELEASE); }

// The magic that marks a valid tail, and the object in the registry
static void mark_tailed(uint8_t *obj, int kind) {
  RC_ST(uint32_t, obj + magic_off(kind), MAGIC);
  const uint64_t k = key(obj, kind);
  lock();
  unsigned slot = 0;
  for (unsigned i = 0; i < TAILED_MAX; i++) {
    if (g_tailed[i] == k) {
      slot = i;
      break;
    }
    if (g_tailed_age[i] < g_tailed_age[slot]) slot = i;
  }
  const int evicted = g_tailed[slot] && g_tailed[slot] != k;
  __atomic_store_n(&g_tailed[slot], k, __ATOMIC_RELEASE);
  g_tailed_age[slot] = ++g_tailed_clock;
  unlock();
  if (evicted && once(&g_log_full))
    rsm_log("seven-string: pitch: the registry of tails is full (%d), the least recently written goes", TAILED_MAX);
}

static int tailed(const uint8_t *obj, int kind) {
  if (!obj) return 0;
  const uint64_t k = key(obj, kind);
  for (unsigned i = 0; i < TAILED_MAX; i++)
    if (__atomic_load_n(&g_tailed[i], __ATOMIC_ACQUIRE) == k) return RC_LD(uint32_t, obj + magic_off(kind)) == MAGIC;
  return 0;
}

// Entry s (6 or 7) of the eight-entry tuning behind a tuning pointer the game passes: the standard tuning's (0), a
// matcher's or a detection's tail. 0 for any other pointer.
static int tail_entry(const uint8_t *tuning, unsigned s, int16_t *out) {
  if (!tuning) return 0;
  if ((uint64_t)(uintptr_t)tuning == RC_GA(standard_tuning)) {
    *out = 0;
    return 1;
  }
  const uint8_t *m = tuning - RC_OFF(matcher_tuning);
  if (tailed(m, MATCHER)) {
    *out = RC_LD(int16_t, m + RC_OFF(seven_matcher_tuning8) + 2 * s);
    return 1;
  }
  const uint8_t *d = tuning - RC_OFF(detection_tuning);
  if (tailed(d, DETECTION)) {
    *out = RC_LD(int16_t, d + RC_OFF(seven_detection_tuning8) + 2 * s);
    return 1;
  }
  return 0;
}

// --- the song's strings per object ------------------------------------------------------------------------------
// The last SetTuning with more than six entries of a matcher or detection (a 7/8-string song): a re-apply of the
// profile's six strings keeps its strings 6/7. And the count a song widened a detection to (seven_s4), which the
// SetTuning song setup runs after it keeps. Game thread; a few objects (rs7string: 4 each).
static struct {
  const void *obj;
  size_t n;
} g_song[4];
static struct {
  const void *det;
  unsigned count;
} g_count[4];

static void forget_song(const void *obj) {
  for (int i = 0; i < 4; i++)
    if (g_song[i].obj == obj) g_song[i].obj = NULL, g_song[i].n = 0;
}

// t = the new vector's first eight entries (n of them); tail = the object's tail before. Returns the entries kept.
static size_t keep_song(const void *obj, const int16_t *tail, int16_t t[8], size_t n, int apply) {
  int r = -1;
  for (int i = 0; i < 4 && r < 0; i++)
    if (g_song[i].obj == obj) r = i;
  if (n > 6) {  // a song with more strings: remembered
    for (int i = 0; i < 4 && r < 0; i++)
      if (!g_song[i].obj) r = i;
    if (r < 0) r = 0;
    g_song[r].obj = obj;
    g_song[r].n = n > 8 ? 8 : n;
    return n;
  }
  if (r >= 0 && apply) {  // the profile's six strings while the tuner re-applies them: the song's 6/7 stay
    for (size_t s = 6; s < g_song[r].n; s++) t[s] = tail[s];
    return g_song[r].n;
  }
  if (r >= 0) g_song[r].obj = NULL, g_song[r].n = 0;  // a six-string song replaces it
  return n;
}

static void set_song_count(const void *det, unsigned count) {
  for (int i = 0; i < 4; i++)
    if (g_count[i].det == det) {
      g_count[i].det = count ? det : NULL;
      g_count[i].count = count;
      return;
    }
  if (!count) return;
  for (int i = 0; i < 4; i++)
    if (!g_count[i].det) {
      g_count[i].det = det;
      g_count[i].count = count;
      return;
    }
  g_count[0].det = det;  // full: the oldest is a freed detection's
  g_count[0].count = count;
}

static unsigned song_count(const void *det) {
  for (int i = 0; i < 4; i++)
    if (det && g_count[i].det == det) return g_count[i].count;
  return 0;
}

// --- the tails (rs7string's pitch.cpp, hooks.cpp) -------------------------------------------------------------
static void fill_tuning(const int16_t *v, size_t n, int16_t out[8]) {
  for (size_t i = 0; i < 8; i++) out[i] = i < n ? RC_LD(int16_t, v + i) : 0;
}

static void put8(uint8_t *dst, const int16_t t[8]) {
  for (int i = 0; i < 8; i++) RC_ST(int16_t, dst + 2 * i, t[i]);
}

// The open notes as the game's unrolled writers make them, for eight strings (16-bit wrap as their `add dx, imm`); a
// bass's strings past 3 keep the raw offset, as the game's do
static void fill_open(const int16_t t[8], int is_bass, int16_t out[8]) {
  for (int i = 0; i < 8; i++) {
    const int base = is_bass ? (i < 4 ? g_bbase[i] : 0) : g_gbase[i];
    out[i] = (int16_t)(uint16_t)((uint16_t)t[i] + (uint16_t)base);
  }
}

// The lowest open note after SetTuning: the game's (string 0's) lowered to that of strings 6..n-1, never raised
static int lowest(int game, const int16_t t[8], size_t n, int is_bass) {
  if (is_bass) return game;
  int low = game;
  for (size_t s = 6; s < (n < 8 ? n : 8); s++)
    if (g_gbase[s] + t[s] < low) low = g_gbase[s] + t[s];
  return low;
}

static void write_det(uint8_t *det, const int16_t t[8]) {
  int16_t open[8];
  const int b = bass(det);
  fill_open(t, b, open);
  put8(det + RC_OFF(seven_detection_tuning8), t);
  put8(det + RC_OFF(seven_detection_open8), open);
  const unsigned song = b ? 0 : song_count(det);  // a song's 7/8 (seven_s4) outlives the SetTuning after it
  RC_ST(uint32_t, det + RC_OFF(seven_detection_count), song ? song : b ? 4 : 6);
  mark_tailed(det, DETECTION);
}

static void det_reset(uint8_t *det) {
  forget_song(det);  // a new / reset detection has no song yet
  set_song_count(det, 0);
  const int16_t zero[8] = {0};
  write_det(det, zero);  // the game's reset zeroes tuning[6] and makes the open notes from it
}

static void det_tuned(uint8_t *det, const int16_t *e, size_t n, int apply) {
  int16_t t[8], before[8];
  fill_tuning(e, n, t);
  const uint8_t *tail = det + RC_OFF(seven_detection_tuning8);
  for (int i = 0; i < 8; i++) before[i] = RC_LD(int16_t, tail + 2 * i);
  n = keep_song(det, RC_LD(uint32_t, det + RC_OFF(seven_detection_magic)) == MAGIC ? before : t, t, n, apply);
  write_det(det, t);
  uint8_t *low = det + RC_OFF(detection_lowest_open);
  RC_ST(int32_t, low, lowest(RC_LD(int32_t, low), t, n, bass(det)));
}

static void mat_new(uint8_t *m) {
  forget_song(m);
  const int16_t zero[8] = {0};
  put8(m + RC_OFF(seven_matcher_tuning8), zero);  // the game's constructor zeroes its tuning[6]
  mark_tailed(m, MATCHER);
}

static void mat_tuned(uint8_t *m, const int16_t *e, size_t n, int apply) {
  int16_t t[8], before[8];
  fill_tuning(e, n, t);
  const uint8_t *tail = m + RC_OFF(seven_matcher_tuning8);
  for (int i = 0; i < 8; i++) before[i] = RC_LD(int16_t, tail + 2 * i);
  keep_song(m, RC_LD(uint32_t, m + RC_OFF(seven_matcher_magic)) == MAGIC ? before : t, t, n, apply);
  put8(m + RC_OFF(seven_matcher_tuning8), t);
  mark_tailed(m, MATCHER);
}

// --- the FretGrid build's cells 6/7 and its bounded wait (rs7string's hooks_grid.cpp, grid.cpp) -------------------
// Rnd_FretGrid_Load polls the build until it says complete (2); a guitar whose strings 6/7 are still loading is "not
// ready" (1). One entry per grid that waits; after WAIT_MS the build completes as the game's, without cells 6/7, and
// a guitar built with 7/8 strings is marked failed: its songs keep six strings (seven_s4). An entry belongs to one
// build: another owner, or a poll WAIT_STALE_MS after the last (the grid's address reused), starts a new one.
#define WAIT_MS 5000.0
#define WAIT_STALE_MS 10000.0
static struct {
  const void *grid, *owner;
  double since, last;
  unsigned calls, reason;
  int failed;
} g_wait[8];
static int g_wait_lines;

static int wait_step(const void *grid, const void *owner, unsigned reason, double now) {
  int e = -1;
  for (int i = 0; i < 8 && e < 0; i++)
    if (g_wait[i].grid == grid) e = i;
  if (e >= 0 && ((owner && g_wait[e].owner && g_wait[e].owner != owner) ||
                 (now > g_wait[e].last && now - g_wait[e].last > WAIT_STALE_MS))) {
    memset(&g_wait[e], 0, sizeof g_wait[e]);  // another build's: its deadline and failure do not carry over
    e = -1;
  }
  if (e < 0) {  // a new grid: a free entry, else the oldest
    e = 0;
    for (int i = 0; i < 8; i++) {
      if (!g_wait[i].grid) {
        e = i;
        break;
      }
      if (g_wait[i].since < g_wait[e].since) e = i;
    }
    memset(&g_wait[e], 0, sizeof g_wait[e]);
    g_wait[e].grid = grid;
    g_wait[e].owner = owner;
    g_wait[e].since = now;
  }
  g_wait[e].calls++;
  g_wait[e].reason = reason;
  g_wait[e].last = now;
  return now - g_wait[e].since >= WAIT_MS;
}

static void wait_done(const void *grid, const void *owner) {
  for (int i = 0; i < 8; i++)
    if (g_wait[i].grid == grid || (owner && g_wait[i].owner == owner)) memset(&g_wait[i], 0, sizeof g_wait[i]);
}

int seven_pitch_neck_failed(const void *guitar) {
  for (int i = 0; guitar && i < 8; i++)
    if (g_wait[i].failed && g_wait[i].owner == guitar) return 1;
  return 0;
}

void seven_pitch_grid_forget_all(void) { memset(g_wait, 0, sizeof g_wait); }

// 1 (keep polling), or 2 once the grid has waited WAIT_MS (the game's completion without cells 6/7)
static int not_ready(void *grid, void *guitar, unsigned reason) {
  const int give_up = wait_step(grid, guitar, reason, rsm_now() * 1000.0);
  if (give_up) {
    const int built = seven_strings_guitar_count(guitar);
    if (built == 7 || built == 8)
      for (int i = 0; i < 8; i++)
        if (g_wait[i].grid == grid) g_wait[i].failed = 1;
    if (g_wait_lines++ < 100)
      rsm_log("seven-string: publication: the FretGrid of guitar %p waited 5 s for its strings 6/7 (reason 0x%x): %s",
              guitar, reason,
              built == 7 || built == 8 ? "it completes without cells 6/7 and its songs keep six strings"
                                       : "it completes as the game's (not built with 7/8 strings)");
  }
  return give_up ? 2 : 1;
}

// The string's model is loaded, as the build's own loop asks it of strings 0-5 (0x1110f8d..0x1110fc5): the entity,
// the render world's scene, the entity's root node; where the game asks one function for all of it (the Mac's
// 0x1002a2ba0, as its build's loop does), that function
static int model_ready(uint8_t *child) {
  uint8_t *entity = child ? rc_mod_ptr(child + RC_OFF(guitar_string_entity)) : NULL;
  if (!entity) return 0;
  if (!seven_has_render_world()) return OV_GUEST(entity_model_root, arg(entity)) != 0;
  uint8_t *world = RC_HOST(OV_GUESTV(render_world, NULL, 0));
  if (!world || !RC_LD(uint32_t, world + RC_OFF(render_world_scene))) return 0;
  if (RC_LD(uint32_t, entity + RC_OFF(entity_flags)) & 1) return rc_mod_ptr(entity + RC_OFF(entity_root_node)) != NULL;
  return OV_GUEST(world_entity_root, arg(entity), arg(world)) != 0;
}

// The build's status at its complete exit: cells 6/7 of the first `frets` rows from cells 0/1 once the guitar's
// strings 6/7 are loaded, with their world points (local + the guitar's offset: the build may come after this
// frame's ApplyOffset). A guitar built with 6 strings gets them without waiting (nothing shows them); a bass, or a
// guitar without the native layout, completes as the game's.
static int grid_build(uint8_t *grid, uint8_t *guitar) {
  uint8_t *info = guitar ? rc_mod_ptr(guitar + RC_OFF(guitar_info)) : NULL;
  const int built = guitar ? seven_strings_guitar_count(guitar) : 0;
  if (!grid || !info || built < 6) return 2;
  if (built > 6) {
    const int why = seven_strings_extra_why(guitar);
    if (why) return not_ready(grid, guitar, 0x100 | (unsigned)why);
    for (int s = 6; s < built && s < 8; s++)  // a guitar loads the strings it was built with (seven_strings.c)
      if (!model_ready(seven_strings_child(guitar, s))) return not_ready(grid, guitar, 3);
  }
  const unsigned frets = RC_LD(uint8_t, info + RC_OFF(instrument_frets));
  if (frets > 26) return built > 6 ? not_ready(grid, guitar, 4) : 2;
  if (seven_grid_fill_extra_cells(grid, frets) != (int)frets && built > 6) return not_ready(grid, guitar, 5);
  wait_done(grid, guitar);  // ready: this build's wait and any failure are over
  float offset[3];
  for (int i = 0; i < 3; i++) offset[i] = RC_LD(float, guitar + RC_OFF(guitar_offset) + 4 * i);
  const int64_t local = RC_OFF(seven_fretgrid_cell_local), world = RC_OFF(seven_fretgrid_cell_world);
  for (unsigned r = 0; r < frets; r++)
    for (int s = 6; s < 8; s++) {
      uint8_t *c = seven_grid_cell(grid, r, (unsigned)s);
      for (int i = 0; c && i < 9; i++) RC_ST(float, c + world + 4 * i, RC_LD(float, c + local + 4 * i) + offset[i % 3]);
    }
  return 2;
}

// --- a played note's string and fret over 7/8 strings (rs7string's publish.cpp Classify) ------------------------
// The game's search (0x13f5020..0x13f51b2, unrolled for six) in order: each string but the note's own, the expected
// note tuning + base + the note's fret; the first within 2 of the played note is its string (result 2 exact, 1 near),
// its fret moved by the difference (at least 0). The search stops at the first miss unless the result is 7 (32-bit
// wrap as the game's).
static int classify(uint8_t *r, const uint8_t *tuning8, unsigned n, int midi, int initial) {
  const unsigned own = RC_LD(uint8_t, r + RC_OFF(tracked_note_string));
  const int fret = RC_LD(uint8_t, r + RC_OFF(tracked_note_fret));
  for (unsigned s = 0; s < n; s++) {
    if (s == own) continue;
    const uint32_t expected = (uint32_t)g_gbase[s] + (uint32_t)(int32_t)RC_LD(int16_t, tuning8 + 2 * s) + (uint32_t)fret;
    const int32_t delta = (int32_t)((uint32_t)midi - expected);
    const int32_t distance = delta < 0 ? (int32_t)(0u - (uint32_t)delta) : delta;
    if (distance <= 2) {
      const int32_t f = delta == 0 ? fret : (int32_t)((uint32_t)fret + (uint32_t)delta);
      RC_ST(int32_t, r + RC_OFF(tracked_note_played_string), (int32_t)s);
      RC_ST(int32_t, r + RC_OFF(tracked_note_played_fret), f < 0 ? 0 : f);
      return delta == 0 ? 2 : 1;
    }
    if (initial != 7) break;
  }
  return initial;
}

// --- the mid points -----------------------------------------------------------------------------------------------
// A new matcher (eax), its fields set but +0x40 and +0x38
static void on_pm0(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  uint8_t *m = P(SEVEN_GET(f, "matcher"));
  if (m) mat_new(m);
}

// Play_Detector_Reset (ebx = the detection), after its open notes: from the detection's constructor and from
// Play_DetectInput's start
static void on_pr0(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  uint8_t *det = P(SEVEN_GET(f, "detection"));
  if (det) det_reset(det);
}

// Play_Detector_SetTuning's exits, at their ret: the game copied the vector's six entries and made the open notes
// and the lowest open note. `from` is its return address: the tuner's profile re-apply keeps a song's strings 6/7.
// The tuning is the vector (`tuning`), or where the vector is not kept at the ret its entries and their count
// (`entries`, `count`: the Mac's).
static void on_set_tuning(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  uint8_t *det = P(SEVEN_GET(f, "detection"));
  if (!det) return;
  const int16_t *e = NULL;
  size_t n;
  // A 32-bit game's point has the vector (`tuning`); a 64-bit one's its entries and count (asked once, quietly)
  static int tv = -2;
  if (tv == -2) tv = rc_guest_ptr_size == 4 ? rc_mid_value(f->point, "tuning") : -1;
  if (tv >= 0) {
    n = vec(P(rc_mid_get(f, tv)), &e);
  } else {
    e = (const int16_t *)P(SEVEN_GET(f, "entries"));
    n = e ? (size_t)SEVEN_GET(f, "count") : 0;
  }
  det_tuned(det, e, n, seven_at(SEVEN_GET(f, "from"), RC_ADDR(tuning_apply_detection_ret)));
}

// Play_Matcher_SetTuning (the matcher, the vector, its return address): after its six-entry copy, or before it where
// the copy reads nothing of the tail (the Mac's)
static void on_pm1(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  uint8_t *m = P(SEVEN_GET(f, "matcher"));
  const int16_t *e = NULL;
  const size_t n = m ? vec(P(SEVEN_GET(f, "tuning")), &e) : 0;
  if (m) mat_tuned(m, e, n, seven_at(SEVEN_GET(f, "from"), RC_ADDR(tuning_apply_matcher_ret)));
}

// Song setup hands the SNG tuning to the detection (the SetTuning after this): a 7/8-entry guitar tuning on a guitar
// built with that many strings (the strings part's count) widens the detection, its count and lowest open note over
// all strings; anything else (6 strings, bass, another built count, a failed neck) stays the game's width.
static int g_song_lines;

static void song_note(const char *what, unsigned a, unsigned b) {
  if (g_song_lines++ < 200) rsm_log("seven-string: pitch: song detection %s (%u, %u)", what, a, b);
}

static void song_setup(uint8_t *det, const uint8_t *tuning, uint8_t *session) {
  const int16_t *e = NULL;
  const size_t n = vec(tuning, &e);
  if (!det || !session || !e || n > 8) return;
  const int b = bass(det);
  const unsigned count = b ? 4 : n == 7 || n == 8 ? (unsigned)n : 6;
  set_song_count(det, 0);  // a previous song of this detection does not carry over
  if (count == 6 || b || (n != 7 && n != 8)) return;
  uint8_t *guitar = rc_mod_ptr(session + RC_OFF(session_guitar));
  const int built = guitar ? seven_strings_guitar_count(guitar) : 0;
  if (seven_pitch_neck_failed(guitar)) {
    song_note("not widened: its guitar's neck failed (no cells 6/7)", count, (unsigned)built);
    return;
  }
  if (built != (int)count) {
    song_note("not widened: the guitar was built with another count (song, guitar)", count, (unsigned)built);
    return;
  }
  if (!tailed(det, DETECTION)) {
    song_note("not widened: the detection has no tail", count, 0);
    return;
  }
  int16_t t[8];
  fill_tuning(e, n, t);
  int low = g_gbase[0] + t[0];
  for (unsigned s = 1; s < count; s++)
    if (g_gbase[s] + t[s] < low) low = g_gbase[s] + t[s];
  set_song_count(det, count);
  RC_ST(uint32_t, det + RC_OFF(seven_detection_count), count);
  RC_ST(int32_t, det + RC_OFF(detection_lowest_open), low);
  song_note("widened (strings, lowest open note)", count, (unsigned)low);
}

static void on_s4(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  song_setup(P(SEVEN_GET(f, "detection")), P(SEVEN_GET(f, "tuning")), P(SEVEN_GET(f, "session")));
}

// The same where song setup hands the SetTuning an input id (the Mac's): its Play_DetectInput's detection
static void on_s4_input(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  uint8_t *input = RC_HOST(OV_GUEST(play_detect_input, (uint32_t)SEVEN_GET(f, "input")));
  uint8_t *det = input ? rc_mod_ptr(input + RC_OFF(detector_detection)) : NULL;
  song_setup(det, P(SEVEN_GET(f, "tuning")), P(SEVEN_GET(f, "session")));
}

// 0x13f3d60 (ecx = an int16 tuning, edx = instrument - 2, esi = string, the fret on the stack) where a string past
// its table joins (eax = -1, then + fret): a guitar's strings 6/7 take the base and the entry of the eight-entry tuning
// behind the pointer. Bass (edx 0) and other pointers as the game's.
static void on_s1(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  const uint32_t s = (uint32_t)SEVEN_GET(f, "string");
  if ((s != 6 && s != 7) || !(uint32_t)SEVEN_GET(f, "instrument2")) return;
  int16_t t;
  if (tail_entry(P(SEVEN_GET(f, "tuning")), s, &t)) SEVEN_SET(f, "pitch", (uint32_t)(g_gbase[s] + t));
}

// Play_MidiToFret (eax = instrument, ecx = MIDI, edx = string, esi = the tuning or 0): a guitar's string 6/7, MIDI -
// base - the tail's entry, returned in eax and ecx as the game's own cases
static void on_i1(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  const uint32_t s = (uint32_t)SEVEN_GET(f, "string");
  if ((s != 6 && s != 7) || (uint32_t)SEVEN_GET(f, "instrument") == 2) return;
  const uint8_t *tuning = P(SEVEN_GET(f, "tuning"));
  int16_t t = 0;
  if (tuning && !tail_entry(tuning, s, &t)) return;
  const uint32_t fret = (uint32_t)SEVEN_GET(f, "midi") - (uint32_t)g_gbase[s] - (uint32_t)(int32_t)t;
  SEVEN_SET(f, "midi", fret);
  SEVEN_SET(f, "fret", fret);
  f->go = SEVEN_CONT(f, "done");
}

// 0x13f4ea0's played note near the expected one on its own string: fret = played - the string's base (the game's
// table has six)
static void on_pubcl_base(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  const uint32_t s = (uint32_t)SEVEN_GET(f, "string");
  if (s != 6 && s != 7) return;
  SEVEN_SET(f, "fret", (uint32_t)SEVEN_GET(f, "fret") - (uint32_t)g_gbase[s]);
  f->go = SEVEN_CONT(f, "done");
}

// Its search for another string: over the detection's 7/8 strings and the matcher's tail when a song widened it
static void on_pubcl(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  uint8_t *tracker = P(SEVEN_GET(f, "tracker")), *record = P(SEVEN_GET(f, "record"));
  uint8_t *input = tracker ? rc_mod_ptr(tracker + RC_OFF(tracker_detector)) : NULL;
  uint8_t *m = input ? rc_mod_ptr(input + RC_OFF(detector_matcher)) : NULL;
  uint8_t *det = m ? rc_mod_ptr(m + RC_OFF(matcher_detection)) : NULL;
  if (!record || !tailed(m, MATCHER) || !tailed(det, DETECTION) || bass(det)) return;
  const unsigned n = RC_LD(uint32_t, det + RC_OFF(seven_detection_count));
  if (n != 7 && n != 8) return;  // six strings: the game's search
  const int r = classify(record, m + RC_OFF(seven_matcher_tuning8), n, (int)(uint32_t)SEVEN_GET(f, "midi"),
                         (int)(uint32_t)SEVEN_GET(f, "result"));
  SEVEN_SET(f, "result", (uint32_t)r);
  f->go = SEVEN_CONT(f, "classified");
}

// The search of a note played near the target's pitch on another string (Learn & Play's 0x13f5210, whose loop its edits
// widen; the Mac's 0x10010f9d0 is unrolled over six): over the detection's 7/8 strings, the first string whose open note
// + the target's fret is the played note (2) or within two of it (1, its fret), else 7. `result` is the returned
// struct's first eight bytes as the Mac returns them: the code, the fret at bit 32, the string at bit 40.
static void on_puban(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  uint8_t *tracker = P(SEVEN_GET(f, "tracker")), *note = P(SEVEN_GET(f, "note"));
  uint8_t *input = tracker ? rc_mod_ptr(tracker + RC_OFF(tracker_detector)) : NULL;
  uint8_t *det = input ? rc_mod_ptr(input + RC_OFF(detector_detection)) : NULL;
  if (!note || !tailed(det, DETECTION) || bass(det)) return;
  const unsigned n = RC_LD(uint32_t, det + RC_OFF(seven_detection_count));
  if (n != 7 && n != 8) return;  // six strings: the game's search
  const unsigned own = RC_LD(uint8_t, note + RC_OFF(detect_target_string));
  const uint32_t fret = RC_LD(uint8_t, note + RC_OFF(detect_target_fret));
  const uint32_t midi = (uint32_t)SEVEN_GET(f, "midi");
  uint64_t r = 7;
  for (unsigned s = 0; s < n; s++) {
    if (s == own) continue;
    const uint32_t open = (uint32_t)(int32_t)RC_LD(int16_t, det + RC_OFF(seven_detection_open8) + 2 * s) + fret;
    const int32_t d = (int32_t)(midi - open);
    if (d == 0) {
      r = 2 | (uint64_t)fret << 32 | (uint64_t)s << 40;
      break;
    }
    if (d >= -2 && d <= 2) {
      const int32_t at = (int32_t)((uint32_t)d + fret);
      r = 1 | (uint64_t)(uint8_t)(at < 0 ? 0 : at) << 32 | (uint64_t)s << 40;
      break;
    }
  }
  SEVEN_SET(f, "result", r);
  f->go = SEVEN_CONT(f, "done");
}

// The note-list publication (Learn & Play's 0x1067b00, whose arrays its edits widen to eight; the Mac's 0x10022e0a0
// keeps six): notes past six are dropped, as the game's stack arrays hold six
static int g_pubnl_logged;

static void on_pubnl_count(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  const int32_t n = (int32_t)(uint32_t)SEVEN_GET(f, "count");
  if (n <= 6) return;
  SEVEN_SET(f, "count", 6);
  if (once(&g_pubnl_logged)) rsm_log("seven-string: pitch: a note list of %d published as its first six", (int)n);
}

// Its test whether a note is an open string, unrolled over six: strings 6/7 of a widened detection
static void on_pubnl_open(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  const uint8_t *det = P(SEVEN_GET(f, "detection"));
  if (!tailed(det, DETECTION) || bass(det)) return;
  const unsigned n = RC_LD(uint32_t, det + RC_OFF(seven_detection_count));
  const uint32_t note = (uint32_t)SEVEN_GET(f, "note"), offset = (uint32_t)SEVEN_GET(f, "offset");
  for (unsigned s = 6; s < n && s < 8; s++)
    if ((uint32_t)(int32_t)RC_LD(int16_t, det + RC_OFF(seven_detection_open8) + 2 * s) + offset == note) {
      f->go = SEVEN_CONT(f, "open");
      return;
    }
}

// 0x110fe40's fret-wire loop (edi = the caller's FretGrid) runs over the guitar's count, not 6
static void on_pubwu(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  uint8_t *grid = P(SEVEN_GET(f, "grid"));
  uint8_t *guitar = grid ? rc_mod_ptr(grid + RC_OFF(fretgrid_guitar)) : NULL;
  uint8_t *info = guitar ? rc_mod_ptr(guitar + RC_OFF(guitar_info)) : NULL;
  const unsigned n = info ? RC_LD(uint8_t, info + RC_OFF(instrument_strings)) : 0;
  if (n != 4 && n != 6 && n != 7 && n != 8) return;
  SEVEN_SET(f, "count", n);
  f->go = SEVEN_CONT(f, "done");
}

// Rnd_FretGrid_Build's complete exit ([ebp+8] grid, [ebp+0xc] guitar): its status (2, or 1 while strings 6/7 load)
static void on_pubgb(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  SEVEN_SET(f, "status", (uint32_t)grid_build(P(SEVEN_GET(f, "grid")), P(SEVEN_GET(f, "guitar"))));
  f->go = SEVEN_CONT(f, "done");
}

// Rnd_GetStringAttachPoint (ecx = InstrumentInfo, the string's low byte, float *out): strings 6/7 from the strings
// part, past 7 none (as the game's past 5); 0-5 the game's
static void on_pubga(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  const unsigned s = (unsigned)SEVEN_GET(f, "string");
  if (s < 6) return;
  void *info = P(SEVEN_GET(f, "info"));
  float *out = (float *)P(SEVEN_GET(f, "out"));
  const int point = s < 8 && info && out ? seven_strings_attach_ext(info, s, out) : 0;
  SEVEN_SET(f, "point", (uint32_t)point);
  f->go = SEVEN_CONT(f, "ret");
}

// 0x1367b30's edge string (eax = InstrumentInfo, cl = first): the physical edge of the built count. With 7/8 strings
// string 6 or 7 lies beyond string 0, so the first edge is the last string and the other edge is string 5.
static void on_pubedge(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  const uint8_t *info = P(SEVEN_GET(f, "info"));
  if (!info) return;
  const unsigned n = RC_LD(uint8_t, info + RC_OFF(instrument_strings));
  const int first = (uint8_t)SEVEN_GET(f, "first") != 0;
  const unsigned edge = first ? (n > 6 ? n - 1 : 0) : (n > 6 ? 5 : n - 1);
  SEVEN_SET(f, "edge", edge & 0xff);
  f->go = SEVEN_CONT(f, "done");
}

// --- shared ---------------------------------------------------------------------------------------------------
int seven_pitch_tuning8(const void *detection, int16_t out[8]) {
  const uint8_t *det = detection;
  const int ok = tailed(det, DETECTION);
  for (int i = 0; i < 8; i++) out[i] = ok ? RC_LD(int16_t, det + RC_OFF(seven_detection_tuning8) + 2 * i) : 0;
  return ok;
}

int seven_pitch_count(const void *detection) {
  const uint8_t *det = detection;
  return tailed(det, DETECTION) ? (int)RC_LD(uint32_t, det + RC_OFF(seven_detection_count)) : 0;
}

int seven_pitch_init(void) {
  static const struct {
    const char *name;
    rc_mid_fn fn;
  } points[] = {
      {"seven_pm0", on_pm0},           {"seven_pr0", on_pr0},       {"seven_s3", on_set_tuning},
      {"seven_pt1", on_set_tuning},    {"seven_pm1", on_pm1},       {"seven_s4", on_s4},
      {"seven_s1", on_s1},             {"seven_i1", on_i1},         {"seven_pubcl_base", on_pubcl_base},
      {"seven_pubcl", on_pubcl},       {"seven_pubwu", on_pubwu},   {"seven_pubgb", on_pubgb},
      {"seven_pubga", on_pubga},       {"seven_pubedge", on_pubedge},
      // the Mac's own moments (none on Learn & Play)
      {"seven_s1b", on_s1},            {"seven_s4_input", on_s4_input}, {"seven_puban", on_puban},
      {"seven_pubnl_count", on_pubnl_count}, {"seven_pubnl_open", on_pubnl_open},
  };
  for (size_t i = 0; i < sizeof points / sizeof *points; i++)
    if (seven_mid(points[i].name, points[i].fn)) return -1;
  return 0;
}
