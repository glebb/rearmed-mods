// What rs-stems does around the Core ML model, on the CPU with Accelerate: htdemucs_6s's spectrogram and normalisation
// for one segment before the model, the inverse spectrogram after it, and a resampler for songs that aren't at the
// model's 44.1 kHz. The split is the one stems/convert/htcore.py makes: everything with complex numbers, padding or
// statistics over the whole segment stays out of the model.
#ifndef STEMS_DSP_H
#define STEMS_DSP_H
#include <stddef.h>
#include <stdint.h>

#define MODEL_RATE 44100
#define SEG 343980           // the model's segment: int(7.8 s x 44100)
#define NFFT 4096
#define HOP 1024
#define FRAMES 336           // ceil(SEG / HOP)
#define BINS 2048            // NFFT / 2; the Nyquist bin is left out
#define FOLD_ROWS 64         // the time branch's input, folded into 64 rows of 5376 (84 samples of zero padding)
#define FOLD_W 5376
#define FOLD (FOLD_ROWS * FOLD_W)
#define SPEC_PLANE (FRAMES * BINS)

typedef struct stft stft;    // transforms and scratch; one per thread
stft *stft_new(void);
void stft_free(stft *s);

typedef struct {
  float mean, std;           // of the segment's spectrogram (real and imaginary parts as channels)
  float meant, stdt;         // of its waveform
} seg_stats;

// One segment (SEG samples a channel, already normalised by the song's statistics) -> the model's inputs: x, 4
// planes of FRAMES x BINS in (time, frequency) order (left real, left imaginary, right real, right imaginary), and xt,
// 2 planes of FOLD (the waveform, normalised, then zeros).
void seg_pre(stft *s, const float *left, const float *right, float *x, float *xt, seg_stats *st);

// One source from the model's outputs -> its waveform for the segment (SEG samples a channel): spec, its 4 planes of
// the model's frequency output (rows of BINS, row_stride apart); wave, its 2 planes of the time output (FOLD long).
void seg_post(stft *s, const float *const spec[4], size_t row_stride, const float *const wave[2], const seg_stats *st,
              float *left, float *right);

// The cross-fade apply_model gives each segment's output: a triangle over the segment, 1 at its middle
float seg_weight(int i);

// A windowed-sinc resampler between two fixed rates, without delay: output frame j is the input at j x in / out
typedef struct {
  int up, down;              // out / in = up / down, reduced
  int half;                  // taps on each side
  float *taps;               // up phases x 2 half
} resampler;

int rs_init(resampler *r, int in_rate, int out_rate);
void rs_free(resampler *r);
size_t rs_length(const resampler *r, size_t in_len);    // the output length for in_len input frames
// out[j] for j0 <= j < j1, from in (n frames; zeros outside)
void rs_run(const resampler *r, const float *in, size_t n, float *out, size_t j0, size_t j1);
// How many output frames depend only on the first `ready` of n input frames (all of them once ready == n)
size_t rs_ready(const resampler *r, size_t ready, size_t n, size_t out_len);
#endif
