// Seven-String Guitar (seven.h): the headstock part, a headstock with seven tuning pegs on a guitar built with seven
// strings. Ported from rs7string (github.com/SomeeName/rs7string, MIT, by SomeeName): src/features/headstock
// (assetload*, hsbuild*, headstockread, the model's half of hooks*), its ARCHITECTURE section 5 ("Headstock model",
// "Peg and knob tint"). The model's data is rs7string's: data/rs7string/headstock (its data/headstock, MIT, with its
// LICENSE.md), compiled into the library gzipped and unpacked when the model is built, each checked against
// rs7string's SHA-256 of it.
//
// The model. rs7string's brandless in-line seven-peg headstock ("Facet") is not shipped as a .nif: rs7string ships a
// copy/insert program (RS7H1: hex text; a version, the SHA-256 of the two game files it is made from, then each output
// as literal runs and copied ranges of those files, with its size and SHA-256) and the program is run on the player's
// own game files. Here the library reads those two files from the game's own archive (base/
// dependency_guitarstandardinlinedrh.psarc: epi_wilshire_cherry.nif and epi_whilshire_peg.nif; the left-handed frame
// epi_whilshire_cherry_lh.nif from ...inlinedlh.psarc, with the left-handed program), refuses them unless their SHA-256
// is the program's, and builds in memory, each output checked against its SHA-256: rs7_inline7.nif (or
// rs7_inline7_lh.nif) and rs7string's six procedural textures rs7hs_*.dds. Nothing is written to the game folder.
//
// Loading (rs7string's AL1). The guitar's frame is loaded once, at the start, by the game's nif handler (on a loader
// thread) and every guitar's frame is a clone of it. Where the handler has loaded a frame of either headstock option
// (in-line: guitar_standard_inline/, 3+3: guitar_standard/; right- or left-handed), seven_al1 loads the model with the
// same handler under the same name, renames its top node rs7_alt, culls it and attaches it as the last child of the
// stock top node: the stock part comes first, so the game's lookups by name (string_size_%d, Locator_Peg_%d) still
// find the stock nodes, and Locator_Peg_7, which only the model has, is found in it. Before that the model's textures
// that the stock frame doesn't name go to the game's dds handler as the files of the stock folder, from memory (a
// memory file and a NiFile adapter of the game's own classes, made here as the game's code makes them: their ctor
// takes its arguments in registers). On the 3+3 option the pegs are peg_bakelite.nif: the in-line peg (and its
// wilshire_hardware.dds) is merged into it the same way.
//
// A guitar built with seven strings (every frame, seven_n5): rs7_alt is shown and its stock siblings culled; the six
// segment params of the headstock's strings (Rnd_Headstock +0x20) point at the model's string_size_1..6 (values carried
// over), and string_size_7 gets params of its own, whose colour the game's segment loop (GSHC) writes for string 6 and
// whose draw layer is string_size_6's; the PegSet's locators of pegs 1-6 move to the model's Locator_Peg_1..6 (or the
// Invert ones) and the game's placement puts the pegs there. Its PegSet loads seven pegs (seven_psl1, the strings part
// asks seven_headstock_pegs), so the game's binder gives peg 7 the model's Locator_Peg_7, and every stock loop over the
// PegSet's count places, layers, fades and colours it. On a 3+3 frame each peg's clone shows the in-line peg, and the
// tuner letters follow the in-line rule (LT1). Guitars built with six strings and bass keep the stock headstock.
//
// What is not ported: rs7string's other model files ([assets] headstock7_file, the edited-file rule dir/suffix), the
// files it writes and reuses (the build runs at every start, in about a tenth of a second), the 8-string model, the
// debug experiments ([debug] head_*), the overlay, SEH (reads go through pointers the game keeps). Its raise of peg 7
// (a 7-peg PegSet on every guitar, peg 7 shown with a fade when the guitar shows the 7th string) isn't needed: a
// guitar's string count is fixed for its life here (C-lite), so its PegSet loads seven from the start.
//
// Settings, [Seven String] in RSMods.ini, read at the start: Headstock7 = on|off (default on; off: the stock six-peg
// headstock, and a 7-string guitar has six pegs), Headstock7Wood = the model's wood brightness, 0.05-1 (default 0.6,
// rs7string's headstock7_wood: the game shows the texture unlit, so its DXT1 end points are darkened).
#include <CommonCrypto/CommonDigest.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <zlib.h>

#include "rsmods_tuning.h"
#include "seven.h"

#define SECTION "Seven String"
#define ALT_NAME "rs7_alt"
#define SEGS 6         // the headstock's string segments (string_size_1..6) and their params
#define SEG_FIELDS 9   // a segment's params: its shader's nine values
#define COLOUR_FIELD 3 // g_diffuseColor, RGBA; the others are one float each
#define HEADS 8        // guitars followed at a time
#define WOOD_DEFAULT 0.6f

#define PTR(f, name) ((uint8_t *)(uintptr_t)SEVEN_GET(f, name))
#define FIELD(p, name) ((uint8_t *)(p) + RC_OFF(name))
#define U8(p, name) RC_LD(uint8_t, FIELD(p, name))
#define U16(p, name) RC_LD(uint16_t, FIELD(p, name))

// ---- the log: at most this many lines of this part a run
static int g_logs = 80;

static void hlog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void hlog(const char *fmt, ...) {
  if (__atomic_sub_fetch(&g_logs, 1, __ATOMIC_RELAXED) < 0) return;
  char buf[320];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  rsm_log("seven-string: headstock: %s", buf);
}
static int once(int *flag) { return !__atomic_exchange_n(flag, 1, __ATOMIC_RELAXED); }

// ---- settings
static int g_on = 1;
static float g_wood = WOOD_DEFAULT;

static void read_settings(void) {
  const char *on = rsm_get(SECTION, "Headstock7"), *wood = rsm_get(SECTION, "Headstock7Wood");
  g_on = !on || strcasecmp(on, "off");
  if (wood && *wood) {
    char *end;
    double v = strtod(wood, &end);
    while (*end == ' ' || *end == '\t') end++;
    if (end != wood && !*end && v >= 0.05 && v <= 1.0)
      g_wood = (float)v;
    else
      hlog("[%s] Headstock7Wood = \"%s\" is not a number in 0.05-1: %.2f instead", SECTION, wood, WOOD_DEFAULT);
  }
}

// ---- guest memory
static uint64_t arg(const void *p) { return (uint64_t)(uintptr_t)p; }
// The guest pointer at base + off as a host pointer, NULL for none
static uint8_t *ptr_at(const void *base, int64_t off) { return base ? rc_mod_ptr((const uint8_t *)base + off) : NULL; }
// Stores a host pointer into the game's memory at `at` as the guest's pointer
static void ptr_put(uint8_t *at, const void *host) {
  uint64_t g = RC_GUEST(host);
  memcpy(at, &g, (size_t)rc_guest_ptr_size);
}
// The game's reference count of an object (NiRefObject, the IFile): the object stays alive here, no destructor runs
static void addref(uint8_t *obj) {
  if (obj) __atomic_add_fetch((int32_t *)(void *)FIELD(obj, ni_ref_count), 1, __ATOMIC_SEQ_CST);
}
static void dropref(uint8_t *obj) {
  if (obj) __atomic_sub_fetch((int32_t *)(void *)FIELD(obj, ni_ref_count), 1, __ATOMIC_SEQ_CST);
}
// A string the game can be given, made once per text (the names are a few constants)
static const char *gstr(const char *text) {
  static struct {
    const char *text;
    char *guest;
  } made[16];
  static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
  char *g = NULL;
  pthread_mutex_lock(&lock);
  for (int i = 0; i < 16 && !g; i++)
    if (made[i].text == text) g = made[i].guest;
  for (int i = 0; i < 16 && !g; i++)
    if (!made[i].text && (g = rc_guest_alloc(strlen(text) + 1))) {
      strcpy(g, text);
      made[i].text = text;
      made[i].guest = g;
    }
  pthread_mutex_unlock(&lock);
  return g;
}
// One buffer in the game's memory for what goes to its handlers (one at a time, under g_lock): the largest so far
static uint8_t *guest_buffer(size_t n) {
  static uint8_t *buf;
  static size_t cap;
  if (n > cap) {
    buf = rc_guest_alloc(n);
    cap = buf ? n : 0;
  }
  return buf;
}

