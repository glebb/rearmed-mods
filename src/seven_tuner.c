// Seven-String Guitar (seven.h): the tuner part, the tuner over eight slots with the low B first. A port of rs7string's
// native 8-slot tuner session (github.com/SomeeName/rs7string, MIT, by SomeeName: src/features/tuner, its
// tools/gen_tuner8s.py and gen_tuner8.py; docs/ARCHITECTURE.md sections 5 and 8).
//
// The session (UI_PlayerTuner) grows (380h bytes on Learn & Play, 458h on the Mac) and its per-string arrays have a
// tail of eight slots (seven_tuner.h). The edits of seven_string are rs7string's copied methods made in place: the
// tuner's methods read the tail, run their loops to the session's count and step through the session's order; the
// GuitarStrings readers among them read the guitar's native count and strings (the strings part). rs7string ran its
// copies for a 7/8-string session only;
// here every session runs them, a 6-string guitar's and a bass's too, so every session gets its tail when it is made
// and keeps it in step with the game's own arrays: count 4 or 6 as the guitar is built and the order 0..n-1, which
// is what the game's loops and steps do; 7 or 8 with the low strings first for a 7/8-string target. The count is the
// session's, never InstrumentInfo +0Bh.
//
// What the mid points do (rs7string's site ids in their names):
//   the tail          TU8INIT (a new session), TS8RS (the standard tuning), seven_ts8bind (the guitar is bound),
//                     TS (ApplyTuning: the targets, and a 7/8-string target from TK's manifest key), TK
//   the order         TS8STA/STB (arrow keys), TS8MD0-3 (the modes' steps), TS8F1-3 and TS8M2E (a mode starts at
//                     the first string of the order)
//   single reads      TS8FIN (a letter), TS8NTI (the labels of strings 6 / 7), TS8SN1/SN2 (the tuned sound's note)
//   low range         TS8SEL (the detector's lowest open note), W (the wrong-instrument threshold), for 7/8 strings
//   others            LT1 (tuner letters on a 3+3 headstock)
// rs7string's TS8PA / TS8PZ (Play_Tuning_ApplyToDetection running) are the pitch part's: it knows that call by its
// return address.
// Game thread only.
#include <stdarg.h>
#include <stdio.h>

#include "seven.h"

// From the strings part (seven_strings.h): seven_strings_count, the count a guitar is built with (4, 6, 7, 8; 0 not
// known), and seven_strings_letter_inline, LT1's in-line byte.

#define PTR(f, name) ((uint8_t *)(uintptr_t)SEVEN_GET(f, name))
#define FIELD(p, name) ((uint8_t *)(p) + RC_OFF(name))

// The open note of strings 6 and 7 at a standard tuning (B1, F#1), as rs7string's guitar base table
static const int g_base_extra[2] = {35, 30};

// ---- the log: at most this many lines of the tuner a run
static int g_logs = 120;

static void tlog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void tlog(const char *fmt, ...) {
  if (g_logs <= 0) return;
  g_logs--;
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  rsm_log("seven-string: tuner: %s", buf);
}

// ---- the session's tail
static int count_of(const uint8_t *s) { return RC_LD(uint8_t, FIELD(s, seven_tuner_count)); }
static int first_of(const uint8_t *s) { return RC_LD(uint8_t, FIELD(s, seven_tuner_order)); }
static int open_of(const uint8_t *s, int k) { return RC_LD(int32_t, FIELD(s, seven_tuner_open) + 4 * k); }

// The count, the order (0..n-1; with more than six strings the strings above 6 lowest first, then 0..5) and the
// camera's reference string: the edge opposite string 0, which is string 5 when strings 6 / 7 lie beyond string 0
static void put_count(uint8_t *s, int n) {
  if (n < 1 || n > 8) n = 6;
  uint8_t *order = FIELD(s, seven_tuner_order);
  for (int k = 0; k < 8; k++) RC_ST(uint8_t, order + k, k);
  if (n > 6) {
    int k = 0;
    for (int t = n - 1; t >= 6; t--) RC_ST(uint8_t, order + k++, t);
    for (int t = 0; t < 6; t++) RC_ST(uint8_t, order + k++, t);
  }
  RC_ST(uint8_t, FIELD(s, seven_tuner_count), n);
  RC_ST(uint8_t, FIELD(s, seven_tuner_edge), (n > 6 ? 6 : n) - 1);
}

