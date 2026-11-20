// The splicing pitch shifter (splice_shift.h).
//
// The input goes into a ring. The output is read `lag` samples behind the input, and lag grows by (1 - ratio) a sample
// for a downward shift (the read point moves slower than the write point), or shrinks for an upward one. A jump
// changes lag by `by` samples and crossfades from the old read point to the new one.
//
// Which jump (repeat): the ring is also kept at a quarter of the rate. The newest LOOK samples are compared with the
// LOOK samples `by` earlier, for every `by` that is allowed (their correlation over the root of their powers: 1 is the
// same sound). A repeat is a `by` that is alike enough (`good`), or, when none is, nearly as alike as the best. That is
// a period for a note, and for a chord a stretch after which all its notes come round together. Where exactly: the
// repeat is looked for again at the read point, sample by sample around the coarse one, and placed between samples
// (place).
//
// When (downward): every seam costs a chord something, since a real chord never repeats exactly, so there are as few
// as can be. With `patient` the lag is left to grow to `most`, and the jump is then the longest repeat that fits in
// it; without, a jump is made as soon as the lag holds one repeat, the shortest. Upward the lag runs out by itself,
// and the jump is the shortest repeat. A sound whose best repeat within `most` is a poor one (under FAR_UNDER) and
// that repeats clearly better (by FAR_GAIN) a little later has its lag let grow on, to `far` at most: two low notes
// a third apart come round together only after 40 to 60 ms, and until then every jump puts one of them out of step.
//
// A close repeat (downward): a sound that comes round cleanly (CLEAN) within `close`, which is a single note from
// about F3 up, or two that share so short a period, is not made to wait. Up there the long jump is the wrong one. A
// real string's overtones are not whole multiples of its first: on a recorded guitar the second is 9 to 13 cents
// sharp of twice the first from A#3 up. Over the 25 ms of a long jump a note at 880 Hz has then
// had its overtones slide half a turn against each other, and no place fits, where over one period they slide a
// hundredth of a turn; and a pitch that moves, a bend or a vibrato, is somewhere else after 25 ms. So the lag is
// taken back one repeat at a time, as soon as it holds one: found in the coarse ring (close_repeat), placed at the
// read point over a few periods, and looked for again, sample by sample, when the lag has grown by it. Its crossfade
// is a straight line that lasts as long as the lag takes to grow by the repeat (CLOSE_FADE at most), so one seam
// runs into the next: an overtone a little off the repeat then comes out turning evenly, which is a pitch a hair
// off, and not in steps. Such a note's lag while it rings is a period or two. A sound stops being taken so when its
// two places are no longer alike enough, as when another note joins it, and at every start.
// It is for a sound that is alone. Whatever does not come round with the repeat, a quieter note under this one, a
// string ringing along, is not shifted by such jumps at all: it comes out at its own pitch, chopped as often as there
// are seams, and through an amplifier's distortion that is a second, wrong note under the played one. Two places 0.99
// alike still leave room for a sound 20 dB down. So the model of the ringing sound is asked as well (beside): only
// when every overtone it has found, down to 45 dB under the loudest, sits at a whole multiple of the repeat or a
// string's own little way off it, is the sound taken so, and each new model is asked again. Until there is a model,
// 70 ms into a note, the lag grows as it would anyway.
//
// A note's start jumps a downward shift to ATTACK_LAG whatever the sound: the old read point holds the tail of what
// rang before, the new one the start, and the start covers the seam. That is what keeps a long lag from being felt:
// what is late is only the ringing on of notes already heard. Two things count as a start:
//   * a pick over ringing notes: the high end (the difference from sample to sample) rising to RISE times its own
//     average of the last 50 ms, its peak held for 30 ms. A pick is a burst of high end;
//   * a note that is new in its own range, however soft its pick and whatever else rings: the guitar is split in
//     ranges, and one of them is `louder` times its loudest of the stretch from 45 to 4 ms before (from 90 to as
//     much as 18 ms before for a narrow range, which is slower to come up itself), or several
//     are a little less: what each is over SOME times that loudest, as a share, is summed, and the sum has to reach
//     what one range at `louder` gives. A note over a ringing chord raises several ranges a little, since its
//     overtones fall on the chord's. Notes that only ring get quieter. Two notes beating against each other go quiet
//     and come back, but as slowly as they went: further back than the quiet they were louder still, and just
//     before now they were nearly as loud as now, so they never pass. The ranges are BROAD wide ones (plain one-pole splits, which cost no time: a note
//     after a quiet moment is found at once) and NARROW of a sixth of an octave from 150 Hz up (resonators, a few
//     milliseconds slow: they tell a note hammered on from the one it replaces a tone away, which no wide range does).
// A rise too small for that, `soft` or more, may be a soft note under louder ones: the lag is then shortened by an
// ordinary jump, the longest repeat that fits, which costs no more than any seam if it was nothing.
// For REST samples after a start the high end's average follows its peak up, so the rest of the same start is not
// taken for another, and no other jump is made: the lag is short then anyway, and the look back still holds what was
// before the start. Starts are looked for under a downward shift alone, where the pause counts down: it ends when the
// shift becomes another.
// Noise is kept from counting as a start by two levels: a range under RANGE_LEAST is never rising, a high end under
// PICK_LEAST never a pick. Those are for a guitar that peaks at LOUD or over. One that comes in quieter has them
// lowered by as much as it is quieter (down to QUIETEST of them), or its hammer-ons and pull-offs would never reach
// them and come out as late as the lag. How loud the guitar is: its own peak, let fall over FALL seconds of playing
// and not at all while it is silent, since a pause says nothing of how loud the guitar is.
#include "splice_shift.h"

#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define RING 16384          // samples, a power of two
#define SLOW 4              // the coarse ring's step
#define CHECK 64            // samples between two looks for a jump
#define LEAST_LAG 6.0       // the read needs two samples either side
#define ATTACK_LAG 24.0     // where a start starts over: half a millisecond back, which holds its first rise
#define LEAST_JUMP 40       // 0.8 ms at 48 kHz: no guitar note repeats sooner
#define NEARLY 0.03         // with no repeat alike enough, how far under the best one still counts
#define FADE 256            // a seam: 5 ms, however alike its two places are (twice that for less alike ones made the
                            // player's own takes 2 dB worse through an amplifier and nothing better)
