// Seven-String Guitar (seven_headstock.c): the pure part of the headstock; see seven_headstock_build.h. The program's
// interpreter is rs7string's hsbuild.cpp (RSXRH1), its frame rules assetload_paths.cpp, its DXT1 scaling assetload_tex.cpp.
#include "seven_headstock_build.h"

#include <CommonCrypto/CommonDigest.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <zlib.h>

#define MAGIC "RSXRH1\n"
#define MAGIC_LEN 7

uint8_t *hs_package_unpack(const hs_package *pk, size_t *len, const char **why) {
  uint8_t *out = malloc(pk->size);
  if (!out) return *why = "no memory", NULL;
  z_stream z;
  memset(&z, 0, sizeof z);
  if (inflateInit2(&z, 16 + MAX_WBITS) != Z_OK) return free(out), *why = "zlib", NULL;
  z.next_in = (Bytef *)pk->gz;
  z.avail_in = (uInt)pk->gz_len;
  z.next_out = out;
  z.avail_out = (uInt)pk->size;
  int r = inflate(&z, Z_FINISH);
  size_t made = z.total_out;
  inflateEnd(&z);
  uint8_t have[CC_SHA256_DIGEST_LENGTH];
  if (r != Z_STREAM_END || made != pk->size) return free(out), *why = "package unpack", NULL;
  if (CC_SHA256(out, (CC_LONG)made, have), memcmp(have, pk->sha, sizeof have))
    return free(out), *why = "package hash mismatch", NULL;
  *len = made;
  return out;
}

