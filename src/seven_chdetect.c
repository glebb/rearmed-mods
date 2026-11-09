// Seven-String Guitar (seven.h): the chdetect part, a chord with a low B detected and scored. After rs7string's
// native N-slot chord detection (github.com/SomeeName/rs7string, MIT, by SomeeName: src/features/nslot, its match.cpp
// "nsmatch", tracker.cpp and hooks; ARCHITECTURE section 7).
//
// A chord's targets are an int32[8] (strings 0-7, -1 none) wherever the game's matcher, Play_Matcher_DetectChord, is
// reached: the note judge's requests, the Lua binding, the whole-chord tracker and the eval wrappers widen their arrays
// to eight by the edits of seven_string (RQ, PR, MS, LU, WC, EV, PD1), and the tracker's eval reads its strings 6/7 from
// the tails the chords part keeps in it (seven_chords.h: seven_eval_*). Here:
//   seven_md1        the matcher's entry: up to six targets go to the game's own body (compacted into an int32[6] when
//                    strings 6/7 have one), seven or eight are judged by the game's algorithm ported to n targets, with
//                    the game's sort and subharmonic table, and [Seven String] ChordWideMisses
//   seven_am1        the mistake analysis's shifted chord: its six targets and the T8's strings 6/7, shifted
//   seven_ch3        a chord's string 6 gets a child eval (the edits CC, CN, CH, BF1 widen its notes to string 6); its slot
//                    is a byte of the parent eval (+0xc1e), which seven_rl1/rl2 release with the parent
//   seven_ag1..ag3   the three child queries, one loop over eight strings
//   seven_up1..up3, seven_pd2..pd5   the per-string technique checks: the game's loop over strings 0-5, then the tails
//
// [Seven String] ChordWideMisses (0-2, default 2) in RSMods.ini: a seven- or eight-note chord may miss that many notes
// (on any string: with 2, a 7-note chord passes without its low B); five- and six-note chords one, two- to four-note
// chords none, as the game has them. Read once at the start.
#include <stdlib.h>
#include <string.h>

#include "seven.h"

// The chords part (seven_chords.h): `record` itself when it is a T8 record of a loaded song, else NULL; a T8's string
// s (0-7): its expected MIDI note, -1 none
extern uint8_t *seven_chords_as_t8(const void *record);
extern int32_t seven_chords_t8_midi(const uint8_t *t8, int s);

static uint8_t *P(uint64_t v) { return (uint8_t *)(uintptr_t)v; }
static uint64_t arg(const void *p) { return (uint64_t)(uintptr_t)p; }

#define TARGETS 8    // a chord's targets: strings 0-7
#define STOCK 6      // the game's arrays: six strings
#define MIDI_NOTES 128
// The game sorts the first 127 of its 128 candidates (the last stays where it is), with STLport's std::sort: its
// introsort loop to runs of 16, then an insertion sort of the first 16 and an unguarded one of the rest
#define SORTED 127
#define INSERTION_RUN 16

static int g_misses = 2;  // ChordWideMisses
static int g_lines;       // log lines of the port's verdicts

// --- memory the game can be given, per thread -------------------------------------------------------------------
// The compacted targets the game's body gets (seven_md1), the eight the mistake analysis gives the wrapper (seven_am1),
// and the port's candidates, which the game's sort works on. Never freed: one block for each thread that judges chords.
typedef struct {
  int32_t six[TARGETS];
  int32_t eight[TARGETS];
  uint8_t cand[];  // MIDI_NOTES candidates of matcher_candidate_size bytes
} scratch_t;

static scratch_t *scratch(void) {
  static __thread scratch_t *s;
  static int logged;
  if (!s) {
    s = rc_guest_alloc(sizeof(scratch_t) + MIDI_NOTES * (size_t)RC_OFF(matcher_candidate_size));
    if (!s && !__atomic_exchange_n(&logged, 1, __ATOMIC_RELAXED))
      rsm_log("seven-string: chord detection: no game memory for a thread's chords: its 7/8-note chords are judged on "
              "their first six strings");
  }
  return s;
}

