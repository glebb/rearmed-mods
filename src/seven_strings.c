// Seven-String Guitar (seven.h): the strings part, the guitar built with seven strings. Ported from rs7string
// (github.com/SomeeName/rs7string, MIT, by SomeeName): src/features/clite (the string count as the guitar's build),
// strings (the native GuitarStrings and its copied functions), string7 (the seventh string's look), headstock (the
// native PegSet); its ARCHITECTURE sections 3, 4 and 5.
//
// The string count is a property of the guitar's build. A guitar profile carries it (+0x38): its load writes player
// 0's desired count there (seven_gkc), two profiles with different counts are not equal (seven_gke), so the game's
// "profile changed, rebuild the guitar" path (the one guitar <-> bass takes) builds a guitar with it, and the
// InstrumentInfo keeps it (+0x0b) for the guitar's life. The desired count comes from the song selection: the stored
// target tuning of the selected arrangement (seven_gkt) has the manifest key Rs7String6 on a 7-string chart, and
// player 0's guitar is rebuilt right there with the game's own rebuild. Entering Session Mode, Guitarcade, Lessons
// or the technique hub, whose code indexes six-string tables with the count, rebuilds it with six; so does the
// tuning list's tuner with a six-string tuning (and seven with a 7-string one). Never while a player tuner holds the
// guitar or a song runs on it. Player 1 is always built with six.
//
// Every GuitarStrings, InstrumentInfo and PegSet has the native layout of eight (seven_strings.h): the edits size
// them, the GuitarStrings ctor builds eight strings and eight ghosts into the native arrays (seven_gsc1 copies the
// first six where the game's own arrays were, for the code that still reads those), and the sixteen GuitarStrings
// functions rs7string runs as relocated copies (bind, load, unload, fades, transforms, update, reset, pluck,
// vibration, colours, free, the section dimming, the glow ramp, the headstock segment colours) are edited in place
// to read the native arrays and the InstrumentInfo's own counts (+0x40 active, +0x41 loaded). A guitar's counts are
// its build count (4 bass, 6, 7): its strings are loaded and shown as the game's own six are, by the same loops.
// The two queries those copies call that have no answer for strings 6 and 7 get one here: the attach point (from
// strings 0 and 1, the next slot of the row) and the atlas row of the string's texture (string 0's).
//
// The seventh string's look: its lane (rs7string's R2-R6, from the lanes of strings 0 and 1), its bends (the stock
// bend code on the native string 6 and its ghost: edits), its bend direction (string 0's: the edits read modes 6/7,
// which seven_info_counts makes string 0's), the section highlight (SNG section string mask bit 6, edits), the
// renderer's mesh of string 6 and its glow, and the pegs (an eight-slot PegSet whose cached tuner colours follow the
// colour manager every frame).
//
// The 7-string headstock model is the headstock part's (seven_headstock.c): it says how many pegs a guitar's PegSet
// loads (PSL1), which segment string 6 has (GSHC) and the in-line byte of the tuner's letters (LT1).
//
// What rs7string has and this part hasn't (the chords are another part): the chord template redirect of the section
// dimming (TP2), 8 strings ([neck] eight), the settings (string7_line, string7_v, headstock_peg: the defaults are the
// behaviour), the rebuild-timing watcher, SEH.
#include <stdarg.h>
#include <stdio.h>

#include "seven.h"

#define ATLAS_V 0.4f  // the atlas row of strings 6 and 7's texture: string 0's (rs7string's [neck] string7_v default)
#define PEG_PEAK 0.5f  // the brightest channel of the seventh peg's tuner colour (the highlight pass doubles it)
enum { SONG_KEY_MIN = -25, SONG_KEY_MAX = 25 };  // a sane semitone offset of the manifest key

// The tuner's peg colour names (+0x1c of a peg slot, rs7string's pegcolour): 0 reset and 1 success (colour kinds 6
// and 5 of string 0), 2-9 strings 0-7 (kind 4)
enum { NAME_RESET = 0, NAME_SUCCESS = 1, NAME_STRING0 = 2, NAME_LAST = 9, KIND_SUCCESS = 5, KIND_RESET = 6 };

#define PTR(f, name) ((uint8_t *)(uintptr_t)SEVEN_GET(f, name))
#define FIELD(p, name) ((uint8_t *)(p) + RC_OFF(name))
#define U8(p, name) RC_LD(uint8_t, FIELD(p, name))

// ---- the log: at most this many lines of this part a run
static int g_logs = 80;

static void slog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void slog(const char *fmt, ...) {
  if (__atomic_sub_fetch(&g_logs, 1, __ATOMIC_RELAXED) < 0) return;
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  rsm_log("seven-string: strings: %s", buf);
}

