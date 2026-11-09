// The 7z archives of cache.psarc and their LZMA (rsmods_7z.h). The decoder follows the LZMA specification's reference
// decoder (LzmaSpec.cpp of the LZMA SDK, public domain); the 7z layout is 7-Zip's 7zFormat.txt, as far as the game's
// archives and RS Re-ARMed's overlay (tools/play-native/cache_overlay.py) use it.
#include "rsmods_7z.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <zlib.h>

// ------------------------------------------------------------------------------------------------
// The range decoder and the LZMA decoder
// ------------------------------------------------------------------------------------------------
#define PROB_BITS 11
#define PROB_ONE (1u << PROB_BITS)
#define PROB_INIT (PROB_ONE >> 1)
#define MOVE_BITS 5
#define TOP (1u << 24)
typedef uint16_t prob;

typedef struct {
  const uint8_t *in;
  size_t pos, len;
  uint32_t range, code;
  int bad;  // read past the input, or a corrupt stream
} rdec;

static uint8_t rd_byte(rdec *r) {
  if (r->pos < r->len) return r->in[r->pos++];
  r->bad = 1;
  return 0;
}

static int rd_init(rdec *r, const uint8_t *in, size_t len) {
  *r = (rdec){in, 0, len, 0xFFFFFFFFu, 0, 0};
  if (rd_byte(r) != 0) return 0;
  for (int i = 0; i < 4; i++) r->code = r->code << 8 | rd_byte(r);
  return !r->bad && r->code != r->range;
}

static void rd_norm(rdec *r) {
  if (r->range < TOP) {
    r->range <<= 8;
    r->code = r->code << 8 | rd_byte(r);
  }
}

static unsigned rd_bit(rdec *r, prob *p) {
  uint32_t v = *p, bound = (r->range >> PROB_BITS) * v;
  unsigned bit;
  if (r->code < bound) {
    v += (PROB_ONE - v) >> MOVE_BITS;
    r->range = bound;
    bit = 0;
  } else {
    v -= v >> MOVE_BITS;
    r->code -= bound;
    r->range -= bound;
    bit = 1;
  }
  *p = (prob)v;
  rd_norm(r);
  return bit;
}

static uint32_t rd_direct(rdec *r, unsigned n) {
  uint32_t res = 0;
  do {
    r->range >>= 1;
    r->code -= r->range;
    uint32_t t = 0u - (r->code >> 31);
    r->code += r->range & t;
    if (r->code == r->range) r->bad = 1;
    rd_norm(r);
    res = (res << 1) + (t + 1);
  } while (--n);
  return res;
}

static uint32_t bit_tree(rdec *r, prob *p, unsigned bits) {
  uint32_t m = 1;
  for (unsigned i = 0; i < bits; i++) m = (m << 1) + rd_bit(r, &p[m]);
  return m - (1u << bits);
}

static uint32_t bit_tree_reverse(rdec *r, prob *p, unsigned bits) {
  uint32_t m = 1, sym = 0;
  for (unsigned i = 0; i < bits; i++) {
    unsigned bit = rd_bit(r, &p[m]);
    m = (m << 1) + bit;
    sym |= bit << i;
  }
  return sym;
}

#define POS_STATES_MAX 16
#define STATES 12
#define END_POS_MODEL 14
#define FULL_DISTANCES 128
#define ALIGN_BITS 4
#define MATCH_MIN 2

typedef struct {
  prob choice, choice2, low[POS_STATES_MAX][8], mid[POS_STATES_MAX][8], high[256];
} len_dec;

typedef struct {
  unsigned lc, lp, pb;
  prob *lit;  // 0x300 << (lc + lp)
  size_t lit_n;
  prob pos_slot[4][64], pos_dec[1 + FULL_DISTANCES - END_POS_MODEL], align[1 << ALIGN_BITS];
  prob is_match[STATES << 4], is_rep[STATES], is_rep_g0[STATES], is_rep_g1[STATES], is_rep_g2[STATES],
      is_rep0_long[STATES << 4];
  len_dec len, rep_len;
  uint32_t rep0, rep1, rep2, rep3;
  unsigned state;
} lz;

static void fill(prob *p, size_t n) {
  for (size_t i = 0; i < n; i++) p[i] = PROB_INIT;
}

static void lz_reset(lz *d) {
  fill(d->lit, d->lit_n);
  fill(&d->pos_slot[0][0], sizeof d->pos_slot / sizeof(prob));
  fill(d->pos_dec, sizeof d->pos_dec / sizeof(prob));
  fill(d->align, sizeof d->align / sizeof(prob));
  fill(d->is_match, sizeof d->is_match / sizeof(prob));
  fill(d->is_rep, STATES);
  fill(d->is_rep_g0, STATES);
  fill(d->is_rep_g1, STATES);
  fill(d->is_rep_g2, STATES);
  fill(d->is_rep0_long, sizeof d->is_rep0_long / sizeof(prob));
  fill((prob *)&d->len, sizeof d->len / sizeof(prob));
  fill((prob *)&d->rep_len, sizeof d->rep_len / sizeof(prob));
  d->rep0 = d->rep1 = d->rep2 = d->rep3 = 0;
  d->state = 0;
}