// ==== the model: rs7string's RS7H1 program (hsbuild.cpp) =========================================================
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"
static const uint8_t PACKAGE_GZ[] = {
#embed "../data/rs7string/headstock/rs7string_headstock.bin.gz"
};
static const uint8_t PACKAGE_LH_GZ[] = {
#embed "../data/rs7string/headstock/rs7string_headstock_lh.bin.gz"
};
#pragma clang diagnostic pop

#define PACKAGE_LIMIT (64u << 20)

// rs7string's two programs as it ships them (data/headstock, the `package` entries of its audit.json): their size
// and SHA-256, which the unpacked copy must have
typedef struct {
  const uint8_t *gz;
  size_t gz_len;
  size_t size;
  uint8_t sha[CC_SHA256_DIGEST_LENGTH];
} package_t;
static const package_t PKG_RH = {PACKAGE_GZ, sizeof PACKAGE_GZ, 14873158,
                                 {0x8f, 0x5b, 0xa0, 0xcb, 0xf6, 0x9d, 0xe3, 0xf0, 0x1b, 0x63, 0xd4, 0x5e, 0x13, 0x53, 0x65, 0xdf,
                                  0xc5, 0x86, 0x34, 0x1e, 0x6e, 0x8e, 0x7c, 0xab, 0xbe, 0x82, 0xf5, 0xd6, 0xf5, 0x6a, 0xad, 0x69}};
static const package_t PKG_LH = {PACKAGE_LH_GZ, sizeof PACKAGE_LH_GZ, 2863876,
                                 {0xe4, 0x9b, 0x71, 0x8c, 0x20, 0xb1, 0x96, 0x95, 0xa9, 0xae, 0x94, 0x2f, 0x99, 0x42, 0xf1, 0x75,
                                  0x92, 0xd6, 0x9f, 0x7e, 0x5b, 0xc5, 0x42, 0xdc, 0x83, 0x9e, 0x21, 0xed, 0xab, 0xf5, 0xa0, 0x7e}};

