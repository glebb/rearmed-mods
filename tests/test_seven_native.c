// Seven-String Guitar's native format reader (src/seven_native.c): the chart converter, the manifest rewriter and the
// three callbacks, with the game's runtime stubbed. The vectors are those of rs7string's tests/model/test_nativesng.cpp
// and test_nativemanifest.cpp (github.com/SomeeName/rs7string, MIT, by SomeeName), with the expectations of this unit's
// form: the RS7X block, Rs7String6 / Rs7String7, and the extended bass (5 and 6 strings: the six stock slots, an RS7X
// block with STRS only, Rs7String4 / Rs7String5; the vectors of upstream's bass tests, as this unit reads them). Built twice: the Mac's pointers (8 bytes) and, with
// -DGUEST32, Learn & Play's (4-byte pointers in a guest window).
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "seven_native_core.c"
#include "seven_native.c"

// --- the game's side -----------------------------------------------------------------------------------------------
#ifdef GUEST32
const int rc_guest_ptr_size = 4;
#else
const int rc_guest_ptr_size = 8;
#endif
uint64_t rc_slide;
static _Alignas(16) uint8_t window[1 << 22];
static size_t window_used = 0x100;
int seven_on = 1;
static int g_bass_feature = 1;  // SEVEN_F_BASS
int seven_feature(enum seven_feature f) { return f == SEVEN_F_BASS ? g_bass_feature : 0; }

void rsm_log(const char *fmt, ...) { (void)fmt; }
int plain_packages_start(const char *by) {
  (void)by;
  return 0;
}
void *rc_guest_alloc(size_t n) {
  void *p = window + window_used;
  window_used += (n + 15) & ~(size_t)15;
  return window_used < sizeof window ? p : NULL;
}
static int g_taken;
int seven_mid_optional(const char *name, rc_mid_fn fn, const char *what) {
  (void)name, (void)fn, (void)what;
  return g_taken++ < 3;
}

// The mid frame's values by name: magic 0, stored 1, cursor 2, body 3, size 4; one continuation, "fail" = 1
static uint64_t vals[8];
int rc_mid_value(int point, const char *name) {
  static const char *const names[] = {"magic", "stored", "cursor", "body", "size"};
  (void)point;
  for (int i = 0; i < 5; i++)
    if (!strcmp(name, names[i])) return i;
  return -1;
}
int rc_mid_cont(int point, const char *name) {
  (void)point, (void)name;
  return 1;
}
uint64_t rc_mid_get(const rc_mid_frame *f, int v) {
  (void)f;
  return vals[v];
}
void rc_mid_set(rc_mid_frame *f, int v, uint64_t x) {
  (void)f;
  vals[v] = x;
}

// --- byte buffers --------------------------------------------------------------------------------------------------
typedef struct {
  uint8_t *p;
  size_t n, cap;
} bytes;

static void b_add(bytes *b, const void *src, size_t k) {
  if (b->n + k > b->cap) {
    b->cap = (b->n + k) * 2 + 64;
    b->p = realloc(b->p, b->cap);
  }
  if (k) memcpy(b->p + b->n, src, k);
  b->n += k;
}
static void b_u8(bytes *b, unsigned v) {
  uint8_t x = (uint8_t)v;
  b_add(b, &x, 1);
}
static void b_u16(bytes *b, unsigned v) {
  uint16_t x = (uint16_t)v;
  b_add(b, &x, 2);
}
static void b_u32(bytes *b, uint32_t v) { b_add(b, &v, 4); }
static void b_f32(bytes *b, float v) { b_add(b, &v, 4); }
static void b_zero(bytes *b, size_t k) {
  for (size_t i = 0; i < k; i++) b_u8(b, 0);
}
static void b_free(bytes *b) {
  free(b->p);
  memset(b, 0, sizeof *b);
}
static uint32_t rd32(const uint8_t *p) {
  uint32_t v;
  memcpy(&v, p, 4);
  return v;
}
static unsigned rd16(const uint8_t *p) {
  uint16_t v;
  memcpy(&v, p, 2);
  return v;
}

// --- a synthetic song, native and stock (upstream's `Song`) ----------------------------------------------------------
// Hand-specified low-to-high game slots of each instrument; the converter's own helpers are never used
static const int k_guitar_slots[] = {7, 6, 0, 1, 2, 3, 4, 5};

typedef struct {
  bytes native, stock;
  size_t counts[128], ncounts, bend_counts[32], nbends;
  int instrument, n;
  size_t tuning, end;
} song;

// how many chord templates and ChordNotes records a song has (tests change them)
static int g_templates = 2, g_chordnotes = 2;

// a bass keeps its strings in the stock six slots: a 5- or 6-string's low B is slot 4, E A D G slots 0-3, a high C slot 5
static int slot_of(const song *s, int i) {
  if (s->instrument) return s->n == 4 ? i : i == 0 ? 4 : i <= 4 ? i - 1 : 5;
  return k_guitar_slots[i + 8 - s->n];
}
static int t_native_of(const song *s, int slot) {
  for (int i = 0; i < s->n; i++)
    if (slot_of(s, i) == slot) return i;
  return -1;
}

static void raw(song *s, size_t k, unsigned seed) {
  for (size_t i = 0; i < k; i++) {
    const uint8_t v = (uint8_t)((i + seed) % 251);
    b_u8(&s->native, v);
    b_u8(&s->stock, v);
  }
}
static void cnt(song *s, uint32_t k) {
  s->counts[s->ncounts++] = s->native.n;
  b_u32(&s->native, k);
  b_u32(&s->stock, k);
}
static void t_fixed(song *s, uint32_t k, size_t stride, unsigned seed) {
  cnt(s, k);
  raw(s, k * stride, seed);
}

static void template_row(bytes *b, int row, int columns, const song *s) {
  b_u32(b, 0x12340000u + (uint32_t)row);
  for (int field = 0; field < 3; field++)
    for (int col = 0; col < columns; col++) {
      const int i = s ? t_native_of(s, col) : col;
      if (field == 2)
        b_u32(b, (uint32_t)(i < 0 ? -1 : 100 + row * 20 + i));
      else
        b_u8(b, (unsigned)(i < 0 ? -1 : field ? (row + i) % 5 : 1 + row + i));
    }
  for (int i = 0; i < 32; i++) b_u8(b, (unsigned)(i + row));
}

static void technique_row(song *s, bytes *b, int row, int columns, int mapped) {
  for (int field = 0; field < 5; field++)
    for (int col = 0; col < columns; col++) {
      const int i = mapped ? t_native_of(s, col) : col;
      switch (field) {
        case 0:
          b_u32(b, i < 0 ? 0 : (uint32_t)(0x2000 + row * 0x100 + i));
          break;
        case 1:
          for (int bend = 0; bend < 32; bend++) {
            b_f32(b, i < 0 ? 0.0f : (float)row * 10.0f + (float)i + (float)bend * 0.125f);
            b_f32(b, i < 0 ? 0.0f : 0.5f + (float)bend);
            b_u32(b, i < 0 ? 0 : 0xA000u + (uint32_t)bend);
          }
          if (!mapped) s->bend_counts[s->nbends++] = b->n;
          b_u32(b, i < 0 ? 0 : row ? 32 : 2);
          break;
        case 2:
        case 3:
          b_u8(b, (unsigned)(i < 0 ? -1 : field * 2 - 2 + row + i));
          break;
        case 4:
          b_u16(b, (unsigned)(i < 0 ? 0 : 20 + row + i));
          break;
      }
    }
}

