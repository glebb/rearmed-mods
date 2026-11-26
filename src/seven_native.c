// Seven-String Guitar (seven.h): the native N-string format, read. Ported from rs7string 1.0.0-beta.5/6
// (github.com/SomeeName/rs7string, MIT, by SomeeName): src/model/nativesng and nativemanifest (the converters) and
// src/features/native/hooks.cpp (the three hooks); the format is docs/release/NATIVE-FORMAT.md there (seven_native.h).
//
// rs7string beta.5 reads a native package by translating it back, while the game loads it, to its internal form;
// that form changed since the commit this port was made from (2edcf68), so what is written here is the old one, which
// the rest of this unit reads: the SNG's extension block is RS7X (upstream now RSXR), the manifest's extra keys
// Rs7String6 / Rs7String7 (upstream now RsxrString6 / 7), and the stock strings are in the game's slot order: string
// index i of an N-string guitar (thickest first) is slot 6 + (N - 7 - i) for the strings below the low E, and slot
// i - (N - 6) for the rest (a 7-string: i 0 = slot 6, the low B; i 1..6 = slots 0..5; an 8-string: i 0 = slot 7, i 1 =
// slot 6).
//   seven_native_psarc  PN1, the PSARC open function's compare with "PSAR" (the byte-swapped magic of the header,
//                       also kept for the table of contents): a "PSXR" magic is replaced by "PSAR" in both places.
//                       Only when Plain Packages is on: a native package is plain, the encrypted table of contents
//                       reader would take its table for ciphertext
//   seven_native_json   JM1, the entry of the JSON parser (all its callers, the array and object parsers too): a text
//                       with "ExtendedRange" (the whole document comes in at its first, outermost call; the rewritten
//                       text has no marker, so the nested calls and the parsers' loops leave it alone) is rewritten in
//                       a buffer of the game's memory, one a thread, and the text pointer the caller passed by
//                       reference is moved to it
//   seven_native_sng    XI1, SNG_Song_Load after the inflate and its size check, before the first section is read:
//                       a body starting "SNXR" is translated in place (the same size: the chart's zero padding after
//                       MetaData takes the RS7X block), one that cannot be goes to the loader's own failure exit
// The three points are taken together or the part stays off (seven_mid_optional); the unit stays on either way.
//
// Extended bass (instrument 1 with 5 or 6 strings) is not supported: its chart is refused with a log line (the song
// does not load), its manifest gets a stock Tuning. A four-string bass in a native header is a stock bass and loads.
// A six-string guitar in a native header is stock too (no block).
#include <dlfcn.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "seven.h"
#include "seven_native.h"

// ---- the chart (rs7string's model/nativesng) -------------------------------------------------------------------
#define MAX_STRINGS 8
#define NATIVE_HEADER 8
#define RS7X_HEADER_SIZE 16
#define RS7X_SECTION_SIZE 12

typedef struct {
  const uint8_t *in;
  size_t size, at;
  uint8_t *out;  // NULL: the sizing pass, which writes nothing
  size_t cap;
  uint64_t w;
  int instrument, n, slot[MAX_STRINGS];
  uint32_t templates, notes;
  const uint8_t *tdata, *cdata;  // the native chord templates and ChordNotes, for the block
  const char *field;             // what is read now, and where the first failure was
  size_t err_at;
  int failed;
} conv;

#define T(x) \
  do {       \
    if (!(x)) return 0; \
  } while (0)

static int bad(conv *c, const char *field, size_t at) {
  if (!c->failed) {
    c->failed = 1;
    c->field = field;
    c->err_at = at;
  }
  return 0;
}

static int take(conv *c, size_t k, const uint8_t **p) {
  if (k > c->size - c->at) return bad(c, c->field, c->at);
  *p = c->in + c->at;
  c->at += k;
  return 1;
}

static int rd_u32(conv *c, uint32_t *v) {
  const uint8_t *p;
  T(take(c, 4, &p));
  memcpy(v, p, 4);
  return 1;
}

static int put(conv *c, const void *p, size_t k) {
  if (c->out) {
    if (c->w > c->cap || k > c->cap - (size_t)c->w) return bad(c, "output bounds", 0);
    if (k) memcpy(c->out + c->w, p, k);
  }
  c->w += k;
  return 1;
}

static int put_u8(conv *c, unsigned v) {
  uint8_t b = (uint8_t)v;
  return put(c, &b, 1);
}
static int put_u16(conv *c, unsigned v) {
  uint16_t b = (uint16_t)v;
  return put(c, &b, 2);
}
static int put_u32(conv *c, uint32_t v) { return put(c, &v, 4); }

static int patch(conv *c, uint64_t off, uint32_t v) {
  if (c->out) {
    if (off > c->cap || 4 > c->cap - (size_t)off) return bad(c, "output patch bounds", 0);
    memcpy(c->out + off, &v, 4);
  }
  return 1;
}

static int copy(conv *c, size_t k) {
  const uint8_t *p;
  T(take(c, k, &p));
  return put(c, p, k);
}

