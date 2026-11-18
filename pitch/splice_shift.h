// A pitch shifter for a live guitar, the second design (pitch_shift.h is the first; pitch/README.md compares them).
// The guitar is read back from a short memory a little slower than it comes in, which lowers it, and the growing lag
// is taken back in jumps: each jump skips ahead by a stretch after which the sound repeats, so the seam is heard as
// little as can be. What is its own:
//
//   * a repeat is looked for over a long look back (32 ms), so a chord's own repeat is found, not only one string's;
//   * seams are few: the lag is let grow to `most_ms` and then taken back by the longest repeat that fits, since
//     every seam costs a chord something;
//   * a note's start takes the lag back to almost nothing at once (downward shifts only), so what is felt as
//     lateness, the start of a note, comes about a millisecond and a half late whatever was ringing before.
//
// No allocation, lock or system call in sp_process. One instance is one mono stream.
#ifndef RS_SPLICE_SHIFT_H
#define RS_SPLICE_SHIFT_H

typedef struct sp sp;

typedef struct {
  double most_ms;    // the lag is not let past this,
  double far_ms;     // except, up to this, for a sound that repeats clearly better a little later. 0: never
  double good;       // 0-1: how alike the sound must be across a jump to count as a clean repeat
  double look_ms;    // how far back two places are compared
  int attack;        // 1: a note's start takes the lag back
  double louder;     // a note is new in its range when it is this many times the loudest of the 45 ms before
  double soft;       // the summed rise (splice_shift.c) from which a soft note is suspected and the lag shortened
                     // by an ordinary jump; 0: never
  int eager;         // 1: with `patient`, a sound that repeats cleanly and soon (a single note, a power chord) still
                     // jumps as soon as its lag holds one repeat
  double nearly;     // how much less alike than the best repeat the longest one taken may be
  int patient;       // 1: jump only when the lag has reached most_ms. 0: as soon as it holds one repeat, which keeps
                     // the lag of ringing notes short and makes two to three times the seams
  int mend;          // 1: at a seam the overtones are carried across each by itself (splice_shift.c, mend)
  double mend_ms;    // how long an overtone is given to turn its phase across the seam,
  double start_mend_ms;  // and across the seam at a note's start, for what rang before it; 0: that seam is not mended
} sp_params;

void sp_defaults(sp_params *p);
// rate in Hz. NULL params: the defaults. NULL when out of memory.
sp *sp_new(double rate, const sp_params *params);
void sp_free(sp *s);
// From any thread; takes effect at the next sp_process
void sp_set_semitones(sp *s, double semitones);
// out may be in
void sp_process(sp *s, const float *in, float *out, int frames);
// The lag right now, in frames
double sp_delay(const sp *s);
// Counts since the start, for the bench: jumps to a repeat, those of them whose two places were less alike than
// `good`, starts that took the lag back, and the jumps' lengths (frames) and likenesses summed
void sp_counts(const sp *s, long *jumps, long *poor, long *attacks, double *jumped, double *scores);

#endif
