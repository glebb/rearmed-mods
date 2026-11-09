// Wwise Vorbis decoding (wwise_vorbis.h), written for rs-stems from the Vorbis I specification. Where the specification
// leaves room (the end of a packet, a codebook with one entry, the overlap of two blocks) it does what libvorbis does,
// because libvorbis is what vgmstream decodes .wem with, and vgmstream's output matches the game's.
//
// What Wwise changes, as ww2ogg and vgmstream found it: the setup header has short fields where Vorbis has fixed ones
// (no floor, mapping, window or transform types: always floor 1, mapping 0, window 0; the residue type in 2 bits) and
// names its codebooks by number in a library the game carries; the library's codebooks are packed (4-bit dimensions,
// 14-bit entry counts, codeword lengths in only the bits they need, a 1-bit lookup type); and audio packets start at
// the mode number, without the packet type bit or a long block's window flags. Those flags only say which window a
// long block uses next to a short one, which the decoder knows from the neighbouring blocks themselves, so packets are
// decoded as they are rather than rebuilt into standard ones first.
#include "wwise_vorbis.h"

#include <Accelerate/Accelerate.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FAST_BITS 10  // codewords up to this long are found with one table lookup
#define MAX_POSTS 256
#define MAX_CH 8

static uint32_t le16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | le16(p + 2) << 16; }
static int ilog(uint32_t v) { return v ? 32 - __builtin_clz(v) : 0; }

#define FAIL(...)                       \
  do {                                  \
    snprintf(err, errlen, __VA_ARGS__); \
    return -1;                          \
  } while (0)

// ------------------------------------------------------------------------------------------------
// Bits, least significant first
// ------------------------------------------------------------------------------------------------
typedef struct {
  const uint8_t *p;
  size_t bytes;  // the data
  size_t limit;  // readable bits; past the data they read as zeros (the padding of a rebuilt packet)
  size_t pos;
  int eop;       // read past the limit: every read from then on fails, as in libvorbis
} bitreader;

static inline uint64_t load64(const bitreader *b, size_t at) {
  uint64_t v = 0;
  if (at + 8 <= b->bytes) {
    memcpy(&v, b->p + at, 8);
    return v;
  }
  for (size_t i = 0; i < 8 && at + i < b->bytes; i++) v |= (uint64_t)b->p[at + i] << (8 * i);
  return v;
}

static inline uint32_t peek(const bitreader *b, int n) {
  uint64_t v = load64(b, b->pos >> 3) >> (b->pos & 7);
  return (uint32_t)(v & ((1ull << n) - 1));
}

// n (0-32) bits, or -1 at the end of the packet
static inline int64_t getbits(bitreader *b, int n) {
  if (b->eop || b->pos + (size_t)n > b->limit) {
    b->eop = 1;
    return -1;
  }
  uint32_t v = n ? peek(b, n) : 0;
  b->pos += (size_t)n;
  return v;
}

// ------------------------------------------------------------------------------------------------
// Codebooks
// ------------------------------------------------------------------------------------------------
typedef struct {
  int dims, entries, used, maxlen;
  uint8_t *len;        // codeword length of each entry, 0: not used
  float *values;       // entries x dims, the vector of each entry (lookup type 1), or NULL
  int single;          // a book with one entry of one bit: libvorbis takes a bit and returns it, whatever the bit
  uint32_t *fast;      // the next FAST_BITS bits -> (entry + 1) | length << 24; 0: a longer codeword
  uint64_t *longcode;  // longer codewords: codeword (first bit as bit 31) << 32 | entry, sorted
  int nlong;
} codebook;

static float float32_unpack(uint32_t x) {
  double mant = x & 0x1fffff;
  long e = (long)((x & 0x7fe00000) >> 21) - 788;
  if (x & 0x80000000) mant = -mant;
  if (e > 63) e = 63;  // libvorbis clamps the exponent
  if (e < -63) e = -63;
  return (float)ldexp(mant, (int)e);
}

// The largest r with r^dims <= entries
static int quantvals(int entries, int dims) {
  int r = 1;
  for (;;) {
    uint64_t p = 1;
    for (int i = 0; i < dims && p <= (uint64_t)entries; i++) p *= (uint64_t)(r + 1);
    if (p > (uint64_t)entries) return r;
    r++;
  }
}

static int cmp_u64(const void *a, const void *b) {
  uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
  return x < y ? -1 : x > y;
}

// Codewords as the specification assigns them: each entry in turn takes the lowest free codeword of its length.
// codes[i] is entry i's codeword with its first bit as bit 31.
static int assign_codewords(const codebook *c, uint32_t *codes, char *err, size_t errlen) {
  uint32_t avail[33] = {0};
  int first = 0;
  while (first < c->entries && !c->len[first]) first++;
  if (first == c->entries) return 0;
  codes[first] = 0;
  for (int i = 1; i <= c->len[first]; i++) avail[i] = 1u << (32 - i);
  for (int i = first + 1; i < c->entries; i++) {
    int z = c->len[i], y = z;
    if (!z) continue;
    while (y > 0 && !avail[y]) y--;
    if (!y) FAIL("a codebook has more codewords than fit");
    uint32_t res = avail[y];
    avail[y] = 0;
    codes[i] = res;
    for (int k = z; k > y; k--) avail[k] = res + (1u << (32 - k));
  }
  return 0;
}