// --- the game's algorithm over n targets (rs7string's MatchN, Play_Matcher_DetectChord 0x10687a0) ---------------
// Dedupe (0x10687bc): a value already seen becomes -1. Returns the targets left.
static int dedupe(const int32_t *in, int n, int32_t *loc) {
  int active = 0;
  for (int i = 0; i < n; i++) {
    loc[i] = in[i];
    for (int j = 0; j < i; j++)
      if (in[i] == in[j]) {
        loc[i] = -1;
        break;
      }
  }
  for (int i = 0; i < n; i++) active += loc[i] > -1;
  return active;
}

// Three targets or fewer: the lowest (if below 65) matches on its fundamental only. That target, -1 when the lowest is
// 65 or higher, 128 (no target's) with more than three.
static int32_t special_target(const int32_t *loc, int n, int active) {
  int32_t low = MIDI_NOTES;
  if (active > 3) return low;
  for (int i = 0; i < n; i++)
    if (loc[i] > 0 && loc[i] < low) low = loc[i];
  return low >= 65 ? -1 : low;
}

// The detection's frame `hist` frames back: the strength of its peaks summed per MIDI bin (float sums, as the game's)
static void frame_bins(const uint8_t *matcher, int hist, float *bins) {
  const uint8_t *det = rc_mod_ptr(matcher + RC_OFF(matcher_detector));
  const uint8_t *d = det ? rc_mod_ptr(det + RC_OFF(detector_detection)) : NULL;
  if (!d || RC_LD(int32_t, d + RC_OFF(detection_frame_count)) <= 0) return;
  int32_t idx = RC_LD(int32_t, d + RC_OFF(detection_frame_write));
  for (int k = hist; k > 0; k--)
    if (--idx < 0) idx = RC_LD(int32_t, d + RC_OFF(detection_frame_capacity)) - 1;
  const uint8_t *frames = rc_mod_ptr(d + RC_OFF(detection_frames));
  const int64_t size = RC_OFF(detection_frame_size);
  uint8_t fr[2048];
  if (!frames || size > (int64_t)sizeof fr) return;
  memcpy(fr, frames + (int64_t)idx * size, (size_t)size);  // the game copies the frame first, then reads it
  const float level = RC_LD(float, fr + RC_OFF(frame_level));
  if (!((double)level > -55.0)) return;  // a frame at -55 dB or below (or NaN) gives no strength
  const int32_t peaks = RC_LD(int32_t, fr + RC_OFF(frame_peak_count));
  const int64_t step = RC_OFF(frame_peak_size), first = RC_OFF(frame_peaks);
  for (int32_t k = 0; k < peaks && first + (k + 1) * step <= size; k++) {
    const int32_t b = RC_LD(int32_t, fr + first + k * step);
    const float s = RC_LD(float, fr + first + k * step + RC_OFF(frame_peak_strength));
    if (b > -1 && b < MIDI_NOTES) bins[b] = s + bins[b];
  }
}

// The candidates: {MIDI bin, strength}, matcher_candidate_size bytes each, in the game's memory
static float strength(const uint8_t *c) { return RC_LD(float, c + RC_OFF(matcher_candidate_strength)); }
static int32_t bin(const uint8_t *c) { return RC_LD(int32_t, c + RC_OFF(matcher_candidate_bin)); }

// The game's comparator (0x1067ad0, inlined in its insertion sorts): the stronger first; NaN never stronger
static int stronger(const uint8_t *a, const uint8_t *b) { return strength(a) > strength(b); }

// 0x1065240 (its unguarded linear insert): moves val left past the weaker ones, from `last`
static void linear_insert(uint8_t *last, const uint8_t *val, int64_t size) {
  uint8_t *next = last - size;
  while (stronger(val, next)) {
    memcpy(last, next, (size_t)size);
    last = next;
    next -= size;
  }
  memcpy(last, val, (size_t)size);
}

// 0x10694d0 (its insertion sort of a run, edi = first): one stronger than the first goes in front, the others insert
static void insertion_sort(uint8_t *first, uint8_t *last, int64_t size) {
  uint8_t val[32];
  if (first == last || size > (int64_t)sizeof val) return;
  for (uint8_t *i = first + size; i != last; i += size) {
    memcpy(val, i, (size_t)size);
    if (stronger(val, first)) {
      memmove(first + size, first, (size_t)(i - first));
      memcpy(first, val, (size_t)size);
    } else {
      linear_insert(i, val, size);
    }
  }
}

