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
// What runs here is the chord frame on an extended instrument (rs7string's B1 and B2): a guitar built with 7 or 8
// strings, a 5/6-string bass. The frame model has the height of six lanes, and no game data sizes it: every chord frame
// of such an instrument is fitted to all its strings, as the game's frame covers all six strings of its own (RS Extended
// Range 1.0.0-beta.4: whatever strings the chord uses). seven_b1 marks a frame when the chord box builder has made its
// geometry; seven_b2, when Rnd_GemGeom_Build applies that geometry's span, scales the frame along the string axis by the
// grid part's fit (seven_grid_chord_frame) so it covers lanes 0 to the guitar's last string (6 for 7 strings, 7 for 8),
// or a bass's low B and high C, its edge on the highway floor (the grid part moves the floor of inverted strings out by
// the added lanes on that side: one for 7 strings and a bass, two for 8). The game's own Rnd_Gem_SetScaleTranslate then
// sets what seven_b2 left in its arguments. Guitars built with 6 strings and 4-string basses keep the game's frames.
//
// And the finger markers on the neck, whose barres cover the seventh string (rs7string's PUBFA, PUBGF and PUBBC; at the end).
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "seven.h"
#include "seven_barre.h"

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

// The count of strings a guitar (Rnd_Guitar) was built with when it is 7 or 8, else 0
static int seven_count(const uint8_t *guitar) {
  int n = guitar ? seven_strings_guitar_count(guitar) : 0;
  return n == 7 || n == 8 ? n : 0;
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

// The last string of a guitar built with `count` strings (7 or 8): every chord frame reaches it, whatever strings the
// chord uses (rs7string's Rs7Chord_OnBoxGeom since 1.0.0-beta.4: top = count - 1; before, 6 when the chord used string 6
// and 5 otherwise, so a chord of strings 0-5 got a frame that stopped short of the seventh string).
static int frame_top(int count) { return (count > SEVEN_CAPACITY ? SEVEN_CAPACITY : count) - 1; }

// B1: the chord box builder has made the chord frame's geometry (geom, 0 when its allocation failed) for the chord
// render object `chord` on the play session `session`. On a guitar built with 7 or 8 strings it is marked, every frame
// (rs7string's Rs7Chord_OnBoxGeom): it covers all the guitar's lanes, up to its last string.
static void on_b1(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  const uint8_t *geom = PTR(f, "geom"), *chord = PTR(f, "chord"), *session = PTR(f, "session");
  const uint8_t *guitar = geom && chord ? ptr_at(session, RC_OFF(session_guitar)) : NULL;
  const int count = seven_count(guitar);
  // a 5/6-string bass (the grid part's fit, top 0 marks it): its frame spans its low B / high C too
  if (!count && !(guitar && seven_grid_is_extended(guitar))) return;
  const int top = count ? frame_top(count) : 0;
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
  if (!seven_at(SEVEN_GET(f, "from"), RC_ADDR(gemgeom_build_span_ret))) return;  // a code address, as mapped
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
  // the grid part's fit over every string (seven_grid.h): up to the guitar's last string (6 or 7, frame_top's), an
  // extended bass's low B and high C, with the floor as the grid part moved it (one spacing for 7 strings and a bass,
  // two for 8)
  seven_frame_fit fit;
  float b[3];
  if (!seven_grid_chord_frame(guitar, &fit, b)) {
    clog("chord frame up to string %d: no lane geometry on its guitar: the game's frame", top ? top : 6);
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

// ---- strings 6 and 7 where a build unrolled a six-string loop (the Mac: no count to widen). Each point sits after the
// game's strings 0-5 and does strings 6 .. seven_chord_native() - 1 by the game's own per-string call (the extra
// strings of a seven-string song have their slot empty: nothing to do); on a target that keeps the loop (Learn & Play:
// its edits widen it) the point is `none` and never runs.
#define STR6 SEVEN_STOCK  // the seventh string's index
static int g_line;        // string7_line: the section string mask has the bits of strings 6/7 (edit group seven_line)

static uint32_t f32bits(float v) {
  uint32_t b;
  memcpy(&b, &v, sizeof b);
  return b;
}

// SH7: the slide-to heads of a chord (strings 0-5 drawn): those of strings 6/7, when the mask slides (0x800) and not
// unpitched (0x400000), at the note's time + sustain, to its slideTo (rs7string's SH1/SH2: the loop to 7, 8 since
// beta.4)
static void on_sh7(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  const uint8_t *notes = PTR(f, "notes"), *note = PTR(f, "note");
  if (!notes || !note) return;
  for (int s = STR6; s < seven_chord_native(); s++) {
    const uint32_t m = RC_LD(uint32_t, FIELD(notes, seven_c8_mask) + 4 * s);
    if (!(m & 0x800) || (m & 0x400000)) continue;
    const float at = RC_LD(float, FIELD(note, sng_note_time)) + RC_LD(float, FIELD(note, sng_note_sustain));
    const uint64_t a[] = {SEVEN_GET(f, "obj"), SEVEN_GET(f, "arg"), (uint32_t)SEVEN_GET(f, "time_arg"), (uint64_t)s,
                          RC_LD(uint8_t, FIELD(notes, seven_c8_slide_to) + s), f32bits(at)};
    rc_call_name_f("chord_slide_head", a, 6, 1u << 5);
  }
}

// PL7: the chord pluck (strings 0-5 plucked): strings 6/7 when the chord frets them, as the game plucks the others (fret
// 0 plucks with 1; the GuitarStrings call checks the guitar's strings itself)
static void on_pl7(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on) return;
  const uint8_t *session = PTR(f, "session"), *tpl = PTR(f, "tpl");
  const uint8_t *t8 = tpl ? seven_chords_as_t8(tpl) : NULL;
  const uint8_t *guitar = ptr_at(session, RC_OFF(session_guitar));
  const uint8_t *strings = ptr_at(guitar, RC_OFF(guitar_strings));
  if (!strings) return;
  for (int s = STR6; s < seven_chord_native(); s++) {
    const int fret = t8 ? seven_chords_t8_fret(t8, s) : -1;
    if (fret < 0) continue;
    OV_GUEST(guitar_strings_pluck, (uint64_t)(uintptr_t)strings, (uint64_t)s, (uint32_t)fret,
             (uint32_t)(fret ? fret : 1));
  }
}

// SM7: the section string mask (strings 0-5 marked): the bits of strings 6/7 when the chord frets them (and
// string7_line is on). The records here are the song's own (the post-processing reads its vector): their T8 through the
// chords part.
static void on_sm7(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on || !g_line) return;
  const uint8_t *records = PTR(f, "records");
  if (!records) return;
  const uint8_t *t8 = seven_chords_t8_of(records + (size_t)SEVEN_GET(f, "index9") * 8);
  if (!t8) return;
  for (int s = STR6; s < seven_chord_native(); s++)
    if (seven_chords_t8_fret(t8, s) >= 0)
      OV_GUEST(section_string_bit, SEVEN_GET(f, "mask"), (uint32_t)SEVEN_GET(f, "phrase"), (uint32_t)s);
}

// ---- the finger markers on the neck (rs7string's publish: PUBFA, PUBGF, PUBBC), 1.0.0-beta.6
// The game's neck marks a finger of a chord with a pill (a "barre" over several strings): the marker loop takes the
// first string in slot order that has the finger, the span function counts the strings from the finger's lowest to
// its highest, the marker's barre clip is seeked to (span - 1) / 5 of its length, and the barre runs up from the anchor
// string. With a seventh string the slot order is not the neck's (string 6 lies beyond string 0: seven_barre.h), so
//   seven_pubfa   the marker function's entry: the finger's anchor becomes its lowest string (a barre over the low B and
//                 E was drawn from the E, one string too high)
//   seven_pubgf   the span function: the span over the template's physical ranks, string 6 counted (Learn & Play only:
//                 the Mac's unrolled copy is its own edit, FG5M in edits.map). Before, the low B was left out of a barre
//   seven_pubbc_scale / seven_pubbc_setscale   when a marker's span changed, the barre capsules' scale: the clip ends at
//                 five guitar gaps, a barre of seven strings is six, so the capsule is scaled up by 6 / 5 (Learn &
//                 Play: an edit calls Entity_SetScale with the bits this callback leaves in ecx; the Mac: this callback
//                 calls it). The gap is the guitar's (its spacing_guitar7 / 8). rs7string also scales the clip's seek by
//                 the gap ratio; that is the game's own code here, left as it is, so a spaced guitar's barre of up to six
//                 strings keeps the stock length
// All of it runs only when the game has every point (seven_mid_optional): an older RS Re-ARMed keeps the stock markers.
#define GUITAR_GAP 1.608f  // the game's string gap (rs7string's kGuitarGap); the barre clip ends at five of them
static int g_barre;       // every point of the fix is taken

// The guitar of a finger marker's owner (the neck's marker manager: its player at seven_marker_player), else NULL
static const uint8_t *marker_guitar(const uint8_t *owner) {
  const uint8_t *player = ptr_at(owner, RC_OFF(seven_marker_player));
  return ptr_at(player, RC_OFF(session_guitar));
}

// PUBFA: eax / r15 = the string the marker loop found, template = its T8 (the neck's marker function is only called
// from that loop). A 7- or 8-slot template: the finger's lowest string
static void on_pubfa(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on || !g_barre) return;
  const uint8_t *t8 = seven_chords_as_t8(PTR(f, "template"));
  if (!t8) return;
  const unsigned n = (unsigned)seven_chords_t8_slots(t8), s = (unsigned)SEVEN_GET(f, "string") & 0xff;
  if (n != 7 && n != 8) return;
  SEVEN_SET(f, "string", seven_barre_anchor((const uint8_t *)FIELD(t8, seven_t8_fingers), n, s));
}