#define FADE_ATTACK 48
#define REST 1920           // samples after a start before the next counts
#define FAR_UNDER 0.9       // a best repeat less alike than this may be passed over for a longer one,
#define FAR_GAIN 0.08       // that is this much more alike
#define RISE 2.0
#define EARLY 4             // looks between two tries for a clean repeat to take early, or for a close one
#define CLEAN 0.99          // how alike a close repeat's two places are to be: a hundredth of the power may differ
#define ALONE 2.5e-5        // and how much of the ringing sound it may get wrong (beside): 46 dB down
#define COARSE 0.12         // and how much less alike than that in the coarse ring, which has it to 4 samples only
#define CLOSE_ROOM 16.0     // samples the lag keeps under a close repeat: the read's own, and the search around it
#define CLOSE_FADE 0.025    // seconds: the longest seam of a close repeat,
#define CLOSE_FADE_LEAST 32 // and the shortest, in samples
#define SOFT_LAG 384        // samples: a lag under this is not shortened for what may be a soft note
#define SOFT_WAIT 0.003     // seconds before it is: the first of a pick's rise looks like one, and is a start by then
#define PATCH 0.002         // seconds over which a step in the output is let out (below, at the output)
#define BROAD 5
#define NARROW 30
#define RANGES (BROAD + NARROW)
#define SOME 1.15           // a range rises when it is this many times its loudest of the stretch before
#define RANGE_LEAST 2e-3    // and over this,
#define PICK_LEAST 3e-4     // and the high end is a pick over this: both for a guitar that peaks at
#define LOUD 0.5            // this or over, and as much lower as a quieter guitar's peak is, down to
#define QUIETEST 0.01       // this share of them
#define FALL 10.0           // seconds of playing over which the guitar's peak is let fall,
#define HEARD 0.01          // playing being what is over this share of it
#define PAST 90             // ms of loudest-per-millisecond kept per range
#define PAST_BROAD 45       // of them, what a broad range looks back over
#define NEAR 4              // the newest ms that don't count, being the start's own rise; more for a narrow range
#define FFT_N 16384         // the spectrum the overtones are found in
#define SPECTRUM 8192       // of the newest samples, at most this many go in it (170 ms: tells 10 Hz apart)
#define FIT 4096            // the overtones are fitted over this many samples, at most,
#define FIT_LEAST 1536      // and with less than this of steady sound to fit there is no model
#define SETTLE 480          // samples after a start that are not steady sound yet
#define TONES 48            // overtones a model has, at most
#define REFRESH 4800        // samples after which a model is made again

// The ringing sound's overtones
typedef struct {
  int n, m;                 // how many, and the samples a fit takes: every second one, so over twice that many
  double w[TONES], lev[TONES];  // radians a sample, and the level
  double cw[TONES], sw[TONES];  // e^(2iw): an overtone's step from one fitted sample to the next
  float taper[FIT / 2];     // a fit's window
  double gram[2 * TONES][2 * TONES];  // a fit's matrix, factored
} model;

// The making of a model, a piece at a time (model_step): what comes next
enum { NONE, TURN, PEAKS, MATRIX, FACTOR, FIRST, FIRST_ON, SECOND, SECOND_ON };

struct sp {
  double rate, ratio, set;
  _Atomic double semitones;
  float ring[RING], slow[RING / SLOW];
  unsigned at;              // samples written
  double sum4;              // the coarse ring's sample being gathered
  double lag, old;          // the read point's lag, and the one being faded from
  int fade, fade_len;       // samples of the fade left, and its length
  int check, rest;
  float past[RANGES][PAST]; // per range: the loudest sample of each of the last milliseconds, a ring
  int past_at, ms, ms_len;  // the next place in the rings, samples into the millisecond under way, and a millisecond's
  double ms_peak[RANGES], quiet[RANGES];  // that millisecond's loudest so far, and the loudest of PAST to PAST_NEAR ms ago
  double hold[RANGES], hold_k;  // the range's level now: its peak, let fall over 3 ms
  double over[RANGES];      // the level from which a range counts as rising
  int near[RANGES];         // the newest ms that don't count for this range
  double split[BROAD - 1], split_k[BROAD - 1];  // the low-passes between the broad ranges, and their coefficients
  double res[NARROW][5];    // a narrow range's resonator: b0, a1, a2, and its last two outputs
  double in1, in2;          // the last two input samples
  double before;            // the last input sample
  double fast, base;        // the high end's peak level, and its average over the last while
  double loud, loud_k;      // the guitar's own peak, and what it falls by a sample while the guitar plays
  double fast_k, base_k;    // what the peak falls by a sample, and the average's share of the newest
  double most, far, good, louder, soft, nearly;
  int soon;                 // a jump is wanted at the next look
  int eager, early;         // clean repeats are taken as soon as they fit, and the looks since that was last tried
  double close, period;     // the longest close repeat, and the one the sound has now (0: none), both in samples
  int seek, span;           // the looks since a close repeat was last looked for, and the samples `place` compares
  int close_fade, even;     // CLOSE_FADE in samples, and whether the fade under way is a straight line
  int alone;                // the number of the model the close repeat in use was last held against
  int look, attack, patient;
  long jumps, poor, attacks;
  double jumped, scores;    // the jumps' lengths and likenesses, summed
  long since;               // samples since the last start
  model model[2];           // of the ringing sound: the one in use, and the one being made
  int active, ready;        // which is in use, and whether it is there at all
  int stage, gen;           // how far the making is, and the number of the one in use
  long made, tried;         // `since` when it was made, and when a making was last begun
  unsigned q;               // the ring's sample the one being made is about
  int spectrum;             // the samples its spectrum is of
  double first[2 * TONES];  // its first fit
  int turn, row;            // how far its spectrum's transform has got (the next span), and its fit's matrix (the next row)
  double fo[TONES], fq[TONES], fa[TONES], fb[TONES];  // a fit of it that is half done: fit_sums' or_, oi, a and b
  double fr[FFT_N], fi[FFT_N];
  float straight[RING / SLOW / 2 + 16];  // the coarse ring's newest stretch, laid out straight (likeness)
  double old2, away, away_step;  // a read point that was being faded from when a jump came: its lag, its share of
                            // what is faded from now, and that share's step down a sample
  int cut;                  // a third read point has just been taken away (a jump inside a fade inside a fade)
  double patch, y1, y2;     // the step that left in the output, which is let out over PATCH; the last two samples
  int patch_left, patch_len, soft_wait;
};

