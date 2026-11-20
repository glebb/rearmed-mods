// The pitch shifters' bench: plays made-up guitar through each shifter and measures what comes out against the same
// guitar made at the lower pitch, which no shifter could beat. No game, no device, no recordings.
//
//   bench [-v] [-s SEMITONES[,SEMITONES...]] [-l PEAK] [-w DIR] [-i]
//     -v  every case, not only the groups     -s  the shifts (default -1,-2)
//     -l  the peak the made-up guitar comes in at (default 0.5; 0.05 is a quiet input, 0.01 a very quiet one)
//     -w  writes what each shifter made, all cases in a row, to DIR as .wav, clean and through a distortion
//     -i  only the new shifter's bands at 0 semitones: how flat and how late they are by themselves
//     -r  recorded guitar in place of the made-up one: every argument after it is a .wav (48 kHz mono, prepare.sh);
//         -f SECONDS and -t SECONDS before it: where in each recording to start, and how much to take (0, 60)
//
// The guitar: each note is a sum of overtones a little sharp of whole multiples (as a string's are), each with its
// own decay, behind a short burst of noise for the pick. The same notes made with every frequency times the ratio are
// the ideal. The cases are single notes over the neck, notes high up as a real guitar has them (their first overtone
// flat of where the others put it: on the recorded guitar the second is 9 to 13 cents sharp of twice the first from
// A#3 up, which is what keeps such a note from repeating), power chords, strummed open chords, two low
// notes close together, a note hammered on, pulled off or picked while others ring, a note high up with a quiet one
// under it, a note that stops as the next one starts (one string), notes with a gap after each, palm-muted hits, a
// fast run, a bend and vibratos.
//
// What is measured, per case:
//   attack   ms the note's start comes later than in the ideal (the level's first rise: the pick and the high end)
//   low, mid ms the overtones under 300 Hz, and from there to 1.5 kHz, come later (each one's rise to half, the median)
//   later    the same for the overtones of a note that starts while the first ones ring: the legato cases
//   held, most  ms the shifter says it is behind while the notes ring, the mean and the most (those that say)
//   tune     the worst overtone under 2 kHz, in cents off its place
//   junk     dB of what is not at an overtone's place, against everything
//   waver    dB an overtone's level moves about against the ideal's over time (the louder ones count more)
//   differ   dB the spectrum over time is off the ideal's, on average: the one measure of the cases that move, and
//            for chords it also moves with the overtones' phases alone (the row "ideal" shows by how much)
//   left     dB of a note that stopped when the next one started, 30 to 80 ms on, against while it rang: its first
//            overtone (the row "ideal": how fast the made-up note dies away). For notes with a gap after each: all
//            that sounds from 70 to 110 ms into the gap (a shifter 30 ms behind has the note's end by then), against
//            the note's last 60 ms, the worst of the gaps
// low to waver follow single overtones, those with no other within 30 Hz, and only in the cases that ring.
// The row "ideal" is the ideal made again with other phases: what the measures say of a perfect shifter.
// Through a distortion nothing is measured: -w writes that for listening.
// The shifters: "splice" (splice_shift.c), "splice-e" (the same with `eager`), "bands" (pitch_shift.c), "two-tap" (the plain crossfading delay line, the known-bad end), and "rab"
// (RocksmithAudioBridge's, when built in: run.sh).
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../pitch_shift.h"
#include "../splice_shift.h"

#define SR 48000.0
#define BLOCK 128
#define MAXN 24
#define T0 0.05     // the first note's start
#define STEADY 0.12 // seconds after the last start, where the steady measures begin
#define NFFT 16384

// ------------------------------------------------------------------------------------------------
// The guitar
// ------------------------------------------------------------------------------------------------
typedef struct {
  double midi, at, len, tau;      // len 0: rings on; tau: the first overtone's decay
  double bend, bend_at, bend_len; // semitones, reached over bend_len
  double vib, vib_hz;             // cents either way
  double amp;                     // 1: as loud as the others
  int soft;                       // a hammer-on or pull-off: no pick, and the note comes up over 4 ms
  double sharp;                   // cents the second overtone is sharp of twice the first, by the first being flat
} note;

typedef struct {
  const char *name, *group;
  int n, steady;                  // steady: the notes ring and stay where they are
  int left;                       // 1: the first note stops as the second starts: what is left of it is measured;
                                  // 2: every note stops and a gap follows: what sounds in the gaps is
  double dur;
  note note[MAXN];
} tcase;

static tcase g_case[96];
static int g_cases;

static tcase *add(const char *group, const char *name, double dur, int steady) {
  tcase *c = &g_case[g_cases++];
  memset(c, 0, sizeof *c);
  c->name = name;
  c->group = group;
  c->dur = dur;
  c->steady = steady;
  return c;
}
static note *hit(tcase *c, double midi, double at, double tau) {
  note *n = &c->note[c->n++];
  n->midi = midi;
  n->at = T0 + at;
  n->tau = tau;
  n->amp = 1;
  return n;
}
static void chord(const char *group, const char *name, const double *midi, int n, double strum) {
  tcase *c = add(group, name, 1.6, 1);
  for (int i = 0; i < n; i++) hit(c, midi[i], i * strum, 1.0);
}

static void make_cases(void) {
  static const char *const names[] = {"E2", "A2", "D3", "G3", "B3", "E4", "A4", "E5"};
  static const double single[] = {40, 45, 50, 55, 59, 64, 69, 76};
  for (int i = 0; i < 8; i++) hit(add("single notes", names[i], 1.6, 1), single[i], 0, 1.0);
  static const char *const up[] = {"E4", "B4", "E5", "A5", "C6", "E6"};
  for (int i = 0; i < 6; i++) hit(add("high notes", up[i], 1.6, 1), (double[]){64, 71, 76, 81, 84, 88}[i], 0, 0.7)->sharp = (double[]){6, 9, 10, 12, 12, 10}[i];
  chord("power chords", "E5", (double[]){40, 47, 52}, 3, 0);
  chord("power chords", "G5", (double[]){43, 50, 55}, 3, 0);
  chord("power chords", "A5", (double[]){45, 52, 57}, 3, 0);
  chord("power chords", "C5", (double[]){48, 55, 60}, 3, 0);
  chord("open chords", "E", (double[]){40, 47, 52, 56, 59, 64}, 6, 0.008);
  chord("open chords", "Em", (double[]){40, 47, 52, 55, 59, 64}, 6, 0.008);
  chord("open chords", "G", (double[]){43, 47, 50, 55, 59, 67}, 6, 0.008);
  chord("open chords", "Am", (double[]){45, 52, 57, 60, 64}, 5, 0.008);
  chord("open chords", "C", (double[]){48, 52, 55, 60, 64}, 5, 0.008);
  chord("open chords", "D", (double[]){50, 57, 62, 66}, 4, 0.008);
  chord("low and close", "E2+G2", (double[]){40, 43}, 2, 0);
  chord("low and close", "E2+G#2", (double[]){40, 44}, 2, 0);
  chord("low and close", "G2+B2", (double[]){43, 47}, 2, 0);
  chord("low and close", "A2+C3", (double[]){45, 48}, 2, 0);
  // A note that starts while others ring, with little or no pick: what a shifter is behind by then is what that
  // note comes late by
  static const struct {
    const char *name;
    double from[7], to, amp;
    int soft, ring;  // ring: the first notes ring on under the new one
  } legato[] = {
      {"hammer-on A3 C4", {57}, 60, 0.8, 1, 0},     {"hammer-on E3 F3", {52}, 53, 0.8, 1, 0},
      {"pull-off E4 D4", {64}, 62, 0.7, 1, 0},      {"note over E chord", {40, 47, 52, 56, 59, 64}, 71, 0.5, 0, 1},
      {"hammer-on in Am", {45, 52, 57, 60, 64}, 62, 0.5, 1, 1},
      {"picked note over Am", {45, 52, 57, 60, 64}, 66, 1.0, 0, 1}, {"picked note over E5", {40, 47, 52}, 67, 1.0, 0, 1},
  };
  for (int i = 0; i < 7; i++) {
    tcase *l = add("legato", legato[i].name, 1.6, 1);
    for (int j = 0; legato[i].from[j] > 0; j++) hit(l, legato[i].from[j], 0, 1.0)->len = legato[i].ring ? 0 : 0.4;
    note *to = hit(l, legato[i].to, 0.4, 1.0);
    to->amp = legato[i].amp, to->soft = legato[i].soft;
  }
  // A note high up with a quiet one under it, as a string left ringing is: the quiet one is to come out lower too
  static const struct {
    const char *name;
    double high, low, amp;
  } under[] = {{"A5, E4 30 dB under", 81, 64, 0.03}, {"E5, B3 36 dB under", 76, 59, 0.016}, {"C6, G3 40 dB under", 84, 55, 0.01}};
  for (int i = 0; i < 3; i++) {
    tcase *u = add("a quiet note under", under[i].name, 1.6, 1);
    hit(u, under[i].low, 0, 1.0)->amp = under[i].amp;
    hit(u, under[i].high, 0.3, 0.7)->sharp = 10;
  }
  // A note that stops as the next one starts, as on one string: nothing of it should be left
  static const struct {
    const char *name;
    double from, to;
  } one[] = {{"E3 then B3", 52, 59}, {"A3 then C4", 57, 60}, {"E4 then G4", 64, 67}, {"B4 then D5", 71, 74}, {"E5 then G5", 76, 79}, {"A5 then C6", 81, 84}};
  for (int i = 0; i < 6; i++) {
    tcase *o = add("one string", one[i].name, 1.2, 0);
    o->left = 1;
    hit(o, one[i].from, 0, 1.0)->len = 0.5 + i * 0.0013;  // not always at the same place in the first note's turn
    hit(o, one[i].to, 0.5 + i * 0.0013, 1.0);
  }
  // Notes of many lengths with a gap after each, as when a part is played short: a seam falls in some of the gaps,
  // and nothing of the note before should sound there
  for (int i = 0; i < 4; i++) {
    tcase *g = add("notes with gaps", (const char *[]){"0.20 to 0.26 s", "0.29 to 0.35 s", "0.38 to 0.44 s", "0.47 to 0.53 s"}[i], 0, 0);
    double at = 0;
    g->left = 2;
    for (int j = 0; j < 3; j++) {
      double len = 0.20 + 0.03 * (3 * i + j);
      hit(g, (double[]){45, 50, 52}[j], at, 1.0)->len = len;
      at += len + 0.12;
    }
    g->dur = at + 0.08;
  }
  tcase *c = add("moving", "palm mutes", 1.2, 0);
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 3; j++) hit(c, (double[]){40, 47, 52}[j], i * 0.14, 0.05);
  c = add("moving", "run", 1.4, 0);
  for (int i = 0; i < 12; i++) hit(c, (double[]){52, 54, 56, 57, 59, 61, 63, 64, 63, 61, 59, 57}[i], i * 0.09, 1.0)->len = 0.09;
  c = add("moving", "bend", 1.4, 0);
  note *n = hit(c, 62, 0, 1.0);
  n->bend = 2, n->bend_at = 0.15, n->bend_len = 0.3;
  c = add("moving", "vibrato", 1.4, 0);
  n = hit(c, 57, 0, 1.0);
  n->vib = 35, n->vib_hz = 5.5;
  c = add("moving", "vibrato high up", 1.4, 0);
  n = hit(c, 79, 0, 0.7);
  n->vib = 35, n->vib_hz = 5.5, n->sharp = 10;
  c = add("moving", "bend high up", 1.4, 0);
  n = hit(c, 74, 0, 0.7);
  n->bend = 2, n->bend_at = 0.15, n->bend_len = 0.3, n->sharp = 10;
}

