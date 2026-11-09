// rs-stems: the Song Stems mod's helper (stems/README.md has the contract with the mod). It decodes the song the game
// is about to play (Wwise Vorbis, wwise_vorbis.c), separates it with htdemucs_6s converted to Core ML on the Neural
// Engine (the steps around the model in stems_dsp.c), and writes the song without the guitar or the bass into a file
// the mod maps, from the start, while it works. Finished songs are kept as AAC in a cache of a fixed size.
//
//   rs-stems serve                                   what the game starts: commands from the FIFO, until they stop
//   rs-stems warm                                    load the model once (the first load compiles it) and time it
//   rs-stems separate <wem> <file id> <part> <out.pcm>  one song, no cache, timings on stdout (tests)
//   rs-stems decode <wem> <out.wav>                  the song as the helper decodes it, 16-bit (tests)
//
// RS_STEMS_HOME (the helper and the model), RS_STEMS_CACHE (the cache, codebooks.bin, the FIFO and work/), and
// RS_STEMS_OVERLAP (between segments, default 0.25) change the defaults.
#import <Accelerate/Accelerate.h>
#import <AudioToolbox/AudioToolbox.h>
#import <CoreML/CoreML.h>
#import <Foundation/Foundation.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "stems_dsp.h"
#include "wwise_vorbis.h"

enum { PART_GUITAR, PART_BASS };
static const char *const PART_NAME[] = {"guitar", "bass"};
static const int PART_SOURCE[] = {4, 1};  // htdemucs_6s's sources: drums, bass, other, vocals, guitar, piano
#define HEADER 64
#define STATE_WORKING 0
#define STATE_DONE 1
#define STATE_FAILED 2
#define HEARTBEAT_TIMEOUT 10.0
#define MODEL_NAME "htdemucs_6s.mlmodelc"
#define LOG_MAX (1 << 20)

#define FAIL(...)                       \
  do {                                  \
    snprintf(err, errlen, __VA_ARGS__); \
    return -1;                          \
  } while (0)

static char g_home[PATH_MAX], g_cache[PATH_MAX], g_work[PATH_MAX];
static int g_logfd = 2;
static _Atomic uint64_t g_gen;  // the separate asked for last; a job with another number stops

static double now(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + t.tv_nsec * 1e-9;
}

static double load1(void) {
  double l[3] = {0};
  getloadavg(l, 3);
  return l[0];
}

static void say(const char *fmt, ...) {
  char line[2048];
  struct timeval tv;
  gettimeofday(&tv, NULL);
  struct tm tm;
  localtime_r(&tv.tv_sec, &tm);
  int n = (int)strftime(line, sizeof line, "%Y-%m-%d %H:%M:%S", &tm);
  n += snprintf(line + n, sizeof line - (size_t)n, ".%03d ", (int)(tv.tv_usec / 1000));
  va_list ap;
  va_start(ap, fmt);
  n += vsnprintf(line + n, sizeof line - (size_t)n - 1, fmt, ap);
  va_end(ap);
  if (n > (int)sizeof line - 2) n = (int)sizeof line - 2;
  line[n++] = '\n';
  write(g_logfd, line, (size_t)n);
}

static void mkdir_p(const char *path) {
  char p[PATH_MAX];
  snprintf(p, sizeof p, "%s", path);
  for (char *s = p + 1; *s; s++)
    if (*s == '/') {
      *s = 0;
      mkdir(p, 0755);
      *s = '/';
    }
  mkdir(p, 0755);
}

static void folders(void) {
  const char *h = getenv("HOME"), *e = getenv("RS_STEMS_HOME");
  if (e && *e)
    snprintf(g_home, sizeof g_home, "%s", e);
  else
    snprintf(g_home, sizeof g_home, "%s/Library/Application Support/RS Re-ARMed/stems", h ? h : "");
  e = getenv("RS_STEMS_CACHE");
  if (e && *e)
    snprintf(g_cache, sizeof g_cache, "%s", e);
  else
    snprintf(g_cache, sizeof g_cache, "%s/Library/Caches/RS Re-ARMed/stems", h ? h : "");
  snprintf(g_work, sizeof g_work, "%s/work", g_cache);
}

static uint8_t *read_all(const char *path, size_t *len) {
  int fd = open(path, O_RDONLY);
  struct stat st;
  if (fd < 0 || fstat(fd, &st) || st.st_size <= 0) {
    if (fd >= 0) close(fd);
    return NULL;
  }
  uint8_t *b = malloc((size_t)st.st_size);
  size_t got = 0;
  while (b && got < (size_t)st.st_size) {
    ssize_t r = read(fd, b + got, (size_t)st.st_size - got);
    if (r <= 0) break;
    got += (size_t)r;
  }
  close(fd);
  if (!b || got != (size_t)st.st_size) {
    free(b);
    return NULL;
  }
  *len = got;
  return b;
}

static float overlap(void) {
  const char *e = getenv("RS_STEMS_OVERLAP");
  float v = e && *e ? strtof(e, NULL) : 0.25f;
  return v < 0 ? 0 : v > 0.9f ? 0.9f : v;
}

// ------------------------------------------------------------------------------------------------
// The song
// ------------------------------------------------------------------------------------------------
typedef struct {
  uint32_t rate, channels, frames;
  int16_t *pcm;  // interleaved, as the game's decoder gives it
} song;

// Tests only: RS_STEMS_TEST_RATE has the song as if the game had it at that rate (resampled), for the path a song at
// another rate than the model's takes
static int test_rate(ww_audio *a, char *err, size_t errlen) {
  const char *e = getenv("RS_STEMS_TEST_RATE");
  int to = e ? atoi(e) : 0;
  if (to <= 0 || (uint32_t)to == a->rate) return 0;
  resampler r = {0};
  if (rs_init(&r, (int)a->rate, to)) FAIL("no resampler to %d Hz", to);
  size_t n = rs_length(&r, a->frames);
  float *in = malloc(a->frames * sizeof(float)), *out = malloc(n * sizeof(float)), *pcm = malloc(n * a->channels * 4);
  int ok = in && out && pcm;
  for (uint32_t c = 0; ok && c < a->channels; c++) {
    for (size_t i = 0; i < a->frames; i++) in[i] = a->pcm[i * a->channels + c];
    rs_run(&r, in, a->frames, out, 0, n);
    for (size_t i = 0; i < n; i++) pcm[i * a->channels + c] = out[i];
  }
  rs_free(&r);
  free(in);
  free(out);
  if (!ok) {
    free(pcm);
    FAIL("out of memory");
  }
  free(a->pcm);
  a->pcm = pcm;
  a->frames = (uint32_t)n;
  a->rate = (uint32_t)to;
  return 0;
}