void sp_defaults(sp_params *p) {
  p->most_ms = 30;
  p->far_ms = 62;
  p->good = 0.97;
  p->look_ms = 32;
  p->attack = 1;
  p->louder = 1.5;
  p->nearly = 0.08;
  p->soft = 0.15;
  p->patient = 1;
  p->eager = 0;
  p->close_ms = 6;
}

sp *sp_new(double rate, const sp_params *params) {
  sp_params d;
  if (!params) {
    sp_defaults(&d);
    params = &d;
  }
  sp *s = calloc(1, sizeof *s);
  if (!s) return NULL;
  s->rate = rate;
  s->ratio = 1;
  s->most = fmin(params->most_ms * 1e-3 * rate, RING / 4);
  s->far = fmin(params->far_ms * 1e-3 * rate, RING / 4 - 8);
  s->good = params->good;
  s->look = (int)(params->look_ms * 1e-3 * rate / SLOW);
  if (s->look > RING / SLOW / 4) s->look = RING / SLOW / 4;
  s->attack = params->attack;
  s->louder = params->louder;
  s->nearly = params->nearly;
  s->soft = params->soft > 0 ? params->soft : 1e9;
  s->patient = params->patient;
  s->eager = params->eager;
  s->close = params->close_ms * 1e-3 * rate;
  s->close_fade = (int)(CLOSE_FADE * rate);
  s->span = s->look * SLOW < 768 ? s->look * SLOW : 768;
  s->lag = LEAST_LAG;
  s->patch_len = (int)(PATCH * rate), s->soft_wait = (int)(SOFT_WAIT * rate);
  s->tried = -REFRESH;
  s->ms_len = (int)(rate / 1000);
  s->hold_k = exp(-1 / (0.003 * rate));
  s->fast_k = exp(-1 / (0.03 * rate)), s->base_k = 1 - exp(-1 / (0.05 * rate));
  s->loud_k = exp(-1 / (FALL * rate));
  for (int k = 0; k < RANGES; k++) {
    // A resonator of a sixth of an octave takes about 8.65 / (pi f) seconds to come up, and that much more of the
    // newest is its own rise. No more than a fifth of the look back, or slow beating would pass.
    double slow = k < BROAD ? 0 : 8650 / (M_PI * 150 * pow(2, (k - BROAD) / 6.0));
    s->near[k] = NEAR + (int)fmin(slow, PAST / 5 - NEAR);
    s->over[k] = RANGE_LEAST * QUIETEST;
  }
  for (int k = 0; k < BROAD - 1; k++) s->split_k[k] = 1 - exp(-2 * M_PI * (double[]){2500, 1000, 400, 160}[k] / rate);
  for (int k = 0; k < NARROW; k++) {
    double w = 2 * M_PI * 150 * pow(2, k / 6.0) / rate, alpha = sin(w) * sinh(M_LN2 / 2 / 6 * w / sin(w));
    s->res[k][0] = alpha / (1 + alpha), s->res[k][1] = -2 * cos(w) / (1 + alpha), s->res[k][2] = (1 - alpha) / (1 + alpha);
  }
  return s;
}

void sp_free(sp *s) { free(s); }
void sp_set_semitones(sp *s, double semitones) { atomic_store_explicit(&s->semitones, semitones, memory_order_relaxed); }
double sp_delay(const sp *s) { return s->lag; }
void sp_counts(const sp *s, long *jumps, long *poor, long *attacks, double *jumped, double *scores) {
  *jumps = s->jumps, *poor = s->poor, *attacks = s->attacks, *jumped = s->jumped, *scores = s->scores;
}

// The ring `lag` samples behind the newest, between samples by a cubic through the four around
static double read_at(const sp *s, double lag) {
  double p = (double)s->at - 1 - lag;
  double f = floor(p), t = p - f;
  unsigned i = (unsigned)(long long)f;
  double a = s->ring[(i - 1) & (RING - 1)], b = s->ring[i & (RING - 1)], c = s->ring[(i + 1) & (RING - 1)], d = s->ring[(i + 2) & (RING - 1)];
  return b + 0.5 * t * (c - a + t * (2 * a - 5 * b + 4 * c - d + t * (3 * (b - c) + d - a)));
}

// How alike the `n` samples before `from` and before `to` are (places counted back from the newest), in `ring` of
// `mask` + 1 samples
static double alike(const float *ring, unsigned mask, unsigned newest, int from, int to, int n) {
  double dot = 0, a = 0, b = 0;
  for (int j = 0; j < n; j++) {
    double x = ring[(newest - (unsigned)(from + j)) & mask], y = ring[(newest - (unsigned)(to + j)) & mask];
    dot += x * y, a += x * x, b += y * y;
  }
  return a > 1e-12 && b > 1e-12 ? dot / sqrt(a * b) : 0;
}

// How alike the newest `look` coarse samples are to those `by` coarse samples earlier, for by = lo..hi, into
// score[lo..hi]. The ring's newest stretch is laid out straight first, newest first, so the sums run over plain arrays.
static void likeness(sp *s, int lo, int hi, double *score) {
  int n = s->look, need = n + hi;
  float *v = s->straight;
  unsigned newest = s->at / SLOW - 1;
  for (int j = 0; j < need; j++) v[j] = s->slow[(newest - (unsigned)j) & (RING / SLOW - 1)];
  double own = 0, other = 0;
  for (int j = 0; j < n; j++) own += (double)v[j] * v[j], other += (double)v[j + lo] * v[j + lo];
  for (int by = lo; by <= hi; by++) {
    const float *u = v + by;
    float d0 = 0, d1 = 0, d2 = 0, d3 = 0;  // four sums side by side: the compiler makes one wide sum of them
    int j = 0;
    for (; j + 4 <= n; j += 4) d0 += v[j] * u[j], d1 += v[j + 1] * u[j + 1], d2 += v[j + 2] * u[j + 2], d3 += v[j + 3] * u[j + 3];
    for (; j < n; j++) d0 += v[j] * u[j];
    score[by] = own > 1e-12 && other > 1e-12 ? (double)(d0 + d1 + d2 + d3) / sqrt(own * other) : 0;
    other += (double)v[by + n] * v[by + n] - (double)v[by] * v[by];  // the other stretch moves on by one
  }
}

