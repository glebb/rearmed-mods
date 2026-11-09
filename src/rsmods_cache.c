// The tuning and tone files' edits, and a profile's tones (rsmods_cache.h). A small JSON reader that keeps the text:
// the files are changed where they need to be, and the rest of them stays as the game has it.
#include "rsmods_cache.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

// ------------------------------------------------------------------------------------------------
// JSON, read in place
// ------------------------------------------------------------------------------------------------
static const char *ws(const char *p) {
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
  return p;
}

// p at a '"': after the string's closing quote, or NULL
static const char *skip_string(const char *p) {
  for (p++; *p; p++) {
    if (*p == '\\') {
      if (!*++p) return NULL;
    } else if (*p == '"') {
      return p + 1;
    }
  }
  return NULL;
}

// The end of the value at p (after white space), or NULL
static const char *skip_value(const char *p) {
  p = ws(p);
  if (*p == '"') return skip_string(p);
  if (*p == '{' || *p == '[') {
    int depth = 0;
    for (; *p; p++) {
      if (*p == '"') {
        if (!(p = skip_string(p))) return NULL;
        p--;
      } else if (*p == '{' || *p == '[') {
        depth++;
      } else if ((*p == '}' || *p == ']') && --depth == 0) {
        return p + 1;
      }
    }
    return NULL;
  }
  const char *s = p;
  while (*p && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;
  return p > s ? p : NULL;
}

typedef struct {
  const char *start;  // the member's key's opening quote
  const char *key;    // inside the quotes
  size_t key_len;
  const char *val, *end;  // the value
} member;

// The next member of an object; *cur starts at its '{'. 1 if there is one, 0 at its end, -1 on bad JSON.
static int next_member(const char **cur, member *m) {
  const char *p = ws(*cur);
  if (*p == '{' || *p == ',') p = ws(p + 1);
  if (*p == '}') return 0;
  if (*p != '"') return -1;
  const char *k = skip_string(p);
  if (!k) return -1;
  m->start = p;
  m->key = p + 1;
  m->key_len = (size_t)(k - 1 - (p + 1));
  p = ws(k);
  if (*p != ':') return -1;
  m->val = ws(p + 1);
  if (!(m->end = skip_value(m->val))) return -1;
  *cur = m->end;
  return 1;
}

// The next element of an array; *cur starts at its '['. 1, 0 at its end, -1 on bad JSON.
static int next_element(const char **cur, const char **val, const char **end) {
  const char *p = ws(*cur);
  if (*p == '[' || *p == ',') p = ws(p + 1);
  if (*p == ']') return 0;
  *val = p;
  if (!(*end = skip_value(p))) return -1;
  *cur = *end;
  return 1;
}

static int key_is(const member *m, const char *key) {
  return m->key_len == strlen(key) && !memcmp(m->key, key, m->key_len);
}

// The value of key in the object at obj, or NULL
static const char *get(const char *obj, const char *key, const char **end) {
  obj = ws(obj);
  if (*obj != '{') return NULL;
  member m;
  for (const char *cur = obj; next_member(&cur, &m) == 1;)
    if (key_is(&m, key)) {
      if (end) *end = m.end;
      return m.val;
    }
  return NULL;
}

// A string value, unescaped (\uXXXX as UTF-8, a surrogate pair as one character); 1 if it is one
static int string_of(const char *v, char *out, size_t n) {
  if (!v || *v != '"' || !n) return 0;
  size_t k = 0;
  for (const char *p = v + 1; *p && *p != '"'; p++) {
    unsigned c = (unsigned char)*p;
    if (c == '\\') {
      c = (unsigned char)*++p;
      if (c == 'n') c = '\n';
      else if (c == 't') c = '\t';
      else if (c == 'r') c = '\r';
      else if (c == 'b') c = '\b';
      else if (c == 'f') c = '\f';
      else if (c == 'u') {
        unsigned u = 0;
        for (int i = 0; i < 4; i++) {
          char h = *++p;
          if (!h) return 0;
          u = u * 16 + (unsigned)(h >= '0' && h <= '9' ? h - '0' : (h | 0x20) - 'a' + 10);
        }
        if (u >= 0xD800 && u < 0xDC00 && p[1] == '\\' && p[2] == 'u') {
          unsigned lo = 0;
          for (int i = 3; i < 7; i++) {
            char h = p[i];
            if (!h) return 0;
            lo = lo * 16 + (unsigned)(h >= '0' && h <= '9' ? h - '0' : (h | 0x20) - 'a' + 10);
          }
          if (lo >= 0xDC00 && lo < 0xE000) {
            u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
            p += 6;
          }
        }
        char b[4];
        int bn = u < 0x80 ? (b[0] = (char)u, 1)
                 : u < 0x800 ? (b[0] = (char)(0xC0 | u >> 6), b[1] = (char)(0x80 | (u & 0x3F)), 2)
                 : u < 0x10000 ? (b[0] = (char)(0xE0 | u >> 12), b[1] = (char)(0x80 | ((u >> 6) & 0x3F)),
                                  b[2] = (char)(0x80 | (u & 0x3F)), 3)
                               : (b[0] = (char)(0xF0 | u >> 18), b[1] = (char)(0x80 | ((u >> 12) & 0x3F)),
                                  b[2] = (char)(0x80 | ((u >> 6) & 0x3F)), b[3] = (char)(0x80 | (u & 0x3F)), 4);
        for (int i = 0; i < bn; i++)
          if (k + 1 < n) out[k++] = b[i];
        continue;
      }
      if (!c) return 0;
    }
    if (k + 1 < n) out[k++] = (char)c;
  }
  out[k] = 0;
  return 1;
}

// ------------------------------------------------------------------------------------------------
// Output
// ------------------------------------------------------------------------------------------------
typedef struct {
  char *s;
  size_t n, cap;
  int oom;
} text;

static void put(text *t, const char *s, size_t n) {
  if (t->oom) return;
  if (t->n + n + 1 > t->cap) {
    size_t cap = t->cap ? t->cap : 4096;
    while (t->n + n + 1 > cap) cap *= 2;
    char *ns = realloc(t->s, cap);
    if (!ns) {
      t->oom = 1;
      return;
    }
    t->s = ns;
    t->cap = cap;
  }
  memcpy(t->s + t->n, s, n);
  t->n += n;
  t->s[t->n] = 0;
}

static void puts_(text *t, const char *s) { put(t, s, strlen(s)); }

static void put_escaped(text *t, const char *s) {
  for (; *s; s++) {
    unsigned char c = (unsigned char)*s;
    if (c == '"' || c == '\\') {
      char e[2] = {'\\', (char)c};
      put(t, e, 2);
    } else if (c < 0x20) {
      char e[8];
      snprintf(e, sizeof e, "\\u%04x", c);
      puts_(t, e);
    } else {
      put(t, (const char *)&c, 1);
    }
  }
}

static char *done(text *t) {
  if (t->oom) {
    free(t->s);
    return NULL;
  }
  return t->s;
}

// ------------------------------------------------------------------------------------------------
// Custom Tunings
// ------------------------------------------------------------------------------------------------
int rsm_tuning_parse(const char *name, const char *value, rsm_tuning *out) {
  while (*name == ' ' || *name == '\t') name++;
  size_t len = strlen(name);
  while (len && (name[len - 1] == ' ' || name[len - 1] == '\t')) len--;
  if (!len || len >= sizeof out->name) return 0;
  memcpy(out->name, name, len);
  out->name[len] = 0;
  out->id = 0;
  const char *p = value;
  for (int i = 0; i < 6; i++) {
    while (*p == ' ' || *p == '\t' || *p == ',') p++;
    char *e;
    long v = strtol(p, &e, 10);
    if (e == p || v < -24 || v > 24) return 0;
    out->strings[i] = (int)v;
    p = e;
  }
  while (*p == ' ' || *p == '\t' || *p == ',') p++;
  return *p == 0;
}

// The six strings of a definition (its "Strings" object); 1 if it has them
static int strings_of(const char *def, int s[6]) {
  const char *st = get(def, "Strings", NULL);
  if (!st) return 0;
  for (int i = 0; i < 6; i++) {
    char key[8];
    snprintf(key, sizeof key, "string%d", i);
    const char *v = get(st, key, NULL);
    if (!v) return 0;
    char *e;
    s[i] = (int)strtol(v, &e, 10);
    if (e == v) return 0;
  }
  return 1;
}

static int same_strings(const int a[6], const int b[6]) { return !memcmp(a, b, 6 * sizeof *a); }

char *rsm_tunings_edit(const char *json, const rsm_tuning *t, int n) {
  const char *st = get(ws(json), "Static", NULL), *end = NULL;
  const char *defs = st ? get(st, "TuningDefinitions", &end) : NULL;
  if (!defs || *defs != '{') return NULL;
  text out = {0};
  put(&out, json, (size_t)(defs - json));
  puts_(&out, "{");
  int first = 1;
  member m;
  int r;
  for (const char *cur = defs; (r = next_member(&cur, &m)) == 1;) {
    int s[6], replaced = 0;
    int has = strings_of(m.val, s);
    for (int i = 0; i < n && !replaced; i++)
      replaced = (has && same_strings(s, t[i].strings)) ||
                 (m.key_len == strlen(t[i].name) && !strncasecmp(m.key, t[i].name, m.key_len));
    if (replaced) continue;
    puts_(&out, first ? "\n\t\t" : ",\n\t\t");
    put(&out, m.start, (size_t)(m.end - m.start));
    first = 0;
  }
  if (r < 0) {
    free(out.s);
    return NULL;
  }
  for (int i = 0; i < n; i++) {
    int dup = 0;
    for (int j = 0; j < i && !dup; j++) dup = same_strings(t[j].strings, t[i].strings) || !strcasecmp(t[j].name, t[i].name);
    if (dup) continue;
    char strings[160];
    snprintf(strings, sizeof strings,
             "\", \"Strings\" : { \"string0\" : %d, \"string1\" : %d, \"string2\" : %d, \"string3\" : %d, "
             "\"string4\" : %d, \"string5\" : %d } }",
             t[i].strings[0], t[i].strings[1], t[i].strings[2], t[i].strings[3], t[i].strings[4], t[i].strings[5]);
    char id[24];
    snprintf(id, sizeof id, "$[%d]", t[i].id > 0 ? t[i].id : -1);
    puts_(&out, first ? "\n\t\t\"" : ",\n\t\t\"");
    put_escaped(&out, t[i].name);
    puts_(&out, "\" : { \"UIName\" : \"");
    puts_(&out, id);
    put_escaped(&out, t[i].name);
    puts_(&out, strings);
    first = 0;
  }
  puts_(&out, "\n\t}");
  puts_(&out, end);
  return done(&out);
}

#define LOCALIZATION_FIRST 37500
#define LOCALIZATION_LANGUAGES 7
#define IDS_MAX (1 << 20)

char *rsm_localization_add(const char *csv, size_t len, rsm_tuning *t, int n, size_t *out_len) {
  uint8_t *used = calloc(IDS_MAX / 8, 1);
  if (!used) return NULL;
  for (size_t i = 0; i < len;) {  // the ids the file has: each line's leading number
    long id = 0;
    int digits = 0;
    while (i < len && csv[i] >= '0' && csv[i] <= '9' && id < IDS_MAX) id = id * 10 + (csv[i++] - '0'), digits++;
    if (digits && id < IDS_MAX) used[id / 8] |= (uint8_t)(1 << (id % 8));
    while (i < len && csv[i] != '\n') i++;
    i++;
  }
  text out = {0};
  put(&out, csv, len);
  if (len && csv[len - 1] != '\n') puts_(&out, "\r\n");
  int next = LOCALIZATION_FIRST;
  for (int i = 0; i < n; i++) {
    while (next < IDS_MAX && used[next / 8] & (1 << (next % 8))) next++;
    if (next >= IDS_MAX) break;
    t[i].id = next++;
    // a comma is {C} in the file, as in the game's own lines
    text name = {0};
    for (const char *p = t[i].name; *p; p++) {
      if (*p == ',')
        puts_(&name, "{C}");
      else if (*p != '\r' && *p != '\n')
        put(&name, p, 1);
    }
    char id[16];
    snprintf(id, sizeof id, "%d", t[i].id);
    puts_(&out, id);
    for (int l = 0; l < LOCALIZATION_LANGUAGES; l++) {
      puts_(&out, ",");
      if (name.s) puts_(&out, name.s);
    }
    puts_(&out, ",\r\n");
    if (name.oom) out.oom = 1;
    free(name.s);
  }
  free(used);
  *out_len = out.n;
  return done(&out);
}

// ------------------------------------------------------------------------------------------------
// Default tones
// ------------------------------------------------------------------------------------------------
int rsm_profile_tones(const char *profile_json, rsm_tone *out, int max) {
  const char *tones = get(ws(profile_json), "CustomTones", NULL), *v, *e;
  int n = 0;
  if (!tones || *tones != '[') return 0;
  for (const char *cur = tones; n < max && next_element(&cur, &v, &e) == 1;) {
    if (*v != '{') continue;
    rsm_tone *t = &out[n];
    memset(t, 0, sizeof *t);
    const char *gear_end, *vol_end;
    t->gear = get(v, "GearList", &gear_end);
    t->volume = get(v, "Volume", &vol_end);
    if (!string_of(get(v, "Name", NULL), t->name, sizeof t->name) || !*t->name || !t->gear || *t->gear != '{')
      continue;
    t->gear_len = (size_t)(gear_end - t->gear);
    t->volume_len = t->volume ? (size_t)(vol_end - t->volume) : 0;
    char amp[64];
    const char *a = get(t->gear, "Amp", NULL);
    t->bass = a && string_of(get(a, "Key", NULL), amp, sizeof amp) && !strncmp(amp, "Bass_", 5);
    n++;
  }
  return n;
}

typedef struct {
  const char *at, *end, *with;
  size_t with_len;
} span;

static int by_position(const void *a, const void *b) {
  const span *x = a, *y = b;
  return x->at < y->at ? -1 : x->at > y->at;
}

char *rsm_tones_edit(const char *json, const rsm_tone_change *c, int n, int *applied) {
  const char *st = get(ws(json), "Static", NULL);
  const char *tm = st ? get(st, "ToneManager", NULL) : NULL;
  const char *tones = tm ? get(tm, "Tones", NULL) : NULL, *v, *e;
  if (!tones || *tones != '[') return NULL;
  span *spans = calloc((size_t)(2 * n + 1), sizeof *spans);
  int ns = 0, found = 0, r;
  if (!spans) return NULL;
  for (const char *cur = tones; (r = next_element(&cur, &v, &e)) == 1;) {
    char key[64];
    if (*v != '{' || !string_of(get(v, "Key", NULL), key, sizeof key)) continue;
    for (int i = 0; i < n; i++) {
      if (!c[i].tone || strcmp(key, c[i].key)) continue;
      const char *gend, *vend;
      const char *gear = get(v, "GearList", &gend), *vol = get(v, "Volume", &vend);
      if (!gear) break;
      spans[ns++] = (span){gear, gend, c[i].tone->gear, c[i].tone->gear_len};
      if (vol && c[i].tone->volume) spans[ns++] = (span){vol, vend, c[i].tone->volume, c[i].tone->volume_len};
      found++;
      break;
    }
  }
  if (r < 0) {
    free(spans);
    return NULL;
  }
  qsort(spans, (size_t)ns, sizeof *spans, by_position);
  text out = {0};
  const char *p = json;
  for (int i = 0; i < ns; i++) {
    put(&out, p, (size_t)(spans[i].at - p));
    put(&out, spans[i].with, spans[i].with_len);
    p = spans[i].end;
  }
  puts_(&out, p);
  free(spans);
  if (applied) *applied = found;
  return done(&out);
}
