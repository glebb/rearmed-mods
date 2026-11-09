// rs-stems' signal processing around the model (stems_dsp.h). Every step is the one demucs takes (htdemucs.py _spec,
// _ispec, _magnitude, _mask; spec.py; apply.py), with torch.stft / torch.istft as demucs calls them: a periodic Hann
// window of 4096, hop 1024, centred with reflect padding, normalised (1/64 each way).
#include "stems_dsp.h"

#include <Accelerate/Accelerate.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PAD_L 1536                       // demucs pads the segment by HOP / 2 x 3 (reflecting) before the STFT
#define PAD_LEN (FRAMES * HOP + 2 * PAD_L)  // ... and at the end up to FRAMES x HOP + PAD_L: 347136 in all
#define OUT_AT (NFFT / 2 + PAD_L)        // the segment's first sample in the inverse STFT's overlap-add
#define OLA_LEN ((FRAMES + 4) * HOP + NFFT)

struct stft {
  vDSP_DFT_Setup fwd, inv;
  float win[NFFT];
  float inv_env[HOP];  // 1 / the summed squared window of the overlapping frames, which torch.istft divides by
  float *pad, *ola;
  float frame[NFFT];
  float re[NFFT / 2], im[NFFT / 2], re2[NFFT / 2], im2[NFFT / 2];
};

stft *stft_new(void) {
  stft *s = calloc(1, sizeof *s);
  if (!s) return NULL;
  s->fwd = vDSP_DFT_zrop_CreateSetup(NULL, NFFT, vDSP_DFT_FORWARD);
  s->inv = vDSP_DFT_zrop_CreateSetup(s->fwd, NFFT, vDSP_DFT_INVERSE);
  s->pad = malloc(PAD_LEN * sizeof(float));
  s->ola = malloc(OLA_LEN * sizeof(float));
  if (!s->fwd || !s->inv || !s->pad || !s->ola) {
    stft_free(s);
    return NULL;
  }
  for (int i = 0; i < NFFT; i++) s->win[i] = (float)(0.5 - 0.5 * cos(2 * M_PI * i / NFFT));
  for (int i = 0; i < HOP; i++) {
    float e = 0;
    for (int k = 0; k < NFFT / HOP; k++) e += s->win[i + k * HOP] * s->win[i + k * HOP];
    s->inv_env[i] = 1.f / e;
  }
  return s;
}

void stft_free(stft *s) {
  if (!s) return;
  if (s->inv) vDSP_DFT_DestroySetup(s->inv);
  if (s->fwd) vDSP_DFT_DestroySetup(s->fwd);
  free(s->pad);
  free(s->ola);
  free(s);
}

// Mean and unbiased standard deviation, in double: the sums run over millions of values
static void stats(const float *a, size_t n, const float *b, size_t m, float *mean, float *std) {
  double s = 0, q = 0;
  for (size_t i = 0; i < n; i++) s += a[i];
  for (size_t i = 0; i < m; i++) s += b[i];
  double mu = s / (double)(n + m);
  for (size_t i = 0; i < n; i++) q += (a[i] - mu) * (a[i] - mu);
  for (size_t i = 0; i < m; i++) q += (b[i] - mu) * (b[i] - mu);
  *mean = (float)mu;
  *std = (float)sqrt(q / (double)(n + m - 1));
}

// The segment, reflected at both ends (without repeating the edge sample), PAD_L before and the rest after
static void reflect_pad(const float *x, float *out) {
  for (int i = 0; i < PAD_L; i++) out[i] = x[PAD_L - i];
  memcpy(out + PAD_L, x, SEG * sizeof(float));
  for (int i = PAD_L + SEG; i < PAD_LEN; i++) out[i] = x[2 * (SEG - 1) - (i - PAD_L)];
}