// A repeat of the sound, in samples (a multiple of SLOW), no longer than `limit`: the longest there is, or the
// shortest. 0 in silence, or when there is none that short. *alike_best, if given: how alike the best of them is.
static int repeat(sp *s, double limit, int longest, double *alike_best) {
  enum { MOST = RING / SLOW / 4 };
  double score[MOST + 2], best = -1;
  int lo = LEAST_JUMP / SLOW, hi = (int)(fmin(fmax(s->most, s->far), limit) / SLOW);
  if (hi > MOST) hi = MOST;
  if (hi < lo) return 0;
  likeness(s, lo, hi + 1, score);
  // A sound is also like itself a moment ago, the more the lower it is: that is not a repeat. The search starts
  // where the likeness has stopped falling from there.
  while (lo < hi && score[lo + 1] <= score[lo]) lo++;
  for (int by = lo; by <= hi; by++)
    if (score[by] > best) best = score[by];
  if (alike_best) *alike_best = best;
  if (best <= 0) return 0;
  double need = !longest && best >= s->good ? s->good : best - (longest ? s->nearly : NEARLY);
  if (longest) {
    for (int by = hi; by >= lo; by--)
      if (score[by] >= need) {
        while (by > lo && score[by - 1] > score[by]) by--;  // back to the top of this rise
        return by * SLOW;
      }
  } else
    for (int by = lo; by <= hi; by++)
      if (score[by] >= need) {
        while (by < hi && score[by + 1] > score[by]) by++;  // on to the top of this rise
        return by * SLOW;
      }
  return 0;
}

// How alike the best repeat longer than `from` samples and no longer than `far` is
static double beyond(sp *s, double from) {
  enum { MOST = RING / SLOW / 4 };
  double score[MOST + 2], best = 0;
  int lo = (int)(from / SLOW) + 1, hi = (int)(s->far / SLOW);
  if (hi >= MOST) hi = MOST - 1;
  if (hi < lo) return 0;
  likeness(s, lo, hi, score);
  for (int by = lo; by <= hi; by++) best = fmax(best, score[by]);
  return best;
}

// How much of the ringing sound, as the model has it, a close repeat of `period` samples would get wrong: the power
// of each overtone by how far it is from a whole multiple of the repeat, against all of them. An overtone a little
// off (a string's own, a fiftieth of a turn a repeat) comes out turning evenly and costs nothing to speak of; one
// half way between two multiples, which is another sound's, comes out at another pitch altogether, and counts half
// (what seams that run into each other make of it, against what it should be): so the fourth power of the sine.
static double beside(const model *md, double period) {
  double all = 1e-30, off = 0;
  for (int i = 0; i < md->n; i++) {
    double h = md->w[i] * period / (2 * M_PI), d = sin(M_PI * (h - round(h))), p = md->lev[i] * md->lev[i];
    all += p, off += p * d * d * d * d / 2;
  }
  return off / all;
}

// A close repeat: the shortest stretch, no longer than `limit` samples, after which the coarse ring has the sound
// come round nearly cleanly; the read point then says how cleanly, and where exactly (place). 0 when there is none,
// or when the likeness is still rising at the limit: its top is then past it.
static int close_repeat(sp *s, double limit) {
  enum { MOST = RING / SLOW / 4 };
  double score[MOST + 2];
  int lo = LEAST_JUMP / SLOW, hi = (int)(limit / SLOW);
  if (hi > MOST) hi = MOST;
  if (hi <= lo) return 0;
  likeness(s, lo, hi + 1, score);
  while (lo < hi && score[lo + 1] <= score[lo]) lo++;  // as in repeat
  for (int by = lo; by <= hi; by++)
    if (score[by] >= CLEAN - COARSE) {
      while (by <= hi && score[by + 1] > score[by]) by++;
      return by <= hi ? by * SLOW : 0;
    }
  return 0;
}

// The jump of about `near` samples from the read point, toward the input (down, 1) or away from it, that lands on
// the most alike place, going by the `n` samples before each: how far, to a fraction of a sample. *score: how alike.
static double place(const sp *s, int down, int near, int n, double *score) {
  enum { SPAN = SLOW + 2 };
  int here = (int)s->lag, top = 0;
  double v[2 * SPAN + 3];
  for (int k = -SPAN - 1; k <= SPAN + 1; k++) {
    int by = near + k;
    v[k + SPAN + 1] = by < 1 || (down && here - by < 3) ? -2 : alike(s->ring, RING - 1, s->at - 1, here, down ? here - by : here + by, n);
  }
  for (int k = -SPAN; k <= SPAN; k++)
    if (v[k + SPAN + 1] > v[top + SPAN + 1]) top = k;
  double a = v[top + SPAN], b = v[top + SPAN + 1], c = v[top + SPAN + 2], by = near + top;
  if (a > -1 && c > -1 && b >= a && b >= c && a - 2 * b + c < -1e-9) by += 0.5 * (a - c) / (a - 2 * b + c);
  *score = b;
  return by;
}

// ------------------------------------------------------------------------------------------------
// The model of the ringing sound, which the close repeat asks whether a note is alone (beside): its overtones, found
// in a spectrum of up to 170 ms of it, and their levels fitted all at once to the newest samples (least squares:
// overtones too close for a spectrum to tell apart are still told apart by a fit that knows where they are), each
// one's frequency set right from how far its phase comes between two fits. It is made in the background once a note
// has rung long enough, a small piece of the work every 64 samples (model_step), again when a longer fit has become
// possible and then every 0.1 s. All of it comes from sound already in the ring: nothing waits for it.
//
// A seam is a crossfade and nothing else. The model was made for more: every overtone it has was carried across a
// seam by itself, its level kept and its phase turned from the old place's to the new one's over 85 ms, so that none
// dipped in the crossfade. A chord measured steadier so, in the shifter's own sound. But for those 85 ms the
// overtones are out of step with one another, and an amplifier's distortion makes its sound out of the waveform:
// through a driven tone that was the further from the true thing (the recording played back slower, through the same
// tone) by 3 to 12 dB on every kind of playing, and was heard in the game as artifacts in chords and a noise at each
// pick. A plain seam leaves the waveform the guitar's own but for its 5 ms. pitch/README.md has the figures, and
// the commit before the one that took it out has the code.
// ------------------------------------------------------------------------------------------------
// A transform in two parts, so that it need not be done in one go: the samples put in the order it wants them, and
// then its passes, `passes` of them from the span `len` on. Returns the next span; the transform is done when that
// is over n.
static void fft_order(double *re, double *im, int n) {
  for (int i = 1, j = 0; i < n; i++) {
    int bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      double t = re[i];
      re[i] = re[j], re[j] = t;
      t = im[i], im[i] = im[j], im[j] = t;
    }
  }
}