static uint32_t g_rng;
static double rnd(void) {
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return g_rng / 4294967296.0;
}

// An overtone of the case's steady stretch: where it is and how loud there
typedef struct {
  double hz, amp;
  int last;  // of a note that starts after the case's first
} partial;
static partial g_part[4096];
static int g_parts;

#define STIFF 1.2e-4  // how far a string's overtones are sharp: f(k) = k f sqrt(1 + STIFF k^2)

// The case at `ratio` times its pitch; `seed` sets the overtones' phases (the pick is the same for every seed).
// Fills g_part for a steady case.
static void render(const tcase *c, double ratio, uint32_t seed, float *out, int frames) {
  double *sum = calloc((size_t)frames, sizeof *sum);
  double last = 0;
  for (int i = 0; i < c->n; i++)
    if (c->note[i].at > last) last = c->note[i].at;
  g_parts = 0;
  for (int i = 0; i < c->n; i++) {
    const note *n = &c->note[i];
    double f0 = 440 * pow(2, (n->midi - 69) / 12);
    int start = (int)(n->at * SR), harmonics = 0;
    double fac[160], amp[160], tau[160], ph[160];
    g_rng = seed * 2654435761u + (uint32_t)i * 40503u + 1;
    // The first overtone flat by a share `pull` of the note, the k-th by a k-th of that (in Hz): what makes the
    // second `sharp` cents sharp of twice the first
    double twice = pow(2, n->sharp / 1200), pull = (2 * twice - 2) / (2 * twice - 0.5);
    for (int k = 1; k <= 160; k++) {
      double f = k * sqrt(1 + STIFF * k * k) * (1 - pull / (k * k)) / (1 - pull);
      if (f * f0 > 7000) break;
      fac[harmonics] = f;
      amp[harmonics] = fabs(sin(k * M_PI * 0.17)) / k / (1 + pow(f * f0 / 3500, 2));
      tau[harmonics] = n->tau / (1 + 0.3 * (k - 1));
      ph[harmonics] = 2 * M_PI * rnd();
      amp[harmonics] *= n->amp;
      if (c->steady && g_parts < 4096 && !(n->len > 0 && n->at + n->len < last + STEADY)) {  // not of a note that has ended
        g_part[g_parts].hz = f * f0 * ratio;
        g_part[g_parts].last = n->at > c->note[0].at + 0.1;
        g_part[g_parts++].amp = amp[harmonics] * exp(-(last + STEADY - n->at) / tau[harmonics]);
      }
      harmonics++;
    }
    g_rng = (uint32_t)i * 7919u + 12345u;
    double prev = 0;
    for (int t = start; t < frames; t++) {
      double rise = n->soft ? 0.004 : 0.0015;
      double s = (t - start) / SR, semis = 0, env = s < rise ? 0.5 - 0.5 * cos(M_PI * s / rise) : 1;
      if (n->bend_len > 0) semis = n->bend * fmin(1, fmax(0, (s - n->bend_at) / n->bend_len));
      if (n->vib > 0) semis += n->vib / 100 * sin(2 * M_PI * n->vib_hz * s) * fmin(1, s / 0.2);
      if (n->len > 0 && s > n->len) env *= exp(-(s - n->len) / 0.012);
      if (env < 1e-5 && s > 0.1) break;
      double w = 2 * M_PI * ratio * f0 * pow(2, semis / 12) / SR, v = 0;
      for (int k = 0; k < harmonics; k++) {
        v += amp[k] * exp(-s / tau[k]) * sin(ph[k]);
        ph[k] += w * fac[k];
      }
      double white = rnd() - 0.5, pick = n->soft ? 0 : (white - prev) * 0.25 * n->amp * exp(-s / 0.002);
      prev = white;
      sum[t] += env * (v + pick);
    }
  }
  for (int t = 0; t < frames; t++) out[t] = (float)sum[t];
  free(sum);
}

// ------------------------------------------------------------------------------------------------
// The shifters
// ------------------------------------------------------------------------------------------------
typedef struct {
  const char *name;
  void *(*make)(double semitones);
  void (*run)(void *state, float *samples, int frames);
  void (*end)(void *state);
  double (*lag)(void *state);  // the shifter's own account of its delay right now, in frames; NULL: it gives none
  int exact;                   // that account is of this very moment, not an average over a while
} shifter;

static ps_params g_params;
static void *bands_make(double st) {
  ps *s = ps_new(SR, &g_params);
  ps_set_semitones(s, st);
  return s;
}
static void bands_run(void *s, float *x, int n) { ps_process(s, x, x, n); }
static void bands_end(void *s) { ps_free(s); }

static sp_params g_splice;
static void *splice_make(double st) {
  sp *s = sp_new(SR, &g_splice);
  sp_set_semitones(s, st);
  return s;
}
static void *eager_make(double st) {
  sp_params p = g_splice;
  p.eager = 1;
  sp *s = sp_new(SR, &p);
  sp_set_semitones(s, st);
  return s;
}
static void splice_run(void *s, float *x, int n) { sp_process(s, x, x, n); }
static long g_jumps, g_poor, g_attacks;  // the splice shifter's counts, over every run
static double g_jumped, g_scores;
static void splice_end(void *s) {
  long jumps, poor, attacks;
  double jumped, scores;
  sp_counts(s, &jumps, &poor, &attacks, &jumped, &scores);
  g_jumps += jumps, g_poor += poor, g_attacks += attacks, g_jumped += jumped, g_scores += scores;
  sp_free(s);
}
static double splice_lag(void *s) { return sp_delay(s); }

// The plain delay-line shifter: two taps half a window apart, each fading in and out as it crosses the window
#define TAP_WINDOW 1024
typedef struct {
  float ring[4096];
  int at;
  double phase, step;
} twotap;
static void *twotap_make(double st) {
  twotap *t = calloc(1, sizeof *t);
  t->step = (1 - pow(2, st / 12)) / TAP_WINDOW;
  return t;
}
static double tap(const twotap *t, double delay) {
  double p = t->at - delay;
  int i = (int)floor(p);
  double f = p - i;
  return t->ring[i & 4095] * (1 - f) + t->ring[(i + 1) & 4095] * f;
}
static void twotap_run(void *state, float *x, int n) {
  twotap *t = state;
  for (int i = 0; i < n; i++) {
    t->ring[t->at & 4095] = x[i];
    double a = t->phase, b = a + 0.5 < 1 ? a + 0.5 : a - 0.5;
    x[i] = (float)(tap(t, 1 + a * TAP_WINDOW) * (0.5 - 0.5 * cos(2 * M_PI * a)) +
                   tap(t, 1 + b * TAP_WINDOW) * (0.5 - 0.5 * cos(2 * M_PI * b)));
    t->phase += t->step;
    t->phase -= floor(t->phase);
    t->at++;
  }
}

#ifdef HAVE_RAB
void *rab_new(int semitones);
void rab_process(void *state, float *samples, unsigned frames);
void rab_free(void *state);
double rab_delay(void *state);
static void *rab_make(double st) { return rab_new((int)lround(st)); }
static void rab_run(void *s, float *x, int n) { rab_process(s, x, (unsigned)n); }

