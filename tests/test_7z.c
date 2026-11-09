// rsmods_7z.c: LZMA and LZMA2 made by liblzma, a 7z archive laid out as the game's are, and the archive written again
// with a file changed (tests/vectors_7z.h, from make_7z_vectors.py).
#include <stdlib.h>
#include <string.h>

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

int main(void) {
  test_decode();
  test_encode();
  test_archive();
  return check_done("7z");
}