static int fft_passes(double *re, double *im, int n, int len, int passes) {
  for (; len <= n && passes > 0; len <<= 1, passes--) {
    double ang = -2 * M_PI / len, wr = cos(ang), wi = sin(ang);
    for (int i = 0; i < n; i += len) {
      double cr = 1, ci = 0;
      for (int j = 0; j < len / 2; j++) {
        double ur = re[i + j], ui = im[i + j];
        double vr = re[i + j + len / 2] * cr - im[i + j + len / 2] * ci, vi = re[i + j + len / 2] * ci + im[i + j + len / 2] * cr;
        re[i + j] = ur + vr, im[i + j] = ui + vi;
        re[i + j + len / 2] = ur - vr, im[i + j + len / 2] = ui - vi;
        double t = cr * wr - ci * wi;
        ci = cr * wi + ci * wr, cr = t;
      }
    }
  }
  return len;
}

// The sum over a fit's window of e^(i t k): three sums of a plain run of e^(i t k), each of which has a closed form
static void taper_sum(double t, int m, double *re, double *im) {
  *re = *im = 0;
  for (int k = -1; k <= 1; k++) {
    double th = t + k * 2 * M_PI / m, half = th / 2, run = fabs(sin(half)) < 1e-9 ? m : sin(m * half) / sin(half);
    double ph = th * (m - 1) / 2 + k * M_PI / m, share = k ? -0.25 : 0.5;
    *re += share * run * cos(ph), *im += share * run * sin(ph);
  }
}

// The model's overtones where the ring's sample `last` is: the samples up to it, under the window, fitted with all of
// them at once. Every second sample goes in (md->m of them, so twice that far back): the overtones are all under
// 5 kHz, and nothing a guitar has above 12 kHz is loud enough to pass for one of them. c[2i], c[2i + 1]: overtone i
// there is c[2i] cos(w k) + c[2i + 1] sin(w k), k samples before `last`, so its level is their hypot and its phase at
// `last` their atan2.
//
// In parts, for a model in the making, which is in no hurry: the sums over the samples (fit_sums, over the `from`-th
// to the `to`-th fitted sample before `last`, begun with or_ 1 and the rest 0), and then the overtones from the sums
// (fit_solve).
static void fit_sums(const sp *s, const model *md, unsigned last, int from, int to, double *or_, double *oi, double *a, double *b) {
  int n = md->n;
  for (int k = from; k < to; k++) {
    double x = md->taper[k] * s->ring[(last - (unsigned)(2 * k)) & (RING - 1)];
    for (int i = 0; i < n; i++) {  // the overtones don't depend on each other: the compiler does several at a time
      double t = or_[i] * md->cw[i] - oi[i] * md->sw[i];
      a[i] += x * or_[i], b[i] += x * oi[i];
      oi[i] = or_[i] * md->sw[i] + oi[i] * md->cw[i], or_[i] = t;
    }
  }
}

static void fit_solve(const model *md, const double *a, const double *b, double *c) {
  int n = md->n;
  for (int i = 0; i < n; i++) c[2 * i] = a[i], c[2 * i + 1] = b[i];
  // The matrix is L times its transpose: first L, then the transpose
  for (int i = 0; i < 2 * n; i++) {
    for (int k = 0; k < i; k++) c[i] -= md->gram[i][k] * c[k];
    c[i] /= md->gram[i][i];
  }
  for (int i = 2 * n - 1; i >= 0; i--) {
    for (int k = i + 1; k < 2 * n; k++) c[i] -= md->gram[k][i] * c[k];
    c[i] /= md->gram[i][i];
  }
}

// The fit's matrix for the model's overtones, in two parts: its rows from overtone `from` on, as many as hold about
// `pairs` pairs of overtones (the pairs are the work: returns the overtone to go on from, md->n when done), and then
// the whole of it factored in place (0 if the overtones can't be told apart).
static int fit_rows(model *md, int from, int pairs) {
  int m = md->m, n = md->n, i = from;
  for (; i < n && pairs > 0; pairs -= i + 1, i++)
    for (int j = 0; j <= i; j++) {
      double dr, di, ar, ai;
      taper_sum(2 * (md->w[i] - md->w[j]), m, &dr, &di);
      taper_sum(2 * (md->w[i] + md->w[j]), m, &ar, &ai);
      md->gram[2 * i][2 * j] = (dr + ar) / 2;          // cos with cos
      md->gram[2 * i + 1][2 * j + 1] = (dr - ar) / 2;  // sin with sin
      md->gram[2 * i + 1][2 * j] = (ai + di) / 2;      // sin i with cos j
      if (j < i) md->gram[2 * i][2 * j + 1] = (ai - di) / 2;  // cos i with sin j
    }
  return i;
}

static int fit_factor(model *md) {
  int n = md->n;
  double ridge = 1e-6 * md->gram[0][0];
  for (int i = 0; i < 2 * n; i++)
    for (int j = 0; j <= i; j++) {
      double v = md->gram[i][j] + (i == j ? ridge : 0);
      for (int k = 0; k < j; k++) v -= md->gram[i][k] * md->gram[j][k];
      if (i == j) {
        if (v <= 0) return 0;
        md->gram[i][i] = sqrt(v);
      } else
        md->gram[i][j] = v / md->gram[j][j];
    }
  return 1;
}

