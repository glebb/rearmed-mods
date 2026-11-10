// A song's tunings, from its archive. See rsmods_tuning.h.
//
// PSARC (big endian): "PSAR", version, "zlib", the table of contents' end (from the start of the file), an entry's
// size (30), the entries, the block size (65536) and flags (4: the table is encrypted). An entry: MD5, its first block,
// its size (40 bits) and offset (40 bits). After the entries, each block's stored size (2 bytes for 64 KiB blocks):
// 0 is a whole block stored as it is, a block that zlib doesn't shrink is stored as it is too.
#include <CommonCrypto/CommonCryptor.h>
#include <ctype.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <zlib.h>

#include "rsmods_tuning.h"

#define PSARC_ENCRYPTED 4
#define MAX_ENTRIES 200000
#define MAX_FILE (256u << 20)

typedef struct {
  uint32_t block;
  uint64_t size, offset;
} entry;

struct rsm_psarc {
  const rsm_psarc_io *io;
  int fd;
  uint32_t block_size;
  int nentries;
  entry *e;
  uint32_t *zlen;  // each block's stored size
  uint32_t nblocks;
  char **names;    // entries 1..: names[i - 1]
  char *names_text;
};

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint64_t be40(const uint8_t *p) { return (uint64_t)p[0] << 32 | be32(p + 1); }

int rsm_psarc_key_from_hex(const char *hex, uint8_t key[32]) {
  for (int i = 0; i < 32; i++) {
    unsigned v;
    if (!isxdigit((unsigned char)hex[2 * i]) || !isxdigit((unsigned char)hex[2 * i + 1]) || sscanf(hex + 2 * i, "%2x", &v) != 1)
      return 0;
    key[i] = (uint8_t)v;
  }
  return 1;
}

static int decrypt_toc(uint8_t *buf, size_t n, const uint8_t key[32]) {
  static const uint8_t iv[16] = {0};
  size_t whole = (n + 15) & ~(size_t)15, moved = 0;  // CFB: the bytes past n don't change those before
  uint8_t *in = calloc(1, whole), *out = malloc(whole);
  CCCryptorRef c = NULL;
  int ok = in && out &&
           CCCryptorCreateWithMode(kCCDecrypt, kCCModeCFB, kCCAlgorithmAES, ccNoPadding, iv, key, 32, NULL, 0, 0, 0,
                                   &c) == kCCSuccess;
  if (ok) {
    memcpy(in, buf, n);
    ok = CCCryptorUpdate(c, in, whole, out, whole, &moved) == kCCSuccess && moved >= n;
    if (ok) memcpy(buf, out, n);
  }
  if (c) CCCryptorRelease(c);
  free(in);
  free(out);
  return ok;
}

static int plain_open(const char *path) { return open(path, O_RDONLY); }
static const rsm_psarc_io plain_io = {plain_open, pread, close};

static int read_at(const rsm_psarc *a, void *buf, size_t n, uint64_t at) {
  size_t done = 0;
  while (done < n) {
    ssize_t r = a->io->pread(a->fd, (uint8_t *)buf + done, n - done, (off_t)(at + done));
    if (r <= 0) return 0;
    done += (size_t)r;
  }
  return 1;
}

static void *read_entry(rsm_psarc *a, int i, size_t *len) {
  if (i < 0 || i >= a->nentries || a->e[i].size > MAX_FILE) return NULL;
  uint64_t size = a->e[i].size, off = a->e[i].offset, pos = 0;
  uint32_t b = a->e[i].block;
  uint8_t *out = malloc((size_t)size + 1), *in = malloc(a->block_size);
  if (!out || !in) goto fail;
  while (pos < size) {
    if (b >= a->nblocks) goto fail;
    uint32_t stored = a->zlen[b++];
    uint64_t want = size - pos < a->block_size ? size - pos : a->block_size;
    if (!stored) {  // a whole block as it is
      if (!read_at(a, out + pos, (size_t)want, off)) goto fail;
      pos += want;
      off += a->block_size;
      continue;
    }
    if (stored > a->block_size || !read_at(a, in, stored, off)) goto fail;
    off += stored;
    int zlib = stored >= 2 && (in[0] & 0x0f) == 8 && ((unsigned)in[0] << 8 | in[1]) % 31 == 0;
    uLongf got = (uLongf)want;
    if (zlib && uncompress(out + pos, &got, in, stored) == Z_OK && got == want) {
      pos += want;
      continue;
    }
    if (stored != want) goto fail;  // neither zlib nor the block as it is
    memcpy(out + pos, in, stored);
    pos += want;
  }
  free(in);
  out[size] = 0;
  if (len) *len = (size_t)size;
  return out;
fail:
  free(in);
  free(out);
  return NULL;
}