// The song decoded and made 16-bit as vgmstream does it (x 32767, truncated), which matches the game's own decoder
static int load_song(const char *wem, song *s, double *secs, char *err, size_t errlen) {
  char cbpath[PATH_MAX];
  size_t wl = 0, cl = 0;
  double t = now();
  snprintf(cbpath, sizeof cbpath, "%s/codebooks.bin", g_cache);
  uint8_t *w = read_all(wem, &wl), *cb = read_all(cbpath, &cl);
  ww_audio a;
  int rc = -1;
  if (!w)
    snprintf(err, errlen, "can't read %s", wem);
  else if (!cb)
    snprintf(err, errlen, "no %s (the mod writes it from the game)", cbpath);
  else if (!ww_decode(w, wl, cb, cl, &a, err, errlen) && !test_rate(&a, err, errlen)) {
    size_t n = (size_t)a.frames * a.channels;
    s->pcm = malloc(n * sizeof *s->pcm);
    if (s->pcm) {
      for (size_t i = 0; i < n; i++) {
        float x = a.pcm[i] * 32767.f;
        s->pcm[i] = (int16_t)(x > 32767.f ? 32767.f : x < -32768.f ? -32768.f : x);
      }
      s->rate = a.rate;
      s->channels = a.channels;
      s->frames = a.frames;
      rc = 0;
    } else {
      snprintf(err, errlen, "out of memory");
    }
    ww_free(&a);
  }
  free(w);
  free(cb);
  if (!rc && (s->channels > 2 || !s->frames)) {
    snprintf(err, errlen, "%u channels, %u frames: only mono and stereo songs", s->channels, s->frames);
    free(s->pcm);
    rc = -1;
  }
  *secs = now() - t;
  return rc;
}

// ------------------------------------------------------------------------------------------------
// The file the mod maps (stems/README.md): full size before anything says it's ready, a new file for each song
// ------------------------------------------------------------------------------------------------
typedef struct {
  uint8_t *base;
  size_t size;
  uint32_t total, channels;
} pcm_out;