// lc, lp and pb from their byte; the literal tables for them
static int lz_props(lz *d, unsigned b, unsigned max_lclp) {
  if (b >= 9 * 5 * 5) return 0;
  unsigned lc = b % 9, lp = (b / 9) % 5, pb = b / 45;
  if (lc + lp > max_lclp) return 0;
  size_t n = (size_t)0x300 << (lc + lp);
  if (n != d->lit_n) {
    prob *lit = realloc(d->lit, n * sizeof *lit);
    if (!lit) return 0;
    d->lit = lit;
    d->lit_n = n;
  }
  d->lc = lc;
  d->lp = lp;
  d->pb = pb;
  return 1;
}

static uint32_t len_decode(rdec *r, len_dec *l, unsigned ps) {
  if (!rd_bit(r, &l->choice)) return bit_tree(r, l->low[ps], 3);
  if (!rd_bit(r, &l->choice2)) return 8 + bit_tree(r, l->mid[ps], 3);
  return 16 + bit_tree(r, l->high, 8);
}

static uint32_t distance(lz *d, rdec *r, uint32_t len) {
  unsigned slot = bit_tree(r, d->pos_slot[len < 3 ? len : 3], 6);
  if (slot < 4) return slot;
  unsigned bits = (slot >> 1) - 1;
  uint32_t dist = (2 | (slot & 1)) << bits;
  if (slot < END_POS_MODEL) return dist + bit_tree_reverse(r, d->pos_dec + dist - slot, bits);
  dist += rd_direct(r, bits - ALIGN_BITS) << ALIGN_BITS;
  return dist + bit_tree_reverse(r, d->align, ALIGN_BITS);
}

// Decodes into out from *pos up to end. base: where the dictionary starts (an LZMA2 dictionary reset), whose
// position the literal and position states count from. 1 when it reached end, 2 at an end marker (with marker), 0
// for a corrupt stream.
static int lz_run(lz *d, rdec *r, uint8_t *out, size_t *pos, size_t end, size_t base, int marker) {
  const uint32_t pb_mask = (1u << d->pb) - 1, lp_mask = (1u << d->lp) - 1;
  while (*pos < end) {
    if (r->bad) return 0;
    size_t at = *pos - base;
    unsigned st = d->state, ps = (unsigned)(at & pb_mask);
    if (!rd_bit(r, &d->is_match[(st << 4) + ps])) {
      unsigned prev = at ? out[*pos - 1] : 0;
      prob *p = d->lit + 0x300 * (size_t)((((uint32_t)at & lp_mask) << d->lc) + (prev >> (8 - d->lc)));
      unsigned sym = 1;
      if (st >= 7) {
        unsigned match = out[*pos - d->rep0 - 1];
        do {
          unsigned mbit = (match >> 7) & 1;
          match <<= 1;
          unsigned bit = rd_bit(r, &p[((1 + mbit) << 8) + sym]);
          sym = sym << 1 | bit;
          if (mbit != bit) break;
        } while (sym < 0x100);
      }
      while (sym < 0x100) sym = sym << 1 | rd_bit(r, &p[sym]);
      out[(*pos)++] = (uint8_t)(sym - 0x100);
      d->state = st < 4 ? 0 : st < 10 ? st - 3 : st - 6;
      continue;
    }
    uint32_t len;
    if (rd_bit(r, &d->is_rep[st])) {
      if (!at) return 0;
      if (!rd_bit(r, &d->is_rep_g0[st])) {
        if (!rd_bit(r, &d->is_rep0_long[(st << 4) + ps])) {  // one byte at rep0
          d->state = st < 7 ? 9 : 11;
          out[*pos] = out[*pos - d->rep0 - 1];
          (*pos)++;
          continue;
        }
      } else {
        uint32_t dist;
        if (!rd_bit(r, &d->is_rep_g1[st])) {
          dist = d->rep1;
        } else {
          if (!rd_bit(r, &d->is_rep_g2[st])) {
            dist = d->rep2;
          } else {
            dist = d->rep3;
            d->rep3 = d->rep2;
          }
          d->rep2 = d->rep1;
        }
        d->rep1 = d->rep0;
        d->rep0 = dist;
      }
      len = len_decode(r, &d->rep_len, ps);
      d->state = st < 7 ? 8 : 11;
    } else {
      d->rep3 = d->rep2;
      d->rep2 = d->rep1;
      d->rep1 = d->rep0;
      len = len_decode(r, &d->len, ps);
      d->state = st < 7 ? 7 : 10;
      d->rep0 = distance(d, r, len);
      if (d->rep0 == 0xFFFFFFFFu) return marker && !r->bad && r->code == 0 ? 2 : 0;
    }
    if (d->rep0 >= at) return 0;
    len += MATCH_MIN;
    if (len > end - *pos) return 0;
    for (; len; len--, (*pos)++) out[*pos] = out[*pos - d->rep0 - 1];
  }
  return !r->bad;
}

int rsm_lzma_decode(const uint8_t props[5], const uint8_t *in, size_t in_len, uint8_t *out, size_t out_len) {
  lz d = {0};
  rdec r;
  int ok = 0;
  if (!lz_props(&d, props[0], 8 + 4)) goto done;
  lz_reset(&d);
  if (!rd_init(&r, in, in_len)) goto done;
  size_t pos = 0;
  ok = lz_run(&d, &r, out, &pos, out_len, 0, 1) != 0 && pos == out_len && !r.bad;
done:
  free(d.lit);
  return ok;
}

