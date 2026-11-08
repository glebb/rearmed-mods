// Custom string and note colors, rainbow strings and notes, and custom highway colors: RSMods' features, with its
// settings, written for this build.
//
// The strings: the colors in the game's own table. GameColorManager (game_color_manager) has pointers to
// three floats (game_color_table): six strings for each kind of color, in two banks, normal and colorblind. Both banks are
// written here, each with its own set, and every frame, as the game can load the table again. What it had is put back
// when a mod is turned off.
//
// The notes and the highway: the game colors these through small palette textures, so the palettes themselves are
// replaced, through RS Re-ARMed's render API (rs_render.h): a row per color, from black through the color to white.
//
//   notes      two 256x128 ramps, 16 rows: the six strings and two inactive rows, then the same for colorblind mode. One
//              has heads, tails and open strings, the other the indicators
//   lanes      256x32, two rows: numbered and unnumbered
//   gutters    256x32
//   fret nums  512x32
//
// Found with the renderer's texture log (RS_GL_TEX_LOG). The hash is of the bytes the game uploads, and the
// uncompressed ones differ between the editions: the Mac's are RGB, Learn & Play's BGRA.
//
// Extended Range (mod_extended_range.c says when): RSMods turns the game's colorblind mode on and writes the extended
// range colors into the colorblind bank. Here the game's mode stays, and both banks get them: ZZ's colors with
// CustomStringColors = 1, the player's colorblind ones with 2, the game's own colorblind bank with 0. The notes take
// their colorblind rows (ZZ's colors with 1).
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "color_math.h"
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

// The game's own colors, a row for each string and for the two inactive rows
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