// The overtones of the spectrum in s->fr (powers), which is of `spectrum` samples: the peaks 45 dB under the loudest
// or less, loudest first. One that sits in a much louder one's skirt is the window's ripple and not an overtone.
static void peaks(sp *s, model *md, int spectrum) {
  double *re = s->fr, top = 0, bin = (double)FFT_N / spectrum;
  int low = (int)(70 * FFT_N / s->rate), high = (int)(5000 * FFT_N / s->rate), found[TONES * 4], finds = 0;
  md->n = 0;
  for (int k = low; k <= high; k++) top = fmax(top, re[k]);
  for (int k = low; k <= high && finds < TONES * 4; k++)
    if (re[k] > re[k - 1] && re[k] >= re[k + 1] && re[k] > 3e-5 * top) found[finds++] = k;
  for (int i = 1; i < finds; i++)
    for (int j = i; j > 0 && re[found[j]] > re[found[j - 1]]; j--) {
      int t = found[j];
      found[j] = found[j - 1], found[j - 1] = t;
    }
  for (int i = 0; i < finds && md->n < TONES; i++) {
    int k = found[i], ripple = 0;
    for (int j = 0; j < i; j++) ripple |= abs(found[j] - k) < 3.5 * bin && re[found[j]] > 250 * re[k];
    if (ripple) continue;
    double a = log(re[k - 1] + 1e-300), b = log(re[k]), c = log(re[k + 1] + 1e-300);
    md->w[md->n++] = 2 * M_PI * (k + 0.5 * (a - c) / (a - 2 * b + c)) / FFT_N;
  }
}

// One piece of the work of making a model, if one is due. The pieces are small, a tenth of a millisecond or so each:
// the work is done where the sound is, and a block of 64 samples has 1.3 ms for everything that plays the guitar.
// Done in one go, the spectrum alone took half a millisecond and the matrix of a full chord as much again, ten times
// a second.
#define PASSES 3   // of the spectrum's transform to a piece
#define PAIRS 160  // pairs of overtones of the fit's matrix to a piece
static void model_step(sp *s) {
  model *md = &s->model[!s->active];
  double c[2 * TONES];
  switch (s->stage) {
    case NONE: {
      // Two fits half a fit apart have to lie in the steady sound: a fit is two thirds of it at most
      int span = (int)fmin(FIT, (s->since - SETTLE) * 2 / 3) & ~255;  // in the ring's samples
      int due = !s->ready || (2 * s->model[s->active].m < FIT && span >= FIT) || s->since - s->made >= REFRESH;
      if (span < FIT_LEAST || !due || s->since - s->tried < REFRESH / 8) return;
      s->tried = s->since;
      md->m = span / 2;
      s->q = s->at - 1;
      s->spectrum = (int)fmin(SPECTRUM, s->since - SETTLE);
      // The samples under their window (its cosine by turning, a step a sample), in the order the transform wants
      double step = 2 * M_PI / s->spectrum, wr = cos(step), wi = sin(step), cr = cos(step / 2), ci = sin(step / 2);
      for (int i = 0; i < FFT_N; i++) {
        double x = i < s->spectrum ? s->ring[(s->q + 1 - (unsigned)s->spectrum + (unsigned)i) & (RING - 1)] : 0, t = cr * wr - ci * wi;
        s->fr[i] = x * (0.5 - 0.5 * cr), s->fi[i] = 0;
        ci = cr * wi + ci * wr, cr = t;
      }
      fft_order(s->fr, s->fi, FFT_N);
      s->turn = 2, s->stage = TURN;
      return;
    }
    case TURN:
      s->turn = fft_passes(s->fr, s->fi, FFT_N, s->turn, PASSES);
      if (s->turn <= FFT_N) return;
      for (int k = 0; k < FFT_N / 4; k++) s->fr[k] = s->fr[k] * s->fr[k] + s->fi[k] * s->fi[k];
      s->stage = PEAKS;
      return;
    case PEAKS: {
      peaks(s, md, s->spectrum);
      double step = 2 * M_PI / md->m, wr = cos(step), wi = sin(step), cr = cos(step / 2), ci = sin(step / 2);
      for (int k = 0; k < md->m; k++) {
        double t = cr * wr - ci * wi;
        md->taper[k] = (float)(0.5 - 0.5 * cr);
        ci = cr * wi + ci * wr, cr = t;
      }
      for (int i = 0; i < md->n; i++) md->cw[i] = cos(2 * md->w[i]), md->sw[i] = sin(2 * md->w[i]);
      s->row = 0, s->stage = md->n ? MATRIX : NONE;
      return;
    }
    case MATRIX:
      s->row = fit_rows(md, s->row, PAIRS);
      if (s->row >= md->n) s->stage = FACTOR;
      return;
    case FACTOR:
      s->stage = fit_factor(md) ? FIRST : NONE;
      return;
    case FIRST:
    case SECOND:  // half a fit earlier
      for (int i = 0; i < md->n; i++) s->fo[i] = 1, s->fq[i] = s->fa[i] = s->fb[i] = 0;
      fit_sums(s, md, s->stage == FIRST ? s->q : s->q - (unsigned)md->m, 0, md->m / 2, s->fo, s->fq, s->fa, s->fb);
      s->stage++;
      return;
    case FIRST_ON:
      fit_sums(s, md, s->q, md->m / 2, md->m, s->fo, s->fq, s->fa, s->fb);
      fit_solve(md, s->fa, s->fb, s->first);
      s->stage = SECOND;
      return;
    case SECOND_ON:
      fit_sums(s, md, s->q - (unsigned)md->m, md->m / 2, md->m, s->fo, s->fq, s->fa, s->fb);
      fit_solve(md, s->fa, s->fb, c);
      for (int i = 0; i < md->n; i++) {
        double here = hypot(s->first[2 * i], s->first[2 * i + 1]), back = hypot(c[2 * i], c[2 * i + 1]);
        // The frequency, from how far the phase has come in half a fit against what the spectrum said. A fit with a
        // frequency a little off gives the phase of its window's middle carried on to its end at that frequency: put
        // right here.
        double off = remainder(atan2(s->first[2 * i + 1], s->first[2 * i]) - atan2(c[2 * i + 1], c[2 * i]) - md->w[i] * md->m, 2 * M_PI) / md->m;
        // An overtone that is not about as loud in both places is not ringing steadily, and what its phase did
        // between them says nothing of its frequency
        int steady = here > 1e-6 && back > 0.6 * here && back < 1.6 * here && fabs(off) * md->m < 2.5;
        md->lev[i] = here;
        md->w[i] += steady ? off : 0;
      }
      s->active = !s->active, s->ready = 1, s->gen++, s->made = s->since, s->stage = NONE;
      return;
  }
}

