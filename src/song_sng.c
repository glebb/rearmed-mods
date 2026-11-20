// A song's notes from its archive (song_sng.h)
#include "song_sng.h"

#include <CommonCrypto/CommonCryptor.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <zlib.h>

// The SNG keys, public since the custom-song tools: the Mac's and the PC's
static const uint8_t KEY_MAC[32] = {0x98, 0x21, 0x33, 0x0E, 0x34, 0xB9, 0x1F, 0x70, 0xD0, 0xA4, 0x8C, 0xBD, 0x62, 0x59, 0x93, 0x12,
                                    0x69, 0x70, 0xCE, 0xA0, 0x91, 0x92, 0xC0, 0xE6, 0xCD, 0xA6, 0x76, 0xCC, 0x98, 0x38, 0x28, 0x9D};
static const uint8_t KEY_PC[32] = {0xCB, 0x64, 0x8D, 0xF3, 0xD1, 0x2A, 0x16, 0xBF, 0x71, 0x70, 0x14, 0x14, 0xE6, 0x96, 0x19, 0xEC,
                                   0x17, 0x1C, 0xCA, 0x5D, 0x2A, 0x14, 0x2E, 0x3E, 0x59, 0xDE, 0x7A, 0xDD, 0xA1, 0x8A, 0x3A, 0x30};

enum { MAX_PLAIN = 64 << 20 };

uint8_t *sng_decrypt(const uint8_t *file, size_t n, int mac, size_t *out_n) {
  if (n < 24 + 4 || file[0] != 0x4a || file[1] || file[2] || file[3]) return NULL;
  size_t en = n - 24;
  uint8_t *dec = malloc(en);
  if (!dec) return NULL;
  uint32_t flags;
  memcpy(&flags, file + 4, 4);
  CCCryptorRef c = NULL;
  size_t moved = 0;
  int ok = flags == 1 ? (memcpy(dec, file + 24, en), 1) :  // compressed, not encrypted (Plain Packages)
           CCCryptorCreateWithMode(kCCDecrypt, kCCModeCTR, kCCAlgorithmAES, ccNoPadding, file + 8, mac ? KEY_MAC : KEY_PC, 32,
                                   NULL, 0, 0, kCCModeOptionCTR_BE, &c) == kCCSuccess &&
           CCCryptorUpdate(c, file + 24, en, dec, en, &moved) == kCCSuccess && moved == en;
  if (c) CCCryptorRelease(c);
  uint32_t len;
  memcpy(&len, dec, 4);
  if (!ok || !len || len > MAX_PLAIN) return free(dec), NULL;
  uint8_t *p = malloc(len);
  // the stream, then the file's signature: inflated to its end
  z_stream z = {.next_in = dec + 4, .avail_in = (uInt)(en - 4), .next_out = p, .avail_out = len};
  int r = p && inflateInit(&z) == Z_OK ? inflate(&z, Z_FINISH) : Z_DATA_ERROR;
  if (p) inflateEnd(&z);
  free(dec);
  if (r != Z_STREAM_END || z.total_out != len) return free(p), NULL;
  *out_n = len;
  return p;
}

typedef struct {
  const uint8_t *p, *e;
  int bad;
} reader;

static uint32_t u32(reader *r) {
  if (r->bad || r->e - r->p < 4) return r->bad = 1, 0;
  uint32_t v;
  memcpy(&v, r->p, 4);
  r->p += 4;
  return v;
}

static const uint8_t *skip(reader *r, size_t n) {
  const uint8_t *at = r->p;
  if (r->bad || (size_t)(r->e - r->p) < n) return r->bad = 1, r->p = r->e, NULL;
  r->p += n;
  return at;
}

static uint32_t count(reader *r, size_t each) {  // a list's count, its items skipped
  uint32_t n = u32(r);
  if (each && n > (size_t)(r->e - r->p) / each) return r->bad = 1, 0;
  skip(r, (size_t)n * each);
  return n;
}

static int32_t i32_at(const uint8_t *p) {
  int32_t v;
  memcpy(&v, p, 4);
  return v;
}

static float f32_at(const uint8_t *p) {
  float v;
  memcpy(&v, p, 4);
  return v;
}