static int pcm_create(pcm_out *p, const char *path, uint32_t id, int part, uint32_t rate, uint32_t channels,
                      uint32_t total, char *err, size_t errlen) {
  char tmp[PATH_MAX + 8];
  snprintf(tmp, sizeof tmp, "%s.new", path);
  unlink(tmp);
  int fd = open(tmp, O_RDWR | O_CREAT | O_EXCL, 0644);
  if (fd < 0) FAIL("can't create %s: %s", tmp, strerror(errno));
  p->size = HEADER + (size_t)total * channels * 2;
  p->total = total;
  p->channels = channels;
  if (ftruncate(fd, (off_t)p->size)) {
    close(fd);
    unlink(tmp);
    FAIL("can't size %s: %s", tmp, strerror(errno));
  }
  p->base = mmap(NULL, p->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  close(fd);
  if (p->base == MAP_FAILED) {
    p->base = NULL;
    unlink(tmp);
    FAIL("can't map %s: %s", tmp, strerror(errno));
  }
  uint32_t h[7] = {id, (uint32_t)part, rate, channels, total, STATE_WORKING, 0};
  memcpy(p->base, "RSSTEMS1", 8);
  memcpy(p->base + 8, h, sizeof h);
  // in under its name only now, whole: a mapping of the file it replaces stays as it was
  if (rename(tmp, path)) {
    munmap(p->base, p->size);
    p->base = NULL;
    unlink(tmp);
    FAIL("can't rename %s: %s", tmp, strerror(errno));
  }
  return 0;
}

static int16_t *pcm_data(pcm_out *p) { return (int16_t *)(p->base + HEADER); }

static void pcm_ready(pcm_out *p, uint32_t frames) {
  atomic_store_explicit((_Atomic uint32_t *)(p->base + 32), frames, memory_order_release);
}

static void pcm_state(pcm_out *p, uint32_t state) {
  atomic_store_explicit((_Atomic uint32_t *)(p->base + 28), state, memory_order_release);
}

static void pcm_close(pcm_out *p) {
  if (p->base) munmap(p->base, p->size);
  p->base = NULL;
}

// ------------------------------------------------------------------------------------------------
// The model
// ------------------------------------------------------------------------------------------------
static MLModel *g_model;
static double g_model_load_s;

static int model_load(char *err, size_t errlen) {
  if (g_model) return 0;
  @autoreleasepool {
    double t = now();
    NSError *e = nil;
    NSString *path = [NSString stringWithFormat:@"%s/%s", g_home, MODEL_NAME];
    if (![[NSFileManager defaultManager] fileExistsAtPath:path])
      FAIL("no model at %s (stems/install.sh)", path.UTF8String);
    MLModelConfiguration *cfg = [[MLModelConfiguration alloc] init];
    cfg.computeUnits = MLComputeUnitsCPUAndNeuralEngine;  // never the GPU, which the game draws with
    g_model = [MLModel modelWithContentsOfURL:[NSURL fileURLWithPath:path] configuration:cfg error:&e];
    if (!g_model) FAIL("the model doesn't load: %s", e.localizedDescription.UTF8String);
    g_model_load_s = now() - t;
  }
  return 0;
}

static int model_predict(float *x, float *xt, MLMultiArray *__strong *xo, MLMultiArray *__strong *xto, char *err,
                         size_t errlen) {
  @autoreleasepool {
    NSError *e = nil;
    MLMultiArray *ax = [[MLMultiArray alloc] initWithDataPointer:x
                                                           shape:@[ @1, @4, @FRAMES, @BINS ]
                                                        dataType:MLMultiArrayDataTypeFloat32
                                                         strides:@[ @(4 * SPEC_PLANE), @(SPEC_PLANE), @BINS, @1 ]
                                                     deallocator:nil
                                                           error:&e];
    MLMultiArray *at = [[MLMultiArray alloc] initWithDataPointer:xt
                                                           shape:@[ @1, @2, @FOLD_ROWS, @FOLD_W ]
                                                        dataType:MLMultiArrayDataTypeFloat32
                                                         strides:@[ @(2 * FOLD), @(FOLD), @FOLD_W, @1 ]
                                                     deallocator:nil
                                                           error:&e];
    if (!ax || !at) FAIL("the model's inputs: %s", e.localizedDescription.UTF8String);
    MLDictionaryFeatureProvider *in = [[MLDictionaryFeatureProvider alloc] initWithDictionary:@{@"x" : ax, @"xt" : at}
                                                                                         error:&e];
    id<MLFeatureProvider> out = in ? [g_model predictionFromFeatures:in error:&e] : nil;
    if (!out) FAIL("the model: %s", e.localizedDescription.UTF8String);
    *xo = [out featureValueForName:@"x_out"].multiArrayValue;
    *xto = [out featureValueForName:@"xt_out"].multiArrayValue;
    if (!*xo || !*xto || (*xo).dataType != MLMultiArrayDataTypeFloat32 ||
        (*xto).dataType != MLMultiArrayDataTypeFloat32 || (*xo).strides[3].intValue != 1 ||
        (*xto).strides[3].intValue != 1)
      FAIL("the model's outputs aren't the float32 x_out and xt_out this expects");
  }
  return 0;
}

// ------------------------------------------------------------------------------------------------
// Separation: segments of 7.8 s, cut and cross-faded as demucs' apply_model does, the model on one segment while
// the CPU prepares the next and finishes the one before
// ------------------------------------------------------------------------------------------------
typedef struct {
  const song *s;
  pcm_out *out;
  int source;
  uint64_t gen;            // 0: never superseded
  double started;          // the times are from here
  double t_first, t_done;  // first 30 s ready, all ready
  size_t n44, stride;
  int nseg;
  float *norm[2];          // the song at the model's rate, normalised
  float *acc[2], *sumw;    // the weighted sum of the segments' outputs; frames before done44 hold the stem itself
  size_t done44;
  int resample;
  resampler up;
  float *stem[2];          // the stem at the song's rate (when it isn't the model's)
  uint32_t ready;
  float mean, std;
  seg_stats *st;
  stft *pre, *post;
  float *x[2], *xt[2], *seg[2][2], *res[2], *wave[2];
} sep;

static int superseded(const sep *c) { return c->gen && atomic_load(&g_gen) != c->gen; }

static void prep_segment(sep *c, int k) {
  int slot = k & 1;
  size_t o = (size_t)k * c->stride, len = c->n44 - o < SEG ? c->n44 - o : SEG;
  // TensorChunk.padded: centred in the segment, with real samples around it where there are any
  long start = (long)o - (long)(SEG - len) / 2, lo = start < 0 ? 0 : start;
  long hi = start + SEG > (long)c->n44 ? (long)c->n44 : start + SEG;
  for (int ch = 0; ch < 2; ch++) {
    memset(c->seg[slot][ch], 0, SEG * sizeof(float));
    memcpy(c->seg[slot][ch] + (lo - start), c->norm[ch] + lo, (size_t)(hi - lo) * sizeof(float));
  }
  seg_pre(c->pre, c->seg[slot][0], c->seg[slot][1], c->x[slot], c->xt[slot], &c->st[k]);
}

// The stem finished up to upto (at the model's rate), and the song without it written as far as that allows
static void finish_upto(sep *c, size_t upto) {
  const song *s = c->s;
  for (int ch = 0; ch < 2; ch++)
    for (size_t i = c->done44; i < upto; i++) c->acc[ch][i] = c->acc[ch][i] / c->sumw[i] * c->std + c->mean;
  c->done44 = upto;
  size_t ready = c->resample ? rs_ready(&c->up, upto, c->n44, s->frames) : upto;
  if (ready <= c->ready) return;
  float *const *stem = c->acc;
  if (c->resample) {
    for (int ch = 0; ch < 2; ch++) rs_run(&c->up, c->acc[ch], c->n44, c->stem[ch], c->ready, ready);
    stem = c->stem;
  }
  int16_t *d = pcm_data(c->out);
  for (size_t j = c->ready; j < ready; j++)
    for (uint32_t ch = 0; ch < s->channels; ch++) {
      float part = s->channels == 1 ? 0.5f * (stem[0][j] + stem[1][j]) : stem[ch][j];
      float v = rintf((float)s->pcm[j * s->channels + ch] - part * 32768.f);
      d[j * s->channels + ch] = (int16_t)(v > 32767.f ? 32767.f : v < -32768.f ? -32768.f : v);
    }
  pcm_ready(c->out, (uint32_t)ready);
  c->ready = (uint32_t)ready;
  if (!c->t_first && (ready >= 30 * (size_t)s->rate || ready == s->frames)) c->t_first = now() - c->started;
}

static void post_segment(sep *c, int k, MLMultiArray *xo, MLMultiArray *xto) {
  NSArray<NSNumber *> *sx = xo.strides, *stt = xto.strides;
  int src = c->source;
  [xo getBytesWithHandler:^(const void *xb, NSInteger xsize) {
    [xto getBytesWithHandler:^(const void *tb, NSInteger tsize) {
      (void)xsize;
      (void)tsize;
      const float *xf = xb, *tf = tb;
      size_t cs = sx[1].unsignedLongValue, rs = sx[2].unsignedLongValue;
      size_t tcs = stt[1].unsignedLongValue, trs = stt[2].unsignedLongValue;
      const float *spec[4], *wave[2];
      for (int i = 0; i < 4; i++) spec[i] = xf + (size_t)(4 * src + i) * cs;
      for (int i = 0; i < 2; i++) {
        const float *p = tf + (size_t)(2 * src + i) * tcs;
        if (trs != FOLD_W) {  // rows with padding: gather them
          for (int r = 0; r < FOLD_ROWS; r++) memcpy(c->wave[i] + r * FOLD_W, p + r * trs, FOLD_W * sizeof(float));
          p = c->wave[i];
        }
        wave[i] = p;
      }
      seg_post(c->post, spec, rs, wave, &c->st[k], c->res[0], c->res[1]);
    }];
  }];
  size_t o = (size_t)k * c->stride, len = c->n44 - o < SEG ? c->n44 - o : SEG, d = (SEG - len) / 2;  // center_trim
  for (size_t i = 0; i < len; i++) {
    float w = seg_weight((int)i);
    c->acc[0][o + i] += w * c->res[0][d + i];
    c->acc[1][o + i] += w * c->res[1][d + i];
    c->sumw[o + i] += w;
  }
  finish_upto(c, k + 1 < c->nseg ? (size_t)(k + 1) * c->stride : c->n44);
}

static void sep_free(sep *c) {
  for (int i = 0; i < 2; i++) {
    free(c->norm[i]);
    free(c->acc[i]);
    free(c->stem[i]);
    free(c->x[i]);
    free(c->xt[i]);
    free(c->seg[i][0]);
    free(c->seg[i][1]);
    free(c->res[i]);
    free(c->wave[i]);
  }
  free(c->sumw);
  free(c->st);
  stft_free(c->pre);
  stft_free(c->post);
  rs_free(&c->up);
}

// The song at the model's rate, in two channels, normalised by its mono mix as demucs' callers do
static int sep_setup(sep *c, char *err, size_t errlen) {
  const song *s = c->s;
  size_t n = s->frames;
  float *src[2] = {malloc(n * sizeof(float)), malloc(n * sizeof(float))};
  if (!src[0] || !src[1]) {
    free(src[0]);
    free(src[1]);
    FAIL("out of memory");
  }
  for (size_t i = 0; i < n; i++) {
    src[0][i] = s->pcm[i * s->channels] / 32768.f;
    src[1][i] = s->pcm[i * s->channels + s->channels - 1] / 32768.f;  // mono: the same twice
  }
  c->resample = s->rate != MODEL_RATE;
  if (c->resample) {
    resampler down = {0};
    if (rs_init(&down, (int)s->rate, MODEL_RATE) || rs_init(&c->up, MODEL_RATE, (int)s->rate)) {
      rs_free(&down);
      free(src[0]);
      free(src[1]);
      FAIL("no resampler from %u Hz", s->rate);
    }
    c->n44 = rs_length(&down, n);
    for (int ch = 0; ch < 2; ch++) {
      c->norm[ch] = malloc(c->n44 * sizeof(float));
      c->stem[ch] = malloc(n * sizeof(float));
      if (c->norm[ch]) rs_run(&down, src[ch], n, c->norm[ch], 0, c->n44);
      free(src[ch]);
    }
    rs_free(&down);
  } else {
    c->n44 = n;
    c->norm[0] = src[0];
    c->norm[1] = src[1];
  }
  double sum = 0, sq = 0;
  for (size_t i = 0; c->norm[0] && c->norm[1] && i < c->n44; i++) sum += 0.5 * ((double)c->norm[0][i] + c->norm[1][i]);
  double mu = sum / (double)c->n44;
  for (size_t i = 0; c->norm[0] && c->norm[1] && i < c->n44; i++) {
    double v = 0.5 * ((double)c->norm[0][i] + c->norm[1][i]) - mu;
    sq += v * v;
  }
  c->mean = (float)mu;
  c->std = (float)sqrt(sq / (double)(c->n44 > 1 ? c->n44 - 1 : 1));
  if (c->std <= 0) c->std = 1;  // silence
  c->stride = (size_t)((1.0 - overlap()) * SEG);
  c->nseg = (int)((c->n44 + c->stride - 1) / c->stride);
  c->st = calloc((size_t)c->nseg, sizeof *c->st);
  c->sumw = calloc(c->n44, sizeof(float));
  c->pre = stft_new();
  c->post = stft_new();
  int ok = c->st && c->sumw && c->pre && c->post && c->norm[0] && c->norm[1];
  for (int i = 0; i < 2 && ok; i++) {
    c->acc[i] = calloc(c->n44, sizeof(float));
    c->x[i] = malloc(4 * (size_t)SPEC_PLANE * sizeof(float));
    c->xt[i] = malloc(2 * (size_t)FOLD * sizeof(float));
    c->seg[i][0] = malloc(SEG * sizeof(float));
    c->seg[i][1] = malloc(SEG * sizeof(float));
    c->res[i] = malloc(SEG * sizeof(float));
    c->wave[i] = malloc(FOLD * sizeof(float));
    ok = c->acc[i] && c->x[i] && c->xt[i] && c->seg[i][0] && c->seg[i][1] && c->res[i] && c->wave[i] &&
         (!c->resample || c->stem[i]);
  }
  if (!ok) FAIL("out of memory for %zu frames", c->n44);
  float neg = -c->mean;
  for (int ch = 0; ch < 2; ch++) {
    vDSP_vsadd(c->norm[ch], 1, &neg, c->norm[ch], 1, c->n44);
    vDSP_vsdiv(c->norm[ch], 1, &c->std, c->norm[ch], 1, c->n44);
  }
  return 0;
}

// 0 done, 1 stopped for a newer song, -1 failed
static int separate_song(sep *c, char *err, size_t errlen) {
  if (sep_setup(c, err, errlen)) return -1;
  dispatch_queue_t q = dispatch_get_global_queue(QOS_CLASS_UTILITY, 0);
  __block MLMultiArray *prev_x = nil, *prev_xt = nil;
  prep_segment(c, 0);
  for (int k = 0; k <= c->nseg; k++) {
    if (superseded(c)) return 1;
    dispatch_group_t g = dispatch_group_create();
    if (k + 1 < c->nseg) dispatch_group_async(g, q, ^{ prep_segment(c, k + 1); });
    if (k > 0) {
      MLMultiArray *a = prev_x, *b = prev_xt;
      dispatch_group_async(g, q, ^{ post_segment(c, k - 1, a, b); });
    }
    MLMultiArray *xo = nil, *xto = nil;
    int bad = k < c->nseg && model_predict(c->x[k & 1], c->xt[k & 1], &xo, &xto, err, errlen);
    dispatch_group_wait(g, DISPATCH_TIME_FOREVER);
    if (bad) return -1;
    prev_x = xo;
    prev_xt = xto;
  }
  c->t_done = now() - c->started;
  return 0;
}

// ------------------------------------------------------------------------------------------------
// One separate: a song from the cache, or separated now
// ------------------------------------------------------------------------------------------------
typedef struct {
  char wem[PATH_MAX], pcm[PATH_MAX], key[256];
  uint32_t id;
  int part;
  uint64_t gen;
  double asked;
  int64_t asked_unix;
} job;

typedef struct {
  double decode_s, first_s, done_s;
  int segments;
  uint32_t frames, rate;
} job_times;

// Separates the job's song into its .pcm. 0 done, 1 superseded, -1 failed (the .pcm, if made, says so).
static int run_separation(const job *j, job_times *t, char *err, size_t errlen) {
  song s = {0};
  pcm_out out = {0};
  sep c = {0};
  if (load_song(j->wem, &s, &t->decode_s, err, errlen)) return -1;
  t->frames = s.frames;
  t->rate = s.rate;
  int rc = -1;
  if (!model_load(err, errlen) &&
      !pcm_create(&out, j->pcm, j->id, j->part, s.rate, s.channels, s.frames, err, errlen)) {
    c.s = &s;
    c.out = &out;
    c.source = PART_SOURCE[j->part];
    c.gen = j->gen;
    c.started = j->asked;
    rc = separate_song(&c, err, errlen);
    t->segments = c.nseg;
    t->first_s = c.t_first;
    t->done_s = c.t_done;
    if (rc != 1) pcm_state(&out, rc ? STATE_FAILED : STATE_DONE);
  }
  sep_free(&c);
  pcm_close(&out);
  free(s.pcm);
  return rc;
}

// ------------------------------------------------------------------------------------------------
// The cache: the song without the part as AAC, an index of plays, and a budget
// ------------------------------------------------------------------------------------------------
typedef struct {
  uint32_t id;
  int part;
  uint64_t bytes;
  uint32_t plays;
  int64_t last;  // last played, Unix time
} entry;

static entry *g_ix;
static int g_nix;
static pthread_mutex_t g_ix_lock = PTHREAD_MUTEX_INITIALIZER;
static _Atomic uint64_t g_budget = 500ull * 1000000;
static _Atomic uint32_t g_playing;  // the song asked for last, which is never removed

static void m4a_path(char *out, size_t n, uint32_t id, int part, const char *suffix) {
  snprintf(out, n, "%s/%u.%s%s.m4a", g_cache, id, PART_NAME[part], suffix);
}

static int part_of(const char *s) { return !strcmp(s, "guitar") ? PART_GUITAR : !strcmp(s, "bass") ? PART_BASS : -1; }

static void index_save(void) {
  char path[PATH_MAX], tmp[PATH_MAX + 8];
  snprintf(path, sizeof path, "%s/index.tsv", g_cache);
  snprintf(tmp, sizeof tmp, "%s.new", path);
  FILE *f = fopen(tmp, "w");
  if (!f) return;
  for (int i = 0; i < g_nix; i++)
    fprintf(f, "%u\t%s\t%llu\t%u\t%lld\n", g_ix[i].id, PART_NAME[g_ix[i].part], (unsigned long long)g_ix[i].bytes,
            g_ix[i].plays, (long long)g_ix[i].last);
  if (!fclose(f)) rename(tmp, path);
}

static entry *index_find(uint32_t id, int part) {
  for (int i = 0; i < g_nix; i++)
    if (g_ix[i].id == id && g_ix[i].part == part) return &g_ix[i];
  return NULL;
}

static entry *index_add(uint32_t id, int part) {
  entry *e = index_find(id, part);
  if (e) return e;
  entry *more = realloc(g_ix, (size_t)(g_nix + 1) * sizeof *more);
  if (!more) return NULL;
  g_ix = more;
  g_ix[g_nix] = (entry){id, part, 0, 0, 0};
  return &g_ix[g_nix++];
}

static void index_remove(entry *e) {
  char path[PATH_MAX];
  m4a_path(path, sizeof path, e->id, e->part, "");
  unlink(path);
  *e = g_ix[--g_nix];
}

// The index as it was, without entries whose file has gone, and no files the index doesn't know (left by a crash)
static void index_load(void) {
  char path[PATH_MAX], line[256];
  snprintf(path, sizeof path, "%s/index.tsv", g_cache);
  FILE *f = fopen(path, "r");
  while (f && fgets(line, sizeof line, f)) {
    unsigned id, plays;
    unsigned long long bytes;
    long long last;
    char part[16];
    struct stat st;
    if (sscanf(line, "%u\t%15[a-z]\t%llu\t%u\t%lld", &id, part, &bytes, &plays, &last) != 5 || part_of(part) < 0)
      continue;
    m4a_path(path, sizeof path, id, part_of(part), "");
    if (stat(path, &st)) continue;
    entry *e = index_add(id, part_of(part));
    if (e) *e = (entry){id, part_of(part), (uint64_t)st.st_size, plays, last};
  }
  if (f) fclose(f);
  DIR *d = opendir(g_cache);
  for (struct dirent *de; d && (de = readdir(d));) {
    unsigned id;
    char part[16], rest[16];
    if (sscanf(de->d_name, "%u.%15[a-z]%15s", &id, part, rest) != 3 || strstr(de->d_name, ".m4a") == NULL) continue;
    if (!strcmp(rest, ".m4a") && part_of(part) >= 0 && index_find(id, part_of(part))) continue;
    snprintf(path, sizeof path, "%s/%s", g_cache, de->d_name);
    unlink(path);
  }
  if (d) closedir(d);
  index_save();
}

static double score(const entry *e, int64_t t) { return e->plays * pow(0.5, (double)(t - e->last) / (30 * 86400.0)); }

// Over the budget, the lowest score goes first: its plays, halved for every 30 days since it was last played
static void evict_locked(void) {
  uint64_t total = 0, budget = atomic_load(&g_budget);
  for (int i = 0; i < g_nix; i++) total += g_ix[i].bytes;
  int64_t t = time(NULL);
  while (total > budget) {
    entry *low = NULL;
    for (int i = 0; i < g_nix; i++)
      if (g_ix[i].id != atomic_load(&g_playing) && (!low || score(&g_ix[i], t) < score(low, t))) low = &g_ix[i];
    if (!low) break;
    say("cache: over %llu MB, removing %u %s (%u plays, score %.3f)", (unsigned long long)(budget / 1000000), low->id,
        PART_NAME[low->part], low->plays, score(low, t));
    total -= low->bytes;
    index_remove(low);
  }
  index_save();
}

static int encode_m4a(const char *path, const int16_t *pcm, uint32_t frames, uint32_t ch, uint32_t rate, char *err,
                      size_t errlen) {
  AudioStreamBasicDescription aac = {.mSampleRate = rate, .mFormatID = kAudioFormatMPEG4AAC, .mChannelsPerFrame = ch};
  AudioStreamBasicDescription lin = {.mSampleRate = rate,
                                     .mFormatID = kAudioFormatLinearPCM,
                                     .mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked,
                                     .mBytesPerPacket = 2 * ch,
                                     .mFramesPerPacket = 1,
                                     .mBytesPerFrame = 2 * ch,
                                     .mChannelsPerFrame = ch,
                                     .mBitsPerChannel = 16};
  CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)path, (CFIndex)strlen(path), false);
  ExtAudioFileRef f = NULL;
  OSStatus st = ExtAudioFileCreateWithURL(url, kAudioFileM4AType, &aac, NULL, kAudioFileFlags_EraseFile, &f);
  CFRelease(url);
  if (st) FAIL("can't create %s (%d)", path, (int)st);
  st = ExtAudioFileSetProperty(f, kExtAudioFileProperty_ClientDataFormat, sizeof lin, &lin);
  AudioConverterRef conv = NULL;
  UInt32 size = sizeof conv, rate_bits = ch == 1 ? 128000 : 256000;
  if (!st) st = ExtAudioFileGetProperty(f, kExtAudioFileProperty_AudioConverter, &size, &conv);
  if (!st) st = AudioConverterSetProperty(conv, kAudioConverterEncodeBitRate, sizeof rate_bits, &rate_bits);
  CFArrayRef config = NULL;
  if (!st) st = ExtAudioFileSetProperty(f, kExtAudioFileProperty_ConverterConfig, sizeof config, &config);
  for (uint32_t done = 0; !st && done < frames;) {
    uint32_t n = frames - done < 32768 ? frames - done : 32768;
    AudioBufferList abl = {1, {{ch, n * 2 * ch, (void *)(pcm + (size_t)done * ch)}}};
    st = ExtAudioFileWrite(f, n, &abl);
    done += n;
  }
  OSStatus st2 = ExtAudioFileDispose(f);
  if (st || st2) FAIL("AAC encoding failed (%d)", (int)(st ? st : st2));
  return 0;
}