// 0x1065100 (its unguarded insertion sort of the rest: the first run holds a candidate no weaker than any after it)
static void unguarded_insertion_sort(uint8_t *first, uint8_t *last, int64_t size) {
  uint8_t val[32];
  if (size > (int64_t)sizeof val) return;
  for (uint8_t *i = first; i != last; i += size) {
    memcpy(val, i, (size_t)size);
    linear_insert(i, val, size);
  }
}

// The game's sort of the candidates (0x1068c64..0x1068cf3): its introsort loop (matcher_sort, with its comparator and
// a depth of twice log2 of the count), then the insertion passes, ported (the first takes its range in edi)
static void game_sort(uint8_t *v) {
  const int64_t size = RC_OFF(matcher_candidate_size);
  uint8_t *last = v + SORTED * size;
  int depth = 0;
  for (int k = SORTED; k != 1; k >>= 1) depth++;
  OV_GUEST(matcher_sort, arg(v), arg(last), 0, (uint64_t)(2 * depth), RC_ADDR(matcher_candidate_less));
  if (SORTED > INSERTION_RUN) {
    insertion_sort(v, v + INSERTION_RUN * size, size);
    unguarded_insertion_sort(v + INSERTION_RUN * size, last, size);
  } else {
    insertion_sort(v, last, size);
  }
}

static int count_found(const int32_t *cnt, int n) {
  int found = 0;
  for (int i = 0; i < n; i++) found += cnt[i] > 0;
  return found;
}

// The strength the targets' rows matched and did not, and how many fundamentals (a row's first note) matched
typedef struct {
  float matched, unmatched;
  int fund;
} score_t;

// Walks the sorted candidates, strongest first, until the strength runs out or every target is found; cnt[i] counts
// the candidates on target i's row (six MIDI notes, read from the game's table or the special row)
static score_t score(const uint8_t *v, const uint8_t *const *row, const int32_t *loc, int n, int active,
                     int32_t *cnt) {
  const int64_t size = RC_OFF(matcher_candidate_size);
  score_t r = {0.0f, 0.0f, 0};
  for (int e = 0; e < MIDI_NOTES; e++) {
    const uint8_t *c = v + e * size;
    const float s = strength(c);
    if (!(0.0f < s) && s == s) break;  // the game goes on while 0 < s, or s is NaN
    int hit = 0;
    for (int i = 0; i < n; i++) {
      if (!row[i] && !(loc[i] > -1)) continue;
      for (int k = 0; k < STOCK; k++)
        if (RC_LD(int32_t, row[i] + 4 * k) == bin(c)) {
          if (k == 0) r.fund++;
          r.matched = s + r.matched;
          cnt[i]++;
          hit = 1;
        }
    }
    if (count_found(cnt, n) == active) break;
    if (!hit) r.unmatched = s + r.unmatched;
  }
  return r;
}

// m * k > u as the game's x87 code makes it (a float times a double, compared with a float; the runtime's x87 is
// double precision)
static int scaled_greater(float m, double k, float u) { return (double)m * k > (double)u; }

// The game's rule per target count. Seven or eight: the game has no case (its last rule would need every one); with
// ChordWideMisses the five/six-note rule widened: all found and less unmatched than matched strength, or up to that
// many missing and unmatched below 0.2 x matched.
static int verdict(int active, int found, score_t r, int tol, int misses) {
  const int less = r.unmatched < r.matched;
  if (active == 6)
    return (found >= 6 - tol && less) || (found == 5 - tol && scaled_greater(r.matched, 0.20000000298023224, r.unmatched));
  if (active == 5)
    return (found >= 5 - tol && less) || (found == 4 - tol && scaled_greater(r.matched, 0.20000000298023224, r.unmatched));
  if (active >= 7 && misses > 0)
    return (found >= active - tol && less) ||
           (found < active - tol && found >= active - misses - tol &&
            scaled_greater(r.matched, 0.20000000298023224, r.unmatched));
  return r.fund == active || (found >= active - tol && less);
}

