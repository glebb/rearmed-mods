// rsmods_7z.c: LZMA and LZMA2 made by liblzma, a 7z archive laid out as the game's are, and the archive written again
// with a file changed (tests/vectors_7z.h, from make_7z_vectors.py); archives laid out here for a folder without a file
// and for counts the reader refuses.
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "check.h"
#include "rsmods_7z.h"
#include "vectors_7z.h"

// The same text and noise as make_7z_vectors.py
static const char *const WORDS[] = {"tuning", "string", "tone",  "lead", "rhythm",     "bass",     "standard",
                                    "drop",   "open",   "the",   "a",    "and",        "Guitarcade", "manifest",
                                    "json",   "{",      "}",     "\"",   ":",          ",",        "\n",
                                    "\t",     "0",      "-5",    "E",    "B",          "C#"};

static uint8_t *make_text(size_t n, uint32_t s) {
  uint8_t *out = malloc(n + 1);
  size_t k = 0;
  while (k < n) {
    s = s * 1103515245u + 12345u;
    for (const char *w = WORDS[(s >> 16) % (sizeof WORDS / sizeof *WORDS)]; *w && k < n; w++) out[k++] = (uint8_t)*w;
    if (k < n) out[k++] = ' ';
  }
  return out;
}

static uint8_t *make_noise(size_t n, uint32_t s) {
  uint8_t *out = malloc(n + 1);
  for (size_t k = 0; k < n; k++) {
    s = s * 1103515245u + 12345u;
    out[k] = (uint8_t)(s >> 16);
  }
  return out;
}

static int lzma_is(const unsigned char *in, size_t in_len, uint8_t b0, const uint8_t *want, size_t n) {
  uint8_t props[5] = {b0, 0, 0, 1, 0}, *out = malloc(n + 1);
  int ok = rsm_lzma_decode(props, in, in_len, out, n) && !memcmp(out, want, n);
  free(out);
  return ok;
}

static int lzma2_is(const unsigned char *in, size_t in_len, const uint8_t *want, size_t n) {
  uint8_t *out = malloc(n + 1);
  int ok = rsm_lzma2_decode(in, in_len, out, n) && !memcmp(out, want, n);
  free(out);
  return ok;
}

static void test_decode(void) {
  uint8_t *t1 = make_text(6000, 1), *t2 = make_text(3000, 2), *t3 = make_text(4000, 3), *n4 = make_noise(600, 4);
  CHECK(lzma_is(LZMA_TEXT, sizeof LZMA_TEXT, 0x5d, t1, 6000));
  CHECK(lzma_is(LZMA_LC0_LP2, sizeof LZMA_LC0_LP2, 2 * 9, t2, 3000));  // lc 0, lp 2, pb 0
  CHECK(lzma2_is(LZMA2_TEXT, sizeof LZMA2_TEXT, t3, 4000));
  CHECK(lzma2_is(LZMA2_NOISE, sizeof LZMA2_NOISE, n4, 600));
  CHECK(LZMA2_NOISE[0] == 1);  // a stored chunk
  // a cut stream, a changed byte: no output taken for the data
  CHECK(!lzma_is(LZMA_TEXT, sizeof LZMA_TEXT / 2, 0x5d, t1, 6000));
  CHECK(!lzma2_is(LZMA2_TEXT, sizeof LZMA2_TEXT - 1, t3, 4000));
  uint8_t bad[sizeof LZMA_TEXT];
  memcpy(bad, LZMA_TEXT, sizeof bad);
  bad[700] ^= 0x10;
  CHECK(!lzma_is(bad, sizeof bad, 0x5d, t1, 6000));
  uint8_t props[5] = {9 * 5 * 5, 0, 0, 1, 0}, out[16];
  CHECK(!rsm_lzma_decode(props, LZMA_TEXT, sizeof LZMA_TEXT, out, sizeof out));  // no such lc/lp/pb
  free(t1);
  free(t2);
  free(t3);
  free(n4);
}

static int round_trip(const uint8_t *data, size_t n) {
  size_t len;
  uint8_t props[5], *enc = rsm_lzma_encode_literals(data, n, &len, props), *dec = malloc(n + 1);
  int ok = enc && props[0] == 0x5d && rsm_lzma_decode(props, enc, len, dec, n) && !memcmp(dec, data, n);
  free(enc);
  free(dec);
  return ok;
}

static void test_encode(void) {
  uint8_t *t = make_text(20000, 7), *z = calloc(5000, 1), *n = make_noise(5000, 8);
  CHECK(round_trip(t, 20000));
  CHECK(round_trip(z, 5000));
  CHECK(round_trip(n, 5000));
  CHECK(round_trip((const uint8_t *)"", 0));
  size_t len;
  uint8_t props[5], *enc = rsm_lzma_encode_literals(t, 20000, &len, props);
  CHECK(enc && len < 20000);  // literals of text still pack a little
  free(enc);
  free(t);
  free(z);
  free(n);
}

