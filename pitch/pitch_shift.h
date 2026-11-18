// A pitch shifter for a live guitar: a few semitones down or up, chords included, with a delay that is a few
// milliseconds for the high notes and the pick, and longer only where the low strings need it. For Drop Pedal: the
// guitar comes out as if it were tuned lower, before the game or a tone host hears it. An experiment so far: pitch/README.md
// has what it does and what the bench (pitch/bench) measures.
//
// How: the guitar is split into narrow bands (a row of resonators, a few tens of Hz wide for the low strings and a
// semitone wide higher up). In a band there is mostly one overtone of one string, also in a chord, and a band that
// holds one sine can be moved in frequency without a seam: its signal is turned (multiplied by a slowly rotating
// phase) by the frequency it has to move, (ratio - 1) times its own. Bands that hold the same overtone turn together,
// led by the loudest of them, so the overtone stays in one piece. Nothing is cut and spliced, and no delay line is read:
// the delay is the filters' own.
//
// No allocation, lock or system call in ps_process. One instance is one mono stream.
#ifndef RS_PITCH_SHIFT_H
#define RS_PITCH_SHIFT_H

typedef struct ps ps;

typedef struct {
  double low, high;    // Hz: the first band's centre, and where the bands end
  double min_width;    // Hz: a band's width on the low strings. Narrower tells two low notes apart and costs delay
  double per_octave;   // bands an octave where they are wider than min_width
  int poles;           // 1-4: a band filter's order
  double width;        // a band filter's cutoff, in band widths: wider is faster and tells less apart
  double lock_ms;      // a band takes this long to turn with the loudest band of its overtone
  double track;        // the frequency estimate's smoothing, in the band filter's own time constants
  int at_zero;         // 1: 0 semitones runs the bands too (the bench's check of the bands alone); 0: copies
} ps_params;

void ps_defaults(ps_params *p);
// rate in Hz. NULL params: the defaults. NULL when out of memory.
ps *ps_new(double rate, const ps_params *params);
void ps_free(ps *s);
// From any thread; takes effect at the next ps_process
void ps_set_semitones(ps *s, double semitones);
// out may be in
void ps_process(ps *s, const float *in, float *out, int frames);
int ps_bands(const ps *s);

#endif