// 0x10692c0: exactly two positive targets, the second the first + 12 (an octave dyad)
static int octave_dyad(const int32_t *in, int n) {
  int count = 0, octave = 0;
  int32_t first = 0;
  for (int i = 0; i < n; i++) {
    const int32_t v = in[i];
    if (v <= 0) continue;
    count++;
    if (first <= 0)
      first = v;
    else if (first + 12 == v)
      octave = 1;
    else
      return 0;
  }
  return octave && count == 2;
}

static int match_n(const uint8_t *m, const int32_t *in, int n, int tol, int hist, uint8_t *v) {
  int32_t loc[TARGETS], cnt[TARGETS];
  const int active = dedupe(in, n, loc);
  const int32_t low = special_target(loc, n, active);
  int32_t special[STOCK] = {0};
  if (low > 0 && low < 65) special[0] = low;
  const uint8_t *table = (const uint8_t *)(uintptr_t)RC_GA(matcher_subharmonics);  // rows of six notes per note
  const uint8_t *row[TARGETS] = {0};
  for (int i = 0; i < n; i++) {
    cnt[i] = -1;
    if (loc[i] > -1) {
      row[i] = loc[i] == low ? (const uint8_t *)special : table + (int64_t)loc[i] * STOCK * 4;
      cnt[i] = 0;
    }
  }
  float bins[MIDI_NOTES] = {0};
  frame_bins(m, hist, bins);
  const int64_t size = RC_OFF(matcher_candidate_size);
  memset(v, 0, (size_t)(MIDI_NOTES * size));
  for (int b = 0; b < MIDI_NOTES; b++) {
    RC_ST(int32_t, v + b * size + RC_OFF(matcher_candidate_bin), b);
    RC_ST(float, v + b * size + RC_OFF(matcher_candidate_strength), bins[b]);
  }
  game_sort(v);
  const score_t r = score(v, row, loc, n, active, cnt);
  const int found = count_found(cnt, n);
  const uint8_t *det = rc_mod_ptr(m + RC_OFF(matcher_detection));
  // A bass's octave dyad passes with one note found and unmatched below 0.3 x matched
  if (det && RC_LD(int32_t, det + RC_OFF(detection_instrument)) == 2 && octave_dyad(in, n) &&
      RC_LD(uint8_t, m + RC_OFF(matcher_octave_dyads)) && found >= 1 &&
      scaled_greater(r.matched, 0.30000001192092896, r.unmatched))
    return 1;
  const int ok = verdict(active, found, r, tol, g_misses);
  if (g_lines < 64) {
    g_lines++;
    rsm_log("seven-string: chord detection: %d targets, %d found (tolerance %d, %d may miss): matched %.4g, unmatched "
            "%.4g: %s",
            active, found, tol, g_misses, (double)r.matched, (double)r.unmatched, ok ? "played" : "not played");
  }
  return ok;
}

// --- the mid points -----------------------------------------------------------------------------------------------
// Play_Matcher_DetectChord's entry (stdcall: matcher, int32 targets[8], tolerance, history index; al = played). No
// string 6/7 target: the game's body as it is (it reads six). Up to six targets in all: compacted, in order, into an
// int32[6] the body gets (the -1 holes change nothing it looks at). Seven or eight: judged here, on at its ret.
static void on_md1(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  const int32_t *in = (const int32_t *)P(SEVEN_GET(f, "targets"));
  const uint8_t *m = P(SEVEN_GET(f, "matcher"));
  if (!in || !m || (RC_LD(int32_t, in + STOCK) < 0 && RC_LD(int32_t, in + STOCK + 1) < 0)) return;
  scratch_t *s = scratch();
  if (!s) return;
  int k = 0;
  for (int i = 0; i < TARGETS; i++) k += RC_LD(int32_t, in + i) >= 0;
  if (k <= STOCK) {
    k = 0;
    for (int i = 0; i < TARGETS; i++)
      if (RC_LD(int32_t, in + i) >= 0) RC_ST(int32_t, s->six + k++, RC_LD(int32_t, in + i));
    while (k < STOCK) RC_ST(int32_t, s->six + k++, -1);
    SEVEN_SET(f, "targets", arg(s->six));
    return;
  }
  int32_t t[TARGETS];
  for (int i = 0; i < TARGETS; i++) t[i] = RC_LD(int32_t, in + i);
  const int ok = match_n(m, t, TARGETS, (int)(int32_t)SEVEN_GET(f, "tolerance"), (int)(int32_t)SEVEN_GET(f, "history"),
                         s->cand);
  SEVEN_SET(f, "matched", (uint32_t)ok);
  f->go = SEVEN_CONT(f, "ret");
}