static int file_is(const rsm_7z *z, const char *name, const uint8_t *want, size_t n) {
  int i = rsm_7z_find(z, name);
  size_t len = 0;
  uint8_t *d = i >= 0 ? rsm_7z_read(z, i, &len) : NULL;
  int ok = d && len == n && !memcmp(d, want, n) && d[len] == 0;
  free(d);
  return ok;
}

static void test_archive(void) {
  char why[128] = "";
  uint8_t *a = make_text(3000, 5), *b = make_text(500, 6);
  rsm_7z *z = rsm_7z_open(ARCHIVE, sizeof ARCHIVE, why, sizeof why);
  CHECK(z != NULL);
  if (!z) {
    fprintf(stderr, "open: %s\n", why);
    return;
  }
  CHECK(rsm_7z_files(z) == 3);
  CHECK(!strcmp(rsm_7z_name(z, 0), "manifests/a.json"));
  CHECK(!strcmp(rsm_7z_name(z, 1), "empty.txt"));
  CHECK(!strcmp(rsm_7z_name(z, 2), "manifests/b.json"));  // stored with a backslash
  CHECK(rsm_7z_find(z, "MANIFESTS\\A.JSON") == 0);
  CHECK(rsm_7z_find(z, "manifests/a.jso") == -1);
  CHECK(file_is(z, "manifests/a.json", a, 3000));
  CHECK(file_is(z, "manifests/b.json", b, 500));
  CHECK(file_is(z, "empty.txt", (const uint8_t *)"", 0));

  const char *hello = "{ \"hello\" : 1 }";
  int which[] = {0};
  const uint8_t *data[] = {(const uint8_t *)hello};
  size_t lens[] = {strlen(hello)}, n1 = 0, n2 = 0;
  uint8_t *w1 = rsm_7z_rewrite(z, 1, which, data, lens, &n1);
  CHECK(w1 != NULL);
  rsm_7z *z1 = w1 ? rsm_7z_open(w1, n1, why, sizeof why) : NULL;
  CHECK(z1 != NULL);
  if (z1) {
    CHECK(rsm_7z_files(z1) == 3);
    CHECK(!strcmp(rsm_7z_name(z1, 2), "manifests/b.json"));
    CHECK(file_is(z1, "manifests/a.json", (const uint8_t *)hello, strlen(hello)));
    CHECK(file_is(z1, "manifests/b.json", b, 500));
    CHECK(file_is(z1, "empty.txt", (const uint8_t *)"", 0));
    // written again from the rewritten one, with b changed too: both read back
    int both[] = {0, 2};
    const uint8_t *data2[] = {b, a};
    size_t lens2[] = {500, 3000};
    uint8_t *w2 = rsm_7z_rewrite(z1, 2, both, data2, lens2, &n2);
    rsm_7z *z2 = w2 ? rsm_7z_open(w2, n2, why, sizeof why) : NULL;
    CHECK(z2 != NULL);
    if (z2) {
      CHECK(file_is(z2, "manifests/a.json", b, 500));
      CHECK(file_is(z2, "manifests/b.json", a, 3000));
      CHECK(file_is(z2, "empty.txt", (const uint8_t *)"", 0));
    }
    rsm_7z_close(z2);
    free(w2);
    CHECK(rsm_7z_rewrite(z1, 1, (int[]){1}, data, lens, &n2) == NULL);  // the empty file has no stream to give
    rsm_7z_close(z1);
  }
  free(w1);

  // a changed byte: in a file's packed stream it fails that file's CRC; in the header, the header's
  uint8_t bad[sizeof ARCHIVE];
  memcpy(bad, ARCHIVE, sizeof bad);
  bad[40] ^= 0x01;
  rsm_7z *zb = rsm_7z_open(bad, sizeof bad, why, sizeof why);
  CHECK(zb != NULL);
  if (zb) CHECK(!file_is(zb, "manifests/a.json", a, 3000));
  rsm_7z_close(zb);
  memcpy(bad, ARCHIVE, sizeof bad);
  bad[sizeof bad - 3] ^= 0x01;
  CHECK(rsm_7z_open(bad, sizeof bad, why, sizeof why) == NULL);
  CHECK(rsm_7z_open((const uint8_t *)"PSAR", 4, why, sizeof why) == NULL);
  rsm_7z_close(z);
  free(a);
  free(b);
}

// ------------------------------------------------------------------------------------------------
// Archives laid out by hand, for what the game's don't have: stored (copy) folders, the header not packed
// ------------------------------------------------------------------------------------------------
typedef struct {
  uint8_t b[512];
  size_t n;
} bytes;

static void put(bytes *o, const void *p, size_t n) {
  memcpy(o->b + o->n, p, n);
  o->n += n;
}
static void put8(bytes *o, uint8_t v) { put(o, &v, 1); }
static void put32(bytes *o, uint32_t v) {
  for (int k = 0; k < 4; k++) put8(o, (uint8_t)(v >> (8 * k)));
}
static void put64(bytes *o, uint64_t v) {
  for (int k = 0; k < 8; k++) put8(o, (uint8_t)(v >> (8 * k)));
}
static void putnum(bytes *o, uint64_t v) {  // 7z's number, as 7-Zip writes it
  int n = 0;
  while (n < 8 && v >= 1ull << (7 * (n + 1))) n++;
  if (n == 8) {
    put8(o, 0xFF);
    put64(o, v);
    return;
  }
  put8(o, (uint8_t)(((0xFF00u >> n) & 0xFF) | (v >> (8 * n))));
  for (int k = 0; k < n; k++) put8(o, (uint8_t)(v >> (8 * k)));
}