// A program unpacked (malloc'ed, *len its size), or NULL with the reason in *why: the gzip stream must unpack to
// exactly the program's size and SHA-256
static uint8_t *package_unpack(const package_t *pk, size_t *len, const char **why) {
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
// The outputs of each program, in its order (rs7string's kNames / kNamesLh)
static const char *const OUT_RH[] = {"rs7_inline7.nif",    "rs7_inline8.nif",          "rs7_peg.nif",
                                     "rs7hs_env.dds",      "rs7hs_flat_n.dds",         "rs7hs_hardware.dds",
                                     "rs7hs_hardware_gloss.dds", "rs7hs_wood.dds", "rs7hs_wood_gloss.dds"};
static const char *const OUT_LH[] = {"rs7_inline7_lh.nif"};
#define NOUT_RH (int)(sizeof OUT_RH / sizeof *OUT_RH)
#define NOUT_LH (int)(sizeof OUT_LH / sizeof *OUT_LH)

typedef struct {
  const char *name;
  uint8_t *data;
  uint32_t size;
} hs_file;

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

// Runs the program on the two game files: the outputs whose bit is in `want` (malloc'ed, each checked against its
// SHA-256) in out[], indexed as the program has them. NULL, or why it refused: then nothing in out[].
static const char *hs_build(const uint8_t *pkg, size_t pkg_len, const uint8_t *const tpl[2], const size_t tlen[2],
                            unsigned want, hs_file *out, int nout) {
  if (pkg_len < 6 || pkg_len > PACKAGE_LIMIT || memcmp(pkg, "RS7H1\n", 6)) return "package magic/size";
  if ((pkg_len - 6) % 2) return "package hex length";
  size_t n = (pkg_len - 6) / 2;
  uint8_t *bin = malloc(n);
  if (!bin) return "no memory";
  const char *bad = NULL;
  for (size_t i = 0; i < n && !bad; i++) {
    int hi = nibble(pkg[6 + 2 * i]), lo = nibble(pkg[7 + 2 * i]);
    if (hi < 0 || lo < 0) bad = "package hex digit";
    bin[i] = (uint8_t)(hi << 4 | lo);
  }
  hs_reader r = {bin, n, 0, bad};
  memset(out, 0, (size_t)nout * sizeof *out);
  if (take32(&r) != 1 && !r.bad) r.bad = "package version";
  for (int i = 0; i < 2 && !r.bad; i++) {  // the two game files: their SHA-256, or nothing is made
    const uint8_t *h = take(&r, CC_SHA256_DIGEST_LENGTH);
    uint8_t have[CC_SHA256_DIGEST_LENGTH];
    if (!h) break;
    if (!tpl[i] || !tlen[i] || tlen[i] > PACKAGE_LIMIT) r.bad = "template size";
    else if (CC_SHA256(tpl[i], (CC_LONG)tlen[i], have), memcmp(h, have, sizeof have)) r.bad = "template hash mismatch";
  }
  uint32_t count = take32(&r);
  const char *const *names = count == NOUT_RH ? OUT_RH : count == NOUT_LH ? OUT_LH : NULL;
  if (!names && !r.bad) r.bad = "artifact count";
  size_t total = 0;
  for (uint32_t i = 0; i < count && !r.bad; i++) {
    uint32_t namelen = take32(&r);
    if (namelen > 64) r.bad = "artifact name length";
    const uint8_t *np = take(&r, namelen);
    if (np && (strlen(names[i]) != namelen || memcmp(np, names[i], namelen))) r.bad = "artifact name/order";
    uint32_t size = take32(&r);
    if (!r.bad && (!size || size > PACKAGE_LIMIT - total)) r.bad = "artifact size";
    total += size;
    const uint8_t *hash = take(&r, CC_SHA256_DIGEST_LENGTH);
    uint32_t nops = take32(&r);
    if (!r.bad && (!nops || nops > size || nops > (r.n - r.pos) / 5)) r.bad = "instruction count";
    uint8_t *o = !r.bad && (int)i < nout && (want >> i & 1) ? malloc(size) : NULL;
    if (!r.bad && (int)i < nout && (want >> i & 1) && !o) r.bad = "no memory";
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
        if (tid > 1 || at > PACKAGE_LIMIT || k > PACKAGE_LIMIT - at || at > tlen[tid] || k > tlen[tid] - at)
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
    if (o && !r.bad) out[i] = (hs_file){names[i], o, size};
    else free(o);
  }
  if (!r.bad && r.pos != r.n) r.bad = "trailing package bytes";
  free(bin);
  if (r.bad)
    for (int i = 0; i < nout; i++) {
      free(out[i].data);
      out[i] = (hs_file){0};
    }
  return r.bad;
}

// ---- the game files the program runs on, from the game's own archives (rs7string's headstockread.cpp, which reads
// them through the game's file system; the library reads the archive itself, as mod_cache.c reads cache.psarc)
#define ARCHIVE_RH "base/dependency_guitarstandardinlinedrh.psarc"
#define ARCHIVE_LH "base/dependency_guitarstandardinlinedlh.psarc"
#define FOLDER_INLINE "assets/gameplay/guitar_standard_inline/"

// The named files of one archive (malloc'ed, out[i] for names[i]); 1 when all were read
static int archive_files(const char *psarc, const char *const *names, int n, uint8_t **out, size_t *len) {
  uint8_t key[32];
  uint64_t a = RC_ADDR(key_psarc_hex);
  for (int i = 0; i < n; i++) out[i] = NULL;
  if (!a || !rsm_psarc_key_from_hex((const char *)(uintptr_t)RC_A(a), key)) return 0;
  rsm_psarc *p = rsm_psarc_open(psarc, key);
  int got = 0;
  for (int f = 0; p && f < rsm_psarc_files(p); f++)
    for (int i = 0; i < n; i++)
      if (!out[i] && !strcasecmp(rsm_psarc_name(p, f), names[i]) && (out[i] = rsm_psarc_read(p, f, &len[i]))) got++;
  if (p) rsm_psarc_close(p);
  if (got == n) return 1;
  for (int i = 0; i < n; i++) free(out[i]);
  return 0;
}

// The built files, kept for the run (every frame of a layout and hand may be loaded again)
static hs_file g_rh[NOUT_RH], g_lh[NOUT_LH];
static int g_built_rh, g_built_lh;  // 0 not tried, 1 built, -1 refused or failed

// Darkens a DXT1 (BC1) .dds in place by k (0 < k < 1): both RGB565 end points of every block are scaled; a block whose
// end-point order (4-colour c0 > c1, 3-colour c0 <= c1) would flip gets them swapped and its indices remapped, so no
// block changes mode (rs7string's Rs7Asset_ScaleDxt1). 0 for anything but a plain DXT1 .dds.
static uint16_t dim565(uint16_t c, float k) {
  int r = (int)((c >> 11) * k + 0.5f), g = (int)(((c >> 5) & 63) * k + 0.5f), b = (int)((c & 31) * k + 0.5f);
  return (uint16_t)(r << 11 | g << 5 | b);
}
static int scale_dxt1(uint8_t *dds, size_t size, float k) {
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

// The base colour of the wood (rs7string's IsWoodColour: "wood" in the name, not a gloss or normal map)
static int is_wood_colour(const char *name) {
  return strcasestr(name, "wood") && !strcasestr(name, "gloss") && !strcasestr(name, "_n.") && !strcasestr(name, "normal");
}

// The right-handed program: rs7_inline7.nif and the six textures (the left-handed model names them too)
static int build_rh(void) {
  if (g_built_rh) return g_built_rh > 0;
  static const char *const files[] = {FOLDER_INLINE "epi_wilshire_cherry.nif", FOLDER_INLINE "epi_whilshire_peg.nif"};
  uint8_t *tpl[2];
  size_t len[2];
  g_built_rh = -1;
  if (!archive_files(ARCHIVE_RH, files, 2, tpl, len)) {
    hlog("%s or %s can't be read from %s: the stock six-peg headstock stays", files[0], files[1], ARCHIVE_RH);
    return 0;
  }
  unsigned want = 1u << 0;  // rs7_inline7.nif
  for (int i = 3; i < NOUT_RH; i++) want |= 1u << i;  // rs7hs_*.dds
  const uint8_t *const t[2] = {tpl[0], tpl[1]};
  const size_t l[2] = {len[0], len[1]};
  size_t plen = 0;
  const char *why = NULL;
  uint8_t *pkg = package_unpack(&PKG_RH, &plen, &why);
  if (pkg) why = hs_build(pkg, plen, t, l, want, g_rh, NOUT_RH);
  free(pkg);
  free(tpl[0]);
  free(tpl[1]);
  if (why) {
    hlog("rs7string's headstock program refused the game's files (%s): the stock six-peg headstock stays", why);
    return 0;
  }
  for (int i = 0; i < NOUT_RH; i++)
    if (g_rh[i].data && g_wood < 1.0f && is_wood_colour(g_rh[i].name))
      hlog("%s: wood brightness %.2f %s", g_rh[i].name, g_wood,
           scale_dxt1(g_rh[i].data, g_rh[i].size, g_wood) ? "applied" : "not applied (not a plain DXT1 .dds)");
  g_built_rh = 1;
  hlog("built from the game's own files: %s (%u bytes) and six textures", g_rh[0].name, g_rh[0].size);
  return 1;
}

static int build_lh(void) {
  if (g_built_lh) return g_built_lh > 0;
  static const char *const files[] = {FOLDER_INLINE "epi_whilshire_cherry_lh.nif", FOLDER_INLINE "epi_whilshire_peg.nif"};
  uint8_t *tpl[2];
  size_t len[2];
  g_built_lh = -1;
  if (!archive_files(ARCHIVE_LH, files, 2, tpl, len)) {
    hlog("%s can't be read from %s: the left-handed frame stays stock", files[0], ARCHIVE_LH);
    return 0;
  }
  const uint8_t *const t[2] = {tpl[0], tpl[1]};
  const size_t l[2] = {len[0], len[1]};
  size_t plen = 0;
  const char *why = NULL;
  uint8_t *pkg = package_unpack(&PKG_LH, &plen, &why);
  if (pkg) why = hs_build(pkg, plen, t, l, 1u, g_lh, NOUT_LH);
  free(pkg);
  free(tpl[0]);
  free(tpl[1]);
  if (why) {
    hlog("rs7string's left-handed program refused the game's files (%s): the left-handed frame stays stock", why);
    return 0;
  }
  g_built_lh = 1;
  hlog("built from the game's own files: %s (%u bytes)", g_lh[0].name, g_lh[0].size);
  return 1;
}

// ==== the scene graph ============================================================================================
static const char *node_name(const uint8_t *obj) {
  const char *s = obj ? (const char *)ptr_at(obj, RC_OFF(ni_name)) : NULL;
  return s ? s : "";
}
static int culled(const uint8_t *obj) { return obj && (U16(obj, ni_flags) & 1); }
static void set_culled(uint8_t *obj, int on) {
  if (!obj) return;
  uint16_t f = U16(obj, ni_flags), want = on ? (uint16_t)(f | 1) : (uint16_t)(f & ~1);
  if (want != f) RC_ST(uint16_t, FIELD(obj, ni_flags), want);
}

// The object's NiRTTI: its GetRTTI is `mov eax, rtti; ret` (read from the code), else called
static const uint8_t *rtti_of(const uint8_t *obj) {
  const uint8_t *vt = ptr_at(obj, 0);
  const uint8_t *fn = vt ? ptr_at(vt, RC_SLOT(ni_get_rtti) * rc_guest_ptr_size) : NULL;
  if (!fn) return NULL;
  if (rc_guest_ptr_size == 4 && fn[0] == 0xb8 && fn[5] == 0xc3) return RC_HOST(RC_LD(uint32_t, fn + 1));
  return RC_HOST(OV_VCALL(arg(obj), (int)RC_SLOT(ni_get_rtti)));
}
// Whether obj's class is `kind` or derives from it
static int kind_of(const uint8_t *obj, const char *kind) {
  const uint8_t *r = obj ? rtti_of(obj) : NULL;
  for (int depth = 0; r && depth < 16; depth++) {
    const char *name = (const char *)ptr_at(r, 0);
    if (name && !strcmp(name, kind)) return 1;
    r = ptr_at(r, rc_guest_ptr_size);
  }
  return 0;
}
static int node_or_mesh(const uint8_t *obj) { return kind_of(obj, "NiNode") || kind_of(obj, "NiMesh"); }

// An NiNode's children: count and child i (NULL for an empty slot)
static unsigned child_slots(const uint8_t *node) { return node ? U16(node, ni_node_child_slots) : 0; }
static uint8_t *child(const uint8_t *node, unsigned i) {
  return ptr_at(ptr_at(node, RC_OFF(ni_node_children)), (int64_t)i * rc_guest_ptr_size);
}

// The node called `want` below root (root included): depth-first, at most 256 nodes (rs7string's FindByName)
static uint8_t *find_named(uint8_t *root, const char *want) {
  uint8_t *stack[64];
  int sp = 0, seen = 0;
  if (root) stack[sp++] = root;
  while (sp && seen++ < 256) {
    uint8_t *n = stack[--sp];
    if (!strcmp(node_name(n), want)) return n;
    if (!kind_of(n, "NiNode")) continue;
    unsigned slots = child_slots(n);
    for (int i = (int)(slots < 48 ? slots : 48) - 1; i >= 0; i--) {
      uint8_t *c = child(n, (unsigned)i);
      if (c && sp < 64) stack[sp++] = c;
    }
  }
  return NULL;
}

// rs7_alt: breadth-first, the first three levels below root
static uint8_t *find_alt(uint8_t *root) {
  uint8_t *q[64];
  int head = 0, tail = 0;
  if (root) q[tail++] = root;
  for (int depth = 0; depth < 3 && head < tail; depth++)
    for (int end = tail; head < end; head++) {
      uint8_t *n = q[head];
      if (!kind_of(n, "NiNode")) continue;
      for (unsigned i = 0; i < child_slots(n) && i < 32; i++) {
        uint8_t *c = child(n, i);
        if (!c) continue;
        if (!strcmp(node_name(c), ALT_NAME)) return c;
        if (tail < 64) q[tail++] = c;
      }
    }
  return NULL;
}

// Renames a node (the game's fixed string; the old name's reference is left as it is, as rs7string does)
static void rename_node(uint8_t *node, const char *to) {
  const char *g = node ? gstr(to) : NULL;
  uint8_t *fs = g ? RC_HOST(OV_GUEST(fixed_string_create, arg(g))) : NULL;
  if (fs) ptr_put(FIELD(node, ni_name), fs);
}

// The top object of a loaded nif resource (its NiStream's first), NULL without one
static uint8_t *top_object(const uint8_t *res) {
  const uint8_t *stream = ptr_at(res, RC_OFF(nif_resource_stream));
  if (!stream || RC_LD(uint32_t, FIELD(stream, ni_stream_top_count)) < 1) return NULL;
  return ptr_at(ptr_at(stream, RC_OFF(ni_stream_top)), 0);
}

// The entity's root scene node, as the game's entity setters find it: NULL while the world has no scene
static uint8_t *entity_root(uint8_t *entity) {
  uint8_t *world = entity ? RC_HOST(OV_GUESTV(render_world, NULL, 0)) : NULL;
  if (!world || !RC_LD(uint32_t, world + RC_OFF(render_world_scene))) return NULL;
  if (RC_LD(uint32_t, entity + RC_OFF(entity_flags)) & 1) return ptr_at(entity, RC_OFF(entity_root_node));
  return RC_HOST(OV_GUEST(world_entity_root, arg(entity), arg(world)));
}

// ==== loading (loader threads) ===================================================================================
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;  // one load of ours at a time
static __thread int t_inside;  // this thread is in our own call of the handler: its point does nothing
static int g_merged[2][2];     // [layout][left-handed]: the frame got the model (PSL1's seventh peg)
static int g_peg_merged;       // peg_bakelite.nif got the in-line peg (PGH1)

// The headstock options (rs7string's kLayouts): the package folder of their frames, and the peg of the option when it
// isn't the in-line one. Both frames hang under an identity "Scene Root" and their nut and string spans coincide, so
// the in-line model sits on the 3+3 neck where it sits on the in-line one: no transform.
static const struct {
  const char *option, *folder, *peg;
} LAYOUTS[] = {
    {"in-line", "/gameplay/guitar_standard_inline/", NULL},
    {"3+3", "/gameplay/guitar_standard/", "peg_bakelite"},
};

// The layout whose folder a resource name (a .nif) is directly in, -1 for none; *lh: its base name ends in _lh; base:
// the base name without .nif, lower case
static int layout_of(const char *name, int *lh, char *base, size_t nbase) {
  char low[256];
  size_t len = name ? strlen(name) : 0;
  if (len < 5 || len >= sizeof low || strcasecmp(name + len - 4, ".nif")) return -1;
  for (size_t i = 0; i <= len; i++) {
    char c = name[i];
    low[i] = c >= 'A' && c <= 'Z' ? (char)(c + 32) : c == '\\' ? '/' : c;
  }
  char *slash = strrchr(low, '/');
  if (!slash || slash[1] == '.') return -1;
  snprintf(base, nbase, "%.*s", (int)(low + len - 4 - slash - 1), slash + 1);
  size_t bl = strlen(base);
  *lh = bl >= 3 && !strcmp(base + bl - 3, "_lh");
  for (int l = 0; l < 2; l++) {
    const char *at = strstr(low, LAYOUTS[l].folder);
    if (at && at + strlen(LAYOUTS[l].folder) - 1 == slash) return l;
  }
  return -1;
}

// The texture names (base names of ".dds") in a file's bytes, in their first order (rs7string's DdsNames)
#define MAX_TEX 32
static int dds_names(const uint8_t *buf, size_t size, char names[][64], int max) {
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
static int name_in(const char *nm, char names[][64], int n) {
  for (int i = 0; i < n; i++)
    if (!strcasecmp(names[i], nm)) return 1;
  return 0;
}

// The game's dds handler on a file in memory, registered under key: as the game's file job does it (an NiFile adapter
// over its memory IFile, whose blob holds the bytes), with those objects made here (the adapter's ctor takes its
// arguments in registers). The objects live for the run, their reference counts never reach zero (no destructor of
// the game's runs on them); the adapter's own destructor releases what this took. The resource, NULL if refused.
static void *call_dds_handler(const char *key, const uint8_t *bytes, uint32_t size) {
  static uint8_t *blob, *file, *adapter;
  if (!blob) {
    blob = rc_guest_alloc((size_t)RC_OFF(mem_blob_size_bytes));
    file = rc_guest_alloc((size_t)RC_OFF(mem_file_size));
    adapter = rc_guest_alloc((size_t)RC_OFF(nf_adapter_size));
  }
  static char *keybuf;
  if (!keybuf) keybuf = rc_guest_alloc(256);
  uint8_t *data = guest_buffer(size);
  if (!blob || !file || !adapter || !data || !keybuf || strlen(key) >= 256) return NULL;
  memcpy(data, bytes, size);
  strcpy(keybuf, key);
  uint8_t *mem_vt = (uint8_t *)(uintptr_t)RC_GA(mem_file_vtable);
  // The blob: the file's bytes and size (the memory file's Read, Size, Seek and Size64 read these)
  memset(blob, 0, (size_t)RC_OFF(mem_blob_size_bytes));
  ptr_put(blob, mem_vt);  // only its AddRef / Release could be reached, and its count never drops to zero
  RC_ST(int32_t, FIELD(blob, ni_ref_count), 1 << 30);
  ptr_put(FIELD(blob, mem_blob_data), data);
  RC_ST(uint32_t, FIELD(blob, mem_blob_size), size);
  RC_ST(uint64_t, FIELD(blob, mem_blob_size64), size);
  // The memory IFile over it, at its start
  memset(file, 0, (size_t)RC_OFF(mem_file_size));
  ptr_put(file, mem_vt);
  RC_ST(int32_t, FIELD(file, ni_ref_count), 1 << 30);
  ptr_put(FIELD(file, mem_file_blob), blob);
  // The adapter, as Res_NiFileAdapter_Ctor makes it: the file held (AddRef), its state good, the game's read and
  // write functions
  memset(adapter, 0, (size_t)RC_OFF(nf_adapter_size));
  ptr_put(adapter, (void *)(uintptr_t)RC_GA(nf_adapter_vtable));
  ptr_put(FIELD(adapter, nf_adapter_file), file);
  addref(file);
  RC_ST(uint8_t, FIELD(adapter, nf_adapter_good), 1);
  ptr_put(FIELD(adapter, nf_adapter_read), (void *)(uintptr_t)RC_GA(nf_adapter_read_fn));
  ptr_put(FIELD(adapter, nf_adapter_write), (void *)(uintptr_t)RC_GA(nf_adapter_write_fn));
  void *res = RC_HOST(OV_GUEST(dds_stream_handler, arg(keybuf), arg(adapter), 0));
  OV_GUESTV(nf_adapter_dtor, (const uint64_t[]){arg(adapter)}, 1);
  return res;
}

// The model's textures that the stock file doesn't name, from the built ones, registered as files of the stock
// file's folder; how many the game took
static int register_textures(const char *name, const hs_file *model, const uint8_t *stock, uint32_t stock_size) {
  static char model_tex[MAX_TEX][64], stock_tex[MAX_TEX][64];
  int nm = dds_names(model->data, model->size, model_tex, MAX_TEX), ns = dds_names(stock, stock_size, stock_tex, MAX_TEX);
  const char *slash = strrchr(name, '/');
  int folder = slash ? (int)(slash - name + 1) : 0, took = 0;
  for (int i = 0; i < nm; i++) {
    if (name_in(model_tex[i], stock_tex, ns)) continue;  // the shader's own textures: the game's
    const hs_file *tex = NULL;
    for (int k = 0; k < NOUT_RH && !tex; k++)
      if (g_rh[k].data && !strcasecmp(g_rh[k].name, model_tex[i])) tex = &g_rh[k];
    if (!tex) {
      hlog("texture %s: not one of rs7string's: left to the game", model_tex[i]);
      continue;
    }
    char key[256];
    snprintf(key, sizeof key, "%.*s%s", folder, name, tex->name);
    void *res = call_dds_handler(key, tex->data, tex->size);
    took += res != NULL;
    if (!res) hlog("texture %s: the game's dds handler refused it", key);
  }
  return took;
}

// Loads `bytes` with the game's nif handler under `name` and hangs its top node, renamed rs7_alt and culled, as the
// last child of the stock resource's top node. The model's resource is kept (its stream holds the only other
// references). 1 when attached.
static int merge(const char *name, const uint8_t *res, const uint8_t *bytes, uint32_t size, const char *what) {
  uint8_t *top = top_object(res);
  if (!top || !kind_of(top, "NiNode")) {
    hlog("%s: its top object isn't an NiNode: %s not merged", name, what);
    return 0;
  }
  uint8_t *buf = guest_buffer(size);
  if (!buf) return 0;
  memcpy(buf, bytes, size);
  t_inside = 1;
  uint8_t *res2 = RC_HOST(OV_GUEST(nif_mem_handler, arg(name), arg(buf), size, 0));
  t_inside = 0;
  uint8_t *alt = res2 ? top_object(res2) : NULL;
  if (!alt || !kind_of(alt, "NiNode") || ptr_at(alt, RC_OFF(ni_parent))) {
    hlog("%s: %s didn't load as a scene (resource %p, top %p): the stock model only", name, what, (void *)res2,
         (void *)alt);
    return 0;
  }
  rename_node(alt, ALT_NAME);
  set_culled(alt, 1);  // every clone starts with the stock part shown
  unsigned before = child_slots(top);
  OV_VCALL(arg(top), (int)RC_SLOT(ni_node_attach), arg(alt), 0);
  if (ptr_at(alt, RC_OFF(ni_parent)) != top) {
    hlog("%s: %s not attached: the stock model only", name, what);
    return 0;
  }
  hlog("%s: %s (%u bytes) merged as %s under \"%s\" (child slots %u -> %u)", name, what, size, ALT_NAME,
       node_name(top), before, child_slots(top));
  return 1;
}

// A frame of a guitar (not a bass): the stock model has Locator_Peg_1 and string_size_6, not string_size_7
static int guitar_frame(const uint8_t *res) {
  uint8_t *top = top_object(res);
  return top && kind_of(top, "NiNode") && find_named(top, "Locator_Peg_1") && find_named(top, "string_size_6") &&
         !find_named(top, "string_size_7");
}

// 3+3: the in-line peg and its wilshire_hardware.dds (the 3+3 peg package has the other textures it names) from the
// in-line archive, the texture registered in the peg's folder, the peg merged into peg_bakelite.nif
static void merge_inline_peg(const char *name, const uint8_t *res, const uint8_t *stock, uint32_t stock_size) {
  static const char *const files[] = {FOLDER_INLINE "epi_whilshire_peg.nif", FOLDER_INLINE "wilshire_hardware.dds"};
  uint8_t *f[2];
  size_t len[2];
  if (!archive_files(ARCHIVE_RH, files, 2, f, len)) {
    hlog("%s: the in-line peg can't be read from %s: 7-string guitars keep this option's pegs", name, ARCHIVE_RH);
    return;
  }
  char stock_tex[MAX_TEX][64], key[256];
  int ns = dds_names(stock, stock_size, stock_tex, MAX_TEX);
  const char *slash = strrchr(name, '/');
  snprintf(key, sizeof key, "%.*swilshire_hardware.dds", slash ? (int)(slash - name + 1) : 0, name);
  if (!name_in("wilshire_hardware.dds", stock_tex, ns) && !call_dds_handler(key, f[1], (uint32_t)len[1]))
    hlog("%s: the game's dds handler refused %s", name, key);
  if (merge(name, res, f[0], (uint32_t)len[0], "the in-line peg")) __atomic_store_n(&g_peg_merged, 1, __ATOMIC_RELEASE);
  free(f[0]);
  free(f[1]);
}

// AL1: the game's nif handler has loaded `name` (the point is on its way out with the resource): a frame of either
// headstock option gets the model, the 3+3 peg the in-line peg. On a loader thread; the handler is called again from
// here (its point then does nothing), so the resource in eax is put back at the end.
static void on_al1(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on || !g_on || t_inside) return;
  uint8_t *res = PTR(f, "res");
  const char *name = (const char *)PTR(f, "name");
  char base[128];
  int lh = 0, l = res ? layout_of(name, &lh, base, sizeof base) : -1;
  if (l < 0) return;
  const uint8_t *stock = PTR(f, "buf");
  uint32_t stock_size = (uint32_t)SEVEN_GET(f, "size");
  pthread_mutex_lock(&g_lock);
  if (LAYOUTS[l].peg && !strcmp(base, LAYOUTS[l].peg)) {
    merge_inline_peg(name, res, stock, stock_size);
  } else if (guitar_frame(res) && build_rh() && (!lh || build_lh())) {
    const hs_file *model = lh ? &g_lh[0] : &g_rh[0];
    size_t largest = model->size;  // the guest buffer at its largest once (what it held before isn't freed)
    for (int i = 0; i < NOUT_RH; i++) largest = g_rh[i].size > largest ? g_rh[i].size : largest;
    guest_buffer(largest);
    int took = register_textures(name, model, stock, stock_size);
    hlog("%s: %d texture(s) of the model registered (%s frame of the %s headstock)", name, took,
         lh ? "left-handed" : "right-handed", LAYOUTS[l].option);
    if (merge(name, res, model->data, model->size, model->name)) __atomic_store_n(&g_merged[l][lh], 1, __ATOMIC_RELEASE);
  }
  pthread_mutex_unlock(&g_lock);
  SEVEN_SET(f, "res", (uintptr_t)res);  // the calls above changed eax
}

// ==== a guitar that shows the model (game thread) ================================================================
// The names of a segment's nine shader values (Rnd_SetStringShaderParams), in their order in the params
static const char *const SEG_NAMES[SEG_FIELDS] = {
    "g_flickerPhase", "g_flickerFade", "g_sharpToBlurLerpVal", "g_diffuseColor",  "v_positionString",
    "g_vertexWidthScale", "g_glowScale", "g_glowPow", "g_glowBrightness"};

typedef struct {
  uint8_t *guitar;
  uint8_t *info;   // its InstrumentInfo while the model is shown (LT1 compares it, never reads the guitar)
  unsigned seen;   // the frame count it was last updated at (a slot that isn't is reused)
  uint8_t *root;   // the frame entity's root the alt was found under
  uint8_t *alt;    // rs7_alt in this frame instance (NULL: none)
  uint8_t *sibs[8];  // the stock parts culled here
  int nsibs;
  int applied, bound, pointed, swapped, layered, non_inline, frames;
  uint8_t *stock_seg[SEGS], *alt_seg[SEGS];  // slot s: string_size_<6-s> of the stock part and of the model
  uint8_t *seg7;                             // the model's string_size_7 (no stock slot)
  uint8_t *params7[SEG_FIELDS];              // its params, ours
  uint8_t *alt_hold[SEGS][SEG_FIELDS];       // the model's segments' values (ours, one reference each)
  uint8_t *stock_hold[SEGS][SEG_FIELDS];     // the stock params' values (one reference each, ours)
  int seg_ok[SEGS];
  uint8_t *stock_loc[SEGS], *alt_loc[SEGS], *alt_loc7;  // PegSet slot s: the stock locator, the model's; slot 6's
  uint8_t *peg_done[SEVEN_CAPACITY];  // 3+3: peg entities whose clone shows the in-line peg
  uint8_t *peg_checked;               // a stock guitar's PegSet checked for a left-over in-line peg
} head;

static head g_heads[HEADS];
static unsigned g_frame;
static int g_peg_swapped;  // a 3+3 peg swap happened at least once

static head *slot_of(uint8_t *guitar) {
  head *old = &g_heads[0];
  for (int i = 0; i < HEADS; i++)
    if (g_heads[i].guitar == guitar) return &g_heads[i];
  for (int i = 0; i < HEADS; i++) {
    if (!g_heads[i].guitar) {
      old = &g_heads[i];
      break;
    }
    if (g_frame - g_heads[i].seen > g_frame - old->seen) old = &g_heads[i];
  }
  memset(old, 0, sizeof *old);  // a free one, or the one updated longest ago (its guitar is likely gone: never read)
  old->guitar = guitar;
  return old;
}

static uint8_t *pegset_of(const head *h) {
  return ptr_at(ptr_at(h->guitar, RC_OFF(guitar_headstock)), RC_OFF(headstock_pegs));
}
static uint8_t *peg_slot(uint8_t *pegs, int s) { return FIELD(pegs, pegs_slots) + s * RC_OFF(peg_slot_size); }

// ---- the string segments
// Our params of a segment: its nine values on the mesh (the game's helpers find or make each by name, as
// Rnd_SetStringShaderParams does: it takes its params in a register), with the values of `from` (the stock params of
// the same string)
static void bind_segment(uint8_t *out[SEG_FIELDS], uint8_t *mesh, const uint8_t *from) {
  static float *rgba;
  if (!rgba) rgba = rc_guest_alloc(4 * sizeof(float));
  for (int i = 0; i < SEG_FIELDS; i++) {
    const uint8_t *src = ptr_at(from, (int64_t)i * rc_guest_ptr_size);
    uint8_t *ed;
    if (i == COLOUR_FIELD) {
      if (!rgba) continue;
      for (int k = 0; k < 4; k++) rgba[k] = src ? RC_LD(float, FIELD(src, ni_extra_value) + 4 * k) : 1.0f;
      ed = RC_HOST(OV_GUEST(mesh_colour_extra, arg(mesh), arg(rgba)));
    } else {
      float v = src ? RC_LD(float, FIELD(src, ni_extra_value)) : 0.0f;
      const char *nm = gstr(SEG_NAMES[i]);
      if (!nm) continue;
      const uint64_t a[] = {arg(mesh), arg(nm), rsm_f32bits(v)};
      ed = RC_HOST(rc_call_name_f("mesh_float_extra", a, 3, 1u << 2));
    }
    out[i] = ed;
    addref(ed);
  }
}

static uint8_t *stock_params(const head *h, int s) {
  return ptr_at(ptr_at(h->guitar, RC_OFF(guitar_headstock)), RC_OFF(headstock_seg_params) + s * rc_guest_ptr_size);
}

static void find_segments(head *h) {
  uint8_t *parent = ptr_at(h->alt, RC_OFF(ni_parent)), *stock = NULL;
  for (unsigned i = 0; i < child_slots(parent) && i < 32 && !stock; i++) {  // the stock part: the first other NiNode
    uint8_t *c = child(parent, i);
    if (c && c != h->alt && kind_of(c, "NiNode")) stock = c;
  }
  for (int s = 0; s < SEGS; s++) {
    char name[24];
    snprintf(name, sizeof name, "string_size_%d", SEGS - s);
    h->alt_seg[s] = find_named(h->alt, name);
    h->stock_seg[s] = stock ? find_named(stock, name) : NULL;
  }
  h->seg7 = find_named(h->alt, "string_size_7");
}

// Once per frame instance: our params for the model's segments, and a reference to each of the stock params' values
static void bind_alt(head *h) {
  int n = 0;
  for (int s = 0; s < SEGS; s++) {
    uint8_t *params = stock_params(h, s);
    h->seg_ok[s] = 0;
    if (!params || !h->alt_seg[s] || !kind_of(h->alt_seg[s], "NiMesh")) continue;
    bind_segment(h->alt_hold[s], h->alt_seg[s], params);
    for (int i = 0; i < SEG_FIELDS; i++) {
      uint8_t *p = ptr_at(params, (int64_t)i * rc_guest_ptr_size);
      h->stock_hold[s][i] = p && h->alt_hold[s][i] ? p : NULL;
      addref(h->stock_hold[s][i]);
    }
    h->seg_ok[s] = 1;
    n++;
  }
  if (h->seg7 && kind_of(h->seg7, "NiMesh")) bind_segment(h->params7, h->seg7, stock_params(h, 0));  // string_size_6's
  h->bound = 1;
  hlog("guitar %p: %d of %d headstock string segments of the model bound%s", (void *)h->guitar, n, SEGS,
       h->params7[COLOUR_FIELD] ? "; string_size_7 to its own params" : "; no string_size_7");
}

// The game's per-frame segment values go through the six stock params: point them at the model's values (or back),
// the current values carried over, so nothing is reset
static void point_params(head *h, int to_alt) {
  for (int s = 0; s < SEGS; s++) {
    uint8_t *params = stock_params(h, s);
    if (!params || !h->seg_ok[s]) continue;
    for (int i = 0; i < SEG_FIELDS; i++) {
      uint8_t *from = to_alt ? h->stock_hold[s][i] : h->alt_hold[s][i], *to = to_alt ? h->alt_hold[s][i] : h->stock_hold[s][i];
      if (!from || !to) continue;
      memcpy(FIELD(to, ni_extra_value), FIELD(from, ni_extra_value), i == COLOUR_FIELD ? 16 : 4);
      uint8_t *at = params + i * rc_guest_ptr_size, *old = ptr_at(at, 0);
      if (old == to) continue;
      addref(to);
      ptr_put(at, to);
      dropref(old);  // still held by its mesh and by us
    }
  }
  h->pointed = to_alt;
}

// string_size_7 draws in string_size_6's noteway layer (the game gives string_size_1..6 theirs at load; string_size_7
// kept the file's, drew after the fret bars and over the nut): the u16 of the object's layer property. 1 written, 0
// equal already, -1 a layer property missing.
static uint8_t *layer_property(const uint8_t *obj) {
  const uint8_t *node = ptr_at(obj, RC_OFF(ni_av_properties));
  for (int i = 0; node && i < 64; i++) {
    uint8_t *prop = ptr_at(node, RC_OFF(ni_property_node_item));
    if (prop && (int32_t)OV_VCALL(arg(prop), (int)RC_SLOT(ni_property_type)) == RC_OFF(noteway_layer_type)) return prop;
    node = ptr_at(node, RC_OFF(ni_property_node_next));
  }
  return NULL;
}
static int layer7(head *h) {
  const uint8_t *from = layer_property(h->alt_seg[0] ? h->alt_seg[0] : h->stock_seg[0]);
  uint8_t *to = layer_property(h->seg7);
  if (!from || !to) return -1;
  uint16_t v = U16(from, noteway_layer_value), was = U16(to, noteway_layer_value);
  if (v == was) return 0;
  RC_ST(uint16_t, FIELD(to, noteway_layer_value), v);
  hlog("guitar %p: string_size_7 draws in layer %u (string_size_6's), not %u", (void *)h->guitar, v, was);
  return 1;
}

// ---- the pegs: the PegSet slots' locators (an NiPointer at +8 of a slot) move to the model's Locator_Peg_1..6, so the
// game's placement puts the pegs (and the tuner's letters) on the model; slot 6 got Locator_Peg_7, which only the
// model has, from the game's binder
static void find_locators(head *h) {
  uint8_t *info = ptr_at(h->guitar, RC_OFF(guitar_info));
  const char *fmt = info && U8(info, instrument_inverted) ? "Locator_Invert_Peg_%d" : "Locator_Peg_%d";
  char name[32];
  for (int s = 0; s < SEGS; s++) {
    snprintf(name, sizeof name, fmt, s + 1);
    h->alt_loc[s] = find_named(h->alt, name);
    h->stock_loc[s] = NULL;
  }
  snprintf(name, sizeof name, fmt, SEVEN_STOCK + 1);
  h->alt_loc7 = find_named(h->alt, name);
}

static void set_locator(uint8_t *slot, uint8_t *node) {
  uint8_t *at = FIELD(slot, peg_slot_locator), *cur = ptr_at(at, 0);
  if (!node || cur == node) return;
  addref(node);
  ptr_put(at, node);
  dropref(cur);  // still held by its tree
}

static void swap_locators(head *h, int to_alt) {
  uint8_t *pegs = pegset_of(h);
  if (!pegs) return;
  int count = U16(pegs, pegs_count), n = 0;
  if (count > SEVEN_STOCK && h->alt_loc7 && RC_LD(uint8_t, peg_slot(pegs, SEVEN_STOCK))) {
    set_locator(peg_slot(pegs, SEVEN_STOCK), h->alt_loc7);  // the binder's own choice; on the model either way
    n++;
  }
  for (int s = 0; s < SEGS && s < count; s++) {
    uint8_t *slot = peg_slot(pegs, s);
    if (!RC_LD(uint8_t, slot)) continue;
    if (to_alt) {
      if (!h->alt_loc[s]) continue;
      if (!h->stock_loc[s]) h->stock_loc[s] = ptr_at(slot, RC_OFF(peg_slot_locator));
      set_locator(slot, h->alt_loc[s]);
    } else {
      set_locator(slot, h->stock_loc[s]);
    }
    n++;
  }
  h->swapped = to_alt;
  hlog("guitar %p: %d peg locators of PegSet %p on the %s model%s", (void *)h->guitar, n, (void *)pegs,
       to_alt ? "seven-peg" : "stock", !to_alt ? "" : h->alt_loc7 ? " (peg 7 on its Locator_Peg_7)" : ": it has no Locator_Peg_7");
}

// The game's placement of the pegs on their locators (Rnd_PegSet_ApplyTransforms)
static void place_pegs(head *h) {
  uint8_t *pegs = pegset_of(h);
  if (pegs && U8(pegs, pegs_loaded)) OV_GUEST(pegs_apply_transforms, arg(pegs));
}

// ---- 3+3: the in-line peg in each peg's clone of peg_bakelite.nif, which carries it as rs7_alt (culled). Shown: the
// bakelite parts culled, rs7_alt shown, the bakelite's locator_letter / Locator_Arrow renamed, so the game's lookups by
// name (the tuner's letters, the precision widget) find the in-line ones; undone the same way.
#define LETTER "locator_letter"
#define ARROW "Locator_Arrow"
#define LETTER_OFF "rs7s_locator_letter"
#define ARROW_OFF "rs7s_Locator_Arrow"

static uint8_t *find_outside(uint8_t *root, uint8_t *alt, const char *want) {  // in root's children but alt's subtree
  uint8_t *found = NULL;
  for (unsigned i = 0; i < child_slots(root) && i < 32 && !found; i++) {
    uint8_t *c = child(root, i);
    if (c && c != alt) found = find_named(c, want);
  }
  return found;
}

// show: the in-line peg, else the option's own. 0 when the clone has no rs7_alt.
static int peg_show_inline(uint8_t *entity, int show) {
  uint8_t *eroot = entity_root(entity), *alt = find_alt(eroot), *root = ptr_at(alt, RC_OFF(ni_parent));
  if (!alt || !root) return 0;
  for (unsigned i = 0; i < child_slots(root) && i < 32; i++) {
    uint8_t *c = child(root, i);
    if (!c || c == alt || !node_or_mesh(c)) continue;
    const char *nm = node_name(c);
    if (!strcmp(nm, LETTER) || !strcmp(nm, ARROW) || !strcmp(nm, LETTER_OFF) || !strcmp(nm, ARROW_OFF)) continue;
    set_culled(c, show);
  }
  set_culled(alt, !show);
  uint8_t *letter = find_outside(root, alt, show ? LETTER : LETTER_OFF), *arrow = find_outside(root, alt, show ? ARROW : ARROW_OFF);
  if (letter) rename_node(letter, show ? LETTER_OFF : LETTER);
  if (arrow) rename_node(arrow, show ? ARROW_OFF : ARROW);
  return 1;
}

static void inline_pegs(head *h) {  // every frame while shown on a 3+3 frame: cheap once done
  uint8_t *pegs = pegset_of(h);
  if (!pegs || !U8(pegs, pegs_loaded)) return;
  int n = 0, swapped = 0;
  for (int s = 0; s < SEVEN_CAPACITY; s++) {
    uint8_t *slot = peg_slot(pegs, s), *e = RC_LD(uint8_t, slot) ? ptr_at(slot, RC_OFF(peg_slot_entity)) : NULL;
    if (!e || h->peg_done[s] == e) continue;
    n++;
    if (peg_show_inline(e, 1)) {
      swapped++;
      __atomic_store_n(&g_peg_swapped, 1, __ATOMIC_RELAXED);
    }
    h->peg_done[s] = e;  // without a merged peg in its clone: left as it is
  }
  if (n) hlog("guitar %p (3+3 frame): %d of %d pegs show the in-line peg", (void *)h->guitar, swapped, n);
}

static void restore_pegs(head *h) {
  uint8_t *pegs = pegset_of(h);
  for (int s = 0; s < SEVEN_CAPACITY; s++) {
    uint8_t *e = pegs ? ptr_at(peg_slot(pegs, s), RC_OFF(peg_slot_entity)) : NULL;
    if (e && h->peg_done[s] == e) peg_show_inline(e, 0);
    h->peg_done[s] = NULL;
  }
}

// A stock guitar, once per loaded PegSet after a swap ever: a peg clone still showing the in-line peg (a pool entity
// a seven-string guitar had) gets its own back
static void stock_pegs(head *h) {
  uint8_t *pegs = pegset_of(h);
  if (!pegs || pegs == h->peg_checked || !U8(pegs, pegs_loaded)) return;
  h->peg_checked = pegs;
  for (int s = 0; s < SEVEN_CAPACITY; s++) {
    uint8_t *e = ptr_at(peg_slot(pegs, s), RC_OFF(peg_slot_entity)), *alt = e ? find_alt(entity_root(e)) : NULL;
    if (alt && !culled(alt)) peg_show_inline(e, 0);
  }
}

// ---- show and restore
static void restore(head *h) {
  if (h->pointed) point_params(h, 0);
  if (h->swapped) {
    swap_locators(h, 0);
    place_pegs(h);
  }
  for (int i = 0; i < h->nsibs; i++) set_culled(h->sibs[i], 0);
  h->nsibs = 0;
  set_culled(h->alt, 1);
  if (h->non_inline) restore_pegs(h);
  h->non_inline = 0;
  h->applied = 0;
  h->info = NULL;
}

static void show(head *h) {  // every frame while wanted: the model shown, its stock siblings culled
  uint8_t *parent = ptr_at(h->alt, RC_OFF(ni_parent));
  for (unsigned i = 0; i < child_slots(parent) && i < 32; i++) {
    uint8_t *c = child(parent, i);
    if (!c || c == h->alt || culled(c) || !node_or_mesh(c)) continue;  // effects stay; culled ones too
    int known = 0;
    for (int k = 0; k < h->nsibs; k++) known |= h->sibs[k] == c;
    if (!known && h->nsibs < 8) h->sibs[h->nsibs++] = c;
    set_culled(c, 1);
  }
  set_culled(h->alt, 0);
  h->applied = 1;
  if (!h->bound) {
    find_segments(h);
    find_locators(h);
    bind_alt(h);
  }
  if (!h->pointed) point_params(h, 1);
  if (!h->swapped) swap_locators(h, 1);
  if (h->swapped) place_pegs(h);
  uint8_t *info = ptr_at(h->guitar, RC_OFF(guitar_info));
  h->info = info;
  h->non_inline = info && !U8(info, instrument_inline);
  if (h->non_inline) inline_pegs(h);
  if (h->seg7 && !h->layered) h->layered = layer7(h) >= 0;
}

// The guitar's headstock is loaded: its six segment params and its PegSet (the model is bound to them once)
static int loaded(const head *h) {
  uint8_t *pegs = pegset_of(h);
  if (!pegs || !U8(pegs, pegs_loaded)) return 0;
  for (int s = 0; s < SEGS; s++)
    if (!stock_params(h, s)) return 0;
  return 1;
}

// Every frame of a guitar: want = it was built with seven strings
static void head_frame(uint8_t *guitar, int want) {
  head *h = slot_of(guitar);
  h->seen = ++g_frame;
  if (!want && !h->applied) {  // a stock guitar: two compares (and a check of its pegs after a 3+3 swap ever)
    if (__atomic_load_n(&g_peg_swapped, __ATOMIC_RELAXED)) stock_pegs(h);
    return;
  }
  uint8_t *frame = ptr_at(ptr_at(guitar, RC_OFF(guitar_headstock)), RC_OFF(guitar_head_frame));
  uint8_t *root = frame ? entity_root(frame) : NULL;
  if (!root) return;  // no world scene now: nothing is drawn, the state is kept
  if (want && !h->applied && !loaded(h)) return;  // its load hasn't finished: the stock headstock until then
  if (root != h->root) {  // a new frame instance: the old one's pointers are forgotten
    uint8_t *keep = h->guitar;
    memset(h, 0, sizeof *h);
    h->guitar = keep;
    h->seen = g_frame;
    h->root = root;
    h->alt = root ? find_alt(root) : NULL;
    static int told;
    if (want && root && !h->alt && told++ < 4)
      hlog("guitar %p: its frame (root %p) has no %s: the stock headstock", (void *)guitar, (void *)root, ALT_NAME);
  }
  if (!h->alt) return;
  if (want) {
    show(h);
    if (++h->frames == 1)
      hlog("guitar %p shows the seven-peg headstock (%d stock parts culled, first \"%s\"; inverted %u, left-handed %u, "
           "in-line %u)", (void *)guitar, h->nsibs, h->nsibs ? node_name(h->sibs[0]) : "",
           h->info ? U8(h->info, instrument_inverted) : 0, h->info ? U8(h->info, instrument_lefty) : 0,
           h->info ? U8(h->info, instrument_inline) : 0);
  } else {
    restore(h);
    h->frames = 0;
    hlog("guitar %p: back to the stock headstock", (void *)guitar);
  }
}

// N5 (Rnd_Guitar_Update), every frame of a guitar (the strings part's point; this is its second hook)
static void on_frame(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on || !g_on) return;
  uint8_t *guitar = PTR(f, "guitar");
  if (guitar) head_frame(guitar, seven_strings_guitar_count(guitar) >= 7);
}

// ---- PGH1: the selected peg's highlight pass draws the mesh list collected per peg clone (by a walk that ignores the
// culled bit) without looking at the bit, so in a 3+3 clone the hidden knob (bakelite or in-line) glowed too. A mesh
// that is, or lies under, a culled rs7_alt or a culled sibling of one is skipped as the pass skips its own.
static int is_alt(const uint8_t *node) { return !strcmp(node_name(node), ALT_NAME); }

static int peg_mesh_hidden(const uint8_t *mesh) {
  const uint8_t *n = mesh;
  for (int depth = 0; n && depth < 16; depth++) {
    const uint8_t *parent = ptr_at(n, RC_OFF(ni_parent));
    if (culled(n)) {
      int hidden = is_alt(n);
      for (unsigned i = 0; parent && !hidden && i < child_slots(parent) && i < 32; i++) {
        const uint8_t *c = child(parent, i);
        hidden = c && c != n && is_alt(c);
      }
      if (hidden) return 1;
    }
    n = parent;
  }
  return 0;
}

static void on_pgh1(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on || !__atomic_load_n(&g_peg_merged, __ATOMIC_ACQUIRE)) return;
  if (peg_mesh_hidden(PTR(f, "mesh"))) f->go = SEVEN_CONT(f, "skip");
}