static void sections_of(song *s) {
  cnt(s, 2);
  for (int row = 0; row < 2; row++) {
    raw(s, 52, 11 + (unsigned)row);
    for (int i = 0; i < 36; i++) {
      b_u8(&s->native, (1u << (i % s->n)) | (1u << ((i + 1) % s->n)));
      b_u8(&s->stock, (1u << slot_of(s, i % s->n)) | (1u << slot_of(s, (i + 1) % s->n)));
    }
  }
}

static void levels_of(song *s) {
  cnt(s, 2);
  for (int level = 0; level < 2; level++) {
    raw(s, 4, (unsigned)level);
    t_fixed(s, 2, 28, 17);
    t_fixed(s, 1, 12, 18);
    t_fixed(s, 1, 20, 19);
    t_fixed(s, 2, 20, 20);
    cnt(s, (uint32_t)s->n + 1);
    for (int i = 0; i <= s->n; i++) {
      const size_t a = s->native.n, b = s->stock.n;
      raw(s, 63, 21 + (unsigned)i);
      s->native.p[a + 16] = (uint8_t)(i == s->n ? 255 : i);
      s->stock.p[b + 16] = (uint8_t)(i == s->n ? 255 : slot_of(s, i));
      t_fixed(s, (uint32_t)(i % 3), 12, 22);
    }
    t_fixed(s, 2, 4, 23);
    t_fixed(s, 3, 4, 24);
    t_fixed(s, 1, 4, 25);
  }
}

static void make_song(song *s, int instrument, int strings, int vocals, size_t padding) {
  memset(s, 0, sizeof *s);
  s->instrument = instrument;
  s->n = strings;
  const uint8_t head[8] = {'S', 'N', 'X', 'R', 1, (uint8_t)instrument, (uint8_t)strings, 0};
  b_add(&s->native, head, 8);
  t_fixed(s, 2, 16, 1);
  t_fixed(s, 1, 44, 2);
  cnt(s, (uint32_t)g_templates);
  for (int row = 0; row < g_templates; row++) {
    template_row(&s->native, row, strings, NULL);
    template_row(&s->stock, row, 6, s);
  }
  cnt(s, (uint32_t)g_chordnotes);
  for (int row = 0; row < g_chordnotes; row++) {
    technique_row(s, &s->native, row, strings, 0);
    technique_row(s, &s->stock, row, 6, 1);
  }
  t_fixed(s, vocals ? 1 : 0, 60, 3);
  if (vocals) {
    t_fixed(s, 2, 32, 4);
    t_fixed(s, 1, 144, 5);
    t_fixed(s, 2, 44, 6);
  }
  t_fixed(s, 2, 24, 7);
  t_fixed(s, 1, 16, 8);
  cnt(s, 2);
  raw(s, 4, 9);
  t_fixed(s, 3, 4, 10);
  raw(s, 4, 11);
  t_fixed(s, 1, 4, 12);
  t_fixed(s, 1, 260, 13);
  t_fixed(s, 2, 260, 14);
  t_fixed(s, 2, 8, 15);
  t_fixed(s, 1, 8, 16);
  sections_of(s);
  levels_of(s);
  raw(s, 79, 26);
  s->tuning = s->native.n;
  s->counts[s->ncounts++] = s->tuning;
  b_u32(&s->native, (uint32_t)strings);
  b_u32(&s->stock, instrument ? 6 : (uint32_t)strings);
  for (int i = 0; i < strings; i++) b_u16(&s->native, (unsigned)(-7 - i));
  for (int slot = 0; slot < (instrument ? 6 : strings); slot++) {
    const int i = t_native_of(s, slot);
    b_u16(&s->stock, (unsigned)(i < 0 ? 0 : -7 - i));
  }
  raw(s, 12, 27);
  s->end = s->native.n;
  b_zero(&s->native, padding);
}

static void song_free(song *s) {
  b_free(&s->native);
  b_free(&s->stock);
}

static int all_zero(const uint8_t *p, size_t k) {
  for (size_t i = 0; i < k; i++)
    if (p[i]) return 0;
  return 1;
}

// The RS7X block behind the stock body: the rules of docs/release/RS7X-SPEC.md, and the cells of the synthetic song
static void check_block(const song *s, const uint8_t *out, size_t size) {
  const size_t tail = s->stock.n, cols = s->instrument ? 0 : (size_t)s->n - 6;
  if (s->instrument ? s->n == 4 : s->n <= 6) {
    CHECK(all_zero(out + tail, size - tail));  // no block for a six-string guitar or a four-string bass
    return;
  }
  const uint8_t *b = out + tail;
  const size_t n = size - tail;
  CHECK(!memcmp(b, "RS7X", 4) && rd16(b + 4) == 1 && rd16(b + 6) == 16);
  CHECK(rd32(b + 8) == n);  // the block's size: everything after MetaData
  CHECK(rd32(b + 12) == (s->instrument ? 2u : 4u));  // an extended bass: STRS and PAD0
  size_t at = 16;
  // STRS: critical, instrument 0, the string count, its columns, 6, 7, ...
  CHECK(!memcmp(b + at, "STRS", 4) && rd16(b + at + 4) == 1 && rd16(b + at + 6) == 1);
  CHECK(rd32(b + at + 8) == 4 + cols);
  CHECK(b[at + 12] == s->instrument && b[at + 13] == s->n && b[at + 14] == cols && b[at + 15] == 0);
  for (size_t c = 0; c < cols; c++) CHECK(b[at + 16 + c] == 6 + c);
  at += 12 + rd32(b + at + 8);
  if (s->instrument) {  // a bass: no CHDT, no CHNT; PAD0 is the rest
    CHECK(!memcmp(b + at, "PAD0", 4) && rd16(b + at + 4) == 1 && rd16(b + at + 6) == 0);
    CHECK(at + 12 + rd32(b + at + 8) == n);
    CHECK(all_zero(b + at + 12, n - at - 12));
    return;
  }
  // CHDT: critical, one { fret, finger } a column of each template
  CHECK(!memcmp(b + at, "CHDT", 4) && rd16(b + at + 4) == 1 && rd16(b + at + 6) == 1);
  CHECK(rd32(b + at + 8) == 4 + 2 * (size_t)g_templates * cols && rd32(b + at + 12) == (uint32_t)g_templates);
  for (int row = 0; row < g_templates; row++)
    for (size_t c = 0; c < cols; c++) {
      const int i = t_native_of(s, 6 + (int)c);
      const uint8_t *cell = b + at + 16 + ((size_t)row * cols + c) * 2;
      CHECK(cell[0] == 1 + row + i && cell[1] == (row + i) % 5);
    }
  at += 12 + rd32(b + at + 8);
  // CHNT: not critical; per column mask, slideTo, slideUnpitchTo, vibrato, the used bends
  CHECK(!memcmp(b + at, "CHNT", 4) && rd16(b + at + 4) == 1 && rd16(b + at + 6) == 0);
  CHECK(rd32(b + at + 12) == (uint32_t)g_chordnotes);
  const uint8_t *p = b + at + 16;
  for (int row = 0; row < g_chordnotes; row++)
    for (size_t c = 0; c < cols; c++) {
      const int i = t_native_of(s, 6 + (int)c);
      const unsigned used = row ? 32u : 2u;
      CHECK(rd32(p) == (uint32_t)(0x2000 + row * 0x100 + i));
      CHECK((int8_t)p[4] == 2 + row + i && (int8_t)p[5] == 4 + row + i);
      CHECK(rd16(p + 6) == (unsigned)(20 + row + i) && p[8] == used);
      for (unsigned bend = 0; bend < used; bend++) {
        float time, step;
        memcpy(&time, p + 9 + 12 * bend, 4);
        memcpy(&step, p + 13 + 12 * bend, 4);
        CHECK(time == (float)row * 10.0f + (float)i + (float)bend * 0.125f);
        CHECK(step == 0.5f + (float)bend);
        CHECK(rd32(p + 17 + 12 * bend) == 0xA000u + bend);
      }
      p += 9 + 12 * used;
    }
  at = (size_t)(p - b);
  // PAD0: not critical, all that is left, zero
  CHECK(!memcmp(b + at, "PAD0", 4) && rd16(b + at + 6) == 0);
  CHECK(at + 12 + rd32(b + at + 8) == n);
  CHECK(all_zero(b + at + 12, n - at - 12));
}

