// Seven-String Guitar (seven.h): the colours part, rs7string's native colour manager (github.com/SomeeName/rs7string,
// MIT, by SomeeName: src/features/colours, its ARCHITECTURE sections 4 and 5).
//
// The game's colour manager (GameColorManager, game_color_manager) has, for each colour mode (normal, colourblind) and
// each of 28 kinds of colour (a string lit, dimmed, its glow, its peg, a note's parts, ...), a column per string: a
// pointer to the colour (three floats) and a V, where a note's ramp texture is sampled (or for a few kinds an index).
// The game has six columns. The edits (CM*) give it eight: tables of 2 x 28 x 8 appended at seven_color_table (the
// pointers) and seven_color_uv (the V), the manager grown to hold them, and every reader and writer of the game moved
// to them. The stock six-column tables stay, holding the first six columns of every kind: what writes there by the
// stock layout, as RSMods does on Windows and mod_colors.c / mod_extended_range.c do here (string colours, rainbow
// strings, Extended Range), writes through those pointers into the colours the game reads.
//
// The constructor and the build of the tables fill columns 0 to 5 (and 6 and 7 with the constructor's default); at
// their ends (seven_cminit, seven_cmbuild) the columns of strings 6 and 7 are filled in, and the first six copied into
// the stock tables. String 6 gets its own colour and the kinds made from it as string 0's are from its colour (dimmed
// to the other strings' dimmed brightness, the glow at full brightness, other kinds as bright as string 0's); kinds the
// strings share keep string 0's. String 7 (an eighth string) has a placeholder colour. Two places of the table's build
// write a kind for all columns at once (seven_cmuv, seven_cmix), and a function that gives a string's colour as
// 0xRRGGBB from six on its stack (seven_cmfall) gets strings 6 and 7.
//
// String 6's colour ([Seven String] in RSMods.ini): string6_N, and string6_CB in colourblind mode (string6_N if
// empty); with RSMods' ExtendedRange on, and ExtendedRangeColor on, RSMods' low B instead in both modes, as rs7string
// takes it (00C68E, or string0_CB with CustomStringColors = 2). A change of these is published on the game thread.
//
// A note's colour comes from a ramp texture (seven_colours.h): string 6's V samples lines of the grey row no row is
// sampled at (V6, rs7string's), so string 6 is grey with the game's own ramp. With the render API, mod_colors.c draws
// a ramp with string 6's colour on those lines (seven_colours_game_ramp, seven_colours_ramp): rs7string repaints the
// same lines in a copy of the Direct3D texture.
//
// What rs7string has and this part hasn't: the check that the game built no manager before the edits (they are on
// before the game runs here), the D3D ramp copies and their self-check (the render API replaces the texture whole, and
// the game's ramp is read from its archive), the glow fix for RSMods' custom colours (GLW: mod_colors.c derives a glow
// without RSMods' bug).
#include <dlfcn.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "rsmods_tuning.h"
#include "seven.h"

#define SECTION "Seven String"

enum { MODES = SEVEN_COLOUR_MODES, KINDS = SEVEN_COLOUR_KINDS, COLUMNS = SEVEN_COLOUR_COLUMNS, STOCK_COLUMNS = 6 };
enum { EXTRA = COLUMNS - STOCK_COLUMNS };  // strings 6 and 7

// String 6's colour without settings: light grey (rs7string's), and RSMods' Extended Range low B
// (ExtendedRangeMode.hpp: colorsNormal[0], 0, 0.7764706, 0.5568628)
#define DEFAULT_RGB 0xC8C8C8u
#define RSMODS_LOW_B 0x00C68Eu
// String 7's placeholder until it has its own: teal, colourblind orange (rs7string's)
static const uint32_t PLACEHOLDER[MODES] = {0x00D6B2, 0xFF8C00};