// ==== what the strings part asks =================================================================================
int seven_headstock_pegs(const void *info, int count) {
  if (count <= SEVEN_STOCK) return count;
  if (!seven_on || !g_on || !info) return SEVEN_STOCK;
  int layout = U8(info, instrument_inline) ? 0 : 1, lh = U8(info, instrument_lefty) != 0;
  return __atomic_load_n(&g_merged[layout][lh], __ATOMIC_ACQUIRE) ? SEVEN_STOCK + 1 : SEVEN_STOCK;
}

void *seven_headstock_seg7_colour(const void *guitar) {
  if (!seven_on || !g_on || !guitar) return NULL;
  for (int i = 0; i < HEADS; i++)
    if (g_heads[i].guitar == guitar) return g_heads[i].applied ? g_heads[i].params7[COLOUR_FIELD] : NULL;
  return NULL;
}

int seven_headstock_letter_inline(const void *info) {
  int own = info ? U8(info, instrument_inline) : 0;
  if (own || !info || !seven_on || !g_on) return own;
  for (int i = 0; i < HEADS; i++)
    if (g_heads[i].guitar && g_heads[i].applied && g_heads[i].non_inline && g_heads[i].info == info) {
      static int told;
      if (once(&told)) hlog("the tuner's letters of a 3+3 frame showing the in-line model follow the in-line rule");
      return 1;
    }
  return 0;
}

int seven_headstock_init(void) {
  read_settings();
  hlog("[%s] Headstock7 = %s, Headstock7Wood = %.2f", SECTION, g_on ? "on" : "off", g_wood);
  static const struct {
    const char *name;
    rc_mid_fn fn;
  } points[] = {
      {"seven_al1", on_al1},
      {"seven_n5", on_frame},
      {"seven_pgh1", on_pgh1},
  };
  for (size_t i = 0; i < sizeof points / sizeof *points; i++)
    if (seven_mid(points[i].name, points[i].fn)) return -1;
  return 0;
}