// PUBGF (Learn & Play): the span function's loop has run over the template's six fingers; lo / hi = its lowest and
// highest string (edx, ecx), and `span = hi - lo + 1`, `hi != 0` follow. For a T8 of seven or eight slots they are
// the physical ranks over all of them
static void on_pubgf(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on || !g_barre) return;
  const uint8_t *t8 = seven_chords_as_t8(PTR(f, "template"));
  if (!t8) return;
  const unsigned n = (unsigned)seven_chords_t8_slots(t8);
  if (n != 7 && n != 8) return;
  unsigned lo, hi;
  seven_barre_span((const uint8_t *)FIELD(t8, seven_t8_fingers), n, (unsigned)SEVEN_GET(f, "finger") & 0xff, &lo, &hi);
  SEVEN_SET(f, "lo", lo);
  SEVEN_SET(f, "hi", hi);
}

// The scale of a marker's capsules for a barre of `span` strings on its owner's guitar, as the float's bits; 0 for a
// guitar that is not built with 7 or 8 strings (or a marker with no capsules): the game's own
static uint32_t barre_scale_bits(rc_mid_frame *f) {
  if (!seven_on || !g_barre || !SEVEN_GET(f, "entity1") || !SEVEN_GET(f, "entity2")) return 0;
  const uint8_t *guitar = marker_guitar(PTR(f, "owner"));
  if (!seven_count(guitar)) return 0;
  // the guitar's own gap: the stock one times its spacing (spacing_guitar7 / 8; 1 without)
  const float k = seven_barre_entity_scale((int32_t)SEVEN_GET(f, "span"), GUITAR_GAP * seven_spacing_k(guitar), GUITAR_GAP);
  return f32bits(k);
}