static int build_lookup(codebook *c, char *err, size_t errlen) {
  c->single = -1;
  for (int i = 0; i < c->entries; i++)
    if (c->len[i]) {
      c->used++;
      if (c->len[i] > c->maxlen) c->maxlen = c->len[i];
      c->single = i;
    }
  if (c->used != 1 || c->maxlen != 1) c->single = -1;
  if (!c->used || c->single >= 0) return 0;
  uint32_t *codes = calloc((size_t)c->entries, sizeof *codes);
  c->fast = calloc(1u << FAST_BITS, sizeof *c->fast);
  if (!codes || !c->fast || assign_codewords(c, codes, err, errlen)) {
    free(codes);
    if (!*err) snprintf(err, errlen, "out of memory");
    return -1;
  }
  for (int i = 0; i < c->entries; i++) {
    int l = c->len[i];
    if (l > FAST_BITS) c->nlong++;
    if (!l || l > FAST_BITS) continue;
    uint32_t base = __builtin_bitreverse32(codes[i]);  // the codeword as the stream has it, first bit lowest
    for (uint32_t fill = 0; fill < 1u << (FAST_BITS - l); fill++)
      c->fast[base | fill << l] = (uint32_t)(i + 1) | (uint32_t)l << 24;
  }
  if (c->nlong) {
    c->longcode = malloc((size_t)c->nlong * sizeof *c->longcode);
    if (!c->longcode) {
      free(codes);
      FAIL("out of memory");
    }
    int k = 0;
    for (int i = 0; i < c->entries; i++)
      if (c->len[i] > FAST_BITS) c->longcode[k++] = (uint64_t)codes[i] << 32 | (uint32_t)i;
    qsort(c->longcode, (size_t)c->nlong, sizeof *c->longcode, cmp_u64);
  }
  free(codes);
  return 0;
}

// The next entry, or -1 at the end of the packet (or on a codeword the book doesn't have)
static int decode_entry(const codebook *c, bitreader *b) {
  if (b->eop || !c->used) return -1;
  if (c->single >= 0) return getbits(b, 1) < 0 ? -1 : c->single;
  uint32_t f = c->fast[peek(b, FAST_BITS)];
  int e, l;
  if (f) {
    e = (int)(f & 0xffffff) - 1;
    l = (int)(f >> 24);
  } else {
    // the codeword whose stretch of the code space holds the next 32 bits: the last one that starts at or before them
    uint32_t w = __builtin_bitreverse32(peek(b, 32));
    int lo = 0, hi = c->nlong;
    while (hi - lo > 1) {
      int mid = (lo + hi) / 2;
      if ((uint32_t)(c->longcode[mid] >> 32) <= w)
        lo = mid;
      else
        hi = mid;
    }
    if (!c->nlong) {
      b->eop = 1;
      return -1;
    }
    uint32_t code = (uint32_t)(c->longcode[lo] >> 32);
    e = (int)(uint32_t)c->longcode[lo];
    l = c->len[e];
    if (code > w || ((code ^ w) >> (32 - l))) {
      b->eop = 1;
      return -1;
    }
  }
  if (b->pos + (size_t)l > b->limit) {
    b->eop = 1;
    return -1;
  }
  b->pos += (size_t)l;
  return e;
}

// A codebook of the library, packed as Wwise has it
static int read_codebook(bitreader *b, codebook *c, char *err, size_t errlen) {
  c->dims = (int)getbits(b, 4);
  c->entries = (int)getbits(b, 14);
  if (c->dims < 1 || c->entries < 1) FAIL("a codebook with no dimensions or entries");
  c->len = calloc((size_t)c->entries, 1);
  if (!c->len) FAIL("out of memory");
  if (getbits(b, 1) == 1) {  // ordered: runs of entries by increasing length
    int len = (int)getbits(b, 5) + 1;
    for (int i = 0; i < c->entries && !b->eop; len++) {
      int n = (int)getbits(b, ilog((uint32_t)(c->entries - i)));
      if (n < 0 || i + n > c->entries || (n && len > 32)) FAIL("a codebook's lengths don't add up");
      memset(c->len + i, len, (size_t)n);
      i += n;
    }
  } else {
    int lenbits = (int)getbits(b, 3), sparse = (int)getbits(b, 1);
    if (lenbits < 1 || lenbits > 5) FAIL("a codebook with %d-bit lengths", lenbits);
    for (int i = 0; i < c->entries && !b->eop; i++)
      if (!sparse || getbits(b, 1) == 1) c->len[i] = (uint8_t)(getbits(b, lenbits) + 1);
  }
  if (getbits(b, 1) == 1) {  // lookup type 1: each entry's vector from a lattice of quantised values
    float min = float32_unpack((uint32_t)getbits(b, 32)), delta = float32_unpack((uint32_t)getbits(b, 32));
    int vbits = (int)getbits(b, 4) + 1, seq = (int)getbits(b, 1), nq = quantvals(c->entries, c->dims);
    uint32_t *q = malloc((size_t)nq * sizeof *q);
    c->values = calloc((size_t)c->entries * (size_t)c->dims, sizeof *c->values);
    if (!q || !c->values) {
      free(q);
      FAIL("out of memory");
    }
    for (int i = 0; i < nq; i++) q[i] = (uint32_t)getbits(b, vbits);
    for (int e = 0; e < c->entries; e++) {
      if (!c->len[e]) continue;
      float last = 0.f;
      int div = 1;
      for (int k = 0; k < c->dims; k++) {
        // in double and rounded once, as libvorbis's fabs() makes it
        float v = (float)(fabs((double)q[(e / div) % nq]) * delta + min + last);
        c->values[e * c->dims + k] = v;
        if (seq) last = v;
        div *= nq;
      }
    }
    free(q);
  }
  if (b->eop) FAIL("a codebook runs past the end of the library");
  return build_lookup(c, err, errlen);
}

