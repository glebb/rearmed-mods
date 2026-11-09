// Test tool for rs-stems (stems/test.sh): reads 16-bit WAV files and the helper's .pcm files.
//
//   pcmtool header <file.pcm>              state, ready frames, total frames, rate, channels
//   pcmtool compare <ref> <file>           lengths, the largest difference (in LSB and dB of full scale), how many
//                                          samples differ; fails on other lengths or more than 2 LSB apart
//   pcmtool stems <song> <ref> <file> [min part] [min song]
//                                          ref and file are the song without a part: the SNR of the part each takes
//                                          out (song - ref against song - file), and of the two files themselves;
//                                          fails below the minimums (dB) given
//   pcmtool resample <in> <rate> <out.wav>  the rs-stems resampler (stems_dsp.c), to compare a song at another rate
//   pcmtool watch <pcm> <seconds> [inode]  waits for the helper to finish the file (one with another inode, if given),
//                                          as the mod sees it: when the frames got ready, whether that only grew
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "../stems_dsp.h"

typedef struct {
  int16_t *pcm;
  uint32_t frames, channels, rate;
  uint32_t h[7];  // a .pcm's header: file ID, part, rate, channels, total, state, ready
} audio;

static uint8_t *slurp(const char *path, size_t *n) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  *n = (size_t)ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *b = malloc(*n ? *n : 1);
  if (b && fread(b, 1, *n, f) != *n) {
    free(b);
    b = NULL;
  }
  fclose(f);
  return b;
}

static int load(const char *path, audio *a) {
  size_t n = 0;
  uint8_t *b = slurp(path, &n);
  memset(a, 0, sizeof *a);
  if (!b) return fprintf(stderr, "pcmtool: can't read %s\n", path), -1;
  if (n >= 64 && !memcmp(b, "RSSTEMS1", 8)) {
    memcpy(a->h, b + 8, sizeof a->h);
    a->rate = a->h[2];
    a->channels = a->h[3];
    a->frames = a->h[4];
    if (n < 64 + (size_t)a->frames * a->channels * 2) return fprintf(stderr, "pcmtool: %s is short\n", path), -1;
    a->pcm = (int16_t *)(b + 64);
    return 0;
  }
  if (n < 12 || memcmp(b, "RIFF", 4) || memcmp(b + 8, "WAVE", 4)) return fprintf(stderr, "pcmtool: %s?\n", path), -1;
  for (size_t at = 12; at + 8 <= n;) {
    uint32_t size;
    memcpy(&size, b + at + 4, 4);
    if (!memcmp(b + at, "fmt ", 4)) {
      uint16_t fmt[8];
      memcpy(fmt, b + at + 8, 16);
      if (fmt[0] != 1 || fmt[7] != 16) return fprintf(stderr, "pcmtool: %s isn't 16-bit PCM\n", path), -1;
      a->channels = fmt[1];
      memcpy(&a->rate, b + at + 12, 4);
    } else if (!memcmp(b + at, "data", 4) && a->channels) {
      if (size > n - at - 8) size = (uint32_t)(n - at - 8);
      a->pcm = (int16_t *)(b + at + 8);
      a->frames = size / 2 / a->channels;
      return 0;
    }
    at += 8 + size + (size & 1);
  }
  return fprintf(stderr, "pcmtool: no data in %s\n", path), -1;
}

static double now(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + t.tv_nsec * 1e-9;
}

// Reads the header as the mod does, every 20 ms, from the file's first appearance with the full size
static int watch(const char *path, double limit, unsigned long long not_inode) {
  double t0 = now(), first30 = 0;
  uint32_t h[7] = {0}, last = 0, changes = 0;
  int shrank = 0, seen = 0;
  while (now() - t0 < limit) {
    struct stat st;
    int fd = open(path, O_RDONLY);
    if (fd >= 0 && !fstat(fd, &st) && (unsigned long long)st.st_ino != not_inode && st.st_size >= 64 &&
        pread(fd, h, sizeof h, 8) == sizeof h && (uint64_t)st.st_size >= 64 + (uint64_t)h[4] * h[3] * 2) {
      if (!seen++) printf("file there after %.2f s (%u frames at %u Hz)\n", now() - t0, h[4], h[2]);
      if (h[6] < last) shrank = 1;
      if (h[6] != last) changes++;
      if (!first30 && h[2] && h[6] >= 30 * h[2]) first30 = now() - t0;
      last = h[6];
      if (h[5]) {
        close(fd);
        printf("state %u after %.2f s, first 30 s ready after %.2f s; ready %u of %u, %u changes, %s\n", h[5],
               now() - t0, first30, h[6], h[4], changes, shrank ? "SHRANK" : "only grew");
        return h[5] == 1 && h[6] == h[4] && !shrank ? 0 : 1;
      }
    }
    if (fd >= 0) close(fd);
    usleep(20000);
  }
  printf("gave up after %.0f s (ready %u, state %u)\n", limit, last, h[5]);
  return 1;
}

