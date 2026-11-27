// Seven-String Guitar (seven.h): what the strings part shares with the others.
//
// The string count is a property of the guitar's build (rs7string's C-lite): a guitar profile carries it (+0x38),
// the guitar built from it keeps it in its InstrumentInfo (+0x0b) for its whole life, and a song selection that asks
// for another count rebuilds player 0's guitar through the game's own rebuild. The song asks for seven strings when its
// arrangement's Tuning has the manifest key Rs7String6 (DotCharter's 7-string charts).
//
// Every GuitarStrings, InstrumentInfo and PegSet of the game has the native layout of eight (offsets.map, seven_*):
//   GuitarStrings  +0x40 main[8], +0x60 ghost[8] (the game's six at +0x4 / +0x1c stay as copies of the first six),
//                  +0x80 glow[8] (the renderer's glow ramp), +0xa0 tail, +0xa4 its guitar, +0xa8 its InstrumentInfo
//   InstrumentInfo +0x40 active strings, +0x41 loaded strings (both the build count, 4 to 8, or with string7_line
//                  off the stock count: seven_shown_count), +0x38 / +0x3c perStringMode of strings 6 / 7 (string 0's)
//   PegSet         slots[8] at +0xc, pegColor[8] at +0x14c, tail at +0x1cc
#ifndef RS_SEVEN_STRINGS_H
#define RS_SEVEN_STRINGS_H

#include <stdint.h>

#define SEVEN_STOCK 6     // the game's strings of a guitar
#define SEVEN_CAPACITY 8  // the native arrays

// The count the guitar (Rnd_Guitar, a host pointer) was built with, InstrumentInfo +0x0b: 4 to 6 (bass), 6 to 8; 0 for
// none. seven_strings_count is the same.
int seven_strings_guitar_count(const void *guitar);
int seven_strings_count(const void *guitar);
// The same for an InstrumentInfo
int seven_strings_info_count(const void *info);
// The strings the guitar of this InstrumentInfo shows (its active count, +0x40), and loads (+0x41), 0 for none
int seven_strings_active(const void *info);
int seven_strings_loaded(const void *info);

// The count the last song selection asked for of player 0's guitar: 7 (8 with Rs7String7 too) for an arrangement with
// Rs7String6, else 6. Player 0's next guitar profile is built with it (a mode entry, Session Mode, Guitarcade, Lessons,
// the technique hub, asks for 6). The bass's is seven_desired_bass (seven_model.h): 5 / 6 with Rs7String4 (/ 5).
int seven_strings_desired(void);

// ---- the pure rules (tests/test_seven_spacing.c)
// The guitar count the manifest keys ask for (Rs7String6, Rs7String7), given whether eight strings may be built and
// whether seven may: 6 without Rs7String6 (Rs7String7 alone is no chart), else 8, 7, or 6 when it may not be built
static inline int seven_wanted_guitar(int has6, int has7, int eight_ok, int seven_ok) {
  if (!has6 || !seven_ok) return 6;
  return has7 && eight_ok ? 8 : 7;
}
// The bass count (Rs7String4, Rs7String5): 4 unless an extended bass may be built, else 5, or 6 with Rs7String5
static inline int seven_wanted_bass(int has4, int has5, int bass_ok) {
  return bass_ok && has4 ? (has5 ? 6 : 5) : 4;
}
// An InstrumentInfo's own counts from its build count: 4 to 8 (a bass 4-6, a guitar 6-8); anything else the game's six
static inline int seven_build_count(int n) { return n == 0 || n < 4 ? SEVEN_STOCK : n > SEVEN_CAPACITY ? SEVEN_CAPACITY : n; }
// What a store of the target tuning is, by its caller (the return address of Play_Tuning_SetPlayerTargetTuning)
enum seven_role { SEVEN_ROLE_OTHER, SEVEN_ROLE_SONG, SEVEN_ROLE_TUNER, SEVEN_ROLE_MODE };
// rs7string's RebuildTarget: the count to rebuild a player's instrument with, 0 for none. Song selection: the
// arrangement's count; the tuning list's tuner: the picked tuning's, unless a song runs; a mode entry: back to stock
// (`stock` 6 for a guitar, 4 for a bass). Players 0 and 1 (local co-op), never another one, never while a player tuner
// holds a guitar.
static inline int seven_rebuild_target_for(enum seven_role r, unsigned player, int want, int was, int tuners, int song,
                                           int stock) {
  if (player > 1 || tuners) return 0;
  if (r == SEVEN_ROLE_SONG) return want != was ? want : 0;
  if (r == SEVEN_ROLE_TUNER) return !song && want != was ? want : 0;
  if (r == SEVEN_ROLE_MODE && want == stock && was != stock) return stock;
  return 0;
}
static inline int seven_rebuild_target(enum seven_role r, unsigned player, int want, int was, int tuners, int song) {
  return seven_rebuild_target_for(r, player, want, was, tuners, song, SEVEN_STOCK);
}