static void free_codebook(codebook *c) {
  free(c->len);
  free(c->values);
  free(c->fast);
  free(c->longcode);
}

// ------------------------------------------------------------------------------------------------
// The setup: floors, residues, mappings, modes
// ------------------------------------------------------------------------------------------------
typedef struct {
  int partitions, pclass[32];
  int cdim[16], csub[16], cmaster[16], subbook[16][8];
  int mult, posts;
  int x[MAX_POSTS];
  int order[MAX_POSTS];          // posts by x
  int lo[MAX_POSTS], hi[MAX_POSTS];  // each post's neighbours among the posts before it (posts 2 on)
} floor1;

typedef struct {
  int type, begin, end, psize, classifications, classbook, stages, partvals;
  int books[64][8];  // -1: none in that pass
} residue;

typedef struct {
  int submaps, steps;
  int mag[256], ang[256];
  int mux[MAX_CH];
  int floor[16], residue[16];
} mapping;

typedef struct {
  int channels, bs[2], mod_packets;
  uint32_t rate, frames;
  int ncb, nfloor, nres, nmap, nmode, mode_bits;
  codebook *cb;
  floor1 *floors;
  residue *res;
  mapping *maps;
  int mode_long[64], mode_map[64];
  // decoding
  float db[256];          // floor 1's dB steps
  float *win[2];          // the rising slope of each block size's window
  vDSP_DFT_Setup dct[2];  // the inverse MDCT as a DCT-IV, per block size
  float *spec[MAX_CH], *block[MAX_CH], *prev[MAX_CH], *tmp;
  int prev_n;             // the previous block's size; 0 before the first
  int *cls;               // residue classifications
  int *ents;              // entries of a residue partition (type 0)
} decoder;

static int read_floor(decoder *d, bitreader *b, floor1 *f, char *err, size_t errlen) {
  int maxclass = -1;
  f->partitions = (int)getbits(b, 5);
  for (int j = 0; j < f->partitions; j++) {
    f->pclass[j] = (int)getbits(b, 4);
    if (f->pclass[j] > maxclass) maxclass = f->pclass[j];
  }
  for (int j = 0; j <= maxclass; j++) {
    f->cdim[j] = (int)getbits(b, 3) + 1;
    f->csub[j] = (int)getbits(b, 2);
    f->cmaster[j] = f->csub[j] ? (int)getbits(b, 8) : -1;
    if (f->cmaster[j] >= d->ncb) FAIL("a floor's master book is out of range");
    for (int k = 0; k < 1 << f->csub[j]; k++) {
      f->subbook[j][k] = (int)getbits(b, 8) - 1;
      if (f->subbook[j][k] >= d->ncb) FAIL("a floor's book is out of range");
    }
  }
  f->mult = (int)getbits(b, 2) + 1;
  int rangebits = (int)getbits(b, 4);
  f->x[0] = 0;
  f->x[1] = 1 << rangebits;
  f->posts = 2;
  for (int j = 0; j < f->partitions; j++)
    for (int k = 0; k < f->cdim[f->pclass[j]]; k++) {
      if (f->posts >= MAX_POSTS) FAIL("a floor with too many points");
      f->x[f->posts++] = (int)getbits(b, rangebits);
    }
  if (b->eop) FAIL("the setup header ends in a floor");
  for (int i = 0; i < f->posts; i++) f->order[i] = i;
  for (int i = 1; i < f->posts; i++)  // insertion sort by x: few points
    for (int j = i; j > 0 && f->x[f->order[j - 1]] > f->x[f->order[j]]; j--) {
      int t = f->order[j];
      f->order[j] = f->order[j - 1];
      f->order[j - 1] = t;
    }
  for (int i = 2; i < f->posts; i++) {
    int lo = 0, hi = 1, lx = 0, hx = f->x[1], cx = f->x[i];
    for (int j = 0; j < i; j++) {
      if (f->x[j] > lx && f->x[j] < cx) lo = j, lx = f->x[j];
      if (f->x[j] < hx && f->x[j] > cx) hi = j, hx = f->x[j];
    }
    f->lo[i] = lo;
    f->hi[i] = hi;
  }
  return 0;
}