// The kinds whose V samples the note ramp (16 rows), and the ones with a V of a six-row texture (11) or an index (14)
static const int RAMP_KINDS[] = {15, 17, 21};
static const int COPIED_KINDS[] = {11, 14};
// String 6's V in the ramp: lines 56 and 57 of 128, and 120 and 121, in the grey rows 7 and 15, where no row's V is
static const float V6[MODES] = {57.0f / 128.0f, 121.0f / 128.0f};

static float g_rgb[MODES][3];   // string 6's colour
static uint32_t g_rgb_hex[MODES];
static uint32_t g_fallback;     // seven_cmfall's string 6, 0xRRGGBB
static float *g_own;            // strings 6 and 7's colours, [mode][kind][extra][3]: the game's memory, the manager's
static uint8_t *g_built;        // the manager the last seven_cmbuild finished
static pthread_t g_build_thread;
static int g_build_thread_known;
static atomic_int g_dirty;      // a colour change not yet in the manager
static unsigned g_serial;       // string 6's ramp lines changed

// ------------------------------------------------------------------------------------------------
// Colours
// ------------------------------------------------------------------------------------------------
static float brightest(const float c[3]) {
  float m = c[0] > c[1] ? c[0] : c[1];
  return m > c[2] ? m : c[2];
}

void seven_colours_dim(const float rgb[3], float dim, float out[3]) {
  float m = brightest(rgb), k = m > 0.0f ? dim / m : 0.0f;
  for (int i = 0; i < 3; i++) out[i] = rgb[i] * k;
}

void seven_colours_glow(const float rgb[3], float out[3]) {
  float m = brightest(rgb), k = m > 0.0f ? 1.0f / m : 0.0f;
  for (int i = 0; i < 3; i++) out[i] = rgb[i] * k;
}

static void to_floats(uint32_t rgb, float out[3]) {
  out[0] = (float)((rgb >> 16) & 0xff) / 255.0f;
  out[1] = (float)((rgb >> 8) & 0xff) / 255.0f;
  out[2] = (float)(rgb & 0xff) / 255.0f;
}

// RRGGBB, "#" before it allowed, blanks around it
static int parse_rgb(const char *s, uint32_t *rgb) {
  if (!s) return 0;
  while (*s == ' ' || *s == '\t') s++;
  if (*s == '#') s++;
  uint32_t v = 0;
  int n = 0;
  for (; *s && *s != ' ' && *s != '\t'; s++, n++) {
    int d = *s >= '0' && *s <= '9'   ? *s - '0'
            : *s >= 'a' && *s <= 'f' ? *s - 'a' + 10
            : *s >= 'A' && *s <= 'F' ? *s - 'A' + 10
                                     : -1;
    if (d < 0 || n >= 6) return 0;
    v = v * 16 + (uint32_t)d;
  }
  while (*s == ' ' || *s == '\t') s++;
  if (n != 6 || *s) return 0;
  *rgb = v;
  return 1;
}

// RSMods' low B for string 6 (rs7string's rs7rsmods::Read): with ExtendedRange on, 00C68E with CustomStringColors
// 0 or 1 (ZZ's colours, or the game's), string0_CB with 2; 0 for none
static int rsmods_low_b(uint32_t *rgb) {
  if (!rsm_on("ExtendedRange")) return 0;
  const char *mode = rsm_get("Toggle Switches", "CustomStringColors");
  if (!mode || !*mode || !strcmp(mode, "0") || !strcmp(mode, "1")) {
    *rgb = RSMODS_LOW_B;
    return 1;
  }
  return !strcmp(mode, "2") && parse_rgb(rsm_get("String Colors", "string0_CB"), rgb);
}

