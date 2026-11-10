// Seven-String Guitar (seven.h): the chords part, the data: a song's chord records over eight strings and the SNG's
// RS7X block. Ported from rs7string (github.com/SomeeName/rs7string, MIT, by SomeeName): src/model/extblock (the RS7X
// parser and its rules, docs/release/RS7X-SPEC.md) and src/features/nslot (the N-slot records, their store and the
// stride redirects; its ARCHITECTURE section 7).
//
// A song's chords live in two vectors of the SNG_Song: chord templates (0x48 bytes, six string columns) and ChordNotes
// records (0x948 bytes, six string slots). For every song the game loads from an SNG this part builds a superset
// record of each (seven_chords.h: T8, C8), the game's record followed by eight-slot arrays, in the game's memory:
//   seven_xb1   the end of SNG_Song_Load, the body still in its memory stream: the bytes after MetaData are the RS7X
//               block (the game never reads them), parsed and checked against the loaded song; then the T8 array
//   seven_pp1   after SNG_Song_PostProcess (the capo rule, the names, the ChordNotes that Fixup adds): the T8 array
//               again and the C8 array
//   seven_sf1   the song's destructor: both arrays go back to the game's allocator
// and the game's own code is handed them where it turns an id into a record (begin + id * 0x48 / 0x948):
//   seven_tp1-tp8, tp11, tp12, seven_cp1, cp2   the T8 / C8 record instead (the game's record when the vector has no
//               array: the instructions run as the game has them)
//   seven_ds1   the double-stop count of the post-processing over the T8's frets of seven strings (one stock string
//               and the seventh is a double stop)
//   seven_bt1, seven_cs1   the tracker's eval keeps strings 6/7 in the unread name of its template copy (the tails)
// The section dimming's template read (TP2, and its edit DM1: frets[s] at +0x48) is here too: the strings part left
// both for this phase.
//
// Slots 6/7 come from the RS7X block (CHDT: fret and finger of each template's extended strings; CHNT: each ChordNotes
// record's mask, slides, vibrato and bends); a package without a block that has a seven-entry tuning (rs7string's
// pre-block format) gives slot 6 from the template's name[31]. Six-string songs and bass get the same records with
// slots 6/7 empty, as in rs7string.
//
// Different from rs7string: the arrays are the game's memory (efd_alloc, freed by the game's string destructor over a
// header that says the block's size: the game's own way to free efd memory by size, as the tuning names part does),
// not the process heap; the block lives with the song's entry (rs7string kept the last eight blocks apart); the TP/CP
// points let the game's instructions run when the vector has no array (rs7string computed the same stock record);
// TP2 without an array places the record so that DM1's eight-slot read is the game's frets; no SEH: the vectors are
// the game's own. 8-string arrangements fill slots 6 and 7 (rs7string with its chord diagrams on, its default); the
// gameplay loops cover seven.
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "seven.h"

#define ENTRIES 64                // songs with records at once (rs7string's kRs7NsEntries)
#define MAX_TRAILING (16u << 20)  // bytes after MetaData that are read at all
#define MAX_ARRAY 0x7fff0000u     // the largest array asked of the game's allocator

// The RS7X format (docs/release/RS7X-SPEC.md)
#define RS7X_HEADER 16
#define RS7X_SECTION 12
#define RS7X_VERSION 1
#define RS7X_CRITICAL 1
#define RS7X_MAX_STRINGS 16
#define RS7X_MAX_FRET 24
#define RS7X_MAX_BENDS 32
#define RS7X_BEND 12  // a bend value: float time, float step, u32
#define RS7X_CELL 9   // a CHNT cell without bends: u32 mask, int8 slideTo, int8 slideUnpitchTo, int16 vibrato, u8 bends

// Note mask bits (the SNG's; rs7string's records.cpp)
enum {
  NOTE_FRET_MUTE = 0x8,
  NOTE_PALM_MUTE = 0x40,
  NOTE_MUTE_NOTE = 0x4000000,
  CN_OPEN = 0x4,
  CN_PALM_MUTE = 0x40,
  CN_SLIDE = 0x800,
  CN_FRET_MUTE = 0x20000,
  CN_UNPITCHED_SLIDE = 0x400000,
  CN_SLIDE_FIXUP = 0x8000000,  // an unpitched slide that SNG_Level_FixupChordNotes makes a pitched one
};

#define NO_CAPO 0xff
#define MAX_OLD_FRET 30  // the old name[31] format's fret range (5 bits, 0 none)
#define MAX_FINGER 4     // the neck's finger table has five entries: thumb..pinky

// MIDI note of each open string at tuning offset 0: the game's six, then B1 and F#1
static const int g_open_midi[SEVEN_CHORD_SLOTS] = {40, 45, 50, 55, 59, 64, 35, 30};

#define PTR(f, name) ((uint8_t *)(uintptr_t)SEVEN_GET(f, name))
#define AT(p, name) ((uint8_t *)(p) + RC_OFF(name))

// ---- the log: at most this many lines of this part a run
static int g_logs = 300;

static void chlog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void chlog(const char *fmt, ...) {
  if (__atomic_sub_fetch(&g_logs, 1, __ATOMIC_RELAXED) < 0) return;
  char buf[400];
  va_list a;
  va_start(a, fmt);
  vsnprintf(buf, sizeof buf, fmt, a);
  va_end(a);
  rsm_log("seven-string: chords: %s", buf);
}

// ---- the RS7X block (rs7string's model/extblock) ----------------------------------------------------------------
enum { RS7X_NONE, RS7X_OK, RS7X_NOT_BLOCK, RS7X_UNSUPPORTED, RS7X_INVALID };

static const char *rs7x_status(int s) {
  static const char *names[] = {"none", "ok", "not an RS7X block", "unsupported", "INVALID"};
  return s >= 0 && s <= RS7X_INVALID ? names[s] : "?";
}

// One extended string inside one ChordNotes record (the game's record's per-string fields)
typedef struct {
  uint32_t mask;
  int8_t slide_to, slide_unpitch_to;
  int16_t vibrato;
  uint8_t bends;        // 0..32
  const uint8_t *bend;  // bends x RS7X_BEND bytes, in the block's copy
} rs7x_tech;

typedef struct {
  int strings;                    // STRS: the instrument's strings (7, 8, ... 16)
  int columns;                    // the extended strings: 6, 7, ... in this order
  int has_templates, has_notes;   // CHDT, CHNT present (absent: every column -1, no techniques)
  int skipped;                    // unknown non-critical sections
  uint32_t templates, notes;      // the song's counts the block was checked against
  int8_t *fret, *finger;          // [template * columns + column]
  rs7x_tech *tech;                // [record * columns + column]
  uint8_t *bytes;                 // the block, copied out of the body
  uint32_t size;
} rs7x;

static void rs7x_free(rs7x *b) {
  if (!b) return;
  free(b->fret);
  free(b->finger);
  free(b->tech);
  free(b->bytes);
  free(b);
}

typedef struct {
  const uint8_t *p;
  size_t n, at;
} cursor;

static int take(cursor *c, void *out, size_t k) {
  if (k > c->n - c->at) return 0;
  memcpy(out, c->p + c->at, k);
  c->at += k;
  return 1;
}

// What a block is checked against, the block being filled and where a failure's reason goes
typedef struct {
  uint32_t templates, notes, tuning;
  rs7x *out;
  char *why;
  size_t len;
} parse_ctx;