int rsm_lzma2_decode(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_len) {
  lz d = {0};
  size_t ip = 0, pos = 0, base = 0;
  int have_dict = 0, have_props = 0, have_state = 0, ok = 0;
  for (;;) {
    if (ip >= in_len) goto done;
    unsigned c = in[ip++];
    if (c == 0) break;
    if (c == 1 || c == 2) {  // stored: 1 resets the dictionary
      if (in_len - ip < 2) goto done;
      size_t n = ((size_t)in[ip] << 8 | in[ip + 1]) + 1;
      ip += 2;
      if (c == 1) {
        base = pos;
        have_dict = 1;
        have_state = 0;
      } else if (!have_dict) {
        goto done;
      }
      if (n > in_len - ip || n > out_len - pos) goto done;
      memcpy(out + pos, in + ip, n);
      ip += n;
      pos += n;
      continue;
    }
    if (c < 0x80 || in_len - ip < 4) goto done;
    size_t unpack = ((size_t)(c & 0x1F) << 16 | (size_t)in[ip] << 8 | in[ip + 1]) + 1;
    size_t pack = ((size_t)in[ip + 2] << 8 | in[ip + 3]) + 1;
    ip += 4;
    unsigned mode = (c >> 5) & 3;  // 0 go on, 1 reset the state, 2 and new props, 3 and the dictionary
    if (mode == 3) {
      base = pos;
      have_dict = 1;
    } else if (!have_dict) {
      goto done;
    }
    if (mode >= 2) {
      if (ip >= in_len || !lz_props(&d, in[ip++], 4)) goto done;
      have_props = 1;
    } else if (!have_props) {
      goto done;
    }
    if (mode >= 1) {
      lz_reset(&d);
      have_state = 1;
    } else if (!have_state) {
      goto done;
    }
    if (unpack > out_len - pos || pack > in_len - ip) goto done;
    rdec r;
    if (!rd_init(&r, in + ip, pack) || lz_run(&d, &r, out, &pos, pos + unpack, base, 0) != 1) goto done;
    ip += pack;
  }
  ok = pos == out_len;
done:
  free(d.lit);
  return ok;
}

// ------------------------------------------------------------------------------------------------
// The range encoder, for literals only
// ------------------------------------------------------------------------------------------------
typedef struct {
  uint8_t *b;
  size_t n, cap;
  int oom;
} buf;

static void put_u8(buf *b, uint8_t v) {
  if (b->oom) return;
  if (b->n == b->cap) {
    size_t cap = b->cap ? b->cap * 2 : 4096;
    uint8_t *nb = realloc(b->b, cap);
    if (!nb) {
      b->oom = 1;
      return;
    }
    b->b = nb;
    b->cap = cap;
  }
  b->b[b->n++] = v;
}

static void put_bytes(buf *b, const uint8_t *p, size_t n) {
  if (b->oom || !n) return;
  if (n > b->cap - b->n) {
    size_t cap = b->cap ? b->cap : 4096;
    while (cap - b->n < n && cap <= SIZE_MAX / 2) cap *= 2;
    uint8_t *nb = cap - b->n < n ? NULL : realloc(b->b, cap);
    if (!nb) {
      b->oom = 1;
      return;
    }
    b->b = nb;
    b->cap = cap;
  }
  memcpy(b->b + b->n, p, n);
  b->n += n;
}

static void put_u32(buf *b, uint32_t v) {
  for (int k = 0; k < 4; k++) put_u8(b, (uint8_t)(v >> (8 * k)));
}

// 7z's number: the first byte's high bits say how many bytes follow
static void put_num(buf *b, uint64_t v) {
  for (int n = 0; n < 8; n++)
    if (v < 1ull << (7 * (n + 1))) {
      put_u8(b, (uint8_t)(((0xFF00u >> n) & 0xFF) | (v >> (8 * n))));
      for (int k = 0; k < n; k++) put_u8(b, (uint8_t)(v >> (8 * k)));
      return;
    }
  put_u8(b, 0xFF);
  for (int k = 0; k < 8; k++) put_u8(b, (uint8_t)(v >> (8 * k)));
}

typedef struct {
  buf *out;
  uint64_t low;
  uint32_t range;
  uint8_t cache;
  uint64_t cache_size;
} renc;

static void re_shift_low(renc *e) {
  if ((uint32_t)e->low < 0xFF000000u || (e->low >> 32) != 0) {
    uint8_t temp = e->cache;
    do {
      put_u8(e->out, (uint8_t)(temp + (uint8_t)(e->low >> 32)));
      temp = 0xFF;
    } while (--e->cache_size != 0);
    e->cache = (uint8_t)((uint32_t)e->low >> 24);
  }
  e->cache_size++;
  e->low = (uint32_t)((uint32_t)e->low << 8);
}

static void re_bit(renc *e, prob *p, unsigned bit) {
  uint32_t bound = (e->range >> PROB_BITS) * *p;
  if (!bit) {
    e->range = bound;
    *p = (prob)(*p + ((PROB_ONE - *p) >> MOVE_BITS));
  } else {
    e->low += bound;
    e->range -= bound;
    *p = (prob)(*p - (*p >> MOVE_BITS));
  }
  while (e->range < TOP) {
    e->range <<= 8;
    re_shift_low(e);
  }
}