// The count from the session's target (7 / 8) or its guitar (4 / 6; six on a guitar built with more: a 6-string
// target tunes six strings, as rs7string's)
static void refresh_count(uint8_t *s) {
  int target = RC_LD(uint8_t, FIELD(s, seven_tuner_target)), n = count_of(s);
  if (target >= 7) {
    n = target;
  } else {
    void *guitar = rc_mod_ptr(FIELD(s, player_tuner_guitar));
    int built = guitar ? seven_strings_count(guitar) : 0;
    if (built > 0) n = built;
    if (n > 6) n = 6;
  }
  if (n != count_of(s)) tlog("session %p: %d strings, first string %d", (void *)s, n, n > 6 ? n - 1 : 0);
  put_count(s, n);
}

static int lowest_open(const uint8_t *s) {
  int low = 127, n = count_of(s);
  for (int k = 0; k < n && k < 8; k++)
    if (open_of(s, k) < low) low = open_of(s, k);
  return low;
}

// One step from the current string along the order: the next (dir 1) or previous (-1) string, or -1 when there is none
static int step(const uint8_t *s, int dir) {
  int n = count_of(s), cur = RC_LD(int32_t, FIELD(s, player_tuner_string));
  const uint8_t *order = FIELD(s, seven_tuner_order);
  for (int r = 0; r < n && r < 8; r++) {
    if (RC_LD(uint8_t, order + r) != cur) continue;
    int to = r + dir;
    return to >= 0 && to < n ? RC_LD(uint8_t, order + to) : -1;
  }
  return -1;
}

// ---- the manifest keys per offset array (TK), which TS reads
typedef struct {
  const void *offsets;
  int found, t6, found7, t7;
} tuner_key;
static tuner_key g_keys[4];
static char *g_key6, *g_key7;   // "Rs7String6", "Rs7String7" in the game's memory
static const char *g_notes[12]; // the note names the precision tuner shows for strings 6 / 7, in the game's memory

static const tuner_key *key_of(const void *offsets) {
  for (int k = 0; k < 4; k++)
    if (g_keys[k].offsets == offsets) return &g_keys[k];
  return NULL;
}

int seven_tuner_seven_for(const void *offsets, int *t6) {
  const tuner_key *k = offsets ? key_of(offsets) : NULL;
  if (!k || !k->found) return 0;
  *t6 = k->t6;
  return 1;
}

// A number member of a JSON object, through the game's JSON vtable (as Play_Tuning_ReadStringOffsets reads the six)
// (a returned pointer is the guest's width: 32 bits on Learn & Play, whose upper half is not the return value)
static int json_number(void *obj, const char *key, double *out) {
  const uint64_t width = rc_guest_ptr_size == 8 ? ~(uint64_t)0 : 0xffffffffu;
  if (!obj) return 0;
  uint64_t node = OV_VCALL((uint64_t)(uintptr_t)obj, (int)RC_SLOT(json_value_find), (uint64_t)(uintptr_t)key);
  if (!(node &= width)) return 0;
  uint64_t num = OV_VCALL((uint64_t)(uintptr_t)RC_HOST(node), (int)RC_SLOT(json_value_number));
  if (!(num &= width)) return 0;
  *out = RC_LD(double, FIELD(RC_HOST(num), json_number_value));
  return 1;
}

// A tuning offset: a number in (-25, 25), truncated as the game's readers do
static int json_offset(void *obj, const char *key, int *out) {
  double d = 0;
  if (!json_number(obj, key, &d) || !(d > -25.0 && d < 25.0)) return 0;
  *out = (int)d;
  return 1;
}

