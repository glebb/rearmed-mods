// Seven-String Guitar (seven.h): the bass part, the game side of extended bass (seven_bass.h; rs7string's src/features/bass
// and the bass parts of its pitch, tuner and headstock features, github.com/SomeeName/rs7string, MIT, by SomeeName).
//
// What lives here: the points that exist for a bass alone and the switch that says the fix is on. The rest of it is in
// the parts that own the code around it: the song's setup per detector, the detector's open and lowest notes and the
// expected pitch (seven_pitch.c: S4, PT1, PR0, S1, I1, and the edits PE0 of seven_string), the tuner's order and
// names (seven_tuner.c, seven_names.c), the strings part's 5/6-string build.
//   seven_pbh     the low B's bend, held over the pitch tracker's dropouts (rs7string's PBH, after the single-note bend
//                 read in Rnd_Note_UpdateInProgress); optional beside the rest
//   seven_psn3    the tuner's per-peg loop calls the named-colour dispatch for each peg: a bass peg without a seat (the
//                 stock 4-peg headstock, no model of ours) has no slot, which the dispatch would write through
//   seven_lt0     the tuner's peg letters read the record of each peg's slot: an empty one takes the nearest seated slot's
//   seven_s1_bass, seven_s1b_bass (the Mac's): the expected pitch of bass strings 4 / 5 in the Mac's 0x10017aa60 /
//                 0x10017a990, whose bass branch has no join the pitch part's seven_s1 sits at
//   the tuner's table of bass open notes (28 33 38 43 0 0 in the game's data): [4] and [5] made 23 and 48 here, once, so the
//   tuner's targets, peg letters and tolerance of those strings are right
// SEVEN_F_BASS is on when every point and the table were taken. Without it the callbacks that read it stay out of the
// way (a point registered cannot be unregistered), and the guitar and the 4-string bass are as the game has them. The
// bend hold has its own line in the log when its point is missing and does not switch the fix off.
// Game thread only (the bend holds).
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "seven.h"

#define PTR(f, name) ((uint8_t *)(uintptr_t)SEVEN_GET(f, name))
#define FIELD(p, name) ((uint8_t *)(p) + RC_OFF(name))

// seven_chords.c (the chords part): the instrument of the song's RS7X block, SEVEN_GUITAR (0) or SEVEN_BASS (1), -1 for a
// song without a valid block. Weak: a chords part without it leaves extended bass off.
extern int seven_chords_block_instrument(const void *song) __attribute__((weak));

// ---- the log: at most this many lines of the bass a run
static int g_logs = 80;
static void blog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void blog(const char *fmt, ...) {
  if (g_logs <= 0) return;
  g_logs--;
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  rsm_log("seven-string: bass: %s", buf);
}

// The song's setup of a detector (seven_bass.h; the pitch part's S4 calls it)
unsigned seven_bass_on_song(const void *det, const void *tuning, int det_instrument) {
  if (!seven_feature(SEVEN_F_BASS) || !det) return 0;
  const uint8_t *song = tuning ? (const uint8_t *)tuning - RC_OFF(sng_song_tuning) : NULL;
  unsigned strings = 0;
  if (det_instrument == SEVEN_DET_BASS && song && seven_chords_block_instrument) {
    const int block = seven_chords_block_strings(song);
    strings = seven_bass_song_strings(block > 0, seven_chords_block_instrument(song), block, det_instrument);
  }
  seven_bass_setup(det, det_instrument, (int)strings, (uint32_t)(rsm_now() * 1000.0));
  if (strings || det_instrument == SEVEN_DET_BASS)
    blog("song %p on detector %p: %s", (const void *)song, det,
         strings == 6   ? "6-string extended bass (low B, high C)"
         : strings == 5 ? "5-string extended bass (low B)"
                        : "stock 4-string bass");
  return strings;
}

// ---- PBH: the low B's bend held over the tracker's dropouts (at most 40 lines of the log)
static seven_bend_hold g_hold[2];  // per input: channel 0 and 2
static int g_bend_logged;

static void log_bend(const seven_bend_hold *h) {
  if (!h->valid || h->peak < 0.5f || g_bend_logged >= 40) return;
  g_bend_logged++;
  blog("low B bend held: %u reads, %u without a pitch (longest gap %.2f s), %u far too low; peak %.2f semitones", h->reads,
       h->missed, (double)h->longest_gap, h->low, (double)h->peak);
}

// The detection of an input id (the channel of a note: 0 or 2; Learn & Play has a function for it, play_get_detection; the
// Mac its Play_DetectInput's), a host pointer or NULL. It calls the game.
static uint8_t *detection_of_input(int channel) {
  if (channel != 0 && channel != 2) return NULL;
  if (RC_ADDR(play_get_detection)) return RC_HOST(OV_GUEST(play_get_detection, (uint32_t)channel));
  uint8_t *input = RC_HOST(OV_GUEST(play_detect_input, (uint32_t)channel));
  return input ? rc_mod_ptr(FIELD(input, detector_detection)) : NULL;
}