// String 6's colour in both modes, from the settings; where it is from in *why
static void read_colours(uint32_t rgb[MODES], const char **why) {
  const char *n = rsm_get(SECTION, "string6_N"), *cb = rsm_get(SECTION, "string6_CB"),
             *follow = rsm_get(SECTION, "ExtendedRangeColor");
  uint32_t er;
  if ((!follow || !*follow || !strcasecmp(follow, "on")) && rsmods_low_b(&er)) {
    rgb[0] = rgb[1] = er;
    *why = "RSMods' Extended Range low B";
    return;
  }
  rgb[0] = DEFAULT_RGB;
  if (n && *n && !parse_rgb(n, &rgb[0])) rsm_log("seven-string: [%s] string6_N = %s isn't RRGGBB", SECTION, n);
  rgb[1] = rgb[0];
  if (cb && *cb && !parse_rgb(cb, &rgb[1])) rsm_log("seven-string: [%s] string6_CB = %s isn't RRGGBB", SECTION, cb);
  *why = SECTION;
}

// 1 if string 6's colour changed
static int update_colours(void) {
  uint32_t rgb[MODES];
  const char *why;
  read_colours(rgb, &why);
  if (!memcmp(rgb, g_rgb_hex, sizeof rgb)) return 0;
  memcpy(g_rgb_hex, rgb, sizeof rgb);
  for (int m = 0; m < MODES; m++) to_floats(rgb[m], g_rgb[m]);
  g_fallback = (uint32_t)(g_rgb[0][0] * 255.0f) << 16 | (uint32_t)(g_rgb[0][1] * 255.0f) << 8 |
               (uint32_t)(g_rgb[0][2] * 255.0f);
  g_serial++;
  rsm_log("seven-string: string 6's colour %06X, colourblind %06X (%s)", rgb[0], rgb[1], why);
  return 1;
}

const float *seven_colours_rgb(int mode) { return g_rgb[mode == 1]; }

// ------------------------------------------------------------------------------------------------
// The manager
// ------------------------------------------------------------------------------------------------
uint8_t *seven_colours_manager(void) {
  uint64_t a = RC_ADDR(game_color_manager);
  return a ? rc_mod_ptr((const void *)(uintptr_t)RC_A(a)) : NULL;
}

int seven_colours_mode(const uint8_t *mgr, int player) {
  return (int)RC_LD(uint32_t, mgr + RC_OFF(game_color_mode) + 4 * (player == 1));
}

// A column's colour pointer (its place in the manager): the eight-column table, or the stock one
static uint8_t *pointer_at(const uint8_t *mgr, int stock, int mode, int kind, int column) {
  int64_t table = stock ? RC_OFF(game_color_table) : RC_OFF(seven_color_table);
  int columns = stock ? STOCK_COLUMNS : COLUMNS;
  return (uint8_t *)mgr + table + (size_t)((mode * KINDS + kind) * columns + column) * (size_t)rc_guest_ptr_size;
}

static uint8_t *v_at(const uint8_t *mgr, int stock, int mode, int kind, int column) {
  int64_t table = stock ? RC_OFF(game_color_uv) : RC_OFF(seven_color_uv);
  int columns = stock ? STOCK_COLUMNS : COLUMNS;
  return (uint8_t *)mgr + table + (size_t)((mode * KINDS + kind) * columns + column) * 4;
}

const float *seven_colours_get(const uint8_t *mgr, int mode, int kind, int string) {
  if (!mgr || mode < 0 || mode >= MODES || kind < 0 || kind >= KINDS || string < 0 || string >= COLUMNS) return NULL;
  return rc_mod_ptr(pointer_at(mgr, 0, mode, kind, string));
}

static void read_rgb(const float *p, float out[3]) {
  for (int i = 0; i < 3; i++) out[i] = p ? RC_LD(float, &p[i]) : 0.0f;
}

static void put_pointer(uint8_t *at, const void *host) {
  uint64_t g = RC_GUEST(host);
  if (rc_guest_ptr_size == 8)
    RC_ST(uint64_t, at, g);
  else
    RC_ST(uint32_t, at, (uint32_t)g);
}

