// Seven-String Guitar (seven.h): the settings of [Seven String] in RSMods.ini (seven_model.h seven_settings), rs7string's
// [neck] spacing_*, string7_line, [chords] diagrams7, [tuner] lowrange and [color] string7_color / _cb, under our names:
// spacing_guitar7, spacing_guitar8, spacing_bass5, spacing_bass6 (0 = stock, else 0.5 to 4.0), string7_line,
// diagrams7, lowrange (on by default), string7_N and string7_CB (RRGGBB, FF66CC). The ini is re-read by the game's own
// poll (rsm_ini_poll, about once a second); the settings follow it: they are read again at most every half second from
// whichever thread asks, and the serial changes when a value did. seven_colours.c reads string6_N the same way.
#include <stdatomic.h>
#include <string.h>
#include <time.h>

#include "seven.h"

#define SECTION "Seven String"

static const char *get_key(const char *key, void *user) {
  (void)user;
  return rsm_get(SECTION, key);
}
static void bad_key(const char *key, const char *value, void *user) {
  (void)user;
  if (!strncmp(key, "spacing_", 8))
    rsm_log("seven-string: [%s] %s = %s isn't 0 or 0.5 to 4.0: stock spacing", SECTION, key, value);
  else
    rsm_log("seven-string: [%s] %s = %s isn't RRGGBB", SECTION, key, value);
}

static seven_settings g_buf[2];  // read into the one not in use, then published
static atomic_int g_cur;
static atomic_uint g_serial;
static atomic_int g_busy;
static atomic_llong g_next_ms;  // the next re-read, CLOCK_MONOTONIC ms; 0 before the first

static long long now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

const seven_settings *seven_settings_get(void) {
  long long now = now_ms(), next = atomic_load(&g_next_ms);
  if ((!next || now >= next) && !atomic_exchange(&g_busy, 1)) {
    atomic_store(&g_next_ms, now + 500);
    rsm_ini_poll();
    int cur = atomic_load(&g_cur), other = !cur;
    seven_settings_parse(&g_buf[other], get_key, bad_key, NULL);
    if (!next || memcmp(&g_buf[other], &g_buf[cur], sizeof g_buf[0])) {
      atomic_store(&g_cur, other);
      atomic_fetch_add(&g_serial, 1);
      if (next) rsm_log("seven-string: settings changed");
    }
    atomic_store(&g_busy, 0);
  }
  return &g_buf[atomic_load(&g_cur)];
}

unsigned seven_settings_serial(void) {
  seven_settings_get();
  return atomic_load(&g_serial);
}

float seven_spacing_for(int instrument, int n) {
  const seven_settings *s = seven_settings_get();
  if (instrument == SEVEN_GUITAR) return n == 7 ? s->spacing_guitar7 : n == 8 ? s->spacing_guitar8 : 0;
  if (instrument == SEVEN_BASS) return n == 5 ? s->spacing_bass5 : n == 6 ? s->spacing_bass6 : 0;
  return 0;
}