// A count (a signed 32-bit number on the wire) of items of at least `min` bytes, written as it is
static int count(conv *c, const char *field, size_t min, uint32_t *n) {
  c->field = field;
  const size_t off = c->at;
  uint32_t v;
  T(rd_u32(c, &v));
  if (v > INT32_MAX || v > (c->size - c->at) / min) return bad(c, field, off);
  *n = v;
  return put_u32(c, v);
}

static int fixed(conv *c, const char *field, size_t stride, uint32_t *n) {
  uint32_t k;
  T(count(c, field, stride, &k));
  if (n) *n = k;
  return copy(c, (size_t)k * stride);
}

// The game's slot of native string i: the strings below the low E take slots 6 (and 7) from the lowest up
static int guitar_slot(int n, int i) {
  const int extra = n - 6;
  return i < extra ? 6 + (extra - 1 - i) : i - extra;
}

static int native_of(const conv *c, int slot) {
  for (int i = 0; i < c->n; i++)
    if (c->slot[i] == slot) return i;
  return 0;  // never: the slots of a guitar are a permutation of 0..n-1
}

// 8 bytes: "SNXR", version 1, instrument, string count, 0
static seven_native_status read_header(conv *c, char *why, size_t cap) {
  const uint8_t *p;
  c->field = "header";
  if (!take(c, NATIVE_HEADER, &p)) return SEVEN_NATIVE_BAD;
  if (p[4] != 1) return bad(c, "header version", 4), SEVEN_NATIVE_BAD;
  if (p[5] > 1) return bad(c, "header instrument", 5), SEVEN_NATIVE_BAD;
  const int guitar = p[5] == 0, n = p[6];
  if (guitar ? (n < 6 || n > 8) : (n < 4 || n > 6)) return bad(c, "header string count", 6), SEVEN_NATIVE_BAD;
  if (p[7] != 0) return bad(c, "header reserved", 7), SEVEN_NATIVE_BAD;
  c->instrument = p[5];
  c->n = n;
  if (!guitar && n != 4) {
    if (why && cap) snprintf(why, cap, "an extended bass (%d strings) is not supported", n);
    return SEVEN_NATIVE_UNSUPPORTED;
  }
  for (int i = 0; i < n; i++) c->slot[i] = guitar ? guitar_slot(n, i) : i;
  return SEVEN_NATIVE_OK;
}

// Chord templates: the stock record (0x48 bytes) of the stock strings, the extra strings' frets and fingers checked (the
// block takes them)
static int templates(conv *c) {
  const int n = c->n;
  const size_t stride = 36 + 6 * (size_t)n;
  T(count(c, "chord templates", stride, &c->templates));
  c->tdata = c->in + c->at;
  for (uint32_t row = 0; row < c->templates; row++) {
    const uint8_t *p;
    uint8_t stock[72] = {0};
    T(take(c, stride, &p));
    memcpy(stock, p, 4);
    memset(stock + 4, 255, 36);  // unused columns: fret, finger and note are all -1
    for (int i = 0; i < n; i++) {
      const int slot = c->slot[i];
      if (slot < 6) {
        stock[4 + slot] = p[4 + i];
        stock[10 + slot] = p[4 + n + i];
        memcpy(stock + 16 + 4 * slot, p + 4 + 2 * n + 4 * i, 4);
      } else {
        const int fret = (int8_t)p[4 + i], finger = (int8_t)p[4 + n + i];
        if (fret < -1 || fret > 24 || finger < -1 || finger > 4 || (fret == -1 && finger != -1))
          return bad(c, "chord template extra fret/finger", (size_t)(p - c->in) + 4 + (size_t)i);
      }
    }
    memcpy(stock + 40, p + 4 + 6 * (size_t)n, 32);
    T(put(c, stock, sizeof stock));
  }
  return 1;
}

// ChordNotes: the stock record (0x948 bytes) of the stock strings, the extra strings' slides checked
static int techniques(conv *c) {
  const size_t n = (size_t)c->n, stride = 396 * n;
  T(count(c, "ChordNotes", stride, &c->notes));
  c->cdata = c->in + c->at;
  for (uint32_t row = 0; row < c->notes; row++) {
    const uint8_t *p;
    uint8_t stock[2376] = {0};
    T(take(c, stride, &p));
    memset(stock + 2352, 255, 12);
    for (size_t i = 0; i < n; i++) {
      const int slot = c->slot[i];
      const size_t bend = 4 * n + 388 * i, slide = 392 * n + i;
      uint32_t used;
      memcpy(&used, p + bend + 384, 4);
      if (used > 32) return bad(c, "ChordNotes bend count", (size_t)(p - c->in) + bend + 384);
      if (slot < 6) {
        memcpy(stock + 4 * slot, p + 4 * i, 4);
        memcpy(stock + 24 + 388 * slot, p + bend, 388);
        stock[2352 + slot] = p[slide];
        stock[2358 + slot] = p[slide + n];
        memcpy(stock + 2364 + 2 * slot, p + 394 * n + 2 * i, 2);
      } else {
        const int a = (int8_t)p[slide], b = (int8_t)p[slide + n];
        if (a < -1 || a > 24 || b < -1 || b > 24) return bad(c, "ChordNotes extra slide", (size_t)(p - c->in) + slide);
      }
    }
    T(put(c, stock, sizeof stock));
  }
  return 1;
}