// String 6 + extra's colour in a kind and mode (g_own), what its column points at
static float *own(int mode, int kind, int extra) { return g_own + ((mode * KINDS + kind) * EXTRA + extra) * 3; }

// The kinds each string has its own colour of (rs7string's PerString); in the others the strings share string 0's
static int per_string(int kind) {
  return kind <= 4 || kind == 7 || kind == 8 || (kind >= 11 && kind <= 13) || (kind >= 15 && kind <= 17) || kind >= 25;
}

// The game's strings' dimmed brightness, as their mean
static float dim_of(const uint8_t *mgr, int mode) {
  float total = 0.0f;
  int n = 0;
  for (int s = 0; s < STOCK_COLUMNS; s++) {
    const float *p = seven_colours_get(mgr, mode, SEVEN_KIND_DIM, s);
    if (!p) continue;
    float c[3];
    read_rgb(p, c);
    total += brightest(c);
    n++;
  }
  return n && total > 0.0f ? total / (float)n : SEVEN_COLOURS_DIM;
}

// rs7string's ExtraColour: string 0's colour in a shared kind; else the string's own, dimmed, glowing, or as bright
// as string 0's is in that kind against its lit colour
static void extra_colour(const uint8_t *mgr, int mode, int kind, int extra, float dim, float out[3]) {
  float stock[3], lit[3], base[3];
  read_rgb(seven_colours_get(mgr, mode, kind, 0), stock);
  read_rgb(seven_colours_get(mgr, mode, SEVEN_KIND_LIT, 0), base);
  memcpy(out, stock, sizeof stock);
  if (!per_string(kind)) return;
  if (extra == 0)
    memcpy(lit, g_rgb[mode], sizeof lit);
  else
    to_floats(PLACEHOLDER[mode], lit);
  memcpy(out, lit, sizeof lit);
  float baseline = brightest(base);
  if (kind == SEVEN_KIND_DIM)
    seven_colours_dim(lit, dim, out);
  else if (kind == SEVEN_KIND_GLOW)
    seven_colours_glow(lit, out);
  else if ((kind == 0 || kind == SEVEN_KIND_PEG || kind == 7 || kind == 8) && baseline > 0.0f)
    for (int i = 0; i < 3; i++) out[i] = lit[i] * (brightest(stock) / baseline);
}

// Strings 6 and 7 into a manager the game has filled, and its first six columns into the stock tables
static void publish(uint8_t *mgr) {
  if (!mgr) return;
  if (!g_own && !(g_own = rc_guest_alloc((size_t)MODES * KINDS * EXTRA * 3 * sizeof(float)))) {
    static int told;
    if (!told++) rsm_log("seven-string: no game memory for string 6's colours");
    return;
  }
  size_t ptr = (size_t)rc_guest_ptr_size;
  for (int m = 0; m < MODES; m++)
    for (int k = 0; k < KINDS; k++) {
      memcpy(pointer_at(mgr, 1, m, k, 0), pointer_at(mgr, 0, m, k, 0), STOCK_COLUMNS * ptr);
      memcpy(v_at(mgr, 1, m, k, 0), v_at(mgr, 0, m, k, 0), STOCK_COLUMNS * 4);
    }
  for (int m = 0; m < MODES; m++) {
    float dim = dim_of(mgr, m);
    for (int k = 0; k < KINDS; k++)
      for (int e = 0; e < EXTRA; e++) {
        float c[3];
        extra_colour(mgr, m, k, e, dim, c);
        for (int i = 0; i < 3; i++) RC_ST(float, &own(m, k, e)[i], c[i]);
        put_pointer(pointer_at(mgr, 0, m, k, STOCK_COLUMNS + e), own(m, k, e));
      }
    // String 6 samples the note ramp at V6, string 7 at string 5's row until it has its own; kinds 11 and 14 as
    // strings 0 and 5
    for (size_t i = 0; i < sizeof RAMP_KINDS / sizeof *RAMP_KINDS; i++) {
      RC_ST(float, v_at(mgr, 0, m, RAMP_KINDS[i], 6), V6[m]);
      RC_ST(uint32_t, v_at(mgr, 0, m, RAMP_KINDS[i], 7), RC_LD(uint32_t, v_at(mgr, 0, m, RAMP_KINDS[i], 5)));
    }
    for (size_t i = 0; i < sizeof COPIED_KINDS / sizeof *COPIED_KINDS; i++) {
      RC_ST(uint32_t, v_at(mgr, 0, m, COPIED_KINDS[i], 6), RC_LD(uint32_t, v_at(mgr, 0, m, COPIED_KINDS[i], 0)));
      RC_ST(uint32_t, v_at(mgr, 0, m, COPIED_KINDS[i], 7), RC_LD(uint32_t, v_at(mgr, 0, m, COPIED_KINDS[i], 5)));
    }
  }
}