static int read_residue(decoder *d, bitreader *b, residue *r, char *err, size_t errlen) {
  r->type = (int)getbits(b, 2);
  r->begin = (int)getbits(b, 24);
  r->end = (int)getbits(b, 24);
  r->psize = (int)getbits(b, 24) + 1;
  r->classifications = (int)getbits(b, 6) + 1;
  r->classbook = (int)getbits(b, 8);
  if (r->type > 2 || r->classbook >= d->ncb) FAIL("a residue of type %d or book %d", r->type, r->classbook);
  int cascade[64];
  for (int j = 0; j < r->classifications; j++) {
    int low = (int)getbits(b, 3), high = getbits(b, 1) == 1 ? (int)getbits(b, 5) : 0;
    cascade[j] = high * 8 + low;
  }
  r->stages = 0;
  for (int j = 0; j < r->classifications; j++) {
    if (ilog((uint32_t)cascade[j]) > r->stages) r->stages = ilog((uint32_t)cascade[j]);
    for (int k = 0; k < 8; k++) {
      r->books[j][k] = cascade[j] & (1 << k) ? (int)getbits(b, 8) : -1;
      if (r->books[j][k] >= d->ncb || (r->books[j][k] >= 0 && !d->cb[r->books[j][k]].values))
        FAIL("a residue book is out of range or has no vectors");
    }
  }
  // a class book with more combinations than entries is refused, as libvorbis does
  const codebook *cb = &d->cb[r->classbook];
  r->partvals = 1;
  for (int k = 0; k < cb->dims; k++) {
    r->partvals *= r->classifications;
    if (r->partvals > cb->entries) FAIL("a residue's class book is too small");
  }
  if (b->eop) FAIL("the setup header ends in a residue");
  return 0;
}

static int read_mapping(decoder *d, bitreader *b, mapping *m, char *err, size_t errlen) {
  m->submaps = getbits(b, 1) == 1 ? (int)getbits(b, 4) + 1 : 1;
  m->steps = getbits(b, 1) == 1 ? (int)getbits(b, 8) + 1 : 0;
  int cbits = ilog((uint32_t)d->channels - 1);
  for (int j = 0; j < m->steps; j++) {
    m->mag[j] = (int)getbits(b, cbits);
    m->ang[j] = (int)getbits(b, cbits);
    if (m->mag[j] == m->ang[j] || m->mag[j] >= d->channels || m->ang[j] >= d->channels)
      FAIL("a mapping couples channels that aren't there");
  }
  if (getbits(b, 2) != 0) FAIL("a mapping's reserved field isn't zero");
  for (int j = 0; j < d->channels; j++) {
    m->mux[j] = m->submaps > 1 ? (int)getbits(b, 4) : 0;
    if (m->mux[j] >= m->submaps) FAIL("a mapping's channel is in no submap");
  }
  for (int j = 0; j < m->submaps; j++) {
    getbits(b, 8);  // an unused time configuration
    m->floor[j] = (int)getbits(b, 8);
    m->residue[j] = (int)getbits(b, 8);
    if (m->floor[j] >= d->nfloor || m->residue[j] >= d->nres) FAIL("a mapping's floor or residue is out of range");
  }
  if (b->eop) FAIL("the setup header ends in a mapping");
  return 0;
}

static int read_setup(decoder *d, bitreader *b, const uint8_t *lib, size_t liblen, char *err, size_t errlen) {
  if (liblen < 8 || memcmp(lib, "RSCB", 4)) FAIL("codebooks.bin isn't a codebook library");
  uint32_t count = le32(lib + 4);
  size_t data = 8 + 4 * (size_t)count;
  if (data > liblen) FAIL("codebooks.bin is cut short");
  d->ncb = (int)getbits(b, 8) + 1;
  d->cb = calloc((size_t)d->ncb, sizeof *d->cb);
  if (!d->cb) FAIL("out of memory");
  for (int i = 0; i < d->ncb; i++) {
    int id = (int)getbits(b, 10);
    if (id < 0 || (uint32_t)id >= count) FAIL("codebook %d isn't in the library (%u codebooks)", id, count);
    size_t at = data + le32(lib + 8 + 4 * (size_t)id);
    if (at >= liblen) FAIL("codebook %d is past the end of codebooks.bin", id);
    bitreader cb = {lib + at, liblen - at, (liblen - at) * 8, 0, 0};
    if (read_codebook(&cb, &d->cb[i], err, errlen)) return -1;
  }
  d->nfloor = (int)getbits(b, 6) + 1;
  d->floors = calloc((size_t)d->nfloor, sizeof *d->floors);
  if (!d->floors) FAIL("out of memory");
  for (int i = 0; i < d->nfloor; i++)
    if (read_floor(d, b, &d->floors[i], err, errlen)) return -1;
  d->nres = (int)getbits(b, 6) + 1;
  d->res = calloc((size_t)d->nres, sizeof *d->res);
  if (!d->res) FAIL("out of memory");
  for (int i = 0; i < d->nres; i++)
    if (read_residue(d, b, &d->res[i], err, errlen)) return -1;
  d->nmap = (int)getbits(b, 6) + 1;
  d->maps = calloc((size_t)d->nmap, sizeof *d->maps);
  if (!d->maps) FAIL("out of memory");
  for (int i = 0; i < d->nmap; i++)
    if (read_mapping(d, b, &d->maps[i], err, errlen)) return -1;
  d->nmode = (int)getbits(b, 6) + 1;
  for (int i = 0; i < d->nmode; i++) {
    d->mode_long[i] = (int)getbits(b, 1);
    d->mode_map[i] = (int)getbits(b, 8);
    if (d->mode_map[i] >= d->nmap) FAIL("a mode's mapping is out of range");
  }
  if (b->eop) FAIL("the setup header is cut short");
  d->mode_bits = ilog((uint32_t)d->nmode - 1);
  return 0;
}