// ---- guest memory
// The guest pointer at base + off as a host pointer, NULL for none
static uint8_t *ptr_at(const void *base, int64_t off) { return base ? rc_mod_ptr((const uint8_t *)base + off) : NULL; }
// Stores a host pointer into the game's memory at `at` as the guest's pointer
static void ptr_put(uint8_t *at, const void *host) {
  uint64_t g = RC_GUEST(host);
  memcpy(at, &g, (size_t)rc_guest_ptr_size);
}
static void ld3(float v[3], const uint8_t *p) {
  for (int i = 0; i < 3; i++) v[i] = RC_LD(float, p + 4 * i);
}
static void st3(uint8_t *p, const float v[3]) {
  for (int i = 0; i < 3; i++) RC_ST(float, p + 4 * i, v[i]);
}
static float max3(const float v[3]) { return v[0] > v[1] ? (v[0] > v[2] ? v[0] : v[2]) : (v[1] > v[2] ? v[1] : v[2]); }

// ---- the native layout
uint8_t *seven_strings_string(const void *strings, int s, int ghost) {
  if (!strings || s < 0 || s >= SEVEN_CAPACITY) return NULL;
  return ptr_at(strings, (ghost ? RC_OFF(seven_gs_ghost) : RC_OFF(seven_gs_main)) + s * rc_guest_ptr_size);
}

int seven_strings_info_count(const void *info) { return info ? U8(info, instrument_strings) : 0; }
int seven_strings_guitar_count(const void *guitar) { return seven_strings_info_count(ptr_at(guitar, RC_OFF(guitar_info))); }
int seven_strings_count(const void *guitar) { return seven_strings_guitar_count(guitar); }
int seven_strings_active(const void *info) { return info ? U8(info, seven_info_active) : 0; }
int seven_strings_letter_inline(const void *info) { return seven_headstock_letter_inline(info); }

// An InstrumentInfo's own counts from its build count (+0x0b), rs7string's RefreshCounts: loaded and active are the
// build count (4 bass; 6 to 8), so the native loops load and show exactly the guitar's strings. perStringMode of
// strings 6 and 7 (+0x38 / +0x3c) are string 0's: a string beside string 0 bends its way (rs7string's BD1-BD4; the
// game's rule would give string 6 the high strings' mode).
static void counts(uint8_t *info) {
  int n = U8(info, instrument_strings);
  int c = n == 4 ? 4 : n < SEVEN_STOCK ? SEVEN_STOCK : n > SEVEN_CAPACITY ? SEVEN_CAPACITY : n;
  RC_ST(uint8_t, FIELD(info, seven_info_active), c);
  RC_ST(uint8_t, FIELD(info, seven_info_loaded), c);
  uint8_t *mode = FIELD(info, instrument_string_mode);
  uint32_t mode0 = RC_LD(uint32_t, mode);
  for (int s = SEVEN_STOCK; s < SEVEN_CAPACITY; s++) RC_ST(uint32_t, mode + 4 * s, mode0);
}

// ---- C-lite: the build count
static int g_desired = SEVEN_STOCK;  // the count player 0's next guitar profile gets (the last song selection's)
static int g_load_player;            // the player of the profile the guitar manager loads next (seven_gkm)
static uint8_t *g_manager;           // the guitar manager (seven_gkm)

int seven_strings_desired(void) { return __atomic_load_n(&g_desired, __ATOMIC_RELAXED); }

// The guitar of player 0 or 1 by the guitar manager, NULL for none
static uint8_t *player_guitar(unsigned player) {
  uint8_t *holder = player < 2 ? ptr_at(g_manager, RC_OFF(guitar_manager_holders) + player * rc_guest_ptr_size) : NULL;
  return ptr_at(holder, RC_OFF(guitar_holder_guitar));
}

// The count the guitar profile of player 0 or 1 asks for: 7 or 8 for a guitar built so, else 6 (bass, no manager)
static int profile_count(unsigned player) {
  uint8_t *holder = player < 2 ? ptr_at(g_manager, RC_OFF(guitar_manager_holders) + player * rc_guest_ptr_size) : NULL;
  uint8_t *profile = ptr_at(holder, RC_OFF(guitar_holder_profile));
  if (!profile || RC_LD(int32_t, FIELD(profile, guitar_profile_type)) != 1) return SEVEN_STOCK;
  int n = U8(profile, guitar_profile_strings);
  return n == 7 || n == 8 ? n : SEVEN_STOCK;
}

// GKC: the guitar branch of GuitarProfile_Load has written the game's 6: player 0's profile gets the desired count,
// every other player's six (only player 0 is ever built with seven). The peg word after it (the pool's pegs for the
// guitar) is 7, by the edit PSQ1 on it, for every guitar: a guitar built with seven may load a seventh peg (PSL1).
static void on_gkc(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  uint8_t *profile = PTR(f, "profile");
  if (!profile) return;
  int n = g_load_player == 0 ? seven_strings_desired() : SEVEN_STOCK;
  RC_ST(uint8_t, FIELD(profile, guitar_profile_strings), n);
  static int last = SEVEN_STOCK;
  if (n != last) slog("guitar profile %p loaded with %d strings (player %d)", (void *)profile, n, g_load_player);
  last = n;
}

