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
#define FADE 256            // a seam between two alike places; twice that between less alike ones
#define FADE_ATTACK 48
#define REST 1920           // samples after a start before the next counts
#define FAR_UNDER 0.9       // a best repeat less alike than this may be passed over for a longer one,
#define FAR_GAIN 0.08       // that is this much more alike
#define RISE 2.0
#define EARLY 4             // looks between two tries for a clean repeat to take early
#define SOFT_LAG 384        // samples: a lag under this is not shortened for what may be a soft note
#define BROAD 5
#define NARROW 30
#define RANGES (BROAD + NARROW)
#define SOME 1.15           // a range rises when it is this many times its loudest of the stretch before
#define PAST 90             // ms of loudest-per-millisecond kept per range
#define PAST_BROAD 45       // of them, what a broad range looks back over
#define NEAR 4              // the newest ms that don't count, being the start's own rise; more for a narrow range
#define FFT_N 16384         // the spectrum the overtones are found in
#define SPECTRUM 8192       // of the newest samples, at most this many go in it (170 ms: tells 10 Hz apart)
#define FIT 4096            // the overtones are fitted over this many samples, at most,
#define FIT_LEAST 1536      // and a seam with less than this of steady sound behind it is not mended
#define SETTLE 480          // samples after a start that are not steady sound yet
#define TONES 48            // overtones mended at a seam
#define MOVED 3.0           // Hz: an overtone this far from where the model has it is on the move, and not mended
#define REFRESH 4800        // samples after which a model is made again
#define MEND_MOST 8192      // samples: the longest mend

// The ringing sound's overtones
typedef struct {
  int n, m;                 // how many, and the samples a fit takes: every second one, so over twice that many
  unsigned ref;             // the ring's sample the phases are at
  double w[TONES], amp[TONES], ph[TONES];  // radians a sample, the level (0: not ringing steadily), the phase
  double cw[TONES], sw[TONES];  // e^(2iw): an overtone's step from one fitted sample to the next
  float taper[FIT / 2];     // a fit's window
  double gram[2 * TONES][2 * TONES];  // a fit's matrix, factored
} model;

// An overtone being carried across a seam
typedef struct {
  double a, was;            // its level at the new read point (0: left to the crossfade), and at the old one
  double lv, dv;            // the level it is given now, on its way from the one to the other, and its step a sample
  double cr, ci, sr, si;    // e^i(its phase at the new read point), and that number's step a sample
  double cphi, sphi;        // at the old read point it is this far behind
  double turn;              // the phase it starts out with against the new read point's, which the glide takes to 0
  double tr, ti, dr, di;    // e^i(what is left of that), and its step a sample for now
} glider;