// TK: Play_Tuning_ReadStringOffsets is reading the target tuning (holder: the address of its JSON object) into an
// offset array; the keys are kept for that array. The game's calls below use eax / ecx / edx: they get their values
// back.
static void on_tk(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  uint8_t *offsets = PTR(f, "offsets"), *holder = PTR(f, "holder");
  if (!offsets) return;
  uint64_t eax = SEVEN_GET(f, "eax"), edx = SEVEN_GET(f, "edx");
  void *json = holder ? rc_mod_ptr(holder) : NULL;
  tuner_key k = {offsets, 0, 0, 0, 0};
  k.found = json_offset(json, g_key6, &k.t6);
  if (k.found) k.found7 = json_offset(json, g_key7, &k.t7);
  SEVEN_SET(f, "offsets", (uintptr_t)offsets);
  SEVEN_SET(f, "eax", eax);
  SEVEN_SET(f, "edx", edx);
  tuner_key *slot = NULL;
  for (int i = 0; i < 4 && !slot; i++)
    if (g_keys[i].offsets == offsets) slot = &g_keys[i];
  for (int i = 0; i < 4 && !slot; i++)
    if (!g_keys[i].offsets) slot = &g_keys[i];
  if (!slot) slot = &g_keys[0];
  if (slot->offsets != k.offsets || slot->found != k.found || slot->t6 != k.t6 || slot->found7 != k.found7 ||
      slot->t7 != k.t7)
    tlog("target tuning read into %p: %s", (void *)offsets,
         k.found7  ? "Rs7String6 and Rs7String7 (8 strings)"
         : k.found ? "Rs7String6 (7 strings)"
                   : "no Rs7String6 (6 strings)");
  *slot = k;
}

// ---- the session's life
// TU8INIT: the constructor's end. The tail as the constructor leaves the game's arrays (zero, no pick state), six
// strings in their order until a guitar or a target says otherwise.
static void on_tu8init(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  uint8_t *s = PTR(f, "session");
  if (!s) return;
  memset(FIELD(s, seven_tuner_tail), 0, (size_t)(RC_OFF(seven_tuner_size) - RC_OFF(seven_tuner_tail)));
  for (int k = 0; k < 8; k++) RC_ST(int32_t, FIELD(s, seven_tuner_last_pick) + 4 * k, -2);
  put_count(s, 6);
}

// TU8DT (the Mac's): the destructor releases the game's six pick and letter slots, unrolled, so there is no count to
// widen; the tail's (where every pick and letter of the session lives, the game's slots stay empty) are released here
// as the game releases each (its vtable's Release) and cleared. Learn & Play widens its loops instead (no call).
static void on_tu8dt(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  uint8_t *s = PTR(f, "session");
  if (!s) return;
  const size_t w = (size_t)rc_guest_ptr_size;
  uint8_t *slots[2] = {FIELD(s, seven_tuner_picks), FIELD(s, seven_tuner_letters)};
  const int n[2] = {8, 16};  // a pick per string, a letter and a sharp per string
  for (int a = 0; a < 2; a++)
    for (int k = 0; k < n[a]; k++) {
      uint8_t *at = slots[a] + (size_t)k * w;
      void *obj = rc_mod_ptr(at);
      if (!obj) continue;
      if (w == 8) RC_ST(uint64_t, at, 0);
      else RC_ST(uint32_t, at, 0);
      OV_VCALL((uint64_t)(uintptr_t)obj, (int)RC_SLOT(tuner_entity_release));
    }
}

// TS8RS: the session's reset has put the standard tuning in the game's targets and zeroed the peaks: the tail too
static void on_ts8rs(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  uint8_t *s = PTR(f, "session");
  if (!s) return;
  for (int k = 0; k < 8; k++) {
    int open = k < 6 ? RC_LD(int32_t, FIELD(s, player_tuner_open) + 4 * k) : g_base_extra[k - 6];
    RC_ST(int32_t, FIELD(s, seven_tuner_open) + 4 * k, open);
    RC_ST(int32_t, FIELD(s, seven_tuner_offset) + 4 * k, 0);
    RC_ST(uint32_t, FIELD(s, seven_tuner_peak) + 4 * k, 0);  // 0.0f
  }
}

// The guitar was bound to the session (or bound again): its count
static void on_ts8bind(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  uint8_t *s = PTR(f, "session");
  if (s) refresh_count(s);
}

// TS: Play_TunerSession_ApplyTuning has put the target tuning in the game's arrays (the guitar's or the bass's open
// notes): strings 0-5 to the tail, strings 6 / 7 from TK's keys for the same offset array. An array TK has not read
// keeps the session's 7/8-string target.
static void on_ts(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  uint8_t *s = PTR(f, "session");
  const uint8_t *offsets = PTR(f, "offsets");
  if (!s || !offsets) return;
  for (int k = 0; k < 6; k++) {
    RC_ST(int32_t, FIELD(s, seven_tuner_open) + 4 * k, RC_LD(int32_t, FIELD(s, player_tuner_open) + 4 * k));
    RC_ST(int32_t, FIELD(s, seven_tuner_offset) + 4 * k, RC_LD(int32_t, FIELD(s, player_tuner_offset) + 4 * k));
  }
  const tuner_key *key = key_of(offsets);
  if (key) {
    int extra[2] = {key->found ? key->t6 : 0, key->found7 ? key->t7 : 0};
    for (int e = 0; e < 2; e++) {
      RC_ST(int32_t, FIELD(s, seven_tuner_open) + 4 * (6 + e), g_base_extra[e] + extra[e]);
      RC_ST(int32_t, FIELD(s, seven_tuner_offset) + 4 * (6 + e), extra[e]);
    }
    RC_ST(uint8_t, FIELD(s, seven_tuner_target), !key->found ? 0 : key->found7 ? 8 : 7);
  }
  refresh_count(s);
}