// rs7string's NormaliseSlot1: a mode entry stores player 0's tuning only, so player 1's guitar of 7 or 8 strings (or bass
// of 5 or 6) built for a co-op song goes back to the stock count with it. `wide`: player 1's instrument is built or
// wanted with more than the stock strings; never while a player tuner holds a guitar.
static inline int seven_slot1_normalise(enum seven_role r, unsigned player, int wide, int tuners) {
  return r == SEVEN_ROLE_MODE && player == 0 && wide && !tuners;
}

// The strings an InstrumentInfo loads and shows (its +0x41 / +0x40) of the build count n: the build count, but the
// strings past the stock count (6 of a guitar, 4 of a bass) are not loaded or shown while string7_line is off (rs7string's
// N1 / N21 pool requests and its culling of the added strings). The build count (+0x0b) stays: the lanes, the notes
// and the detection keep the extended instrument; only its string line is gone.
static inline int seven_shown_count(int n, int bass, int line) {
  const int c = seven_build_count(n), stock = bass ? 4 : SEVEN_STOCK;
  return line || c <= stock ? c : stock;
}

// The edge string of an instrument built with n strings (TU8A's StringAction asks the game's 0x1367b30 for it): the
// physical edge on string 0's side (`first`) or the far one. Stock counts as the game's: 0 and n - 1. A guitar of 7/8:
// its last string (beyond string 0) and string 5. An extended bass: its low B (slot 4, beyond string 0) and its G
// (slot 3) or, with six, its high C (slot 5).
static inline int seven_edge_string(int n, int bass, int first) {
  if (bass && (n == 5 || n == 6)) return first ? 4 : n == 6 ? 5 : 3;
  if (n > 6) return first ? n - 1 : 5;
  return first ? 0 : n - 1;
}

// The end of the added strings (from 6) whose models a guitar built with `built` strings and loading `loaded`
// (seven_shown_count's) has to have before its fret grid's cells 6/7 are filled: the ones it loads. A guitar whose
// added strings are not drawn (string7_line off) has none to wait for, and its lanes and detection keep its count.
static inline int seven_extra_models_end(int built, int loaded) {
  const int n = built < loaded ? built : loaded;
  return n > SEVEN_CAPACITY ? SEVEN_CAPACITY : n < SEVEN_STOCK ? SEVEN_STOCK : n;
}

// String s (0-7) of a GuitarStrings (host pointer), its ghost when ghost: a host pointer, NULL for none
uint8_t *seven_strings_string(const void *strings, int s, int ghost);
// The main string object s (0-7) of a guitar (Rnd_Guitar), NULL for none (rs7string's Child(gs, s, false))
void *seven_strings_child(void *guitar, int s);
// 0 when the strings past six the guitar loads (its loaded count: string 6 of a guitar built with 7; none on a guitar
// built with 6, a bass, or with string7_line off) and their ghosts exist with their models loaded; else the first failing check, as
// rs7string's ExtraWhy numbers them (1 no GuitarStrings, 3 no InstrumentInfo, 4 not the guitar's, 8 + (s - 6) * 6 +
// ghost * 3 + {0 no object, 1 no entity, 2 not loaded}). Only strings below the loaded count are loaded here (not all
// eight, as rs7string's): string 7 of a 7-string guitar never loads.
int seven_strings_extra_why(void *guitar);
// 1 when the InstrumentInfo is a bass's (its +0x0, the profile's instrument class, 2)
int seven_strings_info_is_bass(const void *info);
// The attach point of string 6 or 7 (vec3 to out, the caller's in the game's memory), from strings 0 and 1 by the
// game's own query; 1 when written (rs7string's AttachExt)
int seven_strings_attach_ext(void *info, unsigned string, float *out);

// LT1 (the tuner's peg letters): the in-line byte of the guitar of this InstrumentInfo (+0x0a), or 1 on a 3+3 frame
// that shows the 7-string in-line headstock model (seven_headstock_letter_inline)
int seven_strings_letter_inline(const void *info);

#endif