// After the single-note bend read: for string 4 (the low B) of an extended bass song on its own detector, the read is held
// by seven_bass_hold_bend; anything else is the game's. The game call's clobbered registers come back by themselves.
static void on_pbh(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on || !seven_feature(SEVEN_F_BASS)) return;
  const uint8_t *data = PTR(f, "data");
  if (!data || RC_LD(uint8_t, FIELD(data, sng_note_string)) != 4) return;
  const int channel = (int32_t)SEVEN_GET(f, "channel");
  if (channel != 0 && channel != 2) return;
  const uint8_t *det = detection_of_input(channel);
  if (!det || seven_bass_strings_of(det) < 5) return;
  float semis = (float)SEVEN_GETF(f, "semis");
  const float now = (float)SEVEN_GETF(f, "now");
  seven_bend_hold *hold = &g_hold[channel / 2];
  if (hold->note != data) log_bend(hold);
  const int ok = seven_bass_hold_bend(hold, data, (uint8_t)SEVEN_GET(f, "ok") != 0, &semis, now);
  SEVEN_SETF(f, "semis", semis);
  SEVEN_SET(f, "ok", ok ? 1 : 0);
}

// ---- PSN3: no call of the named-colour dispatch for a peg without a slot (its argument popped as its `ret 4` would, on
// Learn & Play whose thiscall pushed it)
static void on_psn3(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on || PTR(f, "slot")) return;
  static int sp = -2;
  if (sp == -2) sp = rc_guest_ptr_size == 4 ? rc_mid_value(f->point, "sp") : -1;
  if (sp >= 0) rc_mid_set(f, sp, rc_mid_get(f, sp) + (uint64_t)rc_guest_ptr_size);
  f->go = SEVEN_CONT(f, "skip");
}

// ---- LT0: the record an empty peg slot s of the tuner letters borrows: the first seated slot of the PegSet, from slot 3
// for string 5 (a bass's high C, next to the G) and from slot 0 otherwise (the low B next to the E); NULL when none is
// seated (rs7string's LetterSeat)
static uint8_t *letter_seat(uint8_t *pegs, int s) {
  const unsigned count = pegs ? RC_LD(uint16_t, FIELD(pegs, pegs_count)) : 0;
  const unsigned first = s == 5 ? 3 : 0;
  for (unsigned i = 0; i < count; i++) {
    uint8_t *slot = FIELD(pegs, pegs_slots) + (size_t)((first + i) % count) * (size_t)RC_OFF(peg_slot_size);
    if (RC_LD(uint8_t, slot)) return slot;
  }
  return NULL;
}

static void on_lt0(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on || SEVEN_GET(f, "slot")) return;
  uint8_t *seat = letter_seat(PTR(f, "pegs"), (int32_t)SEVEN_GET(f, "index") & 0xff);
  if (seat) SEVEN_SET(f, "slot", (uintptr_t)seat);
}

// ---- S1's bass branch on the Mac (Learn & Play's seven_s1 is its join): string 4 / 5 of an extended bass, pitch =
// the tuning's entry + the base, the fret added by the game after it
static void on_s1_bass(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on || !seven_feature(SEVEN_F_BASS)) return;
  const uint32_t s = (uint32_t)SEVEN_GET(f, "string");
  const int base = seven_bass_extra_base(s, (unsigned)seven_bass_max_strings());
  const uint8_t *tuning = PTR(f, "tuning");
  if (base == SEVEN_BASS_NO_BASE || !tuning) return;
  SEVEN_SET(f, "pitch", (uint32_t)(base + RC_LD(int16_t, tuning + 2 * s)));
  f->go = SEVEN_CONT(f, "done");
}

// ---- the tuner's table of bass open notes: [4] = 23, [5] = 48. Written once, only over the game's own values (28 33 38 43 0 0
// as far as they are there); 1 when the table has them now
static int write_table(void) {
  if (!RC_ADDR(tuner_bass_open_midi)) return 0;
  uint8_t *table = (uint8_t *)(uintptr_t)RC_GA(tuner_bass_open_midi);
  int32_t now[6];
  for (int i = 0; i < 6; i++) now[i] = RC_LD(int32_t, table + 4 * i);
  if (now[4] == seven_bass_open[4] && now[5] == seven_bass_open[5]) return 1;  // already (another instance)
  for (int i = 0; i < 4; i++)
    if (now[i] != seven_bass_open[i]) return 0;
  if (now[4] != 0 || now[5] != 0) return 0;
  RC_ST(int32_t, table + 16, seven_bass_open[4]);
  RC_ST(int32_t, table + 20, seven_bass_open[5]);
  return 1;
}

int seven_bass_init(void) {
  static const struct seven_point points[] = {
      {"seven_psn3", on_psn3, 0},
      {"seven_lt0", on_lt0, 0},
      // the Mac's own moments (none on Learn & Play)
      {"seven_s1_bass", on_s1_bass, 1},
      {"seven_s1b_bass", on_s1_bass, 1},
  };
  int ok = seven_mids_optional("extended bass", points, (int)(sizeof points / sizeof *points));
  if (ok && !seven_chords_block_instrument) {
    rsm_log("seven-string: extended bass off: the chords part does not read a bass block");
    ok = 0;
  }
  if (ok && !RC_HAS_OFF(sng_song_tuning)) ok = 0;
  if (ok && !write_table()) {
    rsm_log("seven-string: extended bass off: the tuner's table of bass open notes is not the game's (or not known on %s)",
            rc_target());
    ok = 0;
  }
  seven_feature_set(SEVEN_F_BASS, ok);
  // the bend hold is an addition of its own: a missing point is its line in the log, the bass stays on
  if (!seven_mid_optional("seven_pbh", on_pbh, "the low B's bend hold (rs7string PBH)") && ok)
    rsm_log("seven-string: extended bass on without the low B's bend hold");
  if (ok) rsm_log("seven-string: extended bass on");
  return 0;
}