// GKE: GuitarProfile_Equals: profiles whose string counts differ are not equal (the guitar is rebuilt)
static void on_gke(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  const uint8_t *a = PTR(f, "profile"), *b = PTR(f, "other");
  if (!a || !b) return;
  int na = U8(a, guitar_profile_strings), nb = U8(b, guitar_profile_strings);
  if (na == nb) return;
  slog("profiles differ in the string count (%d, %d): the guitar is rebuilt", na, nb);
  f->go = SEVEN_CONT(f, "differ");
}

// GKM: Rnd_Guitar_RebuildIfProfileChanged: the manager, and the player whose profile its next load creates (a stale
// 1 makes that load six strings, the safe side)
static void on_gkm(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  g_load_player = (int32_t)SEVEN_GET(f, "player") != 0;
  uint8_t *m = PTR(f, "manager");
  if (m != g_manager) slog("guitar manager %p", (void *)m);
  g_manager = m;
}

// GKL: Lua's GetGuitarStringCount answers 6 for a guitar, 4 for a bass: a guitar built with 7 or 8 answers that
static void on_gkl(rc_mid_frame *f, void *user) {
  if (!seven_on || (int32_t)SEVEN_GET(f, "count") != SEVEN_STOCK) return;
  int n = profile_count((uint32_t)SEVEN_GET(f, "player"));
  if (n != SEVEN_STOCK) SEVEN_SET(f, "count", n);
}

// GKH1 / GKH2: Rnd_Headstock_LoadStringMeshes loops over the InstrumentInfo's count, but the headstock has six
// segment slots (+0x18 loaded[6], +0x20 params[6]): at most six (string_size_6..1 as on a six-string guitar; with
// seven the game's loop read params[6] past the object)
static void on_gkh(rc_mid_frame *f, void *user) {
  if (seven_on && (uint8_t)SEVEN_GET(f, "count") > SEVEN_STOCK) SEVEN_SET(f, "count", SEVEN_STOCK);
}
static void on_gkh2(rc_mid_frame *f, void *user) {
  if (seven_on && (uint8_t)SEVEN_GET(f, "count") > SEVEN_STOCK) SEVEN_SET(f, "count", SEVEN_STOCK);
}

// --- GKT: the stored target tuning
// The manifest key on the Tuning JSON object: the object's find (by name) and the value's number
static const char *g_keys;  // "Rs7String6" in the game's memory

static int has_key(const uint8_t *tuning, const char *key) {
  if (!tuning || !key) return 0;
  uint8_t *node = RC_HOST(OV_VCALL((uintptr_t)tuning, (int)RC_SLOT(json_value_find), (uintptr_t)key));
  uint8_t *num = node ? RC_HOST(OV_VCALL((uintptr_t)node, (int)RC_SLOT(json_value_number))) : NULL;
  if (!num) return 0;
  double d = RC_LD(double, FIELD(num, json_number_value));
  return d > SONG_KEY_MIN && d < SONG_KEY_MAX;
}

// What a store of the target tuning is, by its caller (the return address of Play_Tuning_SetPlayerTargetTuning)
enum role { OTHER, SONG_SELECT, TUNER_MENU, MODE_ENTRY };

static enum role role_of(uint32_t caller, const char **mode) {
  *mode = NULL;
  if (caller == RC_ADDR(tuning_store_song_select)) return SONG_SELECT;
  if (caller == RC_ADDR(tuning_store_tuner_menu) || caller == RC_ADDR(tuning_store_tuner_menu2)) return TUNER_MENU;
  if (caller == RC_ADDR(tuning_store_guitarcade) || caller == RC_ADDR(tuning_store_guitarcade2)) *mode = "Guitarcade";
  else if (caller == RC_ADDR(tuning_store_lessons)) *mode = "Lessons";
  else if (caller == RC_ADDR(tuning_store_session_tuner) || caller == RC_ADDR(tuning_store_session_tuner2) ||
           caller == RC_ADDR(tuning_store_session_start) || caller == RC_ADDR(tuning_store_session_start2))
    *mode = "Session Mode";
  else if (caller == RC_ADDR(tuning_store_technique_hub)) *mode = "the technique hub";
  return *mode ? MODE_ENTRY : OTHER;
}

// The player tuners of the tuner manager that hold a guitar (a rebuild would free it under the tuner)
static int tuners_holding(void) {
  uint8_t *mgr = rc_mod_ptr((const void *)(uintptr_t)RC_GA(tuner_manager));
  int n = 0;
  for (int i = 0; mgr && i < 4; i++) {
    uint8_t *t = ptr_at(mgr, RC_OFF(tuner_manager_tuners) + i * rc_guest_ptr_size);
    n += ptr_at(t, RC_OFF(player_tuner_guitar)) != NULL;
  }
  return n;
}

