// Seven-String Guitar (seven.h): the instrument model (seven_model.h): slots, ranks, per-player counts, the
// extended-bass setups per detector, the feature flags, and the settings' parsing. No game, no ini: tests link it
// alone (tests/test_seven_model.c).
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "seven_model.h"

const int8_t seven_guitar_open[8] = {40, 45, 50, 55, 59, 64, 35, 30};
const int8_t seven_bass_open[6] = {28, 33, 38, 43, 23, 48};

int seven_slot_of(int instrument, int n, int i) {
  if (instrument == SEVEN_GUITAR) {
    if (n < 6 || n > SEVEN_GUITAR_MAX || i < 0 || i >= n) return -1;
    int extra = n - 6;  // 0..2 strings below the low E: slots 6 (and 7)
    if (i < extra) return 5 + extra - i;
    return i - extra;
  }
  if (instrument == SEVEN_BASS) {
    if (n < SEVEN_BASS_STOCK || n > SEVEN_BASS_MAX || i < 0 || i >= n) return -1;
    if (n == 4) return i;
    if (i == 0) return 4;
    if (i <= 4) return i - 1;
    return 5;  // bass 6's high C
  }
  return -1;
}

int seven_rank_of(int instrument, int n, int slot) {
  for (int i = 0; i < n && i < 8; i++)
    if (seven_slot_of(instrument, n, i) == slot) return i;  // native order is thickest (lowest) first
  return -1;
}

// ------------------------------------------------------------------------------------------------
// Per-player counts
// ------------------------------------------------------------------------------------------------
static atomic_int g_guitar[SEVEN_PLAYERS] = {6, 6}, g_bass[SEVEN_PLAYERS] = {4, 4};

int seven_desired_guitar(int p) { return p >= 0 && p < SEVEN_PLAYERS ? atomic_load(&g_guitar[p]) : 6; }
int seven_desired_bass(int p) { return p >= 0 && p < SEVEN_PLAYERS ? atomic_load(&g_bass[p]) : SEVEN_BASS_STOCK; }
void seven_set_desired(int p, int guitar, int bass) {
  if (p < 0 || p >= SEVEN_PLAYERS) return;
  atomic_store(&g_guitar[p], guitar < 6 ? 6 : guitar > SEVEN_GUITAR_MAX ? SEVEN_GUITAR_MAX : guitar);
  atomic_store(&g_bass[p], bass < SEVEN_BASS_STOCK ? SEVEN_BASS_STOCK : bass > SEVEN_BASS_MAX ? SEVEN_BASS_MAX : bass);
}

// ------------------------------------------------------------------------------------------------
// Extended-bass setups (rsxrbass::SetupPlayer and the readers)
// ------------------------------------------------------------------------------------------------
#define ROUND_MS 5000u
typedef struct {
  const void *det;
  int strings, instrument;
  uint32_t ms;
} setup;
static setup g_setup[SEVEN_PLAYERS];
static atomic_int g_max, g_any_guitar;  // published after each change, for the lock-free reads
static atomic_uint g_bass_serial;

static void publish(void) {
  int n = 0, guitar = 0;
  for (int i = 0; i < SEVEN_PLAYERS; i++) {
    if (g_setup[i].det && g_setup[i].strings > n) n = g_setup[i].strings;
    if (g_setup[i].det && g_setup[i].instrument != 2) guitar = 1;
  }
  if (n != atomic_load(&g_max) || guitar != atomic_load(&g_any_guitar)) atomic_fetch_add(&g_bass_serial, 1);
  atomic_store(&g_max, n);
  atomic_store(&g_any_guitar, guitar);
}

void seven_bass_setup(const void *det, int instrument, int strings, uint32_t ms) {
  setup *slot = NULL;
  for (int i = 0; i < SEVEN_PLAYERS; i++)
    if (g_setup[i].det && ms - g_setup[i].ms > ROUND_MS) g_setup[i] = (setup){0};  // an earlier song
  for (int i = 0; i < SEVEN_PLAYERS; i++)
    if (g_setup[i].det == det) slot = &g_setup[i];
  for (int i = 0; i < SEVEN_PLAYERS; i++)
    if (!slot && !g_setup[i].det) slot = &g_setup[i];
  if (!slot) {
    slot = &g_setup[0];
    for (int i = 0; i < SEVEN_PLAYERS; i++)
      if (g_setup[i].ms < slot->ms) slot = &g_setup[i];
  }
  *slot = (setup){det, strings, instrument, ms};
  publish();
}