// The new read point's share of the output's next sample: 1 with no fade under way
static double fade_gain(const sp *s) {
  if (!s->fade) return 1;
  double g = (double)(s->fade_len - s->fade + 1) / (s->fade_len + 1);
  return s->even ? g : 0.5 - 0.5 * cos(M_PI * g);
}

// A jump does not wait for a fade under way to end: a start takes the read point at once. The read point that fade
// was coming from is not dropped for it, which would be a step in the output as large as what it still gave (a
// tenth of full scale has been seen, at ordinary picks): it goes on fading out, beside the one the fade was going
// to, for as long as the new fade lasts.
static void jump(sp *s, double to, int fade) {
  if (s->fade) {
    if (s->away > 0) s->cut = 1;  // a third one: let out as a step is, at the output
    s->old2 = s->old, s->away = 1 - fade_gain(s), s->away_step = s->away / fade;
  } else
    s->away = 0;
  s->old = s->lag;
  s->lag = to;
  s->fade = s->fade_len = fade;
  s->even = 0;
}

// A close repeat's jump: by as many of the repeat as the lag holds, or fewer, by halves, until the two places are
// alike enough. 0 when not even one is: the sound has changed, and the repeat is forgotten.
static int close_seam(sp *s, double drift) {
  int n = (int)fmin(fmax(4 * s->period, s->span / 4), s->span);  // a few periods: a pitch on the move is still one
  int most = (int)((s->lag - CLOSE_ROOM) / s->period);
  if (most < 1) return 0;  // not yet
  for (int m = most; m >= 1; m /= 2) {
    double score, far = place(s, 1, (int)lround(m * s->period), n, &score);
    if (score < CLEAN || s->lag - far < LEAST_LAG + 2) continue;
    double after = s->lag - far;
    s->period = far / m;
    // The seam lasts as long as the lag takes to grow by one repeat again, so that the next one follows on; shorter
    // while there is more than one repeat left to take back
    int fade = after - CLOSE_ROOM >= s->period ? (int)(2 * s->period) : (int)(s->period / drift);
    fade = fade > s->close_fade ? s->close_fade : fade < CLOSE_FADE_LEAST ? CLOSE_FADE_LEAST : fade;
    jump(s, after, fade);
    s->even = 1;
    s->jumps++, s->jumped += far, s->scores += score;
    return 1;
  }
  s->period = 0;
  return 0;
}

// 2 when this sample starts a note, 1 when it may (a small rise)
static int starts(sp *s, double x) {
  // How loud the guitar is, and so how much of the two levels noise is kept under applies
  double size = fabs(x);
  if (size > s->loud)
    s->loud = size;
  else if (size > HEARD * s->loud)
    s->loud *= s->loud_k;
  double level = fmin(1, fmax(QUIETEST, s->loud / LOUD));
  s->fast = fmax(fabs(x - s->before), s->fast * s->fast_k);
  s->before = x;
  // The ranges: what each low-pass lets through less what the next one does; the first is the top, the last the bottom
  double rest = x, rise = 0;
  for (int k = 0; k < RANGES; k++) {
    double part = rest;
    if (k < BROAD - 1) {
      s->split[k] += s->split_k[k] * (x - s->split[k]);
      part = rest - s->split[k];
      rest = s->split[k];
    } else if (k >= BROAD) {
      double *r = s->res[k - BROAD];
      part = r[0] * (x - s->in2) - r[1] * r[3] - r[2] * r[4];
      r[4] = r[3], r[3] = part;
    }
    s->ms_peak[k] = fmax(s->ms_peak[k], fabs(part));
    s->hold[k] = fmax(fabs(part), s->hold[k] * s->hold_k);
    if (s->hold[k] > s->over[k]) rise += s->hold[k] / s->over[k] - 1;
  }
  int fresh = rise >= s->louder / SOME - 1;
  s->in2 = s->in1, s->in1 = x;
  if (++s->ms >= s->ms_len) {
    for (int k = 0; k < RANGES; k++) {
      s->past[k][s->past_at] = (float)s->ms_peak[k];
      s->ms_peak[k] = s->quiet[k] = 0;
      for (int j = s->near[k] - 1; j < (k < BROAD ? PAST_BROAD : PAST); j++) s->quiet[k] = fmax(s->quiet[k], s->past[k][(s->past_at + PAST - j) % PAST]);
      s->over[k] = SOME * s->quiet[k] + RANGE_LEAST * level;
    }
    s->past_at = (s->past_at + 1) % PAST;
    s->ms = 0;
  }
  int start = !s->rest && (s->fast > RISE * s->base + PICK_LEAST * level || fresh);
  s->base += s->base_k * (s->fast - s->base);
  if (s->rest && s->rest-- && s->base < s->fast) s->base = s->fast;
  if (start) s->rest = REST;
  return start ? 2 : !s->rest && rise >= s->soft;
}