// Two stored folders, "XXXX" with no file in it and then "hello", the one file (a.txt). `count`: the second folder's
// count of files (1 in a good archive), with a sizes property after the counts when `sizes`.
static uint8_t *empty_folder_archive(uint64_t count, int sizes, size_t *len) {
  static const char packed[] = "XXXXhello";
  bytes h = {{0}, 0}, out = {{0}, 0};
  put8(&h, 0x01);                                        // header
  put8(&h, 0x04);                                        // main streams
  put8(&h, 0x06), putnum(&h, 0), putnum(&h, 2);          // pack info: from 0, two streams
  put8(&h, 0x09), putnum(&h, 4), putnum(&h, 5), put8(&h, 0x00);
  put8(&h, 0x07), put8(&h, 0x0B), putnum(&h, 2), put8(&h, 0);  // two folders
  for (int i = 0; i < 2; i++) putnum(&h, 1), put8(&h, 0x01), put8(&h, 0x00);  // one coder: copy
  put8(&h, 0x0C), putnum(&h, 4), putnum(&h, 5), put8(&h, 0x00);
  put8(&h, 0x08), put8(&h, 0x0D), putnum(&h, 0), putnum(&h, count);  // substreams: 0 files, then count
  if (sizes) put8(&h, 0x09);  // a size for each file but a folder's last (none here, as count says)
  put8(&h, 0x0A), put8(&h, 1), put32(&h, (uint32_t)crc32(0, (const Bytef *)"hello", 5));
  put8(&h, 0x00), put8(&h, 0x00);
  put8(&h, 0x05), putnum(&h, 1);  // one file
  static const uint8_t name[] = {0, 'a', 0, '.', 0, 't', 0, 'x', 0, 't', 0, 0, 0};
  put8(&h, 0x11), putnum(&h, sizeof name), put(&h, name, sizeof name);
  put8(&h, 0x00), put8(&h, 0x00);
  bytes start = {{0}, 0};
  put64(&start, sizeof packed - 1), put64(&start, h.n), put32(&start, (uint32_t)crc32(0, h.b, (uInt)h.n));
  put(&out, "7z\xBC\xAF\x27\x1C\x00\x04", 8);
  put32(&out, (uint32_t)crc32(0, start.b, (uInt)start.n));
  put(&out, start.b, start.n);
  put(&out, packed, sizeof packed - 1);
  put(&out, h.b, h.n);
  uint8_t *a = malloc(out.n);
  memcpy(a, out.b, out.n);
  *len = out.n;
  return a;
}

static void test_empty_folder(void) {
  char why[128] = "";
  size_t n = 0, n1 = 0, n2 = 0;
  uint8_t *a = empty_folder_archive(1, 0, &n);
  rsm_7z *z = rsm_7z_open(a, n, why, sizeof why);
  CHECK(z != NULL);
  if (!z) {
    fprintf(stderr, "open: %s\n", why);
    free(a);
    return;
  }
  CHECK(rsm_7z_files(z) == 1 && file_is(z, "a.txt", (const uint8_t *)"hello", 5));
  // written again, unchanged and with the file changed: it is still the second folder's
  uint8_t *w1 = rsm_7z_rewrite(z, 0, NULL, NULL, NULL, &n1);
  rsm_7z *z1 = w1 ? rsm_7z_open(w1, n1, why, sizeof why) : NULL;
  CHECK(z1 && file_is(z1, "a.txt", (const uint8_t *)"hello", 5));
  const uint8_t *data[] = {(const uint8_t *)"changed"};
  size_t lens[] = {7};
  uint8_t *w2 = rsm_7z_rewrite(z, 1, (int[]){0}, data, lens, &n2);
  rsm_7z *z2 = w2 ? rsm_7z_open(w2, n2, why, sizeof why) : NULL;
  CHECK(z2 && file_is(z2, "a.txt", (const uint8_t *)"changed", 7));
  rsm_7z_close(z2);
  rsm_7z_close(z1);
  free(w2);
  free(w1);
  rsm_7z_close(z);
  free(a);
  // a folder of several files (solid), or of a count no archive has: refused at once, the sizes never read
  a = empty_folder_archive(2, 0, &n);
  CHECK(rsm_7z_open(a, n, why, sizeof why) == NULL && strstr(why, "solid"));
  free(a);
  a = empty_folder_archive(1ull << 62, 1, &n);
  CHECK(rsm_7z_open(a, n, why, sizeof why) == NULL && strstr(why, "solid"));
  free(a);
}

int main(void) {
  test_decode();
  test_encode();
  test_archive();
  test_empty_folder();
  return check_done("7z");
}