void seg_pre(stft *s, const float *left, const float *right, float *x, float *xt, seg_stats *st) {
  const float *ch[2] = {left, right};
  const float scale = 1.f / 128;  // vDSP's forward real DFT is twice the DFT; the normalised STFT divides by 64
  for (int c = 0; c < 2; c++) {
    float *re = x + (size_t)(2 * c) * SPEC_PLANE, *im = x + (size_t)(2 * c + 1) * SPEC_PLANE;
    reflect_pad(ch[c], s->pad);
    // torch.stft's own centring pads another NFFT / 2, and demucs drops its first two and last two frames: the frames
    // kept start at the padded segment's first sample, so that padding never reaches them
    for (int f = 0; f < FRAMES; f++) {
      DSPSplitComplex z = {s->re, s->im};
      vDSP_vmul(s->pad + (size_t)f * HOP, 1, s->win, 1, s->frame, 1, NFFT);
      vDSP_ctoz((const DSPComplex *)s->frame, 2, &z, 1, NFFT / 2);
      vDSP_DFT_Execute(s->fwd, s->re, s->im, re + (size_t)f * BINS, im + (size_t)f * BINS);
      im[(size_t)f * BINS] = 0;  // vDSP packs the Nyquist bin there; the DC bin of a real signal has no imaginary part
    }
    vDSP_vsmul(re, 1, &scale, re, 1, SPEC_PLANE);
    vDSP_vsmul(im, 1, &scale, im, 1, SPEC_PLANE);
  }
  // the spectrogram normalised over all four planes, the waveform over both channels
  stats(x, 2 * (size_t)SPEC_PLANE, x + 2 * (size_t)SPEC_PLANE, 2 * (size_t)SPEC_PLANE, &st->mean, &st->std);
  float neg = -st->mean, div = 1e-5f + st->std;
  vDSP_vsadd(x, 1, &neg, x, 1, 4 * (size_t)SPEC_PLANE);
  vDSP_vsdiv(x, 1, &div, x, 1, 4 * (size_t)SPEC_PLANE);
  stats(left, SEG, right, SEG, &st->meant, &st->stdt);
  neg = -st->meant;
  div = 1e-5f + st->stdt;
  for (int c = 0; c < 2; c++) {
    float *t = xt + (size_t)c * FOLD;
    vDSP_vsadd(ch[c], 1, &neg, t, 1, SEG);
    vDSP_vsdiv(t, 1, &div, t, 1, SEG);
    memset(t + SEG, 0, (FOLD - SEG) * sizeof(float));
  }
}

void seg_post(stft *s, const float *const spec[4], size_t row_stride, const float *const wave[2], const seg_stats *st,
              float *left, float *right) {
  float *out[2] = {left, right};
  const float scale = 1.f / 64;  // vDSP's inverse real DFT is the plain sum; the normalised iSTFT divides by 64
  for (int c = 0; c < 2; c++) {
    const float *re = spec[2 * c], *im = spec[2 * c + 1];
    memset(s->ola, 0, OLA_LEN * sizeof(float));
    for (int f = 0; f < FRAMES; f++) {
      DSPSplitComplex z = {s->re2, s->im2};
      vDSP_vsmsa(re + (size_t)f * row_stride, 1, &st->std, &st->mean, s->re, 1, BINS);
      vDSP_vsmsa(im + (size_t)f * row_stride, 1, &st->std, &st->mean, s->im, 1, BINS);
      s->im[0] = 0;  // the Nyquist bin, padded with zero by demucs (the DC bin's imaginary part is ignored)
      vDSP_DFT_Execute(s->inv, s->re, s->im, s->re2, s->im2);
      vDSP_ztoc(&z, 1, (DSPComplex *)s->frame, 2, NFFT / 2);
      vDSP_vmul(s->frame, 1, s->win, 1, s->frame, 1, NFFT);
      float *o = s->ola + (size_t)(f + 2) * HOP;  // demucs pads two empty frames in front
      vDSP_vsma(s->frame, 1, &scale, o, 1, o, 1, NFFT);
    }
    for (int i = 0; i < SEG; i++)
      out[c][i] = s->ola[OUT_AT + i] * s->inv_env[(OUT_AT + i) % HOP] + (wave[c][i] * st->stdt + st->meant);
  }
}