// After a separation: the song without the part from its finished .pcm (mapped here, so a newer .pcm in its place
// doesn't matter) into the cache, on the cache's own queue so the next song doesn't wait for it
static void cache_store(dispatch_queue_t q, const char *pcm_path, uint32_t id, int part, int64_t asked) {
  int fd = open(pcm_path, O_RDONLY);
  struct stat st;
  if (fd < 0 || fstat(fd, &st) || st.st_size < HEADER) {
    if (fd >= 0) close(fd);
    return;
  }
  size_t size = (size_t)st.st_size;
  uint8_t *base = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
  close(fd);
  if (base == MAP_FAILED) return;
  dispatch_async(q, ^{
    uint32_t h[5];
    memcpy(h, base + 16, sizeof h);  // rate, channels, total, state, ready
    char tmp[PATH_MAX], path[PATH_MAX], err[256];
    m4a_path(tmp, sizeof tmp, id, part, ".new");
    m4a_path(path, sizeof path, id, part, "");
    double t = now();
    struct stat ms;
    if (h[3] == STATE_DONE &&
        !encode_m4a(tmp, (const int16_t *)(base + HEADER), h[2], h[1], h[0], err, sizeof err) && !rename(tmp, path) &&
        !stat(path, &ms)) {
      pthread_mutex_lock(&g_ix_lock);
      entry *e = index_add(id, part);
      if (e) {
        e->bytes = (uint64_t)ms.st_size;
        e->plays++;
        e->last = asked;
      }
      say("cache: %u %s stored, %.1f MB in %.1f s", id, PART_NAME[part], ms.st_size / 1e6, now() - t);
      evict_locked();
      pthread_mutex_unlock(&g_ix_lock);
    } else {
      unlink(tmp);
      say("cache: %u %s not stored: %s", id, PART_NAME[part], h[3] == STATE_DONE ? err : "not finished");
    }
    munmap(base, size);
  });
}