rsm_psarc *rsm_psarc_open(const char *path, const uint8_t key[32]) { return rsm_psarc_open_io(path, key, NULL); }

rsm_psarc *rsm_psarc_open_io(const char *path, const uint8_t key[32], const rsm_psarc_io *io) {
  uint8_t h[32];
  if (!io) io = &plain_io;
  int fd = io->open(path);
  if (fd < 0) return NULL;
  rsm_psarc *a = calloc(1, sizeof *a);
  uint8_t *toc = NULL;
  if (a) {
    a->io = io;
    a->fd = fd;
  }
  if (!a || !read_at(a, h, sizeof h, 0) || memcmp(h, "PSAR", 4) || memcmp(h + 8, "zlib", 4)) goto fail;
  uint32_t toc_end = be32(h + 12), entry_size = be32(h + 16), n = be32(h + 20);
  a->block_size = be32(h + 24);
  uint32_t flags = be32(h + 28);
  if (toc_end <= 32 || toc_end > (64u << 20) || entry_size < 30 || !n || n > MAX_ENTRIES || a->block_size < 256 ||
      a->block_size > (16u << 20) || (uint64_t)n * entry_size > toc_end - 32)
    goto fail;
  size_t toc_len = toc_end - 32;
  toc = malloc(toc_len);
  if (!toc || !read_at(a, toc, toc_len, 32)) goto fail;
  if ((flags & PSARC_ENCRYPTED) && !decrypt_toc(toc, toc_len, key)) goto fail;
  a->nentries = (int)n;
  a->e = calloc(n, sizeof *a->e);
  if (!a->e) goto fail;
  for (uint32_t i = 0; i < n; i++) {
    const uint8_t *p = toc + (size_t)i * entry_size;
    a->e[i] = (entry){be32(p + 16), be40(p + 20), be40(p + 25)};
  }
  int width = a->block_size <= 0x100 ? 1 : a->block_size <= 0x10000 ? 2 : a->block_size <= 0x1000000 ? 3 : 4;
  size_t ztable = toc_len - (size_t)n * entry_size;
  a->nblocks = (uint32_t)(ztable / (size_t)width);
  a->zlen = calloc(a->nblocks ? a->nblocks : 1, sizeof *a->zlen);
  if (!a->zlen) goto fail;
  for (uint32_t k = 0; k < a->nblocks; k++) {
    const uint8_t *p = toc + (size_t)n * entry_size + (size_t)k * (size_t)width;
    uint32_t v = 0;
    for (int j = 0; j < width; j++) v = v << 8 | p[j];
    a->zlen[k] = v;
  }
  free(toc);
  toc = NULL;
  // the names, one a line, of entries 1 on
  size_t len = 0;
  a->names_text = read_entry(a, 0, &len);
  a->names = calloc(n, sizeof *a->names);
  if (!a->names_text || !a->names) goto fail;
  int k = 0;
  for (char *s = a->names_text, *e; s && *s && k < (int)n - 1; s = e) {
    e = strpbrk(s, "\r\n");
    if (e) *e++ = 0;
    if (*s) a->names[k++] = s;
    while (e && (*e == '\r' || *e == '\n')) e++;
  }
  if (k != (int)n - 1) goto fail;  // a name for every file, or the table isn't what it seemed (a wrong key)
  return a;
fail:
  free(toc);
  if (a) {
    a->fd = -1;
    rsm_psarc_close(a);
  }
  io->close(fd);
  return NULL;
}

