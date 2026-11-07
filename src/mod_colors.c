// RSMods: custom string and note colors (ExtendedRangeMod.cpp, CollectColors.cpp), rainbow strings and notes
// (RainbowNotesMod.cpp) and custom highway colors (CustomHighwayColorsMod.cpp).
//
// The strings: as on Windows, the colors in the game's own table. GameColorManager (game_color_manager) has pointers to
// three floats (game_color_table): six strings for each kind of color, in two banks, normal and colorblind. Both banks are
// written here, each with its own set, and every frame, as the game can load the table again. What it had is put back
// when a mod is turned off.
//
// The notes and the highway: Windows gives Direct3D a texture of its own in the draws of certain meshes. The game
// colors these through small palette textures, so here the palettes themselves are replaced, through RS Re-ARMed's render
// API (rs_render.h), with what Windows draws: the gradient of D3D.cpp, a row per color, black to the color at 40% to white.
//
//   notes      two 256x128 ramps, 16 rows: the six strings and two inactive rows, then the same for colorblind mode. One
//              has heads, tails and open strings, the other the indicators
//   lanes      256x32, two rows: numbered and unnumbered
//   gutters    256x32
//   fret nums  512x32
//
// Found with the renderer's texture log (RS_GL_TEX_LOG). The hash is of the bytes the game uploads, and the
// uncompressed ones differ between the editions: the Mac's are RGB, Learn & Play's BGRA.
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rs_render.h"
#include "rsmods.h"

typedef struct {
  uint8_t r, g, b;
} color;

enum { RAMP_W = 256, RAMP_H = 128, RAMP_ROWS = 16 };
enum { RAMP_A, RAMP_B, LANES, GUTTERS, FRET_NUMBERS, NPALETTES };

static const struct {
  int w, h;
  uint64_t mac, learn_play;
} PALETTES[NPALETTES] = {
    [RAMP_A] = {256, 128, 0x43f3a5e256055935ull, 0x1c62a63a0c96a903ull},
    [RAMP_B] = {256, 128, 0xa849d63d48a49660ull, 0xf14add8066964ed2ull},
    [LANES] = {256, 32, 0x4ae52dfb26944aa1ull, 0x0d0dc54e00c294eeull},
    [GUTTERS] = {256, 32, 0x8e9db6ea2385c04aull, 0xc692c8b9c0541800ull},
    [FRET_NUMBERS] = {512, 32, 0xa2d05469a96005e4ull, 0x96b53f954f4db8dbull},
};

// Settings.hpp: the game's own colors, a row for each string and for the two inactive rows
static const char *const DEFAULT_N[8] = {"FF4F5A", "E2C102", "1DACF9", "FF9216", "3FCC0C", "C825ED", "0ABCB9", "909090"};
static const char *const DEFAULT_CB[8] = {"C12A2A", "A3F400", "1DACF9", "DB7F41", "00C68E", "7648A8", "493647", "4C4C4C"};

static rs_tex_rule *(*render_match)(int, int, uint64_t);
static int (*render_replace)(rs_tex_rule *, int, int, const void *);
static rs_tex_rule *g_rule[NPALETTES];
static int g_ready, g_rainbow, g_rainbow_shown, g_rainbow_strings, g_rainbow_strings_shown;
static double g_next_check, g_next_rainbow;
static char g_settings[1024];

__attribute__((constructor)) static void colors_init(void) {
  int (*version)(void) = (int (*)(void))dlsym(RTLD_DEFAULT, "rs_render_api_version");
  render_match = (rs_tex_rule * (*)(int, int, uint64_t)) dlsym(RTLD_DEFAULT, "rs_render_match");
  render_replace = (int (*)(rs_tex_rule *, int, int, const void *))dlsym(RTLD_DEFAULT, "rs_render_replace");
  if (!version || version() < 1 || !render_match || !render_replace) {
    rsm_log("colors: no render API in this RS Re-ARMed, or not on Metal: note and highway colors stay off");
    return;
  }
  int learn_play = !strcmp(rc_target(), "win-x86");
  for (int i = 0; i < NPALETTES; i++)
    g_rule[i] = render_match(PALETTES[i].w, PALETTES[i].h, learn_play ? PALETTES[i].learn_play : PALETTES[i].mac);
  g_ready = 1;
}