static int resample(const char *in, int to, const char *path) {
  audio a;
  resampler r = {0};
  if (load(in, &a) || to <= 0 || rs_init(&r, (int)a.rate, to)) return 1;
  size_t n = rs_length(&r, a.frames);
  float *x = malloc(a.frames * sizeof(float)), *y = malloc(n * sizeof(float));
  int16_t *out = malloc(n * a.channels * sizeof(int16_t));
  FILE *f = fopen(path, "wb");
  if (!x || !y || !out || !f) return 1;
  for (uint32_t c = 0; c < a.channels; c++) {
    for (size_t i = 0; i < a.frames; i++) x[i] = a.pcm[i * a.channels + c] / 32768.f;
    rs_run(&r, x, a.frames, y, 0, n);
    for (size_t i = 0; i < n; i++) {
      float v = rintf(y[i] * 32768.f);
      out[i * a.channels + c] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
  }
  uint32_t bytes = (uint32_t)(n * a.channels * 2), riff = 36 + bytes;
  uint32_t fmt[4] = {16, 1 | a.channels << 16, (uint32_t)to, (uint32_t)to * a.channels * 2};
  uint16_t align[2] = {(uint16_t)(a.channels * 2), 16};
  fwrite("RIFF", 1, 4, f);
  fwrite(&riff, 4, 1, f);
  fwrite("WAVEfmt ", 1, 8, f);
  fwrite(fmt, 4, 4, f);
  fwrite(align, 2, 2, f);
  fwrite("data", 1, 4, f);
  fwrite(&bytes, 4, 1, f);
  fwrite(out, 2, n * a.channels, f);
  fclose(f);
  printf("%u frames at %u Hz -> %zu frames at %d Hz\n", a.frames, a.rate, n, to);
  return 0;
}

int main(int argc, char **argv) {
  if (argc == 5 && !strcmp(argv[1], "resample")) return resample(argv[2], atoi(argv[3]), argv[4]);
  if ((argc == 4 || argc == 5) && !strcmp(argv[1], "watch"))
    return watch(argv[2], atof(argv[3]), argc == 5 ? strtoull(argv[4], NULL, 10) : 0);
  audio a, b, s;
  if (argc == 3 && !strcmp(argv[1], "header")) {
    if (load(argv[2], &a)) return 1;
    printf("%u %u %u %u %u\n", a.h[5], a.h[6], a.h[4], a.h[2], a.h[3]);
    return 0;
  }
  if (argc == 4 && !strcmp(argv[1], "compare")) {
    if (load(argv[2], &a) || load(argv[3], &b)) return 1;
    size_t n = (size_t)(a.frames < b.frames ? a.frames : b.frames) * a.channels, differ = 0;
    int worst = 0;
    for (size_t i = 0; i < n && a.channels == b.channels; i++) {
      int d = abs(a.pcm[i] - b.pcm[i]);
      differ += d != 0;
      if (d > worst) worst = d;
    }
    printf("frames %u / %u, channels %u / %u; largest difference %d LSB (%.1f dB), %zu of %zu samples differ\n",
           a.frames, b.frames, a.channels, b.channels, worst, worst ? 20 * log10(worst / 32768.0) : -INFINITY, differ, n);
    return a.frames != b.frames || a.channels != b.channels || worst > 2;
  }
  if (argc >= 5 && argc <= 7 && !strcmp(argv[1], "stems")) {
    if (load(argv[2], &s) || load(argv[3], &a) || load(argv[4], &b)) return 1;
    if (b.frames > a.frames && b.frames - a.frames <= 2) b.frames = a.frames;  // a resampled file's last frame or two
    if (a.frames != b.frames || a.channels != b.channels || s.channels != a.channels || s.frames < a.frames) {
      printf("lengths differ: song %u, ref %u, file %u frames\n", s.frames, a.frames, b.frames);
      return 1;
    }
    double part = 0, part_err = 0, back = 0, back_err = 0;
    for (size_t i = 0; i < (size_t)a.frames * a.channels; i++) {
      double r = s.pcm[i] - a.pcm[i], e = s.pcm[i] - b.pcm[i], d = r - e;
      part += r * r;
      part_err += d * d;
      back += (double)a.pcm[i] * a.pcm[i];
    }
    back_err = part_err;
    double p = 10 * log10(part / (part_err + 1e-9)), q = 10 * log10(back / (back_err + 1e-9));
    printf("part taken out: SNR %.1f dB; song without it: SNR %.1f dB\n", p, q);
    return (argc > 5 && p < atof(argv[5])) || (argc > 6 && q < atof(argv[6]));
  }
  fprintf(stderr, "usage: pcmtool header <pcm> | compare <ref> <file> | stems <song> <ref> <file> [dB] [dB] |\n"
                  "       resample <in> <rate> <out.wav> | watch <pcm> <seconds> [inode]\n");
  return 2;
}