void seven_colours_drain(void) {
  if (!seven_on || !atomic_load(&g_dirty) || !g_build_thread_known) return;
  if (!pthread_equal(pthread_self(), g_build_thread)) {
    static int told;
    if (!told++) rsm_log("seven-string: the colour manager is another thread's: a colour change waits for its build");
    return;
  }
  uint8_t *mgr = seven_colours_manager();
  if (mgr && mgr == g_built && atomic_exchange(&g_dirty, 0)) publish(mgr);
}

// ------------------------------------------------------------------------------------------------
// The mid points
// ------------------------------------------------------------------------------------------------
// The end of the constructor: the tables have its defaults
static void cminit(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  uint8_t *mgr = (uint8_t *)(uintptr_t)SEVEN_GET(f, "manager");
  g_built = NULL;
  publish(mgr);
}

// The end of the tables' build: what the game reads from now on
static void cmbuild(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  uint8_t *mgr = (uint8_t *)(uintptr_t)SEVEN_GET(f, "manager");
  static uint8_t *logged;
  if (logged != mgr) {
    logged = mgr;
    rsm_log("seven-string: the colour manager has eight columns");
  }
  g_build_thread = pthread_self();
  g_build_thread_known = 1;
  atomic_store(&g_dirty, 0);  // this publish has every change so far
  publish(mgr);
  g_built = mgr;
}

// A kind's V for every column: the stock store to column 0 is skipped, the value goes to columns 0, 6 and 7 (the
// edited stores after it write 1 to 5)
static void cmuv(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  uint8_t *v = (uint8_t *)(uintptr_t)SEVEN_GET(f, "manager") + RC_OFF(seven_color_uv);
  uint32_t entry = (uint32_t)SEVEN_GET(f, "entry"), bits = (uint32_t)SEVEN_GET(f, "v_bits");
  static const int columns[] = {0, 6, 7};
  for (int i = 0; i < 3; i++) RC_ST(uint32_t, v + 4 * (entry + (uint32_t)columns[i]), bits);
  f->go = SEVEN_CONT(f, "skip");
}

// Kind 14's index for every column: its first entry x8 (the game's x6 skipped), and the value to columns 6 and 7
static void cmix(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  uint8_t *v = (uint8_t *)(uintptr_t)SEVEN_GET(f, "manager") + RC_OFF(seven_color_uv);
  uint32_t entry = (uint32_t)SEVEN_GET(f, "kind") * COLUMNS, bits = (uint32_t)SEVEN_GET(f, "value_bits");
  SEVEN_SET(f, "entry", entry);
  RC_ST(uint32_t, v + 4 * (entry + 6), bits);
  RC_ST(uint32_t, v + 4 * (entry + 7), bits);
  f->go = SEVEN_CONT(f, "skip");
}

// A string's colour as 0xRRGGBB: strings 0 to 5 from the game's six, 6 and 7 these (past the six there is no more)
static void cmfall(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  uint32_t s = (uint32_t)SEVEN_GET(f, "string");
  if (s < STOCK_COLUMNS) return;
  SEVEN_SET(f, "rgb", s == 6 ? g_fallback : s == 7 ? PLACEHOLDER[0] : 0);
  f->go = SEVEN_CONT(f, "skip");
}