// ------------------------------------------------------------------------------------------------
// Audio packets
// ------------------------------------------------------------------------------------------------
static int render_point(int x0, int x1, int y0, int y1, int x) {
  y0 &= 0x7fff;
  y1 &= 0x7fff;
  int dy = y1 - y0, adx = x1 - x0, off = abs(dy) * (x - x0) / adx;
  return dy < 0 ? y0 - off : y0 + off;
}

// A channel's floor curve points, unwrapped; 0 when the channel is silent in this block (or the packet ends)
static int floor_read(const decoder *d, const floor1 *f, bitreader *b, int *y) {
  static const int range[4] = {256, 128, 86, 64};
  if (getbits(b, 1) != 1) return 0;
  int q = range[f->mult - 1], rbits = ilog((uint32_t)q - 1);
  y[0] = (int)getbits(b, rbits);
  y[1] = (int)getbits(b, rbits);
  for (int i = 0, at = 2; i < f->partitions; i++) {
    int cls = f->pclass[i], cbits = f->csub[cls], cval = 0;
    if (cbits && (cval = decode_entry(&d->cb[f->cmaster[cls]], b)) < 0) return 0;
    for (int j = 0; j < f->cdim[cls]; j++) {
      int book = f->subbook[cls][cval & ((1 << cbits) - 1)];
      cval >>= cbits;
      y[at + j] = book >= 0 ? decode_entry(&d->cb[book], b) : 0;
      if (y[at + j] < 0) return 0;
    }
    at += f->cdim[cls];
  }
  for (int i = 2; i < f->posts; i++) {
    int lo = f->lo[i], hi = f->hi[i], pred = render_point(f->x[lo], f->x[hi], y[lo], y[hi], f->x[i]);
    int hiroom = q - pred, loroom = pred, room = (hiroom < loroom ? hiroom : loroom) * 2, v = y[i];
    if (v) {
      if (v >= room)
        v = hiroom > loroom ? v - loroom : -1 - (v - hiroom);
      else
        v = v & 1 ? -((v + 1) >> 1) : v >> 1;
      y[i] = (v + pred) & 0x7fff;
      y[lo] &= 0x7fff;
      y[hi] &= 0x7fff;
    } else {
      y[i] = pred | 0x8000;  // not used: no line ends here
    }
  }
  return 1;
}

static void render_line(const float *db, int n, int x0, int x1, int y0, int y1, float *out) {
  int dy = y1 - y0, adx = x1 - x0, ady = abs(dy), base = dy / adx, sy = dy < 0 ? base - 1 : base + 1;
  int x = x0, y = y0, e = 0;
  ady -= abs(base * adx);
  if (n > x1) n = x1;
  if (x < n) out[x] *= db[y];
  while (++x < n) {
    e += ady;
    if (e >= adx) {
      e -= adx;
      y += sy;
    } else {
      y += base;
    }
    out[x] *= db[y];
  }
}

// Multiplies the spectrum (the residue) by the floor curve
static void floor_apply(const decoder *d, const floor1 *f, const int *y, float *out, int n) {
  int hx = 0, lx = 0, ly = y[0] * f->mult;
  ly = ly < 0 ? 0 : ly > 255 ? 255 : ly;
  for (int j = 1; j < f->posts; j++) {
    int cur = f->order[j], hy = y[cur] & 0x7fff;
    if (hy != y[cur] || f->x[cur] == lx) continue;
    hx = f->x[cur];
    hy *= f->mult;
    hy = hy < 0 ? 0 : hy > 255 ? 255 : hy;
    render_line(d->db, n, lx, hx, ly, hy, out);
    lx = hx;
    ly = hy;
  }
  for (int j = hx; j < n; j++) out[j] *= d->db[ly];
}