int rsm_psarc_files(const rsm_psarc *a) { return a->nentries - 1; }
const char *rsm_psarc_name(const rsm_psarc *a, int i) { return i >= 0 && i < a->nentries - 1 ? a->names[i] : NULL; }
void *rsm_psarc_read(rsm_psarc *a, int i, size_t *len) { return read_entry(a, i + 1, len); }

void rsm_psarc_close(rsm_psarc *a) {
  if (!a) return;
  if (a->fd >= 0) a->io->close(a->fd);
  free(a->e);
  free(a->zlen);
  free(a->names);
  free(a->names_text);
  free(a);
}

// ------------------------------------------------------------------------------------------------
// Manifests
// ------------------------------------------------------------------------------------------------
// The value after "key": in json (from `from`), past white space and the colon; NULL if absent
static const char *value_of(const char *json, const char *key) {
  char quoted[64];
  snprintf(quoted, sizeof quoted, "\"%s\"", key);
  const char *p = strstr(json, quoted);
  if (!p) return NULL;
  p += strlen(quoted);
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
  if (*p != ':') return NULL;
  p++;
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
  return p;
}

int rsm_manifest_arrangement(const char *json, rsm_arrangement *out) {
  memset(out, 0, sizeof *out);
  const char *p = value_of(json, "ArrangementName");
  if (p && *p == '"') {
    p++;
    size_t k = 0;
    while (*p && *p != '"' && k + 1 < sizeof out->name) out->name[k++] = *p++;
    out->name[k] = 0;
  }
  const char *t = value_of(json, "Tuning");
  if (!t || *t != '{') return 0;
  const char *end = strchr(t, '}');
  if (!end) return 0;
  for (int i = 0; i < 6; i++) {
    char key[16];
    snprintf(key, sizeof key, "string%d", i);
    const char *v = value_of(t, key);
    if (!v || v > end) return 0;
    char *after;
    long x = strtol(v, &after, 10);
    if (after == v || x < -48 || x > 48) return 0;
    out->tuning[i] = (int)x;
  }
  const char *c = value_of(json, "CentOffset");
  out->cents = c ? strtod(c, NULL) : 0;
  if (!isfinite(out->cents) || fabs(out->cents) > 2400) out->cents = 0;
  return 1;
}

int rsm_is_song_manifest(const char *name, const char *song_key) {
  size_t kl = strlen(song_key), nl = strlen(name);
  if (!kl || strncmp(name, "manifests/", 10) || nl < 5 || strcmp(name + nl - 5, ".json")) return 0;
  const char *base = strrchr(name, '/') + 1;
  if (strncasecmp(base, song_key, kl) || base[kl] != '_') return 0;
  // the rest is the arrangement: letters, then digits (lead, rhythm2), so "<key>_" isn't the start of another key
  const char *r = base + kl + 1;
  if (!isalpha((unsigned char)*r)) return 0;
  while (isalpha((unsigned char)*r)) r++;
  while (isdigit((unsigned char)*r)) r++;
  return !strcmp(r, ".json");
}

int rsm_is_bass(const rsm_arrangement *a) { return !strncasecmp(a->name, "Bass", 4); }

int rsm_extended_range_rule(const rsm_arrangement *a, long at, int drop_tunings, int fix_bass) {
  int t[6];
  memcpy(t, a->tuning, sizeof t);
  int strings = fix_bass && (t[4] == 0 || t[4] == 12) && (t[5] == 0 || t[5] == 12) ? 4 : 6;
  int lowest = t[0];
  for (int i = 1; i < strings; i++)
    if (t[i] < lowest) lowest = t[i];
  if (fix_bass) t[4] = t[5] = t[3];  // a bass charted with the top two strings left blank
  int drop = 1;
  for (int i = 1; i < 6; i++) drop &= t[i] == t[0] + 2;
  if (floor(440.0 * pow(2.0, a->cents / 1200.0)) <= 260) lowest -= 12;  // a bass an octave down (A220)
  if (drop_tunings && drop && lowest <= at) return 1;
  return lowest <= at && (!drop || lowest <= at - 2);
}
