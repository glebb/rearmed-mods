// The native format (seven_native.h) end to end on the two real RS Extended Range packages DotCharter exported: the
// PSXR archive opened, each ExtendedRange manifest's positional Tuning in the unit's form, each SNXR chart translated
// and read. The packages are not in the repository: they are looked for in $RSXR_FIXTURES (a folder), else
// ~/Downloads, and a missing one is skipped (the synthetic tests in test_seven_native.c, test_tuning.c and
// test_song_key.c cover the same paths for every instrument).
//   Nickelback_Burn-It-To-The-Ground_Dell_v2_3_rsxr_p.psarc: Lead on a 7-string guitar (B standard), Rhythm on a stock
//     6-string in drop B, Bass on a 5-string bass (B standard), vocals
//   Fear-Factory_Soul-Hacker_Dell_v0_2_rsxr_p.psarc: Lead on an 8-string guitar (F# standard), nothing else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "check.h"
#include "rsmods_tuning.h"
#include "seven_native.h"
#include "song_sng.h"

#include <zlib.h>

// The package as it is on disk, but with the magic PSAR: rsm_psarc_read then gives the manifests untranslated
static ssize_t raw_pread(int fd, void *buf, size_t n, off_t off) {
  ssize_t r = pread(fd, buf, n, off);
  if (r >= 4 && off == 0 && !memcmp(buf, "PSXR", 4)) memcpy(buf, "PSAR", 4);
  return r;
}
static int raw_open(const char *p) { return open(p, O_RDONLY); }
static const rsm_psarc_io k_raw = {raw_open, raw_pread, close};

typedef struct {
  const char *arrangement;  // ArrangementName
  int instrument, strings;  // the native header's (0 guitar, 1 bass); strings 0: a stock chart (no SNXR)
  int tuning[6], extra;     // what the unit's form gives rsm_manifest_arrangement
  int lowest;               // the lowest MIDI note of the whole chart
  int on_extra;             // a note on an added string (guitar strings 7/8: MIDI below 40; bass: below 28)
} expect;

typedef struct {
  const char *file, *song_key;
  int arrangements;
  expect e[3];
} package;

static const package k_packages[] = {
    {"Nickelback_Burn-It-To-The-Ground_Dell_v2_3_rsxr_p.psarc",
     "dellnickelbackburnittotheground7stringv23",
     3,
     {{"Lead", 0, 7, {0, 0, 0, 0, 0, 0}, 1, 35, 1},
      {"Rhythm", 0, 0, {-5, -3, -3, -3, -3, -3}, 0, 35, 0},
      {"Bass", 1, 5, {0, 0, 0, 0, 0, 0}, 1, 23, 1}}},
    {"Fear-Factory_Soul-Hacker_Dell_v0_2_rsxr_p.psarc",
     "dellfearfactorysoulhacker8stringt2",
     1,
     {{"Lead", 0, 8, {0, 0, 0, 0, 0, 0}, 2, 30, 1}}},
};

static const expect *expect_of(const package *p, const char *name) {
  for (int i = 0; i < p->arrangements; i++)
    if (!strcmp(p->e[i].arrangement, name)) return &p->e[i];
  return NULL;
}

static int ends_with(const char *s, const char *tail) {
  size_t a = strlen(s), b = strlen(tail);
  return a >= b && !strcmp(s + a - b, tail);
}

// The positional Tuning of a native manifest: how many stringN keys it has
static int native_entries(const char *json) {
  const char *t = strstr(json, "\"Tuning\"");
  const char *end = t ? strchr(t, '}') : NULL;
  int n = 0;
  for (int i = 0; t && end && i < 10; i++) {
    char key[16];
    snprintf(key, sizeof key, "\"string%d\"", i);
    const char *k = strstr(t, key);
    if (k && k < end) n++;
  }
  return n;
}