uint8_t *rsm_lzma_encode_literals(const uint8_t *data, size_t n, size_t *out_len, uint8_t props[5]) {
  enum { LC = 3, PB = 2 };
  static const uint8_t PROPS[5] = {(PB * 5 + 0) * 9 + LC, 0x00, 0x00, 0x10, 0x00};  // 0x5d, 1 MiB
  prob is_match[STATES << 4], *lit = malloc(((size_t)0x300 << LC) * sizeof *lit);
  buf out = {0};
  if (!lit) return NULL;
  fill(is_match, sizeof is_match / sizeof(prob));
  fill(lit, (size_t)0x300 << LC);
  renc e = {&out, 0, 0xFFFFFFFFu, 0, 1};
  for (size_t i = 0; i < n; i++) {
    re_bit(&e, &is_match[i & ((1u << PB) - 1)], 0);  // state 0: always a literal after a literal
    prob *p = lit + 0x300 * (size_t)((i ? data[i - 1] : 0) >> (8 - LC));
    unsigned m = 1;
    for (int k = 7; k >= 0; k--) {
      unsigned bit = (data[i] >> k) & 1;
      re_bit(&e, &p[m], bit);
      m = m << 1 | bit;
    }
  }
  for (int i = 0; i < 5; i++) re_shift_low(&e);
  free(lit);
  if (out.oom) {
    free(out.b);
    return NULL;
  }
  memcpy(props, PROPS, 5);
  *out_len = out.n;
  return out.b;
}

// ------------------------------------------------------------------------------------------------
// 7z
// ------------------------------------------------------------------------------------------------
static const uint8_t SIG[6] = {'7', 'z', 0xBC, 0xAF, 0x27, 0x1C};
static const uint8_t CODER_COPY[1] = {0x00}, CODER_LZMA[3] = {0x03, 0x01, 0x01}, CODER_LZMA2[1] = {0x21};

enum {
  K_END = 0x00, K_HEADER = 0x01, K_ARCHIVE_PROPS = 0x02, K_ADDITIONAL = 0x03, K_MAIN = 0x04, K_FILES = 0x05,
  K_PACK = 0x06, K_UNPACK = 0x07, K_SUBSTREAMS = 0x08, K_SIZE = 0x09, K_CRC = 0x0A, K_FOLDER = 0x0B,
  K_UNPACK_SIZE = 0x0C, K_NUM_STREAMS = 0x0D, K_EMPTY_STREAM = 0x0E, K_NAMES = 0x11, K_DUMMY = 0x19,
  K_ENCODED_HEADER = 0x17,
};

typedef struct {
  uint8_t coder[4], props[16];
  int coder_len, props_len;
  uint64_t unpack, pack_off, pack_len, streams;
  int has_crc;
  uint32_t crc;
} folder;

typedef struct {
  uint64_t type;
  const uint8_t *body;
  size_t len;
} fprop;

struct rsm_7z {
  const uint8_t *d;
  size_t n;
  folder *folders;
  size_t nfolders;
  char **names;
  int *file_folder;  // -1: an empty file
  size_t nfiles;
  fprop *props;
  size_t nprops;
  uint8_t *header;  // the unpacked header when it was packed (props point into it)
};

typedef struct {
  const uint8_t *b;
  size_t n, i;
  int bad;
} cur;

static uint8_t c_u8(cur *c) {
  if (c->i >= c->n) {
    c->bad = 1;
    return 0;
  }
  return c->b[c->i++];
}

static uint64_t c_num(cur *c) {
  unsigned first = c_u8(c), mask = 0x80;
  uint64_t v = 0;
  for (int k = 0; k < 8; k++) {
    if (!(first & mask)) return v | (uint64_t)(first & (mask - 1)) << (8 * k);
    v |= (uint64_t)c_u8(c) << (8 * k);
    mask >>= 1;
  }
  return v;
}

static uint32_t c_u32(cur *c) {
  uint32_t v = 0;
  for (int k = 0; k < 4; k++) v |= (uint32_t)c_u8(c) << (8 * k);
  return v;
}

static void c_skip(cur *c, uint64_t n) {
  if (n > c->n - c->i) {
    c->bad = 1;
    c->i = c->n;
  } else {
    c->i += (size_t)n;
  }
}

// n bits, after an all-defined byte when `all`; malloc'ed, one byte a bit
static uint8_t *c_bits(cur *c, size_t n, int all) {
  uint8_t *v = calloc(n ? n : 1, 1);
  if (!v) {
    c->bad = 1;
    return NULL;
  }
  if (all && c_u8(c)) {
    memset(v, 1, n);
    return v;
  }
  unsigned byte = 0;
  for (size_t k = 0; k < n; k++) {
    if (k % 8 == 0) byte = c_u8(c);
    v[k] = (byte >> (7 - k % 8)) & 1;
  }
  return v;
}

#define FAIL(...)                                    \
  do {                                               \
    if (why) snprintf(why, why_len, __VA_ARGS__);    \
    goto fail;                                       \
  } while (0)

