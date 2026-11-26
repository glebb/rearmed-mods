// Seven-String Guitar (seven.h): the headstock part: a headstock with a tuning peg for each string of an extended
// instrument: seven or eight on a guitar, five or six on a bass. Ported from rs7string (github.com/SomeeName/rs7string,
// MIT, by SomeeName; now RS Extended Range): src/features/headstock (assetload*, hsbuild*, headstockread, hooks_width,
// the models' half of hooks*), its ARCHITECTURE section 5 ("Headstock model", "Peg and knob tint"). The models' data
// is rs7string's: data/rs7string/headstock (its data/headstock, MIT, with its LICENSE.md), compiled into the library
// gzipped and unpacked when the models are built, each checked against rs7string's SHA-256 of it. The pure part (the
// program's interpreter, the frames' names, the wood, the width's matrix) is seven_headstock_build.c.
//
// The models. rs7string's brandless in-line headstocks ("Facet") are not shipped as .nif files: rs7string ships four
// copy/insert programs (RSXRH1: hex text; a version, the SHA-256 of the two game files it is made from, then each output
// as literal runs and copied ranges of those files, with its size and SHA-256) and each is run on the player's own game
// files. Here the library reads those files from the game's own archives, refuses them unless their SHA-256 is the
// program's, and builds in memory, each output checked against its SHA-256. Nothing is written to the game folder.
//   the guitar (epi_wilshire_cherry.nif and epi_whilshire_peg.nif of base/dependency_guitarstandardinlinedrh.psarc):
//     rsxr_inline7.nif (7 pegs), rsxr_inline8.nif (8 pegs), and rsxr's six procedural textures rsxrhs_*.dds;
//     the left-handed frames (epi_whilshire_cherry_lh.nif of ...inlinedlh.psarc): rsxr_inline7_lh.nif, rsxr_inline8_lh.nif;
//   the bass (bass_thunder.nif and peg_2clove.nif of base/dependency_guitarbassinlinedrh.psarc, the left-handed
//     bass_thunder_lh.nif of ...bassinlinedlh.psarc): rsxr_bass5.nif and rsxr_bass6.nif, and the _lh pair; the bass
//     models name the guitar's textures.
//
// Loading (rs7string's AL1). The frame of a headstock is loaded once, at the start, by the game's nif handler (on a
// loader thread) and every instrument's frame is a clone of it. Where the handler has loaded a guitar's frame of
// either headstock option (in-line: guitar_standard_inline/, 3+3: guitar_standard/; right- or left-handed) or a bass's
// (guitar_bass_inline/, guitar_bass/: the 2+2 frame gets the in-line models too), seven_al1 loads the frame's two
// models with the same handler under the same name, renames their top nodes rsxr_alt (the 8-peg guitar, the 6-string
// bass) and rsxr_alt5 (the 7-peg guitar, the 5-string bass), culls them and attaches them as the last children of the
// stock top node: the stock part comes first, so the game's lookups by name (string_size_%d, Locator_Peg_%d) still find
// the stock nodes. Before that the models' textures that the stock frame doesn't name go to the game's dds handler as
// the files of the stock folder, from memory (a memory file and a NiFile adapter of the game's own classes, made here
// as the game's code makes them: their ctor takes its arguments in registers). On the 3+3 option the pegs are
// peg_bakelite.nif: the in-line peg (and its wilshire_hardware.dds) is merged into it the same way.
//
// An instrument built with extra strings (every frame, seven_n5): the model of its count (rsxr_alt5 for 7 and for a bass's
// 5, if the frame has one; else rsxr_alt) is shown and its stock siblings culled; the six segment params of the
// headstock's strings (Rnd_Headstock +0x20) point at the model's segments (a guitar's string_size_1..6, a bass's by
// string: E A D G are string_size_10..7, the low B 11, the high C 6; the values carried over), string_size_7 and _8 get
// params of their own, whose colours the game's segment loop (GSHC) writes for strings 6 and 7 and whose draw layer is
// string_size_6's; a bass's added slots 4 and 5 are marked loaded while the model is shown, so the loop colours them, and
// draw in the E string's layer; the PegSet's locators of pegs 1-6 move to the model's Locator_Peg_1..6 (or the Invert
// ones), pegs 7 and 8 to its Locator_Peg_7 and _8 every frame (the game's binder takes the first of the frame, which with
// two models may be the hidden one's), and the game's placement puts the pegs there. Its PegSet loads as many pegs as
// the frame's models have (seven_psl1: the strings part asks seven_headstock_pegs for a guitar, this part's own hook for
// a bass), so every stock loop over the PegSet's count places, layers, fades and colours them. On a 3+3 frame each
// peg's clone shows the in-line peg, and the tuner letters follow the in-line rule (LT1).
//
// The width: with a [neck] spacing_* factor (seven_spacing_k) the frame root's rotation is scaled along the strings, so
// the headstock's string segments meet the neck strings at the nut, and the pegs are placed again (seven_headstock_frame).
//
// What is not ported: rs7string's other model files ([assets] headstock7_file, the edited-file rule dir/suffix), the
// files it writes and reuses (the build runs at every start, in about a tenth of a second per kit), the debug
// experiments ([debug] head_*), the overlay, SEH (reads go through pointers the game keeps). Its raise of peg 7 (a 7-peg
// PegSet on every guitar) isn't needed: an instrument's string count is fixed for its life here (C-lite), so its PegSet
// loads its pegs from the start.
//
// Settings, [Seven String] in RSMods.ini, read at the start: Headstock7 = on|off (default on; off: the stock headstocks,
// and an extended instrument has the stock pegs), Headstock7Wood = the models' wood brightness, 0.05-1 (default 0.6,
// rs7string's headstock7_wood: the game shows the texture unlit, so its DXT1 end points are darkened).
#include <CommonCrypto/CommonDigest.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <zlib.h>

#include "rsmods_tuning.h"
#include "seven.h"
#include "seven_headstock_build.h"

#define SECTION "Seven String"
#define ALT_NAME "rsxr_alt"    // the frame's first model: the 8-peg guitar, the 6-string bass
#define ALT5_NAME "rsxr_alt5"  // its second, one string fewer: the 7-peg guitar, the 5-string bass
#define SEGS 6         // the headstock's string segments (string_size_1..6) and their params
#define EXTRA 2        // the model's segments and pegs beyond them: string_size_7 / _8, Locator_Peg_7 / _8
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