// An experiment: the guitar split in ranges (fourth-order crossovers at RAB_SPLIT, "250,1000"), one of its shifters on
// each. Does a splice find a better place when a range holds fewer overtones?
typedef struct {
  int n;
  double hz[4], low[4][2][4], high[4][2][4];  // per crossover: two biquads a side, each x1 x2 y1 y2
  void *part[5];
} split;
static double biquad(double *z, double x, double hz, int high) {
  double w = 2 * M_PI * hz / SR, alpha = sin(w) / (2 * M_SQRT1_2), a0 = 1 + alpha, c = cos(w);
  double b0 = (high ? (1 + c) / 2 : (1 - c) / 2) / a0, b1 = (high ? -(1 + c) : 1 - c) / a0;
  double y = b0 * x + b1 * z[0] + b0 * z[1] + 2 * c / a0 * z[2] - (1 - alpha) / a0 * z[3];
  z[1] = z[0], z[0] = x, z[3] = z[2], z[2] = y;
  return y;
}
static void *split_make(double st) {
  split *s = calloc(1, sizeof *s);
  const char *p = getenv("RAB_SPLIT");
  for (p = p && *p ? p : "250,1000"; *p && s->n < 4;) {
    char *end;
    s->hz[s->n++] = strtod(p, &end);
    p = *end ? end + 1 : end;
  }
  for (int i = 0; i <= s->n; i++) s->part[i] = rab_new((int)lround(st));
  return s;
}
static void split_run(void *state, float *x, int n) {
  split *s = state;
  static float part[5][BLOCK];
  for (int i = 0; i < n; i++) {
    double rest = x[i];
    for (int k = 0; k < s->n; k++) {
      part[k][i] = (float)biquad(s->low[k][1], biquad(s->low[k][0], rest, s->hz[k], 0), s->hz[k], 0);
      rest = biquad(s->high[k][1], biquad(s->high[k][0], rest, s->hz[k], 1), s->hz[k], 1);
    }
    part[s->n][i] = (float)rest;
  }
  for (int k = 0; k <= s->n; k++) rab_process(s->part[k], part[k], (unsigned)n);
  for (int i = 0; i < n; i++) {
    double sum = 0;
    for (int k = 0; k <= s->n; k++) sum += part[k][i];
    x[i] = (float)sum;
  }
}
static void split_end(void *state) {
  split *s = state;
  for (int i = 0; i <= s->n; i++) rab_free(s->part[i]);
  free(s);
}
#endif

static const shifter g_shifter[] = {
    {"splice", splice_make, splice_run, splice_end, splice_lag, 1},
    {"splice-e", eager_make, splice_run, splice_end, splice_lag, 1},
    {"bands", bands_make, bands_run, bands_end},
#ifdef HAVE_RAB
    {"rab", rab_make, rab_run, rab_free, rab_delay},
    {"rab-split", split_make, split_run, split_end},
#endif
    {"two-tap", twotap_make, twotap_run, free},
};
#define SHIFTERS (int)(sizeof g_shifter / sizeof *g_shifter)

static double g_seconds[SHIFTERS], g_audio[SHIFTERS];
static double g_held, g_most;  // the last run's delay by the shifter's own account, in ms: the mean and the most
static float g_trace[1 << 16];  // and block by block, in frames

static int g_verbose;       // -v
static double g_level = 0.5;  // -l
static const char *g_only;  // BENCH_ONLY: the one shifter to run; the others' rows are then of the input unshifted

static void shift(int which, double st, const float *in, float *out, int frames) {
  const shifter *s = &g_shifter[which];
  if (g_only && *g_only && strcmp(g_only, s->name)) {
    memcpy(out, in, (size_t)frames * sizeof *in);
    g_held = g_most = NAN;
    return;
  }
  void *state = s->make(st);
  memcpy(out, in, (size_t)frames * sizeof *in);
  clock_t a = clock();
  double sum = 0, most = 0;
  int blocks = 0;
  for (int at = 0; at < frames; at += BLOCK) {
    s->run(state, out + at, frames - at < BLOCK ? frames - at : BLOCK);
    if (s->lag && at >= (int)(T0 * SR)) {
      double lag = s->lag(state);
      sum += lag, most = fmax(most, lag), blocks++;
      if (at / BLOCK < 1 << 16) g_trace[at / BLOCK] = (float)lag;
    }
  }
  g_held = blocks ? sum / blocks / SR * 1e3 : NAN;
  g_most = blocks ? most / SR * 1e3 : NAN;
  g_seconds[which] += (double)(clock() - a) / CLOCKS_PER_SEC;
  g_audio[which] += frames / SR;
  s->end(state);
}

// ------------------------------------------------------------------------------------------------
// The measures
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

// |spectrum| of n samples of x from `at` under a window: 0 Hann, 1 Blackman-Harris (zeros past the end)
static void spectrum(const float *x, int frames, int at, int n, int window, double *mag) {
  static double re[NFFT], im[NFFT];
  for (int i = 0; i < n; i++) {
    double p = 2 * M_PI * i / n;
    double w = window ? 0.35875 - 0.48829 * cos(p) + 0.14128 * cos(2 * p) - 0.01168 * cos(3 * p) : 0.5 - 0.5 * cos(p);
    re[i] = at + i >= 0 && at + i < frames ? x[at + i] * w : 0;
    im[i] = 0;
  }
  fft(re, im, n);
  for (int i = 0; i <= n / 2; i++) mag[i] = hypot(re[i], im[i]);
}

// The level over time: the root of the mean square of `width` samples around each
static void level(const float *x, int frames, int width, double *env) {
  double sum = 0;
  int half = width / 2;
  for (int i = -half; i < frames + half; i++) {
    if (i + half < frames) sum += (double)x[i + half] * x[i + half];
    if (i - half - 1 >= 0) sum -= (double)x[i - half - 1] * x[i - half - 1];
    if (i >= 0 && i < frames) env[i] = sqrt(fmax(sum, 0) / width);
  }
}

// How many samples `got` lags `want`: the shift that makes their levels match best around the first note
static int lag_of(const double *got, const double *want, int frames, int from, int to, int most) {
  int best = 0;
  double best_score = -1;
  if (to > frames - most) to = frames - most;
  for (int lag = -most / 8; lag <= most; lag++) {
    double dot = 0, a = 0, b = 0;
    for (int i = from; i < to; i++) {
      if (i + lag < 0) continue;
      dot += got[i + lag] * want[i];
      a += got[i + lag] * got[i + lag];
      b += want[i] * want[i];
    }
    double score = a > 0 && b > 0 ? dot / sqrt(a * b) : 0;
    if (score > best_score) best_score = score, best = lag;
  }
  return best;
}

// The first sample from `from` on where the level passes a quarter of its most within `span`
static int rise(const double *env, int frames, int from, int span) {
  double peak = 0;
  for (int i = from; i < from + span && i < frames; i++)
    if (env[i] > peak) peak = env[i];
  for (int i = from > 240 ? from - 240 : 0; i < frames; i++)
    if (env[i] > 0.25 * peak) return i;
  return frames;
}

static void drive(const float *x, float *y, int frames, double peak) {
  for (int i = 0; i < frames; i++) y[i] = (float)(0.5 * tanh(20 * x[i] / peak));
}

// One overtone over time: x times e^-i2pi hz t through three 40 ms averages (which let through about 10 Hz either
// side and nothing at multiples of 25 Hz), its value every DHOP samples. The same for what is measured and for the
// ideal, so the averages' own delay drops out.
#define DHOP 240
#define DLEN 1920
#define MAXFRAMES 192000
static int follow(const float *x, int frames, double hz, double *re, double *im) {
  static double ar[MAXFRAMES + 1], ai[MAXFRAMES + 1], br[MAXFRAMES], bi[MAXFRAMES];
  double w = 2 * M_PI * hz / SR, cw = cos(w), sw = -sin(w), or_ = 1, oi = 0;
  for (int i = 0; i < frames; i++) {
    br[i] = x[i] * or_, bi[i] = x[i] * oi;
    double t = or_ * cw - oi * sw;
    oi = or_ * sw + oi * cw, or_ = t;
  }
  for (int pass = 0; pass < 3; pass++) {
    ar[0] = ai[0] = 0;
    for (int i = 0; i < frames; i++) ar[i + 1] = ar[i] + br[i], ai[i + 1] = ai[i] + bi[i];
    for (int i = 0; i < frames; i++) {
      int from = i + 1 - DLEN < 0 ? 0 : i + 1 - DLEN;
      br[i] = (ar[i + 1] - ar[from]) / DLEN, bi[i] = (ai[i + 1] - ai[from]) / DLEN;
    }
  }
  int count = frames / DHOP;
  for (int i = 0; i < count; i++) re[i] = br[i * DHOP], im[i] = bi[i * DHOP];
  return count;
}

// Where (in hops, with a fraction) the overtone first reaches half of its most
static double half_rise(const double *re, const double *im, int count) {
  double most = 0, before = 0;
  for (int i = 0; i < count; i++) most = fmax(most, hypot(re[i], im[i]));
  for (int i = 0; i < count; i++) {
    double a = hypot(re[i], im[i]);
    if (a >= 0.5 * most) return i ? i - 1 + (0.5 * most - before) / (a - before) : 0;
    before = a;
  }
  return count;
}