static seven_native_status convert(const bytes *in, uint8_t *out, char *why, size_t cap) {
  return seven_native_sng(in->p, in->n, out, why, cap);
}

static void check_song(int instrument, int n, int vocals) {
  song s;
  make_song(&s, instrument, n, vocals, 4096);
  uint8_t *out = malloc(s.native.n);
  char why[180] = "old reason";
  const seven_native_status st = convert(&s.native, out, why, sizeof why);
  CHECK(st == SEVEN_NATIVE_OK);
  if (st == SEVEN_NATIVE_OK) {
    CHECK(why[0] == 0);
    // every template and technique column, the unused stock columns, each note in both levels, 36 masks a section,
    // the tuning, and every unchanged byte of the stock sections
    CHECK(!memcmp(out, s.stock.p, s.stock.n));
    check_block(&s, out, s.native.n);
  }
  free(out);
  song_free(&s);
}

static void test_guitar_columns_and_tail(void) {
  check_song(0, 6, 1);
  check_song(0, 7, 1);
  check_song(0, 8, 1);
  check_song(0, 7, 0);
  check_song(0, 8, 0);
  check_song(1, 4, 1);  // a four-string bass in a native header is a stock bass
  check_song(1, 4, 0);
}

// The extended bass: every template and technique column of the six stock slots (the low B slot 4, a high C slot 5, the
// unused one of a five-string empty: fret, finger, note -1, mask 0), the notes' strings, the section masks and the six
// tuning entries, then a block with STRS only
static void test_extended_bass_columns_and_tail(void) {
  for (int n = 5; n <= 6; n++)
    for (int vocals = 0; vocals < 2; vocals++) check_song(1, n, vocals);
  // upstream's example (its block of 32 bytes: the header and STRS), here with PAD0 after it
  song s;
  make_song(&s, 1, 5, 0, 0);
  bytes more = {0};
  b_add(&more, s.native.p, s.native.n);
  b_zero(&more, 44 + 400 + 6 * 2 + 396 * 2 + 2);
  uint8_t *out = malloc(more.n);
  char why[180];
  CHECK(convert(&more, out, why, sizeof why) == SEVEN_NATIVE_OK);
  const uint8_t *b = out + s.stock.n;
  CHECK(!memcmp(b, "RS7X\1\0\x10\0", 8) && rd32(b + 8) == more.n - s.stock.n && rd32(b + 12) == 2);
  CHECK(!memcmp(b + 16, "STRS\1\0\1\0\4\0\0\0\1\5\0\0", 16));
  CHECK(!memcmp(b + 32, "PAD0", 4));
  b_free(&more);
  free(out);
  song_free(&s);
}

// While the unit cannot build an extended bass it is refused, as before wave 2
static void test_extended_bass_is_refused_while_off(void) {
  seven_native_allow_bass(0);
  for (int n = 5; n <= 6; n++) {
    song s;
    make_song(&s, 1, n, 1, 4096);
    uint8_t *out = malloc(s.native.n);
    char why[180];
    CHECK(convert(&s.native, out, why, sizeof why) == SEVEN_NATIVE_UNSUPPORTED);
    CHECK(strstr(why, "bass") && strstr(why, "not supported"));
    free(out);
    song_free(&s);
  }
  song s;  // a four-string bass and the guitars are not affected
  make_song(&s, 1, 4, 1, 4096);
  uint8_t *out = malloc(s.native.n);
  CHECK(convert(&s.native, out, NULL, 0) == SEVEN_NATIVE_OK);
  free(out);
  song_free(&s);
  seven_native_allow_bass(1);
}

// A bass of 4, 5 and 6 strings needs padding for the six stock slots and the block (the format's rule: 256 + (6 - N) *
// (6 templates + 396 ChordNotes + 2), N < 6; 256 for six): the exact missing amount converts, one less does not
static void test_extended_bass_padding(void) {
  const int saved_t = g_templates, saved_c = g_chordnotes;
  for (int n = 5; n <= 6; n++)
    for (int bare = 0; bare < 2; bare++) {
      g_templates = bare ? 0 : 2;
      g_chordnotes = bare ? 0 : 2;
      song s;
      make_song(&s, 1, n, 0, 0);
      char why[180];
      uint8_t *out = malloc(s.native.n + 8192);
      unsigned long long missing = 0;
      CHECK(convert(&s.native, out, why, sizeof why) == SEVEN_NATIVE_TOO_SMALL);
      CHECK(sscanf(why, "missing %llu bytes", &missing) == 1 && missing > 0);
      const unsigned long long rule = 256 + (unsigned long long)(6 - n) * (6u * (unsigned)g_templates + 396u * (unsigned)g_chordnotes + 2);
      CHECK(missing <= rule);
      b_zero(&s.native, (size_t)missing - 1);
      CHECK(convert(&s.native, out, why, sizeof why) == SEVEN_NATIVE_TOO_SMALL);
      CHECK(strstr(why, "missing 1 bytes"));
      b_zero(&s.native, 1);
      CHECK(convert(&s.native, out, why, sizeof why) == SEVEN_NATIVE_OK);
      CHECK(!memcmp(out, s.stock.p, s.stock.n));
      check_block(&s, out, s.native.n);
      free(out);
      song_free(&s);
    }
  g_templates = saved_t;
  g_chordnotes = saved_c;
}