static int fail(const parse_ctx *x, int status, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static int fail(const parse_ctx *x, int status, const char *fmt, ...) {
  if (x->why && x->len) {
    va_list a;
    va_start(a, fmt);
    vsnprintf(x->why, x->len, fmt, a);
    va_end(a);
  }
  return status;
}

// STRS: instrument 0 (guitar), 7..16 strings as many as the MetaData tuning has, columns 6, 7, ... in order
static int parse_strs(const parse_ctx *x, cursor *c) {
  uint8_t h[4];
  if (!take(c, h, 4)) return fail(x, RS7X_INVALID, "STRS too short");
  rs7x *b = x->out;
  b->strings = h[1];
  if (h[0] != 0) return fail(x, RS7X_UNSUPPORTED, "instrument %d", h[0]);
  if (b->strings < 7 || b->strings > RS7X_MAX_STRINGS) return fail(x, RS7X_INVALID, "%d strings", b->strings);
  if ((uint32_t)b->strings != x->tuning)
    return fail(x, RS7X_INVALID, "%d strings but %u tuning entries", b->strings, x->tuning);
  if (h[2] != b->strings - 6) return fail(x, RS7X_INVALID, "%u columns for %d strings", h[2], b->strings);
  for (int i = 0; i < h[2]; i++) {
    uint8_t s;
    if (!take(c, &s, 1)) return fail(x, RS7X_INVALID, "STRS too short");
    if (s != 6 + i) return fail(x, RS7X_INVALID, "column %d is string %u", i, s);
  }
  b->columns = h[2];
  return RS7X_OK;
}

// CHDT: one row per stock template, {int8 fret, int8 finger} per column; fret -1 or 0..24, finger -1..4, no finger
// without a fret
static int parse_chdt(const parse_ctx *x, cursor *c) {
  rs7x *b = x->out;
  uint32_t rows;
  if (!take(c, &rows, 4)) return fail(x, RS7X_INVALID, "CHDT too short");
  if (rows != x->templates) return fail(x, RS7X_INVALID, "CHDT has %u templates, the SNG %u", rows, x->templates);
  const size_t row = (size_t)b->columns * 2, left = c->n - c->at;
  if (!row || left % row || rows != left / row) return fail(x, RS7X_INVALID, "CHDT size");
  const size_t cells = (size_t)rows * (size_t)b->columns;
  free(b->fret);
  free(b->finger);
  b->fret = malloc(cells ? cells : 1);
  b->finger = malloc(cells ? cells : 1);
  if (!b->fret || !b->finger) return fail(x, RS7X_INVALID, "CHDT: no memory");
  for (size_t i = 0; i < cells; i++) {
    int8_t fg[2];
    take(c, fg, 2);
    if (!(fg[0] == -1 || (fg[0] >= 0 && fg[0] <= RS7X_MAX_FRET)) || fg[1] < -1 || fg[1] > MAX_FINGER ||
        (fg[0] == -1 && fg[1] != -1))
      return fail(x, RS7X_INVALID, "CHDT template %zu: fret %d finger %d", i / (size_t)b->columns, fg[0], fg[1]);
    b->fret[i] = fg[0];
    b->finger[i] = fg[1];
  }
  return RS7X_OK;
}

static int slide_ok(int8_t v) { return v == -1 || (v >= 0 && v <= RS7X_MAX_FRET); }

// CHNT: one entry per stock ChordNotes record and column: the record's per-string fields and its bends
static int parse_chnt(const parse_ctx *x, cursor *c) {
  rs7x *b = x->out;
  uint32_t rows;
  if (!take(c, &rows, 4)) return fail(x, RS7X_INVALID, "CHNT too short");
  if (rows != x->notes) return fail(x, RS7X_INVALID, "CHNT has %u records, the SNG %u", rows, x->notes);
  const size_t columns = (size_t)b->columns;
  if (!columns || rows > (c->n - c->at) / (columns * RS7X_CELL)) return fail(x, RS7X_INVALID, "CHNT size");
  const size_t cells = (size_t)rows * columns;
  free(b->tech);
  if (!(b->tech = calloc(cells ? cells : 1, sizeof *b->tech))) return fail(x, RS7X_INVALID, "CHNT: no memory");
  for (size_t i = 0; i < cells; i++) {
    rs7x_tech *t = &b->tech[i];
    uint8_t cell[RS7X_CELL];
    if (!take(c, cell, sizeof cell)) return fail(x, RS7X_INVALID, "CHNT too short");
    memcpy(&t->mask, cell, 4);
    t->slide_to = (int8_t)cell[4];
    t->slide_unpitch_to = (int8_t)cell[5];
    memcpy(&t->vibrato, cell + 6, 2);
    t->bends = cell[8];
    if (t->bends > RS7X_MAX_BENDS) return fail(x, RS7X_INVALID, "CHNT %u bends", t->bends);
    if (!slide_ok(t->slide_to) || !slide_ok(t->slide_unpitch_to))
      return fail(x, RS7X_INVALID, "CHNT record %zu: slide %d / %d", i / columns, t->slide_to, t->slide_unpitch_to);
    const size_t bytes = (size_t)t->bends * RS7X_BEND;
    if (bytes > c->n - c->at) return fail(x, RS7X_INVALID, "CHNT too short");
    t->bend = c->p + c->at;
    c->at += bytes;
  }
  return RS7X_OK;
}

static uint32_t id_at(const uint8_t *p) {
  uint32_t v;
  memcpy(&v, p, 4);
  return v;
}

// Section s at *at: its header and order checked, a known version-1 section parsed, an unknown one skipped
static int parse_section(const parse_ctx *x, const uint8_t *p, uint32_t size, uint32_t first, uint32_t s, size_t *at) {
  if (size - *at < RS7X_SECTION) return fail(x, RS7X_INVALID, "section %u: header past the end", s);
  const uint8_t *id = p + *at;
  uint16_t ver, flags;
  uint32_t n;
  memcpy(&ver, id + 4, 2);
  memcpy(&flags, id + 6, 2);
  memcpy(&n, id + 8, 4);
  *at += RS7X_SECTION;
  if (n > size - *at) return fail(x, RS7X_INVALID, "section %u: %u bytes past the end", s, n);
  cursor c = {p + *at, n, 0};
  *at += n;
  const int strs = !memcmp(id, "STRS", 4), chdt = !memcmp(id, "CHDT", 4), chnt = !memcmp(id, "CHNT", 4);
  if (s == 0 && !strs) return fail(x, RS7X_INVALID, "the first section is not STRS");
  for (size_t q = first; q < (size_t)(id - p); q += RS7X_SECTION + (size_t)id_at(p + q + 8))  // each at most once
    if (id_at(p + q) == id_at(id)) return fail(x, RS7X_INVALID, "section %.4s twice", (const char *)id);
  if ((!strs && !chdt && !chnt) || ver != RS7X_VERSION) {
    if (flags & RS7X_CRITICAL) return fail(x, RS7X_UNSUPPORTED, "critical section %.4s v%u", (const char *)id, ver);
    x->out->skipped++;
    return RS7X_OK;
  }
  int r;
  if (strs) {
    r = parse_strs(x, &c);
  } else if (chdt) {
    x->out->has_templates = 1;
    r = parse_chdt(x, &c);
  } else {
    x->out->has_notes = 1;
    r = parse_chnt(x, &c);
  }
  if (r != RS7X_OK) return r;
  if (c.at != c.n) return fail(x, RS7X_INVALID, "section %.4s: %zu unread bytes", (const char *)id, c.n - c.at);
  return RS7X_OK;
}

// The bytes after MetaData (p, n: the block's own copy, kept by `out`) checked against the loaded song's counts:
// RS7X_OK with *out filled, else why says why
static int rs7x_parse(const uint8_t *p, size_t n, uint32_t templates, uint32_t notes, uint32_t tuning, rs7x *out,
                      char *why, size_t len) {
  const parse_ctx x = {templates, notes, tuning, out, why, len};
  if (why && len) why[0] = 0;
  if (n == 0) return RS7X_NONE;
  if (n < 4 || memcmp(p, "RS7X", 4)) return fail(&x, RS7X_NOT_BLOCK, "no RS7X magic");
  if (n < RS7X_HEADER) return fail(&x, RS7X_INVALID, "%zu bytes, shorter than the header", n);
  uint16_t version, header;
  uint32_t size, count;
  memcpy(&version, p + 4, 2);
  memcpy(&header, p + 6, 2);
  memcpy(&size, p + 8, 4);
  memcpy(&count, p + 12, 4);
  if (version != RS7X_VERSION) return fail(&x, RS7X_UNSUPPORTED, "version %u", version);
  if (size != n || header < RS7X_HEADER || header > size)
    return fail(&x, RS7X_INVALID, "header size %u, block size %u, %zu bytes after MetaData", header, size, n);
  out->templates = templates;
  out->notes = notes;
  size_t at = header;
  for (uint32_t s = 0; s < count; s++) {
    const int r = parse_section(&x, p, size, header, s, &at);
    if (r != RS7X_OK) return r;
  }
  if (at != size) return fail(&x, RS7X_INVALID, "%zu bytes after the last section", (size_t)size - at);
  if (!out->strings) return fail(&x, RS7X_INVALID, "no STRS section");
  return RS7X_OK;
}

// The block's column of string s, -1 when s is not one of its extended strings
static int rs7x_column(const rs7x *b, int s) { return b && s >= 6 && s - 6 < b->columns ? s - 6 : -1; }

// Extended string s of template t: 1 with its fret and finger when the block has it in the chord
static int rs7x_template(const rs7x *b, uint32_t t, int s, int *fret, int *finger) {
  const int c = rs7x_column(b, s);
  if (c < 0 || !b->has_templates || t >= b->templates) return 0;
  const size_t i = (size_t)t * (size_t)b->columns + (size_t)c;
  if (b->fret[i] < 0) return 0;
  *fret = b->fret[i];
  *finger = b->finger[i];
  return 1;
}

// ---- the records (rs7string's records.cpp) -------------------------------------------------------------------------
// The old format (packages before the block): name[31] = ((finger + 1) << 5) | (fret + 1), read only when the name ends
// before it. 1 with string 6's fret and finger.
static int old_name_column(const uint8_t *tpl, int *fret, int *finger) {
  const uint8_t *name = AT(tpl, sng_template_name);
  if (!memchr(name, 0, 31)) return 0;  // a 31-character name: name[31] is text
  const uint8_t b = name[31];
  if ((b & 0x1f) == 0) return 0;
  *fret = (b & 0x1f) - 1;
  *finger = (b >> 5) - 1;
  return 1;
}

// The song as the records need it
typedef struct {
  uint8_t *tpl, *cn, *levels;  // the vectors' first entries (host)
  uint32_t templates, notes, nlevels, tuning_entries;
  int16_t tuning[RS7X_MAX_STRINGS];
  int n;  // tuning entries kept (at most 16)
  uint8_t capo;
} song_view;

// A std::vector of the game at v: its first entry (host) and its size in bytes; 0 for an empty or broken one
static size_t vec(const uint8_t *v, uint8_t **begin) {
  uint8_t *b = rc_mod_ptr(v + RC_OFF(std_vector_begin)), *e = rc_mod_ptr(v + RC_OFF(std_vector_end));
  *begin = b;
  return b && e > b ? (size_t)(e - b) : 0;
}

// 0 for vectors that are not whole records, or arrays too large for the game's allocator
static int read_song(const uint8_t *song, song_view *v) {
  memset(v, 0, sizeof *v);
  uint8_t *tuning;
  const size_t t = vec(AT(song, sng_song_templates), &v->tpl), c = vec(AT(song, sng_song_chord_notes), &v->cn);
  const size_t l = vec(AT(song, sng_song_levels), &v->levels), u = vec(AT(song, sng_song_tuning), &tuning);
  const size_t tsz = (size_t)RC_OFF(sng_template_size), csz = (size_t)RC_OFF(sng_chord_notes_size);
  if (t % tsz || c % csz || l % (size_t)RC_OFF(sng_level_size) || u % 2) return 0;
  if (t / tsz > MAX_ARRAY / (size_t)RC_OFF(seven_t8_size) || c / csz > MAX_ARRAY / (size_t)RC_OFF(seven_c8_size))
    return 0;
  v->templates = (uint32_t)(t / tsz);
  v->notes = (uint32_t)(c / csz);
  v->nlevels = (uint32_t)(l / (size_t)RC_OFF(sng_level_size));
  v->tuning_entries = (uint32_t)(u / 2);
  v->n = v->tuning_entries > RS7X_MAX_STRINGS ? RS7X_MAX_STRINGS : (int)v->tuning_entries;
  for (int i = 0; i < v->n; i++) v->tuning[i] = RC_LD(int16_t, tuning + 2 * i);
  v->capo = RC_LD(uint8_t, AT(song, sng_song_capo));
  return 1;
}

// Slots 6 and 7 of template t: the block's columns, else (a 7-string arrangement without one) name[31]. Returns 1
// when name[31] gave one.
static int ext_columns(const rs7x *b, const uint8_t *tpl, uint32_t t, int n, int fret[2], int finger[2]) {
  fret[0] = fret[1] = finger[0] = finger[1] = -1;
  if (b) {
    for (int i = 0; i < 2; i++) rs7x_template(b, t, 6 + i, &fret[i], &finger[i]);
    return 0;
  }
  return n == 7 && old_name_column(tpl, &fret[0], &finger[0]);
}

// One T8 from the game's template (rs7string's BuildT8). The extended strings' MIDI note is the authored fret's
// (open note + tuning + fret, as the six stock notes[]), and their fret then follows the post-processing's capo rule
// (a fret equal to the capo becomes 0). A 7- or 8-string arrangement fills its slots (both for 8: the diagrams draw the
// eighth column); more strings fill none.
static void build_t8(const uint8_t *tpl, const int fret[2], const int finger[2], const song_view *v, uint8_t *out) {
  const size_t t8 = (size_t)RC_OFF(seven_t8_size), tsz = (size_t)RC_OFF(sng_template_size);
  int8_t *frets = (int8_t *)AT(out, seven_t8_frets), *fingers = (int8_t *)AT(out, seven_t8_fingers);
  uint8_t *midi = AT(out, seven_t8_midi);
  memset(out, 0, t8);
  memcpy(out, tpl, tsz);
  for (int s = 0; s < SEVEN_CHORD_SLOTS; s++) {
    frets[s] = s < 6 ? (int8_t)AT(tpl, sng_template_frets)[s] : -1;
    fingers[s] = s < 6 ? (int8_t)AT(tpl, sng_template_fingers)[s] : -1;
    RC_ST(int32_t, midi + 4 * s, s < 6 ? RC_LD(int32_t, AT(tpl, sng_template_midi) + 4 * s) : -1);
  }
  const int n = v->n;
  for (int i = 0; i < 2; i++) {
    const int s = 6 + i;
    int f = fret[i];
    if (f < 0 || f > MAX_OLD_FRET || s >= n || n > SEVEN_CHORD_SLOTS) continue;
    RC_ST(int32_t, midi + 4 * s, g_open_midi[s] + v->tuning[s] + f);
    if ((uint8_t)f == v->capo) f = 0;
    frets[s] = (int8_t)f;
    fingers[s] = (int8_t)(finger[i] >= -1 && finger[i] <= MAX_FINGER ? finger[i] : -1);
  }
  *AT(out, seven_t8_slots) = (uint8_t)(n > 6 && n <= SEVEN_CHORD_SLOTS ? n : 6);
  uint8_t used = 0;
  for (int s = 0; s < SEVEN_CHORD_SLOTS; s++)
    if (frets[s] != -1) used |= (uint8_t)(1u << s);
  *AT(out, seven_t8_used) = used;
}

// The technique of one extended string in a C8
typedef struct {
  uint32_t mask;
  int8_t slide_to, slide_unpitch_to;
  int16_t vibrato;
  int bends;
  const uint8_t *bend;  // bends x RS7X_BEND bytes (the game's BendData32 value layout)
} tech;

// One C8 from the game's ChordNotes record (rs7string's BuildC8); tech[0..1] = strings 6 and 7 (NULL: none)
static void build_c8(const uint8_t *cn, const tech *t6, const tech *t7, uint8_t *out) {
  const size_t bend = (size_t)RC_OFF(sng_bend_data_size);
  uint8_t *mask = AT(out, seven_c8_mask), *vib = AT(out, seven_c8_vibrato);
  int8_t *slide = (int8_t *)AT(out, seven_c8_slide_to), *unp = (int8_t *)AT(out, seven_c8_slide_unpitch_to);
  memset(out, 0, (size_t)RC_OFF(seven_c8_size));
  memcpy(out, cn, (size_t)RC_OFF(sng_chord_notes_size));
  for (int s = 0; s < SEVEN_CHORD_SLOTS; s++) slide[s] = unp[s] = -1;
  for (int s = 0; s < 6; s++) {
    RC_ST(uint32_t, mask + 4 * s, RC_LD(uint32_t, AT(cn, sng_chord_notes_mask) + 4 * s));
    slide[s] = (int8_t)AT(cn, sng_chord_notes_slide_to)[s];
    unp[s] = (int8_t)AT(cn, sng_chord_notes_slide_unpitch_to)[s];
    RC_ST(int16_t, vib + 2 * s, RC_LD(int16_t, AT(cn, sng_chord_notes_vibrato) + 2 * s));
    memcpy(AT(out, seven_c8_bends) + (size_t)s * bend, AT(cn, sng_chord_notes_bends) + (size_t)s * bend, bend);
  }
  const tech *ts[2] = {t6, t7};
  for (int i = 0; i < 2; i++) {
    const tech *t = ts[i];
    if (!t) continue;
    const int s = 6 + i;
    uint32_t m = t->mask;
    int8_t to = t->slide_to, un = t->slide_unpitch_to;
    // SNG_Level_FixupChordNotes: an unpitched slide flagged CN_SLIDE_FIXUP without a pitched one becomes a pitched
    // slide (the game does this to strings 0-5 of its own record)
    if ((m & CN_SLIDE_FIXUP) && un != -1 && to == -1) {
      to = un;
      un = -1;
      m = (m & ~(uint32_t)CN_UNPITCHED_SLIDE) | CN_SLIDE;
    }
    RC_ST(uint32_t, mask + 4 * s, m);
    slide[s] = to;
    unp[s] = un;
    RC_ST(int16_t, vib + 2 * s, t->vibrato);
    uint8_t *b = AT(out, seven_c8_bends) + (size_t)s * bend;
    const int count = t->bends < 0 ? 0 : t->bends > RS7X_MAX_BENDS ? RS7X_MAX_BENDS : t->bends;
    if (count && t->bend) memcpy(b, t->bend, (size_t)count * RS7X_BEND);
    RC_ST(uint32_t, b + RC_OFF(sng_bend_data_used), (uint32_t)count);
  }
}

// The technique of an extended string in a ChordNotes record that Fixup added for a repeated chord (rs7string's
// SynthMask, after 0x55D490): per sounding string palm mute -> CN_PALM_MUTE, fret-hand mute -> CN_FRET_MUTE, a mute
// note -> the same bit, an open string (fret 0, or the capo's fret) -> CN_OPEN
static uint32_t synth_mask(uint32_t note, int fret, uint8_t capo) {
  if (fret < 0) return 0;
  uint32_t m = 0;
  if (note & NOTE_PALM_MUTE) m |= CN_PALM_MUTE;
  if (note & NOTE_FRET_MUTE) m |= CN_FRET_MUTE;
  if (note & NOTE_MUTE_NOTE) m |= NOTE_MUTE_NOTE;
  if ((fret == capo && capo != NO_CAPO) || fret == 0) m |= CN_OPEN;
  return m;
}

// The eval's tails (rs7string's ClearEvalTails / WriteEvalTails)
void seven_chords_eval_clear(uint8_t *eval) {
  if (!eval) return;
  RC_ST(int32_t, AT(eval, seven_eval_tail_midi), -1);
  RC_ST(int32_t, AT(eval, seven_eval_tail_midi) + 4, -1);
  memset(AT(eval, seven_eval_tail_mask), 0, 8);
  memset(AT(eval, seven_eval_tail_fret), 0xff, 2);
  memset(AT(eval, seven_eval_tail_finger), 0xff, 2);
  memset(AT(eval, seven_eval_tail_slide), 0xff, 2);
  memset(AT(eval, seven_eval_tail_unpitch), 0xff, 2);
  memset(AT(eval, seven_eval_tail_vibrato), 0, 8);  // vibrato[2] and the copy's last name bytes
}

void seven_chords_eval_tails(uint8_t *eval, const uint8_t *t8, const uint8_t *c8) {
  if (!eval) return;
  seven_chords_eval_clear(eval);
  if (t8) {
    memcpy(AT(eval, seven_eval_tail_midi), AT(t8, seven_t8_midi) + 4 * 6, 8);
    memcpy(AT(eval, seven_eval_tail_fret), AT(t8, seven_t8_frets) + 6, 2);
    memcpy(AT(eval, seven_eval_tail_finger), AT(t8, seven_t8_fingers) + 6, 2);
  }
  if (c8) {
    memcpy(AT(eval, seven_eval_tail_mask), AT(c8, seven_c8_mask) + 4 * 6, 8);
    memcpy(AT(eval, seven_eval_tail_slide), AT(c8, seven_c8_slide_to) + 6, 2);
    memcpy(AT(eval, seven_eval_tail_unpitch), AT(c8, seven_c8_slide_unpitch_to) + 6, 2);
    memcpy(AT(eval, seven_eval_tail_vibrato), AT(c8, seven_c8_vibrato) + 2 * 6, 4);
  }
}

// ---- the store: the songs with records ----------------------------------------------------------------------------
// Readers (the stride points, any thread) compare an entry's published vector begin, then read its array: lock-free.
// Writers (the load and the post-processing on the loader thread, the free on any) hold g_lock, publish a begin last
// and unpublish it first. The game's allocator is called outside the lock.
typedef struct {
  const void *song;    // SNG_Song (host), NULL: a free entry
  uint8_t *tpl_begin;  // the game's template vector the T8 array stands for, NULL: none published
  uint32_t tpl_count;
  uint8_t *t8;
  uint8_t *cn_begin;  // the game's ChordNotes vector the C8 array stands for (after the post-processing)
  uint32_t cn_count;
  uint8_t *c8;
  uint32_t loaded_cn;  // ChordNotes at load: Fixup adds records past them in the post-processing
  rs7x *block;
} entry;

static entry g_songs[ENTRIES];
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

// What a writer takes out of an entry, given back after the lock
typedef struct {
  uint8_t *t8, *c8;
  uint32_t t8_count, c8_count;
  rs7x *block;
} spent;

static uint8_t *game_alloc(size_t n) { return n ? RC_HOST(OV_GUEST(efd_alloc, n, 0)) : NULL; }

static void put_ptr(uint8_t *field, const void *host) {
  if (rc_guest_ptr_size == 8)
    RC_ST(uint64_t, field, RC_GUEST(host));
  else
    RC_ST(uint32_t, field, (uint32_t)RC_GUEST(host));
}

// Gives n bytes at p (from game_alloc) back to the game's allocator: its string destructor frees a string's text by
// the size between its start and its buffer's end (efd_str_inline), which a header in the game's memory says
static void game_free(uint8_t *p, size_t n) {
  static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
  static uint8_t *header;
  if (!p || !n) return;
  pthread_mutex_lock(&lock);
  if (!header) header = rc_guest_alloc((size_t)RC_OFF(efd_str_inline) + (size_t)rc_guest_ptr_size);
  if (header) {
    put_ptr(header, p);
    put_ptr(header + RC_OFF(efd_str_inline), p + n);
    OV_GUEST(efd_str_destroy, (uint64_t)(uintptr_t)header);
  }
  pthread_mutex_unlock(&lock);
}

static void give_back(spent *s) {
  game_free(s->t8, (size_t)s->t8_count * (size_t)RC_OFF(seven_t8_size));
  game_free(s->c8, (size_t)s->c8_count * (size_t)RC_OFF(seven_c8_size));
  rs7x_free(s->block);
  memset(s, 0, sizeof *s);
}

// Lock held: the entry's arrays unpublished and moved to *out (the song and its block stay)
static void drop_arrays(entry *e, spent *out) {
  __atomic_store_n(&e->tpl_begin, NULL, __ATOMIC_RELEASE);
  __atomic_store_n(&e->cn_begin, NULL, __ATOMIC_RELEASE);
  out->t8 = e->t8;
  out->t8_count = e->tpl_count;
  out->c8 = e->c8;
  out->c8_count = e->cn_count;
  e->t8 = e->c8 = NULL;
  e->tpl_count = e->cn_count = 0;
}

// Lock held: the entry freed, everything it had moved to *out
static void clear_entry(entry *e, spent *out) {
  drop_arrays(e, out);
  out->block = e->block;
  e->block = NULL;
  e->loaded_cn = 0;
  e->song = NULL;
}

static entry *find(const void *song) {
  for (int i = 0; i < ENTRIES; i++)
    if (g_songs[i].song == song) return &g_songs[i];
  return NULL;
}

// Lock held: the T8 array (n templates at out) built from the song's templates; the count with an extended string
static int build_templates(const song_view *v, const rs7x *b, uint8_t *out, int *by_name) {
  const size_t t8 = (size_t)RC_OFF(seven_t8_size), tsz = (size_t)RC_OFF(sng_template_size);
  int ext = 0;
  *by_name = 0;
  for (uint32_t t = 0; t < v->templates; t++) {
    const uint8_t *tpl = v->tpl + (size_t)t * tsz;
    uint8_t *rec = out + (size_t)t * t8;
    int fret[2], finger[2];
    *by_name += ext_columns(b, tpl, t, v->n, fret, finger);
    build_t8(tpl, fret, finger, v, rec);
    if (*AT(rec, seven_t8_used) & 0xc0) ext++;  // strings 6, 7
  }
  return ext;
}

// The note that first uses each ChordNotes record Fixup added (index loaded..notes-1), in level and note order: its
// mask and chord, as rs7string's FindUser (one pass here). users[r - loaded] = {mask, chord + 1} (0: none).
static void find_users(const song_view *v, uint32_t loaded, uint32_t (*users)[2]) {
  const size_t lsz = (size_t)RC_OFF(sng_level_size), nsz = (size_t)RC_OFF(sng_note_size);
  for (uint32_t l = 0; l < v->nlevels; l++) {
    uint8_t *notes;
    const size_t bytes = vec(v->levels + (size_t)l * lsz + RC_OFF(sng_level_notes), &notes);
    for (size_t at = 0; at + nsz <= bytes; at += nsz) {
      const uint8_t *note = notes + at;
      const int32_t chord = RC_LD(int32_t, AT(note, sng_note_chord));
      const int32_t r = RC_LD(int32_t, AT(note, sng_note_chord_notes));
      if (chord < 0 || r < 0 || (uint32_t)r < loaded || (uint32_t)r >= v->notes || users[r - loaded][1]) continue;
      users[r - loaded][0] = RC_LD(uint32_t, AT(note, sng_note_mask));
      users[r - loaded][1] = (uint32_t)chord + 1;
    }
  }
}

// Lock held: the C8 array (v->notes records at out). Records loaded with the song take strings 6/7 from the block's
// CHNT; the ones Fixup added, from the note that uses them and its chord's T8 (t8, the song's new array).
static void build_notes(const song_view *v, const entry *e, const uint8_t *t8, uint8_t *out, int *from_block,
                        int *synth) {
  const size_t csz = (size_t)RC_OFF(sng_chord_notes_size), c8 = (size_t)RC_OFF(seven_c8_size);
  const uint32_t loaded = e->loaded_cn < v->notes ? e->loaded_cn : v->notes;
  uint32_t(*users)[2] = v->notes > loaded ? calloc(v->notes - loaded, sizeof *users) : NULL;
  if (users) find_users(v, loaded, users);
  const rs7x *b = e->block;
  *from_block = *synth = 0;
  for (uint32_t r = 0; r < v->notes; r++) {
    tech t[2];
    const tech *tp[2] = {NULL, NULL};
    memset(t, 0, sizeof t);
    for (int i = 0; i < 2; i++) {
      const int s = 6 + i, c = rs7x_column(b, s);
      if (r < loaded && b && b->has_notes && c >= 0 && r < b->notes) {
        const rs7x_tech *x = &b->tech[(size_t)r * (size_t)b->columns + (size_t)c];
        t[i] = (tech){x->mask, x->slide_to, x->slide_unpitch_to, x->vibrato, x->bends, x->bend};
        tp[i] = &t[i];
        *from_block += x->mask != 0;
      } else if (r >= loaded && users && users[r - loaded][1]) {
        const uint32_t chord = users[r - loaded][1] - 1;
        if (chord >= v->templates) continue;
        const int fret = (int8_t)AT(t8 + (size_t)chord * (size_t)RC_OFF(seven_t8_size), seven_t8_frets)[s];
        t[i].mask = synth_mask(users[r - loaded][0], fret, v->capo);
        t[i].slide_to = t[i].slide_unpitch_to = -1;
        if (t[i].mask) {
          tp[i] = &t[i];
          ++*synth;
        }
      }
    }
    build_c8(v->cn + (size_t)r * csz, tp[0], tp[1], out + (size_t)r * c8);
  }
  free(users);
}

// The block of a song being loaded: the bytes from the stream's position to its size, copied and parsed. NULL for
// none (the log line says why).
static rs7x *read_block(const void *song, const song_view *v, const uint8_t *body, uint32_t pos, uint32_t size) {
  if (!body || pos > size || size - pos > MAX_TRAILING) {
    chlog("SNG %p: stream position %u, size %u: not checked", song, pos, size);
    return NULL;
  }
  const uint32_t n = size - pos;
  rs7x *b = calloc(1, sizeof *b);
  if (b && n && !(b->bytes = malloc(n))) {
    free(b);
    b = NULL;
  }
  if (!b) {
    chlog("SNG %p: no memory for its %u bytes after MetaData", song, n);
    return NULL;
  }
  if (n) memcpy(b->bytes, body + pos, n);
  b->size = n;
  char why[160];
  const int st = rs7x_parse(b->bytes, n, v->templates, v->notes, v->tuning_entries, b, why, sizeof why);
  if (st == RS7X_OK) {
    int ext6 = 0, techs = 0;
    for (uint32_t t = 0; t < v->templates; t++) {
      int f, g;
      ext6 += rs7x_template(b, t, 6, &f, &g);
    }
    for (size_t i = 0; b->has_notes && i < (size_t)v->notes * (size_t)b->columns; i++) {
      const rs7x_tech *x = &b->tech[i];
      techs += x->mask || x->slide_to != -1 || x->slide_unpitch_to != -1 || x->vibrato || x->bends;
    }
    chlog("SNG %p: body %u bytes, stock sections end at %u, RS7X v%u block %u bytes: guitar, %d strings, %d extended "
          "column(s), CHDT %s (%u templates, %d with a string-6 column), CHNT %s (%u records, %d extended technique "
          "entries), %d unknown section(s) skipped -> the chord records read the block",
          song, size, pos, RS7X_VERSION, n, b->strings, b->columns, b->has_templates ? "yes" : "no", v->templates, ext6,
          b->has_notes ? "yes" : "no", v->notes, techs, b->skipped);
    return b;
  }
  int old = 0;
  for (uint32_t t = 0; t < v->templates; t++) {
    int f, g;
    old += old_name_column(v->tpl + (size_t)t * (size_t)RC_OFF(sng_template_size), &f, &g);
  }
  char what[220];
  if (st == RS7X_NONE)
    snprintf(what, sizeof what, "no RS7X block");
  else if (st == RS7X_NOT_BLOCK)
    snprintf(what, sizeof what, "trailing bytes, no RS7X magic: ignored");
  else
    snprintf(what, sizeof what, "RS7X block %s (%s): ignored", rs7x_status(st), why);
  chlog("SNG %p: body %u bytes, stock sections end at %u, %u bytes after MetaData: %s -> name[31] fallback (%d of %u "
        "templates carry a string-6 column there; %u tuning entries)",
        song, size, pos, n, what, old, v->templates, v->tuning_entries);
  rs7x_free(b);
  return NULL;
}

// XB1: the song's block and its T8 array
static void song_loaded(const void *song, const uint8_t *body, uint32_t pos, uint32_t size) {
  spent old = {0};
  song_view v;
  if (!read_song(song, &v)) {
    pthread_mutex_lock(&g_lock);
    entry *e = find(song);
    if (e) clear_entry(e, &old);
    pthread_mutex_unlock(&g_lock);
    give_back(&old);
    chlog("SNG %p: its chord vectors are not whole records or too large: no chord records", song);
    return;
  }
  rs7x *block = read_block(song, &v, body, pos, size);
  uint8_t *t8 = game_alloc((size_t)v.templates * (size_t)RC_OFF(seven_t8_size));
  pthread_mutex_lock(&g_lock);
  entry *e = find(song);
  if (e) clear_entry(e, &old);  // a song object the game reuses
  if (!e) e = find(NULL);
  if (!e || (v.templates && !t8)) {
    pthread_mutex_unlock(&g_lock);
    give_back(&old);
    spent unused = {t8, NULL, v.templates, 0, block};
    give_back(&unused);
    chlog("SNG %p: %s: its chords read the game's six-string records", song,
          e ? "no game memory for its T8 array" : "no free entry (every one holds a song)");
    return;
  }
  e->song = song;
  e->block = block;
  e->loaded_cn = v.notes;
  int by_name;
  const int ext = build_templates(&v, block, t8, &by_name);
  e->t8 = t8;
  e->tpl_count = v.templates;
  __atomic_store_n(&e->tpl_begin, v.tpl, __ATOMIC_RELEASE);
  pthread_mutex_unlock(&g_lock);
  give_back(&old);
  chlog("SNG %p: %u templates -> T8 %p (%d with a 7th/8th-string column, from %s), %u tuning entries, capo %u, %u "
        "ChordNotes",
        song, v.templates, (void *)t8, ext, block ? "the RS7X block" : by_name ? "name[31] (old format)" : "nothing",
        v.tuning_entries, v.capo, v.notes);
}

// PP1: the T8 array again (the post-processing changed names and capo frets, and may have moved the vector) and the
// C8 array (Fixup added ChordNotes records)
static void song_post_processed(const void *song) {
  spent old = {0};
  song_view v;
  if (!read_song(song, &v)) {
    pthread_mutex_lock(&g_lock);
    entry *e = find(song);
    if (e) drop_arrays(e, &old);
    pthread_mutex_unlock(&g_lock);
    give_back(&old);
    chlog("SNG %p post-processed: its chord vectors are not whole records or too large: no chord records", song);
    return;
  }
  uint8_t *t8 = game_alloc((size_t)v.templates * (size_t)RC_OFF(seven_t8_size));
  uint8_t *c8 = game_alloc((size_t)v.notes * (size_t)RC_OFF(seven_c8_size));
  pthread_mutex_lock(&g_lock);
  entry *e = find(song);
  if (!e || (v.templates && !t8) || (v.notes && !c8)) {
    if (e) drop_arrays(e, &old);
    pthread_mutex_unlock(&g_lock);
    give_back(&old);
    spent unused = {t8, c8, v.templates, v.notes, NULL};
    give_back(&unused);
    if (e) chlog("SNG %p post-processed: no game memory for its records: its chords read the game's records", song);
    return;
  }
  drop_arrays(e, &old);
  int by_name, from_block, synth;
  const int ext = build_templates(&v, e->block, t8, &by_name);
  build_notes(&v, e, t8, c8, &from_block, &synth);
  e->t8 = t8;
  e->tpl_count = v.templates;
  e->c8 = c8;
  e->cn_count = v.notes;
  __atomic_store_n(&e->tpl_begin, v.tpl, __ATOMIC_RELEASE);
  if (v.notes) __atomic_store_n(&e->cn_begin, v.cn, __ATOMIC_RELEASE);
  const uint32_t loaded = e->loaded_cn;
  pthread_mutex_unlock(&g_lock);
  give_back(&old);
  chlog("SNG %p post-processed: T8 %p (%d with an extended column), %u ChordNotes (%u loaded, %u added by Fixup) -> C8 "
        "%p (%d extended technique entries from the block, %d synthesized)",
        song, (void *)t8, ext, v.notes, loaded, v.notes > loaded ? v.notes - loaded : 0, (void *)c8, from_block, synth);
}

// SF1: the song's records go back to the game
static void song_freed(const void *song) {
  spent old = {0};
  pthread_mutex_lock(&g_lock);
  entry *e = find(song);
  if (e) clear_entry(e, &old);
  pthread_mutex_unlock(&g_lock);
  if (e) chlog("SNG %p freed (had %u T8, %u C8)", song, old.t8_count, old.c8_count);
  give_back(&old);
}

// ---- what the others read -------------------------------------------------------------------------------------------
uint8_t *seven_chords_template(const void *templates, uint32_t id) {
  if (!templates) return NULL;
  for (int i = 0; i < ENTRIES; i++) {
    entry *e = &g_songs[i];
    if (__atomic_load_n(&e->tpl_begin, __ATOMIC_ACQUIRE) == templates && id < e->tpl_count)
      return e->t8 + (size_t)id * (size_t)RC_OFF(seven_t8_size);
  }
  return NULL;
}

uint8_t *seven_chords_notes(const void *chord_notes, uint32_t id) {
  if (!chord_notes) return NULL;
  for (int i = 0; i < ENTRIES; i++) {
    entry *e = &g_songs[i];
    if (__atomic_load_n(&e->cn_begin, __ATOMIC_ACQUIRE) == chord_notes && id < e->cn_count)
      return e->c8 + (size_t)id * (size_t)RC_OFF(seven_c8_size);
  }
  return NULL;
}

// `record` when it is a record's start inside a published T8 array (C8 when notes), else NULL
static uint8_t *member(const void *record, int notes) {
  const uint8_t *p = record;
  const size_t size = (size_t)(notes ? RC_OFF(seven_c8_size) : RC_OFF(seven_t8_size));
  for (int i = 0; p && i < ENTRIES; i++) {
    entry *e = &g_songs[i];
    if (!__atomic_load_n(notes ? &e->cn_begin : &e->tpl_begin, __ATOMIC_ACQUIRE)) continue;
    uint8_t *a = notes ? e->c8 : e->t8;
    const size_t n = notes ? e->cn_count : e->tpl_count;
    if (a && p >= a && p < a + n * size && (size_t)(p - a) % size == 0) return (uint8_t *)p;
  }
  return NULL;
}

uint8_t *seven_chords_as_t8(const void *record) { return member(record, 0); }
uint8_t *seven_chords_as_c8(const void *record) { return member(record, 1); }

uint8_t *seven_chords_t8_of(const void *game_template) {
  const uint8_t *p = game_template;
  const size_t tsz = (size_t)RC_OFF(sng_template_size);
  for (int i = 0; p && i < ENTRIES; i++) {
    entry *e = &g_songs[i];
    const uint8_t *b = __atomic_load_n(&e->tpl_begin, __ATOMIC_ACQUIRE);
    if (b && p >= b && p < b + (size_t)e->tpl_count * tsz && (size_t)(p - b) % tsz == 0)
      return e->t8 + (size_t)(p - b) / tsz * (size_t)RC_OFF(seven_t8_size);
  }
  return NULL;
}

int seven_chords_song_templates(const void *song, uint8_t **t8, uint32_t *count) {
  int ok = 0;
  pthread_mutex_lock(&g_lock);
  entry *e = song ? find(song) : NULL;
  if (e && __atomic_load_n(&e->tpl_begin, __ATOMIC_ACQUIRE)) {
    *t8 = e->t8;
    *count = e->tpl_count;
    ok = 1;
  }
  pthread_mutex_unlock(&g_lock);
  return ok;
}

int seven_chords_block_strings(const void *song) {
  pthread_mutex_lock(&g_lock);
  entry *e = song ? find(song) : NULL;
  const int n = e && e->block ? e->block->strings : 0;
  pthread_mutex_unlock(&g_lock);
  return n;
}

static int slot(int s) { return s >= 0 && s < SEVEN_CHORD_SLOTS; }

int seven_chords_t8_slots(const uint8_t *t8) { return t8 ? *AT(t8, seven_t8_slots) : 6; }
int seven_chords_t8_fret(const uint8_t *t8, int s) { return t8 && slot(s) ? (int8_t)AT(t8, seven_t8_frets)[s] : -1; }
int seven_chords_t8_finger(const uint8_t *t8, int s) {
  return t8 && slot(s) ? (int8_t)AT(t8, seven_t8_fingers)[s] : -1;
}
int32_t seven_chords_t8_midi(const uint8_t *t8, int s) {
  return t8 && slot(s) ? RC_LD(int32_t, AT(t8, seven_t8_midi) + 4 * s) : -1;
}
uint32_t seven_chords_c8_mask(const uint8_t *c8, int s) {
  return c8 && slot(s) ? RC_LD(uint32_t, AT(c8, seven_c8_mask) + 4 * s) : 0;
}
int seven_chords_c8_slide_to(const uint8_t *c8, int s) {
  return c8 && slot(s) ? (int8_t)AT(c8, seven_c8_slide_to)[s] : -1;
}
int seven_chords_c8_slide_unpitch_to(const uint8_t *c8, int s) {
  return c8 && slot(s) ? (int8_t)AT(c8, seven_c8_slide_unpitch_to)[s] : -1;
}
int seven_chords_c8_vibrato(const uint8_t *c8, int s) {
  return c8 && slot(s) ? RC_LD(int16_t, AT(c8, seven_c8_vibrato) + 2 * s) : 0;
}
uint8_t *seven_chords_c8_bends(const uint8_t *c8, int s) {
  return c8 && slot(s) ? (uint8_t *)AT(c8, seven_c8_bends) + (size_t)s * (size_t)RC_OFF(sng_bend_data_size) : NULL;
}

// ---- the mid points ---------------------------------------------------------------------------------------------
// SNG_Song_Load before the body's memory stream is destroyed (its buffer, position: the end of MetaData, size)
static void on_xb1(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  const uint8_t *song = PTR(f, "song");
  if (song) song_loaded(song, PTR(f, "body"), (uint32_t)SEVEN_GET(f, "position"), (uint32_t)SEVEN_GET(f, "size"));
}

// After the call of SNG_Song_PostProcess
static void on_pp1(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  const uint8_t *song = PTR(f, "song");
  if (song) song_post_processed(song);
}

// The SNG_Song's deleting destructor, before its body runs: the diagrams part lets go of the song first (rs7string's
// Rs7Diagram_Drop at the start of Rs7Ns_OnSongFree), then its records go
static void on_sf1(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  const uint8_t *song = PTR(f, "song");
  if (!song) return;
  seven_diagrams_song_freed(song);
  song_freed(song);
}

// A template stride site (rs7string's Redirect): `out` = the T8 record of template `chord` plus `add`, `chord9` = chord
// * 9 as the game's first lea leaves it (the stride is 9 * 8), on at `done`. Without a T8 array the game's own
// instructions run. A macro: each point's callback keeps its own value indexes (SEVEN_VAL caches them per place).
#define REDIRECT(f, out, add)                                                                                      \
  ({                                                                                                               \
    const uint32_t id_ = (uint32_t)SEVEN_GET(f, "chord");                                                          \
    uint8_t *t8_ = seven_chords_template(PTR(f, "templates"), id_);                                                \
    if (t8_) {                                                                                                     \
      SEVEN_SET(f, "chord9", id_ * (uint32_t)(RC_OFF(sng_template_size) / 8));                                     \
      SEVEN_SET(f, out, (uintptr_t)(t8_ + (add)));                                                                 \
      f->go = SEVEN_CONT(f, "done");                                                                               \
    }                                                                                                              \
    t8_ != NULL;                                                                                                   \
  })

// TP1 SNG_Song_GetChord, TP3 the neck's finger keep pass, TP4 the neck's finger display, TP5 the chord render, TP6 the
// chord box, TP8 the chord render's spawn, TP11 the post-processing's section string mask
static void on_tp1(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  (void)REDIRECT(f, "template", 0);
}
static void on_tp3(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  (void)REDIRECT(f, "template", 0);
}
static void on_tp4(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  (void)REDIRECT(f, "template", 0);
}
static void on_tp5(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  (void)REDIRECT(f, "template", 0);
}
static void on_tp6(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  (void)REDIRECT(f, "template", 0);
}
static void on_tp8(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  (void)REDIRECT(f, "template", 0);
}
static void on_tp11(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  (void)REDIRECT(f, "template", 0);
}

// TP7 the chord name: the range also clears esi (`xor esi, esi`)
static void on_tp7(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  if (REDIRECT(f, "template", 0)) SEVEN_SET(f, "zero", 0);
}

// What a site that hands out `record + at` gives on a vector without a T8 array: the game's record of template `id`
// moved back so that the T8's frets[8] (+0x48), which the loops the edits widen read (TP2's DM1, TP12's PL1), are the
// game's frets (+0x4): they read the game's six, and fingers[0] where a T8 has frets[6], not the next template's bytes
static uintptr_t as_if_t8(const uint8_t *templates, uint32_t id, int64_t at) {
  const int64_t back = RC_OFF(seven_t8_frets) - RC_OFF(sng_template_frets);
  return (uintptr_t)(templates + (size_t)id * (size_t)RC_OFF(sng_template_size) + at - back);
}

// TP12 the chord pluck: edi = the record's frets (the game's six at +0x4; its loop, widened to seven by the chord
// drawing part's PL1 edits, reads each fret at +0x44 past it: T8 frets[8]). Without a T8 array, as TP2.
static void on_tp12(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  if (REDIRECT(f, "frets", RC_OFF(sng_template_frets))) return;
  const uint8_t *templates = PTR(f, "templates");
  if (!templates) return;
  const uint32_t id = (uint32_t)SEVEN_GET(f, "chord");
  SEVEN_SET(f, "chord9", id * (uint32_t)(RC_OFF(sng_template_size) / 8));
  SEVEN_SET(f, "frets", as_if_t8(templates, id, RC_OFF(sng_template_frets)));
  f->go = SEVEN_CONT(f, "done");
}

// TP2 the section dimming (Rnd_GuitarStrings_UpdateSectionDimming): its loop over the guitar's strings reads frets[s]
// of the record at +0x48 (DM1). Without a T8 array the record is placed so that read is the game's frets at +0x4 (the
// loop is the only reader of edi), not the next template's bytes.
static void on_tp2(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  if (REDIRECT(f, "template", 0)) return;
  const uint8_t *templates = PTR(f, "templates");
  if (!templates) return;
  const uint32_t id = (uint32_t)SEVEN_GET(f, "chord");
  SEVEN_SET(f, "chord9", id * (uint32_t)(RC_OFF(sng_template_size) / 8));
  SEVEN_SET(f, "template", as_if_t8(templates, id, 0));
  f->go = SEVEN_CONT(f, "done");
}

// CP1 SNG_Song_GetChordNotes: eax = the C8 record of ChordNotes record `id`
static void on_cp1(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  uint8_t *c8 = seven_chords_notes(PTR(f, "chord_notes"), (uint32_t)SEVEN_GET(f, "id"));
  if (!c8) return;
  SEVEN_SET(f, "record", (uintptr_t)c8);
  f->go = SEVEN_CONT(f, "done");
}

// CP2 the chord render's spawn: ebx = the C8 record, esi = id * the game's record size (its imul)
static void on_cp2(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  const uint32_t id = (uint32_t)SEVEN_GET(f, "id");
  uint8_t *c8 = seven_chords_notes(PTR(f, "chord_notes"), id);
  if (!c8) return;
  SEVEN_SET(f, "record", (uintptr_t)c8);
  SEVEN_SET(f, "offset", id * (uint32_t)RC_OFF(sng_chord_notes_size));
  f->go = SEVEN_CONT(f, "done");
}

// DS1 SNG_Song_PostProcessNotes' double-stop count (a chord note with exactly two strings gets DOUBLE_STOP): over the
// T8's frets of seven strings, so one stock string and the seventh is a double stop (rs7string's DoubleStopSpan)
static void on_ds1(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  uint8_t *t8 = seven_chords_template(PTR(f, "templates"), (uint32_t)SEVEN_GET(f, "chord"));
  if (!t8) return;
  SEVEN_SET(f, "frets", (uintptr_t)AT(t8, seven_t8_frets));
  SEVEN_SET(f, "count", SEVEN_CHORD_NATIVE);
  SEVEN_SET(f, "played", 0);
  f->go = SEVEN_CONT(f, "done");
}

// DS1's count where the game counts a chord's played strings in a function of its own (the Mac: the post-processing
// calls it with the record and compares its answer with 2): `played` is that answer, made the count over the T8's
// frets of seven strings. Learn & Play counts in line (DS1) and has no such point.
static void on_ds1_count(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  const uint8_t *song = PTR(f, "song");
  uint8_t *templates;
  if (!song || !vec(AT(song, sng_song_templates), &templates)) return;
  uint8_t *t8 = seven_chords_template(templates, (uint32_t)SEVEN_GET(f, "chord"));
  if (!t8) return;
  uint32_t n = 0;
  for (int s = 0; s < SEVEN_CHORD_NATIVE; s++) n += AT(t8, seven_t8_frets)[s] != 0xff;
  SEVEN_SET(f, "played", n);
}

// BT1 Play_Eval_Build's end: the eval's tails from its template and ChordNotes copies' T8 / C8 (anything else: empty)
static void on_bt1(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  uint8_t *eval = PTR(f, "eval");
  if (!eval) return;
  const uint8_t *tpl = PTR(f, "template"), *cn = PTR(f, "chord_notes");
  seven_chords_eval_tails(eval, tpl ? seven_chords_as_t8(tpl) : NULL, cn ? seven_chords_as_c8(cn) : NULL);
}

// CS1 Play_Eval_ClearState: empty tails, and no child evals for strings 6/7
static void clear_state(uint8_t *eval) {
  if (!eval) return;
  seven_chords_eval_clear(eval);
  memset(AT(eval, seven_eval_child67), 0xff, 2);
}
static void on_cs1(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  clear_state(PTR(f, "eval"));
}
// The same where a build has Play_Eval_ClearState in line (the Mac: the eval's constructor and Play_Eval_Build;
// Learn & Play calls it from both, CS1)
static void on_cs1_ctor(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  clear_state(PTR(f, "eval"));
}
static void on_cs1_build(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  clear_state(PTR(f, "eval"));
}

int seven_chords_init(void) {
  static const char *const names[] = {
      "std_vector_begin", "std_vector_end", "efd_str_inline", "sng_song_levels", "sng_song_templates",
      "sng_song_chord_notes", "sng_song_tuning", "sng_song_capo", "sng_level_size", "sng_level_notes",
      "sng_note_size", "sng_note_mask", "sng_note_chord", "sng_note_chord_notes", "sng_template_size",
      "sng_template_frets", "sng_template_fingers", "sng_template_midi", "sng_template_name", "sng_chord_notes_size",
      "sng_chord_notes_mask", "sng_chord_notes_bends", "sng_chord_notes_slide_to", "sng_chord_notes_slide_unpitch_to",
      "sng_chord_notes_vibrato", "sng_bend_data_size", "sng_bend_data_used", "seven_t8_size", "seven_t8_frets",
      "seven_t8_fingers", "seven_t8_midi", "seven_t8_slots", "seven_t8_used", "seven_c8_size", "seven_c8_mask",
      "seven_c8_slide_to", "seven_c8_slide_unpitch_to", "seven_c8_vibrato", "seven_c8_bends", "seven_eval_tail_midi",
      "seven_eval_tail_mask", "seven_eval_tail_fret", "seven_eval_tail_finger", "seven_eval_tail_slide",
      "seven_eval_tail_unpitch", "seven_eval_tail_vibrato", "seven_eval_child67",
  };
  for (size_t i = 0; i < sizeof names / sizeof *names; i++)
    if (rc_off(names[i]) == RC_NO_OFF) {
      rsm_log("seven-string: chords: no offset %s on %s", names[i], rc_target());
      return -1;
    }
  if (!RC_ADDR(efd_alloc) || !RC_ADDR(efd_str_destroy)) {
    rsm_log("seven-string: chords: no game allocator (efd_alloc, efd_str_destroy) on %s", rc_target());
    return -1;
  }
  static const struct {
    const char *name;
    rc_mid_fn fn;
  } points[] = {
      {"seven_xb1", on_xb1},   {"seven_pp1", on_pp1},   {"seven_sf1", on_sf1},   {"seven_tp1", on_tp1},
      {"seven_tp2", on_tp2},   {"seven_tp3", on_tp3},   {"seven_tp4", on_tp4},   {"seven_tp5", on_tp5},
      {"seven_tp6", on_tp6},   {"seven_tp7", on_tp7},   {"seven_tp8", on_tp8},   {"seven_tp11", on_tp11},
      {"seven_tp12", on_tp12}, {"seven_cp1", on_cp1},   {"seven_cp2", on_cp2},   {"seven_ds1", on_ds1},
      {"seven_bt1", on_bt1},   {"seven_cs1", on_cs1},
      // where a build counts or clears in a function's place (none on Learn & Play)
      {"seven_ds1_count", on_ds1_count}, {"seven_cs1_ctor", on_cs1_ctor}, {"seven_cs1_build", on_cs1_build},
  };
  for (size_t i = 0; i < sizeof points / sizeof *points; i++)
    if (seven_mid(points[i].name, points[i].fn)) return -1;
  return 0;
}