static int parse_color(const char *hex, color *c) {
  unsigned r, g, b;
  if (!hex || sscanf(hex, "%02x%02x%02x", &r, &g, &b) != 3) return 0;
  c->r = (uint8_t)r, c->g = (uint8_t)g, c->b = (uint8_t)b;
  return 1;
}

// D3D.cpp's CreateGradientBitmap for one row: black, the color at 40%, white, and the last three pixels white
static void gradient_row(uint8_t *row, int w, color c) {
  for (int x = 0; x < w; x++, row += 4) {
    float t = (float)x / (float)(w - 1);
    float k = t < 0.4f ? t / 0.4f : (t - 0.4f) / 0.6f;
    int white = t >= 0.4f;
    row[0] = (uint8_t)(white ? c.r + (255 - c.r) * k : c.r * k);
    row[1] = (uint8_t)(white ? c.g + (255 - c.g) * k : c.g * k);
    row[2] = (uint8_t)(white ? c.b + (255 - c.b) * k : c.b * k);
    row[3] = 255;
    if (x >= w - 3) row[0] = row[1] = row[2] = 255;
  }
}

// w x h pixels, `rows` bands of equal height, into px
static void fill_bands(uint8_t *px, int w, int h, const color *colors, int rows) {
  int band = h / rows;
  for (int i = 0; i < rows; i++) {
    uint8_t *first = px + (size_t)i * band * w * 4;
    gradient_row(first, w, colors[i]);
    for (int y = 1; y < band; y++) memcpy(first + (size_t)y * w * 4, first, (size_t)w * 4);
  }
}

static void set_ramps(const color *rows) {
  static uint8_t px[RAMP_W * RAMP_H * 4];
  fill_bands(px, RAMP_W, RAMP_H, rows, RAMP_ROWS);
  render_replace(g_rule[RAMP_A], RAMP_W, RAMP_H, px);
  render_replace(g_rule[RAMP_B], RAMP_W, RAMP_H, px);
}

// The rows of `prefix` ("string", "note") from [String Colors]; missing ones are the game's
static void read_rows(const char *prefix, color *rows) {
  for (int i = 0; i < 8; i++) {
    char key[24];
    snprintf(key, sizeof key, "%s%d_N", prefix, i);
    if (i >= 6 || !parse_color(rsm_get("String Colors", key), &rows[i])) parse_color(DEFAULT_N[i], &rows[i]);
    snprintf(key, sizeof key, "%s%d_CB", prefix, i);
    if (i >= 6 || !parse_color(rsm_get("String Colors", key), &rows[8 + i])) parse_color(DEFAULT_CB[i], &rows[8 + i]);
  }
}

// ExtendedRangeMod::UpdateNoteTexture
static void apply_notes(void) {
  const char *custom = rsm_get("Toggle Switches", "CustomStringColors");
  long mode = rsm_long("SeparateNoteColorsMode", 0);
  color rows[RAMP_ROWS];
  if (mode == 0 && custom && atoi(custom) == 2) {
    read_rows("string", rows);
    set_ramps(rows);
    rsm_log("colors: notes as the custom string colors");
  } else if (mode == 2 && rsm_on("SeparateNoteColors")) {
    read_rows("note", rows);
    set_ramps(rows);
    rsm_log("colors: custom note colors");
  } else {
    render_replace(g_rule[RAMP_A], 0, 0, NULL);
    render_replace(g_rule[RAMP_B], 0, 0, NULL);
  }
}

static void replace_single(int palette, const char *key) {
  static uint8_t px[256 * 16 * 4];
  color c;
  if (rsm_on_section("Highway Colors", "CustomHighwayColors") && parse_color(rsm_get("Highway Colors", key), &c)) {
    fill_bands(px, 256, 16, &c, 1);
    render_replace(g_rule[palette], 256, 16, px);
  } else {
    render_replace(g_rule[palette], 0, 0, NULL);
  }
}