// Streams info (pack info, folders, substreams) at c into folders; the packed streams start at 32 + their position.
static int parse_streams(cur *c, folder **out, size_t *nout, char *why, size_t why_len) {
  uint64_t pack_pos = 0, *sizes = NULL, npack = 0;
  folder *f = NULL;
  uint64_t nf = 0;
  for (;;) {
    unsigned t = c_u8(c);
    if (c->bad) FAIL("7z: the header ends early");
    if (t == K_END) break;
    if (t == K_PACK) {
      pack_pos = c_num(c);
      npack = c_num(c);
      if (npack > c->n || sizes) FAIL("7z: pack info");
      sizes = calloc(npack ? npack : 1, sizeof *sizes);
      if (!sizes) FAIL("7z: no memory");
      for (;;) {
        unsigned s = c_u8(c);
        if (c->bad) FAIL("7z: pack info");
        if (s == K_END) break;
        if (s == K_SIZE) {
          for (uint64_t i = 0; i < npack; i++) sizes[i] = c_num(c);
        } else if (s == K_CRC) {
          uint8_t *def = c_bits(c, npack, 1);
          for (uint64_t i = 0; def && i < npack; i++)
            if (def[i]) c_u32(c);
          free(def);
        } else {
          FAIL("7z: pack info %u", s);
        }
      }
    } else if (t == K_UNPACK) {
      if (c_u8(c) != K_FOLDER || f) FAIL("7z: folders");
      nf = c_num(c);
      if (nf > c->n || c_u8(c) != 0) FAIL("7z: external folders");
      f = calloc(nf ? nf : 1, sizeof *f);
      if (!f) FAIL("7z: no memory");
      for (uint64_t i = 0; i < nf; i++) {
        if (c_num(c) != 1) FAIL("7z: a folder of more than one coder");
        unsigned flag = c_u8(c);
        if (flag & 0x10) FAIL("7z: a coder of several streams");
        if (flag & 0x80) FAIL("7z: alternative coders");
        f[i].coder_len = flag & 0x0F;
        if (f[i].coder_len > 4) FAIL("7z: coder id");
        for (int k = 0; k < f[i].coder_len; k++) f[i].coder[k] = c_u8(c);
        if (flag & 0x20) {
          uint64_t pl = c_num(c);
          if (pl > sizeof f[i].props) FAIL("7z: coder props");
          f[i].props_len = (int)pl;
          for (int k = 0; k < f[i].props_len; k++) f[i].props[k] = c_u8(c);
        }
        f[i].streams = 1;
      }
      if (c_u8(c) != K_UNPACK_SIZE) FAIL("7z: folder sizes");
      for (uint64_t i = 0; i < nf; i++) f[i].unpack = c_num(c);
      unsigned s = c_u8(c);
      if (s == K_CRC) {
        uint8_t *def = c_bits(c, nf, 1);
        for (uint64_t i = 0; def && i < nf; i++)
          if (def[i]) f[i].has_crc = 1, f[i].crc = c_u32(c);
        free(def);
        s = c_u8(c);
      }
      if (s != K_END) FAIL("7z: unpack info %u", s);
    } else if (t == K_SUBSTREAMS) {
      if (!f) FAIL("7z: substreams before folders");
      for (;;) {
        unsigned s = c_u8(c);
        if (c->bad) FAIL("7z: substreams");
        if (s == K_END) break;
        if (s == K_NUM_STREAMS) {
          // a folder of several files is a solid archive, which this doesn't read: refused here, so the counts below
          // stay small whatever the archive says
          for (uint64_t i = 0; i < nf; i++)
            if ((f[i].streams = c_num(c)) > 1) FAIL("7z: a solid archive");
        } else if (s == K_SIZE) {
          for (uint64_t i = 0; i < nf; i++)
            for (uint64_t k = 1; k < f[i].streams; k++) c_num(c);
        } else if (s == K_CRC) {
          size_t n = 0;
          for (uint64_t i = 0; i < nf; i++) n += f[i].streams == 1 && f[i].has_crc ? 0 : (size_t)f[i].streams;
          uint8_t *def = c_bits(c, n, 1);
          size_t k = 0;
          for (uint64_t i = 0; def && i < nf; i++) {
            if (f[i].streams == 1 && f[i].has_crc) continue;
            for (uint64_t j = 0; j < f[i].streams; j++, k++)
              if (def[k]) {
                uint32_t crc = c_u32(c);
                if (f[i].streams == 1) f[i].has_crc = 1, f[i].crc = crc;
              }
          }
          free(def);
        } else {
          FAIL("7z: substreams %u", s);
        }
      }
    } else {
      FAIL("7z: streams info %u", t);
    }
  }
  if (c->bad) FAIL("7z: the header ends early");
  if (!f || !sizes || npack != nf) FAIL("7z: one packed stream a folder expected");
  for (uint64_t i = 0, off = 32 + pack_pos; i < nf; i++) {
    f[i].pack_off = off;
    f[i].pack_len = sizes[i];
    off += sizes[i];
  }
  free(sizes);
  *out = f;
  *nout = (size_t)nf;
  return 1;
fail:
  free(sizes);
  free(f);
  return 0;
}

static int coder_is(const folder *f, const uint8_t *id, int len) {
  return f->coder_len == len && !memcmp(f->coder, id, (size_t)len);
}