// ---- the order
// A step block of the game (cur < count - 1 / cur > 0, then cur +- 1) as one step along the order: no further string
// goes on at `none`, else the session's string is the new one (and `string`, when the point has it) and it goes on at
// `moved`, past the game's compare
static void move(rc_mid_frame *f, int dir, int vsession, int vstring, int knone, int kmoved) {
  uint8_t *s = (uint8_t *)(uintptr_t)rc_mid_get(f, vsession);
  if (!s) return;
  int to = step(s, dir);
  if (to < 0) {
    f->go = knone;
    return;
  }
  RC_ST(int32_t, FIELD(s, player_tuner_string), to);
  if (vstring >= 0) rc_mid_set(f, vstring, (uint32_t)to);
  f->go = kmoved;
}

#define STEP_POINT(fn, dir, cont)                                                                                     \
  static void fn(rc_mid_frame *f, void *user) {                                                                       \
    if (!seven_on) return;                                                                                            \
    move(f, dir, SEVEN_VAL(f, "session"), SEVEN_VAL(f, "string"), SEVEN_CONT(f, "none"), SEVEN_CONT(f, cont));         \
  }
#define STEP_POINT_S(fn, dir)                                                                                         \
  static void fn(rc_mid_frame *f, void *user) {                                                                       \
    if (!seven_on) return;                                                                                            \
    move(f, dir, SEVEN_VAL(f, "session"), -1, SEVEN_CONT(f, "none"), SEVEN_CONT(f, "moved"));                          \
  }
STEP_POINT(on_sta_next, 1, "select")
STEP_POINT(on_sta_prev, -1, "select")
STEP_POINT(on_stb_next, 1, "select")
STEP_POINT(on_stb_prev, -1, "select")
STEP_POINT_S(on_md2_next, 1)
STEP_POINT_S(on_md2_prev, -1)
STEP_POINT_S(on_md0, 1)
STEP_POINT_S(on_md1, 1)
STEP_POINT_S(on_md3_next, 1)
STEP_POINT_S(on_md3_prev, -1)

// TS8F1: mode 0 stores string 0 as the current one: the first of the order
static void on_ts8f1(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  uint8_t *s = PTR(f, "session");
  if (s) SEVEN_SET(f, "string", (uint32_t)first_of(s));
}

// TS8F2: the capo step set string 0 and selects it: the first of the order
static void on_ts8f2(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  uint8_t *s = PTR(f, "session");
  if (s) RC_ST(int32_t, FIELD(s, player_tuner_string), first_of(s));
}

// TS8F3: mode 3 selects string 0, or 5 for the reverse pass (the last string of every order): 0 is the first
static void on_ts8f3(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  uint8_t *s = PTR(f, "session");
  if (s && SEVEN_GET(f, "string") == 0) SEVEN_SET(f, "string", (uint32_t)first_of(s));
}

// TS8M2E: mode 2 entered from a menu (sub mode 0, string 0) begins at the first string of the order; the argument
// itself changes, as mode 5 selects again from it. Sub mode 1 (mode 0's fall-back) and the reverse pass keep theirs.
static void on_ts8m2e(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  uint8_t *s = PTR(f, "session");
  if (s && SEVEN_GET(f, "submode") == 0 && SEVEN_GET(f, "string") == 0)
    SEVEN_SET(f, "string", (uint32_t)first_of(s));
}

// ---- single reads
// TS8FIN: FadeIn's letter of the string, from the tail (the game's image lists a function that jumps into this
// instruction's bytes, so it is no edit)
static void on_ts8fin_letter(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  uint8_t *s = PTR(f, "session");
  uint32_t str = (uint32_t)SEVEN_GET(f, "string");
  if (!s || str >= 8) return;
  void *letter = rc_mod_ptr(FIELD(s, seven_tuner_letters) + (size_t)str * 2 * (size_t)rc_guest_ptr_size);
  SEVEN_SET(f, "letter", (uintptr_t)letter);
  f->go = SEVEN_CONT(f, "done");
}

