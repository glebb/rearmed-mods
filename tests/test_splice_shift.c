// The shifter behind Pitch Shifter (pitch/splice_shift.c), without the game: that it shifts by what it is asked, leaves
// the guitar alone at 0 semitones, gives the same samples however the input is cut into buffers, takes its lag back at
// a hammered-on note however quiet the guitar comes in, keeps a high note's lag within a few periods when it is alone
// and not when a quieter note rings under it, lets a note that has stopped be over and a gap after a note stay a gap,
// makes no step at a note's start, and stays finite on silence, noise and a click. How good it sounds is the bench's matter
// (pitch/bench).
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "splice_shift.h"

#define RATE 48000
#define N (RATE * 2)

// A string-like tone: a few overtones of hz, from sample `from` on
static void tone(float *x, double hz, int from) {
  memset(x, 0, N * sizeof *x);
  for (int t = from; t < N; t++)
    for (int k = 1; k <= 5; k++) x[t] += (float)(0.2 / k * sin(2 * M_PI * hz * k * (t - from) / RATE + k));
}

// A note of `from` Hz, and at sample `on` one of `to` Hz in its place: no pick, the old note dies away over `dies`
// seconds as the new one comes up. `level` 1 peaks at about a third.
static void replaced(float *x, double level, int on, double from, double to, double dies) {
  memset(x, 0, N * sizeof *x);
  for (int t = 2400; t < N; t++)
    for (int k = 1; k <= 5; k++) {
      double old = t < on ? 1 : exp(-(t - on) / (dies * RATE));
      double new = t < on ? 0 : t - on < 192 ? 0.5 - 0.5 * cos(M_PI * (t - on) / 192.0) : 1;
      x[t] += (float)(level * 0.2 / k * (old * sin(2 * M_PI * from * k * (t - 2400) / RATE + k) + 0.8 * new * sin(2 * M_PI * to * k * (t - on) / RATE + k)));
    }
}

// How loud x is at hz over the n samples from `from`, under a window that keeps a sound 2 / n of the rate away out
static double level_at(const float *x, double hz, int from, int n) {
  double re = 0, im = 0, all = 0;
  for (int t = 0; t < n; t++) {
    double w = 0.5 - 0.5 * cos(2 * M_PI * (t + 0.5) / n);
    re += w * x[from + t] * cos(2 * M_PI * hz * t / RATE), im += w * x[from + t] * sin(2 * M_PI * hz * t / RATE);
    all += w;
  }
  return 2 * hypot(re, im) / all;
}

static void run(const float *in, float *out, double semitones, int block) {
  sp *s = sp_new(RATE, NULL);
  sp_set_semitones(s, semitones);
  memcpy(out, in, N * sizeof *in);
  for (int at = 0; at < N; at += block) sp_process(s, out + at, out + at, N - at < block ? N - at : block);
  sp_free(s);
}

// How much of x's second half is at hz: its level there by a sum over the samples, against its level overall
static double share_at(const float *x, double hz) {
  double re = 0, im = 0, all = 0;
  for (int t = N / 2; t < N; t++) {
    re += x[t] * cos(2 * M_PI * hz * t / RATE), im += x[t] * sin(2 * M_PI * hz * t / RATE);
    all += (double)x[t] * x[t];
  }
  return 2 * (re * re + im * im) / (N / 2) / all;
}