// A song runs on player 0's guitar: string 0's section-dimming flag, set at the song's start, cleared at its end
static int song_running(void) {
  uint8_t *s0 = seven_strings_string(ptr_at(player_guitar(0), RC_OFF(guitar_strings)), 0, 0);
  return s0 && U8(s0, guitar_string_dim);
}

// rs7string's RebuildTarget: the count to rebuild player 0's guitar with, 0 for none. Song selection: the
// arrangement's count; the tuning list's tuner: the picked tuning's, unless a song runs; a mode entry: back to six.
// Never another player's, never while a player tuner holds a guitar.
static int rebuild_target(enum role r, uint32_t player, int want, int was, int tuners, int song) {
  if (player != 0 || tuners) return 0;
  if (r == SONG_SELECT) return want != was ? want : 0;
  if (r == TUNER_MENU) return !song && want != was ? want : 0;
  if (r == MODE_ENTRY && want == SEVEN_STOCK && was != SEVEN_STOCK) return SEVEN_STOCK;
  return 0;
}

static void on_gkt(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  uint32_t player = (uint32_t)SEVEN_GET(f, "player"), caller = (uint32_t)SEVEN_GET(f, "caller");
  int want = has_key(PTR(f, "tuning"), g_keys) ? 7 : SEVEN_STOCK, was = seven_strings_desired();
  const char *mode;
  enum role r = role_of(caller, &mode);
  if (r == OTHER) return;
  int tuners = tuners_holding(), song = r == TUNER_MENU && song_running();
  int target = rebuild_target(r, player, want, was, tuners, song);
  slog("target tuning stored (player %u, %s%s): %d strings, the guitar's %d%s", player,
       r == SONG_SELECT ? "song selection" : r == TUNER_MENU ? "tuning list" : "entering ", mode ? mode : "", want,
       was, tuners && player == 0 ? "; a tuner holds the guitar: no rebuild" : song ? "; a song runs: no rebuild" : "");
  if (!target) return;
  if (!g_manager) {
    slog("no guitar manager yet: %d strings wait", target);
    return;
  }
  __atomic_store_n(&g_desired, target, __ATOMIC_RELAXED);
  // The game's own rebuild of player 0's guitar from its profile, and the scene attach of both players' guitars
  // (rs7string calls Rnd_Guitar_EnsureForPlayer and 0x4B24F0 itself; this function of the game is that pair)
  int before = seven_strings_guitar_count(player_guitar(0));
  OV_GUESTV(guitar_rebuild_player0, NULL, 0);
  int after = seven_strings_guitar_count(player_guitar(0));
  // A new build: no wait deadline or failed neck of the old guitar carries over (the pitch part's PUBGB records)
  if (after != before) seven_pitch_grid_forget_all();
  slog("player 0's guitar %s: %d strings", after != before ? "rebuilt" : "kept", after);
}

// ---- the native GuitarStrings and InstrumentInfo
// GIA1: the guitar ctor's InstrumentInfo, just constructed (0x48 bytes): its tail zeroed, its counts
static void on_gia1(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  uint8_t *info = PTR(f, "info");
  if (!info) return;
  memset(info + RC_OFF(instrument_size), 0, (size_t)(RC_OFF(seven_info_size) - RC_OFF(instrument_size)));
  counts(info);
}

// The InstrumentInfo's modes after the profile's count reached +0x0b (the guitar's build): its counts
static void on_info_counts(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  uint8_t *info = PTR(f, "info");
  if (info) counts(info);
}

// GSC1: the guitar ctor's GuitarStrings, its ctor built eight strings and eight ghosts into the native arrays: the
// first six of each where the game's six were (code that still reads those reads the same objects), glow[8] and the
// tail zeroed, its guitar and InstrumentInfo
static void on_gsc1(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  uint8_t *gs = PTR(f, "strings"), *guitar = PTR(f, "guitar");
  if (!gs) return;
  size_t ps = (size_t)rc_guest_ptr_size;
  for (int s = 0; s < SEVEN_STOCK; s++) {
    memcpy(FIELD(gs, guitar_strings_string) + s * ps, FIELD(gs, seven_gs_main) + s * ps, ps);
    memcpy(FIELD(gs, guitar_strings_ghost) + s * ps, FIELD(gs, seven_gs_ghost) + s * ps, ps);
  }
  memset(FIELD(gs, seven_gs_glow), 0, SEVEN_CAPACITY * sizeof(float));
  memset(FIELD(gs, seven_gs_tail), 0, 4);
  ptr_put(FIELD(gs, seven_gs_guitar), guitar);
  if (guitar) memcpy(FIELD(gs, seven_gs_info), FIELD(guitar, guitar_info), ps);
  static int told;
  if (!told++) slog("guitar %p: strings %p with eight strings and ghosts", (void *)guitar, (void *)gs);
}