// A cached song decoded into the .pcm, ready frames as they come. 0 done, 1 superseded, -1 not usable.
static int from_cache(const job *j, const char *m4a, char *err, size_t errlen) {
  size_t wl = 0;
  uint8_t *w = read_all(j->wem, &wl);
  ww_audio h;
  int bad = !w || ww_probe(w, wl, &h, err, errlen);
  free(w);
  if (bad) return -1;
  CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)m4a, (CFIndex)strlen(m4a), false);
  ExtAudioFileRef f = NULL;
  OSStatus st = ExtAudioFileOpenURL(url, &f);
  CFRelease(url);
  if (st) FAIL("can't open %s (%d)", m4a, (int)st);
  AudioStreamBasicDescription ff = {0};
  UInt32 size = sizeof ff;
  st = ExtAudioFileGetProperty(f, kExtAudioFileProperty_FileDataFormat, &size, &ff);
  if (st || (uint32_t)ff.mSampleRate != h.rate || ff.mChannelsPerFrame != h.channels) {
    ExtAudioFileDispose(f);
    FAIL("%s doesn't fit the song", m4a);
  }
  AudioStreamBasicDescription lin = {.mSampleRate = h.rate,
                                     .mFormatID = kAudioFormatLinearPCM,
                                     .mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked,
                                     .mBytesPerPacket = 2 * h.channels,
                                     .mFramesPerPacket = 1,
                                     .mBytesPerFrame = 2 * h.channels,
                                     .mChannelsPerFrame = h.channels,
                                     .mBitsPerChannel = 16};
  pcm_out out = {0};
  if (ExtAudioFileSetProperty(f, kExtAudioFileProperty_ClientDataFormat, sizeof lin, &lin) ||
      pcm_create(&out, j->pcm, j->id, j->part, h.rate, h.channels, h.frames, err, errlen)) {
    ExtAudioFileDispose(f);
    if (!*err) snprintf(err, errlen, "can't decode %s", m4a);
    return -1;
  }
  uint32_t got = 0;
  int rc = 0;
  while (got < h.frames) {
    if (j->gen && atomic_load(&g_gen) != j->gen) {
      rc = 1;
      break;
    }
    UInt32 n = h.frames - got < 65536 ? h.frames - got : 65536;
    AudioBufferList abl = {1, {{h.channels, n * 2 * h.channels, pcm_data(&out) + (size_t)got * h.channels}}};
    if (ExtAudioFileRead(f, &n, &abl) || !n) break;
    got += n;
    pcm_ready(&out, got);
  }
  ExtAudioFileDispose(f);
  if (!rc) {
    // short: the mod plays the rest of the song as it is
    if (got < h.frames) say("cache: %s ends %u frames before the song does", m4a, h.frames - got);
    pcm_state(&out, got < h.frames ? STATE_FAILED : STATE_DONE);
  }
  pcm_close(&out);
  return rc;
}