// ------------------------------------------------------------------------------------------------
// The note ramp
// ------------------------------------------------------------------------------------------------
enum { RAMP_W = 256, RAMP_H = 128, RAMP_ROWS = 16 };
#define RAMP_FILE "assets/gameplay/tail/note_gradient_map.dds"
// The hash of the ramp as the game gives it to the renderer (mod_colors.c's RAMP_A): BGRA in Learn & Play, RGB on the
// Mac
#define RAMP_HASH_LEARN_PLAY 0x1c62a63a0c96a903ull
#define RAMP_HASH_MAC 0x43f3a5e256055935ull

// A channel of the grey line g recoloured to c around its anchor a, the grey's value where a row has its colour
// (rs7string's Rs7_Recolor): black to c up to a, c to white above
static uint8_t recolour(uint8_t g, uint8_t a, uint8_t c) {
  if (a == 0) return g == 0 ? 0 : (uint8_t)(c + ((255 - c) * g + 127) / 255);
  if (g <= a) return (uint8_t)((c * g + a / 2) / a);
  if (a == 255) return c;
  return (uint8_t)(c + ((255 - c) * (g - a) + (255 - a) / 2) / (255 - a));
}

int seven_colours_ramp(uint8_t *rgba, int w, int h) {
  if (!seven_on || !rgba || w < 8 || h != RAMP_H) return 0;
  size_t line = (size_t)w * 4;
  int knee = w * 2 / 5;  // 40% across: where the game's rows (and mod_colors.c's) have their colour
  for (int m = 0; m < MODES; m++) {
    int row = m ? 15 : 7, from = row * (h / RAMP_ROWS) + h / (2 * RAMP_ROWS);
    int to = (int)(V6[m] * (float)h) - 1;  // the two lines around V6
    uint8_t c[3] = {(uint8_t)(g_rgb[m][0] * 255.0f + 0.5f), (uint8_t)(g_rgb[m][1] * 255.0f + 0.5f),
                    (uint8_t)(g_rgb[m][2] * 255.0f + 0.5f)};
    const uint8_t *src = rgba + (size_t)from * line, *anchor = src + knee * 4;
    uint8_t *out = rgba + (size_t)to * line;
    for (int x = 0; x < w; x++) {
      for (int k = 0; k < 3; k++) out[x * 4 + k] = recolour(src[x * 4 + k], anchor[k], c[k]);
      out[x * 4 + 3] = src[x * 4 + 3];
    }
    memcpy(out + line, out, line);
  }
  return 1;
}