void sp_process(sp *s, const float *in, float *out, int frames) {
  double st = atomic_load_explicit(&s->semitones, memory_order_relaxed);
  if (st != s->set) {
    s->set = st;
    s->ratio = pow(2, st / 12);
    // A start's pause counts down where starts are looked for, under a downward shift alone: one left over from there
    // would keep any other shift from ever jumping
    if (!(s->ratio < 1)) s->rest = 0, s->soon = 0;
    s->period = 0;
  }
  int down = s->ratio < 1;
  double drift = 1 - s->ratio;
  for (int f = 0; f < frames; f++) {
    double x = in[f];
    s->ring[s->at & (RING - 1)] = (float)x;
    s->sum4 += x;
    s->at++;
    if (s->at % SLOW == 0) {
      s->slow[(s->at / SLOW - 1) & (RING / SLOW - 1)] = (float)(s->sum4 / SLOW);
      s->sum4 = 0;
    }
    if (s->ratio == 1) {  // nothing to shift: the lag is let run out, and then the input is copied
      if (s->lag > 0 && !s->fade) jump(s, 0, FADE);
    } else {
      if (s->close > 0 && (s->at & 63) == 0) model_step(s);  // for the close repeat (beside)
      int start = s->attack && down ? starts(s, x) : 0;
      // What may be a soft note under louder ones is not worth a seam where the read point stands; it is worth an
      // ordinary jump to a repeat a little early, if the lag is long enough to be heard on a note
      // The first of a pick's rise is such a small rise too: the jump waits SOFT_WAIT, by when a pick has become a
      // start, which takes the lag back itself (a jump made first would be cut off by it mid-fade).
      if (start == 1 && s->patient && s->lag >= SOFT_LAG && !s->soon) s->soon = 1, s->check = CHECK - s->soft_wait;
      if (start == 2) {
        // What rang before fades out under the start's first millisecond, a chord as a single note
        if (s->lag > ATTACK_LAG + 24) jump(s, ATTACK_LAG, FADE_ATTACK), s->attacks++;
        s->since = 0, s->ready = 0, s->stage = NONE, s->tried = -REFRESH;
        s->soon = 0, s->period = 0;
      } else if (s->since++, s->fade || s->rest) {
      } else if (s->period > 0 && s->alone != s->gen && (s->alone = s->gen, beside(&s->model[s->active], s->period) > ALONE)) {
        s->period = 0;  // a new model of the ringing sound has something in it that does not come round with the repeat
      } else if (s->period > 0 && s->lag >= s->period + CLOSE_ROOM) {
        close_seam(s, drift);  // a close repeat: looked at every sample, so one seam follows on the last
      } else if (++s->check >= CHECK) {
        s->check = 0;
        double room = s->lag - LEAST_LAG - 2, score = 0, far = 0;
        if (down && s->patient) {
          if (s->lag >= s->most || s->soon) {
            double here = 0;
            int by = repeat(s, room, 1, &here);
            // A sound with no good repeat this short and a clearly better one a little longer (two low notes a
            // third apart come round together only after 40 to 60 ms): the lag is let grow to that one
            if (by && !s->soon && s->lag < s->far && here < FAR_UNDER && beyond(s, room) >= here + FAR_GAIN) by = 0, here = -1;
            s->soon = 0;
            if (by)
              far = place(s, 1, by, s->span, &score);
            else if (here < 0)
              ;
            else if (s->lag >= s->most + 4 * CHECK)
              jump(s, LEAST_LAG, FADE);  // nothing found in time: silence
          } else if (s->close > 0 && !s->period && s->ready && room - 8 >= LEAST_JUMP && ++s->seek >= EARLY) {
            // A sound that comes round cleanly within a few milliseconds, a note high up, and that the model of the
            // ringing sound shows to be all there is: taken a repeat at a time from here on (close_seam). Half of
            // what the coarse ring found, if that is a repeat too.
            s->seek = 0;
            int by = close_repeat(s, fmin(room - 8, s->close));
            double alike = 0, at = by ? place(s, 1, by, s->span, &alike) : 0, half = 0;
            if (alike >= CLEAN && at / 2 >= LEAST_JUMP) half = place(s, 1, (int)lround(at / 2), s->span, &score);
            if (alike >= CLEAN && half > 0 && score >= CLEAN) at = half;
            if (alike >= CLEAN && beside(&s->model[s->active], at) <= ALONE) s->period = at, s->alone = s->gen, close_seam(s, drift);
            score = 0;
          } else if (s->eager && room >= LEAST_JUMP && ++s->early >= EARLY) {
            // A sound that repeats cleanly and soon, a single note or a power chord, loses nothing by a seam: it
            // is not made to wait, and its lag stays within one repeat. Looked for every few looks only.
            s->early = 0;
            int by = repeat(s, room, 0, &score);
            if (by && score >= s->good) far = place(s, 1, by, s->span, &score);
            if (score < s->good) far = 0;
          }
        } else if (down && room >= LEAST_JUMP) {
          int by = repeat(s, s->most, 0, NULL);
          // A lag that holds two of them or more (a high note, or after a long seam): the longest it holds
          if (by && room >= 2 * by) by = repeat(s, room, 1, NULL);
          if (by && room >= by)
            far = place(s, 1, by, s->span, &score);
          else if (!by)
            jump(s, LEAST_LAG, FADE);  // silence: nothing to keep in step
        } else if (!down && s->lag < LEAST_LAG + (CHECK + 2 * FADE) * -drift + 4) {
          // The lag must not run out before the next look, or under a fade
          int by = repeat(s, s->most, 0, NULL);
          if (by) far = place(s, 0, by, s->span, &score);
        }
        if (far > 0) {
          jump(s, down ? s->lag - far : s->lag + far, FADE);
          s->jumps++, s->poor += score < s->good, s->jumped += far, s->scores += score;
        }
      }
      s->lag += drift;
      if (s->lag < 2) s->lag = 2;
    }
    double y = s->lag > 0 ? read_at(s, s->lag) : x, g = 1;
    if (s->fade) {
      s->old += drift;
      if (s->old < 2) s->old = 2;
      g = (double)(s->fade_len - s->fade + 1) / (s->fade_len + 1);
      if (!s->even) g = 0.5 - 0.5 * cos(M_PI * g);
      double from = read_at(s, s->old);
      if (s->away > 0) {  // and the read point the fade before this one was coming from (jump)
        s->old2 += drift;
        if (s->old2 < 2) s->old2 = 2;
        from += s->away * (read_at(s, s->old2) - from);
        s->away -= s->away_step;
      }
      y = g * y + (1 - g) * from;
      if (!--s->fade) s->away = 0;
    }
    // A step all the same: a jump inside a fade inside a fade leaves a third read point, which is dropped. What is
    // missing against where the last two samples were heading is put back, and let out over PATCH.
    if (s->cut) s->cut = 0, s->patch = 2 * s->y1 - s->y2 - y, s->patch_left = s->patch_len;
    if (s->patch_left) y += s->patch * (0.5 - 0.5 * cos(M_PI * s->patch_left-- / s->patch_len));
    s->y2 = s->y1, s->y1 = y;
    out[f] = (float)y;
  }
}