// A residue partition, by vector type: 0 interleaves each vector's values across the partition, 1 lays them in a row
static int vec_split(const decoder *d, const codebook *c, float *a, bitreader *b, int n) {
  int step = n / c->dims;
  for (int i = 0; i < step; i++)
    if ((d->ents[i] = decode_entry(c, b)) < 0) return -1;
  for (int i = 0, o = 0; i < c->dims; i++, o += step)
    for (int j = 0; o + j < n && j < step; j++) a[o + j] += c->values[d->ents[j] * c->dims + i];
  return 0;
}

static int vec_row(const codebook *c, float *a, bitreader *b, int n) {
  for (int i = 0; i < n;) {
    int e = decode_entry(c, b);
    if (e < 0) return -1;
    const float *t = c->values + e * c->dims;
    for (int j = 0; i < n && j < c->dims;) a[i++] += t[j++];
  }
  return 0;
}

// Type 2: one vector across the channels, interleaved
static int vec_channels(const codebook *c, float **a, int offset, int ch, bitreader *b, int n) {
  int k = 0;
  for (int i = offset / ch, m = (offset + n) / ch; i < m;) {
    int e = decode_entry(c, b);
    if (e < 0) return -1;
    const float *t = c->values + e * c->dims;
    for (int j = 0; i < m && j < c->dims; j++) {
      a[k++][i] += t[j];
      if (k == ch) k = 0, i++;
    }
  }
  return 0;
}

// The residue of the channels in v (ch of them), added to v; n is half the block. Ends quietly at the packet's end.
static void residue_read(decoder *d, const residue *r, float **v, int ch, int n, bitreader *b) {
  const codebook *cb = &d->cb[r->classbook];
  int per = cb->dims, vectors = r->type == 2 ? 1 : ch;
  int end = r->end < (r->type == 2 ? n * ch : n) ? r->end : (r->type == 2 ? n * ch : n), len = end - r->begin;
  if (len <= 0) return;
  int parts = len / r->psize, words = (parts + per - 1) / per;
  for (int s = 0; s < r->stages; s++)
    for (int i = 0, l = 0; i < parts; l++) {
      if (s == 0)
        for (int j = 0; j < vectors; j++) {
          int t = decode_entry(cb, b);
          if (t < 0 || t >= r->partvals) return;
          int *c = d->cls + (j * words + l) * per;
          for (int k = per - 1; k >= 0; k--) c[k] = t % r->classifications, t /= r->classifications;
        }
      for (int k = 0; k < per && i < parts; k++, i++)
        for (int j = 0; j < vectors; j++) {
          int book = r->books[d->cls[(j * words + l) * per + k]][s], off = r->begin + i * r->psize, bad;
          if (book < 0 || !d->cb[book].used) continue;
          if (r->type == 2)
            bad = vec_channels(&d->cb[book], v, off, ch, b, r->psize);
          else if (r->type == 1)
            bad = vec_row(&d->cb[book], v[j] + off, b, r->psize);
          else
            bad = vec_split(d, &d->cb[book], v[j] + off, b, r->psize);
          if (bad) return;
        }
    }
}

// The block's samples, n of them per channel, from its n/2 coefficients: the inverse MDCT as a DCT-IV of n/2 plus
// the MDCT's symmetries (no scaling, as libvorbis has it)
static void imdct(decoder *d, int w, const float *in, float *out) {
  int n = d->bs[w], m = n / 2;
  vDSP_DCT_Execute(d->dct[w], in, d->tmp);
  for (int i = 0; i < n; i++) {
    int j = i + m / 2;
    out[i] = j < m ? d->tmp[j] : j < 2 * m ? -d->tmp[2 * m - 1 - j] : -d->tmp[j - 2 * m];
  }
}