// The attach point of string 6 or 7 (Rnd_GetStringAttachPoint rejects strings past 5; the pitch part's PUBGA answers
// with this at its entry, for the load's query and every other): the row of strings 0 and 1 continued,
// a0 + (s - 5) * (a0 - a1), where the 7-string headstock's string_size_7 ends (rs7string's AttachExt). `out` is the
// caller's vec3 in the game's memory: both of the game's answers are read from it.
int seven_strings_attach_ext(void *info, unsigned s, float *out) {
  uint8_t *point = (uint8_t *)out;
  s &= 0xff;
  if (!info || !point || s < SEVEN_STOCK || s >= SEVEN_CAPACITY) return 0;
  float a0[3], a1[3], v[3];
  if (!(OV_GUEST(guitar_string_attach_point, (uintptr_t)info, 0, (uintptr_t)point) & 0xff)) return 0;
  ld3(a0, point);
  if (!(OV_GUEST(guitar_string_attach_point, (uintptr_t)info, 1, (uintptr_t)point) & 0xff)) return 0;
  ld3(a1, point);
  float k = (float)((int)s - 5);
  for (int i = 0; i < 3; i++) v[i] = a0[i] + k * (a0[i] - a1[i]);
  st3(point, v);
  static int told;
  if (told++ < 2) slog("string %u attach (%.3f, %.3f, %.3f) from strings 0 and 1", s, v[0], v[1], v[2]);
  return 1;
}

// The main string object s (0-7) of a guitar, NULL for none
void *seven_strings_child(void *guitar, int s) {
  return seven_strings_string(ptr_at(guitar, RC_OFF(guitar_strings)), s, 0);
}

// 0 when the strings past six the guitar loads (its build count: string 6 of a guitar built with 7) and their ghosts
// exist with their models loaded; else the first failing check, rs7string's ExtraWhy: 1 no GuitarStrings, 3 no
// InstrumentInfo, 4 the GuitarStrings' InstrumentInfo isn't the guitar's, 8 + (s - 6) * 6 + ghost * 3 + {0 no object,
// 1 no entity, 2 not loaded}. A guitar built with six (or a bass) has none to wait for: 0.
int seven_strings_extra_why(void *guitar) {
  uint8_t *gs = ptr_at(guitar, RC_OFF(guitar_strings)), *info = ptr_at(guitar, RC_OFF(guitar_info));
  if (!gs) return 1;
  if (!info) return 3;
  if (ptr_at(gs, RC_OFF(seven_gs_info)) != info) return 4;
  int loaded = U8(info, seven_info_loaded);
  for (int s = SEVEN_STOCK; s < loaded && s < SEVEN_CAPACITY; s++)
    for (int ghost = 0; ghost < 2; ghost++) {
      const uint8_t *obj = seven_strings_string(gs, s, ghost);
      int at = 8 + (s - SEVEN_STOCK) * 6 + ghost * 3;
      if (!obj) return at;
      if (!ptr_at(obj, RC_OFF(guitar_string_entity))) return at + 1;
      if (!U8(obj, guitar_string_loaded)) return at + 2;
    }
  return 0;
}

// GSLD after Rnd_FindStringBaseShape: string 6 or 7's shader parameters take the atlas row of its texture (the game's
// table has six rows), rs7string's ShapeDone
static void shape_done(const uint8_t *string, unsigned s) {
  if (s < SEVEN_STOCK || s >= SEVEN_CAPACITY || !string || !U8(string, guitar_string_loaded)) return;
  uint8_t *params = ptr_at(string, RC_OFF(guitar_string_shader));
  if (params) RC_ST(float, FIELD(params, string_shader_v), ATLAS_V);
}
// shape1 and shape2 have the same values: one function, one index cache
static void on_shape12(rc_mid_frame *f, void *user) {
  if (seven_on) shape_done(PTR(f, "string"), (uint8_t)SEVEN_GET(f, "index"));
}
static void on_shape3(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  unsigned s = (uint8_t)SEVEN_GET(f, "index");
  shape_done(seven_strings_string(PTR(f, "strings"), (int)s, 0), s);
}

// GSHC: the headstock's segment colours over the active strings. The headstock has six segments (the game's check
// would read past its six): string 6 has one while the guitar shows the 7-string headstock model, its string_size_7
// (the headstock part's), which then takes string 6's colour as the game's loop gives the others theirs; string 7
// never has one
static void *segment7(rc_mid_frame *f, int string) {
  return string == SEVEN_STOCK ? seven_headstock_seg7_colour(PTR(f, "guitar")) : NULL;
}
static void on_hs_loaded(rc_mid_frame *f, void *user) {
  int s = (int32_t)SEVEN_GET(f, "string");
  if (seven_on && s >= SEVEN_STOCK) f->go = segment7(f, s) ? SEVEN_CONT(f, "loaded") : SEVEN_CONT(f, "next");
}
static void on_hs_segment(rc_mid_frame *f, void *user) {
  int s = (int32_t)SEVEN_GET(f, "string");
  if (!seven_on || s < SEVEN_STOCK) return;
  SEVEN_SET(f, "segment", (uintptr_t)segment7(f, s));
  f->go = SEVEN_CONT(f, "done");
}