// ==== the models: rs7string's RSXRH1 programs (hsbuild.cpp; the interpreter is seven_headstock_build.c) ==============
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"
static const uint8_t PKG_GUITAR_GZ[] = {
#embed "../data/rs7string/headstock/rsxr_headstock.bin.gz"
};
static const uint8_t PKG_GUITAR_LH_GZ[] = {
#embed "../data/rs7string/headstock/rsxr_headstock_lh.bin.gz"
};
static const uint8_t PKG_BASS_GZ[] = {
#embed "../data/rs7string/headstock/rsxr_headstock_bass.bin.gz"
};
static const uint8_t PKG_BASS_LH_GZ[] = {
#embed "../data/rs7string/headstock/rsxr_headstock_bass_lh.bin.gz"
};
#pragma clang diagnostic pop

// rs7string's four programs as it ships them (data/headstock, the `package` entries of its audit files): each with the
// two game files it is made from (an archive of the game folder and the entries of it), its size and SHA-256 unpacked
enum { KIT_GUITAR, KIT_GUITAR_LH, KIT_BASS, KIT_BASS_LH, KITS };
typedef struct {
  const uint8_t *gz;
  size_t gz_len, size;
  const char *sha_hex;
  const char *archive, *entry[2];
  const char *what;
  hs_file files[HS_MAX_OUT];  // what it made (once built)
  int nfiles;
  int state;  // 0 not tried, 1 built, -1 refused or failed
} kit_t;

#define FOLDER_INLINE "assets/gameplay/guitar_standard_inline/"
#define ARCHIVE_RH "base/dependency_guitarstandardinlinedrh.psarc"
#define FOLDER_BASS "assets/gameplay/guitar_bass_inline/"
static kit_t g_kits[KITS] = {
    [KIT_GUITAR] = {PKG_GUITAR_GZ, sizeof PKG_GUITAR_GZ, 14816399,
                    "dfa2e321ba9190ad30218468b947a6c8238b37826437287f6d876acfb158cb10",
                    "base/dependency_guitarstandardinlinedrh.psarc",
                    {FOLDER_INLINE "epi_wilshire_cherry.nif", FOLDER_INLINE "epi_whilshire_peg.nif"}, "guitar"},
    [KIT_GUITAR_LH] = {PKG_GUITAR_LH_GZ, sizeof PKG_GUITAR_LH_GZ, 5922719,
                       "409bc5bddb214ed672be8acf3edea100e290e0eade136de1a637bd8df6a1ad5a",
                       "base/dependency_guitarstandardinlinedlh.psarc",
                       {FOLDER_INLINE "epi_whilshire_cherry_lh.nif", FOLDER_INLINE "epi_whilshire_peg.nif"},
                       "left-handed guitar"},
    [KIT_BASS] = {PKG_BASS_GZ, sizeof PKG_BASS_GZ, 7574973,
                  "61d80e826c4c278017fc8b2e47735c08dc71659f7594825064331c8d5a579ae7",
                  "base/dependency_guitarbassinlinedrh.psarc",
                  {FOLDER_BASS "bass_thunder.nif", FOLDER_BASS "peg_2clove.nif"}, "bass"},
    [KIT_BASS_LH] = {PKG_BASS_LH_GZ, sizeof PKG_BASS_LH_GZ, 6422701,
                     "f2bafc1191b591faed32faf93c0179d0bdbc8656010778a2a30ff7dd8ed45a19",
                     "base/dependency_guitarbassinlinedlh.psarc",
                     {FOLDER_BASS "bass_thunder_lh.nif", FOLDER_BASS "peg_2clove.nif"}, "left-handed bass"},
};

static int hex_sha(const char *hex, uint8_t sha[32]) {
  for (int i = 0; i < 32; i++) {
    int hi = hex[2 * i] ? (hex[2 * i] <= '9' ? hex[2 * i] - '0' : hex[2 * i] - 'a' + 10) : -1;
    int lo = hex[2 * i + 1] ? (hex[2 * i + 1] <= '9' ? hex[2 * i + 1] - '0' : hex[2 * i + 1] - 'a' + 10) : -1;
    if (hi < 0 || lo < 0) return 0;
    sha[i] = (uint8_t)(hi << 4 | lo);
  }
  return 1;
}

// ---- the game files the program runs on, from the game's own archives (rs7string's headstockread.cpp, which reads
// them through the game's file system; the library reads the archive itself, as mod_cache.c reads cache.psarc)
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

// The kit built from the game's own files (once; under g_lock), or NULL with a log line: nothing is made unless the two
// game files have the program's SHA-256 and each output its own
static kit_t *kit_get(int which) {
  kit_t *k = &g_kits[which];
  if (k->state) return k->state > 0 ? k : NULL;
  k->state = -1;
  hs_package pkg = {k->gz, k->gz_len, k->size, {0}};
  uint8_t *tpl[2];
  size_t len[2];
  if (!hex_sha(k->sha_hex, pkg.sha)) return NULL;
  if (!archive_files(k->archive, k->entry, 2, tpl, len)) {
    hlog("%s or %s can't be read from %s: no %s headstock model", k->entry[0], k->entry[1], k->archive, k->what);
    return NULL;
  }
  size_t plen = 0;
  const char *why = NULL;
  uint8_t *prog = hs_package_unpack(&pkg, &plen, &why);
  if (prog) {
    const uint8_t *const t[2] = {tpl[0], tpl[1]};
    const size_t l[2] = {len[0], len[1]};
    why = hs_build(prog, plen, t, l, ~0u, k->files, HS_MAX_OUT, &k->nfiles);
  }
  free(prog);
  free(tpl[0]);
  free(tpl[1]);
  if (why) {
    hlog("rs7string's %s headstock program refused the game's files (%s): no %s headstock model", k->what, why, k->what);
    return NULL;
  }
  for (int i = 0; i < k->nfiles; i++)
    if (g_wood < 1.0f && hs_is_wood_colour(k->files[i].name))
      hlog("%s: wood brightness %.2f %s", k->files[i].name, g_wood,
           hs_scale_dxt1(k->files[i].data, k->files[i].size, g_wood) ? "applied" : "not applied (not a plain DXT1 .dds)");
  k->state = 1;
  hlog("built the %s headstock models from the game's own files: %d files, first %s (%u bytes)", k->what, k->nfiles,
       k->files[0].name, k->files[0].size);
  return k;
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

// The model called `want` (rsxr_alt, rsxr_alt5): breadth-first, the first three levels below root
static uint8_t *find_alt_named(uint8_t *root, const char *want) {
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
        if (!strcmp(node_name(c), want)) return c;
        if (tail < 64) q[tail++] = c;
      }
    }
  return NULL;
}
static uint8_t *find_alt(uint8_t *root) { return find_alt_named(root, ALT_NAME); }

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