// ------------------------------------------------------------------------------------------------
// serve
// ------------------------------------------------------------------------------------------------
static dispatch_queue_t g_jobq, g_cacheq;
static char g_last_pcm[PATH_MAX];  // the helper keeps one .pcm (the job queue's)
static _Atomic uint64_t g_finished;  // the last job that ran to its end

static void run_job(job *j) {
  if (atomic_load(&g_gen) != j->gen) return;  // a newer song was asked for before this one started
  char m4a[PATH_MAX], err[512] = "";
  if (*g_last_pcm && strcmp(g_last_pcm, j->pcm)) unlink(g_last_pcm);
  snprintf(g_last_pcm, sizeof g_last_pcm, "%s", j->pcm);
  say("separate %s (%u) without the %s, load %.2f", j->key, j->id, PART_NAME[j->part], load1());
  m4a_path(m4a, sizeof m4a, j->id, j->part, "");
  pthread_mutex_lock(&g_ix_lock);
  entry *e = index_find(j->id, j->part);
  int cached = e && !access(m4a, R_OK);
  if (cached) {
    e->plays++;
    e->last = j->asked_unix;
    index_save();
  }
  pthread_mutex_unlock(&g_ix_lock);
  if (cached) {
    int rc = from_cache(j, m4a, err, sizeof err);
    if (rc >= 0) {
      say(rc ? "  stopped for a newer song" : "  from the cache in %.2f s", now() - j->asked);
      if (!rc) atomic_store(&g_finished, j->gen);
      return;
    }
    say("  the cached copy can't be used (%s): separating again", err);
    pthread_mutex_lock(&g_ix_lock);
    if ((e = index_find(j->id, j->part))) index_remove(e);
    index_save();
    pthread_mutex_unlock(&g_ix_lock);
  }
  job_times t = {0};
  int rc = run_separation(j, &t, err, sizeof err);
  if (rc < 0) {
    say("  failed: %s", err);
  } else if (rc > 0) {
    say("  stopped for a newer song");
  } else {
    say("  %u frames at %u Hz decoded in %.2f s; first 30 s ready in %.2f s, all in %.2f s (%d segments)", t.frames,
        t.rate, t.decode_s, t.first_s, t.done_s, t.segments);
    cache_store(g_cacheq, j->pcm, j->id, j->part, j->asked_unix);
  }
  atomic_store(&g_finished, j->gen);
}