// Bad data in a bass chart is as bad as in a guitar's: a section mask bit past its strings, a note on a string past them
static void test_extended_bass_bad_data(void) {
  for (int n = 5; n <= 6; n++) {
    song s;
    make_song(&s, 1, n, 0, 4096);
    uint8_t *out = malloc(s.native.n);
    char why[180];
    // the header: a bass of 7, 3 strings
    for (int bad = 3; bad <= 7; bad += 4) {
      bytes b = {0};
      b_add(&b, s.native.p, s.native.n);
      b.p[6] = (uint8_t)bad;
      CHECK(convert(&b, out, why, sizeof why) == SEVEN_NATIVE_BAD);
      b_free(&b);
    }
    // the tuning's count against the header's
    bytes b = {0};
    b_add(&b, s.native.p, s.native.n);
    uint32_t other = (uint32_t)(n == 5 ? 6 : 5);
    memcpy(b.p + s.tuning, &other, 4);
    CHECK(convert(&b, out, why, sizeof why) == SEVEN_NATIVE_BAD);
    b_free(&b);
    free(out);
    song_free(&s);
  }
}

static void test_detection_and_bad_headers(void) {
  song s;
  make_song(&s, 0, 7, 1, 4096);
  CHECK(seven_native_is_sng(s.native.p, s.native.n));
  CHECK(!seven_native_is_sng(NULL, 4));
  for (size_t k = 0; k < 4; k++) CHECK(!seven_native_is_sng(s.native.p, k));
  const size_t offsets[] = {4, 5, 6, 7};
  const uint8_t values[] = {2, 2, 17, 1};
  uint8_t *out = malloc(s.native.n);
  char why[180];
  for (size_t i = 0; i < 4; i++) {
    bytes b = {0};
    b_add(&b, s.native.p, s.native.n);
    b.p[offsets[i]] = values[i];
    CHECK(convert(&b, out, why, sizeof why) == SEVEN_NATIVE_BAD);
    CHECK(strstr(why, "byte") && why[0]);
    b_free(&b);
  }
  s.native.p[0] = 0;
  CHECK(convert(&s.native, out, why, sizeof why) == SEVEN_NATIVE_NOT_NATIVE);
  CHECK(seven_native_sng(s.native.p, s.native.n, s.native.p, why, sizeof why) == SEVEN_NATIVE_NOT_NATIVE);
  free(out);
  song_free(&s);
}

static void test_counts_and_padding_rules(void) {
  song s;
  make_song(&s, 0, 8, 1, 4096);
  uint8_t *out = malloc(s.native.n);
  char why[180];
  for (size_t i = 0; i < s.ncounts; i++) {
    bytes b = {0};
    b_add(&b, s.native.p, s.native.n);
    uint32_t big = UINT32_MAX;
    memcpy(b.p + s.counts[i], &big, 4);
    CHECK(convert(&b, out, why, sizeof why) == SEVEN_NATIVE_BAD);
    CHECK(why[0]);
    b_free(&b);
  }
  for (size_t i = 0; i < s.nbends; i++) {
    bytes b = {0};
    b_add(&b, s.native.p, s.native.n);
    uint32_t big = 33;
    memcpy(b.p + s.bend_counts[i], &big, 4);
    CHECK(convert(&b, out, why, sizeof why) == SEVEN_NATIVE_BAD);
    b_free(&b);
  }
  bytes b = {0};
  b_add(&b, s.native.p, s.native.n);
  b.p[b.n - 1] = 1;  // a padding byte that is not zero
  CHECK(convert(&b, out, why, sizeof why) == SEVEN_NATIVE_BAD);
  uint32_t seven = 7;  // the tuning's count against the header's
  memcpy(b.p + s.tuning, &seven, 4);
  CHECK(convert(&b, out, why, sizeof why) == SEVEN_NATIVE_BAD);
  b_free(&b);
  free(out);
  song_free(&s);
}

// The records of a seven- or eight-string chart are wider than the stock ones, so its block mostly fits without padding
// (the format asks for 256 bytes all the same); a chart with no chord data does not: one byte less than the count the
// refusal gives is refused again, and the exact amount converts with an empty PAD0
static void test_padding_missing_bytes_and_exact_fit(void) {
  const int saved_t = g_templates, saved_c = g_chordnotes;
  for (int n = 6; n <= 8; n++) {
    for (int bare = 0; bare < 2; bare++) {
      g_templates = bare ? 0 : 2;
      g_chordnotes = bare ? 0 : 2;
      song s;
      make_song(&s, 0, n, 0, 0);
      char why[180];
      uint8_t *out = malloc(s.native.n + 8192);
      unsigned long long missing = 0;
      const seven_native_status st = convert(&s.native, out, why, sizeof why);
      if (n == 6 || !bare) {
        CHECK(st == SEVEN_NATIVE_OK);
        check_block(&s, out, s.native.n);
      } else {
        CHECK(st == SEVEN_NATIVE_TOO_SMALL);
        CHECK(sscanf(why, "missing %llu bytes", &missing) == 1 && missing > 0);
        b_zero(&s.native, (size_t)missing - 1);
        CHECK(convert(&s.native, out, why, sizeof why) == SEVEN_NATIVE_TOO_SMALL);
        CHECK(strstr(why, "missing 1 bytes"));
        b_zero(&s.native, 1);
        CHECK(convert(&s.native, out, why, sizeof why) == SEVEN_NATIVE_OK);
        check_block(&s, out, s.native.n);
        CHECK(!memcmp(out + s.native.n - 12, "PAD0", 4) && rd32(out + s.native.n - 4) == 0);
      }
      free(out);
      song_free(&s);
    }
  }
  g_templates = saved_t;
  g_chordnotes = saved_c;
}

static void test_every_truncation_is_safe(void) {
  for (int instrument = 0; instrument < 2; instrument++)
    for (int n = instrument ? 4 : 6; n <= (instrument ? 6 : 8); n++) {
      song s;
      make_song(&s, instrument, n, 1, 4096);
      uint8_t *out = malloc(s.native.n);
      char why[180];
      // every size near the ends, every 17th between (the converter's cost is the chart's size)
      for (size_t size = 0; size < s.native.n; size += (size < 400 || size + 400 > s.end) ? 1 : 17) {
        const seven_native_status st = seven_native_sng(s.native.p, size, out, why, sizeof why);
        CHECK(size >= s.end ? st != SEVEN_NATIVE_NOT_NATIVE && st != SEVEN_NATIVE_BAD : st != SEVEN_NATIVE_OK);
      }
      free(out);
      song_free(&s);
    }
}

// --- the manifest ----------------------------------------------------------------------------------------------------
// An arrangement's attributes named `name` ("Bass" is a bass, any other name a guitar) with the marker and a native Tuning
static char *attributes(const char *name, int count, const int *tuning) {
  static char buf[8][1024];
  static int k;
  char *s = buf[k++ & 7];
  s += 0;
  int n = snprintf(s, 1024, "{\"ArrangementName\": \"%s\", \"ExtendedRange\": {\"Version\": 1}, \"Tuning\": {", name);
  for (int i = 0; i < count; i++) n += snprintf(s + n, 1024 - (size_t)n, "%s\"string%d\": %d", i ? ", " : "", i, tuning[i]);
  snprintf(s + n, 1024 - (size_t)n, "}, \"After\": 1}");
  return s;
}
#define ATTR(name, ...) attributes(name, (int)(sizeof((int[]){__VA_ARGS__}) / sizeof(int)), (int[]){__VA_ARGS__})