// The entity's root scene node, as the game's entity setters find it: NULL while the world has no scene. The Mac's
// setters ask the service manager inline (no render_world): there the game's entity_model_root answers the same.
static uint8_t *entity_root(uint8_t *entity) {
  if (entity && !seven_has_render_world()) return RC_HOST(OV_GUEST(entity_model_root, arg(entity)));
  uint8_t *world = entity ? RC_HOST(OV_GUESTV(render_world, NULL, 0)) : NULL;
  if (!world || !RC_LD(uint32_t, world + RC_OFF(render_world_scene))) return NULL;
  if (RC_LD(uint32_t, entity + RC_OFF(entity_flags)) & 1) return ptr_at(entity, RC_OFF(entity_root_node));
  return RC_HOST(OV_GUEST(world_entity_root, arg(entity), arg(world)));
}

// ==== loading (loader threads) ===================================================================================
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;  // one load of ours at a time
static __thread int t_inside;  // this thread is in our own call of the handler: its point does nothing
// The pegs the model of a frame has (0 none, 7 or 8 for a guitar's, 5 or 6 for a bass's): PSL1's peg count
static int g_merged[2][2];       // a guitar's frame: [layout: 0 in-line, 1 3+3][left-handed]
static int g_bass_merged[2][2];  // a bass's frame: [0 in-line, 1 2+2][left-handed]
static int g_peg_merged;       // peg_bakelite.nif got the in-line peg (PGH1)
static int g_bass_ok;          // the bass part's point (PSL1 of a bass) is taken

#define MAX_TEX HS_MAX_TEX

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
  // write functions. Where the game's ctor takes ordinary arguments (the Mac's), it makes it itself.
  memset(adapter, 0, (size_t)RC_OFF(nf_adapter_size));
  if (RC_ADDR(nf_adapter_read_fn)) {
    ptr_put(adapter, (void *)(uintptr_t)RC_GA(nf_adapter_vtable));
    ptr_put(FIELD(adapter, nf_adapter_file), file);
    addref(file);
    RC_ST(uint8_t, FIELD(adapter, nf_adapter_good), 1);
    ptr_put(FIELD(adapter, nf_adapter_read), (void *)(uintptr_t)RC_GA(nf_adapter_read_fn));
    ptr_put(FIELD(adapter, nf_adapter_write), (void *)(uintptr_t)RC_GA(nf_adapter_write_fn));
  } else {
    OV_GUEST(nf_adapter_ctor, arg(adapter), arg(file));
    if (!U8(adapter, nf_adapter_good) || ptr_at(adapter, RC_OFF(nf_adapter_file)) != file) {
      hlog("%s: the game's file adapter didn't take the texture: left to the game", key);
      return NULL;  // nothing to release: the file's count never drops to zero
    }
  }
  void *res = RC_HOST(OV_GUEST(dds_stream_handler, arg(keybuf), arg(adapter), 0));
  OV_GUESTV(nf_adapter_dtor, (const uint64_t[]){arg(adapter)}, 1);
  return res;
}

// The model's textures that the stock file doesn't name, from the guitar kit's built ones (a bass model names them
// too), registered as files of the stock file's folder; `done` lists the names registered for this frame already (a
// frame's second model shares them). How many the game took.
static int register_textures(const char *name, const hs_file *model, const kit_t *tex, const uint8_t *stock,
                             uint32_t stock_size, char done[][64], int *ndone) {
  static char model_tex[MAX_TEX][64], stock_tex[MAX_TEX][64];
  int nm = hs_dds_names(model->data, model->size, model_tex, MAX_TEX),
      ns = hs_dds_names(stock, stock_size, stock_tex, MAX_TEX);
  const char *slash = strrchr(name, '/');
  int folder = slash ? (int)(slash - name + 1) : 0, took = 0;
  for (int i = 0; i < nm; i++) {
    if (hs_name_in(model_tex[i], stock_tex, ns)) continue;  // the shader's own textures: the game's
    if (hs_name_in(model_tex[i], done, *ndone)) continue;   // the frame's other model has registered it
    const hs_file *t = hs_find(tex->files, tex->nfiles, model_tex[i]);
    if (!t || !t->data) {
      hlog("texture %s: not one of rs7string's: left to the game", model_tex[i]);
      continue;
    }
    char key[256];
    snprintf(key, sizeof key, "%.*s%s", folder, name, t->name);
    void *res = call_dds_handler(key, t->data, t->size);
    took += res != NULL;
    if (res && *ndone < MAX_TEX) strcpy(done[(*ndone)++], t->name);
    if (!res) hlog("texture %s: the game's dds handler refused it", key);
  }
  return took;
}

// Loads `bytes` with the game's nif handler under `name` and hangs its top node, renamed `alt_name` and culled, as the
// last child of the stock resource's top node. The model's resource is kept (its stream holds the only other
// references). 1 when attached.
static int merge(const char *name, const uint8_t *res, const uint8_t *bytes, uint32_t size, const char *what,
                 const char *alt_name) {
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
  rename_node(alt, alt_name);
  set_culled(alt, 1);  // every clone starts with the stock part shown
  unsigned before = child_slots(top);
  OV_VCALL(arg(top), (int)RC_SLOT(ni_node_attach), arg(alt), 0);
  if (ptr_at(alt, RC_OFF(ni_parent)) != top) {
    hlog("%s: %s not attached: the stock model only", name, what);
    return 0;
  }
  hlog("%s: %s (%u bytes) merged as %s under \"%s\" (child slots %u -> %u)", name, what, size, alt_name,
       node_name(top), before, child_slots(top));
  return 1;
}

// A frame of a guitar (not a bass): the stock model has Locator_Peg_1 and string_size_6, not string_size_7
static int guitar_frame(const uint8_t *res) {
  uint8_t *top = top_object(res);
  return top && kind_of(top, "NiNode") && find_named(top, "Locator_Peg_1") && find_named(top, "string_size_6") &&
         !find_named(top, "string_size_7");
}
// A stock bass frame: Locator_Peg_1 and the bass's string_size_7..10
static int bass_frame(const uint8_t *res) {
  uint8_t *top = top_object(res);
  return top && kind_of(top, "NiNode") && find_named(top, "Locator_Peg_1") && find_named(top, "string_size_7");
}