static int by_value(const void *a, const void *b) { return (*(const double *)a > *(const double *)b) - (*(const double *)a < *(const double *)b); }
static double median(double *v, int n) {
  if (!n) return NAN;
  qsort(v, (size_t)n, sizeof *v, by_value);
  return n & 1 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

enum { ATTACK, LOW, MID, LAST, HELD, MOST, TUNE, JUNK, WAVER, DIFFER, LEFT, MEASURES };
typedef struct {
  double v[MEASURES];  // NAN: not measured in this case
} measures;

static double g_ratio = 1;  // the shift being measured
static measures measure(const tcase *c, const float *got, const float *want, int frames) {
  measures m;
  for (int i = 0; i < MEASURES; i++) m.v[i] = NAN;
  double *eg = malloc((size_t)frames * sizeof *eg), *ew = malloc((size_t)frames * sizeof *ew);
  int t0 = (int)(T0 * SR);
  double last = 0;
  for (int i = 0; i < c->n; i++) last = fmax(last, c->note[i].at);

  level(got, frames, 48, eg);
  level(want, frames, 48, ew);
  m.v[ATTACK] = (rise(eg, frames, t0, 5760) - rise(ew, frames, t0, 2880)) / SR * 1e3;
  int lag = lag_of(eg, ew, frames, t0 - 480, t0 + 9600, 3840);
  if (lag < 0) lag = 0;

  static double mg[NFFT / 2 + 1], mw[NFFT / 2 + 1];
  if (c->steady) {
    static double gr[MAXFRAMES / DHOP], gi[MAXFRAMES / DHOP], wr[MAXFRAMES / DHOP], wi[MAXFRAMES / DHOP];
    double loud = 0, low[512], mid[512], late_last[512], waver = 0, weight = 0, tune = 0;
    // The level is followed from the first notes' start on, so through the start of a later note over them
    int lows = 0, mids = 0, lasts = 0, from = (int)((c->note[0].at + 0.05 + STEADY + 0.06) * SR) / DHOP;
    int from_last = (int)((last + STEADY + 0.06) * SR) / DHOP;
    for (int i = 0; i < g_parts; i++) loud = fmax(loud, g_part[i].amp);
    for (int i = 0; i < g_parts; i++) {
      const partial *p = &g_part[i];
      if (p->hz > 4000 || p->amp < 0.03 * loud) continue;
      double near = 1e9;
      for (int j = 0; j < g_parts; j++)
        if (j != i && g_part[j].amp > 0.1 * p->amp) near = fmin(near, fabs(g_part[j].hz - p->hz));
      if (near < 30) continue;  // not by itself: the averages would follow two at once
      int count = follow(got, frames, p->hz, gr, gi);
      follow(want, frames, p->hz, wr, wi);
      double late = half_rise(gr, gi, count) - half_rise(wr, wi, count);
      if (p->last) {
        if (lasts < 512) late_last[lasts++] = late * DHOP / SR * 1e3;
      } else if (p->hz < 300 && lows < 512)
        low[lows++] = late * DHOP / SR * 1e3;
      else if (p->hz < 1500 && mids < 512)
        mid[mids++] = late * DHOP / SR * 1e3;
      int by = (int)lround(late), n = 0;
      if (by < 0) by = 0;
      int begin = p->last ? from_last : from;
      double start = hypot(wr[begin], wi[begin]), sum = 0, sq = 0, tr = 0, ti = 0;
      for (int t = begin; t + by + 1 < count - 20; t++) {
        double a = hypot(wr[t], wi[t]), b = hypot(gr[t + by], gi[t + by]);
        if (a < 0.1 * start) break;
        double db = 20 * log10((b + 1e-9) / (a + 1e-9));
        sum += db, sq += db * db, n++;
        tr += gr[t + by + 1] * gr[t + by] + gi[t + by + 1] * gi[t + by];
        ti += gi[t + by + 1] * gr[t + by] - gr[t + by + 1] * gi[t + by];
      }
      if (n < 10) continue;
      waver += p->amp * sqrt(fmax(sq / n - sum / n * sum / n, 0));
      weight += p->amp;
      double hz = p->hz + atan2(ti, tr) * SR / (2 * M_PI * DHOP);
      if (p->hz < 2000) tune = fmax(tune, fabs(1200 * log2(hz / p->hz)));
    }
    m.v[LOW] = median(low, lows);
    m.v[MID] = median(mid, mids);
    m.v[LAST] = median(late_last, lasts);
    if (weight > 0) m.v[WAVER] = waver / weight, m.v[TUNE] = tune;

    double bin = SR / NFFT;
    static unsigned char place[NFFT / 2 + 1];
    memset(place, 0, sizeof place);
    for (int i = 0; i < g_parts; i++) {
      int k = (int)lround(g_part[i].hz / bin);
      for (int j = k - 6; j <= k + 6; j++)
        if (j >= 0 && j <= NFFT / 2) place[j] = 1;
    }
    // Two stretches, one after the other: a shifter that makes its first seam late is not let off
    m.v[JUNK] = 0;
    for (int w = 0; w < 2; w++) {
      double all = 1e-30, off = 1e-30;
      spectrum(got, frames, (int)((last + STEADY) * SR) + lag + w * NFFT, NFFT, 1, mg);
      for (int k = (int)(40 / bin); k < (int)(10000 / bin); k++) {
        all += mg[k] * mg[k];
        if (!place[k]) off += mg[k] * mg[k];
      }
      m.v[JUNK] += 10 * log10(off / all) / 2;
    }
  }

  double most = 0, sum = 0;
  int cells = 0;
  for (int pass = 0; pass < 2; pass++)
    for (int at = t0 - 960; at + 2048 < frames - lag; at += 512) {
      spectrum(want, frames, at, 2048, 0, mw);
      if (!pass) {
        for (int k = 2; k < 1024; k++) most = fmax(most, mw[k]);
        continue;
      }
      spectrum(got, frames, at + lag, 2048, 0, mg);
      for (int k = 2; k < 1024; k++) {
        if (mw[k] < most * 0.00316) continue;
        sum += fabs(20 * log10((mg[k] + most * 0.000316) / (mw[k] + most * 0.000316)));
        cells++;
      }
    }
  if (cells) m.v[DIFFER] = sum / cells;
  if (c->left == 1) {
    // The first note's first overtone, under a window that keeps what is 40 Hz away out: from 100 to 50 ms before
    // the second note starts, and from 30 to 80 ms after
    double w = 2 * M_PI * 440 * pow(2, (c->note[0].midi - 69) / 12) * sqrt(1 + STIFF) * g_ratio / SR, level[2];
    for (int k = 0; k < 2; k++) {
      int from = (int)(c->note[1].at * SR) + (k ? 1440 : -4800);
      double re = 0, im = 0, all = 0;
      for (int t = 0; t < 2400; t++) {
        double win = 0.5 - 0.5 * cos(2 * M_PI * (t + 0.5) / 2400);
        re += win * got[from + t] * cos(w * t), im += win * got[from + t] * sin(w * t), all += win;
      }
      level[k] = hypot(re, im) / all;
    }
    m.v[LEFT] = 20 * log10((level[1] + 1e-9) / (level[0] + 1e-9));
  }
  if (c->left == 2) {
    m.v[LEFT] = -200;
    for (int i = 0; i < c->n; i++) {
      int stop = (int)((c->note[i].at + c->note[i].len) * SR);
      double before = 1e-30, after = 1e-30;
      for (int t = stop - 2880; t < stop; t++) before += (double)got[t] * got[t] / 2880;
      for (int t = stop + 3360; t < stop + 5280 && t < frames; t++) after += (double)got[t] * got[t] / 1920;
      m.v[LEFT] = fmax(m.v[LEFT], 10 * log10(after / before));
    }
  }
  free(eg), free(ew);
  return m;
}

// ------------------------------------------------------------------------------------------------
// A shifter against the true thing on a recording. The true thing: the recording played back slower (every sound is
// then lower by the shift, and nothing else is changed but its length). A moment of what the shifter made is held
// against the same moment there, spectrum against spectrum, in windows of 85 ms, each first driven hard by itself as
// an amplifier does, which brings up what is quiet: a note sounded on, a string under the played note left at its own
// pitch. *extra: the power of what the shifter's sound has and the true one has not (a bin 10 dB over the true one's
// loudest within 25 Hz), against all of it; *lost: the same the other way round. `lag`: where the shifter says it
// is, block by block, in frames (g_trace); without it the shifter is taken to be 15 ms behind.
// ------------------------------------------------------------------------------------------------
#define TW 4096
// in at the place p, between samples: a sinc of 32 samples under a window, cut a little under half the rate, from a
// table of 256 places between two samples
static double between(const float *in, int frames, double p) {
  static float table[257][32];
  static int made;
  if (!made) {
    for (int f = 0; f <= 256; f++)
      for (int j = -15; j <= 16; j++) {
        double x = j - f / 256.0, s = fabs(x) < 1e-9 ? 0.92 : sin(M_PI * 0.92 * x) / (M_PI * x), w = cos(M_PI * x / 33);
        table[f][j + 15] = (float)(s * w * w);
      }
    made = 1;
  }
  int i0 = (int)floor(p), f = (int)lround((p - i0) * 256);
  if (i0 < 15 || i0 + 16 >= frames) return 0;
  double sum = 0;
  for (int j = 0; j < 32; j++) sum += in[i0 - 15 + j] * table[f][j];
  return sum;
}
static void against_true(const float *in, const float *got, int frames, double ratio, const float *lag, double *extra, double *lost, double *all_got, double *all_true) {
  static double mg[TW / 2 + 1], mt[TW / 2 + 1];
  static float sg[TW], st[TW];
  double top = 0;
  for (int i = 0; i < frames; i++) top = fmax(top, fabs(in[i]));
  for (int t = 4800; t + TW + 4800 < frames; t += TW / 2) {
    int block = (t + TW / 2) / BLOCK;
    double from = t + TW / 2 - (lag && block < 1 << 16 ? lag[block] : 0.015 * SR), pg = 1e-9, pt = 1e-9;
    for (int i = 0; i < TW; i++) {
      sg[i] = got[t + i], st[i] = (float)between(in, frames, from + (i - TW / 2) * ratio);
      pg = fmax(pg, fabs(sg[i])), pt = fmax(pt, fabs(st[i]));
    }
    if (pt < 0.02 * top) continue;  // nothing is playing
    for (int i = 0; i < TW; i++) sg[i] = (float)tanh(30 * sg[i] / pg), st[i] = (float)tanh(30 * st[i] / pt);
    spectrum(sg, TW, 0, TW, 1, mg);
    spectrum(st, TW, 0, TW, 1, mt);
    for (int k = 4; k < (int)(6000 * TW / SR); k++) {
      double near_t = 0, near_g = 0;
      for (int j = k - 2; j <= k + 2; j++) near_t = fmax(near_t, mt[j]), near_g = fmax(near_g, mg[j]);
      *all_got += mg[k] * mg[k], *all_true += mt[k] * mt[k];
      if (mg[k] > 3.16 * near_t) *extra += mg[k] * mg[k];
      if (mt[k] > 3.16 * near_g) *lost += mt[k] * mt[k];
    }
  }
}

// ------------------------------------------------------------------------------------------------
static void put_wav(const char *dir, const char *name, int st, const char *kind, const float *x, int frames) {
  char path[1024];
  snprintf(path, sizeof path, "%s/%+d-%s-%s.wav", dir, st, name, kind);
  FILE *f = fopen(path, "wb");
  if (!f) {
    perror(path);
    return;
  }
  uint32_t bytes = (uint32_t)frames * 2, rate = (uint32_t)SR;
  uint32_t head[11] = {0x46464952, 36 + bytes, 0x45564157, 0x20746d66, 16, 0x00010001, rate, rate * 2, 0x00100002, 0x61746164, bytes};
  fwrite(head, 4, 11, f);
  for (int i = 0; i < frames; i++) {
    double v = x[i] * 32767;
    int16_t s = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    fwrite(&s, 2, 1, f);
  }
  fclose(f);
}

// The bands alone, at 0 semitones: a click through them, and how flat and how late it comes out
static void identity(void) {
  ps_params p = g_params;
  p.at_zero = 1;
  ps *s = ps_new(SR, &p);
  static float x[NFFT];
  static double mag[NFFT / 2 + 1];
  memset(x, 0, sizeof x);
  x[0] = 1;
  ps_process(s, x, x, NFFT);
  printf("bands at 0 semitones: %d bands\n", ps_bands(s));
  static double re[NFFT], im[NFFT];
  for (int i = 0; i < NFFT; i++) re[i] = x[i], im[i] = 0;
  fft(re, im, NFFT);
  for (int i = 0; i <= NFFT / 2; i++) mag[i] = hypot(re[i], im[i]);
  printf("  %-16s %8s %8s %9s\n", "", "least dB", "most dB", "late ms");
  for (double lo = 70; lo < 9000; lo *= 2) {
    double least = 1e9, most = -1e9, num = 0, den = 0;
    for (int k = (int)(lo * NFFT / SR); k < (int)(2 * lo * NFFT / SR); k++) {
      double db = 20 * log10(mag[k] + 1e-12);
      least = fmin(least, db), most = fmax(most, db);
      // the delay at this frequency: how fast the phase falls from bin to bin
      double pr = re[k] * re[k + 1] + im[k] * im[k + 1], pi = im[k + 1] * re[k] - re[k + 1] * im[k], w = mag[k] * mag[k + 1];
      num += -atan2(pi, pr) * w;
      den += w;
    }
    printf("  %5.0f-%5.0f Hz   %8.1f %8.1f %9.1f\n", lo, 2 * lo, least, most, num / den * NFFT / (2 * M_PI) / SR * 1e3);
  }
  ps_free(s);
}

// ------------------------------------------------------------------------------------------------
// Recorded guitar (-r): how late a note's start comes out, at every start that follows a quiet moment; and, where the
// notes then ring on for 0.85 s with nothing new starting, waver, tune and junk as above, with the recording's own
// overtones standing in for the ideal (ringing_of). And for a recording with its notes beside it (the same name,
// .mid), how late the notes come that start while others ring ("over"): those the level does not show; and junk and
// waver of the stretches that ring on by the pitch of the note that starts them, an octave to a row, which is where
// a shifter that is good low down and poor high up shows. And all of each recording against the true thing
// (against_true): "extra" and "lost". The rest is for listening (-w).
// ------------------------------------------------------------------------------------------------
static double g_from, g_length = 60;  // the stretch of each recording that is used, in seconds (-f, -t)

// A .wav of 48 kHz mono, 16-bit or 32-bit float (prepare.sh makes those); NULL for anything else
static float *read_wav(const char *path, int *frames) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  uint8_t h[12], c[8], b[40];
  float *x = NULL;
  unsigned format = 0, bits = 0, channels = 0, rate = 0;
  if (fread(h, 1, 12, f) == 12 && !memcmp(h, "RIFF", 4) && !memcmp(h + 8, "WAVE", 4))
    while (fread(c, 1, 8, f) == 8) {
      uint32_t n = c[4] | c[5] << 8 | c[6] << 16 | (uint32_t)c[7] << 24;
      if (!memcmp(c, "fmt ", 4)) {
        uint32_t take = n < 40 ? n : 40;
        memset(b, 0, sizeof b);
        if (fread(b, 1, take, f) != take) break;
        format = b[0] | b[1] << 8, channels = b[2] | b[3] << 8, bits = b[14] | b[15] << 8;
        rate = b[4] | b[5] << 8 | b[6] << 16 | (uint32_t)b[7] << 24;
        if (format == 0xfffe) format = b[24] | b[25] << 8;  // the extensible form: the real one is further in
        fseek(f, (long)(n - take + (n & 1)), SEEK_CUR);
      } else if (!memcmp(c, "data", 4)) {
        int size = bits / 8;
        if (channels != 1 || rate != 48000 || !((format == 1 && bits == 16) || (format == 3 && bits == 32))) break;
        int count = (int)(n / (uint32_t)size), skip = (int)(g_from * 48000);
        if (skip >= count) break;
        fseek(f, (long)skip * size, SEEK_CUR);
        count -= skip;
        if (count > (int)(g_length * 48000)) count = (int)(g_length * 48000);
        x = malloc((size_t)count * sizeof *x);
        for (int i = 0; i < count; i++) {
          uint8_t v[4];
          if (fread(v, 1, (size_t)size, f) != (size_t)size) {
            count = i;
            break;
          }
          if (size == 2)
            x[i] = (int16_t)(v[0] | v[1] << 8) / 32768.f;
          else
            memcpy(&x[i], v, 4);
        }
        *frames = count;
        break;
      } else
        fseek(f, (long)(n + (n & 1)), SEEK_CUR);
    }
  fclose(f);
  return x;
}