enum { NONE, MATRIX, FIRST, SECOND };  // the making of a model: what comes next

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
  double fast_k, base_k;    // what the peak falls by a sample, and the average's share of the newest
  double most, far, good, louder, soft, nearly;
  double moved;             // radians a sample: an overtone this far from where the model has it is on the move
  int soon;                 // a jump is wanted at the next look
  int eager, early;         // clean repeats are taken as soon as they fit, and the looks since that was last tried
  int look, attack, patient;
  long jumps, poor, attacks;
  double jumped, scores;    // the jumps' lengths and likenesses, summed
  int mend, mend_len, start_len;  // seams are mended (below), and how long a mend lasts, at a jump and at a start
  long since;               // samples since the last start
  model model[2];           // of the ringing sound: the one in use, and the one being made
  int active, ready;        // which is in use, and whether it is there at all
  int stage, gen;           // how far the making is, and the number of the one in use
  long made, tried;         // `since` when it was made, and when a making was last begun
  unsigned q;               // the ring's sample the one being made is about
  int spectrum;             // the samples its spectrum is of
  double first[2 * TONES];  // its first fit
  double fr[FFT_N], fi[FFT_N];
  float straight[RING / SLOW / 2 + 16];  // the coarse ring's newest stretch, laid out straight (likeness)
  glider glide[TONES];
  int gliding, glide_len;   // samples of the glide under way that are left, and its length
  int glide_n, glide_gen;   // its overtones, and the number of the model they are of
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
  p->mend = 1;
  p->mend_ms = 85;
  p->start_mend_ms = 85;
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
  s->moved = 2 * M_PI * MOVED / rate;
  s->soft = params->soft > 0 ? params->soft : 1e9;
  s->patient = params->patient;
  s->eager = params->eager;
  s->mend = params->mend;
  s->mend_len = (int)fmin(params->mend_ms * 1e-3 * rate, MEND_MOST);
  s->start_len = (int)fmin(params->start_mend_ms * 1e-3 * rate, MEND_MOST);
  s->lag = LEAST_LAG;
  s->tried = -REFRESH;
  s->ms_len = (int)(rate / 1000);
  s->hold_k = exp(-1 / (0.003 * rate));
  s->fast_k = exp(-1 / (0.03 * rate)), s->base_k = 1 - exp(-1 / (0.05 * rate));
  for (int k = 0; k < RANGES; k++) {
    // A resonator of a sixth of an octave takes about 8.65 / (pi f) seconds to come up, and that much more of the
    // newest is its own rise. No more than a fifth of the look back, or slow beating would pass.
    double slow = k < BROAD ? 0 : 8650 / (M_PI * 150 * pow(2, (k - BROAD) / 6.0));
    s->near[k] = NEAR + (int)fmin(slow, PAST / 5 - NEAR);
    s->over[k] = 2e-3;
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

// The jump of about `near` samples from the read point, toward the input (down, 1) or away from it, that lands on
// the most alike place: how far, to a fraction of a sample. *score: how alike.
static double place(const sp *s, int down, int near, double *score) {
  enum { SPAN = SLOW + 2 };
  int here = (int)s->lag, n = s->look * SLOW < 768 ? s->look * SLOW : 768, top = 0;
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
// Mending a seam. Across a jump every overtone whose period does not divide the jump arrives out of step. In a plain
// crossfade it dips, to nothing when it is half a period out, and even with its level kept, a phase that steps within
// 10 ms is a blip in pitch: that is most of what a seam adds, and a chord has many such overtones. So:
//
//   * a model of the ringing sound is kept: its overtones, found in a spectrum of up to 170 ms of it, their levels and
//     phases fitted all at once to the newest samples (least squares: overtones too close for a spectrum to tell apart
//     are still told apart by a fit that knows where they are), and each one's frequency set right from how far its
//     phase comes between two fits. It is made in the background once a note has rung long enough, a piece of the
//     work every 128 samples (model_step), again when a longer fit has become possible and then every 0.4 s;
//   * at a seam one more fit gives the overtones' phases now (and keeps their frequencies right), and from then on,
//     sample by sample, each overtone's two out-of-step copies are taken out of the crossfade and one copy put in that
//     keeps its level and turns its phase from the old place's to the new one's slowly, over `mend_ms`, far longer
//     than the crossfade (glide). A seam inside that time takes each overtone on from where its turn has got to.
//
// What the model doesn't account for is crossfaded as before, quickly, so a note it doesn't know of yet is not held
// back. All of it comes from sound already in the ring: nothing waits for it.
// ------------------------------------------------------------------------------------------------
static void fft(double *re, double *im, int n) {
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
  for (int len = 2; len <= n; len <<= 1) {
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
static void fit(sp *s, const model *md, unsigned last, double *c) {
  int m = md->m, n = md->n;
  double or_[TONES], oi[TONES], a[TONES], b[TONES];
  for (int i = 0; i < n; i++) or_[i] = 1, oi[i] = a[i] = b[i] = 0;
  for (int k = 0; k < m; k++) {
    double x = md->taper[k] * s->ring[(last - (unsigned)(2 * k)) & (RING - 1)];
    for (int i = 0; i < n; i++) {  // the overtones don't depend on each other: the compiler does several at a time
      double t = or_[i] * md->cw[i] - oi[i] * md->sw[i];
      a[i] += x * or_[i], b[i] += x * oi[i];
      oi[i] = or_[i] * md->sw[i] + oi[i] * md->cw[i], or_[i] = t;
    }
  }
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

// The fit's matrix for the model's overtones, factored in place, and their steps for the fit. 0 if they can't be told
// apart.
static int fit_matrix(model *md) {
  int m = md->m, n = md->n;
  for (int i = 0; i < n; i++) md->cw[i] = cos(2 * md->w[i]), md->sw[i] = sin(2 * md->w[i]);
  for (int i = 0; i < n; i++)
    for (int j = 0; j <= i; j++) {
      double dr, di, ar, ai;
      taper_sum(2 * (md->w[i] - md->w[j]), m, &dr, &di);
      taper_sum(2 * (md->w[i] + md->w[j]), m, &ar, &ai);
      md->gram[2 * i][2 * j] = (dr + ar) / 2;          // cos with cos
      md->gram[2 * i + 1][2 * j + 1] = (dr - ar) / 2;  // sin with sin
      md->gram[2 * i + 1][2 * j] = (ai + di) / 2;      // sin i with cos j
      if (j < i) md->gram[2 * i][2 * j + 1] = (ai - di) / 2;  // cos i with sin j
    }
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

// One piece of the work of making a model, if one is due
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
      for (int i = 0; i < FFT_N; i++) {
        double x = i < s->spectrum ? s->ring[(s->q + 1 - (unsigned)s->spectrum + (unsigned)i) & (RING - 1)] : 0;
        s->fr[i] = x * (0.5 - 0.5 * cos(2 * M_PI * (i + 0.5) / s->spectrum)), s->fi[i] = 0;
      }
      fft(s->fr, s->fi, FFT_N);
      for (int k = 0; k < FFT_N / 4; k++) s->fr[k] = s->fr[k] * s->fr[k] + s->fi[k] * s->fi[k];
      s->stage = MATRIX;
      return;
    }
    case MATRIX:
      peaks(s, md, s->spectrum);
      for (int k = 0; k < md->m; k++) md->taper[k] = (float)(0.5 - 0.5 * cos(2 * M_PI * (k + 0.5) / md->m));
      s->stage = md->n && fit_matrix(md) ? FIRST : NONE;
      for (int i = 0; s->stage && i < md->n; i++) md->cw[i] = cos(2 * md->w[i]), md->sw[i] = sin(2 * md->w[i]);
      return;
    case FIRST:
      fit(s, md, s->q, s->first);
      s->stage = SECOND;
      return;
    case SECOND:
      fit(s, md, s->q - (unsigned)md->m, c);  // half a fit earlier
      for (int i = 0; i < md->n; i++) {
        double here = hypot(s->first[2 * i], s->first[2 * i + 1]), back = hypot(c[2 * i], c[2 * i + 1]);
        // The frequency, from how far the phase has come in half a fit against what the spectrum said. A fit with a
        // frequency a little off gives the phase of its window's middle carried on to its end at that frequency: put
        // right here.
        double off = remainder(atan2(s->first[2 * i + 1], s->first[2 * i]) - atan2(c[2 * i + 1], c[2 * i]) - md->w[i] * md->m, 2 * M_PI) / md->m;
        // An overtone that is not about as loud in both places is not ringing steadily: left to the crossfade
        int steady = here > 1e-6 && back > 0.6 * here && back < 1.6 * here && fabs(off) * md->m < 2.5;
        md->amp[i] = steady ? here : 0;
        md->w[i] += steady ? off : 0;
        md->ph[i] = atan2(s->first[2 * i + 1], s->first[2 * i]) + off * (md->m - 1);  // the window's middle is m - 1 back
      }
      md->ref = s->q;
      s->active = !s->active, s->ready = 1, s->gen++, s->made = s->since, s->stage = NONE;
      return;
  }
}

// Starts the glide for a crossfade from the lag `from` to the lag `to`, both as they are before this sample's drift,
// over `len` samples. 0 when there is no model to go by. The model says which overtones there are; where each one's
// phase is at the two read points is measured, by a fit of the samples before each. Measured, not worked out from the
// model's frequency: a note that is bent, or settling after its start, has moved since the model was made.
static int mend(sp *s, double from, double to, int len) {
  if (!s->ready) return 0;
  model *md = &s->model[s->active];
  unsigned q = s->at - 1;
  // Where the two read points are for the crossfade's first sample, and the ring's sample just before each
  double pa = (double)q - from - (1 - s->ratio), pb = (double)q - to - (1 - s->ratio), qa = floor(pa), qb = floor(pb);
  double here[2 * TONES], there[2 * TONES], span = qb - qa;
  fit(s, md, (unsigned)(long long)qa, here);
  fit(s, md, (unsigned)(long long)qb, there);
  int same = s->gliding && s->glide_gen == s->gen;
  for (int i = 0; i < md->n; i++) {
    glider *g = &s->glide[i];
    double la = hypot(here[2 * i], here[2 * i + 1]), lb = hypot(there[2 * i], there[2 * i + 1]), w = md->w[i];
    double pha = atan2(here[2 * i + 1], here[2 * i]), phb = atan2(there[2 * i + 1], there[2 * i]);
    double left = same && g->a > 0 ? atan2(g->ti, g->tr) : 0;  // of the turn under way
    g->a = 0;
    // An overtone that is not about as loud in both places is not ringing steadily: left to the crossfade
    if (md->amp[i] <= 0 || la < 1e-6 || lb < 0.6 * la || lb > 1.6 * la) continue;
    // Its frequency now, from how far its phase comes between the two read points against what the model says, when
    // they are far enough apart to tell; and the fits' phases put right for it (as in model_step)
    double off = fabs(span) >= 256 ? remainder(phb - pha - w * span, 2 * M_PI) / span : 0;
    if (fabs(off * span) > 2.5 || fabs(off) > s->moved) continue;
    w += off;
    pha += off * (md->m - 1) + w * (pa - qa), phb += off * (md->m - 1) + w * (pb - qb);
    double apart = remainder(phb - pha, 2 * M_PI);
    g->a = lb, g->was = la;
    g->cr = cos(phb), g->ci = sin(phb), g->sr = cos(w * s->ratio), g->si = sin(w * s->ratio);
    g->cphi = cos(apart), g->sphi = sin(apart);
    g->turn = remainder(left - apart, 2 * M_PI);
    g->tr = cos(g->turn), g->ti = sin(g->turn), g->dr = g->di = 0;
    g->lv = la, g->dv = 0;
  }
  s->gliding = s->glide_len = len, s->glide_n = md->n, s->glide_gen = s->gen;
  return 1;
}

// This sample's mend: what the gliding overtones are, less what the read points give of them. `share`: how much of
// the output is the new read point's (1 once the crossfade is over).
static double glide(sp *s, double share) {
  int at = s->glide_len - s->gliding, len = s->glide_len;
  if (at % 16 == 0) {
    // Each overtone's turn for the next 16 samples: where it has got to, and a straight line to where it is to be
    double now = 0.5 + 0.5 * cos(M_PI * at / len), then = at + 16 >= len ? 0 : 0.5 + 0.5 * cos(M_PI * (at + 16) / len);
    for (int i = 0; i < s->glide_n; i++) {
      glider *g = &s->glide[i];
      if (g->a <= 0) continue;
      double norm = hypot(g->cr, g->ci);
      g->cr /= norm, g->ci /= norm;
      g->tr = cos(g->turn * now), g->ti = sin(g->turn * now);
      g->dr = (cos(g->turn * then) - g->tr) / 16, g->di = (sin(g->turn * then) - g->ti) / 16;
      g->lv = g->a + (g->was - g->a) * now, g->dv = (g->was - g->a) * (then - now) / 16;
    }
  }
  double fix = 0;
  for (int i = 0; i < s->glide_n; i++) {
    glider *g = &s->glide[i];
    if (g->a <= 0) continue;
    double out = g->cr * g->tr - g->ci * g->ti, old = g->cr * g->cphi + g->ci * g->sphi;
    fix += g->lv * out - share * g->a * g->cr - (1 - share) * g->was * old;
    double t = g->cr * g->sr - g->ci * g->si;
    g->ci = g->cr * g->si + g->ci * g->sr, g->cr = t;
    g->tr += g->dr, g->ti += g->di, g->lv += g->dv;
  }
  s->gliding--;
  return fix;
}

static void jump(sp *s, double to, int fade) {
  s->old = s->lag;
  s->lag = to;
  s->fade = s->fade_len = fade;
}

// A jump with its seam mended, if that can be: over `len` samples
static void seam(sp *s, double to, int fade, int len) {
  if (!(s->mend && len > fade && mend(s, s->lag, to, len))) s->gliding = 0;
  jump(s, to, fade);
}

// 2 when this sample starts a note, 1 when it may (a small rise)
static int starts(sp *s, double x) {
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
      s->over[k] = SOME * s->quiet[k] + 2e-3;
    }
    s->past_at = (s->past_at + 1) % PAST;
    s->ms = 0;
  }
  int start = !s->rest && (s->fast > RISE * s->base + 3e-4 || fresh);
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
      if (s->lag > 0 && !s->fade) s->gliding = 0, jump(s, 0, FADE);
    } else {
      if (s->mend && (s->at & 127) == 0) model_step(s);
      int start = s->attack && down ? starts(s, x) : 0;
      // What may be a soft note under louder ones is not worth a seam where the read point stands; it is worth an
      // ordinary jump to a repeat a little early, if the lag is long enough to be heard on a note
      if (start == 1 && s->patient && s->lag >= SOFT_LAG) s->soon = 1, s->check = CHECK;
      if (start == 2) {
        // What rang before the start rings on under it: its overtones are carried across this seam as across any
        if (s->lag > ATTACK_LAG + 24) seam(s, ATTACK_LAG, FADE_ATTACK, s->start_len), s->attacks++;
        s->since = 0, s->ready = 0, s->stage = NONE, s->tried = -REFRESH;
        s->soon = 0;
      } else if (s->since++, !s->fade && !s->rest && ++s->check >= CHECK) {
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
              far = place(s, 1, by, &score);
            else if (here < 0)
              ;
            else if (s->lag >= s->most + 4 * CHECK)
              s->gliding = 0, jump(s, LEAST_LAG, FADE);  // nothing found in time: silence
          } else if (s->eager && room >= LEAST_JUMP && ++s->early >= EARLY) {
            // A sound that repeats cleanly and soon, a single note or a power chord, loses nothing by a seam: it
            // is not made to wait, and its lag stays within one repeat. Looked for every few looks only.
            s->early = 0;
            int by = repeat(s, room, 0, &score);
            if (by && score >= s->good) far = place(s, 1, by, &score);
            if (score < s->good) far = 0;
          }
        } else if (down && room >= LEAST_JUMP) {
          int by = repeat(s, s->most, 0, NULL);
          // A lag that holds two of them or more (a high note, or after a long seam): the longest it holds
          if (by && room >= 2 * by) by = repeat(s, room, 1, NULL);
          if (by && room >= by)
            far = place(s, 1, by, &score);
          else if (!by)
            s->gliding = 0, jump(s, LEAST_LAG, FADE);  // silence: nothing to keep in step
        } else if (!down && s->lag < LEAST_LAG + (CHECK + 2 * FADE) * -drift + 4) {
          // The lag must not run out before the next look, or under a fade
          int by = repeat(s, s->most, 0, NULL);
          if (by) far = place(s, 0, by, &score);
        }
        if (far > 0) {
          seam(s, down ? s->lag - far : s->lag + far, score >= s->good ? FADE : 2 * FADE, s->mend_len);
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
      g = 0.5 - 0.5 * cos(M_PI * (double)(s->fade_len - s->fade + 1) / (s->fade_len + 1));
      y = g * y + (1 - g) * read_at(s, s->old);
      s->fade--;
    }
    if (s->gliding) y += glide(s, g);
    out[f] = (float)y;
  }
}
