// The pitch shifter (pitch_shift.h).
//
// A band k has a centre w and a low-pass of `poles` equal one-pole stages. Its signal is brought down to 0 Hz
// (x * e^-iwn), low-passed there (b, the band's slowly turning amplitude) and brought back up (z = b * e^iwn): a
// resonator at w whose output is complex, so its phase can be turned. The output is the sum of Re(g * z * u):
//
//   g  a fixed gain. Its phase steps by the same angle from band to band, which is what makes neighbours add up where
//      they cross (at the crossing one lags and the other leads by the filter's phase there); its size is set once so
//      that the bands together are flat (calibrate).
//   u  the turn, a unit number that rotates by (ratio - 1) * f each sample, f the frequency the band is found to
//      hold: the phase b advances by from sample to sample, averaged with the signal's level as the weight.
//
// Every TICK samples the bands are grouped: a band louder than its neighbours leads, the bands down to the quietest
// between two leaders follow the nearer one. A follower turns at its leader's rate and its u moves to the leader's
// over lock_ms, so an overtone that two or three bands share comes out of all of them in step. After silence every
// u starts from the same place.
#include "pitch_shift.h"

#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define TICK 16
#define MAX_POLES 4
#define QUIET 1e-9          // the bands' summed power under which the input counts as silence
#define QUIET_TICKS 90      // that many ticks of it (30 ms at 48 kHz) and the turns start over
#define FLOOR 1e-11         // a band under this power leads nothing

typedef struct {
  double w, cw, sw;         // the centre in radians a sample, and e^iw
  double or_, oi;           // e^iwn
  double a;                 // a stage's coefficient
  double lr[MAX_POLES], li[MAX_POLES];
  double pr, pi;            // b one sample ago
  double cr, ci, ca;        // b * conj(b before), smoothed, and the smoothing's coefficient
  double ur, ui, rr, ri;    // u, and its step a sample
  double gr, gi;
  double hz, width, mag;
  int lead;                 // the band this one follows; itself when it leads
} band;

struct ps {
  double rate, lock;
  int n, poles, tick, quiet, at_zero;
  _Atomic double semitones;
  double ratio, set;        // the ratio in use, and the semitones it was made from
  band *b;
};

void ps_defaults(ps_params *p) {
  p->low = 62;
  p->high = 15000;
  p->min_width = 36;
  p->per_octave = 12;
  p->poles = 2;
  p->width = 0.5;
  p->lock_ms = 3;
  p->track = 1;
  p->at_zero = 0;
}

// A band filter's answer to a sine `hz` from its centre: (a / (1 - (1 - a) e^-it))^poles
static void answer(const band *b, int poles, double rate, double hz, double *re, double *im) {
  double t = 2 * M_PI * (hz - b->hz) / rate, dr = 1 - (1 - b->a) * cos(t), di = (1 - b->a) * sin(t);
  double m = b->a / (dr * dr + di * di), sr = m * dr, si = -m * di, r = 1, i = 0;
  for (int j = 0; j < poles; j++) {
    double x = r * sr - i * si;
    i = r * si + i * sr;
    r = x;
  }
  *re = r;
  *im = i;
}

// The gains' sizes: each band's is moved until the bands together answer every centre with 1
static void calibrate(ps *s) {
  for (int pass = 0; pass < 40; pass++)
    for (int j = 0; j < s->n; j++) {
      double re = 0, im = 0;
      for (int k = 0; k < s->n; k++) {
        double hr, hi;
        answer(&s->b[k], s->poles, s->rate, s->b[j].hz, &hr, &hi);
        re += s->b[k].gr * hr - s->b[k].gi * hi;
        im += s->b[k].gr * hi + s->b[k].gi * hr;
      }
      double f = pow(hypot(re, im), -0.6);
      s->b[j].gr *= f;
      s->b[j].gi *= f;
    }
  // A real sine is half in a band (the other half is its mirror image at -w), and the output is the real part
  for (int j = 0; j < s->n; j++) s->b[j].gr *= 2, s->b[j].gi *= 2;
}

ps *ps_new(double rate, const ps_params *params) {
  ps_params d;
  if (!params) {
    ps_defaults(&d);
    params = &d;
  }
  ps *s = calloc(1, sizeof *s);
  if (!s) return NULL;
  s->rate = rate;
  s->poles = params->poles < 1 ? 1 : params->poles > MAX_POLES ? MAX_POLES : params->poles;
  s->at_zero = params->at_zero;
  s->ratio = 1;
  double high = params->high < rate * 0.45 ? params->high : rate * 0.45, step = pow(2, 1 / params->per_octave) - 1;
  int n = 0;
  for (double f = params->low; f < high; f += fmax(params->min_width, f * step)) n++;
  s->n = n;
  s->b = calloc((size_t)n, sizeof *s->b);
  if (!s->b) {
    free(s);
    return NULL;
  }
  // The angle a neighbour's gain is turned by: twice a filter's phase where two bands cross
  double turn = 2 * s->poles * atan(1 / (2 * params->width)), f = params->low;
  for (int k = 0; k < n; k++) {
    band *b = &s->b[k];
    b->hz = f;
    b->width = fmax(params->min_width, f * step);
    b->w = 2 * M_PI * f / rate;
    b->cw = cos(b->w);
    b->sw = sin(b->w);
    b->or_ = 1;
    double cutoff = params->width * b->width;
    b->a = 1 - exp(-2 * M_PI * cutoff / rate);
    b->ca = 1 - exp(-2 * M_PI * cutoff / (rate * fmax(params->track, 0.01)));
    b->gr = cos(-turn * k);
    b->gi = sin(-turn * k);
    b->ur = b->rr = 1;
    b->lead = k;
    f += b->width;
  }
  calibrate(s);
  s->lock = 1 - exp(-TICK / (rate * fmax(params->lock_ms, 0.01) * 1e-3));
  return s;
}