// A manifest: the raw one native (ExtendedRange, N positional entries), the unit's form what the expectation says
static void check_manifest(const package *p, rsm_psarc *a, rsm_psarc *raw, int i, int *seen) {
  char *t = rsm_psarc_read(a, i, NULL), *r = rsm_psarc_read(raw, i, NULL);
  CHECK(t && r);
  rsm_arrangement ar;
  if (t && r && rsm_manifest_arrangement(t, &ar)) {
    const expect *e = expect_of(p, ar.name);
    CHECK(e != NULL);
    if (e) {
      seen[e - p->e] = 1;
      const int native = e->strings != 0;
      CHECK(!!strstr(r, "ExtendedRange") == native);  // DotCharter marks only the extended arrangements
      CHECK(!strstr(t, "ExtendedRange"));
      if (native) {
        CHECK(native_entries(r) == e->strings);
        // the reader's form is the game's: the same rewrite as the JSON point's
        char *out;
        size_t n;
        int rewritten = 0, bad = 0;
        CHECK(seven_native_manifest(r, strlen(r), &out, &n, &rewritten, &bad) && rewritten == 1 && bad == 0);
        if (rewritten) {
          CHECK(n == strlen(t) && !memcmp(out, t, n));
          free(out);
        }
        CHECK(!seven_native_manifest(t, strlen(t), &out, &n, NULL, NULL));  // nothing left to translate
      } else {
        CHECK(!strcmp(r, t));
      }
      CHECK(!memcmp(ar.tuning, e->tuning, sizeof ar.tuning) && ar.extra == e->extra);
      CHECK(rsm_is_bass(&ar) == (e->instrument == 1));
      CHECK(!!strstr(t, "Rs7String4") == (e->instrument == 1 && e->strings >= 5));
      CHECK(!!strstr(t, "Rs7String5") == (e->instrument == 1 && e->strings == 6));
      CHECK(!!strstr(t, "Rs7String6") == (e->instrument == 0 && e->strings >= 7));
      CHECK(!!strstr(t, "Rs7String7") == (e->instrument == 0 && e->strings == 8));
      // RSMods' Extended Range rule at B (-5): a standard-tuned extended instrument is not low (its added strings are
      // its own colours); the drop B 6-string is when drop tunings count from B itself (else a drop needs A, -7)
      CHECK(rsm_extended_range_rule(&ar, -5, 1, 1) == (e->tuning[0] <= -5));
      CHECK(!rsm_extended_range_rule(&ar, -5, 0, 1));
    }
  }
  free(t), free(r);
}

// The STRS of a translated chart's RS7X block: instrument, strings, columns; 0 when there is no block
static int block_of(const uint8_t *p, size_t n, int *instrument, int *strings, int *columns) {
  for (size_t i = 0; i + 32 <= n; i++)
    if (!memcmp(p + i, "RS7X", 4) && !memcmp(p + i + 16, "STRS", 4)) {
      *instrument = p[i + 28], *strings = p[i + 29], *columns = p[i + 30];
      return 1;
    }
  return 0;
}

// An SNG: plain (flags 1), native or not as expected, translated to a block of the right shape
static void check_sng(const package *p, rsm_psarc *a, int i) {
  const char *name = rsm_psarc_name(a, i);
  const expect *e = NULL;
  for (int k = 0; k < p->arrangements; k++) {
    char tail[32];
    snprintf(tail, sizeof tail, "_%s.sng", p->e[k].arrangement);
    for (char *c = tail; *c; c++)
      if (*c >= 'A' && *c <= 'Z') *c += 32;
    if (ends_with(name, tail)) e = &p->e[k];
  }
  if (!e) return;  // vocals
  size_t n = 0;
  uint8_t *f = rsm_psarc_read(a, i, &n);
  CHECK(f && n > 28 && f[0] == 0x4a && f[4] == 1);  // a plain SNG
  if (!f || n <= 28) return free(f);
  // the body as it is (flags 1: a u32 length, then zlib): native with the header's instrument and count, or stock
  uint32_t len;
  memcpy(&len, f + 24, 4);
  uLongf body_n = len;
  uint8_t *body = len && len < (64u << 20) ? malloc(len) : NULL;
  CHECK(body && uncompress(body, &body_n, f + 28, (uLong)(n - 28)) == Z_OK && body_n == len);
  if (body) {
    CHECK(seven_native_is_sng(body, body_n) == (e->strings != 0));
    if (e->strings) CHECK(body[4] == 1 && body[5] == e->instrument && body[6] == e->strings && body[7] == 0);
  }
  // translated (sng_decrypt does it for a plain SNG): the unit's form, a block for the added strings only
  size_t plain_n = 0;
  uint8_t *plain = sng_decrypt(f, n, 0, &plain_n);
  CHECK(plain && plain_n == len && !seven_native_is_sng(plain, plain_n));
  int instrument = -1, strings = 0, columns = -1;
  const int has_block = plain && block_of(plain, plain_n, &instrument, &strings, &columns);
  if (e->instrument ? e->strings > 4 : e->strings > 6) {
    CHECK(has_block && instrument == e->instrument && strings == e->strings);
    CHECK(columns == (e->instrument ? 0 : e->strings - 6));
  } else {
    CHECK(!has_block);
  }
  if (body && plain && !e->strings) CHECK(!memcmp(body, plain, plain_n));  // a stock chart is left as it is
  // with extended bass off (an RS Re-ARMed that can't build one) the 5-string chart is refused, a guitar's still read
  if (body && e->strings) {
    uint8_t *out = malloc(body_n);
    seven_native_allow_bass(0);
    const seven_native_status st = seven_native_sng(body, body_n, out, NULL, 0);
    CHECK(e->instrument ? st == SEVEN_NATIVE_UNSUPPORTED : st == SEVEN_NATIVE_OK);
    seven_native_allow_bass(1);
    CHECK(seven_native_sng(body, body_n, out, NULL, 0) == SEVEN_NATIVE_OK && plain && !memcmp(out, plain, plain_n));
    free(out);
  }
  free(body);
  free(plain), free(f);
}