static void clean_work(void) {
  DIR *d = opendir(g_work);
  for (struct dirent *de; d && (de = readdir(d));) {
    size_t l = strlen(de->d_name);
    if ((l > 4 && !strcmp(de->d_name + l - 4, ".pcm")) || (l > 8 && !strcmp(de->d_name + l - 8, ".pcm.new"))) {
      char path[PATH_MAX];
      snprintf(path, sizeof path, "%s/%s", g_work, de->d_name);
      unlink(path);
    }
  }
  if (d) closedir(d);
}

static void command(char *line, uint32_t *cur_id, int *cur_part) {
  char *f[6] = {0};
  int n = 0;
  for (char *s = line, *t; n < 6 && (t = strsep(&s, "\t"));) f[n++] = t;
  if (!f[0] || !*f[0]) return;  // the heartbeat
  if (!strcmp(f[0], "budget") && n >= 2) {
    long mb = strtol(f[1], NULL, 10);
    if (mb < 1) mb = 1;
    if ((uint64_t)mb * 1000000 != atomic_load(&g_budget)) {
      atomic_store(&g_budget, (uint64_t)mb * 1000000);
      say("cache budget %ld MB", mb);
      dispatch_async(g_cacheq, ^{
        pthread_mutex_lock(&g_ix_lock);
        evict_locked();
        pthread_mutex_unlock(&g_ix_lock);
      });
    }
    return;
  }
  if (!strcmp(f[0], "separate") && n >= 4 && part_of(f[3]) >= 0) {
    uint32_t id = (uint32_t)strtoul(f[2], NULL, 10);
    int part = part_of(f[3]);
    uint64_t gen = atomic_load(&g_gen);
    if (gen && atomic_load(&g_finished) != gen && id == *cur_id && part == *cur_part) {
      say("separate %u %s: already under way", id, f[3]);
      return;
    }
    job *j = calloc(1, sizeof *j);
    if (!j) return;
    snprintf(j->wem, sizeof j->wem, "%s", f[1]);
    snprintf(j->pcm, sizeof j->pcm, "%s/%u.%s.pcm", g_work, id, PART_NAME[part]);
    snprintf(j->key, sizeof j->key, "%s", n >= 5 ? f[4] : "");
    j->id = id;
    j->part = part;
    j->asked = now();
    j->asked_unix = time(NULL);
    j->gen = atomic_fetch_add(&g_gen, 1) + 1;
    atomic_store(&g_playing, id);
    *cur_id = id;
    *cur_part = part;
    dispatch_async(g_jobq, ^{
      run_job(j);
      free(j);
    });
    return;
  }
  say("unknown command: %s", f[0]);
}

