// Sunlight (macOS only): the game for a screen outdoors. Reflected daylight adds the same light to every pixel, so what
// is left to see is a difference in luminance, and a dark or saturated color has little: on the game's black, blue and
// purple notes are gone long before yellow ones. Sunlight = on, and the key goes through SunlightMode: 0 off, 1 bright,
// 2 daylight. Bright lifts the picture and brings every color up to one luminance. Daylight, in a song, turns it over:
// black becomes white and the notes dark ink, all of one luminance, as dark text on white is what stays readable in
// the sun. In the menus daylight is bright: their pictures don't survive being turned over. In a song the venue goes
// too (mod_loft.c), as it would be lifted into noise behind the highway, and daylight needs a plain background.
//
// The picture is changed after the game has drawn it, by a Core Image filter on the window's CAMetalLayer, so it needs
// nothing of the game or the renderer and covers everything drawn, the overlay too. Screenshots from rsm_screenshot are
// of the frame before it. CIColorCube would take its table in linear light, where near black has hardly a grid point;
// CIColorCubeWithColorSpace with sRGB takes it in the values the game writes (measured: a grey ramp through a square
// root comes back within 1 of 255). AppKit is the main thread's, and the mods' tick is not on it.
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <dispatch/dispatch.h>
#include <math.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <stdio.h>
#include <stdlib.h>

#include "rsmods.h"

enum { GRADE_OFF, GRADE_BRIGHT, GRADE_DAYLIGHT, NGRADES };

#define CUBE 32
#define SATURATION 1.5f    // how far colors are pushed from white before anything else
#define BRIGHT_FLOOR 0.30f // bright: the least luminance of a full color (0.40 turns red and orange the same pink)
#define BRIGHT_GAMMA 0.55f
#define DAYLIGHT_INK 0.10f // daylight: the most luminance of the ink
#define DAYLIGHT_GAMMA 0.60f
#define BLACK_BELOW 0.03f  // what is darker stays the background
#define SHOW_SECONDS 2.0
#define KEEP_FOR 3.0       // seconds after the key when the ini's older value is not read back
#define CHECK_SECONDS 2.0  // a window or layer made later

static const char *const MODE_NAMES[NGRADES] = {"off", "bright", "daylight"};

static int g_mode, g_on, g_shown_grade;  // off from the start: with the mod off the layer is never touched
static int g_in_song;
static double g_changed = -100, g_next_check;

// ------------------------------------------------------------------------------------------------
// The colors. All in the values the game writes (sRGB); luminance from linear light.
// ------------------------------------------------------------------------------------------------
static float to_linear(float v) { return powf(v, 2.2f); }
static float from_linear(float v) { return powf(v, 1.f / 2.2f); }
static float luminance(const float lin[3]) { return 0.2126f * lin[0] + 0.7152f * lin[1] + 0.0722f * lin[2]; }

static void sunlight_grade(int grade, const float in[3], float out[3]) {
  float v = fmaxf(in[0], fmaxf(in[1], in[2]));
  float t = v > BLACK_BELOW ? (v - BLACK_BELOW) / (1.f - BLACK_BELOW) : 0.f;
  float lin[3];
  if (t > 0.f)  // the color at full value, more saturated
    for (int i = 0; i < 3; i++) lin[i] = to_linear(fmaxf(0.f, 1.f - (1.f - in[i] / v) * SATURATION));
  else
    lin[0] = lin[1] = lin[2] = 0.f;
  float y = luminance(lin);
  if (grade == GRADE_DAYLIGHT) {
    t = powf(t, DAYLIGHT_GAMMA);
    float k = y > DAYLIGHT_INK ? DAYLIGHT_INK / y : 1.f;
    for (int i = 0; i < 3; i++) out[i] = 1.f + (from_linear(lin[i] * k) - 1.f) * t;
  } else {
    t = powf(t, BRIGHT_GAMMA);
    float w = y < BRIGHT_FLOOR ? (BRIGHT_FLOOR - y) / (1.f - y) : 0.f;  // white to add for that luminance
    for (int i = 0; i < 3; i++) out[i] = from_linear(lin[i] + (1.f - lin[i]) * w) * t;
  }
}

// ------------------------------------------------------------------------------------------------
// The filter on the window's layer (main thread)
// ------------------------------------------------------------------------------------------------
static void *send0(void *o, const char *s) { return ((void *(*)(void *, SEL))objc_msgSend)(o, sel_registerName(s)); }
static void *send1(void *o, const char *s, const void *a) {
  return ((void *(*)(void *, SEL, const void *))objc_msgSend)(o, sel_registerName(s), a);
}
static void send2(void *o, const char *s, const void *a, const void *b) {
  ((void (*)(void *, SEL, const void *, const void *))objc_msgSend)(o, sel_registerName(s), a, b);
}
static void send_bool(void *o, const char *s, int v) {
  ((void (*)(void *, SEL, signed char))objc_msgSend)(o, sel_registerName(s), (signed char)v);
}
static unsigned long count_of(void *array) {
  return ((unsigned long (*)(void *, SEL))objc_msgSend)(array, sel_registerName("count"));
}
static void *item_at(void *array, unsigned long i) {
  return ((void *(*)(void *, SEL, unsigned long))objc_msgSend)(array, sel_registerName("objectAtIndex:"), i);
}

// The view hosting a CAMetalLayer, under `view`
static void *metal_view(void *view, Class metal, int depth) {
  void *layer = send0(view, "layer");
  if (layer && ((signed char (*)(void *, SEL, Class))objc_msgSend)(layer, sel_registerName("isKindOfClass:"), metal))
    return view;
  if (depth >= 4) return NULL;
  void *subviews = send0(view, "subviews");
  for (unsigned long i = 0, n = subviews ? count_of(subviews) : 0; i < n; i++) {
    void *found = metal_view(item_at(subviews, i), metal, depth + 1);
    if (found) return found;
  }
  return NULL;
}