// GRM1: Rnd_StringRenderer_Setup collects a mesh per main string: the loaded strings of a guitar built with 7 or 8,
// else the game's six. In place of the game's store of 6 (the point is on it, and goes on after it): the loop it
// counts down comes back to the instruction after the store, so the count is set once.
static void on_grm1(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  uint8_t *gs = ptr_at(PTR(f, "renderer"), RC_OFF(string_renderer_strings));
  uint8_t *info = ptr_at(gs, RC_OFF(seven_gs_info));
  int n = info ? U8(info, seven_info_loaded) : 0;
  SEVEN_SET(f, "meshes", n > SEVEN_STOCK && n <= SEVEN_CAPACITY ? n : SEVEN_STOCK);
  f->go = SEVEN_CONT(f, "skip");
}

// GRM3: the atlas row of meshes 6 and 7 (the game's table has six; its read past it is replaced)
static void on_grm3(rc_mid_frame *f, void *user) {
  if (seven_on && (int32_t)SEVEN_GET(f, "row") >= SEVEN_STOCK * (int)sizeof(float)) SEVEN_SETF(f, "v", ATLAS_V);
}

// ---- the native PegSet
// The PegSet of a guitar, NULL for none
static uint8_t *pegs_of(const uint8_t *guitar) { return ptr_at(ptr_at(guitar, RC_OFF(guitar_headstock)), RC_OFF(headstock_pegs)); }
static uint8_t *peg_slot(uint8_t *pegs, int s) { return FIELD(pegs, pegs_slots) + s * RC_OFF(peg_slot_size); }

// PSC1: the end of Rnd_PegSet_Ctor: slots 6 and 7 take slot 0's fresh defaults, pegColor[6] moves from where the
// game's ctor wrote it (slots 6/7 are there now) to pegColor[8] (+0x14c; [6] and [7] = [0]), the tail
static void on_psc1(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  uint8_t *pegs = PTR(f, "pegs");
  int64_t size = RC_OFF(peg_colour_size);
  uint8_t colours[SEVEN_STOCK * 16];
  if (!pegs || size > 16) return;
  memcpy(colours, FIELD(pegs, pegs_colours), (size_t)(SEVEN_STOCK * size));
  for (int s = SEVEN_STOCK; s < SEVEN_CAPACITY; s++) memcpy(peg_slot(pegs, s), peg_slot(pegs, 0), (size_t)RC_OFF(peg_slot_size));
  for (int i = 0; i < SEVEN_CAPACITY; i++)
    memcpy(FIELD(pegs, seven_pegs_colours) + i * size, colours + (i < SEVEN_STOCK ? i : 0) * size, (size_t)size);
  memset(FIELD(pegs, seven_pegs_tail), 0, 4);
  RC_ST(uint8_t, FIELD(pegs, seven_pegs_tail), SEVEN_CAPACITY);
}

// PSL1: Rnd_PegSet_Load takes, binds and prepares the InstrumentInfo's count of pegs: as many as the headstock part
// says. The stock frames have Locator_Peg_1..6: seven only where the frame carries the 7-string model, whose
// Locator_Peg_7 the game's binder finds for peg 7; else six (a seventh peg would be shown without a place)
static void on_psl1(rc_mid_frame *f, void *user) {
  int count = (int32_t)SEVEN_GET(f, "count");
  if (!seven_on || count <= SEVEN_STOCK) return;
  int n = seven_headstock_pegs(PTR(f, "info"), count);
  SEVEN_SET(f, "count", n);
  static int told;
  if (!told++) slog("PegSet %p: %d pegs on a guitar built with %d%s", (void *)PTR(f, "pegs"), n, count,
                    n > SEVEN_STOCK ? "" : " (no seventh peg without the 7-string headstock model)");
}

// The tuner colour of the seventh peg: string 6's colour as bright as string 0's tuner colour is against its lit
// colour, its brightest channel at most PEG_PEAK (rs7string's NamedColour: the highlight pass doubles it, and a
// saturated channel changed the hue)
static int named_colour(const uint8_t *mgr, int player, float out[3]) {
  int mode = mgr && player >= 0 && player < 2 ? seven_colours_mode(mgr, player) : -1;
  if (mode < 0 || mode >= SEVEN_COLOUR_MODES) return 0;
  const float *lit = seven_colours_get(mgr, mode, SEVEN_KIND_LIT, 0), *peg = seven_colours_get(mgr, mode, SEVEN_KIND_PEG, 0);
  const float *rgb = seven_colours_rgb(mode);
  if (!lit || !peg || !rgb) return 0;
  float c1[3], c4[3];
  ld3(c1, (const uint8_t *)lit);
  ld3(c4, (const uint8_t *)peg);
  float k = max3(c1) > 0 ? max3(c4) / max3(c1) : 1.0f;
  for (int i = 0; i < 3; i++) out[i] = rgb[i] * k;
  float m = max3(out);
  if (m > PEG_PEAK)
    for (int i = 0; i < 3; i++) out[i] *= PEG_PEAK / m;
  return 1;
}