void ps_free(ps *s) {
  if (!s) return;
  free(s->b);
  free(s);
}

void ps_set_semitones(ps *s, double semitones) { atomic_store_explicit(&s->semitones, semitones, memory_order_relaxed); }
int ps_bands(const ps *s) { return s->n; }

// Who leads whom, and every band's step
static void group(ps *s) {
  band *b = s->b;
  int n = s->n, last = -1;
  double sum = 0;
  for (int k = 0; k < n; k++) {
    double m = hypot(b[k].or_, b[k].oi);  // the two rotating numbers drift off length 1
    b[k].or_ /= m;
    b[k].oi /= m;
    m = hypot(b[k].ur, b[k].ui);
    b[k].ur /= m;
    b[k].ui /= m;
    b[k].mag = (b[k].pr * b[k].pr + b[k].pi * b[k].pi) * (b[k].gr * b[k].gr + b[k].gi * b[k].gi);
    sum += b[k].mag;
  }
  if (sum < QUIET) {
    if (++s->quiet == QUIET_TICKS)
      for (int k = 0; k < n; k++) b[k].ur = 1, b[k].ui = 0;
  } else
    s->quiet = 0;
  for (int k = 0; k < n; k++) {
    int peak = b[k].mag > FLOOR && (k == 0 || b[k].mag > b[k - 1].mag) && (k == n - 1 || b[k].mag >= b[k + 1].mag);
    if (!peak) continue;
    if (last < 0)
      for (int j = 0; j < k; j++) b[j].lead = k;
    else {
      int low = last + 1;  // the quietest band between the two leaders: from it on, the bands follow this one
      for (int j = last + 1; j < k; j++)
        if (b[j].mag < b[low].mag) low = j;
      for (int j = last + 1; j < k; j++) b[j].lead = j < low ? last : k;
    }
    b[k].lead = k;
    last = k;
  }
  for (int k = last + 1; k < n; k++) b[k].lead = last < 0 ? k : last;
  for (int k = 0; k < n; k++) {
    band *l = &b[b[k].lead];
    // The frequency the leader holds: its centre and the phase its amplitude advances by a sample, kept within
    // a band and a half of the centre
    double off = l->mag > FLOOR ? atan2(l->ci, l->cr) : 0, most = 3 * M_PI * l->width / s->rate;
    off = off > most ? most : off < -most ? -most : off;
    double inc = (s->ratio - 1) * (l->w + off);
    b[k].rr = cos(inc);
    b[k].ri = sin(inc);
    if (l != &b[k]) {
      double r = b[k].ur + s->lock * (l->ur - b[k].ur), i = b[k].ui + s->lock * (l->ui - b[k].ui), m = hypot(r, i);
      if (m > 1e-6) b[k].ur = r / m, b[k].ui = i / m;
    }
  }
}

void ps_process(ps *s, const float *in, float *out, int frames) {
  double st = atomic_load_explicit(&s->semitones, memory_order_relaxed);
  if (st != s->set) {
    s->set = st;
    s->ratio = pow(2, st / 12);
  }
  if (s->ratio == 1 && !s->at_zero) {
    if (out != in) memmove(out, in, (size_t)frames * sizeof *in);
    return;
  }
  int poles = s->poles, n = s->n;
  for (int f = 0; f < frames; f++) {
    if (s->tick-- == 0) {
      group(s);
      s->tick = TICK - 1;
    }
    double x = in[f] + 1e-25, y = 0;
    for (int k = 0; k < n; k++) {
      band *b = &s->b[k];
      double mr = x * b->or_, mi = -x * b->oi, a = b->a;
      for (int j = 0; j < poles; j++) {
        mr = b->lr[j] += a * (mr - b->lr[j]);
        mi = b->li[j] += a * (mi - b->li[j]);
      }
      b->cr += b->ca * (mr * b->pr + mi * b->pi - b->cr);
      b->ci += b->ca * (mi * b->pr - mr * b->pi - b->ci);
      b->pr = mr;
      b->pi = mi;
      double zr = mr * b->or_ - mi * b->oi, zi = mr * b->oi + mi * b->or_;
      double yr = zr * b->ur - zi * b->ui, yi = zr * b->ui + zi * b->ur;
      y += b->gr * yr - b->gi * yi;
      double t = b->or_ * b->cw - b->oi * b->sw;
      b->oi = b->or_ * b->sw + b->oi * b->cw;
      b->or_ = t;
      t = b->ur * b->rr - b->ui * b->ri;
      b->ui = b->ur * b->ri + b->ui * b->rr;
      b->ur = t;
    }
    out[f] = (float)y;
  }
}