// PUBBC (Learn & Play): the edit after this point calls Entity_SetScale(capsule, scale) twice unless scale_bits is 0
static void on_pubbc_scale(rc_mid_frame *f, void *user) {
  (void)user;
  SEVEN_SET(f, "scale_bits", barre_scale_bits(f));
}

// PUBBC (the Mac): the game's Entity_SetScale(entity, 1, scale) on both capsules
static void on_pubbc_setscale(rc_mid_frame *f, void *user) {
  (void)user;
  const uint32_t bits = barre_scale_bits(f);
  if (!bits) return;
  const uint64_t e[] = {SEVEN_GET(f, "entity1"), SEVEN_GET(f, "entity2")};
  for (int i = 0; i < 2; i++) {
    const uint64_t a[3] = {e[i], 1, bits};
    rc_call_name_f("entity_set_scale", a, 3, 1u << 2);
  }
}

int seven_chdraw_init(void) {
  g_line = seven_settings_get()->string7_line;
  seven_edits_after("seven_line", &g_line);  // the section mask's bits of strings 6/7 (N12/N13/SM), at start like upstream's
  if (seven_mid("seven_b1", on_b1) || seven_mid("seven_b2", on_b2) || seven_mid("seven_sh7", on_sh7) ||
      seven_mid("seven_pl7", on_pl7) || seven_mid("seven_sm7", on_sm7))
    return -1;
  // the finger markers' barres: all four, or none of it (a point this RS Re-ARMed lacks leaves the game's markers)
  const char *what = "barre markers over all strings";
  const int a = seven_mid_optional("seven_pubfa", on_pubfa, what);
  const int b = seven_mid_optional("seven_pubgf", on_pubgf, what);
  const int c = seven_mid_optional("seven_pubbc_scale", on_pubbc_scale, what);
  const int d = seven_mid_optional("seven_pubbc_setscale", on_pubbc_setscale, what);
  g_barre = a && b && c && d && RC_HAS_OFF(seven_marker_player);
  seven_edits_after("seven_barre", &g_barre);
  return 0;
}