int hs_name_ok(const char *name) {
  size_t n = name ? strlen(name) : 0;
  int nif = n > 9 && !strncmp(name, "rsxr_", 5) && !strcmp(name + n - 4, ".nif");
  int dds = n > 11 && !strncmp(name, "rsxrhs_", 7) && !strcmp(name + n - 4, ".dds");
  if (!nif && !dds) return 0;
  for (size_t i = 0; i + 4 < n; i++) {
    char c = name[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return 0;
  }
  return 1;
}

typedef struct {
  const uint8_t *p;
  size_t n, pos;
  const char *bad;
} hs_reader;

static const uint8_t *take(hs_reader *r, size_t n) {
  if (r->bad) return NULL;
  if (n > r->n - r->pos) {
    r->bad = "truncated package";
    return NULL;
  }
  const uint8_t *q = r->p + r->pos;
  r->pos += n;
  return q;
}
static uint32_t take32(hs_reader *r) {
  const uint8_t *q = take(r, 4);
  return q ? (uint32_t)q[0] | (uint32_t)q[1] << 8 | (uint32_t)q[2] << 16 | (uint32_t)q[3] << 24 : 0;
}
static int nibble(uint8_t c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; }

void hs_free(hs_file *out, int n) {
  for (int i = 0; i < n; i++) {
    free(out[i].data);
    out[i] = (hs_file){{0}, NULL, 0, {0}};
  }
}

const hs_file *hs_find(const hs_file *files, int n, const char *name) {
  for (int i = 0; i < n; i++)
    if (files[i].name[0] && !strcmp(files[i].name, name)) return &files[i];
  return NULL;
}

const char *hs_build(const uint8_t *pkg, size_t pkg_len, const uint8_t *const tpl[2], const size_t tlen[2],
                     unsigned want, hs_file *out, int nout, int *count_out) {
  if (count_out) *count_out = 0;
  memset(out, 0, (size_t)nout * sizeof *out);
  if (pkg_len < MAGIC_LEN || pkg_len > HS_LIMIT || memcmp(pkg, MAGIC, MAGIC_LEN)) return "package magic/size";
  if ((pkg_len - MAGIC_LEN) % 2) return "package hex length";
  size_t n = (pkg_len - MAGIC_LEN) / 2;
  uint8_t *bin = malloc(n);
  if (!bin) return "no memory";
  const char *bad = NULL;
  for (size_t i = 0; i < n && !bad; i++) {
    int hi = nibble(pkg[MAGIC_LEN + 2 * i]), lo = nibble(pkg[MAGIC_LEN + 1 + 2 * i]);
    if (hi < 0 || lo < 0) bad = "package hex digit";
    bin[i] = (uint8_t)(hi << 4 | lo);
  }
  hs_reader r = {bin, n, 0, bad};
  if (take32(&r) != 1 && !r.bad) r.bad = "package version";
  for (int i = 0; i < 2 && !r.bad; i++) {  // the two game files: their SHA-256, or nothing is made
    const uint8_t *h = take(&r, CC_SHA256_DIGEST_LENGTH);
    uint8_t have[CC_SHA256_DIGEST_LENGTH];
    if (!h) break;
    if (!tpl[i] || !tlen[i] || tlen[i] > HS_LIMIT) r.bad = "template size";
    else if (CC_SHA256(tpl[i], (CC_LONG)tlen[i], have), memcmp(h, have, sizeof have)) r.bad = "template hash mismatch";
  }
  uint32_t count = take32(&r);
  if ((!count || count > 64 || (int)count > nout) && !r.bad) r.bad = "artifact count";
  size_t total = 0;
  for (uint32_t i = 0; i < count && !r.bad; i++) {
    uint32_t namelen = take32(&r);
    if (namelen >= HS_NAME_MAX && !r.bad) r.bad = "artifact name length";
    const uint8_t *np = take(&r, namelen);
    char name[HS_NAME_MAX] = "";
    if (np && !r.bad) {
      memcpy(name, np, namelen);
      name[namelen] = 0;
      if (!hs_name_ok(name)) r.bad = "artifact name";
      for (uint32_t k = 0; k < i && !r.bad; k++)
        if (!strcmp(out[k].name, name)) r.bad = "artifact name repeated";
    }
    uint32_t size = take32(&r);
    if (!r.bad && (!size || size > HS_LIMIT - total)) r.bad = "artifact size";
    total += size;
    const uint8_t *hash = take(&r, CC_SHA256_DIGEST_LENGTH);
    uint32_t nops = take32(&r);
    if (!r.bad && (!nops || nops > size || nops > (r.n - r.pos) / 5)) r.bad = "instruction count";
    int asked = want >> i & 1;
    uint8_t *o = !r.bad && asked ? malloc(size) : NULL;
    if (!r.bad && asked && !o) r.bad = "no memory";
    size_t made = 0;
    for (uint32_t op = 0; op < nops && !r.bad; op++) {
      const uint8_t *tag = take(&r, 1), *from = NULL;
      uint32_t k = 0;
      if (!tag) break;
      if (*tag == 0) {
        k = take32(&r);
        from = take(&r, k);
      } else if (*tag == 1) {
        uint32_t tid = take32(&r), at = take32(&r);
        k = take32(&r);
        if (tid > 1 || at > HS_LIMIT || k > HS_LIMIT - at || at > tlen[tid] || k > tlen[tid] - at)
          r.bad = "copy range";
        else
          from = tpl[tid] + at;
      } else {
        r.bad = "instruction tag";
      }
      if (!r.bad && (!k || k > size - made)) r.bad = "instruction output size";
      if (!r.bad && o) memcpy(o + made, from, k);
      made += k;
    }
    if (!r.bad && made != size) r.bad = "output size mismatch";
    if (!r.bad && o) {
      uint8_t have[CC_SHA256_DIGEST_LENGTH];
      CC_SHA256(o, size, have);
      if (memcmp(have, hash, sizeof have)) r.bad = "output hash mismatch";
    }
    if (!r.bad) {
      memcpy(out[i].name, name, namelen + 1);
      out[i].size = size;
      memcpy(out[i].sha, hash, sizeof out[i].sha);
      out[i].data = o;
      o = NULL;
    }
    free(o);
  }
  if (!r.bad && r.pos != r.n) r.bad = "trailing package bytes";
  free(bin);
  if (r.bad) {
    hs_free(out, nout);
    return r.bad;
  }
  if (count_out) *count_out = (int)count;
  return NULL;
}

// ---- the wood
static uint16_t dim565(uint16_t c, float k) {
  int r = (int)((c >> 11) * k + 0.5f), g = (int)(((c >> 5) & 63) * k + 0.5f), b = (int)((c & 31) * k + 0.5f);
  return (uint16_t)(r << 11 | g << 5 | b);
}
int hs_scale_dxt1(uint8_t *dds, size_t size, float k) {
  if (!dds || size < 128 + 8 || !(k > 0.0f && k < 1.0f) || memcmp(dds, "DDS ", 4) || memcmp(dds + 84, "DXT1", 4))
    return 0;
  for (size_t o = 128; o + 8 <= size; o += 8) {
    uint8_t *b = dds + o;
    uint16_t c0 = (uint16_t)(b[0] | b[1] << 8), c1 = (uint16_t)(b[2] | b[3] << 8);
    uint16_t n0 = dim565(c0, k), n1 = dim565(c1, k);
    uint32_t idx = (uint32_t)b[4] | (uint32_t)b[5] << 8 | (uint32_t)b[6] << 16 | (uint32_t)b[7] << 24;
    int four = c0 > c1;
    if (four && n0 == n1) {
      idx = 0;  // one colour left: every texel c0 (index 3 would be black in 3-colour mode)
    } else if ((n0 > n1) != four) {
      uint16_t t = n0;
      n0 = n1;
      n1 = t;
      uint32_t remap = 0;
      for (int i = 0; i < 16; i++) {
        uint32_t v = idx >> (2 * i) & 3;
        v = v < 2 ? v ^ 1 : four ? v ^ 1 : v;
        remap |= v << (2 * i);
      }
      idx = remap;
    }
    b[0] = (uint8_t)n0, b[1] = (uint8_t)(n0 >> 8), b[2] = (uint8_t)n1, b[3] = (uint8_t)(n1 >> 8);
    for (int i = 0; i < 4; i++) b[4 + i] = (uint8_t)(idx >> (8 * i));
  }
  return 1;
}

int hs_is_wood_colour(const char *name) {
  return strcasestr(name, "wood") && !strcasestr(name, "gloss") && !strcasestr(name, "_n.") && !strcasestr(name, "normal");
}

// ---- texture names in a nif
int hs_dds_names(const uint8_t *buf, size_t size, char names[][64], int max) {
  int n = 0;
#define TOK(c) (((c) >= 'a' && (c) <= 'z') || ((c) >= 'A' && (c) <= 'Z') || ((c) >= '0' && (c) <= '9') || (c) == '_' || \
                (c) == '-' || (c) == '.' || (c) == '/' || (c) == '\\')
  for (size_t i = 0; buf && i + 4 <= size; i++) {
    if (buf[i] != '.' || (buf[i + 1] | 0x20) != 'd' || (buf[i + 2] | 0x20) != 'd' || (buf[i + 3] | 0x20) != 's') continue;
    if (i + 4 < size && TOK(buf[i + 4]) && buf[i + 4] != '/' && buf[i + 4] != '\\') continue;
    size_t b = i, base;
    while (b > 0 && TOK(buf[b - 1]) && i - b < 200) b--;
    base = b;
    for (size_t k = b; k < i; k++)
      if (buf[k] == '/' || buf[k] == '\\') base = k + 1;
    if (base == i || i + 4 - base >= 64) continue;
    char nm[64];
    memcpy(nm, buf + base, i + 4 - base);
    nm[i + 4 - base] = 0;
    int dup = 0;
    for (int k = 0; k < n && !dup; k++) dup = !strcasecmp(names[k], nm);
    if (!dup && n < max) strcpy(names[n++], nm);
  }
#undef TOK
  return n;
}
int hs_name_in(const char *nm, char names[][64], int n) {
  for (int i = 0; i < n; i++)
    if (!strcasecmp(names[i], nm)) return 1;
  return 0;
}