// One audio packet; appends the frames it completes (from the previous block's centre to this one's) to out,
// interleaved, and returns how many (0 for the first block), or -1 for a packet that isn't audio
static int audio_packet(decoder *d, const uint8_t *p, size_t size, float *out) {
  bitreader b = {p, size, size * 8, 0, 0};
  if (!d->mod_packets && getbits(&b, 1) != 0) return -1;
  int mode = (int)getbits(&b, d->mode_bits);
  if (mode < 0 || mode >= d->nmode) return -1;
  int w = d->mode_long[mode], n = d->bs[w], half = n / 2, ch = d->channels;
  if (d->mod_packets)
    b.limit += w ? 5 : 7;  // libvorbis reads the rebuilt packet, padded to a whole byte after the bits put back
  else if (w)
    getbits(&b, 2);  // the window flags
  const mapping *m = &d->maps[d->mode_map[mode]];
  int y[MAX_CH][MAX_POSTS], used[MAX_CH];
  for (int c = 0; c < ch; c++) {
    used[c] = floor_read(d, &d->floors[m->floor[m->mux[c]]], &b, y[c]);
    memset(d->spec[c], 0, (size_t)half * sizeof(float));
  }
  for (int i = 0; i < m->steps; i++)  // coupled channels are decoded together
    if (used[m->mag[i]] || used[m->ang[i]]) used[m->mag[i]] = used[m->ang[i]] = 1;
  for (int s = 0; s < m->submaps; s++) {
    float *v[MAX_CH];
    int k = 0, any = 0;
    const residue *r = &d->res[m->residue[s]];
    for (int c = 0; c < ch; c++) {
      if (m->mux[c] != s) continue;
      // types 0 and 1 decode only the channels in use; type 2 all of them when any is
      if (r->type == 2 || used[c]) v[k++] = d->spec[c];
      any |= used[c];
    }
    if (any && k) residue_read(d, r, v, k, half, &b);
  }
  for (int i = m->steps - 1; i >= 0; i--) {
    float *pm = d->spec[m->mag[i]], *pa = d->spec[m->ang[i]];
    for (int j = 0; j < half; j++) {
      float mag = pm[j], ang = pa[j];
      if (mag > 0) {
        if (ang > 0)
          pa[j] = mag - ang;
        else
          pa[j] = mag, pm[j] = mag + ang;
      } else {
        if (ang > 0)
          pa[j] = mag + ang;
        else
          pa[j] = mag, pm[j] = mag - ang;
      }
    }
  }
  for (int c = 0; c < ch; c++) {
    if (used[c])
      floor_apply(d, &d->floors[m->floor[m->mux[c]]], y[c], d->spec[c], half);
    else
      memset(d->spec[c], 0, (size_t)half * sizeof(float));
    imdct(d, w, d->spec[c], d->block[c]);
  }
  // overlap with the previous block: the window slope is the shorter block's, centred on the previous block's 3/4
  // point and this one's 1/4 point; outside it each block counts in full (libvorbis's blockin)
  int frames = 0;
  if (d->prev_n) {
    int pn = d->prev_n, ov = (pn < n ? pn : n) / 2, k0 = pn / 4 - ov / 2, c0 = n / 4 - ov / 2;
    const float *win = d->win[ov == d->bs[0] / 2 ? 0 : 1];
    frames = pn / 4 + n / 4;
    for (int c = 0; c < ch; c++) {
      const float *pv = d->prev[c], *cu = d->block[c];
      float *o = out + c;
      int k = 0;
      for (; k < k0; k++) o[k * ch] = pv[k];
      for (int j = 0; j < ov; j++, k++) o[k * ch] = pv[k] * win[ov - 1 - j] + cu[c0 + j] * win[j];
      for (int j = c0 + ov; k < frames; k++, j++) o[k * ch] = cu[j];
    }
  }
  for (int c = 0; c < ch; c++) memcpy(d->prev[c], d->block[c] + half, (size_t)half * sizeof(float));
  d->prev_n = n;
  return frames;
}

// ------------------------------------------------------------------------------------------------
// The file
// ------------------------------------------------------------------------------------------------
typedef struct {
  const uint8_t *fmt, *data;
  size_t data_size;
  uint32_t setup, audio;
} wem_layout;

static int read_header(const uint8_t *w, size_t len, decoder *d, wem_layout *l, char *err, size_t errlen) {
  if (len < 12 || memcmp(w, "RIFF", 4) || memcmp(w + 8, "WAVE", 4)) FAIL("not a little-endian RIFF WAVE file");
  size_t fmt_size = 0;
  memset(l, 0, sizeof *l);
  for (size_t at = 12; at + 8 <= len;) {
    size_t size = le32(w + at + 4), body = at + 8;
    if (size > len - body) size = len - body;
    if (!memcmp(w + at, "fmt ", 4)) l->fmt = w + body, fmt_size = size;
    if (!memcmp(w + at, "data", 4)) l->data = w + body, l->data_size = size;
    at = body + size;
  }
  if (!l->fmt || !l->data) FAIL("no fmt or data chunk");
  if (le16(l->fmt) != 0xffff || fmt_size != 0x42 || le16(l->fmt + 0x10) != 0x30)
    FAIL("not the Wwise Vorbis this reads (codec %#x, fmt size %#zx)", le16(l->fmt), fmt_size);
  const uint8_t *vorb = l->fmt + 0x18;
  d->channels = (int)le16(l->fmt + 2);
  d->rate = le32(l->fmt + 4);
  d->frames = le32(vorb);
  uint32_t signal = le32(vorb + 4);
  // the packet form, by the value vgmstream and ww2ogg read it from: these four have standard packets
  d->mod_packets = !(signal == 0x4a || signal == 0x4b || signal == 0x69 || signal == 0x70);
  l->setup = le32(vorb + 0x10);
  l->audio = le32(vorb + 0x14);
  d->bs[0] = 1 << vorb[0x28];
  d->bs[1] = 1 << vorb[0x29];
  if (d->channels < 1 || d->channels > MAX_CH) FAIL("%d channels", d->channels);
  if (vorb[0x28] < 6 || vorb[0x29] > 13 || d->bs[0] > d->bs[1]) FAIL("block sizes %d and %d", d->bs[0], d->bs[1]);
  if (l->setup + 2 > l->data_size || l->audio > l->data_size) FAIL("the setup or audio is past the data");
  return 0;
}