static char *current(const char *tuning, const char *name) {
  static char buf[8][1024];
  static int k;
  char *s = buf[k++ & 7];
  snprintf(s, 1024, "{\"ArrangementName\": \"%s\", \"Tuning\": {%s}, \"After\": 1}", name, tuning);
  return s;
}

// The rewritten text; "<unchanged>" when the rewriter leaves it to the game
static const char *run_manifest(const char *in, int *rewritten, int *bad) {
  static char buf[8][4096];
  static int k;
  char *res = buf[k++ & 7], *out = NULL;
  size_t n = 0;
  if (!seven_native_manifest(in, strlen(in), &out, &n, rewritten, bad)) return "<unchanged>";
  CHECK(n == strlen(out));
  snprintf(res, 4096, "%s", out);
  free(out);
  return res;
}
static int same(const char *a, const char *b) { return !strcmp(a, b); }

static void test_manifest_guitar_strings_move_to_their_slots(void) {
  int rw = -1, bad = -1;
  CHECK(same(run_manifest(ATTR("Lead", 1, 2, 3, 4, 5, 6), &rw, &bad),
             current("\"string0\": 1, \"string1\": 2, \"string2\": 3, \"string3\": 4, \"string4\": 5, \"string5\": 6", "Lead")));
  CHECK(rw == 1 && bad == 0);
  CHECK(same(run_manifest(ATTR("Lead", -7, 1, 2, 3, 4, 5, 6), NULL, NULL),
             current("\"string0\": 1, \"string1\": 2, \"string2\": 3, \"string3\": 4, \"string4\": 5, \"string5\": 6, "
                     "\"Rs7String6\": -7",
                     "Lead")));
  CHECK(same(run_manifest(ATTR("Lead", -8, -7, 1, 2, 3, 4, 5, 6), NULL, NULL),
             current("\"string0\": 1, \"string1\": 2, \"string2\": 3, \"string3\": 4, \"string4\": 5, \"string5\": 6, "
                     "\"Rs7String6\": -7, \"Rs7String7\": -8",
                     "Lead")));
}

static void test_manifest_bass(void) {
  int rw = -1, bad = -1;
  // a four-string bass is a stock one
  CHECK(same(run_manifest(ATTR("Bass", 1, 2, 3, 4), NULL, NULL),
             current("\"string0\": 1, \"string1\": 2, \"string2\": 3, \"string3\": 4, \"string4\": 0, \"string5\": 0", "Bass")));
  // an extended bass: the low B (native 0) is slot 4, E A D G slots 0-3, a high C slot 5; Rs7String4 (and 5) say so
  CHECK(same(run_manifest(ATTR("Bass", -5, 1, 2, 3, 4), &rw, &bad),
             current("\"string0\": 1, \"string1\": 2, \"string2\": 3, \"string3\": 4, \"string4\": -5, \"string5\": 0, "
                     "\"Rs7String4\": -5",
                     "Bass")));
  CHECK(rw == 1 && bad == 0);
  CHECK(same(run_manifest(ATTR("Bass", -5, 1, 2, 3, 4, 9), &rw, &bad),
             current("\"string0\": 1, \"string1\": 2, \"string2\": 3, \"string3\": 4, \"string4\": -5, \"string5\": 9, "
                     "\"Rs7String4\": -5, \"Rs7String5\": 9",
                     "Bass")));
  CHECK(rw == 1 && bad == 0);
  // seven bass strings, a guitar's name with five (strings of a bass only), a repeated key: harmless six entries
  CHECK(same(run_manifest(ATTR("Bass", 1, 2, 3, 4, 5, 6, 7), &rw, &bad),
             current("\"string0\": 1, \"string1\": 2, \"string2\": 3, \"string3\": 4, \"string4\": 5, \"string5\": 6", "Bass")));
  CHECK(rw == 0 && bad == 1);
  // while the unit cannot build one: a stock Tuning of what is there, counted as invalid, as before wave 2
  seven_native_allow_bass(0);
  CHECK(same(run_manifest(ATTR("Bass", -5, 1, 2, 3, 4), &rw, &bad),
             current("\"string0\": -5, \"string1\": 1, \"string2\": 2, \"string3\": 3, \"string4\": 4, \"string5\": 0", "Bass")));
  CHECK(rw == 0 && bad == 1);
  CHECK(same(run_manifest(ATTR("Bass", -5, 1, 2, 3, 4, 9), NULL, NULL),
             current("\"string0\": -5, \"string1\": 1, \"string2\": 2, \"string3\": 3, \"string4\": 4, \"string5\": 9", "Bass")));
  CHECK(same(run_manifest(ATTR("Bass", 1, 2, 3, 4), NULL, NULL),
             current("\"string0\": 1, \"string1\": 2, \"string2\": 3, \"string3\": 4, \"string4\": 0, \"string5\": 0", "Bass")));
  seven_native_allow_bass(1);
}

// A package with a guitar 7 and a bass 5 (the real test chart's shape): both arrangements of one header file are rewritten
static void test_manifest_mixed_guitar_and_bass_package(void) {
  int rw = -1, bad = -1;
  char in[4096], want[4096];
  snprintf(in, sizeof in, "{\"Entries\": {\"A\": {\"Attributes\": %s}, \"B\": {\"Attributes\": %s}, \"C\": {\"Attributes\": %s}}}",
           ATTR("Lead", -7, 1, 2, 3, 4, 5, 6), ATTR("Bass", -2, 0, 0, 0, 0), ATTR("Rhythm", 0, 0, 0, 0, 0, 0));
  snprintf(want, sizeof want, "{\"Entries\": {\"A\": {\"Attributes\": %s}, \"B\": {\"Attributes\": %s}, \"C\": {\"Attributes\": %s}}}",
           current("\"string0\": 1, \"string1\": 2, \"string2\": 3, \"string3\": 4, \"string4\": 5, \"string5\": 6, \"Rs7String6\": -7", "Lead"),
           current("\"string0\": 0, \"string1\": 0, \"string2\": 0, \"string3\": 0, \"string4\": -2, \"string5\": 0, \"Rs7String4\": -2", "Bass"),
           current("\"string0\": 0, \"string1\": 0, \"string2\": 0, \"string3\": 0, \"string4\": 0, \"string5\": 0", "Rhythm"));
  CHECK(same(run_manifest(in, &rw, &bad), want));
  CHECK(rw == 3 && bad == 0);
}