// ---- frames
static const struct {
  int kind;
  const char *folder;
} FOLDERS[] = {
    {HS_FRAME_GUITAR_INLINE, "/gameplay/guitar_standard_inline/"},
    {HS_FRAME_GUITAR_33, "/gameplay/guitar_standard/"},
    {HS_FRAME_BASS_INLINE, "/gameplay/guitar_bass_inline/"},
    {HS_FRAME_BASS_22, "/gameplay/guitar_bass/"},
};

hs_frame hs_frame_of(const char *name) {
  hs_frame none = {HS_FRAME_NONE, 0, 0, ""};
  char low[256];
  size_t len = name ? strlen(name) : 0;
  if (len < 5 || len >= sizeof low || strcasecmp(name + len - 4, ".nif")) return none;
  for (size_t i = 0; i <= len; i++) {
    char c = name[i];
    low[i] = c >= 'A' && c <= 'Z' ? (char)(c + 32) : c == '\\' ? '/' : c;
  }
  char *slash = strrchr(low, '/');
  if (!slash || slash[1] == '.') return none;
  size_t blen = (size_t)(low + len - 4 - slash - 1);
  if (!blen || blen >= sizeof none.base) return none;
  hs_frame f = none;
  memcpy(f.base, slash + 1, blen);
  f.base[blen] = 0;
  f.lh = blen >= 3 && !strcmp(f.base + blen - 3, "_lh");
  for (size_t i = 0; i < sizeof FOLDERS / sizeof *FOLDERS; i++) {
    size_t fl = strlen(FOLDERS[i].folder);
    const char *at = strstr(low, FOLDERS[i].folder);
    if (!at || at + fl - 1 != slash) continue;
    f.kind = FOLDERS[i].kind;
    break;
  }
  if (f.kind == HS_FRAME_BASS_INLINE || f.kind == HS_FRAME_BASS_22) {
    if (strncmp(f.base, "bass_", 5)) f.kind = HS_FRAME_NONE;  // the folder's pegs and other files
  } else if (f.kind == HS_FRAME_GUITAR_33) {
    f.peg = !strcmp(f.base, "peg_bakelite");
  }
  return f.kind == HS_FRAME_NONE ? none : f;
}

void hs_width_matrix(const float in[9], float k, float out[9]) {
  for (int i = 0; i < 9; i++) out[i] = in[i];
  out[2] *= k;
  out[5] *= k;
  out[8] *= k;
}