// The game's ramp, RGBA, from static.psarc, if it is the one the game gives the renderer
static uint8_t *read_game_ramp(void) {
  int (*version)(void) = (int (*)(void))dlsym(RTLD_DEFAULT, "rs_render_api_version");
  uint64_t (*hash)(const void *, size_t) = (uint64_t(*)(const void *, size_t))dlsym(RTLD_DEFAULT, "rs_render_hash");
  if (!version || version() < 1 || !hash) {
    rsm_log("seven-string: no render API: string 6's notes are grey");
    return NULL;
  }
  uint8_t key[32];
  uint64_t key_at = RC_ADDR(key_psarc_hex);
  rsm_psarc *a = key_at && rsm_psarc_key_from_hex((const char *)(uintptr_t)RC_A(key_at), key)
                     ? rsm_psarc_open("static.psarc", key)
                     : NULL;
  uint8_t *dds = NULL;
  size_t n = 0;
  for (int i = 0; a && i < rsm_psarc_files(a) && !dds; i++)
    if (!strcasecmp(rsm_psarc_name(a, i), RAMP_FILE)) dds = rsm_psarc_read(a, i, &n);
  rsm_psarc_close(a);
  // A DDS of 256 x 128, 24-bit RGB (masks ff0000, ff00, ff: B, G, R in memory), its pixels after the 128-byte header
  size_t pixels = (size_t)RAMP_W * RAMP_H;
  int ok = dds && n >= 128 + pixels * 3 && !memcmp(dds, "DDS ", 4) && RC_LD(uint32_t, dds + 12) == RAMP_H &&
           RC_LD(uint32_t, dds + 16) == RAMP_W && (RC_LD(uint32_t, dds + 80) & 0x40) &&
           RC_LD(uint32_t, dds + 88) == 24 && RC_LD(uint32_t, dds + 92) == 0xff0000 &&
           RC_LD(uint32_t, dds + 96) == 0xff00 && RC_LD(uint32_t, dds + 100) == 0xff;
  uint8_t *rgba = ok ? malloc(pixels * 4) : NULL, *game = ok ? malloc(pixels * 4) : NULL;
  if (!rgba || !game) ok = 0;
  if (ok) {
    int learn_play = !strcmp(rc_target(), "win-x86");
    const uint8_t *p = dds + 128;
    for (size_t i = 0; i < pixels; i++, p += 3) {
      uint8_t *o = rgba + i * 4;
      o[0] = p[2], o[1] = p[1], o[2] = p[0], o[3] = 255;
      if (learn_play)
        memcpy(game + i * 4, p, 3), game[i * 4 + 3] = 255;
      else
        memcpy(game + i * 3, o, 3);
    }
    uint64_t want = learn_play ? RAMP_HASH_LEARN_PLAY : RAMP_HASH_MAC;
    ok = hash(game, pixels * (learn_play ? 4 : 3)) == want;
  }
  if (!ok) {
    rsm_log("seven-string: %s in static.psarc %s: string 6's notes are grey", RAMP_FILE,
            !dds ? "not read" : "isn't the ramp the game draws");
    free(rgba);
    rgba = NULL;
  }
  free(game);
  free(dds);
  return rgba;
}

const uint8_t *seven_colours_game_ramp(void) {
  static int tried;
  static uint8_t *game, *ramp;
  static unsigned painted;
  if (!seven_on) return NULL;
  if (!tried) {
    tried = 1;
    game = read_game_ramp();
    if (game && !(ramp = malloc((size_t)RAMP_W * RAMP_H * 4))) {
      free(game);
      game = NULL;
    }
    if (game) painted = g_serial - 1;
  }
  if (!game) return NULL;
  if (painted != g_serial) {
    memcpy(ramp, game, (size_t)RAMP_W * RAMP_H * 4);
    seven_colours_ramp(ramp, RAMP_W, RAMP_H);
    painted = g_serial;
  }
  return ramp;
}

unsigned seven_colours_ramp_serial(void) { return g_serial; }

// ------------------------------------------------------------------------------------------------
// The settings, on the game thread after its frame
// ------------------------------------------------------------------------------------------------
static void frame(rc_ctx *c, void *user) {
  static double next;
  if (!seven_on) return;
  double now = rsm_now();
  if (now < next) return;
  next = now + 0.5;
  if (update_colours()) atomic_store(&g_dirty, 1);
  seven_colours_drain();
}

int seven_colours_init(void) {
  static const struct {
    const char *name;
    rc_mid_fn fn;
  } points[] = {{"seven_cminit", cminit},
                {"seven_cmbuild", cmbuild},
                {"seven_cmuv", cmuv},
                {"seven_cmix", cmix},
                {"seven_cmfall", cmfall}};
  for (size_t i = 0; i < sizeof points / sizeof *points; i++)
    if (seven_mid(points[i].name, points[i].fn)) return -1;
  update_colours();
  if (rc_hook_add_name("game_frame", NULL, frame, NULL))
    rsm_log("seven-string: no frame hook: a change of string 6's colour needs a restart");
  return 0;
}