// The whole song as Nashville Numbers reads it: each arrangement's notes, the added strings' among them
static void check_song(const package *p, const char *path) {
  const uint8_t key[32] = {0};  // a plain package: no key used
  sng_arrangement *arr = NULL;
  const int k = sng_read_song(path, p->song_key, key, NULL, &arr);
  CHECK(k == p->arrangements);
  for (int i = 0; i < k; i++) {
    const expect *e = expect_of(p, arr[i].name);
    CHECK(e != NULL);
    if (!e) continue;
    CHECK(arr[i].bass == (e->instrument == 1) && arr[i].n > 100 && arr[i].manifest.extra == e->extra);
    int lowest = 128, below = 0;
    for (int j = 0; j < arr[i].n; j++)
      for (int m = 0; m < arr[i].ev[j].n; m++) {
        const int v = arr[i].ev[j].midi[m];
        if (v < lowest) lowest = v;
        below += v < (e->instrument ? 28 : 40);
      }
    CHECK(lowest == e->lowest);
    CHECK(e->on_extra ? below > 0 : 1);
  }
  sng_free_all(arr, k);
}

int main(void) {
  const char *dir = getenv("RSXR_FIXTURES");
  char home[1024];
  if (!dir) {
    snprintf(home, sizeof home, "%s/Downloads", getenv("HOME") ? getenv("HOME") : ".");
    dir = home;
  }
  int ran = 0;
  for (size_t pk = 0; pk < sizeof k_packages / sizeof *k_packages; pk++) {
    const package *p = &k_packages[pk];
    char path[1400];
    snprintf(path, sizeof path, "%s/%s", dir, p->file);
    if (access(path, R_OK)) {
      printf("test_seven_fixtures: %s not found in %s: skipped\n", p->file, dir);
      continue;
    }
    ran++;
    const uint8_t key[32] = {0};
    rsm_psarc *a = rsm_psarc_open(path, key), *raw = rsm_psarc_open_io(path, key, &k_raw);
    CHECK(a && raw && rsm_psarc_is_native(a) && !rsm_psarc_is_native(raw));
    if (!a || !raw) {
      rsm_psarc_close(a), rsm_psarc_close(raw);
      continue;
    }
    int seen[3] = {0}, sngs = 0;
    for (int i = 0; i < rsm_psarc_files(a); i++) {
      const char *name = rsm_psarc_name(a, i);
      if (ends_with(name, ".json")) check_manifest(p, a, raw, i, seen);
      if (ends_with(name, ".sng")) check_sng(p, a, i), sngs++;
      if (ends_with(name, ".hsan")) {  // the header file: every arrangement in the unit's form
        char *t = rsm_psarc_read(a, i, NULL);
        CHECK(t && !strstr(t, "ExtendedRange"));
        free(t);
      }
    }
    for (int i = 0; i < p->arrangements; i++) CHECK(seen[i]);
    CHECK(sngs >= p->arrangements);
    rsm_psarc_close(a), rsm_psarc_close(raw);
    check_song(p, path);
  }
  if (!ran) printf("test_seven_fixtures: no package found: nothing checked\n");
  return check_done("test_seven_fixtures");
}