// TS8NTI: the precision tuner's label of a string. The game's table has strings 0-5; strings 6 / 7 show the name of
// their open note.
static void nti_label(rc_mid_frame *f, int vstring, int vlabel, int vnote_tuner, int kdone) {
  uint32_t str = (uint32_t)rc_mid_get(f, vstring);
  uint8_t *nt = (uint8_t *)(uintptr_t)rc_mid_get(f, vnote_tuner);
  if (str < 6 || str >= 8 || !nt) return;
  int midi = open_of(nt - RC_OFF(player_tuner_note_tuner), (int)str);
  rc_mid_set(f, vlabel, (uintptr_t)g_notes[((midi % 12) + 12) % 12]);
  f->go = kdone;
}

static void on_nti_label(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  nti_label(f, SEVEN_VAL(f, "string"), SEVEN_VAL(f, "label"), SEVEN_VAL(f, "note_tuner"), SEVEN_CONT(f, "done"));
}

static void on_nti_label2(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  nti_label(f, SEVEN_VAL(f, "string"), SEVEN_VAL(f, "label"), SEVEN_VAL(f, "note_tuner"), SEVEN_CONT(f, "done"));
}

// TS8SN1 / TS8SN2: PlayTunedSound clamps the string to the six Wwise colours; the note name is of the string itself
static const uint8_t *g_sound_session;
static int g_sound_string = -1;

static void on_ts8sn1(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  g_sound_session = PTR(f, "session");
  g_sound_string = (int)(uint32_t)SEVEN_GET(f, "string");
}

static void on_ts8sn2(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  const uint8_t *s = PTR(f, "session");
  int str = g_sound_string;
  g_sound_string = -1;
  if (!s || s != g_sound_session || str < 0 || str >= 8) return;
  SEVEN_SET(f, "note", (uint32_t)open_of(s, str));
  f->go = SEVEN_CONT(f, "done");
}

// ---- low range (7/8 strings): the wrong-instrument check and the detector's lowest open note follow the lowest string
// TS8SEL: a selected string of a 7/8-string session lowers the guitar detection's lowest open note to the session's
// lowest (never raises it). The game's call uses eax / ecx / edx: they get their values back.
static void on_ts8sel(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  uint8_t *s = PTR(f, "session");
  if (!s || count_of(s) < 7) return;
  int low = lowest_open(s), str = (int)(uint32_t)SEVEN_GET(f, "string");
  uint64_t eax = SEVEN_GET(f, "eax"), ecx = SEVEN_GET(f, "ecx"), edx = SEVEN_GET(f, "edx");
  uint32_t input = RC_LD(uint32_t, FIELD(s, player_tuner_input));
  // The detection of the session's input: Learn & Play has a function for it (play_get_detection); the Mac game has
  // it inlined everywhere, so there the input's Play_DetectInput is asked for, which is the detector too
  uint8_t *holder = NULL, *det;
  if (RC_ADDR(play_get_detection)) {
    det = RC_HOST(OV_GUEST(play_get_detection, input));
  } else {
    holder = RC_HOST(OV_GUEST(play_detect_input, input));
    det = holder ? rc_mod_ptr(FIELD(holder, detector_detection)) : NULL;
  }
  SEVEN_SET(f, "eax", eax);
  SEVEN_SET(f, "ecx", ecx);
  SEVEN_SET(f, "edx", edx);
  if (str >= 0 && str < 8) tlog("session %p selects string %d, target MIDI %d", (void *)s, str, open_of(s, str));
  if (!det) return;
  uint8_t *detector = holder ? holder : rc_mod_ptr(FIELD(det, detection_detector));
  uint8_t *matcher = detector ? rc_mod_ptr(FIELD(detector, detector_matcher)) : NULL;
  if (!matcher || rc_mod_ptr(FIELD(matcher, matcher_detection)) != det) return;  // a detection with its matcher
  if (RC_LD(int32_t, FIELD(det, detection_instrument)) == 2) return;             // not a bass's
  int cur = RC_LD(int32_t, FIELD(det, detection_lowest_open));
  if (low >= cur) return;
  tlog("low range: detection %p lowest open note %d -> %d", (void *)det, cur, low);
  RC_ST(int32_t, FIELD(det, detection_lowest_open), low);
}