typedef struct {
  int string, fret;  // a single note's, until the tuning is read; -1 for a chord
  sk_event e;
} note;

static int by_time(const void *a, const void *b) {
  float x = ((const sk_event *)a)->time, y = ((const sk_event *)b)->time;
  return x < y ? -1 : x > y;
}

int sng_parse(const uint8_t *d, size_t n, int bass, double cents, sng_arrangement *out) {
  static const int GUITAR[6] = {40, 45, 50, 55, 59, 64}, BASS[6] = {28, 33, 38, 43, 48, 53};
  reader r = {d, d + n, 0};
  int shift = (int)lround(cents / 100);
  memset(out, 0, sizeof *out);
  count(&r, 16);  // beats
  uint32_t phrases = u32(&r);
  const uint8_t *phrase = skip(&r, (size_t)phrases * 44);
  uint32_t templates = u32(&r);
  const uint8_t *tmpl = skip(&r, (size_t)templates * 72);
  count(&r, 2376);
  if (count(&r, 60) || r.bad) return 0;  // vocals
  uint32_t iterations = u32(&r);
  const uint8_t *iteration = skip(&r, (size_t)iterations * 24);
  count(&r, 16);
  uint32_t linked = u32(&r);
  for (uint32_t i = 0; i < linked && !r.bad; i++) skip(&r, 4), count(&r, 4);
  count(&r, 260), count(&r, 260), count(&r, 8), count(&r, 8), count(&r, 88);
  uint32_t levels = u32(&r);
  if (r.bad || levels > 64) return 0;
  // the notes of the whole chart, and each level's count; the strings' notes from the metadata, after the levels
  size_t cap = 1024, kept = 0;
  note *notes = malloc(cap * sizeof *notes);
  int *level_notes = calloc(levels + 1, sizeof *level_notes);
  for (uint32_t l = 0; l < levels && !r.bad; l++) {
    int difficulty = (int)u32(&r);
    count(&r, 28), count(&r, 12), count(&r, 20), count(&r, 20);
    uint32_t cnt = u32(&r);
    level_notes[l] = (int)cnt;
    for (uint32_t i = 0; i < cnt && !r.bad; i++) {
      const uint8_t *q = skip(&r, 63);
      count(&r, 12);
      if (r.bad) break;
      int32_t pi = i32_at(q + 32);
      int want = -1;
      if (pi >= 0 && (uint32_t)pi < iterations) {
        int32_t ph = i32_at(iteration + (size_t)pi * 24);
        if (ph >= 0 && (uint32_t)ph < phrases) want = i32_at(phrase + (size_t)ph * 44 + 4);
      }
      if (want != difficulty) continue;
      if (kept == cap) {
        note *more = realloc(notes, (cap *= 2) * sizeof *notes);
        if (!more) {
          r.bad = 1;
          break;
        }
        notes = more;
      }
      note *x = &notes[kept++];
      x->e.time = f32_at(q + 12);
      x->e.sustain = f32_at(q + 55);
      int32_t chord = i32_at(q + 20);
      x->e.n = 0;
      if (chord >= 0 && (uint32_t)chord < templates) {
        const uint8_t *t = tmpl + (size_t)chord * 72 + 16;
        for (int s = 0; s < 6; s++) {
          int32_t m = i32_at(t + 4 * s);
          if (m >= 0) x->e.midi[x->e.n++] = m + shift;
        }
        x->string = -1;
      } else {
        x->string = (int8_t)q[16], x->fret = (int8_t)q[17];
        x->e.n = x->string >= 0 && x->string < 6 ? -1 : 0;  // the 7th and 8th strings: not here
      }
    }
    count(&r, 4), count(&r, 4), count(&r, 4);
  }
  // the metadata: capo, tuning
  const uint8_t *meta = skip(&r, 40 + 1 + 32 + 2 + 4);
  uint32_t strings = u32(&r);
  int tuning[6] = {0};
  for (uint32_t s = 0; s < strings && !r.bad; s++) {
    const uint8_t *t = skip(&r, 2);
    if (t && s < 6) tuning[s] = (int16_t)(t[0] | t[1] << 8);
  }
  if (r.bad) {
    free(notes), free(level_notes);
    return 0;
  }
  int capo = (int8_t)meta[40];
  out->ev = malloc((kept ? kept : 1) * sizeof *out->ev);
  for (size_t i = 0; i < kept; i++) {
    sk_event e = notes[i].e;
    if (e.n < 0) {  // a single note: its string's note
      int s = notes[i].string, f = notes[i].fret;
      if (f == 0 && capo > 0) f = capo;
      e.n = 1;
      e.midi[0] = (bass ? BASS : GUITAR)[s] + tuning[s] + f + shift;
    }
    if (e.n > 0) out->ev[out->n++] = e;
  }
  free(notes);
  qsort(out->ev, (size_t)out->n, sizeof *out->ev, by_time);  // a merge of the levels' notes
  out->levels = (int)levels;
  out->level_notes = level_notes;
  return 1;
}