// Play_AnalyzeChordMistake tries the chord shifted by -2..2 semitones (edi): the wrapper it calls gets the six shifted
// targets and the chord's (a T8's) strings 6/7 shifted the same way
static void on_am1(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  const int32_t *six = (const int32_t *)P(SEVEN_GET(f, "targets"));
  scratch_t *s = scratch();
  if (!six || !s) return;
  const uint8_t *t8 = seven_chords_as_t8(P(SEVEN_GET(f, "chord")));
  const int32_t shift = (int32_t)(uint32_t)SEVEN_GET(f, "shift");
  for (int i = 0; i < STOCK; i++) RC_ST(int32_t, s->eight + i, RC_LD(int32_t, six + i));
  for (int i = STOCK; i < TARGETS; i++) {
    const int32_t midi = t8 ? seven_chords_t8_midi(t8, i) : -1;
    RC_ST(int32_t, s->eight + i, midi < 0 ? -1 : midi + shift);
  }
  SEVEN_SET(f, "targets", arg(s->eight));
}

// --- the child evals of a chord's strings -------------------------------------------------------------------------
static uint8_t *eval_of(uint8_t *tracker, int slot) {
  return tracker + RC_OFF(tracker_evals) + (int64_t)slot * RC_OFF(eval_size);
}

// The child eval slot of string s of a parent eval, -1 none: strings 0-5 the game's int32[6], 6/7 the bytes (0xff none)
static int child(const uint8_t *ev, int s) {
  if (s < STOCK) return RC_LD(int32_t, ev + RC_OFF(eval_children) + 4 * s);
  return (int8_t)RC_LD(uint8_t, ev + RC_OFF(seven_eval_child67) + (s - STOCK));
}

// Play_Tracker_AddNoteOrChord's child loop, the store of string s's child slot (eax) into the parent's (slot
// [ebp-0xe00], -1 none): strings 0-5 the game's; 6/7 the parent's byte, none when the parent has no slot
static void on_ch3(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  const uint32_t s = (uint32_t)SEVEN_GET(f, "string");
  if (s < STOCK || s >= TARGETS) return;
  const int32_t parent = (int32_t)(uint32_t)SEVEN_GET(f, "parent");
  uint8_t *tracker = P(SEVEN_GET(f, "tracker"));
  if (parent != -1 && tracker)
    RC_ST(uint8_t, eval_of(tracker, parent) + RC_OFF(seven_eval_child67) + (s - STOCK), (uint8_t)SEVEN_GET(f, "child"));
  f->go = SEVEN_CONT(f, "done");
}

// The eval releases (esi a chord's slot in the tracker): the children of strings 6/7 go first, as the game's six
// after (tracker_eval_release, the same function), and their bytes become 0xff
static void release_tails(rc_mid_frame *f) {
  uint8_t *slot = P(SEVEN_GET(f, "slot")), *tracker = P(SEVEN_GET(f, "tracker"));
  if (!slot || !tracker) return;
  uint8_t *bytes = slot + RC_OFF(tracker_evals) + RC_OFF(seven_eval_child67);
  for (int i = 0; i < TARGETS - STOCK; i++) {
    const uint8_t c = RC_LD(uint8_t, bytes + i);
    if (c == 0xff) continue;
    OV_GUEST(tracker_eval_release, arg(tracker), c);
    RC_ST(uint8_t, bytes + i, 0xff);
  }
}

static void on_rl(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  release_tails(f);
}

// An eval's judgements A and B and its flag k (0-2) of each: "empty" when neither is on; else 1 ANDed with the flag of
// each one on
static int empty(const uint8_t *e) {
  return !RC_LD(uint8_t, e + RC_OFF(eval_judge_a_on)) && !RC_LD(uint8_t, e + RC_OFF(eval_judge_b_on));
}