static void apply_highway(void) {
  static uint8_t px[256 * 32 * 4];
  color lanes[2];
  if (rsm_on_section("Highway Colors", "CustomHighwayColors") &&
      parse_color(rsm_get("Highway Colors", "CustomHighwayNumbered"), &lanes[0]) &&
      parse_color(rsm_get("Highway Colors", "CustomHighwayUnNumbered"), &lanes[1])) {
    fill_bands(px, 256, 32, lanes, 2);
    render_replace(g_rule[LANES], 256, 32, px);
  } else {
    render_replace(g_rule[LANES], 0, 0, NULL);
  }
  replace_single(GUTTERS, "CustomHighwayGutter");
  replace_single(FRET_NUMBERS, "CustomFretNubmers");  // the key's name on Windows
}

// RSColor::setH, with the same clamp as ClampColorComponents
static uint8_t hue_channel(float v) {
  if (v > 1.0f) v = 1.0f;
  if (v < 0.0f) v = -v;
  return (uint8_t)(v * 255.0f);
}

static color hue_color(float degrees) {
  float rad = degrees * (float)M_PI / 180.0f, u = cosf(rad), w = sinf(rad);
  color c = {hue_channel(0.299f + 0.701f * u + 0.168f * w), hue_channel(0.299f - 0.299f * u - 0.328f * w),
             hue_channel(0.299f - 0.300f * u + 1.250f * w)};
  return c;
}

// As RainbowWorker: 2 degrees every 16 ms, a turn in about three seconds
static float rainbow_hue(double now) { return fmodf((float)now * 125.0f, 360.0f); }

// ERMode::GenerateRainbowTextures: the strings 20 degrees apart, the two inactive rows as the last string, and the same for
// colorblind mode.
static void apply_rainbow(double now) {
  float h = rainbow_hue(now);
  color rows[RAMP_ROWS];
  for (int i = 0; i < 8; i++) rows[i] = rows[8 + i] = hue_color(h + 20.0f * (float)(i < 6 ? i : 5));
  set_ramps(rows);
}

// ------------------------------------------------------------------------------------------------
// The strings
// ------------------------------------------------------------------------------------------------
// StringState.h: the kinds of color, by their order in the table
enum {
  ST_ENABLED = 1,
  ST_DISABLED = 2,
  ST_GLOW = 3,
  ST_PEGS_TUNING = 4,
  ST_PEGS_IN_TUNE = 7,
  ST_PEGS_OUT_TUNE = 8,
  ST_TEXT = 13,
  ST_BODY_NORM = 15,
  ST_BODY_ACC = 16,
  ST_COUNT = 17
};
enum { BANK_ENTRIES = 0xa8, STRINGS = 6, BANKS = 2 };

typedef struct {
  float r, g, b;
} rgb;

static const int CUSTOM_STATES[] = {ST_ENABLED,       ST_DISABLED, ST_GLOW,      ST_PEGS_TUNING, ST_PEGS_IN_TUNE,
                                    ST_PEGS_OUT_TUNE, ST_TEXT,     ST_BODY_NORM, ST_BODY_ACC};
#define NCUSTOM ((int)(sizeof CUSTOM_STATES / sizeof CUSTOM_STATES[0]))
static const int RAINBOW_STATES[] = {ST_ENABLED, ST_GLOW, ST_DISABLED};

static rgb g_custom[BANKS][NCUSTOM][STRINGS];
static int g_custom_on;
static rgb g_saved[BANKS][ST_COUNT][STRINGS];
static const char *g_saved_for;  // the manager g_saved is of; NULL: nothing written since the last restore

static const char *color_manager(void) {
  static int known = -1;
  if (known < 0) known = RC_ADDR(game_color_manager) && RC_HAS_OFF(game_color_table);
  if (!known) return NULL;
  return rc_mod_ptr((const void *)(uintptr_t)RC_A(RC_ADDR(game_color_manager)));
}

// The three floats of a color, in the game's memory; NULL if it has none
static float *string_color(const char *mgr, int bank, int state, int string) {
  int i = bank * BANK_ENTRIES + state * STRINGS + string;
  return rc_mod_ptr(mgr + RC_OFF(game_color_table) + (size_t)i * (size_t)rc_guest_ptr_size);
}