// W: the guitar's wrong-instrument check compares the mean of ten detected notes with the game's limit (34, a note
// below a guitar's); a 7/8-string session's limit is its lowest open note - 1 when that is lower. The compare runs
// against the game's limit: the mean becomes the limit - 1, the limit or + 1, as it orders against the session's.
static void on_w(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  const uint8_t *s = PTR(f, "session");
  if (!s || count_of(s) < 7) return;
  double limit = RC_LD(float, (const void *)(uintptr_t)RC_GA(player_tuner_low_note_limit));
  double thr = lowest_open(s) - 1, mean = SEVEN_GETF(f, "mean");
  if (thr >= limit || mean != mean) return;  // the game's limit, or a NaN (unordered either way)
  SEVEN_SETF(f, "mean", limit + (mean < thr ? -1 : mean > thr ? 1 : 0));
}

// ---- others
// LT1: PlacePegLetters turns the letters of a 3+3 headstock's far pegs round unless the InstrumentInfo says in-line:
// the strings part answers for a guitar that shows the in-line 7-string headstock on a 3+3 frame. The flags are the
// skipped compare's (the answer against zero, as the game compares the in-line byte), which the next jump reads.
static void on_lt1(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  const uint8_t *info = PTR(f, "info");
  if (!info) return;
  uint8_t v = (uint8_t)seven_strings_letter_inline(info);
  uint32_t fl = f->flags & ~((1u << 0) | (1u << 2) | (1u << 4) | (1u << 6) | (1u << 7) | (1u << 11));
  if (!v) fl |= 1u << 6;                            // ZF
  if (v & 0x80) fl |= 1u << 7;                      // SF
  if (!(__builtin_popcount(v) & 1)) fl |= 1u << 2;  // PF
  f->flags = fl;
  f->go = SEVEN_CONT(f, "done");
}

int seven_tuner_init(void) {
  static const char keys[] = "Rs7String6\0Rs7String7";
  static const char *const names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  char *mem = rc_guest_alloc(sizeof keys + 12 * 3);
  if (!mem) {
    rsm_log("seven-string: tuner: no memory in the game for its strings");
    return -1;
  }
  memcpy(mem, keys, sizeof keys);
  g_key6 = mem;
  g_key7 = mem + strlen(keys) + 1;
  char *p = mem + sizeof keys;
  for (int k = 0; k < 12; k++, p += 3) {
    memcpy(p, names[k], strlen(names[k]) + 1);
    g_notes[k] = p;
  }
  static const struct {
    const char *name;
    rc_mid_fn fn;
  } points[] = {
      {"seven_tu8init", on_tu8init},       {"seven_ts8rs", on_ts8rs},
      {"seven_tu8dt", on_tu8dt},
      {"seven_ts8bind", on_ts8bind},       {"seven_ts", on_ts},
      {"seven_tk", on_tk},                 {"seven_ts8fin_letter", on_ts8fin_letter},
      {"seven_ts8f1", on_ts8f1},           {"seven_ts8f2", on_ts8f2},
      {"seven_ts8m2e", on_ts8m2e},         {"seven_ts8f3", on_ts8f3},
      {"seven_ts8sta_next", on_sta_next},  {"seven_ts8sta_prev", on_sta_prev},
      {"seven_ts8stb_prev", on_stb_prev},  {"seven_ts8stb_next", on_stb_next},
      {"seven_ts8sel", on_ts8sel},         {"seven_lt1", on_lt1},
      {"seven_w", on_w},                   {"seven_ts8sn1", on_ts8sn1},
      {"seven_ts8sn2", on_ts8sn2},         {"seven_ts8md2_prev", on_md2_prev},
      {"seven_ts8md2_next", on_md2_next},  {"seven_ts8md0", on_md0},
      {"seven_ts8md1", on_md1},            {"seven_ts8md3_prev", on_md3_prev},
      {"seven_ts8md3_next", on_md3_next},  {"seven_ts8nti_label", on_nti_label},
      {"seven_ts8nti_label2", on_nti_label2},
  };
  for (size_t k = 0; k < sizeof points / sizeof *points; k++)
    if (seven_mid(points[k].name, points[k].fn)) return -1;
  return 0;
}