// The notes of a standard MIDI file: when each starts, in seconds, and its pitch. For a recording that comes with
// one (the same name, .mid): every note is in it, also those that start while others ring, which the level alone
// does not show.
typedef struct {
  double at;
  int pitch;
} midi_note;

static int read_midi(const char *path, midi_note *note, int most) {
  FILE *f = fopen(path, "rb");
  if (!f) return 0;
  static uint8_t d[1 << 20];
  static struct {
    long tick;
    double us;
  } tempo[256];
  static long tick_of[8192];
  size_t len = fread(d, 1, sizeof d, f), p = 14;
  fclose(f);
  if (len < 14 || memcmp(d, "MThd", 4)) return 0;
  int division = d[12] << 8 | d[13], tempos = 0, notes = 0;
  while (p + 8 <= len && !memcmp(d + p, "MTrk", 4)) {
    size_t end = p + 8 + ((size_t)d[p + 4] << 24 | d[p + 5] << 16 | d[p + 6] << 8 | d[p + 7]);
    long tick = 0;
    int status = 0;
    for (p += 8; p < end && p < len;) {
      long delta = 0;
      while (p < len && d[p] & 0x80) delta = delta << 7 | (d[p++] & 0x7f);
      delta = delta << 7 | d[p++];
      tick += delta;
      if (d[p] & 0x80) status = d[p++];
      if (status == 0xff) {  // a meta event: its kind, its length, its bytes
        int kind = d[p++], n = 0;
        while (d[p] & 0x80) n = n << 7 | (d[p++] & 0x7f);
        n = n << 7 | d[p++];
        if (kind == 0x51 && n == 3 && tempos < 256) tempo[tempos].tick = tick, tempo[tempos++].us = d[p] << 16 | d[p + 1] << 8 | d[p + 2];
        p += (size_t)n;
      } else if (status == 0xf0 || status == 0xf7) {
        int n = 0;
        while (d[p] & 0x80) n = n << 7 | (d[p++] & 0x7f);
        n = n << 7 | d[p++];
        p += (size_t)n;
      } else {
        int kind = status & 0xf0, two = kind != 0xc0 && kind != 0xd0;
        if (kind == 0x90 && d[p + 1] > 0 && notes < most && notes < 8192) tick_of[notes] = tick, note[notes++].pitch = d[p];
        p += two ? 2 : 1;
      }
    }
    p = end;
  }
  for (int i = 0; i < notes; i++) {  // ticks to seconds, through the tempo changes before the note
    double at = 0, us = 500000;
    long from = 0;
    for (int t = 0; t < tempos && tempo[t].tick <= tick_of[i]; t++) {
      at += (tempo[t].tick - from) * us / division / 1e6;
      from = tempo[t].tick, us = tempo[t].us;
    }
    note[i].at = at + (tick_of[i] - from) * us / division / 1e6;
  }
  return notes;
}

// How many ms later a note comes up in `got` than in `in`: its first overtone (or the second, if that is the louder)
// followed in both around its start, each one's rise to half of what it reaches. NAN when the overtone was already
// there before the note (the same pitch ringing), so that nothing rises.
static double note_late(const float *in, const float *got, int frames, double at, int pitch, double ratio) {
  static double ir[MAXFRAMES / DHOP], ii[MAXFRAMES / DHOP], gr[MAXFRAMES / DHOP], gi[MAXFRAMES / DHOP];
  int from = (int)((at - 0.2) * SR), len = (int)(0.5 * SR), start = (int)(0.2 * SR) / DHOP;
  if (from < 0 || from + len > frames) return NAN;
  double hz = 440 * pow(2, (pitch - 69) / 12.0), best = NAN;
  for (int h = 1; h <= 2; h++) {
    int count = follow(in + from, len, hz * h, ir, ii);
    follow(got + from, len, hz * h * ratio, gr, gi);
    double most_in = 0, most_got = 0, before = hypot(ir[start + 4], ii[start + 4]), hi = count, hg = count, prev = 0;
    for (int t = start; t < count; t++) most_in = fmax(most_in, hypot(ir[t], ii[t])), most_got = fmax(most_got, hypot(gr[t], gi[t]));
    if (most_in < 2e-3 || before > 0.35 * most_in) continue;
    for (int t = start - 4; t < count; t++) {
      double a = hypot(ir[t], ii[t]);
      if (a >= 0.5 * most_in) {
        hi = t - 1 + (0.5 * most_in - prev) / (a - prev);
        break;
      }
      prev = a;
    }
    prev = 0;
    for (int t = start - 4; t < count; t++) {
      double a = hypot(gr[t], gi[t]);
      if (a >= 0.5 * most_got) {
        hg = t - 1 + (0.5 * most_got - prev) / (a - prev);
        break;
      }
      prev = a;
    }
    if (hi < count && hg < count && (isnan(best) || h == 1)) best = (hg - hi) * DHOP / SR * 1e3;
    if (!isnan(best) && h == 1) break;
  }
  return best;
}