int ww_probe(const uint8_t *wem, size_t len, ww_audio *out, char *err, size_t errlen) {
  decoder d = {0};
  wem_layout l;
  memset(out, 0, sizeof *out);
  if (read_header(wem, len, &d, &l, err, errlen)) return -1;
  out->rate = d.rate;
  out->channels = (uint32_t)d.channels;
  out->frames = d.frames;
  return 0;
}

static int prepare(decoder *d, char *err, size_t errlen) {
  for (int i = 0; i < 256; i++) d->db[i] = (float)pow(10.0, (i + 1) * 7.0 / 256.0 - 7.0);  // -140 dB to 0 dB
  for (int w = 0; w < 2; w++) {
    int len = d->bs[w] / 2;
    d->win[w] = malloc((size_t)len * sizeof(float));
    d->dct[w] = vDSP_DCT_CreateSetup(NULL, (vDSP_Length)len, vDSP_DCT_IV);
    if (!d->win[w] || !d->dct[w]) FAIL("no DCT of %d", len);
    for (int i = 0; i < len; i++) {
      double s = sin((i + 0.5) / len * M_PI / 2);
      d->win[w][i] = (float)sin(M_PI / 2 * s * s);
    }
  }
  int big = d->bs[1], maxpsize = 1, maxwords = 1;
  for (int i = 0; i < d->nres; i++) {
    if (d->res[i].psize > maxpsize) maxpsize = d->res[i].psize;
    int per = d->cb[d->res[i].classbook].dims, parts = big * d->channels / 2 / d->res[i].psize;
    if ((parts + per - 1) / per * per > maxwords) maxwords = (parts + per - 1) / per * per;
  }
  d->tmp = malloc((size_t)big * sizeof(float));
  d->cls = malloc((size_t)(maxwords + 8) * (size_t)d->channels * sizeof(int));
  d->ents = malloc((size_t)maxpsize * sizeof(int));
  if (!d->tmp || !d->cls || !d->ents) FAIL("out of memory");
  for (int c = 0; c < d->channels; c++) {
    d->spec[c] = malloc((size_t)big / 2 * sizeof(float));
    d->block[c] = malloc((size_t)big * sizeof(float));
    d->prev[c] = malloc((size_t)big / 2 * sizeof(float));
    if (!d->spec[c] || !d->block[c] || !d->prev[c]) FAIL("out of memory");
  }
  return 0;
}

static void release(decoder *d) {
  for (int i = 0; i < d->ncb && d->cb; i++) free_codebook(&d->cb[i]);
  free(d->cb);
  free(d->floors);
  free(d->res);
  free(d->maps);
  for (int w = 0; w < 2; w++) {
    free(d->win[w]);
    if (d->dct[w]) vDSP_DFT_DestroySetup(d->dct[w]);
  }
  for (int c = 0; c < MAX_CH; c++) {
    free(d->spec[c]);
    free(d->block[c]);
    free(d->prev[c]);
  }
  free(d->tmp);
  free(d->cls);
  free(d->ents);
}

int ww_decode(const uint8_t *wem, size_t len, const uint8_t *codebooks, size_t cblen, ww_audio *out, char *err,
              size_t errlen) {
  decoder d = {0};
  wem_layout l;
  memset(out, 0, sizeof *out);
  *err = 0;
  if (read_header(wem, len, &d, &l, err, errlen)) return -1;
  const uint8_t *s = l.data + l.setup;
  size_t ssize = le16(s);
  int rc = -1;
  if (l.setup + 2 + ssize > l.data_size) {
    snprintf(err, errlen, "the setup header is past the data");
    return -1;
  }
  bitreader b = {s + 2, ssize, ssize * 8, 0, 0};
  if (read_setup(&d, &b, codebooks, cblen, err, errlen) || prepare(&d, err, errlen)) goto done;
  size_t total = (size_t)d.frames * (size_t)d.channels;
  float *pcm = calloc(total + (size_t)d.bs[1] * (size_t)d.channels, sizeof *pcm);  // room for a block past the end
  if (!pcm) {
    snprintf(err, errlen, "out of memory for %u frames", d.frames);
    goto done;
  }
  size_t have = 0;  // frames decoded
  for (size_t at = l.audio; at + 2 <= l.data_size && have < d.frames;) {
    size_t size = le16(l.data + at);
    if (at + 2 + size > l.data_size) break;  // a packet cut short: the rest stays silent
    int n = size ? audio_packet(&d, l.data + at + 2, size, pcm + have * (size_t)d.channels) : 0;
    if (n > 0) have += (size_t)n;
    at += 2 + size;
  }
  if (have < d.frames) memset(pcm + have * (size_t)d.channels, 0, (d.frames - have) * (size_t)d.channels * sizeof *pcm);
  out->pcm = pcm;
  out->rate = d.rate;
  out->channels = (uint32_t)d.channels;
  out->frames = d.frames;
  rc = 0;
done:
  release(&d);
  return rc;
}

void ww_free(ww_audio *a) {
  free(a->pcm);
  a->pcm = NULL;
}