void sng_free(sng_arrangement *a) {
  free(a->level_notes);
  free(a->ev);
  memset(a, 0, sizeof *a);
}

void sng_free_all(sng_arrangement *a, int n) {
  for (int i = 0; i < n; i++) sng_free(&a[i]);
  free(a);
}

// "songs/bin/<platform>/<song_key>_<arrangement>.sng": its platform (1 the Mac, 0 the PC) and the base name's
// length without ".sng", or -1
static int sng_name(const char *name, const char *song_key, int *mac) {
  static const char *const DIRS[] = {"songs/bin/macos/", "songs/bin/generic/"};
  for (int i = 0; i < 2; i++) {
    size_t dl = strlen(DIRS[i]), kl = strlen(song_key), nl = strlen(name);
    if (strncasecmp(name, DIRS[i], dl) || nl < dl + kl + 6 || strcasecmp(name + nl - 4, ".sng")) continue;
    if (strncasecmp(name + dl, song_key, kl) || name[dl + kl] != '_') continue;
    if (strchr(name + dl, '/')) continue;
    *mac = i == 0;
    return (int)(nl - dl - 4);
  }
  return -1;
}

int sng_read_song(const char *archive, const char *song_key, const uint8_t key[32], const rsm_psarc_io *io, sng_arrangement **out) {
  *out = NULL;
  rsm_psarc *a = rsm_psarc_open_io(archive, key, io);
  if (!a) return 0;
  int files = rsm_psarc_files(a), n = 0, cap = 0;
  sng_arrangement *list = NULL;
  for (int i = 0; i < files; i++) {
    const char *name = rsm_psarc_name(a, i);
    int mac, len = sng_name(name, song_key, &mac);
    if (len < 0) continue;
    // its manifest: manifests/<dir>/<base>.json
    char want[256];
    snprintf(want, sizeof want, "/%.*s.json", len, strrchr(name, '/') + 1);
    rsm_arrangement arr = {.name = ""};
    for (int j = 0; j < files && !*arr.name; j++) {
      const char *m = rsm_psarc_name(a, j);
      size_t ml = strlen(m), wl = strlen(want);
      if (strncmp(m, "manifests/", 10) || ml <= wl || strcasecmp(m + ml - wl, want)) continue;
      char *json = rsm_psarc_read(a, j, NULL);
      if (json && !rsm_manifest_arrangement(json, &arr)) *arr.name = 0;
      free(json);
    }
    if (!*arr.name) continue;  // vocals, or a manifest that can't be read
    size_t raw_n = 0, plain_n = 0;
    uint8_t *raw = rsm_psarc_read(a, i, &raw_n), *plain = raw ? sng_decrypt(raw, raw_n, mac, &plain_n) : NULL;
    sng_arrangement got;
    if (plain && sng_parse(plain, plain_n, rsm_is_bass(&arr), arr.cents, &got)) {
      snprintf(got.name, sizeof got.name, "%s", arr.name);
      if (n == cap) {
        sng_arrangement *more = realloc(list, (size_t)(cap = cap ? cap * 2 : 4) * sizeof *list);
        if (!more) {
          sng_free(&got);
          free(plain), free(raw);
          break;
        }
        list = more;
      }
      list[n++] = got;
    }
    free(plain);
    free(raw);
  }
  rsm_psarc_close(a);
  *out = list;
  return n;
}