static void test_manifest_the_arrangement_name_decides_the_instrument(void) {
  int rw, bad;
  const char *seven =
      "\"string0\": 1, \"string1\": 2, \"string2\": 3, \"string3\": 4, \"string4\": 5, \"string5\": 6, \"Rs7String6\": -7";
  // any name but "Bass" is a guitar, as is an arrangement without a name
  CHECK(same(run_manifest(ATTR("Rhythm", -7, 1, 2, 3, 4, 5, 6), NULL, NULL), current(seven, "Rhythm")));
  CHECK(same(run_manifest(ATTR("bass", -7, 1, 2, 3, 4, 5, 6), NULL, NULL), current(seven, "bass")));
  char want[512];
  snprintf(want, sizeof want, "{\"Tuning\": {%s}}", seven);
  CHECK(same(run_manifest("{\"ExtendedRange\": {\"Version\": 1}, \"Tuning\": {\"string0\": -7, \"string1\": 1, \"string2\": 2, "
                 "\"string3\": 3, \"string4\": 4, \"string5\": 5, \"string6\": 6}}",
                 NULL, NULL),
             want));
  // five strings are a bass only
  CHECK(same(run_manifest(ATTR("Lead", -5, 1, 2, 3, 4), &rw, &bad),
             current("\"string0\": -5, \"string1\": 1, \"string2\": 2, \"string3\": 3, \"string4\": 4, \"string5\": 0", "Lead")));
  CHECK(rw == 0 && bad == 1);
}

static void test_manifest_marker_is_removed_wherever_it_stands(void) {
  const char *marker = "\"ExtendedRange\": {\"Version\": 1}";
  const char *native =
      "\"Tuning\": {\"string0\": 0, \"string1\": 0, \"string2\": 0, \"string3\": 0, \"string4\": 0, \"string5\": 0, \"string6\": 0}";
  const char *cur =
      "\"Tuning\": {\"string0\": 0, \"string1\": 0, \"string2\": 0, \"string3\": 0, \"string4\": 0, \"string5\": 0, "
      "\"Rs7String6\": 0}";
  char in[1024], want[1024];
  snprintf(in, sizeof in, "{%s,\r\n  %s}", marker, native);
  snprintf(want, sizeof want, "{%s}", cur);
  CHECK(same(run_manifest(in, NULL, NULL), want));
  snprintf(in, sizeof in, "{%s ,\r\n  %s\r\n}", native, marker);
  snprintf(want, sizeof want, "{%s\r\n}", cur);
  CHECK(same(run_manifest(in, NULL, NULL), want));
  snprintf(in, sizeof in, "{\"A\": [1, {\"b\": \"}\\\"\"}], %s, \"B\": 2, %s}", marker, native);
  snprintf(want, sizeof want, "{\"A\": [1, {\"b\": \"}\\\"\"}], \"B\": 2, %s}", cur);
  CHECK(same(run_manifest(in, NULL, NULL), want));
}

static void test_manifest_every_arrangement_of_a_header_file(void) {
  int rw = -1, bad = -1;
  const char *six = "\"string0\": 0, \"string1\": 0, \"string2\": 0, \"string3\": 0, \"string4\": 0, \"string5\": 0";
  char in[4096], want[4096], seven[256];
  snprintf(in, sizeof in,
           "{\"Entries\": {\"A\": {\"Attributes\": %s}, \"B\": {\"Attributes\": %s}, \"C\": {\"Attributes\": {\"Tuning\": "
           "{\"string0\": 3}}}}, \"InsertRoot\": \"x\"}",
           ATTR("Lead", 0, 0, 0, 0, 0, 0, 0), ATTR("Bass", 0, 0, 0, 0));
  snprintf(seven, sizeof seven, "%s, \"Rs7String6\": 0", six);
  snprintf(want, sizeof want,
           "{\"Entries\": {\"A\": {\"Attributes\": %s}, \"B\": {\"Attributes\": %s}, \"C\": {\"Attributes\": {\"Tuning\": "
           "{\"string0\": 3}}}}, \"InsertRoot\": \"x\"}",
           current(seven, "Lead"), current(six, "Bass"));
  CHECK(same(run_manifest(in, &rw, &bad), want));
  CHECK(rw == 2 && bad == 0);
}

static void test_manifest_text_without_a_native_arrangement_is_left_alone(void) {
  CHECK(same(run_manifest("{\"Tuning\": {\"string0\": 0, \"string6\": 1}}", NULL, NULL), "<unchanged>"));
  CHECK(same(run_manifest("{\"Note\": \"\\\"ExtendedRange\\\"\", \"Tuning\": {\"string0\": 0}}", NULL, NULL), "<unchanged>"));
  CHECK(same(run_manifest("", NULL, NULL), "<unchanged>"));
  CHECK(same(run_manifest("{\"ExtendedRange\": {\"Version\": 1}, \"Tuning\": {\"string0\": 0}", NULL, NULL), "<unchanged>"));  // not closed
  CHECK(same(run_manifest("{\"ExtendedRange\": 1, \"Tuning\": {\"string0\": 0}} trailing", NULL, NULL), "<unchanged>"));
  CHECK(same(run_manifest("{\"ExtendedRange\": 1x\", \"Tuning\": {\"string0\": 0}, \"a\": \"}\"}", NULL, NULL), "<unchanged>"));
  char *out;
  size_t n;
  CHECK(!seven_native_manifest(NULL, 0, &out, &n, NULL, NULL));
  CHECK(!seven_native_manifest("{}", 2, NULL, &n, NULL, NULL));
}

static void test_manifest_an_invalid_native_arrangement_gets_six_harmless_entries(void) {
  int rw = -1, bad = -1;
  const char *six = "\"string0\": 1, \"string1\": 2, \"string2\": 3, \"string3\": 4, \"string4\": 5, \"string5\": 6";
  // nine guitar strings, five guitar strings, seven bass strings
  CHECK(same(run_manifest(ATTR("Lead", 1, 2, 3, 4, 5, 6, 7, 8, 9), &rw, &bad), current(six, "Lead")));
  CHECK(rw == 0 && bad == 1);
  CHECK(same(run_manifest(ATTR("Lead", 1, 2, 3, 4, 5), NULL, NULL),
             current("\"string0\": 1, \"string1\": 2, \"string2\": 3, \"string3\": 4, \"string4\": 5, \"string5\": 0", "Lead")));
  CHECK(same(run_manifest(ATTR("Bass", 1, 2, 3, 4, 5, 6, 7), NULL, NULL), current(six, "Bass")));
  // an unknown version, a marker that is not an object, no Tuning object
  char in[1024], want[1024];
  const char *native =
      "\"Tuning\": {\"string0\": 1, \"string1\": 2, \"string2\": 3, \"string3\": 4, \"string4\": 5, \"string5\": 6, \"string6\": 7}";
  snprintf(want, sizeof want, "{\"Tuning\": {%s}}", six);
  snprintf(in, sizeof in, "{\"ExtendedRange\": {\"Version\": 2}, %s}", native);
  CHECK(same(run_manifest(in, NULL, NULL), want));
  snprintf(in, sizeof in, "{\"ExtendedRange\": 1, %s}", native);
  CHECK(same(run_manifest(in, NULL, NULL), want));
  CHECK(same(run_manifest("{\"ExtendedRange\": {\"Version\": 1}, \"Tuning\": 5}", &rw, &bad), "{\"Tuning\": 5}"));
  CHECK(bad == 1);
  // keys that are not exactly string0..stringN-1 with number values
  CHECK(same(run_manifest("{\"ExtendedRange\": {\"Version\": 1}, \"Tuning\": {\"string0\": 1, \"string1\": 2, \"string2\": 3, "
                 "\"string3\": 4, \"string4\": 5, \"string5\": 6, \"string9\": 7}}",
                 NULL, NULL),
             want));
  CHECK(same(run_manifest("{\"ExtendedRange\": {\"Version\": 1}, \"Tuning\": {\"string0\": 1, \"string1\": 2, \"string2\": 3, "
                 "\"string3\": 4, \"string4\": 5, \"string5\": 6, \"string6\": \"x\"}}",
                 NULL, NULL),
             want));
  CHECK(same(run_manifest("{\"ExtendedRange\": {\"Version\": 1}, \"Tuning\": {\"string0\": 1, \"string1\": 2, \"string2\": 3, "
                 "\"string3\": 4, \"string4\": 5, \"string5\": 6, \"string5\": 7}}",
                 NULL, NULL),
             want));
}