// A folder's data, malloc'ed with a NUL after it
static uint8_t *unpack(const uint8_t *d, size_t n, const folder *f) {
  if (f->pack_off > n || f->pack_len > n - f->pack_off || f->unpack > (1u << 30)) return NULL;
  const uint8_t *in = d + f->pack_off;
  size_t in_len = (size_t)f->pack_len, len = (size_t)f->unpack;
  uint8_t *out = malloc(len + 1);
  if (!out) return NULL;
  int ok = 0;
  if (coder_is(f, CODER_COPY, 1))
    ok = in_len >= len && (memcpy(out, in, len), 1);
  else if (coder_is(f, CODER_LZMA, 3) && f->props_len == 5)
    ok = rsm_lzma_decode(f->props, in, in_len, out, len);
  else if (coder_is(f, CODER_LZMA2, 1) && f->props_len == 1)
    ok = rsm_lzma2_decode(in, in_len, out, len);
  if (ok && f->has_crc && (uint32_t)crc32(0, out, (uInt)len) != f->crc) ok = 0;
  if (!ok) {
    free(out);
    return NULL;
  }
  out[len] = 0;
  return out;
}

// UTF-16LE names, each ending in a 0, as UTF-8 with '/'
static int parse_names(rsm_7z *z, const uint8_t *p, size_t len) {
  size_t i = 0;
  for (size_t f = 0; f < z->nfiles; f++) {
    char name[1024];
    size_t k = 0;
    for (;;) {
      if (i + 2 > len) return 0;
      uint32_t u = p[i] | p[i + 1] << 8;
      i += 2;
      if (!u) break;
      if (u >= 0xD800 && u < 0xDC00 && i + 2 <= len) {
        uint32_t lo = p[i] | p[i + 1] << 8;
        if (lo >= 0xDC00 && lo < 0xE000) {
          u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
          i += 2;
        }
      }
      if (u == '\\') u = '/';
      if (k + 5 >= sizeof name) return 0;
      if (u < 0x80) {
        name[k++] = (char)u;
      } else if (u < 0x800) {
        name[k++] = (char)(0xC0 | u >> 6);
        name[k++] = (char)(0x80 | (u & 0x3F));
      } else if (u < 0x10000) {
        name[k++] = (char)(0xE0 | u >> 12);
        name[k++] = (char)(0x80 | ((u >> 6) & 0x3F));
        name[k++] = (char)(0x80 | (u & 0x3F));
      } else {
        name[k++] = (char)(0xF0 | u >> 18);
        name[k++] = (char)(0x80 | ((u >> 12) & 0x3F));
        name[k++] = (char)(0x80 | ((u >> 6) & 0x3F));
        name[k++] = (char)(0x80 | (u & 0x3F));
      }
    }
    name[k] = 0;
    free(z->names[f]);  // names given twice: the later ones
    if (!(z->names[f] = strdup(name))) return 0;
  }
  return 1;
}

rsm_7z *rsm_7z_open(const uint8_t *data, size_t n, char *why, size_t why_len) {
  rsm_7z *z = calloc(1, sizeof *z);
  uint8_t *empty = NULL;
  if (!z) FAIL("7z: no memory");
  z->d = data;
  z->n = n;
  if (n < 32 || memcmp(data, SIG, 6) || data[6] != 0) FAIL("not a 7z archive");
  uint64_t off = 0, size = 0;
  for (int k = 0; k < 8; k++) off |= (uint64_t)data[12 + k] << (8 * k), size |= (uint64_t)data[20 + k] << (8 * k);
  uint32_t crc = data[28] | data[29] << 8 | data[30] << 16 | (uint32_t)data[31] << 24;
  if (off > n - 32 || size > n - 32 - off) FAIL("7z: the header is past the end");
  if ((uint32_t)crc32(0, data + 32 + off, (uInt)size) != crc) FAIL("7z: the header's CRC");
  cur c = {data + 32 + off, (size_t)size, 0, 0};
  // A packed header may unpack to another packed header; one that unpacks to itself would never end
  for (int depth = 0; c.n && c.b[0] == K_ENCODED_HEADER; depth++) {
    folder *hf = NULL;
    size_t nhf = 0;
    if (depth == 4) FAIL("7z: packed headers inside each other");
    c.i = 1;
    if (!parse_streams(&c, &hf, &nhf, why, why_len)) goto fail;
    uint8_t *h = nhf ? unpack(data, n, &hf[0]) : NULL;
    size_t hlen = nhf ? (size_t)hf[0].unpack : 0;
    free(hf);
    if (!h) FAIL("7z: the packed header can't be unpacked");
    free(z->header);
    z->header = h;
    c = (cur){h, hlen, 0, 0};
  }
  if (c_u8(&c) != K_HEADER) FAIL("7z: no header");
  unsigned t = c_u8(&c);
  if (t == K_ARCHIVE_PROPS) {
    for (;;) {
      uint64_t pt = c_num(&c);
      if (c.bad) FAIL("7z: archive properties");
      if (!pt) break;
      c_skip(&c, c_num(&c));
    }
    t = c_u8(&c);
  }
  if (t == K_ADDITIONAL) FAIL("7z: additional streams");
  if (t == K_MAIN) {
    if (!parse_streams(&c, &z->folders, &z->nfolders, why, why_len)) goto fail;
    t = c_u8(&c);
  }
  if (t == K_FILES) {
    uint64_t nf = c_num(&c);
    if (nf > c.n) FAIL("7z: files");
    z->nfiles = (size_t)nf;
    z->names = calloc(nf ? nf : 1, sizeof *z->names);
    z->file_folder = calloc(nf ? nf : 1, sizeof *z->file_folder);
    if (!z->names || !z->file_folder) FAIL("7z: no memory");
    for (;;) {
      uint64_t pt = c_num(&c);
      if (c.bad) FAIL("7z: file properties");
      if (!pt) break;
      uint64_t len = c_num(&c);
      const uint8_t *body = c.b + c.i;
      c_skip(&c, len);
      if (c.bad) FAIL("7z: file properties");
      fprop *np = realloc(z->props, (z->nprops + 1) * sizeof *np);
      if (!np) FAIL("7z: no memory");
      z->props = np;
      z->props[z->nprops++] = (fprop){pt, body, (size_t)len};
      if (pt == K_EMPTY_STREAM) {
        cur bc = {body, (size_t)len, 0, 0};
        free(empty);
        empty = c_bits(&bc, z->nfiles, 0);
        if (!empty || bc.bad) FAIL("7z: empty streams");
      } else if (pt == K_NAMES) {
        if (!len || body[0] != 0 || !parse_names(z, body + 1, (size_t)len - 1)) FAIL("7z: names");
      }
    }
  }
  size_t k = 0;
  for (size_t i = 0; i < z->nfiles; i++) {
    if (!z->names[i]) FAIL("7z: a file without a name");
    if (empty && empty[i]) {
      z->file_folder[i] = -1;
      continue;
    }
    while (k < z->nfolders && z->folders[k].streams == 0) k++;
    if (k >= z->nfolders) FAIL("7z: more files than folders");
    z->file_folder[i] = (int)k++;
  }
  free(empty);
  return z;
fail:
  free(empty);
  rsm_7z_close(z);
  return NULL;
}