static void *game_view(void) {
  Class app = objc_getClass("NSApplication"), metal = objc_getClass("CAMetalLayer");
  if (!app || !metal) return NULL;
  void *windows = send0(send0((void *)app, "sharedApplication"), "windows");
  for (unsigned long i = 0, n = windows ? count_of(windows) : 0; i < n; i++) {
    void *content = send0(item_at(windows, i), "contentView");
    void *view = content ? metal_view(content, metal, 0) : NULL;
    if (view) return view;
  }
  return NULL;
}

// The filter of a grade, made once and kept
static void *grade_filter(int grade) {
  static void *filters[NGRADES];
  if (filters[grade]) return filters[grade];
  Class cls = objc_getClass("CIFilter");
  void *f = cls ? send1((void *)cls, "filterWithName:", CFSTR("CIColorCubeWithColorSpace")) : NULL;
  CGColorSpaceRef srgb = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  const size_t bytes = (size_t)CUBE * CUBE * CUBE * 4 * sizeof(float);
  float *cube = malloc(bytes);
  if (f && srgb && cube) {
    float *p = cube;
    for (int b = 0; b < CUBE; b++)
      for (int g = 0; g < CUBE; g++)
        for (int r = 0; r < CUBE; r++, p += 4) {
          const float in[3] = {(float)r / (CUBE - 1), (float)g / (CUBE - 1), (float)b / (CUBE - 1)};
          sunlight_grade(grade, in, p);
          p[3] = 1.f;
        }
    int size = CUBE;
    CFNumberRef dimension = CFNumberCreate(NULL, kCFNumberIntType, &size);
    CFDataRef data = CFDataCreate(NULL, (const UInt8 *)cube, (CFIndex)bytes);
    send2(f, "setValue:forKey:", dimension, CFSTR("inputCubeDimension"));
    send2(f, "setValue:forKey:", data, CFSTR("inputCubeData"));
    send2(f, "setValue:forKey:", srgb, CFSTR("inputColorSpace"));
    CFRelease(dimension);
    CFRelease(data);
    filters[grade] = (void *)CFRetain(f);
  }
  free(cube);
  if (srgb) CGColorSpaceRelease(srgb);
  return filters[grade];
}

static void apply_on_main(void *ctx) {
  static void *applied_layer;
  static int applied_grade, warned;
  int grade = (int)(intptr_t)ctx;
  void *view = game_view();
  if (!view) {
    if (grade && !warned) rsm_log("sunlight: no Metal layer in the game's windows: the picture stays as it is");
    warned = grade != GRADE_OFF;
    return;
  }
  void *layer = send0(view, "layer");
  if (layer == applied_layer && grade == applied_grade) return;
  void *filter = grade ? grade_filter(grade) : NULL;
  if (grade && !filter) {
    rsm_log("sunlight: no color cube filter");
    return;
  }
  Class transaction = objc_getClass("CATransaction"), array = objc_getClass("NSArray");
  send_bool(view, "setLayerUsesCoreImageFilters:", 1);
  send0((void *)transaction, "begin");
  send_bool((void *)transaction, "setDisableActions:", 1);
  send1(layer, "setFilters:", filter ? send1((void *)array, "arrayWithObject:", filter) : NULL);
  send0((void *)transaction, "commit");
  applied_layer = layer;
  applied_grade = grade;
  rsm_log("sunlight: %s", MODE_NAMES[grade]);
}

// ------------------------------------------------------------------------------------------------
// The mod
// ------------------------------------------------------------------------------------------------
int rsm_sunlight_hides_venue(void) { return g_on && g_mode != GRADE_OFF && g_in_song; }

static int clamp_mode(long v) { return v < GRADE_OFF || v >= NGRADES ? GRADE_BRIGHT : (int)v; }

static void tick(void) {
  double now = rsm_now();
  g_on = rsm_on("Sunlight");
  g_in_song = rsm_menu_in(RSM_SONG_MODES);
  if (now - g_changed >= KEEP_FOR) g_mode = clamp_mode(rsm_long("SunlightMode", GRADE_BRIGHT));
  int grade = g_on ? g_mode : GRADE_OFF;
  if (grade == GRADE_DAYLIGHT && !g_in_song) grade = GRADE_BRIGHT;
  if (grade != g_shown_grade || (grade && now >= g_next_check)) {
    g_shown_grade = grade;
    g_next_check = now + CHECK_SECONDS;
    dispatch_async_f(dispatch_get_main_queue(), (void *)(intptr_t)grade, apply_on_main);
  }
  if (g_on && now - g_changed < SHOW_SECONDS) {
    char txt[48];
    snprintf(txt, sizeof txt, "Sunlight: %s", MODE_NAMES[g_mode]);
    rsm_text(txt, 0.5f, 1.f / 54, 0.04f, RSM_ALIGN_CENTER, 0xffffffff);
  }
}

static void key(const rsm_key_event *e) {
  if (e->down || !rsm_on("Sunlight") || e->keycode != rsm_key("SunlightKey")) return;
  char v[8];
  g_mode = (g_mode + 1) % NGRADES;
  g_changed = rsm_now();
  snprintf(v, sizeof v, "%d", g_mode);
  rsm_set("Mod Settings", "SunlightMode", v);
}

const rsm_mod rsm_mod_sunlight = {"sunlight", tick, key, NULL, NULL};
