// A manifest's tones for Tone Host (song_plan.h). The JSON is walked as it is written: a tone is an object of "Tones",
// its knobs the numbers of every "KnobValues" below it.
#include "song_plan.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

enum { MAX_TONES = 16 };

typedef struct {
  char key[128];
  char gear[SONG_PLAN_GEAR];
  size_t len;
  float volume;
  int has_volume;
} tone;

static const char *ws(const char *p) {
  while (*p && isspace((unsigned char)*p)) p++;
  return p;
}

// A string at p (its opening quote): copied without its escapes' backslashes, which no key or tone name needs more of;
// the place after it, or NULL
static const char *string(const char *p, char *out, size_t n) {
  if (*p != '"') return NULL;
  size_t o = 0;
  for (p++; *p && *p != '"'; p++) {
    if (*p == '\\' && p[1]) p++;
    if (out && o + 1 < n) out[o++] = *p;
  }
  if (out && n) out[o] = 0;
  return *p == '"' ? p + 1 : NULL;
}

static const char *value(const char *p, tone *t, int depth, const char *parent);

static void add_knob(tone *t, const char *name, float v) {
  if (!song_plan_is_gear(name)) return;
  int w = snprintf(t->gear + t->len, sizeof t->gear - t->len, " %s=%g", name, v);
  if (w > 0 && (size_t)w < sizeof t->gear - t->len) t->len += (size_t)w;
  else t->gear[t->len] = 0;  // one that doesn't fit whole is left out
}

// An object at p (its brace), `depth` below the tone (0 the tone itself), the value of the member `parent`
static const char *object(const char *p, tone *t, int depth, const char *parent) {
  p = ws(p + 1);
  while (*p && *p != '}') {
    char key[160];
    if (!(p = string(p, key, sizeof key))) return NULL;
    p = ws(p);
    if (*p != ':') return NULL;
    p = ws(p + 1);
    if (t && *p != '{' && *p != '[') {
      char text[160] = "";
      const char *from = p;
      if (*p == '"') string(p, text, sizeof text);
      if (!depth && !strcmp(key, "Key")) snprintf(t->key, sizeof t->key, "%s", text);
      if (!depth && !strcmp(key, "Volume")) {
        t->volume = strtof(*from == '"' ? text : from, NULL);
        t->has_volume = 1;
      }
      if (!strcmp(parent, "KnobValues") && *from != '"') add_knob(t, key, strtof(from, NULL));
    }
    if (!(p = value(p, t, depth + 1, key))) return NULL;
    p = ws(p);
    if (*p == ',') p = ws(p + 1);
  }
  return *p ? p + 1 : NULL;
}

// Any value at p: the place after it, or NULL for what isn't JSON
static const char *value(const char *p, tone *t, int depth, const char *parent) {
  p = ws(p);
  if (*p == '"') return string(p, NULL, 0);
  if (*p == '{') return object(p, t, depth, parent);
  if (*p == '[') {
    p = ws(p + 1);
    while (*p && *p != ']') {
      if (!(p = value(p, t, depth + 1, ""))) return NULL;
      p = ws(p);
      if (*p == ',') p = ws(p + 1);
    }
    return *p ? p + 1 : NULL;
  }
  const char *from = p;
  while (*p && !strchr(",}] \t\r\n", *p)) p++;
  return p > from ? p : NULL;
}

// The value of the first member `key` in the text: a string's text or a number's, "" when there is none
static void member(const char *json, const char *key, char *out, size_t n) {
  char quoted[64];
  snprintf(quoted, sizeof quoted, "\"%s\"", key);
  *out = 0;
  for (const char *p = json; (p = strstr(p, quoted)); p += strlen(quoted)) {
    const char *v = ws(p + strlen(quoted));
    if (*v != ':') continue;
    v = ws(v + 1);
    if (*v == '"') string(v, out, n);
    else snprintf(out, n, "%.*s", (int)strcspn(v, ",}] \t\r\n"), v);
    return;
  }
}

int song_plan_parse(const char *json, song_plan *out) {
  static _Thread_local tone tones[MAX_TONES];
  int n = 0;
  memset(out, 0, sizeof *out);
  out->max_difficulty = -1;
  char text[160];
  member(json, "MaxPhraseDifficulty", text, sizeof text);
  if (*text) out->max_difficulty = atoi(text);
  for (const char *p = json; (p = strstr(p, "\"Tones\"")); p += 7) {
    const char *v = ws(p + 7);
    if (*v != ':' || *(v = ws(v + 1)) != '[') continue;
    v = ws(v + 1);
    while (*v == '{' && n < MAX_TONES) {
      tone *t = &tones[n];
      memset(t, 0, sizeof *t);
      if (!(v = object(v, t, 0, ""))) break;
      if (t->has_volume) add_knob(t, "Meter_Tone_Balance_Return", t->volume);
      if (*t->key) n++;
      v = ws(v);
      if (*v == ',') v = ws(v + 1);
    }
    break;
  }
  static const char *const slots[SONG_PLAN_SLOTS] = {"Tone_Base", "Tone_A", "Tone_B", "Tone_C", "Tone_D"};
  for (int s = 0; s < SONG_PLAN_SLOTS; s++) {
    member(json, slots[s], text, sizeof text);
    for (int i = 0; *text && i < n; i++)
      if (!strcasecmp(tones[i].key, text)) {
        memcpy(out->gear[s], tones[i].gear, sizeof out->gear[s]);
        break;
      }
  }
  return out->gear[0][0] != 0;
}