static int linked_difficulties(conv *c) {
  uint32_t k;
  T(count(c, "linked difficulties", 8, &k));
  for (uint32_t i = 0; i < k; i++) {
    T(copy(c, 4));
    T(fixed(c, "linked difficulty phrases", 4, NULL));
  }
  return 1;
}

// Sections: 52 bytes, then 36 string masks, bit i = native string i, which become bit slot
static int sections(conv *c) {
  uint32_t k;
  T(count(c, "sections", 88, &k));
  for (uint32_t i = 0; i < k; i++) {
    const uint8_t *masks;
    T(copy(c, 52));
    T(take(c, 36, &masks));
    for (int m = 0; m < 36; m++) {
      if (masks[m] >> c->n) return bad(c, "section string mask", c->at - 36 + (size_t)m);
      uint8_t mapped = 0;
      for (int s = 0; s < c->n; s++)
        if (masks[m] & (1 << s)) mapped |= (uint8_t)(1 << c->slot[s]);
      T(put_u8(c, mapped));
    }
  }
  return 1;
}

static int level_notes(conv *c) {
  uint32_t k;
  T(count(c, "level notes", 67, &k));
  for (uint32_t i = 0; i < k; i++) {
    const uint8_t *p;
    T(take(c, 63, &p));
    T(put(c, p, 16));
    const int string = (int8_t)p[16];
    if (string < -1 || string >= c->n) return bad(c, "note string index", c->at - 63 + 16);
    T(put_u8(c, string == -1 ? 0xff : (unsigned)c->slot[string]));
    T(put(c, p + 17, 46));
    T(fixed(c, "note bends", 12, NULL));
  }
  return 1;
}

static int levels(conv *c) {
  uint32_t k;
  T(count(c, "levels", 36, &k));
  for (uint32_t i = 0; i < k; i++) {
    c->field = "level difficulty";
    T(copy(c, 4));
    T(fixed(c, "level anchors", 28, NULL));
    T(fixed(c, "level anchor extensions", 12, NULL));
    T(fixed(c, "level handshapes", 20, NULL));
    T(fixed(c, "level arpeggios", 20, NULL));
    T(level_notes(c));
    T(fixed(c, "level average notes", 4, NULL));
    T(fixed(c, "level notes excluding ignored", 4, NULL));
    T(fixed(c, "level notes all", 4, NULL));
  }
  return 1;
}

// MetaData: the tuning in slot order (a guitar keeps N entries, a bass its six), the rest of the body zero
static int metadata(conv *c) {
  c->field = "MetaData";
  T(copy(c, 79));
  const size_t off = c->at;
  uint32_t k;
  const uint8_t *p;
  T(rd_u32(c, &k));
  if (k != (uint32_t)c->n) return bad(c, "MetaData tuning count", off);
  T(take(c, 2 * (size_t)c->n, &p));
  int16_t tuning[MAX_STRINGS] = {0};
  for (int i = 0; i < c->n; i++) memcpy(&tuning[c->slot[i]], p + 2 * i, 2);
  const int entries = c->instrument ? 6 : c->n;
  T(put_u32(c, (uint32_t)entries));
  T(put(c, tuning, 2 * (size_t)entries));
  T(copy(c, 12));
  for (size_t i = c->at; i < c->size; i++)
    if (c->in[i]) return bad(c, "nonzero padding", i);
  return 1;
}

// A block's section: id, version 1, flags, the payload's size (patched by end_section)
static int section(conv *c, const char *id, unsigned flags, uint64_t *size_at) {
  T(put(c, id, 4));
  T(put_u16(c, 1));
  T(put_u16(c, flags));
  *size_at = c->w;
  return put_u32(c, 0);
}
static int end_section(conv *c, uint64_t size_at) { return patch(c, size_at, (uint32_t)(c->w - size_at - 4)); }