// PSN1: UI_PlayerTuner_SetPegColorAnim reads the colour name of a peg from the game's table of six: a peg index past
// it takes name 0 (a string colour), and PSN2 gives the peg the seventh string's. The tuner's player is the one whose
// colour mode the PegSet's cached colours follow (N5).
static void on_psn1(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  uint8_t *tuner = PTR(f, "tuner");
  uint8_t *pegs = pegs_of(ptr_at(tuner, RC_OFF(player_tuner_guitar)));
  uint32_t player = tuner ? RC_LD(uint32_t, FIELD(tuner, player_tuner_player)) : 2;
  if (pegs && player < 2) RC_ST(uint8_t, FIELD(pegs, seven_pegs_colour_player), player);
  if ((int32_t)SEVEN_GET(f, "index") < SEVEN_STOCK) return;
  SEVEN_SET(f, "name", (uintptr_t)rc_mod_ptr((const void *)(uintptr_t)RC_GA(tuner_peg_names)));
  f->go = SEVEN_CONT(f, "done");
}

// PSN2: the named colour goes to the peg's slot; a peg that isn't active has none (the game passes NULL, which its
// colour call would write through): no call, its argument popped as its `ret 4` would
static void on_psn2(rc_mid_frame *f, void *user) {
  if (!seven_on || PTR(f, "slot")) return;
  SEVEN_SET(f, "sp", (uint32_t)SEVEN_GET(f, "sp") + (uint32_t)rc_guest_ptr_size);
  f->go = SEVEN_CONT(f, "skip");
}

// After it: an active peg past the sixth gets the seventh string's tuner colour, name 2 + index
static void on_psn2_after(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  int index = (int32_t)SEVEN_GET(f, "index");
  uint8_t *tuner = PTR(f, "tuner");
  uint8_t *pegs = pegs_of(ptr_at(tuner, RC_OFF(player_tuner_guitar)));
  if (index < SEVEN_STOCK || index >= SEVEN_CAPACITY || !pegs || index >= RC_LD(uint16_t, FIELD(pegs, pegs_count))) return;
  uint8_t *slot = peg_slot(pegs, index);
  float rgb[3];
  if (!RC_LD(uint8_t, slot) || !named_colour(seven_colours_manager(), RC_LD(int32_t, FIELD(tuner, player_tuner_player)), rgb)) return;
  st3(FIELD(slot, peg_slot_rgb), rgb);
  RC_ST(uint32_t, FIELD(slot, peg_slot_named), NAME_STRING0 + index);
}

// ---- the 7th string's look
// N5, every frame of a guitar: the tuner colours its PegSet slots cached (0x111d360 copies a named colour into a
// slot) follow the colour manager, so a palette change (RSMods.ini, colourblind mode) reaches a selected peg and its
// glow; names 0/1 the reset and success colours, 2-9 the strings' tuner colours (rs7string's pegcolour Refresh)
static void on_n5(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  uint8_t *pegs = pegs_of(PTR(f, "guitar"));
  const uint8_t *mgr = pegs && U8(pegs, pegs_loaded) ? seven_colours_manager() : NULL;
  if (!mgr) return;
  int player = U8(pegs, seven_pegs_colour_player), mode = seven_colours_mode(mgr, player < 2 ? player : 0);
  int n = RC_LD(uint16_t, FIELD(pegs, pegs_count));
  if (mode < 0 || mode >= SEVEN_COLOUR_MODES) return;
  for (int s = 0; s < n && s < SEVEN_CAPACITY; s++) {
    uint8_t *slot = peg_slot(pegs, s);
    uint32_t name = RC_LD(uint32_t, FIELD(slot, peg_slot_named));
    if (!RC_LD(uint8_t, slot) || name > NAME_LAST) continue;
    int kind = name == NAME_RESET ? KIND_RESET : name == NAME_SUCCESS ? KIND_SUCCESS : SEVEN_KIND_PEG;
    int string = name < NAME_STRING0 ? 0 : (int)name - NAME_STRING0;
    float want[3], now[3];
    const float *src = seven_colours_get(mgr, mode, kind, string);
    if (string == SEVEN_STOCK ? !named_colour(mgr, player, want) : !src) continue;
    if (string != SEVEN_STOCK) ld3(want, (const uint8_t *)src);
    ld3(now, FIELD(slot, peg_slot_rgb));
    if (memcmp(want, now, sizeof want)) st3(FIELD(slot, peg_slot_rgb), want);
  }
}