void seven_bass_clear(void) {
  memset(g_setup, 0, sizeof g_setup);
  publish();
}

int seven_bass_strings_of(const void *det) {
  if (!det) return 0;
  for (int i = 0; i < SEVEN_PLAYERS; i++)
    if (g_setup[i].det == det) return g_setup[i].strings;
  return 0;
}
int seven_bass_max_strings(void) { return atomic_load(&g_max); }
int seven_bass_any_guitar(void) { return atomic_load(&g_any_guitar); }
uint32_t seven_bass_serial(void) { return atomic_load(&g_bass_serial); }

// ------------------------------------------------------------------------------------------------
// Features
// ------------------------------------------------------------------------------------------------
static atomic_int g_feature[SEVEN_F_COUNT];
void seven_feature_set(enum seven_feature f, int on) {
  if ((unsigned)f < SEVEN_F_COUNT) atomic_store(&g_feature[f], on != 0);
}
int seven_feature(enum seven_feature f) { return (unsigned)f < SEVEN_F_COUNT ? atomic_load(&g_feature[f]) : 0; }

// ------------------------------------------------------------------------------------------------
// Settings' parsing
// ------------------------------------------------------------------------------------------------
int seven_parse_rgb(const char *s, uint32_t *rgb) {
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

int seven_parse_spacing(const char *s, float *v) {
  if (!s) return 0;
  while (*s == ' ' || *s == '\t') s++;
  if (!*s) {
    *v = 0;
    return 1;
  }
  char *end;
  float f = strtof(s, &end);
  while (*end == ' ' || *end == '\t') end++;
  if (end == s || *end || f != f) return 0;
  if (f != 0 && (f < 0.5f || f > 4.0f)) return 0;
  *v = f;
  return 1;
}

static int parse_bool(const char *s, int dflt) {
  if (!s) return dflt;
  while (*s == ' ' || *s == '\t') s++;
  if (!*s) return dflt;
  return strcmp(s, "0") && strcasecmp(s, "off") && strcasecmp(s, "false") && strcasecmp(s, "no");
}

void seven_settings_parse(seven_settings *s, seven_get_fn get, seven_bad_fn bad, void *user) {
  memset(s, 0, sizeof *s);
  s->string7_line = s->diagrams7 = s->lowrange = 1;
  s->string7_rgb[0] = s->string7_rgb[1] = 0xFF66CC;
  static const struct {
    const char *key;
    size_t off;
  } spacing[] = {{"spacing_guitar7", offsetof(seven_settings, spacing_guitar7)},
                 {"spacing_guitar8", offsetof(seven_settings, spacing_guitar8)},
                 {"spacing_bass5", offsetof(seven_settings, spacing_bass5)},
                 {"spacing_bass6", offsetof(seven_settings, spacing_bass6)}};
  for (size_t i = 0; i < sizeof spacing / sizeof *spacing; i++) {
    const char *v = get(spacing[i].key, user);
    float f;
    if (!v) continue;
    if (!seven_parse_spacing(v, &f)) {
      if (bad) bad(spacing[i].key, v, user);
      f = 0;
    }
    *(float *)((char *)s + spacing[i].off) = f;
  }
  s->string7_line = parse_bool(get("string7_line", user), 1);
  s->diagrams7 = parse_bool(get("diagrams7", user), 1);
  s->lowrange = parse_bool(get("lowrange", user), 1);
  const char *n = get("string7_N", user), *cb = get("string7_CB", user);
  if (n && *n) {
    if (seven_parse_rgb(n, &s->string7_rgb[0])) {
      s->string7_set[0] = 1;
      s->string7_rgb[1] = s->string7_rgb[0];
    } else if (bad)
      bad("string7_N", n, user);
  }
  if (cb && *cb) {
    if (seven_parse_rgb(cb, &s->string7_rgb[1]))
      s->string7_set[1] = 1;
    else if (bad)
      bad("string7_CB", cb, user);
  }
}