// The RS7X block (docs/release/RS7X-SPEC.md at rs7string 2edcf68), in the padding after MetaData: STRS, CHDT, CHNT and
// PAD0 (non-critical, the rest of the buffer: the block's size is what is left)
static int extension(conv *c) {
  const uint64_t start = c->w;
  const int n = c->n;
  uint64_t sec;
  T(put(c, "RS7X", 4));
  T(put_u16(c, 1));
  T(put_u16(c, RS7X_HEADER_SIZE));
  T(put_u32(c, c->out ? (uint32_t)(c->cap - (size_t)start) : 0));
  T(put_u32(c, 4));
  T(section(c, "STRS", 1, &sec));
  T(put_u8(c, 0));  // guitar
  T(put_u8(c, (unsigned)n));
  T(put_u8(c, (unsigned)(n - 6)));
  T(put_u8(c, 0));
  for (int slot = 6; slot < n; slot++) T(put_u8(c, (unsigned)slot));
  T(end_section(c, sec));

  T(section(c, "CHDT", 1, &sec));
  T(put_u32(c, c->templates));
  for (uint32_t row = 0; row < c->templates; row++) {
    const uint8_t *p = c->tdata + (size_t)row * (36 + 6 * (size_t)n);
    for (int slot = 6; slot < n; slot++) {
      const int i = native_of(c, slot);
      T(put_u8(c, p[4 + i]));
      T(put_u8(c, p[4 + n + i]));
    }
  }
  T(end_section(c, sec));

  T(section(c, "CHNT", 0, &sec));
  T(put_u32(c, c->notes));
  for (uint32_t row = 0; row < c->notes; row++) {
    const uint8_t *p = c->cdata + (size_t)row * 396 * (size_t)n;
    for (int slot = 6; slot < n; slot++) {
      const int i = native_of(c, slot);
      const uint8_t *bends = p + 4 * (size_t)n + 388 * (size_t)i;
      uint32_t used;
      memcpy(&used, bends + 384, 4);
      T(put(c, p + 4 * i, 4));
      T(put_u8(c, p[392 * (size_t)n + (size_t)i]));
      T(put_u8(c, p[393 * (size_t)n + (size_t)i]));
      T(put(c, p + 394 * (size_t)n + 2 * (size_t)i, 2));
      T(put_u8(c, used));
      T(put(c, bends, 12 * (size_t)(used & 0xff)));
    }
  }
  T(end_section(c, sec));

  T(section(c, "PAD0", 0, &sec));
  if (c->out) T(patch(c, sec, (uint32_t)(c->cap - (size_t)c->w)));
  return 1;
}

static int run(conv *c) {
  T(fixed(c, "beats", 16, NULL));
  T(fixed(c, "phrases", 44, NULL));
  T(templates(c));
  T(techniques(c));
  uint32_t vocals;
  T(fixed(c, "vocals", 60, &vocals));
  if (vocals) {
    T(fixed(c, "symbol headers", 32, NULL));
    T(fixed(c, "symbol textures", 144, NULL));
    T(fixed(c, "symbol definitions", 44, NULL));
  }
  T(fixed(c, "phrase iterations", 24, NULL));
  T(fixed(c, "phrase extra info", 16, NULL));
  T(linked_difficulties(c));
  T(fixed(c, "actions", 260, NULL));
  T(fixed(c, "events", 260, NULL));
  T(fixed(c, "tones", 8, NULL));
  T(fixed(c, "DNA", 8, NULL));
  T(sections(c));
  T(levels(c));
  T(metadata(c));
  if (!c->instrument && c->n > 6) T(extension(c));  // a stock-shaped guitar or bass has no block
  return 1;
}

static seven_native_status fail(const conv *c, char *why, size_t cap) {
  if (why && cap) snprintf(why, cap, "%s at byte %zu", c->field, c->err_at);
  return SEVEN_NATIVE_BAD;
}

int seven_native_is_sng(const uint8_t *body, size_t size) { return body && size >= 4 && !memcmp(body, "SNXR", 4); }

seven_native_status seven_native_sng(const uint8_t *body, size_t size, uint8_t *out, char *why, size_t why_cap) {
  if (why && why_cap) why[0] = 0;
  if (!seven_native_is_sng(body, size)) return SEVEN_NATIVE_NOT_NATIVE;
  if (!out || out == body || size > UINT32_MAX) {
    if (why && why_cap) snprintf(why, why_cap, "no separate output, or a body past the format's 32-bit size");
    return SEVEN_NATIVE_BAD;
  }
  conv sizing = {.in = body, .size = size};
  seven_native_status st = read_header(&sizing, why, why_cap);
  if (st != SEVEN_NATIVE_OK) return st == SEVEN_NATIVE_BAD ? fail(&sizing, why, why_cap) : st;
  if (!run(&sizing)) return fail(&sizing, why, why_cap);
  if (sizing.w > size) {
    if (why && why_cap) snprintf(why, why_cap, "missing %llu bytes of padding", (unsigned long long)(sizing.w - size));
    return SEVEN_NATIVE_TOO_SMALL;
  }
  memset(out, 0, size);
  conv writing = {.in = body, .size = size, .out = out, .cap = size};
  read_header(&writing, NULL, 0);
  if (!run(&writing)) return fail(&writing, why, why_cap);
  return SEVEN_NATIVE_OK;
}

// ---- the manifest (rs7string's model/nativemanifest) ---------------------------------------------------------------
#define MAX_TEXT (16u << 20)
#define MAX_DEPTH 64
#define MAX_KEYS 16
static const char k_marker[] = "\"ExtendedRange\"";

typedef struct {
  size_t begin;      // the key's opening quote
  size_t kb, ke;     // the key without its quotes
  size_t vb, ve;     // the value
  int object;        // index of the value's object, -1 when it is not one
} member;
typedef struct {
  member *m;
  size_t n, cap;
} object;
typedef struct {
  const char *s;
  size_t size, at;
  object *o;
  size_t no, cap;
  jmp_buf jb;
} scan;

typedef struct {
  size_t begin, end;
  char *text;  // NULL: the span is removed
} edit;

static void scan_fail(scan *sc) { longjmp(sc->jb, 1); }