// One frame's two models from the built kits: the first (`file0`, `pegs0` pegs) as rsxr_alt, the second (`file1`,
// one string fewer) as rsxr_alt5, each with the textures it names. The pegs the frame has: the first's, else the
// second's, 0 for none (the stock headstock stays).
static int merge_models(const char *name, const uint8_t *res, const uint8_t *stock, uint32_t stock_size,
                        const kit_t *own, const kit_t *tex, const char *const file[2], const int pegs[2]) {
  size_t largest = 0;  // the guest buffer at its largest once (what it held before isn't freed)
  const hs_file *m[2];
  for (int i = 0; i < 2; i++) {
    m[i] = hs_find(own->files, own->nfiles, file[i]);
    if (m[i] && m[i]->data && m[i]->size > largest) largest = m[i]->size;
  }
  for (int i = 0; i < tex->nfiles; i++)
    if (tex->files[i].data && tex->files[i].size > largest) largest = tex->files[i].size;
  guest_buffer(largest);
  char done[MAX_TEX][64];
  int ndone = 0, have = 0;
  for (int i = 0; i < 2; i++) {
    if (!m[i] || !m[i]->data) {
      hlog("%s: %s isn't in the %s kit: no model for it", name, file[i], own->what);
      continue;
    }
    int took = register_textures(name, m[i], tex, stock, stock_size, done, &ndone);
    hlog("%s: %d texture(s) of %s registered", name, took, m[i]->name);
    if (merge(name, res, m[i]->data, m[i]->size, m[i]->name, have ? ALT5_NAME : ALT_NAME)) have = have ? have : pegs[i];
  }
  return have;
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
  int ns = hs_dds_names(stock, stock_size, stock_tex, MAX_TEX);
  const char *slash = strrchr(name, '/');
  snprintf(key, sizeof key, "%.*swilshire_hardware.dds", slash ? (int)(slash - name + 1) : 0, name);
  if (!hs_name_in("wilshire_hardware.dds", stock_tex, ns) && !call_dds_handler(key, f[1], (uint32_t)len[1]))
    hlog("%s: the game's dds handler refused %s", name, key);
  if (merge(name, res, f[0], (uint32_t)len[0], "the in-line peg", ALT_NAME)) __atomic_store_n(&g_peg_merged, 1, __ATOMIC_RELEASE);
  free(f[0]);
  free(f[1]);
}

// A guitar's frame: the 8-peg and the 7-peg model (rsxr_inline8 / 7 of the right- or left-handed kit; the textures
// are the right-handed kit's). Returns the pegs the frame has.
static int merge_guitar(const char *name, const uint8_t *res, const uint8_t *stock, uint32_t stock_size, int lh) {
  const kit_t *tex = kit_get(KIT_GUITAR), *own = lh ? kit_get(KIT_GUITAR_LH) : tex;
  if (!tex || !own) return 0;
  const char *const file[2] = {lh ? "rsxr_inline8_lh.nif" : "rsxr_inline8.nif", lh ? "rsxr_inline7_lh.nif" : "rsxr_inline7.nif"};
  return merge_models(name, res, stock, stock_size, own, tex, file, (const int[2]){8, 7});
}

// A bass's frame: the 6-string and the 5-string model (rsxr_bass6 / 5 of the bass kit)
static int merge_bass(const char *name, const uint8_t *res, const uint8_t *stock, uint32_t stock_size, int lh) {
  const kit_t *tex = kit_get(KIT_GUITAR), *own = kit_get(lh ? KIT_BASS_LH : KIT_BASS);
  if (!tex || !own) return 0;
  const char *const file[2] = {lh ? "rsxr_bass6_lh.nif" : "rsxr_bass6.nif", lh ? "rsxr_bass5_lh.nif" : "rsxr_bass5.nif"};
  return merge_models(name, res, stock, stock_size, own, tex, file, (const int[2]){6, 5});
}