static void test_json_extent_ends_at_the_closing_bracket(void) {
  const char text[] = "  {\"a\": [1, {\"b\": \"}]\\\"\"}], \"c\": {}}GARBAGE{";
  const size_t end = sizeof text - 1 - 8;
  CHECK(seven_native_json_extent(text, sizeof text) == end);
  CHECK(seven_native_json_extent(text, end) == end);
  CHECK(seven_native_json_extent(text, end - 1) == 0);  // not closed within the limit
  CHECK(seven_native_json_extent("[1, 2] x", 16) == 6);
  CHECK(seven_native_json_extent(" \xEF\xBB\xBF{}", 16) == 0);  // a mark is only skipped at the very start
  CHECK(seven_native_json_extent("\xEF\xBB\xBF\r\n{}", 16) == 7);
  CHECK(seven_native_json_extent("{\"a\": 1", 16) == 0);  // ends at the terminator, never closed
  CHECK(seven_native_json_extent("{\"a\\", 16) == 0);
  CHECK(seven_native_json_extent("12", 16) == 0 && seven_native_json_extent("", 16) == 0 &&
        seven_native_json_extent(NULL, 16) == 0);
}

static void test_manifest_text_with_a_mark_or_bytes_behind_it(void) {
  char *attr = ATTR("Lead", -7, 1, 2, 3, 4, 5, 6);
  char marked[2048], behind[2048], want[2048];
  snprintf(marked, sizeof marked, "\xEF\xBB\xBF%s", attr);
  snprintf(want, sizeof want, "\xEF\xBB\xBF%s",
           current("\"string0\": 1, \"string1\": 2, \"string2\": 3, \"string3\": 4, \"string4\": 5, \"string5\": 6, "
                   "\"Rs7String6\": -7",
                   "Lead"));
  CHECK(same(run_manifest(marked, NULL, NULL), want));
  snprintf(behind, sizeof behind, "%s}}junk", attr);
  CHECK(same(run_manifest(behind, NULL, NULL), "<unchanged>"));  // the caller passes the value's own extent
  CHECK(seven_native_json_extent(behind, strlen(behind)) == strlen(attr));
  const char two[2] = {'{', '}'};  // nothing behind it: the mark test must not read a third byte
  CHECK(seven_native_json_extent(two, 1u << 20) == 2);
}

static void test_manifest_offsets_are_whole_semitones_within_two_octaves(void) {
  int rw, bad;
  const char *rest = ", \"string1\": 1, \"string2\": 2, \"string3\": 3, \"string4\": 4, \"string5\": 5}}";
  char in[512];
  CHECK(same(run_manifest(ATTR("Lead", -24, 24, 0, 0, 0, 0), &rw, &bad),
             current("\"string0\": -24, \"string1\": 24, \"string2\": 0, \"string3\": 0, \"string4\": 0, \"string5\": 0", "Lead")));
  CHECK(rw == 1 && bad == 0);
  const char *values[] = {"25", "-25", "0.5", "1e1", "07", "100", "-", "true"};
  for (size_t i = 0; i < sizeof values / sizeof *values; i++) {
    snprintf(in, sizeof in, "{\"ExtendedRange\": {\"Version\": 1}, \"Tuning\": {\"string0\": %s%s", values[i], rest);
    CHECK(same(run_manifest(in, &rw, &bad), "{\"Tuning\": {\"string0\": 0, \"string1\": 1, \"string2\": 2, \"string3\": 3, "
                                   "\"string4\": 4, \"string5\": 5}}"));
    CHECK(rw == 0 && bad == 1);
  }
}

static void test_manifest_repeated_and_nested_members_never_leave_a_seventh_key(void) {
  int rw, bad;
  const char *marker = "\"ExtendedRange\": {\"Version\": 1}";
  const char *native =
      "\"Tuning\": {\"string0\": 1, \"string1\": 2, \"string2\": 3, \"string3\": 4, \"string4\": 5, \"string5\": 6, \"string6\": 7}";
  const char *tuned = "\"Tuning\": {\"string0\": 1, \"string1\": 2, \"string2\": 3, \"string3\": 4, \"string4\": 5, \"string5\": 6}";
  const char *zero = "\"Tuning\": {\"string0\": 0, \"string1\": 0, \"string2\": 0, \"string3\": 0, \"string4\": 0, \"string5\": 0}";
  char in[2048], want[2048];
  // a repeated Tuning: the game's parser may keep either, so both become six entries
  snprintf(in, sizeof in, "{%s, \"Tuning\": {}, %s}", marker, native);
  snprintf(want, sizeof want, "{%s, %s}", zero, tuned);
  CHECK(same(run_manifest(in, &rw, &bad), want));
  CHECK(rw == 0 && bad == 1);
  snprintf(in, sizeof in, "{%s, %s, \"Tuning\": 5}", marker, native);
  snprintf(want, sizeof want, "{%s, \"Tuning\": 5}", tuned);
  CHECK(same(run_manifest(in, NULL, NULL), want));
  // a marker inside a marker: the outer arrangement is still rewritten
  snprintf(in, sizeof in, "{\"ExtendedRange\": {\"Version\": 1, \"ExtendedRange\": 1}, %s}", native);
  CHECK(same(run_manifest(in, NULL, NULL),
             "{\"Tuning\": {\"string0\": 2, \"string1\": 3, \"string2\": 4, \"string3\": 5, \"string4\": 6, "
             "\"string5\": 7, \"Rs7String6\": 1}}"));
  // a marked arrangement inside another one's Tuning: the outer Tuning replaces it
  snprintf(in, sizeof in, "{%s, \"Tuning\": {\"string0\": {%s, %s}, \"string6\": 1}}", marker, marker, native);
  snprintf(want, sizeof want, "{%s}", zero);
  CHECK(same(run_manifest(in, NULL, NULL), want));
}

// --- the three callbacks -----------------------------------------------------------------------------------------------
static uint8_t *host(uint64_t g) { return RC_HOST(g); }
static uint64_t guest_of(const void *h) { return RC_GUEST(h); }
static void set_ptr(uint8_t *p, const void *h) {
  if (rc_guest_ptr_size == 8)
    RC_ST(uint64_t, p, RC_GUEST(h));
  else
    RC_ST(uint32_t, p, (uint32_t)RC_GUEST(h));
}