static uint8_t fold(const uint8_t *e, int k, uint8_t al) {
  if (RC_LD(uint8_t, e + RC_OFF(eval_judge_a_on))) al &= RC_LD(uint8_t, e + RC_OFF(eval_judge_a) + k);
  if (RC_LD(uint8_t, e + RC_OFF(eval_judge_b_on))) al &= RC_LD(uint8_t, e + RC_OFF(eval_judge_b) + k);
  return al;
}

// The three child queries as one loop over eight strings (rs7string's Rs7Ns_Aggregate; for strings 0-5 the game's
// unrolled result): a chord judged whole answers by its own flags; else every child's flags ANDed, an empty child
// failing the chord (zero_on_empty) or skipped; a string query (string != NULL) that string's child alone
static uint8_t aggregate(const uint8_t *ev, uint8_t *tracker, int k, int zero_on_empty, const uint8_t *string) {
  if (!ev || !RC_LD(uint8_t, ev + RC_OFF(eval_is_chord))) return 0;
  if (RC_LD(uint8_t, ev + RC_OFF(eval_whole))) return empty(ev) ? 0 : fold(ev, k, 1) & 1;
  if (string) {
    const int s = RC_LD(uint8_t, string);
    if (s >= TARGETS) return 0;
    const int c = child(ev, s);
    if (c < 0) return 0;
    const uint8_t *ce = eval_of(tracker, c);
    return empty(ce) ? 0 : fold(ce, k, 1) & 1;
  }
  uint8_t al = 1;
  for (int s = 0; s < TARGETS; s++) {
    const int c = child(ev, s);
    if (c < 0) continue;
    const uint8_t *ce = eval_of(tracker, c);
    if (empty(ce)) {
      if (zero_on_empty) al = 0;
      continue;
    }
    al = fold(ce, k, al);
  }
  return al & 1;
}

// The three points have the same values in the same order (seven_ag1's string unread), so one place serves them
static void answer(rc_mid_frame *f, int k, int zero_on_empty, int by_string) {
  const uint8_t *string = by_string ? P(SEVEN_GET(f, "string")) : NULL;
  SEVEN_SET(f, "result", aggregate(P(SEVEN_GET(f, "eval")), P(SEVEN_GET(f, "tracker")), k, zero_on_empty, string));
  f->go = SEVEN_CONT(f, "ret");
}

static void on_ag1(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  answer(f, 2, 1, 0);
}

static void on_ag2(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  answer(f, 0, 0, 1);
}

static void on_ag3(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  answer(f, 1, 0, 1);
}

// --- the per-string technique checks over the tails (rs7string's tracker.cpp) -----------------------------------
// Tail t (string 6 + t) sounds when its MIDI note is above 0, as the game's loops ask of strings 0-5
static int tail_sounds(const uint8_t *ev, int t) {
  return RC_LD(int32_t, ev + RC_OFF(seven_eval_midi8) + 4 * (STOCK + t)) > 0;
}
static uint32_t tail_mask(const uint8_t *ev, int t) { return RC_LD(uint32_t, ev + RC_OFF(seven_eval_tail_mask) + 4 * t); }

// The eval of a loop pointer past the six per-string masks (eval + 0x2bc + 24)
static const uint8_t *eval_of_masks(uint64_t end) {
  return end ? P(end) - (RC_OFF(eval_string_masks) + 4 * STOCK) : NULL;
}

// "Every sounding string slides unpitched": a sounding tail string without 0x400000 clears it
static void all_unpitched(rc_mid_frame *f) {
  const uint8_t *ev = P(SEVEN_GET(f, "eval"));
  for (int t = 0; ev && t < TARGETS - STOCK; t++)
    if (tail_sounds(ev, t) && !(tail_mask(ev, t) & 0x400000)) SEVEN_SET(f, "all_unpitched", 0);
}

static void on_up(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  all_unpitched(f);
}