// AL1: the game's nif handler has loaded `name` (the point is on its way out with the resource): a frame of either
// headstock option gets the models, the 3+3 peg the in-line peg. On a loader thread; the handler is called again from
// here (its point then does nothing), so the resource in eax is put back at the end.
static void on_al1(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on || !g_on || t_inside) return;
  uint8_t *res = PTR(f, "res");
  const char *name = (const char *)PTR(f, "name");
  hs_frame fr = res ? hs_frame_of(name) : (hs_frame){HS_FRAME_NONE, 0, 0, ""};
  if (fr.kind == HS_FRAME_NONE) return;
  const uint8_t *stock = PTR(f, "buf");
  uint32_t stock_size = (uint32_t)SEVEN_GET(f, "size");
  pthread_mutex_lock(&g_lock);
  if (fr.peg) {
    merge_inline_peg(name, res, stock, stock_size);
  } else if ((fr.kind == HS_FRAME_GUITAR_INLINE || fr.kind == HS_FRAME_GUITAR_33) && guitar_frame(res)) {
    int layout = fr.kind == HS_FRAME_GUITAR_33;
    int pegs = merge_guitar(name, res, stock, stock_size, fr.lh);
    hlog("%s: %d-peg headstock models merged (%s frame of the %s headstock)", name, pegs,
         fr.lh ? "left-handed" : "right-handed", layout ? "3+3" : "in-line");
    __atomic_store_n(&g_merged[layout][fr.lh], pegs, __ATOMIC_RELEASE);
  } else if ((fr.kind == HS_FRAME_BASS_INLINE || fr.kind == HS_FRAME_BASS_22) && g_bass_ok && seven_feature(SEVEN_F_BASS) && bass_frame(res)) {
    int layout = fr.kind == HS_FRAME_BASS_22;
    int pegs = merge_bass(name, res, stock, stock_size, fr.lh);
    hlog("%s: %d-peg bass headstock models merged (%s frame of the %s bass headstock)", name, pegs,
         fr.lh ? "left-handed" : "right-handed", layout ? "2+2" : "in-line");
    __atomic_store_n(&g_bass_merged[layout][fr.lh], pegs, __ATOMIC_RELEASE);
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
  uint8_t *alt;    // the model shown in this frame instance, rsxr_alt or rsxr_alt5 (NULL: none)
  int five;        // alt is the one string fewer (rsxr_alt5): a bass built with 5, a guitar with 7
  int bass;        // the instrument is a bass (its segments are named by string, find_segments)
  uint8_t *sibs[8];  // the stock parts culled here
  int nsibs;
  int applied, bound, pointed, swapped, layered, non_inline, frames;
  uint8_t *stock_seg[SEGS], *alt_seg[SEGS];  // slot s: the stock part's segment and the model's (guitar string_size_<6-s>)
  uint8_t *segx[EXTRA];                      // the model's string_size_7 / _8 (no stock slot)
  uint8_t *paramsx[EXTRA][SEG_FIELDS];       // their params, ours
  uint8_t *alt_hold[SEGS][SEG_FIELDS];       // the model's segments' values (ours, one reference each)
  uint8_t *stock_hold[SEGS][SEG_FIELDS];     // the stock params' values (one reference each, ours)
  uint8_t loaded_was[SEGS];                  // a bass's loaded flags point_params replaced
  int seg_ok[SEGS];
  uint8_t *stock_loc[SEGS], *alt_loc[SEGS], *alt_locx[EXTRA];  // PegSet slot s: the stock locator, the model's; slots 6, 7's
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

static uint8_t *pegset_of_guitar(const uint8_t *guitar) {
  return ptr_at(ptr_at(guitar, RC_OFF(guitar_headstock)), RC_OFF(headstock_pegs));
}
static uint8_t *pegset_of(const head *h) { return pegset_of_guitar(h->guitar); }
static uint8_t *peg_slot(uint8_t *pegs, int s) { return FIELD(pegs, pegs_slots) + s * RC_OFF(peg_slot_size); }

// ---- the string segments
// Where value i of a segment's params is: the Mac's params have a word before the nine values (headstock_seg_values);
// Learn & Play's start with them (its tables predate the name)
static int64_t seg_value(int i) {
  static int64_t base = -1;
  if (base < 0) base = rc_guest_ptr_size == 4 ? 0 : RC_OFF(headstock_seg_values);
  return base + (int64_t)i * rc_guest_ptr_size;
}

// Our params of a segment: its nine values on the mesh (the game's helpers find or make each by name, as
// Rnd_SetStringShaderParams does: it takes its params in a register), with the values of `from` (the stock params of
// the same string)
static void bind_segment(uint8_t *out[SEG_FIELDS], uint8_t *mesh, const uint8_t *from) {
  static float *rgba;
  if (!rgba) rgba = rc_guest_alloc(4 * sizeof(float));
  for (int i = 0; i < SEG_FIELDS; i++) {
    const uint8_t *src = ptr_at(from, seg_value(i));
    uint8_t *ed;
    if (i == COLOUR_FIELD) {
      if (!rgba) continue;
      for (int k = 0; k < 4; k++) rgba[k] = src ? RC_LD(float, FIELD(src, ni_extra_value) + 4 * k) : 1.0f;
      if (RC_ADDR(mesh_colour_extra)) {
        ed = RC_HOST(OV_GUEST(mesh_colour_extra, arg(mesh), arg(rgba)));
      } else {  // the Mac's helper takes the name
        const char *nm = gstr(SEG_NAMES[i]);
        if (!nm) continue;
        ed = RC_HOST(OV_GUEST(mesh_colour_extra_named, arg(mesh), arg(nm), arg(rgba)));
      }
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

// The segments of the stock part and of the model. A guitar's are string_size_<6-s>; a bass's go by string (hs_bass_seg:
// E A D G are the stock string_size_10..7, the added low B string_size_11, the high C string_size_6), the first `count`
// of them: a bass binds slots 0..count-1, so a 5/6-string bass fits the six stock slots and has no extra segment
static void find_segments(head *h) {
  uint8_t *parent = ptr_at(h->alt, RC_OFF(ni_parent)), *stock = NULL;
  for (unsigned i = 0; i < child_slots(parent) && i < 32 && !stock; i++) {  // the stock part: the first other NiNode
    uint8_t *c = child(parent, i);
    if (c && c != h->alt && kind_of(c, "NiNode")) stock = c;
  }
  const uint8_t *info = ptr_at(h->guitar, RC_OFF(guitar_info));
  int count = info ? seven_strings_info_count(info) : 0;
  h->bass = seven_strings_info_is_bass(info);
  for (int s = 0; s < SEGS; s++) {
    char name[24];
    snprintf(name, sizeof name, "string_size_%d", h->bass ? hs_bass_seg(s) : SEGS - s);
    int used = !h->bass || s < count;
    h->alt_seg[s] = used ? find_named(h->alt, name) : NULL;
    h->stock_seg[s] = used && stock ? find_named(stock, name) : NULL;
  }
  for (int e = 0; e < EXTRA; e++) {
    char name[24];
    snprintf(name, sizeof name, "string_size_%d", SEGS + 1 + e);
    h->segx[e] = h->bass ? NULL : find_named(h->alt, name);
  }
}

// Once per frame instance: our params for the model's segments, and a reference to each of the stock params' values.
// An added bass string (slots 4, 5) has no values of its own in the stock params: its segment takes its neighbour's
// (the low B the E's, the high C the G's: the bass shader has the four stock rows)
static void bind_alt(head *h) {
  int n = 0, x = 0;
  for (int s = 0; s < SEGS; s++) {
    uint8_t *params = stock_params(h, s);
    h->seg_ok[s] = 0;
    if (!params || !h->alt_seg[s] || !kind_of(h->alt_seg[s], "NiMesh")) continue;
    uint8_t *from = h->bass ? stock_params(h, hs_bass_neighbour(s)) : params;
    bind_segment(h->alt_hold[s], h->alt_seg[s], from ? from : params);
    for (int i = 0; i < SEG_FIELDS; i++) {
      uint8_t *p = ptr_at(params, seg_value(i));
      h->stock_hold[s][i] = p && h->alt_hold[s][i] ? p : NULL;
      addref(h->stock_hold[s][i]);
    }
    h->seg_ok[s] = 1;
    n++;
  }
  for (int e = 0; e < EXTRA; e++)
    if (h->segx[e] && kind_of(h->segx[e], "NiMesh")) {
      bind_segment(h->paramsx[e], h->segx[e], stock_params(h, 0));  // string_size_6's row
      x++;
    }
  h->bound = 1;
  hlog("guitar %p: %d of %d headstock string segments of the model bound; %d beyond them (string_size_7/8) on their own params",
       (void *)h->guitar, n, SEGS, x);
}

// The game's per-frame segment values go through the six stock params: point them at the model's values (or back),
// the current values carried over, so nothing is reset. The stock params of a bass's added strings have no values:
// pointed at the model's, and back to empty. The game's colour loop colours a slot only when its loaded flag is set (the
// frame's loader ran once, at the stock string count, so a bass's added slots stay unloaded): set while shown.
static void point_params(head *h, int to_alt) {
  uint8_t *head_obj = ptr_at(h->guitar, RC_OFF(guitar_headstock));
  for (int s = 0; s < SEGS; s++) {
    uint8_t *params = stock_params(h, s);
    if (!params || !h->seg_ok[s]) continue;
    for (int i = 0; i < SEG_FIELDS; i++) {
      uint8_t *from = to_alt ? h->stock_hold[s][i] : h->alt_hold[s][i], *to = to_alt ? h->alt_hold[s][i] : h->stock_hold[s][i];
      uint8_t *at = params + seg_value(i), *old = ptr_at(at, 0);
      if (!to) {  // a slot the game never bound: empty again
        if (!to_alt && from && old == from) {
          memset(at, 0, (size_t)rc_guest_ptr_size);
          dropref(from);  // still held by its mesh and by us
        }
        continue;
      }
      if (from) memcpy(FIELD(to, ni_extra_value), FIELD(from, ni_extra_value), i == COLOUR_FIELD ? 16 : 4);
      if (old == to) continue;
      addref(to);
      ptr_put(at, to);
      dropref(old);  // still held by its mesh and by us
    }
    if (h->bass && head_obj) {
      uint8_t *loaded = FIELD(head_obj, headstock_seg_loaded) + s;
      if (to_alt) {
        h->loaded_was[s] = *loaded;
        *loaded = 1;
      } else {
        *loaded = h->loaded_was[s];
      }
    }
  }
  h->pointed = to_alt;
}

// A segment draws in string_size_6's noteway layer (the game gives string_size_1..6 theirs at load; string_size_7 and 8
// kept the file's, which drew after the fret bars and over the nut; a bass's added segments weren't seen by the loader
// either: they get the E string's): the u16 of the object's layer property. 1 written, 0 equal already, -1 a layer
// property missing.
static uint8_t *layer_property(const uint8_t *obj) {
  const uint8_t *node = ptr_at(obj, RC_OFF(ni_av_properties));
  for (int i = 0; node && i < 64; i++) {
    uint8_t *prop = ptr_at(node, RC_OFF(ni_property_node_item));
    if (prop && (int32_t)OV_VCALL(arg(prop), (int)RC_SLOT(ni_property_type)) == RC_OFF(noteway_layer_type)) return prop;
    node = ptr_at(node, RC_OFF(ni_property_node_next));
  }
  return NULL;
}
static int copy_layer(head *h, const uint8_t *from_obj, uint8_t *to_obj, const char *what) {
  const uint8_t *from = layer_property(from_obj);
  uint8_t *to = layer_property(to_obj);
  if (!from || !to) return -1;
  uint16_t v = U16(from, noteway_layer_value), was = U16(to, noteway_layer_value);
  if (v == was) return 0;
  RC_ST(uint16_t, FIELD(to, noteway_layer_value), v);
  hlog("guitar %p: %s draws in layer %u, not %u", (void *)h->guitar, what, v, was);
  return 1;
}
// Once per frame instance: the segments beyond string_size_6, then a bass's added ones
static void layer_segments(head *h) {
  const uint8_t *from = h->alt_seg[0] ? h->alt_seg[0] : h->stock_seg[0];
  for (int e = 0; e < EXTRA; e++)
    if (h->segx[e] && !(h->layered >> e & 1) && from) {
      char what[24];
      snprintf(what, sizeof what, "string_size_%d", SEGS + 1 + e);
      if (copy_layer(h, from, h->segx[e], what) >= 0) h->layered |= 1 << e;
    }
  if (h->bass && !(h->layered >> EXTRA & 1) && h->alt_seg[0]) {
    int done = 1;
    for (int s = SEVEN_BASS_STOCK; s < SEGS; s++)
      if (h->alt_seg[s]) done &= copy_layer(h, h->alt_seg[0], h->alt_seg[s], "a bass's added segment") >= 0;
    if (done) h->layered |= 1 << EXTRA;
  }
}

// ---- the pegs: the PegSet slots' locators (an NiPointer at +8 of a slot) move to the model's Locator_Peg_1..6, so the
// game's placement puts the pegs (and the tuner's letters) on the model; slots 6 and 7 got Locator_Peg_7 / _8, which
// only the models have, from the game's binder: the first of them in the frame, which with two models in it may be the
// hidden one's, so they are put on the shown model's, every frame
static void find_locators(head *h) {
  uint8_t *info = ptr_at(h->guitar, RC_OFF(guitar_info));
  const char *fmt = info && U8(info, instrument_inverted) ? "Locator_Invert_Peg_%d" : "Locator_Peg_%d";
  char name[32];
  for (int s = 0; s < SEGS; s++) {
    snprintf(name, sizeof name, fmt, s + 1);
    h->alt_loc[s] = find_named(h->alt, name);
    h->stock_loc[s] = NULL;
  }
  for (int e = 0; e < EXTRA; e++) {
    snprintf(name, sizeof name, fmt, SEGS + e + 1);
    h->alt_locx[e] = find_named(h->alt, name);
  }
}

static void set_locator(uint8_t *slot, uint8_t *node) {
  uint8_t *at = FIELD(slot, peg_slot_locator), *cur = ptr_at(at, 0);
  if (!node || cur == node) return;
  addref(node);
  ptr_put(at, node);
  dropref(cur);  // still held by its tree
}

// Slots 6 and 7 on the shown model's Locator_Peg_7 / _8; how many were
static int extra_locators(head *h, uint8_t *pegs) {
  int count = U16(pegs, pegs_count), n = 0;
  for (int e = 0; e < EXTRA && SEGS + e < count; e++) {
    uint8_t *slot = peg_slot(pegs, SEGS + e);
    if (h->alt_locx[e] && RC_LD(uint8_t, slot)) {
      set_locator(slot, h->alt_locx[e]);
      n++;
    }
  }
  return n;
}

static void swap_locators(head *h, int to_alt) {
  uint8_t *pegs = pegset_of(h);
  if (!pegs) return;
  int count = U16(pegs, pegs_count), n = extra_locators(h, pegs), x = n;
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
  hlog("guitar %p: %d peg locators of PegSet %p on the %s model (%d of them beyond the sixth)", (void *)h->guitar, n,
       (void *)pegs, to_alt ? (h->five ? "one-string-fewer" : "extended") : "stock", x);
}

// The game's placement of the pegs on their locators (Rnd_PegSet_ApplyTransforms)
static void place_pegs(head *h) {
  uint8_t *pegs = pegset_of(h);
  if (pegs && U8(pegs, pegs_loaded)) OV_GUEST(pegs_apply_transforms, arg(pegs));
}

// ---- 3+3: the in-line peg in each peg's clone of peg_bakelite.nif, which carries it as rsxr_alt (culled). Shown: the
// bakelite parts culled, rsxr_alt shown, the bakelite's locator_letter / Locator_Arrow renamed, so the game's lookups by
// name (the tuner's letters, the precision widget) find the in-line ones; undone the same way.
#define LETTER "locator_letter"
#define ARROW "Locator_Arrow"
#define LETTER_OFF "rsxrs_locator_letter"
#define ARROW_OFF "rsxrs_Locator_Arrow"

static uint8_t *find_outside(uint8_t *root, uint8_t *alt, const char *want) {  // in root's children but alt's subtree
  uint8_t *found = NULL;
  for (unsigned i = 0; i < child_slots(root) && i < 32 && !found; i++) {
    uint8_t *c = child(root, i);
    if (c && c != alt) found = find_named(c, want);
  }
  return found;
}

// show: the in-line peg, else the option's own. 0 when the clone has no rsxr_alt.
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
  if (h->non_inline && !h->bass) restore_pegs(h);
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
  uint8_t *info = ptr_at(h->guitar, RC_OFF(guitar_info));
  if (h->swapped) {
    uint8_t *pegs = pegset_of(h);
    if (pegs) extra_locators(h, pegs);
    place_pegs(h);
  }
  h->info = info;
  h->non_inline = info && !U8(info, instrument_inline);
  if (h->non_inline && !h->bass) inline_pegs(h);  // a bass keeps its own pegs on the model
  int strings = info ? seven_strings_info_count(info) : 0;
  for (int e = 0; e < EXTRA; e++)  // the 8-peg model on a 7-string guitar: string_size_8 has no string to follow
    if (h->segx[e]) set_culled(h->segx[e], !hs_extra_shown(e, strings));
  layer_segments(h);
}

// The guitar's headstock is loaded: its six segment params and its PegSet (the model is bound to them once)
static int loaded(const head *h) {
  uint8_t *pegs = pegset_of(h);
  if (!pegs || !U8(pegs, pegs_loaded)) return 0;
  for (int s = 0; s < SEGS; s++)
    if (!stock_params(h, s)) return 0;
  return 1;
}

// Every frame of a guitar: want = it was built with seven strings or more (a bass: five or more)
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
  // The frame's second model has one string fewer: a bass built with five, a guitar built with seven (its frame has the
  // 8-peg model first); without one in the frame the first stays
  const uint8_t *info = ptr_at(guitar, RC_OFF(guitar_info));
  int count = info ? seven_strings_info_count(info) : 0;
  int five = info && (seven_strings_info_is_bass(info) ? count == 5 : count == 7);
  if (root != h->root || five != h->five) {  // a new frame instance, or the other model: the old pointers are forgotten
    if (root == h->root && h->applied) restore(h);  // the same frame switches models: its stock parts back first
    uint8_t *keep = h->guitar;
    memset(h, 0, sizeof *h);
    h->guitar = keep;
    h->seen = g_frame;
    h->root = root;
    h->five = five;
    h->alt = five ? find_alt_named(root, ALT5_NAME) : NULL;
    if (!h->alt) h->alt = find_alt(root);
    static int told;
    if (want && !h->alt && told++ < 4)
      hlog("guitar %p: its frame (root %p) has no %s: the stock headstock", (void *)guitar, (void *)root, ALT_NAME);
  }
  if (!h->alt) return;
  if (want) {
    show(h);
    if (++h->frames == 1)
      hlog("guitar %p shows the %d-string headstock model (%s, %d stock parts culled, first \"%s\"; inverted %u, "
           "left-handed %u, in-line %u)", (void *)guitar, count, node_name(h->alt), h->nsibs,
           h->nsibs ? node_name(h->sibs[0]) : "", h->info ? U8(h->info, instrument_inverted) : 0,
           h->info ? U8(h->info, instrument_lefty) : 0, h->info ? U8(h->info, instrument_inline) : 0);
  } else {
    restore(h);
    h->frames = 0;
    hlog("guitar %p: back to the stock headstock", (void *)guitar);
  }
}

// The width: the headstock frame narrowed across the strings by the factor the instrument's attach points were scaled
// by (seven_spacing_k), so the frame's string segments (string_size_N, static model geometry at the stock pitch) meet
// the neck strings at the nut. The frame root's rotation gets its string axis (local z) scaled by k, from a copy of
// what it was (the game's), and the pegs are placed again on their moved locators (rs7string's RsxrHead_Width).
// Every frame of an instrument built with extra strings; a frame that goes back to k = 1 gets its rotation back.
#define WIDTHS 4
static struct {
  uint8_t *guitar, *root;
  float stock[9];
  float applied;
  unsigned seen;
} g_width[WIDTHS];

static void head_width(uint8_t *guitar, int extended) {
  int at = -1, free_at = -1, old = 0;
  for (int i = 0; i < WIDTHS; i++) {
    if (g_width[i].guitar == guitar) at = i;
    if (!g_width[i].guitar && free_at < 0) free_at = i;
    if (g_width[i].seen < g_width[old].seen) old = i;
  }
  if (at < 0 && !extended) return;  // a stock instrument with nothing to restore: the common case, no call into the game
  float k = extended ? seven_spacing_k(guitar) : 1.0f;
  if (at < 0 && k == 1.0f) return;
  uint8_t *frame = ptr_at(ptr_at(guitar, RC_OFF(guitar_headstock)), RC_OFF(guitar_head_frame));
  uint8_t *root = frame ? entity_root(frame) : NULL;
  if (at >= 0 && g_width[at].root != root) {  // a new frame instance (or none): the old one went with its model
    memset(&g_width[at], 0, sizeof g_width[at]);
    at = -1;
    if (k == 1.0f) return;
  }
  if (!root || !isfinite(k)) return;
  if (at < 0) {
    at = free_at >= 0 ? free_at : old;
    g_width[at].guitar = guitar;
    g_width[at].root = root;
    g_width[at].applied = 1.0f;
    memcpy(g_width[at].stock, FIELD(root, ni_local_rotate), sizeof g_width[at].stock);
  }
  g_width[at].seen = g_frame;
  float m[9];
  hs_width_matrix(g_width[at].stock, k, m);
  memcpy(FIELD(root, ni_local_rotate), m, sizeof m);
  uint8_t *pegs = pegset_of_guitar(guitar);
  if (pegs && U8(pegs, pegs_loaded)) OV_GUEST(pegs_apply_transforms, arg(pegs));
  if (k != g_width[at].applied)
    hlog("spacing: guitar %p headstock frame root %p narrowed across the strings by k %.3f (was %.3f)", (void *)guitar,
         (void *)root, (double)k, (double)g_width[at].applied);
  g_width[at].applied = k;
  if (k == 1.0f) memset(&g_width[at], 0, sizeof g_width[at]);
}

// N5's per-frame width entry (the strings part's hook is the foundation's; this is ours): the pegs follow the frame
void seven_headstock_frame(void *guitar) {
  if (!seven_on || !g_on || !seven_feature(SEVEN_F_SPACING) || !guitar) return;
  const uint8_t *info = ptr_at(guitar, RC_OFF(guitar_info));
  int count = info ? seven_strings_info_count(info) : 0;
  head_width(guitar, seven_strings_info_is_bass(info) ? count >= 5 : count >= 7);
}

// N5 (Rnd_Guitar_Update), every frame of a guitar (the strings part's point; this is its second hook)
static void on_frame(rc_mid_frame *f, void *user) {
  (void)user;
  if (!seven_on || !g_on) return;
  uint8_t *guitar = PTR(f, "guitar");
  if (!guitar) return;
  const uint8_t *info = ptr_at(guitar, RC_OFF(guitar_info));
  int count = seven_strings_info_count(info);
  head_frame(guitar, seven_strings_info_is_bass(info) ? count >= 5 : count >= 7);
  seven_headstock_frame(guitar);
}

// ---- PGH1: the selected peg's highlight pass draws the mesh list collected per peg clone (by a walk that ignores the
// culled bit) without looking at the bit, so in a 3+3 clone the hidden knob (bakelite or in-line) glowed too. A mesh
// that is, or lies under, a culled rsxr_alt or a culled sibling of one is skipped as the pass skips its own.
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
  int bass = info && RC_LD(int32_t, FIELD(info, instrument_type)) == 2;
  int stock = bass ? SEVEN_BASS_STOCK : SEVEN_STOCK;
  if (count <= stock) return count;
  if (!seven_on || !g_on || !info) return stock;
  int have = 0;  // the pegs the models of the frame have (a bass: the largest of the frames of its hand)
  if (bass) {
    int lh = U8(info, instrument_lefty) != 0;
    for (int layout = 0; layout < 2; layout++) {
      int n = __atomic_load_n(&g_bass_merged[layout][lh], __ATOMIC_ACQUIRE);
      have = n > have ? n : have;
    }
  } else {
    int layout = U8(info, instrument_inline) ? 0 : 1, lh = U8(info, instrument_lefty) != 0;
    have = __atomic_load_n(&g_merged[layout][lh], __ATOMIC_ACQUIRE);
  }
  return have > stock ? (count < have ? count : have) : stock;
}

// A bass built with 5 or 6 strings: its PegSet's pegs. The strings part's PSL1 hook leaves a count of six or less as it is;
// the frame has Locator_Peg_5 / _6 only where it carries the bass models (the game's binder finds them there), else
// four, as the stock bass has
static void on_psl1_bass(rc_mid_frame *f, void *user) {
  (void)user;
  int count = (int32_t)SEVEN_GET(f, "count");
  const void *info = PTR(f, "info");
  if (!seven_on || !g_bass_ok || count <= SEVEN_BASS_STOCK || !seven_strings_info_is_bass(info)) return;
  int n = seven_headstock_pegs(info, count);
  SEVEN_SET(f, "count", n);
  static int told;
  if (!told++) hlog("PegSet %p: %d pegs on a bass built with %d%s", (void *)PTR(f, "pegs"), n, count,
                    n > SEVEN_BASS_STOCK ? "" : " (no extended bass headstock model)");
}

// The colour param of the model's string_size_<string + 1> (string 6 or 7) while this guitar shows it: the segment the
// game's colour loop (GSHC) gives that string's colour; NULL for any other string, a model without that segment, or
// the stock headstock
void *seven_headstock_seg_colour(const void *guitar, int string) {
  int e = string - SEGS;
  if (!seven_on || !g_on || !guitar || e < 0 || e >= EXTRA) return NULL;
  for (int i = 0; i < HEADS; i++)
    if (g_heads[i].guitar == guitar) return g_heads[i].applied && g_heads[i].segx[e] ? g_heads[i].paramsx[e][COLOUR_FIELD] : NULL;
  return NULL;
}
void *seven_headstock_seg7_colour(const void *guitar) { return seven_headstock_seg_colour(guitar, SEVEN_STOCK); }

// GSHC for the eighth string (the strings part's hooks stop at the seventh: they give string 7 no segment): the
// segment of an eight-string guitar's model takes the string's colour as the game's loop gives the others theirs
static void on_gshc_loaded(rc_mid_frame *f, void *user) {
  (void)user;
  int s = (int32_t)SEVEN_GET(f, "string");
  if (seven_on && s == SEVEN_STOCK + 1 && seven_headstock_seg_colour(PTR(f, "guitar"), s)) f->go = SEVEN_CONT(f, "loaded");
}
static void on_gshc_segment(rc_mid_frame *f, void *user) {
  (void)user;
  int s = (int32_t)SEVEN_GET(f, "string");
  void *seg = seven_on && s == SEVEN_STOCK + 1 ? seven_headstock_seg_colour(PTR(f, "guitar"), s) : NULL;
  if (!seg) return;
  SEVEN_SET(f, "segment", (uintptr_t)seg);
  f->go = SEVEN_CONT(f, "done");
}

int seven_headstock_letter_inline(const void *info) {
  int own = info ? U8(info, instrument_inline) : 0;
  if (own || !info || !seven_on || !g_on) return own;
  for (int i = 0; i < HEADS; i++)
    if (g_heads[i].guitar && g_heads[i].applied && g_heads[i].non_inline && g_heads[i].info == info) {
      static int told;
      if (once(&told)) hlog("the tuner's letters of a 3+3 or 2+2 frame showing the in-line model follow the in-line rule");
      return 1;
    }
  return 0;
}

int seven_headstock_bass_ready(void) { return g_bass_ok; }

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
  // The extended bass's pegs, and the eight-string guitar's segment colour: points added after the unit first shipped,
  // so an older RS Re-ARMed has the guitar's seven-peg headstock without them. The bass's fix is on only when its hook on
  // PSL1 (another callback of the point the strings part has) is taken; the strings part's own peg word is the other half
  // of it (it sets the bass's count to 5 or 6)
  g_bass_ok = seven_mid_optional("seven_psl1", on_psl1_bass, "the extended bass's headstock");
  seven_mid_optional("seven_gshc_loaded", on_gshc_loaded, "the eight-string guitar's headstock segment colour");
  seven_mid_optional("seven_gshc_segment", on_gshc_segment, "the eight-string guitar's headstock segment colour");
  return 0;
}