static int serve(void) {
  char path[PATH_MAX];
  mkdir_p(g_work);
  snprintf(path, sizeof path, "%s/rs-stems.log", g_cache);
  struct stat st;
  int trunc = !stat(path, &st) && st.st_size > LOG_MAX;
  g_logfd = open(path, O_WRONLY | O_CREAT | O_APPEND | (trunc ? O_TRUNC : 0), 0644);
  if (g_logfd < 0) g_logfd = 2;
  // one helper at a time: a second one waits until the first has ended (the game started again before it timed out)
  snprintf(path, sizeof path, "%s/rs-stems.lock", g_cache);
  int lock = open(path, O_RDWR | O_CREAT, 0644);
  if (lock < 0 || flock(lock, LOCK_EX)) {
    say("can't lock %s: %s", path, strerror(errno));
    return 1;
  }
  say("rs-stems %d started (cache %s)", (int)getpid(), g_cache);
  snprintf(path, sizeof path, "%s/commands", g_cache);
  if (!lstat(path, &st) && !S_ISFIFO(st.st_mode)) unlink(path);
  if (mkfifo(path, 0600) && errno != EEXIST) {
    say("can't make the FIFO %s: %s", path, strerror(errno));
    return 1;
  }
  int fd = open(path, O_RDONLY | O_NONBLOCK);
  int keep = open(path, O_WRONLY | O_NONBLOCK);  // a writer of our own, so the FIFO never reads as ended
  if (fd < 0 || keep < 0) {
    say("can't open the FIFO %s: %s", path, strerror(errno));
    return 1;
  }
  clean_work();
  pthread_mutex_lock(&g_ix_lock);
  index_load();
  pthread_mutex_unlock(&g_ix_lock);
  dispatch_queue_attr_t utility =
      dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_UTILITY, 0);  // out of the game's way
  g_jobq = dispatch_queue_create("rs-stems.jobs", utility);
  g_cacheq = dispatch_queue_create("rs-stems.cache", utility);
  dispatch_async(g_jobq, ^{
    char err[512];
    if (model_load(err, sizeof err))
      say("%s", err);
    else
      say("model loaded in %.2f s", g_model_load_s);
  });
  char buf[8192];
  size_t have = 0;
  uint32_t cur_id = 0;
  int cur_part = -1;
  double beat = now();
  for (;;) {
    struct pollfd p = {fd, POLLIN, 0};
    if (poll(&p, 1, 1000) > 0) {
      ssize_t r = read(fd, buf + have, sizeof buf - 1 - have);
      if (r > 0) have += (size_t)r;
      char *start = buf, *nl;
      while ((nl = memchr(start, '\n', have - (size_t)(start - buf)))) {
        *nl = 0;
        beat = now();
        command(start, &cur_id, &cur_part);
        start = nl + 1;
      }
      have -= (size_t)(start - buf);
      memmove(buf, start, have);
      if (have == sizeof buf - 1) have = 0;  // a line too long for any command
    }
    if (now() - beat > HEARTBEAT_TIMEOUT) {
      say("no heartbeat for %.0f s: ending", HEARTBEAT_TIMEOUT);
      return 0;
    }
  }
}

// ------------------------------------------------------------------------------------------------
// The other commands
// ------------------------------------------------------------------------------------------------
static int warm(void) {
  char err[512];
  printf("load %.2f; model %s/%s\n", load1(), g_home, MODEL_NAME);
  fflush(stdout);
  if (model_load(err, sizeof err)) {
    fprintf(stderr, "rs-stems: %s\n", err);
    return 1;
  }
  printf("model loaded in %.2f s\n", g_model_load_s);
  float *x = calloc(4 * (size_t)SPEC_PLANE, sizeof(float)), *xt = calloc(2 * (size_t)FOLD, sizeof(float));
  MLMultiArray *xo = nil, *xto = nil;
  double t = now();
  if (!x || !xt || model_predict(x, xt, &xo, &xto, err, sizeof err)) {
    fprintf(stderr, "rs-stems: %s\n", x && xt ? err : "out of memory");
    return 1;
  }
  printf("first segment in %.2f s\n", now() - t);
  free(x);
  free(xt);
  return 0;
}

static int separate_once(const char *wem, const char *id, const char *part, const char *out) {
  job j = {0};
  snprintf(j.wem, sizeof j.wem, "%s", wem);
  snprintf(j.pcm, sizeof j.pcm, "%s", out);
  j.id = (uint32_t)strtoul(id, NULL, 10);
  j.part = part_of(part);
  if (j.part < 0) {
    fprintf(stderr, "rs-stems: the part is guitar or bass\n");
    return 2;
  }
  printf("load %.2f; overlap %.2f\n", load1(), overlap());
  char err[512];
  if (model_load(err, sizeof err)) {
    fprintf(stderr, "rs-stems: %s\n", err);
    return 1;
  }
  printf("model loaded in %.2f s\n", g_model_load_s);
  fflush(stdout);
  __block int rc = 0;
  __block job_times t = {0};
  dispatch_semaphore_t done = dispatch_semaphore_create(0);
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
    char e[512];
    job jj = j;
    jj.asked = now();
    rc = run_separation(&jj, &t, e, sizeof e);
    if (rc) fprintf(stderr, "rs-stems: %s\n", e);
    dispatch_semaphore_signal(done);
  });
  dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
  if (rc) return 1;
  struct rusage ru;
  getrusage(RUSAGE_SELF, &ru);
  printf("%u frames at %u Hz decoded in %.2f s; first 30 s ready in %.2f s, all in %.2f s (%d segments); "
         "peak memory %.0f MB\n",
         t.frames, t.rate, t.decode_s, t.first_s, t.done_s, t.segments, ru.ru_maxrss / 1048576.0);
  return 0;
}

static int decode_once(const char *wem, const char *out) {
  song s = {0};
  double secs;
  char err[512];
  if (load_song(wem, &s, &secs, err, sizeof err)) {
    fprintf(stderr, "rs-stems: %s\n", err);
    return 1;
  }
  FILE *f = fopen(out, "wb");
  if (!f) {
    fprintf(stderr, "rs-stems: can't write %s\n", out);
    return 1;
  }
  uint32_t bytes = s.frames * s.channels * 2, h[4] = {16, 1 | s.channels << 16, s.rate, s.rate * s.channels * 2};
  uint16_t align[2] = {(uint16_t)(s.channels * 2), 16};
  uint32_t riff = 36 + bytes;
  fwrite("RIFF", 1, 4, f);
  fwrite(&riff, 4, 1, f);
  fwrite("WAVEfmt ", 1, 8, f);
  fwrite(h, 4, 4, f);
  fwrite(align, 2, 2, f);
  fwrite("data", 1, 4, f);
  fwrite(&bytes, 4, 1, f);
  fwrite(s.pcm, 2, (size_t)s.frames * s.channels, f);
  fclose(f);
  printf("%u frames at %u Hz, %u channels, decoded in %.2f s\n", s.frames, s.rate, s.channels, secs);
  free(s.pcm);
  return 0;
}

int main(int argc, char **argv) {
  @autoreleasepool {
    folders();
    if (argc == 2 && !strcmp(argv[1], "serve")) return serve();
    if (argc == 2 && !strcmp(argv[1], "warm")) return warm();
    if (argc == 6 && !strcmp(argv[1], "separate")) return separate_once(argv[2], argv[3], argv[4], argv[5]);
    if (argc == 4 && !strcmp(argv[1], "decode")) return decode_once(argv[2], argv[3]);
    fprintf(stderr,
            "usage: rs-stems serve | warm | separate <wem> <file id> <guitar|bass> <out.pcm> | decode <wem> <out.wav>\n"
            "  helper and model: %s\n  cache: %s\n",
            g_home, g_cache);
    return 2;
  }
}