int main(void) {
  float *in = malloc(N * sizeof *in), *a = malloc(N * sizeof *a), *b = malloc(N * sizeof *b);

  // Two semitones down, and one up: the first overtone, a third of the power as the tone is made, is where it should
  // be and not where it was
  tone(in, 220, 2400);
  run(in, a, -2, 128);
  double down = 220 * pow(2, -2 / 12.0), up = 220 * pow(2, 1 / 12.0);
  CHECK(share_at(in, 220) > 0.6);
  CHECK(share_at(a, down) > 0.5);
  CHECK(share_at(a, 220) < 0.01);
  run(in, b, 1, 128);
  CHECK(share_at(b, up) > 0.5);
  CHECK(share_at(b, 220) < 0.01);

  // Upward a seam takes some of the lag itself, more the larger the shift, and a high note's repeat gives back less
  // than that: several of them are taken then, or the lag would run out and the note come out where it was
  for (int j = 0; j < 4; j++) {
    double hz = (double[]){659.26, 440, 220, 880}[j], st = (double[]){5, 7, 12, 12}[j];
    tone(b, hz, 2400);
    run(b, a, st, 128);
    CHECK(share_at(a, hz * pow(2, st / 12)) > 0.5);
    CHECK(share_at(a, hz) < 0.01);
  }

  // As loud as it went in
  run(in, a, -2, 128);
  double pin = 0, pout = 0;
  for (int t = N / 2; t < N; t++) pin += (double)in[t] * in[t], pout += (double)a[t] * a[t];
  CHECK(pout > 0.8 * pin && pout < 1.25 * pin);

  // The same samples whatever the buffers' size
  run(in, b, -2, 1000);
  CHECK(!memcmp(a, b, N * sizeof *a));
  run(in, b, -2, 7);
  CHECK(!memcmp(a, b, N * sizeof *a));

  // 0 semitones: the guitar itself, once the shifter's first few samples are over
  run(in, a, 0, 128);
  CHECK(!memcmp(a + RATE / 2, in + RATE / 2, (N - RATE / 2) * sizeof *a));

  // Changed while it runs: back to 0 gives the guitar itself again
  sp *s = sp_new(RATE, NULL);
  sp_set_semitones(s, -3);
  memcpy(a, in, N * sizeof *in);
  sp_process(s, a, a, N / 2);
  sp_set_semitones(s, 0);
  sp_process(s, a + N / 2, a + N / 2, N / 2);
  sp_free(s);
  CHECK(!memcmp(a + N - RATE / 4, in + N - RATE / 4, RATE / 4 * sizeof *a));

  // From down to up just after a note started: the pause a start leaves behind under a downward shift doesn't keep
  // the upward one from shifting
  s = sp_new(RATE, NULL);
  sp_set_semitones(s, -1);
  memcpy(a, in, N * sizeof *in);
  int left = 2400 + RATE / 100;  // 10 ms into the note
  sp_process(s, a, a, left);
  sp_set_semitones(s, 0);
  sp_process(s, a + left, a + left, RATE / 100);
  sp_set_semitones(s, 2);
  sp_process(s, a + left + RATE / 100, a + left + RATE / 100, N - left - RATE / 100);
  sp_free(s);
  CHECK(share_at(a, 220 * pow(2, 2 / 12.0)) > 0.5);
  CHECK(share_at(a, 220) < 0.01);

  // A hammered-on note has no pick: it is found by its own range rising, over a level that noise stays under. That
  // level follows the guitar's: a guitar that comes in loud and one that comes in 30 dB quieter both have the lag,
  // long by then (the notes are low ones, which wait for a long repeat), taken back at the note
  for (int quiet = 0; quiet < 2; quiet++) {
    int on = RATE * 3 / 10;
    replaced(in, quiet ? 1.4 / 32 : 1.4, on, 110, 130.81, 0.012);
    s = sp_new(RATE, NULL);
    sp_set_semitones(s, -2);
    double before = 0, after = 0;
    for (int at = 0; at < N; at += 64) {
      sp_process(s, in + at, a + at, 64);
      if (at == on - 128) before = sp_delay(s);
      if (at == on + 448) after = sp_delay(s);  // 10 ms into the note
    }
    sp_free(s);
    CHECK(before > 0.015 * RATE);
    CHECK(after < 0.003 * RATE);
  }

  // A note high up comes round within a few milliseconds, and is taken a repeat at a time: from its start on, the lag
  // never holds more than a few of its periods, whatever the shift, where a low note's is let grow to 30 ms. And the
  // note comes out where it should.
  for (int st = -1; st >= -7; st -= 3) {
    tone(in, 659.26, 2400);
    s = sp_new(RATE, NULL);
    sp_set_semitones(s, st);
    double most = 0;
    for (int at = 0; at < N; at += 16) {
      sp_process(s, in + at, a + at, 16);
      if (at > RATE / 4) most = fmax(most, sp_delay(s));
    }
    sp_free(s);
    CHECK(most < 0.006 * RATE);
    CHECK(share_at(a, 659.26 * pow(2, st / 12.0)) > 0.5);
    CHECK(share_at(a, 659.26) < 0.01);
  }

  // That is for a note that is alone. With a quieter one under it, 30 dB down, which such jumps would leave at its own
  // pitch, the high note waits like any chord (its lag passes 15 ms), and the quiet one comes out where it should.
  // On its own the high note does not wait.
  for (int under = 0; under < 2; under++) {
    tone(in, 659.26, 2400);
    tone(b, 207.65, 2400);
    for (int t = 0; t < N; t++) in[t] += (float)(under * 0.03 * b[t]);
    s = sp_new(RATE, NULL);
    sp_set_semitones(s, -2);
    double most = 0;
    for (int at = 0; at < N; at += 16) {
      sp_process(s, in + at, a + at, 16);
      if (at > RATE / 4) most = fmax(most, sp_delay(s));
    }
    sp_free(s);
    CHECK(under ? most > 0.015 * RATE : most < 0.006 * RATE);
    if (under) CHECK(level_at(a, 207.65 * pow(2, -2 / 12.0), RATE, RATE / 2) > 5 * level_at(a, 207.65, RATE, RATE / 2));
  }

  // A note that stops as the next one starts, as on one string, is over: 30 to 80 ms after the new note's start,
  // what is left of the old one's first overtone is a tenth of what it was or less, wherever in its turn the start
  // finds it.
  // A high pair, taken a repeat at a time, and a low one, which waits.
  for (int pair = 0; pair < 2; pair++)
    for (int j = 0; j < 8; j++) {
      double from = pair ? 146.83 : 440, to = pair ? 220 : 523.25, down = pow(2, -2 / 12.0);
      int on = RATE * 6 / 10 + j * 37;
      replaced(in, 1.4, on, from, to, 0.001);
      run(in, a, -2, 128);
      CHECK(level_at(a, from * down, on + RATE * 3 / 100, RATE / 20) < 0.1 * level_at(a, from * down, on - RATE / 10, RATE / 20));
    }

  // A note that stops, and then a gap: the gap stays a gap, whatever a seam that falls just after the note has stopped
  // does. The note's overtones are a little off whole multiples, as a string's are. From 32 ms after the stop, by when
  // a shifter 30 ms behind has played the note out, to 62 ms: what sounds is 25 dB under the note or less, for notes
  // of many lengths, so that a seam falls in some of the gaps.
  for (int j = 0; j < 24; j++) {
    int stop = 2400 + (int)((0.20 + 0.015 * j) * RATE);
    memset(in, 0, N * sizeof *in);
    for (int t = 2400; t < stop + 48; t++)
      for (int k = 1; k <= 5; k++)
        in[t] += (float)((t < stop ? 1 : 1 - (t - stop) / 48.0) * 0.2 / k * sin(2 * M_PI * 146.83 * k * (1 + 0.003 * (k - 1)) * (t - 2400) / RATE + k));
    run(in, a, -2, 128);
    double note = 0, gap = 0;
    for (int t = stop - RATE / 20; t < stop; t++) note += (double)a[t] * a[t];
    for (int t = stop + RATE * 32 / 1000; t < stop + RATE * 62 / 1000; t++) gap += (double)a[t] * a[t];
    CHECK(gap / (RATE * 3 / 100) < 3e-3 * note / (RATE / 20));
  }

  // No clicks. A note's start takes the read point at once, whatever the shifter was in the middle of (a fade from
  // one read point to another, overtones being carried), and that must not show as a step in the output. Notes that
  // start every 150 ms over the ones before, each coming up over 2 ms, at three shifts: from one sample to the next
  // the output never moves much more than the guitar itself ever does.
  for (int st = -1; st >= -5; st -= 2) {
    memset(in, 0, N * sizeof *in);
    for (int j = 0; j < 12; j++) {
      int from = 2400 + j * (RATE * 15 / 100);
      double hz = 110 * pow(2, (double[]){0, 3, 7, 10, 5, 8, 12, 2, 7, 3, 10, 0}[j] / 12);
      for (int t = from; t < N; t++) {
        double up = t - from < 96 ? 0.5 - 0.5 * cos(M_PI * (t - from) / 96.0) : 1, ring = exp(-(t - from) / (0.4 * RATE));
        for (int k = 1; k <= 5; k++) in[t] += (float)(up * ring * 0.08 / k * sin(2 * M_PI * hz * k * (1 + 0.003 * (k - 1)) * (t - from) / RATE + k + j));
      }
    }
    run(in, a, st, 64);
    double most_in = 0, most_out = 0;
    for (int t = 1; t < N; t++) most_in = fmax(most_in, fabs(in[t] - in[t - 1])), most_out = fmax(most_out, fabs(a[t] - a[t - 1]));
    CHECK(most_out < 1.5 * most_in);
  }

  // Silence, noise and a click: nothing but numbers comes out, and silence stays silent
  for (int kind = 0; kind < 3; kind++) {
    unsigned r = 1;
    for (int t = 0; t < N; t++) {
      r = r * 1664525u + 1013904223u;
      in[t] = kind == 0 ? 0 : kind == 1 ? (float)((r >> 8) / 8388608.0 - 1) * 0.5f : t == 5000;
    }
    run(in, a, -5, 128);
    int finite = 1, silent = 1;
    for (int t = 0; t < N; t++) finite &= isfinite(a[t]) && fabsf(a[t]) < 4, silent &= a[t] == 0;
    CHECK(finite);
    if (kind == 0) CHECK(silent);
  }
  free(in), free(a), free(b);
  return check_done("splice_shift");
}