// The starts: where the level passes a quarter of the most it reaches in the next 30 ms, having been under that from
// 15 to 5 ms before, and that most is a tenth of the recording's loudest or more
static int starts_of(const double *env, int frames, int *at, int most) {
  double loudest = 0;
  int n = 0;
  for (int i = 0; i < frames; i++) loudest = fmax(loudest, env[i]);
  for (int i = 720; i + 1440 < frames && n < most; i++) {
    if (env[i] < 0.025 * loudest || env[i] <= env[i - 1]) continue;
    double after = 0, before = 0;
    for (int j = i; j < i + 1440; j += 12) after = fmax(after, env[j]);
    if (after < 0.1 * loudest || env[i] < 0.25 * after || env[i - 1] >= 0.25 * after) continue;
    for (int j = i - 720; j < i - 240; j += 12) before = fmax(before, env[j]);
    if (before >= 0.25 * after) continue;
    at[n++] = i;
    i += 4800;
  }
  return n;
}

// A stretch of a recording where notes ring and nothing new starts: its overtones, found in the recording itself.
// The shifted sound should have each of them at ratio times its frequency, with the level the recording's own has
// over time: so the recording stands in for the ideal, overtone by overtone.
#define RING_FROM 0.05   // seconds after the start where the stretch begins
#define RING_LEN 0.85
typedef struct {
  int ok, strong, all;
  double hz[24], amp[24];  // the loud ones with no other within 30 Hz: followed one by one
  double place[400];       // every peak: where sound is expected
  double junk;             // dB of the recording's own sound that is at none of them
} ringing;

static double off_places(const double *mag, const double *place, int n, double ratio) {
  static unsigned char at[NFFT / 2 + 1];
  double bin = SR / NFFT, all = 1e-30, off = 1e-30;
  memset(at, 0, sizeof at);
  for (int i = 0; i < n; i++) {
    int k = (int)lround(place[i] * ratio / bin);
    for (int j = k - 6; j <= k + 6; j++)
      if (j >= 0 && j <= NFFT / 2) at[j] = 1;
  }
  for (int k = (int)(60 / bin); k < (int)(8000 / bin); k++) {
    all += mag[k] * mag[k];
    if (!at[k]) off += mag[k] * mag[k];
  }
  return 10 * log10(off / all);
}

static ringing ringing_of(const float *in, const double *env, int frames, int start, int next) {
  static double mag[NFFT / 2 + 1], re[MAXFRAMES / DHOP], im[MAXFRAMES / DHOP];
  ringing r = {0};
  int from = start + (int)(RING_FROM * SR), len = (int)(RING_LEN * SR);
  if (from + len + 2 * NFFT > frames || next < from + len) return r;
  for (int i = from + 2400; i < from + len; i += 48)
    if (env[i] > 1.25 * env[from + 2400]) return r;  // something new starts in it (env: the level over 20 ms)
  double bin = SR / NFFT, top = 0;
  spectrum(in, frames, from + 4800, NFFT, 1, mag);
  for (int k = (int)(60 / bin); k < (int)(8000 / bin); k++) top = fmax(top, mag[k]);
  for (int k = (int)(60 / bin); k < (int)(8000 / bin) && r.all < 400; k++) {
    if (mag[k] < 0.001 * top) continue;
    int peak = 1;
    for (int j = k - 3; j <= k + 3; j++) peak &= mag[k] >= mag[j];
    if (!peak) continue;
    double a = log(mag[k - 1] + 1e-30), b = log(mag[k]), c = log(mag[k + 1] + 1e-30);
    r.place[r.all++] = (k + 0.5 * (a - c) / (a - 2 * b + c)) * bin;
  }
  r.junk = off_places(mag, r.place, r.all, 1);
  for (int i = 0; i < r.all && r.strong < 24; i++) {
    int k = (int)lround(r.place[i] / bin);
    if (r.place[i] < 70 || r.place[i] > 4000 || mag[k] < 0.03 * top) continue;
    int alone = 1;
    for (int j = 0; j < r.all; j++)
      alone &= j == i || fabs(r.place[j] - r.place[i]) >= 30 || mag[(int)lround(r.place[j] / bin)] < 0.1 * mag[k];
    if (!alone) continue;
    // Its frequency, better than the spectrum's bins give it: how fast its phase turns against the first guess
    int count = follow(in + from, len, r.place[i], re, im);
    double tr = 0, ti = 0;
    for (int t = 30; t + 1 < count; t++) tr += re[t + 1] * re[t] + im[t + 1] * im[t], ti += im[t + 1] * re[t] - re[t + 1] * im[t];
    r.hz[r.strong] = r.place[i] + atan2(ti, tr) * SR / (2 * M_PI * DHOP);
    r.amp[r.strong++] = mag[k];
  }
  r.ok = r.strong > 0;
  return r;
}

// How far v[0..n) is from the straight line through it: the root of the mean square
static double off_line(const double *v, int n) {
  double sx = 0, sy = 0, sxx = 0, sxy = 0, sq = 0;
  for (int i = 0; i < n; i++) sx += i, sy += v[i], sxx += (double)i * i, sxy += i * v[i];
  double slope = (n * sxy - sx * sy) / (n * sxx - sx * sx), base = (sy - slope * sx) / n;
  for (int i = 0; i < n; i++) sq += (v[i] - base - slope * i) * (v[i] - base - slope * i);
  return sqrt(sq / n);
}

// The shifted sound over the same stretch: *waver dB its overtones' levels move about against the recording's,
// tune[] each one's cents off its place (returns how many), *junk dB of it at none of the places
static int ringing_in(const ringing *r, const float *in, const float *got, int frames, int start, double ratio, double *waver, double *tune, double *junk) {
  static double mag[NFFT / 2 + 1], ir[MAXFRAMES / DHOP], ii[MAXFRAMES / DHOP], gr[MAXFRAMES / DHOP], gi[MAXFRAMES / DHOP];
  int from = start + (int)(RING_FROM * SR), len = (int)(RING_LEN * SR), tunes = 0;
  double sum = 0, weight = 0;
  for (int i = 0; i < r->strong; i++) {
    int count = follow(in + from, len, r->hz[i], ir, ii), n = 0;
    follow(got + from, len, r->hz[i] * ratio, gr, gi);
    double first = hypot(ir[30], ii[30]), tr = 0, ti = 0, own[MAXFRAMES / DHOP], off[MAXFRAMES / DHOP];
    for (int t = 30; t + 1 < count; t++) {
      double a = hypot(ir[t], ii[t]), b = hypot(gr[t], gi[t]);
      if (a < 0.1 * first) break;
      own[n] = 20 * log10(a + 1e-9), off[n] = 20 * log10((b + 1e-9) / (a + 1e-9)), n++;
      tr += gr[t + 1] * gr[t] + gi[t + 1] * gi[t], ti += gi[t + 1] * gr[t] - gr[t + 1] * gi[t];
    }
    // Only an overtone that dies away evenly in the recording itself: one that beats there (two strings almost on
    // the same pitch) beats at another rate when shifted, by any shifter. And the two levels' difference less its
    // own slope, which the lag and the decay make.
    if (n < 10 || off_line(own, n) > 0.2) continue;
    sum += r->amp[i] * off_line(off, n);
    weight += r->amp[i];
    double hz = r->hz[i] * ratio;
    tune[tunes++] = fabs(1200 * log2((hz + atan2(ti, tr) * SR / (2 * M_PI * DHOP)) / hz));
  }
  *waver = weight > 0 ? sum / weight : NAN;
  // Two stretches, one after the other: a shifter that makes its first seam late is not let off
  spectrum(got, frames, from + 4800, NFFT, 1, mag);
  *junk = off_places(mag, r->place, r->all, ratio) / 2;
  spectrum(got, frames, from + 4800 + NFFT, NFFT, 1, mag);
  *junk += off_places(mag, r->place, r->all, ratio) / 2;
  return tunes;
}