static void test_callback_psarc(void) {
  rc_mid_frame f = {0};
  g_on = 1;
  // Learn & Play's value (the swapped magic) and the Mac's (the file's bytes) both become "PSAR" in their own form,
  // in the register and in the stored copy
  vals[0] = 0x50535852u, vals[1] = 0x50535852u;
  on_psarc(&f, NULL);
  CHECK(vals[0] == 0x50534152u && vals[1] == 0x50534152u);
  vals[0] = 0x52585350u, vals[1] = 0x52585350u;
  on_psarc(&f, NULL);
  CHECK(vals[0] == 0x52415350u && vals[1] == 0x52415350u);
  // a stock magic, and anything else, stay
  vals[0] = vals[1] = 0x50534152u;
  on_psarc(&f, NULL);
  CHECK(vals[0] == 0x50534152u);
  vals[0] = vals[1] = 0x12345678u;
  on_psarc(&f, NULL);
  CHECK(vals[0] == 0x12345678u && vals[1] == 0x12345678u);
  // off: nothing is touched
  g_on = 0;
  vals[0] = vals[1] = 0x50535852u;
  on_psarc(&f, NULL);
  CHECK(vals[0] == 0x50535852u);
  g_on = 1;
}

static void test_callback_json(void) {
  rc_mid_frame f = {0};
  g_on = 1;
  uint8_t *cell = rc_guest_alloc(16);
  const char *plain = "{\"Tuning\": {\"string0\": 0}}";
  char *text = rc_guest_alloc(2048);
  snprintf(text, 2048, "%s", plain);
  set_ptr(cell, text);
  vals[2] = (uint64_t)(uintptr_t)cell;
  on_json(&f, NULL);
  CHECK(host(rc_guest_ptr_size == 8 ? RC_LD(uint64_t, cell) : RC_LD(uint32_t, cell)) == (uint8_t *)text);  // no marker: untouched

  snprintf(text, 2048, "%s", ATTR("Lead", -7, 1, 2, 3, 4, 5, 6));
  set_ptr(cell, text);
  on_json(&f, NULL);
  const char *now = (const char *)host(rc_guest_ptr_size == 8 ? RC_LD(uint64_t, cell) : RC_LD(uint32_t, cell));
  CHECK(now != text && same(now, current("\"string0\": 1, \"string1\": 2, \"string2\": 3, \"string3\": 4, \"string4\": 5, "
                                         "\"string5\": 6, \"Rs7String6\": -7",
                                         "Lead")));
  CHECK(guest_of(now) != 0);
  // the nested calls of the game's own parsers see the rewritten text: no second rewrite, the pointer stays
  const char *before = now;
  on_json(&f, NULL);
  CHECK((const char *)host(rc_guest_ptr_size == 8 ? RC_LD(uint64_t, cell) : RC_LD(uint32_t, cell)) == before);
  // a later document in the same thread reuses the buffer (or its successor), and the old text is not needed
  snprintf(text, 2048, "%s", ATTR("Lead", -8, -7, 1, 2, 3, 4, 5, 6));
  set_ptr(cell, text);
  on_json(&f, NULL);
  now = (const char *)host(rc_guest_ptr_size == 8 ? RC_LD(uint64_t, cell) : RC_LD(uint32_t, cell));
  CHECK(strstr(now, "\"Rs7String7\": -8") != NULL);
  // a null cursor or text is left alone
  vals[2] = 0;
  on_json(&f, NULL);
  set_ptr(cell, NULL);
  vals[2] = (uint64_t)(uintptr_t)cell;
  on_json(&f, NULL);
}

static void test_callback_sng(void) {
  rc_mid_frame f = {0};
  g_on = 1;
  song s;
  make_song(&s, 0, 7, 1, 4096);
  uint8_t *body = rc_guest_alloc(s.native.n);
  memcpy(body, s.native.p, s.native.n);
  vals[3] = (uint64_t)(uintptr_t)body, vals[4] = s.native.n;
  f.go = 0;
  on_sng(&f, NULL);
  CHECK(f.go == 0);  // converted in place: the loader goes on
  CHECK(!memcmp(body, s.stock.p, s.stock.n));
  check_block(&s, body, s.native.n);
  // a stock body is not touched
  memcpy(body, s.stock.p, s.stock.n);
  const uint8_t first = body[0];
  on_sng(&f, NULL);
  CHECK(f.go == 0 && body[0] == first && !memcmp(body, s.stock.p, s.stock.n));
  // a native body that cannot be converted goes to the loader's failure exit; an extended bass too while the unit cannot
  // build one (SEVEN_F_BASS), and is converted (an RS7X block with STRS only) when it can
  memcpy(body, s.native.p, s.native.n);
  body[6] = 17;
  on_sng(&f, NULL);
  CHECK(f.go == 1);
  song_free(&s);
  make_song(&s, 1, 5, 1, 4096);
  memcpy(body, s.native.p, s.native.n);
  vals[4] = s.native.n;
  f.go = 0;
  g_bass_feature = 0;
  on_sng(&f, NULL);
  CHECK(f.go == 1 && !memcmp(body, "SNXR", 4));
  g_bass_feature = 1;
  f.go = 0;
  on_sng(&f, NULL);
  CHECK(f.go == 0 && !memcmp(body, s.stock.p, s.stock.n));
  check_block(&s, body, s.native.n);
  memcpy(body, s.native.p, s.native.n);
  // switched off: the body stays native for the stock parser (the game fails it as before)
  g_on = 0;
  f.go = 0;
  on_sng(&f, NULL);
  CHECK(f.go == 0 && !memcmp(body, "SNXR", 4));
  g_on = 1;
  song_free(&s);
}

static void test_init_needs_all_three_points(void) {
  g_taken = 0;
  g_on = 0;
  CHECK(seven_native_init() == 0 && g_on == 1);
  g_taken = 1;  // the first call to seven_mid_optional now answers 1, the next two 0
  g_taken = 2;
  CHECK(seven_native_init() == 0 && g_on == 0);
}

int main(void) {
#ifdef GUEST32
  rc_slide = (uint64_t)(uintptr_t)window;
  window_used = 0x100;
#endif
  test_guitar_columns_and_tail();
  test_extended_bass_columns_and_tail();
  test_extended_bass_is_refused_while_off();
  test_extended_bass_padding();
  test_extended_bass_bad_data();
  test_detection_and_bad_headers();
  test_counts_and_padding_rules();
  test_padding_missing_bytes_and_exact_fit();
  test_every_truncation_is_safe();
  test_manifest_guitar_strings_move_to_their_slots();
  test_manifest_bass();
  test_manifest_mixed_guitar_and_bass_package();
  test_manifest_the_arrangement_name_decides_the_instrument();
  test_manifest_marker_is_removed_wherever_it_stands();
  test_manifest_every_arrangement_of_a_header_file();
  test_manifest_text_without_a_native_arrangement_is_left_alone();
  test_manifest_an_invalid_native_arrangement_gets_six_harmless_entries();
  test_json_extent_ends_at_the_closing_bracket();
  test_manifest_text_with_a_mark_or_bytes_behind_it();
  test_manifest_offsets_are_whole_semitones_within_two_octaves();
  test_manifest_repeated_and_nested_members_never_leave_a_seventh_key();
  test_callback_psarc();
  test_callback_json();
  test_callback_sng();
  test_init_needs_all_three_points();
  return check_done("test_seven_native");
}