static void *grow(void *p, size_t *cap, size_t n, size_t each, scan *sc) {
  if (n < *cap) return p;
  size_t k = *cap ? *cap * 2 : 8;
  while (k <= n) k *= 2;
  void *q = realloc(p, k * each);
  if (!q) {
    if (sc) scan_fail(sc);
    return NULL;
  }
  *cap = k;
  return q;
}

static char peek(const scan *sc) { return sc->at < sc->size ? sc->s[sc->at] : '\0'; }
static void space(scan *sc) {
  while (sc->at < sc->size && (sc->s[sc->at] == ' ' || sc->s[sc->at] == '\t' || sc->s[sc->at] == '\r' || sc->s[sc->at] == '\n'))
    sc->at++;
}
static void expect(scan *sc, char c) {
  if (peek(sc) != c) scan_fail(sc);
  sc->at++;
}
static int is_alnum(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

// A string: at its opening quote, left one past the closing one
static void scan_string(scan *sc) {
  expect(sc, '"');
  while (sc->at < sc->size && sc->s[sc->at] != '"') sc->at += sc->s[sc->at] == '\\' ? 2 : 1;
  expect(sc, '"');
}

static int scan_value(scan *sc, int depth);

static void scan_array(scan *sc, int depth) {
  sc->at++;
  space(sc);
  if (peek(sc) == ']') {
    sc->at++;
    return;
  }
  for (;;) {
    scan_value(sc, depth + 1);
    space(sc);
    if (peek(sc) != ',') break;
    sc->at++;
    space(sc);
  }
  expect(sc, ']');
}

static int scan_object(scan *sc, int depth) {
  sc->o = grow(sc->o, &sc->cap, sc->no, sizeof *sc->o, sc);
  const size_t index = sc->no++;
  memset(&sc->o[index], 0, sizeof sc->o[index]);
  sc->at++;
  space(sc);
  if (peek(sc) != '}') {
    for (;;) {
      member m;
      m.begin = sc->at;
      scan_string(sc);
      m.kb = m.begin + 1;
      m.ke = sc->at - 1;
      space(sc);
      expect(sc, ':');
      space(sc);
      m.vb = sc->at;
      m.object = scan_value(sc, depth + 1);
      m.ve = sc->at;
      object *o = &sc->o[index];
      o->m = grow(o->m, &o->cap, o->n, sizeof *o->m, sc);
      o->m[o->n++] = m;
      space(sc);
      if (peek(sc) != ',') break;
      sc->at++;
      space(sc);
    }
  }
  expect(sc, '}');
  return (int)index;
}

// The object's index of an object value, else -1
static int scan_value(scan *sc, int depth) {
  if (depth > MAX_DEPTH) scan_fail(sc);
  const char c = peek(sc);
  if (c == '{') return scan_object(sc, depth);
  if (c == '[') {
    scan_array(sc, depth);
    return -1;
  }
  if (c == '"') {
    scan_string(sc);
    return -1;
  }
  const size_t begin = sc->at;  // a number or a literal: letters, digits, sign, point
  while (sc->at < sc->size && (is_alnum(sc->s[sc->at]) || sc->s[sc->at] == '-' || sc->s[sc->at] == '+' || sc->s[sc->at] == '.'))
    sc->at++;
  if (sc->at == begin) scan_fail(sc);
  return -1;
}

static void scan_run(scan *sc) {
  if (sc->size >= 3 && !memcmp(sc->s, "\xEF\xBB\xBF", 3)) sc->at = 3;
  space(sc);
  scan_value(sc, 0);
  space(sc);
  if (sc->at != sc->size) scan_fail(sc);
}

static void scan_free(scan *sc) {
  for (size_t i = 0; i < sc->no; i++) free(sc->o[i].m);
  free(sc->o);
}

static size_t klen(const member *m) { return m->ke - m->kb; }
static int text_is(const scan *sc, size_t b, size_t e, const char *lit) {
  return e - b == strlen(lit) && !memcmp(sc->s + b, lit, e - b);
}
static const member *find(const scan *sc, const object *o, const char *key) {
  for (size_t i = 0; i < o->n; i++)
    if (text_is(sc, o->m[i].kb, o->m[i].ke, key)) return &o->m[i];
  return NULL;
}

// A growing zero-terminated text
typedef struct {
  char *p;
  size_t n, cap;
  int oom;
} sbuf;
static void sb_add(sbuf *b, const char *s, size_t k) {
  if (b->oom) return;
  if (b->n + k + 1 > b->cap) {
    size_t cap = b->cap ? b->cap : 64;
    while (b->n + k + 1 > cap) cap *= 2;
    char *q = realloc(b->p, cap);
    if (!q) {
      b->oom = 1;
      return;
    }
    b->p = q;
    b->cap = cap;
  }
  memcpy(b->p + b->n, s, k);
  b->n += k;
  b->p[b->n] = 0;
}
static void sb_str(sbuf *b, const char *s) { sb_add(b, s, strlen(s)); }

static int is_digit(char c) { return c >= '0' && c <= '9'; }

// A tuning offset: a whole number of semitones from -24 to 24, written plainly (no fraction, exponent or leading zero)
static int is_offset(const char *v, size_t k) {
  if (k && v[0] == '-') v++, k--;
  if (!k || k > 2 || !is_digit(v[0]) || (k == 2 && (v[0] == '0' || !is_digit(v[1])))) return 0;
  return (k == 1 ? v[0] - '0' : (v[0] - '0') * 10 + (v[1] - '0')) <= 24;
}

// "stringI" with I in 0..15 and no leading zero: I, else -1
static int string_index(const char *k, size_t len) {
  if (len <= 6 || len > 8 || memcmp(k, "string", 6)) return -1;
  const char *n = k + 6;
  if (len == 7) return is_digit(n[0]) ? n[0] - '0' : -1;
  return n[0] == '1' && n[1] >= '0' && n[1] <= '5' ? 10 + (n[1] - '0') : -1;
}

static void put_key(sbuf *b, const char *key, const char *v, size_t k) {
  if (b->n > 1) sb_str(b, ", ");
  sb_str(b, "\"");
  sb_str(b, key);
  sb_str(b, "\": ");
  sb_add(b, v, k);
}

// The unit's Tuning of a native arrangement: string0..5 in the game's slot order, then Rs7String6 / Rs7String7. A guitar
// of 6 to 8 strings or a four-string bass is valid; an extended bass is not (it is no instrument of this unit). *valid
// comes in as "may be valid" and goes out 0 when the arrangement is not a valid native one: the result is then the
// harmless six-entry form, from what is there.
static char *tuning_text(const scan *sc, const object *arrangement, const object *marker, const object *tuning, int *valid) {
  static const char *const stock[6] = {"string0", "string1", "string2", "string3", "string4", "string5"};
  const char *native[MAX_KEYS];
  size_t nlen[MAX_KEYS];
  int seen[MAX_KEYS] = {0};
  const int count = (int)(tuning->n < MAX_KEYS + 1 ? tuning->n : MAX_KEYS + 1);
  const member *version = find(sc, marker, "Version"), *name = find(sc, arrangement, "ArrangementName");
  const int bass = name && text_is(sc, name->vb, name->ve, "\"Bass\"");
  const int shape = bass ? count == 4 : count >= 6 && count <= 8;
  *valid = *valid && version && text_is(sc, version->vb, version->ve, "1") && shape;
  for (size_t m = 0; m < tuning->n; m++) {
    const member *mem = &tuning->m[m];
    const int i = string_index(sc->s + mem->kb, klen(mem));
    const char *value = sc->s + mem->vb;
    const size_t vlen = mem->ve - mem->vb;
    if (i < 0 || i >= count || i >= MAX_KEYS || seen[i] || !is_offset(value, vlen)) {
      *valid = 0;
      continue;
    }
    seen[i] = 1;
    native[i] = value;
    nlen[i] = vlen;
  }
  const char *slots[MAX_STRINGS];
  size_t slen[MAX_STRINGS];
  for (int s = 0; s < MAX_STRINGS; s++) slots[s] = "0", slen[s] = 1;
  for (int i = 0; i < count && i < MAX_KEYS; i++) {
    const int slot = *valid ? (bass ? i : guitar_slot(count, i)) : i < 6 ? i : -1;
    if (slot >= 0 && seen[i]) slots[slot] = native[i], slen[slot] = nlen[i];
  }
  sbuf out = {0};
  sb_str(&out, "{");
  for (int s = 0; s < 6; s++) put_key(&out, stock[s], slots[s], slen[s]);
  if (*valid && !bass && count >= 7) put_key(&out, "Rs7String6", slots[6], slen[6]);
  if (*valid && !bass && count == 8) put_key(&out, "Rs7String7", slots[7], slen[7]);
  sb_str(&out, "}");
  if (out.oom) {
    free(out.p);
    return NULL;
  }
  return out.p;
}

static int edit_cmp(const void *a, const void *b) {
  const edit *x = a, *y = b;
  return x->begin < y->begin ? -1 : x->begin > y->begin;
}

// 1 with *out the rewritten text; 0 when there is nothing to rewrite; -1 on out of memory
static int rewrite(const char *text, size_t size, char **out, size_t *out_len, int *rewritten, int *bad_count, scan *sc) {
  static const object none;
  edit *edits = NULL;
  size_t ne = 0, cap = 0;
  int found = 0, badn = 0, rc = -1;
  scan_run(sc);
  for (size_t oi = 0; oi < sc->no; oi++) {
    const object *o = &sc->o[oi];
    const member *marker = find(sc, o, "ExtendedRange");
    if (!marker) continue;
    // The marker goes, with the comma that separated it from its neighbour
    const size_t index = (size_t)(marker - o->m);
    edits = grow(edits, &cap, ne + 3, sizeof *edits, NULL);
    if (!edits) goto done;
    if (index + 1 < o->n)
      edits[ne++] = (edit){marker->begin, o->m[index + 1].begin, NULL};
    else if (index > 0)
      edits[ne++] = (edit){o->m[index - 1].ve, marker->ve, NULL};
    else
      edits[ne++] = (edit){marker->begin, marker->ve, NULL};
    // Every Tuning member is rewritten (the game's parser may keep the first or the last of a repeated key); the
    // arrangement is valid only with exactly one, an object
    const object *info = marker->object >= 0 ? &sc->o[marker->object] : &none;
    size_t tunings = 0;
    for (size_t m = 0; m < o->n; m++) tunings += text_is(sc, o->m[m].kb, o->m[m].ke, "Tuning");
    int valid = tunings == 1;
    for (size_t m = 0; m < o->n; m++) {
      if (!text_is(sc, o->m[m].kb, o->m[m].ke, "Tuning")) continue;
      if (o->m[m].object < 0) {
        valid = 0;
        continue;
      }
      int one = tunings == 1;
      char *current = tuning_text(sc, o, info, &sc->o[o->m[m].object], &one);
      if (!current) goto done;
      valid = valid && one;
      edits = grow(edits, &cap, ne + 3, sizeof *edits, NULL);
      if (!edits) {
        free(current);
        goto done;
      }
      edits[ne++] = (edit){o->m[m].vb, o->m[m].ve, current};
    }
    if (valid)
      found++;
    else
      badn++;
  }
  if (!ne) {
    rc = 0;
    goto done;
  }
  // Two edits overlap only when a marked object sits inside another one's marker or Tuning value: the spans of a JSON
  // text nest, so the inner edit lies inside the outer one, which replaces it
  qsort(edits, ne, sizeof *edits, edit_cmp);
  size_t kept = 0;
  for (size_t i = 1; i < ne; i++) {
    if (edits[i].begin >= edits[kept].end) {
      edits[++kept] = edits[i];
    } else {
      free(edits[i].text);
    }
  }
  sbuf res = {0};
  size_t pos = 0;
  for (size_t i = 0; i <= kept; i++) {
    sb_add(&res, text + pos, edits[i].begin - pos);
    if (edits[i].text) sb_str(&res, edits[i].text);
    pos = edits[i].end;
  }
  sb_add(&res, text + pos, size - pos);
  ne = kept + 1;
  if (res.oom || !res.p) {
    free(res.p);
    goto done;
  }
  *out = res.p;
  *out_len = res.n;
  if (rewritten) *rewritten = found;
  if (bad_count) *bad_count = badn;
  rc = 1;
done:
  for (size_t i = 0; i < ne; i++) free(edits[i].text);
  free(edits);
  return rc;
}

int seven_native_manifest(const char *text, size_t size, char **out, size_t *out_len, int *rewritten, int *bad_count) {
  if (!text || !out || !out_len || size > MAX_TEXT || size < sizeof k_marker - 1 || !memmem(text, size, k_marker, sizeof k_marker - 1))
    return 0;
  scan sc = {.s = text, .size = size};
  int rc = 0;
  if (!setjmp(sc.jb)) rc = rewrite(text, size, out, out_len, rewritten, bad_count, &sc);  // else: not JSON this scanner accepts
  scan_free(&sc);
  return rc == 1;
}

size_t seven_native_json_extent(const char *text, size_t limit) {
  if (!text) return 0;
  size_t at = 0;
  // byte by byte: a two-byte "{}" need not have a third byte behind it
  if (limit >= 3 && text[0] == '\xEF' && text[1] == '\xBB' && text[2] == '\xBF') at = 3;
  while (at < limit && (text[at] == ' ' || text[at] == '\t' || text[at] == '\r' || text[at] == '\n')) at++;
  if (at >= limit || (text[at] != '{' && text[at] != '[')) return 0;
  size_t depth = 0;
  int in_string = 0;
  for (; at < limit && text[at]; at++) {
    const char c = text[at];
    if (in_string) {
      if (c == '\\') {
        if (++at >= limit || !text[at]) return 0;
      } else if (c == '"') {
        in_string = 0;
      }
    } else if (c == '"') {
      in_string = 1;
    } else if (c == '{' || c == '[') {
      depth++;
    } else if ((c == '}' || c == ']') && --depth == 0) {
      return at + 1;
    }
  }
  return 0;
}

// ---- the three points -------------------------------------------------------------------------------------------
#define MAX_BODY (64u << 20)
#define MAX_JSON (16u << 20)
#define LOG_MAX 200

// "PSXR" and "PSAR" as the two games hold the header's first word: Learn & Play after its byte swap (the number
// 0x50535852), the Mac before it (the file's bytes read as a little-endian number)
#define PSXR_SWAPPED 0x50535852u
#define PSAR_SWAPPED 0x50534152u
#define PSXR_RAW 0x52585350u
#define PSAR_RAW 0x52415350u

static int g_on;
static int g_logs_sng, g_logs_json, g_logs_psarc, g_hint;

static void nlog(int *budget, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void nlog(int *budget, const char *fmt, ...) {
  if (__atomic_add_fetch(budget, 1, __ATOMIC_RELAXED) > LOG_MAX) return;
  char buf[300];
  va_list a;
  va_start(a, fmt);
  vsnprintf(buf, sizeof buf, fmt, a);
  va_end(a);
  rsm_log("seven-string: native: %s", buf);
}

// Plain Packages' edits on (a native package is a plain archive); an RS Re-ARMed without rc_edit_is_on is taken to have them
static int plain_packages_on(void) {
  static int (*is_on)(const char *) = (int (*)(const char *))-1;
  if (is_on == (int (*)(const char *))-1) is_on = (int (*)(const char *))dlsym(RTLD_DEFAULT, "rc_edit_is_on");
  return !is_on || is_on("plain_packages");
}

// PN1: the header's magic at the compare with "PSAR"
static void on_psarc(rc_mid_frame *f, void *user) {
  if (!g_on || !seven_on) return;
  const uint32_t magic = (uint32_t)SEVEN_GET(f, "magic");
  const uint32_t stock = magic == PSXR_SWAPPED ? PSAR_SWAPPED : magic == PSXR_RAW ? PSAR_RAW : 0;
  if (!stock) return;
  if (!plain_packages_on()) {
    if (!__atomic_exchange_n(&g_hint, 1, __ATOMIC_RELAXED))
      rsm_log("seven-string: native: a package with the PSXR magic is refused: Plain Packages is off (a native package is plain)");
    return;
  }
  SEVEN_SET(f, "magic", stock);
  SEVEN_SET(f, "stored", stock);
  nlog(&g_logs_psarc, "package with the PSXR magic accepted");
}

// One buffer of the game's memory a thread, kept until that thread's next rewrite (the game parses from it)
static __thread char *g_jbuf;
static __thread size_t g_jcap;

// JM1: the JSON parser's entry, the address of the text pointer
static void on_json(rc_mid_frame *f, void *user) {
  if (!g_on || !seven_on) return;
  uint8_t *cursor = (uint8_t *)(uintptr_t)SEVEN_GET(f, "cursor");
  if (!cursor) return;
  const char *text = rc_mod_ptr(cursor);
  if (!text || (g_jbuf && text >= g_jbuf && text < g_jbuf + g_jcap)) return;  // the text we wrote: nothing in it to do
  const size_t size = seven_native_json_extent(text, MAX_JSON);
  char *out;
  size_t n;
  int rewritten = 0, bad = 0;
  if (!size || !seven_native_manifest(text, size, &out, &n, &rewritten, &bad)) return;
  if (n + 1 > g_jcap) {
    size_t cap = n + 1 > 2 * g_jcap ? n + 1 : 2 * g_jcap;
    char *mem = rc_guest_alloc(cap);  // the old one is the game's, never freed
    if (!mem) {
      free(out);
      return;
    }
    g_jbuf = mem;
    g_jcap = cap;
  }
  memcpy(g_jbuf, out, n + 1);
  free(out);
  const uint64_t g = RC_GUEST(g_jbuf);
  if (rc_guest_ptr_size == 8)
    RC_ST(uint64_t, cursor, g);
  else
    RC_ST(uint32_t, cursor, (uint32_t)g);
  nlog(&g_logs_json, "manifest text of %zu bytes: %d native arrangement(s) rewritten%s", size, rewritten,
       bad ? ", INVALID native arrangement(s) given a six-string tuning" : "");
}

// XI1: the inflated body in its memory stream
static void on_sng(rc_mid_frame *f, void *user) {
  if (!g_on || !seven_on) return;
  uint8_t *buf = (uint8_t *)(uintptr_t)SEVEN_GET(f, "body");
  const size_t size = (uint32_t)SEVEN_GET(f, "size");
  if (!buf || size < 4 || memcmp(buf, "SNXR", 4)) return;
  const unsigned instrument = size > 6 ? buf[5] : 0, strings = size > 6 ? buf[6] : 0;
  char why[200] = "";
  uint8_t *in = size <= MAX_BODY ? malloc(size) : NULL, *out = in ? malloc(size) : NULL;
  seven_native_status st = SEVEN_NATIVE_BAD;
  if (!in || !out) {
    if (size > MAX_BODY)
      snprintf(why, sizeof why, "a body of %zu bytes is too large", size);
    else
      snprintf(why, sizeof why, "no memory");
  } else {
    memcpy(in, buf, size);
    st = seven_native_sng(in, size, out, why, sizeof why);
  }
  if (st == SEVEN_NATIVE_OK) {
    memcpy(buf, out, size);
    nlog(&g_logs_sng, "SNXR body converted in place: %s, %u strings, %zu bytes", instrument ? "bass" : "guitar", strings, size);
  } else {
    nlog(&g_logs_sng, "SNXR body (%zu bytes) not converted (%s): load refused", size, why);
    f->go = SEVEN_CONT(f, "fail");
  }
  free(in);
  free(out);
}

int seven_native_init(void) {
  const int a = seven_mid_optional("seven_native_psarc", on_psarc, "native charts");
  const int b = seven_mid_optional("seven_native_json", on_json, "native charts");
  const int c = seven_mid_optional("seven_native_sng", on_sng, "native charts");
  g_on = a && b && c;
  if (a || b || c) {
    if (g_on)
      rsm_log("seven-string: native N-string charts: on (PSXR packages, ExtendedRange manifests, SNXR charts; needs Plain Packages)");
    else
      rsm_log("seven-string: native N-string charts: off, this RS Re-ARMed lacks some of their mid points");
  }
  return 0;
}