// The lane of string 6, which has no cell of the game's: one string spacing past string 0, 2 * cell 0 - cell 1
// (rs7string's R2-R6; the same float as its x87 code for finite values)
static float lane(const uint8_t *cell0, int64_t off) {
  float a = RC_LD(float, cell0 + off), b = RC_LD(float, cell0 + RC_OFF(seven_fretgrid_cell) + off);
  return (float)(2.0 * a - b);
}
// The point's string is 6 (string_at = string * the cell size). A macro: each point's own value index
#define IS_STRING6(f) (seven_on && (int64_t)(int32_t)SEVEN_GET(f, "string_at") == SEVEN_STOCK * RC_OFF(seven_fretgrid_cell))

// R3a-c (Rnd_NoteFx_Init) and R4 (Rnd_RenderNote_GetLanePos): `lea eax,[ecx+eax+0x3c]`, then the cell's vec3 through
// eax. For string 6 its lane goes to a scratch in the game's memory (one per point: the game reads it right after),
// and eax is moved so the lea lands on it. The four have the same values.
static uint8_t *g_scratch;  // 4 x vec3

static void lane_vec(rc_mid_frame *f, int k) {
  if (!IS_STRING6(f)) return;
  if (!g_scratch && !(g_scratch = rc_guest_alloc(4 * 3 * sizeof(float)))) return;
  uint32_t cells = (uint32_t)SEVEN_GET(f, "cells");
  const uint8_t *cell0 = RC_HOST(cells + (uint32_t)RC_OFF(note_lane_pos));
  uint8_t *out = g_scratch + k * 3 * sizeof(float);
  float v[3];
  for (int i = 0; i < 3; i++) v[i] = lane(cell0, 4 * i);
  st3(out, v);
  SEVEN_SET(f, "cells", (uint32_t)RC_GUEST(out) - (uint32_t)SEVEN_GET(f, "string_at") - (uint32_t)RC_OFF(note_lane_pos));
}
static void on_r3a(rc_mid_frame *f, void *user) { lane_vec(f, 0); }
static void on_r3b(rc_mid_frame *f, void *user) { lane_vec(f, 1); }
static void on_r3c(rc_mid_frame *f, void *user) { lane_vec(f, 2); }
static void on_r4(rc_mid_frame *f, void *user) { lane_vec(f, 3); }

// R2: a note head's lane z (its row's cell + 0x44), R6: a gem's lane y (+0x40), after the game's load of string 6's
static void on_r2(rc_mid_frame *f, void *user) {
  if (IS_STRING6(f)) SEVEN_SETF(f, "z", lane(PTR(f, "row"), RC_OFF(note_head_lane_z)));
}
static void on_r6(rc_mid_frame *f, void *user) {
  if (IS_STRING6(f)) SEVEN_SETF(f, "y", lane(PTR(f, "row"), RC_OFF(gem_lane_y)));
}

int seven_strings_init(void) {
  static const char key[] = "Rs7String6";
  char *k = rc_guest_alloc(sizeof key);
  if (!k) {
    rsm_log("seven-string: strings: no game memory for the manifest key");
    return -1;
  }
  memcpy(k, key, sizeof key);
  g_keys = k;
  static const struct {
    const char *name;
    rc_mid_fn fn;
  } points[] = {
      {"seven_gkc", on_gkc},
      {"seven_gke", on_gke},
      {"seven_gkm", on_gkm},
      {"seven_gkl", on_gkl},
      {"seven_gkt", on_gkt},
      {"seven_gkh1", on_gkh},
      {"seven_gkh2", on_gkh2},
      {"seven_gia1", on_gia1},
      {"seven_gsc1", on_gsc1},
      {"seven_info_counts", on_info_counts},
      {"seven_gsld_shape1", on_shape12},
      {"seven_gsld_shape2", on_shape12},
      {"seven_gsld_shape3", on_shape3},
      {"seven_gshc_loaded", on_hs_loaded},
      {"seven_gshc_segment", on_hs_segment},
      {"seven_grm1", on_grm1},
      {"seven_grm3", on_grm3},
      {"seven_psc1", on_psc1},
      {"seven_psl1", on_psl1},
      {"seven_psn1", on_psn1},
      {"seven_psn2", on_psn2},
      {"seven_psn2_after", on_psn2_after},
      {"seven_n5", on_n5},
      {"seven_r3a", on_r3a},
      {"seven_r3b", on_r3b},
      {"seven_r3c", on_r3c},
      {"seven_r4", on_r4},
      {"seven_r2", on_r2},
      {"seven_r6", on_r6},
  };
  for (size_t i = 0; i < sizeof points / sizeof *points; i++)
    if (seven_mid(points[i].name, points[i].fn)) return -1;
  return 0;
}