static void recordings(char **file, int files, const double *shifts, int nshift, const char *dir) {
  enum { MOST = 1024 };
  static int at[MOST];
  static double late[SHIFTERS][8][MOST * 4];
  static int lates[SHIFTERS][8];
  double held[SHIFTERS][8] = {{0}}, most[SHIFTERS][8] = {{0}};
  static double tune[SHIFTERS][8][MOST * 24];
  static ringing ring[MOST];
  int tunes[SHIFTERS][8] = {{0}}, rings = 0, used = 0, starts = 0;
  double waver[SHIFTERS][8] = {{0}}, junk[SHIFTERS][8] = {{0}}, own_junk = 0;
  static midi_note note[4096];
  static double over[SHIFTERS][8][4096];  // how late the notes that start over ringing ones come
  static double behind[SHIFTERS][8][8192];  // how far behind the shifter is when a note starts, any note
  int overs[SHIFTERS][8] = {{0}}, behinds[SHIFTERS][8] = {{0}};
  enum { OCTAVES = 4 };  // from E2, E3, E4 and E5
  static int pitch_of[MOST];
  double by_junk[SHIFTERS][8][OCTAVES] = {{{0}}}, by_waver[SHIFTERS][8][OCTAVES] = {{{0}}};
  int by_n[SHIFTERS][8][OCTAVES] = {{{0}}}, by_wn[SHIFTERS][8][OCTAVES] = {{{0}}}, by_any = 0;
  double extra[SHIFTERS][8] = {{0}}, lost[SHIFTERS][8] = {{0}}, all_got[SHIFTERS][8] = {{0}}, all_true[SHIFTERS][8] = {{0}};
  for (int fi = 0; fi < files; fi++) {
    int frames = 0;
    float *in = read_wav(file[fi], &frames);
    if (!in || frames < 9600) {
      fprintf(stderr, "%s: not a 48 kHz mono 16-bit or float .wav of 0.2 s or more (prepare.sh makes those)\n", file[fi]);
      free(in);
      continue;
    }
    const char *name = strrchr(file[fi], '/');
    name = name ? name + 1 : file[fi];
    char stem[200];
    snprintf(stem, sizeof stem, "%.*s", (int)(strlen(name) > 4 ? strlen(name) - 4 : strlen(name)), name);
    float *got = malloc((size_t)frames * sizeof *got), *driven = malloc((size_t)frames * sizeof *driven);
    double *ei = malloc((size_t)frames * sizeof *ei), *eg = malloc((size_t)frames * sizeof *eg), peak = 0;
    for (int i = 0; i < frames; i++) peak = fmax(peak, fabs(in[i]));
    level(in, frames, 48, ei);
    int n = starts_of(ei, frames, at, MOST);
    used++, starts += n;
    level(in, frames, 960, eg);
    for (int k = 0; k < n; k++) {
      ring[k] = ringing_of(in, eg, frames, at[k], k + 1 < n ? at[k + 1] : frames);
      if (ring[k].ok) rings++, own_junk += ring[k].junk;
    }
    if (dir) {
      put_wav(dir, stem, 0, "input", in, frames);
      drive(in, driven, frames, peak);
      put_wav(dir, stem, 0, "input-driven", driven, frames);
    }
    // The notes that start while something rings as loud as a quarter of what they bring
    char mid[1100];
    snprintf(mid, sizeof mid, "%.*s.mid", (int)(strlen(file[fi]) > 4 ? strlen(file[fi]) - 4 : strlen(file[fi])), file[fi]);
    static midi_note every[4096];
    int notes = read_midi(mid, note, 4096), ringing = 0, all = 0;
    for (int k = 0; k < notes; k++)
      if (note[k].at - g_from > 0.2 && (note[k].at - g_from) * SR < frames - 9600) every[all] = note[k], every[all++].at -= g_from;
    for (int k = 0; k < n; k++) {  // the written note nearest each start, within half a second
      double near = 0.5;
      pitch_of[k] = 0;
      for (int j = 0; j < all; j++)
        if (fabs(every[j].at - at[k] / SR) < near) near = fabs(every[j].at - at[k] / SR), pitch_of[k] = every[j].pitch;
    }
    for (int k = 0; k < notes; k++) {
      int i = (int)((note[k].at - g_from) * SR);
      double before = 0, after = 0;
      if (i < 9600 || i + 14400 > frames) continue;
      for (int j = i - 1440; j < i - 480; j += 12) before = fmax(before, ei[j]);
      for (int j = i; j < i + 2880; j += 12) after = fmax(after, ei[j]);
      if (before < 0.25 * after) continue;
      note[ringing] = note[k];
      note[ringing++].at -= g_from;
    }
    for (int si = 0; si < nshift && si < 8; si++)
      for (int w = 0; w < SHIFTERS; w++) {
        shift(w, shifts[si], in, got, frames);
        if (!(g_only && *g_only && strcmp(g_only, g_shifter[w].name)))  // not for a shifter that was not run
          against_true(in, got, frames, pow(2, shifts[si] / 12), g_shifter[w].lag ? g_trace : NULL, &extra[w][si], &lost[w][si], &all_got[w][si], &all_true[w][si]);
        // By the shifter's own account: the least it is behind from 20 ms before the note is written down to
        // 25 ms after, which lets it notice the note
        for (int k = 0; k < all && g_shifter[w].exact && behinds[w][si] < 8192; k++) {
          double least = 1e9;
          for (int b = (int)((every[k].at - 0.02) * SR) / BLOCK; b <= (int)((every[k].at + 0.025) * SR) / BLOCK; b++)
            if (b >= 0 && b < 1 << 16) least = fmin(least, g_trace[b]);
          if (least > 1e8) continue;  // past where the shifter's account is kept
          behind[w][si][behinds[w][si]++] = least / SR * 1e3;
          if (g_verbose) printf("  %s %+g note %d at %.3f s: behind %.1f ms\n", stem, shifts[si], every[k].pitch, every[k].at + g_from, least / SR * 1e3);
        }
        for (int k = 0; k < ringing && overs[w][si] < 4096; k++) {
          double late = note_late(in, got, frames, note[k].at, note[k].pitch, pow(2, shifts[si] / 12));
          if (!isnan(late)) over[w][si][overs[w][si]++] = late;
        }
        if (!isnan(g_held)) held[w][si] += g_held, most[w][si] = fmax(most[w][si], g_most);
        level(got, frames, 48, eg);
        for (int k = 0; k < n; k++) {
          // The level the recording passes at its start, in what the shifter made: the same level, not a share of
          // what comes after, or a louder note a little later would be taken for this one
          double after = 0;
          for (int j = at[k]; j < at[k] + 1440 && j < frames; j += 12) after = fmax(after, ei[j]);
          int j = at[k] - 240;
          if (eg[j] >= 0.25 * after) continue;  // what rang before is still as loud here: no start to time
          while (j < at[k] + 4800 && j < frames && eg[j] < 0.25 * after) j++;
          if (j >= at[k] + 4800 || j >= frames) continue;
          if (lates[w][si] < MOST * 4) late[w][si][lates[w][si]++] = (j - at[k]) / SR * 1e3;
          if (g_verbose && j - at[k] > 480)
            printf("  %s %+g %s: the start at %.3f s comes %.1f ms late\n", stem, shifts[si], g_shifter[w].name, (at[k] + g_from * SR) / SR, (j - at[k]) / SR * 1e3);
        }
        for (int k = 0; k < n; k++) {
          double wv, jk;
          if (!ring[k].ok || tunes[w][si] + 24 > MOST * 24) continue;
          tunes[w][si] += ringing_in(&ring[k], in, got, frames, at[k], pow(2, shifts[si] / 12), &wv, tune[w][si] + tunes[w][si], &jk);
          if (!isnan(wv)) waver[w][si] += wv;
          junk[w][si] += jk;
          if (pitch_of[k]) {
            int o = pitch_of[k] < 40 ? 0 : pitch_of[k] >= 40 + 12 * OCTAVES ? OCTAVES - 1 : (pitch_of[k] - 40) / 12;
            by_junk[w][si][o] += jk, by_n[w][si][o]++, by_any = 1;
            if (!isnan(wv)) by_waver[w][si][o] += wv, by_wn[w][si][o]++;
          }
        }
        if (dir) {
          char kind[64];
          snprintf(kind, sizeof kind, "%s", g_shifter[w].name);
          put_wav(dir, stem, (int)shifts[si], kind, got, frames);
          drive(got, driven, frames, peak);
          snprintf(kind, sizeof kind, "%s-driven", g_shifter[w].name);
          put_wav(dir, stem, (int)shifts[si], kind, driven, frames);
        }
      }
    free(in), free(got), free(driven), free(ei), free(eg);
  }
  printf("\n%d recordings, %d note starts after a quiet moment, %d of them ringing on undisturbed (the recordings' own junk there: %.1f dB)\n",
         used, starts, rings, rings ? own_junk / rings : 0);
  printf("splice, over all its runs: %.1f jumps a second of %.1f ms, alike %.3f on average, %.0f%% less alike than it asks; %.1f attacks a second\n",
         g_jumps / g_audio[0], g_jumps ? g_jumped / g_jumps / SR * 1e3 : 0, g_jumps ? g_scores / g_jumps : 0,
         g_jumps ? 100.0 * g_poor / g_jumps : 0, g_attacks / g_audio[0]);
  for (int si = 0; si < nshift && si < 8; si++) {
    printf("\n%+g semitones\n    %-10s %7s %7s %7s %7s %7s %7s %7s %7s %7s %7s %11s %5s %7s %7s\n", shifts[si], "", "extra", "lost", "starts", "middle", "9 in 10", "worst", "waver", "tune", "9 in 10", "junk", "held/most", "over", "middle", "9 in 10");
    printf("    %-10s %7s %7s %7s %7s %7s %7s %7s %7s %7s %7s %11s %5s %7s %7s\n", "", "dB", "dB", "timed", "ms", "ms", "ms", "dB", "cents", "cents", "dB", "ms", "", "ms", "ms");
    for (int w = 0; w < SHIFTERS; w++) {
      int n = lates[w][si];
      double *v = late[w][si];
      printf("    %-10s %7.1f %7.1f %7d", g_shifter[w].name, 10 * log10(extra[w][si] / (all_got[w][si] + 1e-30) + 1e-20), 10 * log10(lost[w][si] / (all_true[w][si] + 1e-30) + 1e-20), n);
      if (n) {
        qsort(v, (size_t)n, sizeof *v, by_value);
        printf(" %7.1f %7.1f %7.1f", v[n / 2], v[(int)(0.9 * (n - 1))], v[n - 1]);
      } else
        printf(" %7s %7s %7s", "", "", "");
      int t = tunes[w][si];
      if (rings && t) {
        qsort(tune[w][si], (size_t)t, sizeof(double), by_value);
        printf(" %7.2f %7.1f %7.1f %7.1f", waver[w][si] / rings, tune[w][si][t / 2], tune[w][si][(int)(0.9 * (t - 1))], junk[w][si] / rings);
      } else
        printf(" %7s %7s %7s %7s", "", "", "", "");
      if (g_shifter[w].lag && used)
        printf(" %5.1f/%-5.1f", held[w][si] / used, most[w][si]);
      else
        printf(" %11s", "");
      int o = overs[w][si];
      if (o) {
        qsort(over[w][si], (size_t)o, sizeof(double), by_value);
        printf(" %5d %7.1f %7.1f", o, over[w][si][o / 2], over[w][si][(int)(0.9 * (o - 1))]);
      }
      int b = behinds[w][si];
      if (b) {
        int far = 0;
        qsort(behind[w][si], (size_t)b, sizeof(double), by_value);
        for (int i = 0; i < b; i++) far += behind[w][si][i] > 10;
        printf("   of %d notes: behind %.1f ms in the middle, %.1f for 9 in 10, over 10 ms for %.0f%%", b, behind[w][si][b / 2], behind[w][si][(int)(0.9 * (b - 1))], 100.0 * far / b);
      }
      printf("\n");
    }
    if (by_any) {
      printf("    the notes that ring on, by the pitch written for them: junk dB, waver dB (how many)\n    %-10s", "");
      for (int o = 0; o < OCTAVES; o++) printf(" %22s", (const char *[]){"E2 to D#3", "E3 to D#4", "E4 to D#5", "E5 and up"}[o]);
      printf("\n");
      for (int w = 0; w < SHIFTERS; w++) {
        printf("    %-10s", g_shifter[w].name);
        for (int o = 0; o < OCTAVES; o++)
          if (by_n[w][si][o])
            printf("  %6.1f %5.2f (%3d)   ", by_junk[w][si][o] / by_n[w][si][o], by_wn[w][si][o] ? by_waver[w][si][o] / by_wn[w][si][o] : NAN, by_n[w][si][o]);
          else
            printf(" %22s", "");
        printf("\n");
      }
    }
  }
}