// A row of a palette, with white at the very end, where the game's palettes have their highlight
static void gradient_row(uint8_t *row, int w, color c) {
  for (int x = 0; x < w; x++, row += 4) {
    float t = (float)x / (float)(w - 1);
    int end = x >= w - 3;
    row[0] = end ? 255 : ramp_channel(c.r, t);
    row[1] = end ? 255 : ramp_channel(c.g, t);
    row[2] = end ? 255 : ramp_channel(c.b, t);
    row[3] = 255;
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

// ZZ's (ZagatoZee's) extended range colors, by kind, low string first (RSMods' ExtendedRangeMode.hpp)
enum { ZZ_ENABLED, ZZ_GLOW, ZZ_DISABLED, ZZ_PEGS_TUNING, ZZ_PEGS_IN_TUNE, ZZ_PEGS_OUT_TUNE, ZZ_TEXT, ZZ_BODY_NORM,
       ZZ_BODY_ACC, ZZ_KINDS };
static const rgb ZZ[ZZ_KINDS][6] = {
    [ZZ_ENABLED] = {{0.0f, 0.7764706f, 0.5568628f}, {1.0f, 0.3098039f, 0.3529412f}, {0.8862745f, 0.7568628f, 0.007843138f},
                    {0.1137255f, 0.6745098f, 0.9764706f}, {1.0f, 0.572549f, 0.08627451f}, {0.2470588f, 0.8f, 0.04705882f}},
    [ZZ_GLOW] = {{0.0f, 1.0f, 0.6470588f}, {1.0f, 0.0f, 0.0627451f}, {1.0f, 0.7803922f, 0.0f}, {0.0f, 0.6627451f, 1.0f},
                 {1.0f, 0.4470588f, 0.0f}, {0.2627451f, 1.0f, 0.0f}},
    [ZZ_DISABLED] = {{0.0f, 0.2980392f, 0.2117647f}, {0.2980392f, 0.09019608f, 0.1058824f}, {0.2980392f, 0.254902f, 0.0f},
                     {0.03529412f, 0.2117647f, 0.2980392f}, {0.2980392f, 0.1647059f, 0.02352941f},
                     {0.09019608f, 0.2980392f, 0.01568628f}},
    [ZZ_PEGS_TUNING] = {{0.003921569f, 0.55f, 0.3417647f}, {0.7490196f, 0.0f, 0.05882353f}, {0.6862745f, 0.4588235f, 0.0f},
                        {0.0f, 0.3764706f, 0.7294118f}, {0.6980392f, 0.2196078f, 0.0f}, {0.1254902f, 0.5490196f, 0.0f}},
    [ZZ_PEGS_IN_TUNE] = {{0.003921569f, 0.6f, 0.4117647f}, {0.7490196f, 0.0f, 0.05882353f}, {0.6862745f, 0.4588235f, 0.0f},
                         {0.0f, 0.3764706f, 0.7294118f}, {0.6980392f, 0.2196078f, 0.0f}, {0.1254902f, 0.5490196f, 0.0f}},
    [ZZ_PEGS_OUT_TUNE] = {{0.003921569f, 0.6f, 0.4117647f}, {0.7490196f, 0.0f, 0.05882353f}, {0.6862745f, 0.4588235f, 0.0f},
                          {0.0f, 0.3764706f, 0.7294118f}, {0.6980392f, 0.2196078f, 0.0f}, {0.1254902f, 0.5490196f, 0.0f}},
    [ZZ_TEXT] = {{0.2470588f, 1.0f, 0.7843137f}, {1.0f, 0.2509804f, 0.2509804f}, {1.0f, 1.0f, 0.2509804f},
                 {0.2509804f, 0.5019608f, 1.0f}, {1.0f, 0.627451f, 0.2509804f}, {0.2509804f, 1.0f, 0.2509804f}},
    [ZZ_BODY_NORM] = {{0.0f, 0.7764706f, 0.5568628f}, {0.7490196f, 0.372549f, 0.372549f}, {0.7490196f, 0.7176471f, 0.372549f},
                      {0.3686275f, 0.6156863f, 0.7372549f}, {0.7490196f, 0.5529412f, 0.372549f},
                      {0.372549f, 0.7490196f, 0.5921569f}},
    [ZZ_BODY_ACC] = {{1.0f, 1.0f, 1.0f}, {0.9764706f, 0.572549f, 0.5372549f}, {0.9843137f, 0.9490196f, 0.5843138f},
                     {0.5490196f, 0.9098039f, 0.9921569f}, {0.9882353f, 0.8196079f, 0.5254902f},
                     {0.6901961f, 0.9568627f, 0.5137255f}},
};

static int string_color_mode(void) {
  const char *custom = rsm_get("Toggle Switches", "CustomStringColors");
  return custom ? atoi(custom) : 0;
}

// The notes take the string colors, their own colors or the game's, by the settings; in an extended range song, the
// colorblind rows in both halves
static void apply_notes(void) {
  int custom = string_color_mode(), er = rsm_extended_range();
  long mode = rsm_long("SeparateNoteColorsMode", 0);
  color rows[RAMP_ROWS];
  if (mode == 0 && custom == 2) {
    read_rows("string", rows);
    rsm_log("colors: notes as the custom string colors");
  } else if (mode == 2 && rsm_on("SeparateNoteColors")) {
    read_rows("note", rows);
    rsm_log("colors: custom note colors");
  } else if (er) {
    for (int i = 0; i < 8; i++) parse_color(DEFAULT_CB[i], &rows[8 + i]);
    if (custom == 1)
      for (int i = 0; i < 6; i++) {
        rgb c = ZZ[ZZ_ENABLED][i];
        rows[8 + i] = (color){(uint8_t)(c.r * 255.0f + 0.5f), (uint8_t)(c.g * 255.0f + 0.5f), (uint8_t)(c.b * 255.0f + 0.5f)};
      }
  } else {
    render_replace(g_rule[RAMP_A], 0, 0, NULL);
    render_replace(g_rule[RAMP_B], 0, 0, NULL);
    return;
  }
  if (er) {
    memcpy(rows, rows + 8, 8 * sizeof *rows);
    rsm_log("colors: notes in the extended range colors");
  }
  set_ramps(rows);
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

#define RAINBOW_DEGREES_PER_SECOND 120.0f  // a turn in three seconds
#define RAINBOW_STRING_STEP 24.0f          // the six strings across a third of the wheel

static rgb rainbow_rgb(double now, int string) {
  float hue = (float)fmod(now * RAINBOW_DEGREES_PER_SECOND, 360.0) + RAINBOW_STRING_STEP * (float)string;
  return to_rgb((hsl){hue, 1.0f, 0.5f});
}

// Each string a step further round, the two inactive rows as the last string, and the same for colorblind mode
static void apply_rainbow(double now) {
  color rows[RAMP_ROWS];
  for (int i = 0; i < 8; i++) {
    rgb c = rainbow_rgb(now, i < 6 ? i : 5);
    rows[i] = rows[8 + i] = (color){(uint8_t)(c.r * 255.0f), (uint8_t)(c.g * 255.0f), (uint8_t)(c.b * 255.0f)};
  }
  set_ramps(rows);
}

// ------------------------------------------------------------------------------------------------
// The strings
// ------------------------------------------------------------------------------------------------
// The kinds of color, by their order in the game's table
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

static const int CUSTOM_STATES[] = {ST_ENABLED,       ST_DISABLED, ST_GLOW,      ST_PEGS_TUNING, ST_PEGS_IN_TUNE,
                                    ST_PEGS_OUT_TUNE, ST_TEXT,     ST_BODY_NORM, ST_BODY_ACC};
#define NCUSTOM ((int)(sizeof CUSTOM_STATES / sizeof CUSTOM_STATES[0]))
static const int RAINBOW_STATES[] = {ST_ENABLED, ST_GLOW, ST_DISABLED};

static rgb g_custom[BANKS][NCUSTOM][STRINGS];
static color g_custom_from[BANKS][STRINGS];  // the player's colors, what g_custom is made from
static int g_custom_on, g_custom_stale;
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

// Every kind of color for the player's string colors, from what the game has for its own (g_saved)
static void derive_colors(void) {
  for (int b = 0; b < BANKS; b++)
    for (int i = 0; i < STRINGS; i++) {
      color from = g_custom_from[b][i];
      rgb c = {from.r / 255.0f, from.g / 255.0f, from.b / 255.0f};
      for (int k = 0; k < NCUSTOM; k++) {
        int state = CUSTOM_STATES[k];
        g_custom[b][k][i] = state == ST_ENABLED ? c : restyle(c, g_saved[b][ST_ENABLED][i], g_saved[b][state][i]);
      }
    }
  g_custom_stale = 0;
}

static void read_custom_strings(void) {
  const char *custom = rsm_get("Toggle Switches", "CustomStringColors");
  color rows[RAMP_ROWS];
  g_custom_on = custom && atoi(custom) == 2;
  read_rows("string", rows);
  for (int i = 0; i < STRINGS; i++) {
    g_custom_from[0][i] = rows[i];
    g_custom_from[1][i] = rows[8 + i];
  }
  g_custom_stale = 1;
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

// The extended range colors of a kind of color (by CUSTOM_STATES' order) for a string: ZZ's, the player's colorblind ones,
// or the game's colorblind bank
static const rgb *extended_range_color(int mode, int k, int i) {
  static const int ZZ_OF[NCUSTOM] = {ZZ_ENABLED,       ZZ_DISABLED, ZZ_GLOW,      ZZ_PEGS_TUNING, ZZ_PEGS_IN_TUNE,
                                     ZZ_PEGS_OUT_TUNE, ZZ_TEXT,     ZZ_BODY_NORM, ZZ_BODY_ACC};
  if (mode == 1) return &ZZ[ZZ_OF[k]][i];
  if (mode == 2) return &g_custom[1][k][i];
  return &g_saved[1][CUSTOM_STATES[k]][i];
}

static void apply_strings(double now) {
  const char *mgr = color_manager();
  if (!mgr) return;
  int er = rsm_extended_range(), mode = string_color_mode();
  if (!g_custom_on && !g_rainbow_strings && !er) {
    if (g_saved_for) restore_strings(mgr);
    return;
  }
  if (g_saved_for != mgr) {
    save_strings(mgr);
    g_custom_stale = 1;
  }
  if (g_custom_on && g_custom_stale) derive_colors();
  rgb rainbow[STRINGS];
  if (g_rainbow_strings)
    for (int i = 0; i < STRINGS; i++) rainbow[i] = rainbow_rgb(now, i);
  for (int b = 0; b < BANKS; b++)
    for (int i = 0; i < STRINGS; i++) {
      if (er)
        for (int k = 0; k < NCUSTOM; k++) {
          float *p = string_color(mgr, b, CUSTOM_STATES[k], i);
          if (p) memcpy(p, extended_range_color(mode, k, i), sizeof(rgb));
        }
      else if (g_custom_on)
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
  static int er_shown;
  int er = rsm_extended_range();
  if (er != er_shown) {
    er_shown = er;
    g_next_check = 0;
    g_settings[0] = 0;  // the notes again
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