int rsm_7z_files(const rsm_7z *z) { return (int)z->nfiles; }

const char *rsm_7z_name(const rsm_7z *z, int file) {
  return file >= 0 && (size_t)file < z->nfiles ? z->names[file] : NULL;
}

int rsm_7z_find(const rsm_7z *z, const char *name) {
  for (size_t i = 0; i < z->nfiles; i++) {
    const char *a = z->names[i], *b = name;
    for (; *a && *b; a++, b++) {
      char x = *a == '\\' ? '/' : *a, y = *b == '\\' ? '/' : *b;
      if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
      if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
      if (x != y) break;
    }
    if (!*a && !*b) return (int)i;
  }
  return -1;
}

uint8_t *rsm_7z_read(const rsm_7z *z, int file, size_t *len) {
  if (file < 0 || (size_t)file >= z->nfiles) return NULL;
  if (z->file_folder[file] < 0) {
    uint8_t *e = calloc(1, 1);
    if (e) *len = 0;
    return e;
  }
  const folder *f = &z->folders[z->file_folder[file]];
  uint8_t *out = unpack(z->d, z->n, f);
  if (out) *len = (size_t)f->unpack;
  return out;
}

uint8_t *rsm_7z_rewrite(const rsm_7z *z, int n, const int *files, const uint8_t *const *data, const size_t *lens,
                        size_t *out_len) {
  size_t nf = z->nfolders;
  folder *nfo = calloc(nf ? nf : 1, sizeof *nfo);
  uint8_t **packed = calloc(nf ? nf : 1, sizeof *packed);  // a new packed stream, or NULL: the old one
  buf body = {0}, h = {0}, eh = {0}, out = {0};
  uint8_t *hp = NULL, *result = NULL;
  if (!nfo || !packed) goto done;
  memcpy(nfo, z->folders, nf * sizeof *nfo);
  for (int i = 0; i < n; i++) {
    if (files[i] < 0 || (size_t)files[i] >= z->nfiles || z->file_folder[files[i]] < 0) goto done;
    folder *f = &nfo[z->file_folder[files[i]]];
    size_t plen;
    uint8_t props[5];
    uint8_t *p = rsm_lzma_encode_literals(data[i], lens[i], &plen, props);
    if (!p) goto done;
    free(packed[f - nfo]);
    packed[f - nfo] = p;
    memcpy(f->coder, CODER_LZMA, 3);
    f->coder_len = 3;
    memcpy(f->props, props, 5);
    f->props_len = 5;
    f->unpack = lens[i];
    f->pack_len = plen;
    f->has_crc = 1;
    f->crc = (uint32_t)crc32(0, data[i], (uInt)lens[i]);
  }
  for (size_t i = 0; i < nf; i++) {
    folder *f = &nfo[i];
    if (packed[i]) {
      put_bytes(&body, packed[i], (size_t)f->pack_len);
      continue;
    }
    if (f->pack_off > z->n || f->pack_len > z->n - f->pack_off) goto done;
    if (!f->has_crc && f->streams) {  // a file's CRC is always written: one this archive hasn't, from its data
      uint8_t *d = unpack(z->d, z->n, f);
      if (!d) goto done;
      f->crc = (uint32_t)crc32(0, d, (uInt)f->unpack);
      f->has_crc = 1;
      free(d);
    }
    put_bytes(&body, z->d + f->pack_off, (size_t)f->pack_len);
  }
  // The header: the packed streams from 0, one coder a folder, the files' CRCs in the substreams, the files' properties
  // as they were (the same files, in the same order). A folder holds one file unless the header says otherwise: a
  // folder without one (which the archive may have) needs each folder's count, or the files after it would take the
  // wrong folders.
  int one_each = 1;
  for (size_t i = 0; i < nf; i++) one_each &= nfo[i].streams == 1;
  put_u8(&h, K_HEADER);
  put_u8(&h, K_MAIN);
  put_u8(&h, K_PACK);
  put_num(&h, 0);
  put_num(&h, nf);
  put_u8(&h, K_SIZE);
  for (size_t i = 0; i < nf; i++) put_num(&h, nfo[i].pack_len);
  put_u8(&h, K_END);
  put_u8(&h, K_UNPACK);
  put_u8(&h, K_FOLDER);
  put_num(&h, nf);
  put_u8(&h, 0);
  for (size_t i = 0; i < nf; i++) {
    put_num(&h, 1);
    put_u8(&h, (uint8_t)(nfo[i].coder_len | (nfo[i].props_len ? 0x20 : 0)));
    put_bytes(&h, nfo[i].coder, (size_t)nfo[i].coder_len);
    if (nfo[i].props_len) {
      put_num(&h, (uint64_t)nfo[i].props_len);
      put_bytes(&h, nfo[i].props, (size_t)nfo[i].props_len);
    }
  }
  put_u8(&h, K_UNPACK_SIZE);
  for (size_t i = 0; i < nf; i++) put_num(&h, nfo[i].unpack);
  put_u8(&h, K_END);
  put_u8(&h, K_SUBSTREAMS);
  if (!one_each) {
    put_u8(&h, K_NUM_STREAMS);
    for (size_t i = 0; i < nf; i++) put_num(&h, nfo[i].streams);
  }
  put_u8(&h, K_CRC);
  put_u8(&h, 1);
  for (size_t i = 0; i < nf; i++)
    if (nfo[i].streams) put_u32(&h, nfo[i].crc);
  put_u8(&h, K_END);
  put_u8(&h, K_END);
  put_u8(&h, K_FILES);
  put_num(&h, z->nfiles);
  for (size_t i = 0; i < z->nprops; i++) {
    if (z->props[i].type == K_DUMMY) continue;
    put_num(&h, z->props[i].type);
    put_num(&h, z->props[i].len);
    put_bytes(&h, z->props[i].body, z->props[i].len);
  }
  put_u8(&h, K_END);
  put_u8(&h, K_END);
  if (body.oom || h.oom) goto done;
  // packed with LZMA, as the game's are
  size_t hp_len;
  uint8_t hprops[5];
  hp = rsm_lzma_encode_literals(h.b, h.n, &hp_len, hprops);
  if (!hp) goto done;
  put_u8(&eh, K_ENCODED_HEADER);
  put_u8(&eh, K_PACK);
  put_num(&eh, body.n);
  put_num(&eh, 1);
  put_u8(&eh, K_SIZE);
  put_num(&eh, hp_len);
  put_u8(&eh, K_END);
  put_u8(&eh, K_UNPACK);
  put_u8(&eh, K_FOLDER);
  put_num(&eh, 1);
  put_u8(&eh, 0);
  put_num(&eh, 1);
  put_u8(&eh, 0x20 | 3);
  put_bytes(&eh, CODER_LZMA, 3);
  put_num(&eh, 5);
  put_bytes(&eh, hprops, 5);
  put_u8(&eh, K_UNPACK_SIZE);
  put_num(&eh, h.n);
  put_u8(&eh, K_CRC);
  put_u8(&eh, 1);
  put_u32(&eh, (uint32_t)crc32(0, h.b, (uInt)h.n));
  put_u8(&eh, K_END);
  put_u8(&eh, K_END);
  if (eh.oom) goto done;
  uint8_t start[20];
  uint64_t next_off = body.n + hp_len, next_size = eh.n;
  uint32_t next_crc = (uint32_t)crc32(0, eh.b, (uInt)eh.n);
  for (int k = 0; k < 8; k++) start[k] = (uint8_t)(next_off >> (8 * k)), start[8 + k] = (uint8_t)(next_size >> (8 * k));
  for (int k = 0; k < 4; k++) start[16 + k] = (uint8_t)(next_crc >> (8 * k));
  put_bytes(&out, SIG, 6);
  put_u8(&out, 0);
  put_u8(&out, 4);
  put_u32(&out, (uint32_t)crc32(0, start, 20));
  put_bytes(&out, start, 20);
  put_bytes(&out, body.b, body.n);
  put_bytes(&out, hp, hp_len);
  put_bytes(&out, eh.b, eh.n);
  if (out.oom) goto done;
  result = out.b;
  out.b = NULL;
  *out_len = out.n;
done:
  for (size_t i = 0; packed && i < nf; i++) free(packed[i]);
  free(packed);
  free(nfo);
  free(body.b);
  free(h.b);
  free(eh.b);
  free(hp);
  free(out.b);
  return result;
}

void rsm_7z_close(rsm_7z *z) {
  if (!z) return;
  for (size_t i = 0; z->names && i < z->nfiles; i++) free(z->names[i]);
  free(z->names);
  free(z->file_folder);
  free(z->folders);
  free(z->props);
  free(z->header);
  free(z);
}
