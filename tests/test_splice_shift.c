// The shifter behind Pitch Shifter (pitch/splice_shift.c), without the game: that it shifts by what it is asked, leaves
// the guitar alone at 0 semitones, gives the same samples however the input is cut into buffers, takes its lag back at
// a hammered-on note however quiet the guitar comes in, and stays finite on silence, noise and a click. How good it
// sounds is the bench's matter (pitch/bench).
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

// A note, and at sample `on` one hammered on from it: no pick, the old note dies away in a few ms as the new one comes
// up. `level` 1 peaks at about a third.
static void hammered(float *x, double level, int on) {
  memset(x, 0, N * sizeof *x);
  for (int t = 2400; t < N; t++)
    for (int k = 1; k <= 5; k++) {
      double old = t < on ? 1 : exp(-(t - on) / (0.012 * RATE));
      double new = t < on ? 0 : t - on < 192 ? 0.5 - 0.5 * cos(M_PI * (t - on) / 192.0) : 1;
      x[t] += (float)(level * 0.2 / k * (old * sin(2 * M_PI * 220 * k * (t - 2400) / RATE + k) + 0.8 * new * sin(2 * M_PI * 261.63 * k * (t - on) / RATE + k)));
    }
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

  // As loud as it went in
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
  // long by then, taken back at the note
  for (int quiet = 0; quiet < 2; quiet++) {
    int on = RATE * 3 / 10;
    hammered(in, quiet ? 1.4 / 32 : 1.4, on);
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