// "All / no sounding string ...": every sounding tail ANDs the result with its test, (mask & bits) != 0 (== 0 with
// `none`), the mask's low byte with `byte`; last_test is the last test made, as the game's register keeps it. The
// three points share the callers' value order (one place each: SEVEN_GET caches a value's index per place).
static void fold_tails(rc_mid_frame *f, uint32_t bits, int byte, int none) {
  const uint8_t *ev = eval_of_masks(SEVEN_GET(f, "masks_end"));
  if (!ev) return;
  uint8_t r = (uint8_t)SEVEN_GET(f, "result"), l = (uint8_t)SEVEN_GET(f, "last_test");
  for (int t = 0; t < TARGETS - STOCK; t++) {
    if (!tail_sounds(ev, t)) continue;
    const uint32_t mask = byte ? (tail_mask(ev, t) & 0xff) : tail_mask(ev, t);
    l = (uint8_t)(((mask & bits) != 0) != none);
    r &= l;
  }
  SEVEN_SET(f, "result", r);
  SEVEN_SET(f, "last_test", l);
}

// 0x13f7b00: every sounding string slides (0x800)
static void on_pd2(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  fold_tails(f, 0x800, 0, 0);
}

// 0x13f7b40: no sounding string bends (0x1000), slides (0x800), slides unpitched (0x400000) or tremolos (0x10)
static void on_pd3(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  fold_tails(f, 0x401810, 0, 1);
}

// 0x13f7bb0: every sounding string tremolos (its mask's low byte, 0x10)
static void on_pd4(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  fold_tails(f, 0x10, 1, 0);
}

// 0x13f7bf0: the extreme slide distance (slideTo - fret, both as bytes) over strings with a fret other than 0: the
// first sets it, then a farther one in its direction; the tails continue the game's loop. ecx = the last fretted
// tail's distance, edx = string 7's fret byte, as rs7string leaves them.
static void on_pd5(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  const uint64_t end = SEVEN_GET(f, "slides_end");
  if (!end) return;
  const uint8_t *ev = P(end) - (RC_OFF(eval_slide_to) + STOCK);
  int32_t extreme = (int32_t)(uint32_t)SEVEN_GET(f, "extreme");
  int32_t distance = (int32_t)(uint32_t)SEVEN_GET(f, "distance");
  for (int t = 0; t < TARGETS - STOCK; t++) {
    const int32_t fret = RC_LD(uint8_t, ev + RC_OFF(seven_eval_tail_fret) + t);
    if (fret == 0) continue;
    const int32_t d = (int32_t)RC_LD(uint8_t, ev + RC_OFF(seven_eval_tail_slide) + t) - fret;
    distance = d;
    if (extreme == 0) extreme = d;
    if (extreme < 0 && d < extreme) extreme = d;
    if (extreme > 0 && d > extreme) extreme = d;
  }
  SEVEN_SET(f, "extreme", (uint32_t)extreme);
  SEVEN_SET(f, "distance", (uint32_t)distance);
  SEVEN_SET(f, "fret", (uint32_t)RC_LD(uint8_t, ev + RC_OFF(seven_eval_tail_fret) + 1));
}

// --- shared ---------------------------------------------------------------------------------------------------
int seven_chdetect_wide_misses(void) { return g_misses; }

int seven_chdetect_init(void) {
  const char *v = rsm_get("Seven String", "ChordWideMisses");
  if (v) {
    char *e;
    const long n = strtol(v, &e, 10);
    g_misses = e != v && n >= 0 && n <= 2 ? (int)n : 2;
  }
  rsm_log("seven-string: chord detection: [Seven String] ChordWideMisses = %d: a 7/8-note chord may miss %d notes "
          "(5/6-note chords 1, 2-4-note chords none, as the game has them)",
          g_misses, g_misses);
  static const struct {
    const char *name;
    rc_mid_fn fn;
  } points[] = {
      {"seven_md1", on_md1}, {"seven_am1", on_am1}, {"seven_ch3", on_ch3}, {"seven_rl1", on_rl},
      {"seven_rl2", on_rl},  {"seven_ag1", on_ag1}, {"seven_ag2", on_ag2}, {"seven_ag3", on_ag3},
      {"seven_up1", on_up},  {"seven_up2", on_up},  {"seven_up3", on_up},  {"seven_pd2", on_pd2},
      {"seven_pd3", on_pd3}, {"seven_pd4", on_pd4}, {"seven_pd5", on_pd5},
  };
  for (size_t i = 0; i < sizeof points / sizeof *points; i++)
    if (seven_mid(points[i].name, points[i].fn)) return -1;
  return 0;
}