float seg_weight(int i) { return (float)(i < SEG / 2 ? i + 1 : SEG - i) / (float)(SEG / 2); }

// ------------------------------------------------------------------------------------------------
// Resampling
// ------------------------------------------------------------------------------------------------
static int gcd(int a, int b) { return b ? gcd(b, a % b) : a; }

static double bessel_i0(double x) {
  double s = 1, t = 1;
  for (int k = 1; k < 64 && t > 1e-14 * s; k++) {
    t *= (x / (2 * k)) * (x / (2 * k));
    s += t;
  }
  return s;
}

int rs_init(resampler *r, int in_rate, int out_rate) {
  const double zeros = 16, beta = 8.6, rolloff = 0.94;  // ~80 dB stopband, passband to 94% of the lower Nyquist
  int g = gcd(in_rate, out_rate);
  r->up = out_rate / g;
  r->down = in_rate / g;
  double fc = (out_rate < in_rate ? (double)out_rate / in_rate : 1.0) * rolloff, hw = zeros / fc;
  r->half = (int)ceil(hw);
  int n = 2 * r->half;
  r->taps = malloc((size_t)r->up * (size_t)n * sizeof(float));
  if (!r->taps) return -1;
  for (int p = 0; p < r->up; p++) {
    float *h = r->taps + (size_t)p * n;
    double sum = 0;
    for (int i = 0; i < n; i++) {
      double x = (double)p / r->up + r->half - 1 - i, u = fc * x, v = 0;
      if (fabs(x) < hw) {
        double q = x / hw;
        v = fc * (u == 0 ? 1 : sin(M_PI * u) / (M_PI * u)) * bessel_i0(beta * sqrt(1 - q * q)) / bessel_i0(beta);
      }
      h[i] = (float)v;
      sum += v;
    }
    for (int i = 0; i < n; i++) h[i] = (float)(h[i] / sum);  // each phase passes DC unchanged
  }
  return 0;
}

void rs_free(resampler *r) {
  free(r->taps);
  r->taps = NULL;
}

size_t rs_length(const resampler *r, size_t in_len) {
  return (size_t)(((uint64_t)in_len * (uint64_t)r->up + (uint64_t)r->down - 1) / (uint64_t)r->down);
}

void rs_run(const resampler *r, const float *in, size_t n, float *out, size_t j0, size_t j1) {
  int taps = 2 * r->half;
  for (size_t j = j0; j < j1; j++) {
    uint64_t pos = (uint64_t)j * (uint64_t)r->down;
    long start = (long)(pos / (uint64_t)r->up) - r->half + 1;
    const float *h = r->taps + (size_t)(pos % (uint64_t)r->up) * (size_t)taps;
    if (start >= 0 && (size_t)start + (size_t)taps <= n) {
      vDSP_dotpr(in + start, 1, h, 1, out + j, (vDSP_Length)taps);
    } else {
      float acc = 0;
      for (int i = 0; i < taps; i++)
        if (start + i >= 0 && (size_t)(start + i) < n) acc += in[start + i] * h[i];
      out[j] = acc;
    }
  }
}

size_t rs_ready(const resampler *r, size_t ready, size_t n, size_t out_len) {
  if (ready >= n) return out_len;
  if (ready <= (size_t)r->half) return 0;
  // output j reads input up to floor(j x down / up) + half, which must be before `ready`
  uint64_t m = (uint64_t)(ready - (size_t)r->half);
  size_t j = (size_t)((m * (uint64_t)r->up + (uint64_t)r->down - 1) / (uint64_t)r->down);
  return j < out_len ? j : out_len;
}