static double env_or(const char *name, double dflt) {
  const char *v = getenv(name);
  return v && *v ? atof(v) : dflt;
}

static void row(const char *who, const measures *m) {
  printf("    %-10s", who);
  for (int i = 0; i < MEASURES; i++)
    if (isnan(m->v[i]))
      printf(" %7s", "");
    else
      printf(i == WAVER || i == DIFFER ? " %7.2f" : " %7.1f", m->v[i]);
  printf("\n");
}

int main(int argc, char **argv) {
  int verbose = 0, only_identity = 0, nshift = 0, first_file = 0;
  double shifts[8];
  const char *shift_list = "-1,-2", *dir = NULL;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-v")) verbose = g_verbose = 1;
    else if (!strcmp(argv[i], "-i")) only_identity = 1;
    else if (!strcmp(argv[i], "-s") && i + 1 < argc) shift_list = argv[++i];
    else if (!strcmp(argv[i], "-w") && i + 1 < argc) dir = argv[++i];
    else if (!strcmp(argv[i], "-l") && i + 1 < argc && atof(argv[i + 1]) > 0) g_level = atof(argv[++i]);
    else if (!strcmp(argv[i], "-f") && i + 1 < argc) g_from = atof(argv[++i]);
    else if (!strcmp(argv[i], "-t") && i + 1 < argc) g_length = atof(argv[++i]);
    else if (!strcmp(argv[i], "-r") && i + 1 < argc) {
      first_file = ++i;  // the rest are recordings
      break;
    }
    else return fprintf(stderr, "usage: bench [-v] [-i] [-s -1,-2] [-l peak] [-w dir] [-f from] [-t seconds] [-r recording.wav ...]\n"), 2;
  }
  for (const char *p = shift_list; *p && nshift < 8;) {
    char *end;
    shifts[nshift++] = strtod(p, &end);
    p = *end ? end + 1 : end;
  }
  ps_defaults(&g_params);
  g_params.low = env_or("PS_LOW", g_params.low);
  g_params.high = env_or("PS_HIGH", g_params.high);
  g_params.min_width = env_or("PS_MIN_WIDTH", g_params.min_width);
  g_params.per_octave = env_or("PS_PER_OCTAVE", g_params.per_octave);
  g_params.poles = (int)env_or("PS_POLES", g_params.poles);
  g_params.width = env_or("PS_WIDTH", g_params.width);
  g_params.lock_ms = env_or("PS_LOCK_MS", g_params.lock_ms);
  g_params.track = env_or("PS_TRACK", g_params.track);
  sp_defaults(&g_splice);
  g_splice.most_ms = env_or("SP_MOST_MS", g_splice.most_ms);
  g_splice.far_ms = env_or("SP_FAR_MS", g_splice.far_ms);
  g_splice.good = env_or("SP_GOOD", g_splice.good);
  g_splice.look_ms = env_or("SP_LOOK_MS", g_splice.look_ms);
  g_splice.attack = (int)env_or("SP_ATTACK", g_splice.attack);
  g_splice.louder = env_or("SP_LOUDER", g_splice.louder);
  g_splice.soft = env_or("SP_SOFT", g_splice.soft);
  g_splice.nearly = env_or("SP_NEARLY", g_splice.nearly);
  g_splice.eager = (int)env_or("SP_EAGER", g_splice.eager);
  g_splice.close_ms = env_or("SP_CLOSE_MS", g_splice.close_ms);
  g_splice.patient = (int)env_or("SP_PATIENT", g_splice.patient);
  g_only = getenv("BENCH_ONLY");
  if (first_file) {
    recordings(argv + first_file, argc - first_file, shifts, nshift, dir);
    return 0;
  }
  identity();
  if (only_identity) return 0;
  make_cases();

  enum { WHO = SHIFTERS + 1 };  // the shifters, then the ideal with other phases
  int most = (int)(2.0 * SR);
  float *in = malloc((size_t)most * sizeof *in), *want = malloc((size_t)most * sizeof *want), *got = malloc((size_t)most * sizeof *got);
  float *tour[WHO + 1] = {0};
  int tour_at = 0, tour_len = (int)(g_cases * 2.0 * SR);
  for (int si = 0; si < nshift; si++) {
    double st = shifts[si], ratio = g_ratio = pow(2, st / 12);
    printf("\n%+g semitones\n    %-10s %7s %7s %7s %7s %7s %7s %7s %7s %7s %7s %7s\n", st, "", "attack", "low", "mid", "later", "held", "most", "tune", "junk", "waver", "differ", "left");
    printf("    %-10s %7s %7s %7s %7s %7s %7s %7s %7s %7s %7s %7s\n", "", "ms", "ms", "ms", "ms", "ms", "ms", "cents", "dB", "dB", "dB", "dB");
    if (dir)
      for (int w = 0; w <= WHO; w++) {
        free(tour[w]);
        tour[w] = calloc((size_t)tour_len, sizeof **tour);
      }
    tour_at = 0;
    const char *group = "";
    measures total[WHO];
    int counted[WHO][MEASURES];
    for (int ci = 0; ci <= g_cases; ci++) {
      const tcase *c = ci < g_cases ? &g_case[ci] : NULL;
      if (!c || strcmp(c->group, group)) {
        if (*group) {
          printf("  %s\n", group);
          for (int w = 0; w < WHO; w++) {
            measures m = total[w];
            for (int i = 0; i < MEASURES; i++)
              m.v[i] = !counted[w][i] ? NAN : i == TUNE || i == MOST ? m.v[i] : m.v[i] / counted[w][i];  // the worst of two, the mean of the rest
            row(w < SHIFTERS ? g_shifter[w].name : "ideal", &m);
          }
        }
        if (!c) break;
        group = c->group;
        memset(counted, 0, sizeof counted);
        memset(total, 0, sizeof total);
      }
      int frames = (int)(c->dur * SR);
      double peak = 0;
      render(c, 1, 1, in, frames);
      for (int i = 0; i < frames; i++) peak = fmax(peak, fabs(in[i]));
      for (int i = 0; i < frames; i++) in[i] *= (float)(g_level / peak);
      if (verbose) printf("  %s: %s\n", c->group, c->name);
      for (int w = 0; w < WHO; w++) {
        if (w < SHIFTERS)
          shift(w, st, in, got, frames);
        else {
          render(c, ratio, 2, got, frames);
          for (int i = 0; i < frames; i++) got[i] *= (float)(g_level / peak);
        }
        render(c, ratio, 1, want, frames);  // after the other render: g_part is this one's
        for (int i = 0; i < frames; i++) want[i] *= (float)(g_level / peak);
        measures m = measure(c, got, want, frames);
        if (w < SHIFTERS) m.v[HELD] = g_held, m.v[MOST] = g_most;
        if (verbose) row(w < SHIFTERS ? g_shifter[w].name : "ideal", &m);
        for (int i = 0; i < MEASURES; i++) {
          if (isnan(m.v[i])) continue;
          total[w].v[i] = i == TUNE || i == MOST ? fmax(total[w].v[i], m.v[i]) : total[w].v[i] + m.v[i];
          counted[w][i]++;
        }
        if (dir) memcpy(tour[w] + tour_at, got, (size_t)frames * sizeof *got);
      }
      if (dir) memcpy(tour[WHO] + tour_at, in, (size_t)frames * sizeof *in);
      tour_at += frames + (int)(0.3 * SR);
    }
    if (dir) {
      float *driven = malloc((size_t)tour_at * sizeof *driven);
      for (int w = 0; w <= WHO; w++) {
        const char *name = w < SHIFTERS ? g_shifter[w].name : w == SHIFTERS ? "ideal" : "input";
        for (int i = 0; i < tour_at; i++) tour[w][i] *= (float)(0.5 / g_level);  // written as loud whatever -l is
        put_wav(dir, name, (int)st, "clean", tour[w], tour_at);
        drive(tour[w], driven, tour_at, 0.5);
        put_wav(dir, name, (int)st, "driven", driven, tour_at);
      }
      free(driven);
    }
  }
  printf("\nprocessor time, of the audio's length:");
  for (int w = 0; w < SHIFTERS; w++) printf("  %s %.1f%%", g_shifter[w].name, 100 * g_seconds[w] / g_audio[w]);
  printf("\n");
  return 0;
}