// CollectColors::RGB2HSL, with grey as hue and saturation 0
static void rgb_to_hsl(rgb c, int *h, float *s, float *l) {
  float lo = fminf(c.r, fminf(c.g, c.b)), hi = fmaxf(c.r, fmaxf(c.g, c.b)), d = hi - lo, f;
  *l = (lo + hi) / 2;
  *h = 0;
  *s = 0;
  if (d == 0.0f) return;
  *s = *l <= 0.5f ? d / (lo + hi) : d / (2 - hi - lo);
  f = c.r == hi ? (c.g - c.b) / d : c.g == hi ? 2 + (c.b - c.r) / d : 4 + (c.r - c.g) / d;
  *h = (int)(60 * f);
  if (*h < 0) *h += 360;
}

// CollectColors::HSL2RGB, with the hue brought into range: Windows' hue + 5 can pass 360 or go below 0
static rgb hsl_to_rgb(int hue, float s, float l) {
  rgb c = {0, 0, 0};
  float v = l <= 0.5f ? l * (1 + s) : l + s - l * s;
  if (v <= 0) return c;
  float h = (float)(((hue % 360) + 360) % 360) / 60.0f, lo = 2 * l - v, sv = (v - lo) / v;
  int sextant = (int)h;
  float vsf = v * sv * (h - (float)sextant), mid1 = lo + vsf, mid2 = v - vsf;
  switch (sextant) {
    case 0: c = (rgb){v, mid1, lo}; break;
    case 1: c = (rgb){mid2, v, lo}; break;
    case 2: c = (rgb){lo, v, mid1}; break;
    case 3: c = (rgb){lo, mid2, v}; break;
    case 4: c = (rgb){mid1, lo, v}; break;
    default: c = (rgb){v, lo, mid2}; break;
  }
  return c;
}

// ERMode::GetCustomColors: what each kind of color is made from a string's color. (Windows looks the two note body colors
// up by a name it doesn't have, so they come out red there; here they are what CollectColors has for them.)
static void derive_colors(int bank, int string, color from) {
  int cb = bank == 1, h;
  float s, l;
  rgb c = {from.r / 255.0f, from.g / 255.0f, from.b / 255.0f};
  rgb_to_hsl(c, &h, &s, &l);
  const rgb out[NCUSTOM] = {
      c,
      hsl_to_rgb(h, s - (cb ? 0.428f : 0.144f), l / (cb ? 2.812f : 3.136f)),
      hsl_to_rgb(h - 4, 1.0f, 0.5f),
      hsl_to_rgb(h, 1.0f, 0.25f),
      hsl_to_rgb(h + (cb ? -1 : 4), 1.0f, 0.349f),
      hsl_to_rgb(0, 0.0f, 1.0f),
      hsl_to_rgb(h + (cb ? 2 : 3), 1.0f, cb ? 0.624f : 0.625f),
      cb ? c : hsl_to_rgb(h + 5, 1.0f, 0.5f),
      cb ? hsl_to_rgb(0, 0.0f, 1.0f) : hsl_to_rgb(h + 3, 0.909f, 0.761f),
  };
  for (int i = 0; i < NCUSTOM; i++) g_custom[bank][i][string] = out[i];
}

static void read_custom_strings(void) {
  const char *custom = rsm_get("Toggle Switches", "CustomStringColors");
  color rows[RAMP_ROWS];
  g_custom_on = custom && atoi(custom) == 2;
  read_rows("string", rows);
  for (int i = 0; i < STRINGS; i++) {
    derive_colors(0, i, rows[i]);
    derive_colors(1, i, rows[8 + i]);
  }
}

static void save_strings(const char *mgr) {
  for (int b = 0; b < BANKS; b++)
    for (int st = 0; st < ST_COUNT; st++)
      for (int i = 0; i < STRINGS; i++) {
        const float *p = string_color(mgr, b, st, i);
        if (p) memcpy(&g_saved[b][st][i], p, sizeof(rgb));
      }
  g_saved_for = mgr;
}

static void restore_strings(const char *mgr) {
  if (g_saved_for == mgr)
    for (int b = 0; b < BANKS; b++)
      for (int st = 0; st < ST_COUNT; st++)
        for (int i = 0; i < STRINGS; i++) {
          float *p = string_color(mgr, b, st, i);
          if (p) memcpy(p, &g_saved[b][st][i], sizeof(rgb));
        }
  g_saved_for = NULL;
}

static void apply_strings(double now) {
  const char *mgr = color_manager();
  if (!mgr) return;
  if (!g_custom_on && !g_rainbow_strings) {
    if (g_saved_for) restore_strings(mgr);
    return;
  }
  if (g_saved_for != mgr) save_strings(mgr);
  rgb rainbow[STRINGS];
  if (g_rainbow_strings)
    for (int i = 0; i < STRINGS; i++) {
      color c = hue_color(rainbow_hue(now) + 20.0f * (float)i);
      rainbow[i] = (rgb){c.r / 255.0f, c.g / 255.0f, c.b / 255.0f};
    }
  for (int b = 0; b < BANKS; b++)
    for (int i = 0; i < STRINGS; i++) {
      if (g_custom_on)
        for (int k = 0; k < NCUSTOM; k++) {
          float *p = string_color(mgr, b, CUSTOM_STATES[k], i);
          if (p) memcpy(p, &g_custom[b][k][i], sizeof(rgb));
        }
      if (g_rainbow_strings)
        for (int k = 0; k < 3; k++) {
          float *p = string_color(mgr, b, RAINBOW_STATES[k], i);
          if (p) memcpy(p, &rainbow[i], sizeof(rgb));
        }
    }
}

// What the palettes and string colors are made from, in one string
static void read_settings(char *out, size_t n) {
  static const char *const STRING_KEYS[] = {"string", "note"};
  size_t used = (size_t)snprintf(out, n, "%s %d %ld", rsm_get("Toggle Switches", "CustomStringColors") ?: "",
                                 rsm_on("SeparateNoteColors"), rsm_long("SeparateNoteColorsMode", 0));
  for (int k = 0; k < 2; k++)
    for (int i = 0; i < 6; i++)
      for (int cb = 0; cb < 2 && used < n; cb++) {
        char key[24];
        snprintf(key, sizeof key, "%s%d_%s", STRING_KEYS[k], i, cb ? "CB" : "N");
        used += (size_t)snprintf(out + used, n - used, " %s", rsm_get("String Colors", key) ?: "");
      }
  static const char *const HIGHWAY_KEYS[] = {"CustomHighwayColors", "CustomHighwayNumbered", "CustomHighwayUnNumbered",
                                             "CustomHighwayGutter", "CustomFretNubmers"};
  for (int i = 0; i < 5 && used < n; i++)
    used += (size_t)snprintf(out + used, n - used, " %s", rsm_get("Highway Colors", HIGHWAY_KEYS[i]) ?: "");
}

static void tick(void) {
  double now = rsm_now();
  if (g_rainbow && !rsm_on("RainbowNotes")) g_rainbow = 0;
  if (g_rainbow_strings && !rsm_on("RainbowStrings")) g_rainbow_strings = 0;
  if (g_rainbow_strings != g_rainbow_strings_shown) {
    g_rainbow_strings_shown = g_rainbow_strings;
    rsm_log("colors: rainbow strings %s", g_rainbow_strings ? "on" : "off");
  }
  if (g_ready) {
    if (g_rainbow && now >= g_next_rainbow) {
      g_next_rainbow = now + 1.0 / 30.0;
      apply_rainbow(now);
    }
    if (g_rainbow != g_rainbow_shown) {
      g_rainbow_shown = g_rainbow;
      rsm_log("colors: rainbow notes %s", g_rainbow ? "on" : "off");
      if (!g_rainbow) apply_notes();
    }
  }
  if (now >= g_next_check) {
    g_next_check = now + 0.5;
    char settings[sizeof g_settings];
    read_settings(settings, sizeof settings);
    if (strcmp(settings, g_settings)) {
      strcpy(g_settings, settings);
      read_custom_strings();
      if (g_ready) {
        if (!g_rainbow) apply_notes();
        apply_highway();
      }
    }
  }
  apply_strings(now);
}

static void key(const rsm_key_event *e) {
  if (e->down) return;
  if (g_ready && rsm_on("RainbowNotes") && e->keycode == rsm_key("RainbowNotesKey")) g_rainbow = !g_rainbow;
  if (rsm_on("RainbowStrings") && e->keycode == rsm_key("RainbowStringsKey")) g_rainbow_strings = !g_rainbow_strings;
}

const rsm_mod rsm_mod_colors = {"colors", tick, key, NULL, NULL};
