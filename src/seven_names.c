// Seven-String Guitar (seven.h): the names part, a 7-string tuning's name ("B Standard 7Str"). A port of rs7string's
// tuning names (github.com/SomeeName/rs7string, MIT, by SomeeName: src/features/tuner/tuningname.cpp, hooks_names.cpp
// and hooks_wrap.cpp; docs/ARCHITECTURE.md section 8, its issue #15).
//
// The game names a tuning by matching string0..5 of the arrangement's manifest Tuning against the six offsets of the
// tuning definitions it has. A 7-string arrangement (the manifest key Rs7String6, the low B's offset) has six offsets
// that mean something else: B Standard is all zero, which the game calls E Standard. Three of the game's functions
// name a tuning; for an arrangement whose Tuning has Rs7String6 their result is changed after they have made it:
//   the manifest one (rs7string's TJ): the song list's rows, the song's and the arrangement's pages. Its outputs are
//     a raw token (the rows' group and sort key) and the display text ("$[36578]Eb Standard", or with a CentOffset the
//     localized text and ": A436"). Its entry reads the key and the six offsets (the game releases the Tuning before
//     it returns), its return names them.
//   the arrangement one (TU, TUT): the pages that have an arrangement's id. TUT is where it has found the
//     arrangement's Tuning and Attributes (CentOffset); its return names them. Its output is localized text.
//   the tuner's (TN): an offset array the tuner part's TK has read Rs7String6 for (seven_tuner_seven_for). Its
//     output is localized text without the CentOffset.
// A known 7-string tuning gets its name (the families below, in the game's spelling: flats, upper case, its accidental
// glyphs) with the mark ([Seven String] TuningNameSuffix, default 7Str, and TuningNamePrefix); any other tuning of a
// 7-string arrangement keeps the game's name, the mark added. English only, as rs7string: the names are plain text,
// which the game's localization passes as it is. A 6-string guitar's or a bass's arrangement keeps the game's name,
// the custom tunings' (mod_cache.c) too: nothing here runs without the key. An 8-string arrangement (Rs7String7 as
// well) gets the 8-string names with the mark's 7 made 8, as rs7string's; the tuner's has the 7-string ones.
//
// A call's entry and its return are tied by the stack pointer, which is the same at both (TU's frame pointer is 4
// below it): a few calls a thread, which the game's threads don't share. The texts are written into the game's
// strings as the game's own are: in their own buffer when it fits, else in one of the game's allocator (efd_alloc), the
// old one freed by the game's string destructor (efd_str_destroy, rs7string's X_STRDTOR). rs7string's construct and
// assign (X_STRCON, X_STRASG) take their arguments in registers of their own, which a mod's call into the game doesn't
// do. Its localization call (X_LOC) is localize_texts.
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <strings.h>

#include "seven.h"

#define TEXT_MAX 1024  // a game string read or written here, in bytes
#define AFFIX_MAX 31   // the mark's prefix and suffix, ASCII

// ---- the names (values in and out)

// strings 0..5 all at k (B Standard's offsets from E A D G B e: 0), the 7th string at k or k - 2. k = +1 (C) .. -9 (D),
// the owner's list in rs7string's issue #15; flats throughout, as the game's "Eb Standard" and "Eb Drop Db".
typedef struct {
  int k;
  const char *standard, *drop;
} family;

static const family g_families7[] = {
    {+1, "C Standard", "C Drop Bb"},   {0, "B Standard", "Drop A"},       {-1, "Bb Standard", "Bb Drop Ab"},
    {-2, "A Standard", "A Drop G"},    {-3, "Ab Standard", "Ab Drop Gb"}, {-4, "G Standard", "G Drop F"},
    {-5, "Gb Standard", "Gb Drop E"},  {-6, "F Standard", "F Drop Eb"},   {-7, "E Standard", "E Drop D"},
    {-8, "Eb Standard", "Eb Drop Db"}, {-9, "D Standard", "D Drop C"},
};

// Eight strings (the 8th string's base is F#1): the canonical instrument keeps F#, the others flats
static const family g_families8[] = {
    {1, "G Standard", "G Drop F"},     {0, "F# Standard", "Drop E"},      {-1, "F Standard", "F Drop Eb"},
    {-2, "E Standard", "E Drop D"},    {-3, "Eb Standard", "Eb Drop Db"}, {-4, "D Standard", "D Drop C"},
    {-5, "Db Standard", "Db Drop B"},  {-6, "C Standard", "C Drop Bb"},   {-7, "B Standard", "B Drop A"},
    {-8, "Bb Standard", "Bb Drop Ab"}, {-9, "A Standard", "A Drop G"},
};

// Exact offsets (string0..5, Rs7String6; the 7th string's base is B1). Extended Drop D / C / B: a 6-string Drop D,
// D Drop C, C# Drop B with the 7th string a fourth below string 0. Double Drop D and Open G7: rs7string's issue gives
// the notes only; these registers (D2 A2 D3 G3 B3 D4 D4, G1 D2 G2 B2 D3 F3 G3) are rs7string's guess.
static const struct {
  int t[7];
  const char *name;
} g_alternates[] = {
    {{-2, 0, 0, 0, 0, 0, -2}, "Extended Drop D"},      {{-4, -2, -2, -2, -2, -2, -4}, "Extended Drop C"},
    {{-5, -3, -3, -3, -3, -3, -5}, "Extended Drop B"}, {{5, 5, 5, 4, 3, -2, 3}, "Double Drop D"},
    {{-2, -2, -3, -5, -6, -9, -4}, "Open G7"},
};

// The name of t[0..n-1] all at k but the last, which is at k or k - 2; NULL when there is none
static const char *family_name(const family *fams, size_t nfams, const int *t, int n) {
  for (int s = 1; s < n - 1; s++)
    if (t[s] != t[0]) return NULL;
  for (size_t i = 0; i < nfams; i++) {
    if (t[0] != fams[i].k) continue;
    if (t[n - 1] == fams[i].k) return fams[i].standard;
    if (t[n - 1] == fams[i].k - 2) return fams[i].drop;
    return NULL;
  }
  return NULL;
}

// t[0..5] = string0..5 (low E .. high e), t[6] = Rs7String6: the canonical name ("B Standard", "Bb Drop Ab",
// "Extended Drop D"), or NULL
static const char *name7(const int t[7]) {
  for (size_t i = 0; i < sizeof g_alternates / sizeof *g_alternates; i++)
    if (!memcmp(g_alternates[i].t, t, sizeof g_alternates[i].t)) return g_alternates[i].name;
  return family_name(g_families7, sizeof g_families7 / sizeof *g_families7, t, 7);
}

// The same with t[7] = Rs7String7: strings 0..6 all at k, the 8th at k or k - 2
static const char *name8(const int t[8]) {
  return family_name(g_families8, sizeof g_families8 / sizeof *g_families8, t, 8);
}

// The 8-string mark from the 7-string one: its first 7 made 8 ("7Str" -> "8Str", "7-String" -> "8-String"); a suffix
// without a 7, or none, gives "8Str"
static void suffix8(char *out, size_t n, const char *suffix7) {
  const char *seven = suffix7 ? strchr(suffix7, '7') : NULL;
  if (!seven) {
    snprintf(out, n, "8Str");
    return;
  }
  snprintf(out, n, "%s", suffix7);
  if ((size_t)(seven - suffix7) < n) out[seven - suffix7] = '8';
}

// "prefix base suffix", an empty part and its blank left out
static void compose(char *out, size_t n, const char *base, const char *prefix, const char *suffix) {
  snprintf(out, n, "%s%s%s%s%s", prefix && *prefix ? prefix : "", prefix && *prefix ? " " : "", base ? base : "",
           suffix && *suffix ? " " : "", suffix && *suffix ? suffix : "");
}

// The game's display form: ASCII upper case, and a word that is a note with an accidental ("Bb", "C#") the letter and
// U+266D / U+266F in UTF-8 ("B♭", "C♯"), as the game's localization has its names ("E♭ STANDARD")
static void display(char *out, size_t n, const char *name) {
  size_t o = 0;
  for (const char *p = name; *p && o + 1 < n;) {
    if (*p == ' ') {
      out[o++] = *p++;
      continue;
    }
    size_t w = strcspn(p, " ");
    if (w == 2 && p[0] >= 'A' && p[0] <= 'G' && (p[1] == 'b' || p[1] == '#')) {
      if (o + 4 >= n) break;
      out[o++] = p[0];
      memcpy(out + o, p[1] == 'b' ? "\xE2\x99\xAD" : "\xE2\x99\xAF", 3);
      o += 3;
    } else {
      for (size_t i = 0; i < w && o + 1 < n; i++) out[o++] = p[i] >= 'a' && p[i] <= 'z' ? (char)(p[i] - 'a' + 'A') : p[i];
    }
    p += w;
  }
  if (n) out[o] = 0;
}

// The pitch reference the game appends to a matched name for a CentOffset (TJ 0x10ee3df..0x10ee41b): (int)(440 *
// 2^(cents / 1200)) with its float steps, truncated; 440 for 0
static int reference_a(int cents) {
  if (!cents) return 440;
  float x = (float)((double)cents / 1200.0);
  float p = powf(2.0f, x);
  float a = (float)((double)p * 440.0);
  return (int)a;
}

// ": A<reference>" for cents other than 0, as the game's (its format at 0x1d2ab58); "" for 0
static void decoration(char *out, size_t n, int cents) {
  if (cents)
    snprintf(out, n, ": A%d", reference_a(cents));
  else if (n)
    out[0] = 0;
}

// A stock display text (localized, maybe with ": A<digits>" at the end) with the mark in display form around the
// name, the decoration staying last: ("E STANDARD: A436", "", "7Str") -> "E STANDARD 7STR: A436"
static void mark_stock(char *out, size_t n, const char *stock, const char *prefix, const char *suffix) {
  size_t name_len = strlen(stock);
  const char *tail = "", *last = NULL;
  for (const char *p = strstr(stock, ": A"); p; p = strstr(p + 1, ": A")) last = p;
  if (last && last[3] && strspn(last + 3, "0123456789") == strlen(last + 3)) {
    tail = last;
    name_len = (size_t)(last - stock);
  }
  char pre[AFFIX_MAX * 3 + 2] = "", suf[AFFIX_MAX * 3 + 2] = "";
  if (prefix && *prefix) display(pre, sizeof pre, prefix);
  if (suffix && *suffix) display(suf, sizeof suf, suffix);
  snprintf(out, n, "%s%s%.*s%s%s%s", pre, *pre ? " " : "", (int)name_len, stock, *suf ? " " : "", suf, tail);
}

// ---- the settings: [Seven String] in RSMods.ini, read at the start
typedef struct {
  int on;                     // TuningNames
  char prefix[AFFIX_MAX + 1]; // TuningNamePrefix
  char suffix[AFFIX_MAX + 1]; // TuningNameSuffix
} names_config;
static names_config g_cfg = {1, "", "7Str"};

// The display text of a known tuning of a 7-string (eight: 8-string) arrangement, decorated for its CentOffset when
// `decorate`: 1; 0 when the tuning has no name here (out is "")
static int known_display(char *out, size_t n, const int t[8], int eight, const names_config *c, int cents,
                         int decorate) {
  const char *name = eight ? name8(t) : name7(t);
  if (n) out[0] = 0;
  if (!c->on || !name) return 0;
  char suffix[AFFIX_MAX + 2], composed[TEXT_MAX], deco[24] = "";
  if (eight)
    suffix8(suffix, sizeof suffix, c->suffix);
  else
    snprintf(suffix, sizeof suffix, "%s", c->suffix);
  compose(composed, sizeof composed, name, c->prefix, suffix);
  display(out, n, composed);
  if (decorate) decoration(deco, sizeof deco, cents);
  strlcat(out, deco, n);
  return 1;
}

// A prefix or suffix: printable ASCII, at most AFFIX_MAX characters
static int valid_affix(const char *s) {
  if (strlen(s) > AFFIX_MAX) return 0;
  for (; *s; s++)
    if (*s < 0x20 || *s > 0x7e) return 0;
  return 1;
}

static void read_config(names_config *c) {
  const char *on = rsm_get("Seven String", "TuningNames");
  c->on = !on || strcasecmp(on, "off");
  static const struct {
    const char *key;
    size_t at;
  } affixes[] = {{"TuningNamePrefix", offsetof(names_config, prefix)},
                 {"TuningNameSuffix", offsetof(names_config, suffix)}};
  for (size_t i = 0; i < 2; i++) {
    const char *v = rsm_get("Seven String", affixes[i].key);
    if (!v) continue;  // absent: the default; present and empty: none
    if (!valid_affix(v)) {
      rsm_log("seven-string: names: [Seven String] %s = \"%s\" is not ASCII of at most %d characters: \"%s\" instead",
              affixes[i].key, v, AFFIX_MAX, (char *)c + affixes[i].at);
      continue;
    }
    snprintf((char *)c + affixes[i].at, AFFIX_MAX + 1, "%s", v);
  }
}

// ---- the log: each line once (the song list is named again and again), at most 60 lines a run
static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t g_log_seen[64];
static unsigned g_log_next;
static int g_logs = 60;

static void nlog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void nlog(const char *fmt, ...) {
  char buf[TEXT_MAX];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  uint32_t h = 2166136261u;
  for (const char *p = buf; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
  pthread_mutex_lock(&g_log_lock);
  int show = g_logs > 0;
  for (int i = 0; i < 64 && show; i++)
    if (g_log_seen[i] == h) show = 0;
  if (show) {
    g_log_seen[g_log_next++ % 64] = h;
    g_logs--;
  }
  pthread_mutex_unlock(&g_log_lock);
  if (show) rsm_log("seven-string: names: %s", buf);
}

// ---- the game's strings: text inline (16 bytes at its start) while the word at efd_str_inline is the address of the
// one at efd_str_size, else at the address at its start; efd_str_size is where the text ends, efd_str_inline where its
// buffer does (as mod_song_list_titles.c and rsmods.c read them)
static void put_ptr(uint8_t *field, const void *host) {
  if (rc_guest_ptr_size == 8)
    RC_ST(uint64_t, field, RC_GUEST(host));
  else
    RC_ST(uint32_t, field, (uint32_t)RC_GUEST(host));
}

// The text of the string at s, at most TEXT_MAX - 1 bytes ("" for none)
static void read_text(const uint8_t *s, char *out, size_t n) {
  out[0] = 0;
  if (!s) return;
  const uint8_t *end_at = s + RC_OFF(efd_str_size);
  const uint8_t *data = rc_mod_ptr(s + RC_OFF(efd_str_inline)) == end_at ? s : rc_mod_ptr(s);
  const uint8_t *end = rc_mod_ptr(end_at);
  if (!data || !end || end < data || (size_t)(end - data) >= n) return;
  memcpy(out, data, (size_t)(end - data));
  out[end - data] = 0;
}

// Makes the string at s hold text: in its own buffer when it fits, else in one from the game's allocator, the old one
// freed by the game's string destructor. 1, or 0 when it couldn't.
static int put_text(uint8_t *s, const char *text) {
  size_t n = strlen(text);
  uint8_t *end_at = s + RC_OFF(efd_str_size), *buf_end_at = s + RC_OFF(efd_str_inline);
  uint8_t *buf_end = rc_mod_ptr(buf_end_at);
  uint8_t *data = buf_end == end_at ? s : rc_mod_ptr(s);
  if (!data || !buf_end || buf_end < data) return 0;
  if ((size_t)(buf_end - data) < n + 1) {
    uint8_t *buf = RC_HOST(OV_GUEST(efd_alloc, n + 1, 0));
    if (!buf) return 0;
    OV_GUEST(efd_str_destroy, (uint64_t)(uintptr_t)s);  // frees the old text, unless it is inline
    put_ptr(s, buf);
    put_ptr(buf_end_at, buf + n + 1);
    data = buf;
  }
  memcpy(data, text, n);
  data[n] = 0;
  put_ptr(end_at, data + n);
  return 1;
}

// A thread's string and text for the localization, in the game's memory (made once a thread, never freed)
static __thread uint8_t *t_scratch;
#define SCRATCH_TEXT 0x20

// The game's localization of text (the language the game runs in) into out: 1, or 0 when it can't be asked
static int game_localize(char *out, size_t n, const char *text) {
  if (!RC_ADDR(localize_texts) || !RC_ADDR(service_holder)) return 0;
  void *holder = rc_mod_ptr((const void *)(uintptr_t)RC_GA(service_holder));
  size_t len = strlen(text);
  if (!holder || SCRATCH_TEXT + len + 1 > TEXT_MAX) return 0;
  if (!t_scratch) t_scratch = rc_guest_alloc(TEXT_MAX);
  uint8_t *s = t_scratch;
  if (!s) return 0;
  memcpy(s + SCRATCH_TEXT, text, len + 1);
  put_ptr(s + RC_OFF(efd_str_size), s);  // an empty inline string, as the game hands it one
  put_ptr(s + RC_OFF(efd_str_inline), s + RC_OFF(efd_str_size));
  s[0] = 0;
  OV_GUEST(localize_texts, (uint64_t)(uintptr_t)s, (uint64_t)(uintptr_t)holder, 1,
           (uint64_t)(uintptr_t)(s + SCRATCH_TEXT));
  read_text(s, out, n);
  OV_GUEST(efd_str_destroy, (uint64_t)(uintptr_t)s);
  return *out != 0;
}

// "$[36578]Eb Standard" -> "E♭ STANDARD" through the game's localization; text without "$" is as it is. When the game
// can't be asked: the English after the id, in the display form.
static void localize(char *out, size_t n, const char *text) {
  const char *close = strchr(text, ']');
  if (text[0] == '$' && game_localize(out, n, text)) return;
  if (!strncmp(text, "$[", 2) && close)
    display(out, n, close + 1);
  else
    snprintf(out, n, "%s", text);
}

// ---- the arrangement's Tuning: numbers of a JSON object, through the game's JSON vtable (as the tuner part reads
// them); the keys in the game's memory
enum { KEY_7, KEY_8, KEY_CENTS, KEY_STRING0 };  // Rs7String6, Rs7String7, CentOffset, string0..string5
static const char *g_keys[KEY_STRING0 + 6];

static int json_number(void *obj, const char *key, double *out) {
  if (!obj || !key) return 0;
  uint64_t node = OV_VCALL((uint64_t)(uintptr_t)obj, (int)RC_SLOT(json_value_find), (uint64_t)(uintptr_t)key);
  if (!(uint32_t)node) return 0;
  uint64_t num = OV_VCALL((uint64_t)(uintptr_t)RC_HOST(node), (int)RC_SLOT(json_value_number));
  if (!(uint32_t)num) return 0;
  *out = RC_LD(double, (uint8_t *)RC_HOST(num) + RC_OFF(json_number_value));
  return 1;
}

// A tuning offset: a number in (-25, 25), truncated as the game's readers do
static int json_offset(void *obj, const char *key, int *out) {
  double d = 0;
  if (!json_number(obj, key, &d) || !(d > -25.0 && d < 25.0)) return 0;
  *out = (int)d;
  return 1;
}

// A Tuning with Rs7String6 and all of string0..5: t[0..6] (and t[7], *eight, with Rs7String7). 0: not a 7-string one.
// It calls the game: eax, ecx and edx change.
static int read_seven(void *tuning, int t[8], int *eight) {
  *eight = 0;
  if (!json_offset(tuning, g_keys[KEY_7], &t[6])) return 0;
  for (int s = 0; s < 6; s++)
    if (!json_offset(tuning, g_keys[KEY_STRING0 + s], &t[s])) return 0;
  *eight = json_offset(tuning, g_keys[KEY_8], &t[7]);
  return 1;
}

// ---- the calls under way, a thread's: from an entry (or TUT) to the return with the same stack pointer
typedef struct {
  uint32_t sp;     // 0: free
  uint8_t *raw;    // TJ's outputs; TU's output in disp
  uint8_t *disp;
  int cents, eight;
  int t[8];
} name_call;
#define CALLS 4
static __thread name_call t_tj[CALLS], t_tu[CALLS];
static __thread unsigned t_tj_next, t_tu_next;

static void forget(name_call *calls, uint32_t sp) {
  for (int i = 0; i < CALLS; i++)
    if (calls[i].sp == sp) calls[i].sp = 0;
}

// A slot for a new call: a free one, else the oldest (a call that never returned, which the game's don't do)
static name_call *open_call(name_call *calls, unsigned *next, uint32_t sp) {
  for (int i = 0; i < CALLS; i++)
    if (!calls[i].sp) {
      calls[i].sp = sp;
      return &calls[i];
    }
  name_call *c = &calls[(*next)++ % CALLS];
  c->sp = sp;
  return c;
}

static int take(name_call *calls, uint32_t sp, name_call *out) {
  for (int i = 0; i < CALLS; i++)
    if (calls[i].sp == sp) {
      *out = calls[i];
      calls[i].sp = 0;
      return 1;
    }
  return 0;
}

static void offsets_text(char *out, size_t n, const int t[8], int eight) {
  int k = snprintf(out, n, "%d %d %d %d %d %d | 7th %d", t[0], t[1], t[2], t[3], t[4], t[5], t[6]);
  if (eight && k > 0 && (size_t)k < n) snprintf(out + k, n - (size_t)k, " 8th %d", t[7]);
}

static const char *suffix_of(int eight, char *buf, size_t n) {
  if (!eight) return g_cfg.suffix;
  suffix8(buf, n, g_cfg.suffix);
  return buf;
}

#define PTR(f, name) ((uint8_t *)(uintptr_t)SEVEN_GET(f, name))

// ---- TJ: the manifest resolver
// Its entry: a 7-string Tuning's offsets and the outputs, for its return. The game's calls change ecx and edx, the
// outputs: they get their values back (eax is not read before the game sets it).
static void on_tj(rc_mid_frame *f, void *user) {
  if (!seven_on || !g_cfg.on) return;
  uint32_t sp = (uint32_t)SEVEN_GET(f, "sp");
  uint8_t *raw = PTR(f, "raw"), *disp = PTR(f, "display"), *tuning = PTR(f, "tuning");
  forget(t_tj, sp);
  if (!tuning || !raw || !disp) return;
  int t[8] = {0}, eight = 0, seven = read_seven(tuning, t, &eight);
  SEVEN_SET(f, "raw", (uintptr_t)raw);
  SEVEN_SET(f, "display", (uintptr_t)disp);
  if (!seven) return;
  name_call *c = open_call(t_tj, &t_tj_next, sp);
  c->raw = raw;
  c->disp = disp;
  c->cents = (int32_t)SEVEN_GET(f, "cents");
  c->eight = eight;
  memcpy(c->t, t, sizeof t);
}

// Its return: a known tuning's name is the raw token (the rows' group) and, decorated like a matched stock name, the
// display text; any other tuning keeps its token and its display text gets the mark
static void on_tj_ret(rc_mid_frame *f, void *user) {
  if (!seven_on || !g_cfg.on) return;
  name_call c;
  if (!take(t_tj, (uint32_t)SEVEN_GET(f, "sp"), &c)) return;
  char raw0[TEXT_MAX], disp0[TEXT_MAX], raw[TEXT_MAX], disp[TEXT_MAX], loc[TEXT_MAX], deco[24], s8[AFFIX_MAX + 2],
      offs[96];
  read_text(c.raw, raw0, sizeof raw0);
  read_text(c.disp, disp0, sizeof disp0);
  if (known_display(raw, sizeof raw, c.t, c.eight, &g_cfg, 0, 0)) {
    decoration(deco, sizeof deco, c.cents);
    snprintf(disp, sizeof disp, "%s%s", raw, deco);
    put_text(c.raw, raw);
  } else {
    snprintf(raw, sizeof raw, "%s", raw0);
    localize(loc, sizeof loc, disp0);
    mark_stock(disp, sizeof disp, loc, g_cfg.prefix, suffix_of(c.eight, s8, sizeof s8));
  }
  put_text(c.disp, disp);
  offsets_text(offs, sizeof offs, c.t, c.eight);
  nlog("the song list / song pages: %s, CentOffset %d: \"%s\" / \"%s\" -> \"%s\" / \"%s\"", offs, c.cents, raw0, disp0,
       raw, disp);
}

// ---- TU: the arrangement resolver
// TUT: it has found the arrangement's Tuning (and Attributes with its CentOffset). The game's next instructions set
// eax, ecx and edx themselves.
static void on_tut(rc_mid_frame *f, void *user) {
  if (!seven_on || !g_cfg.on) return;
  uint32_t sp = (uint32_t)SEVEN_GET(f, "frame") + 4;  // the entry's stack pointer, the return's
  uint8_t *tuning = PTR(f, "tuning"), *attributes = PTR(f, "attributes"), *out = PTR(f, "out");
  forget(t_tu, sp);
  int t[8] = {0}, eight = 0;
  if (!tuning || !out || !read_seven(tuning, t, &eight)) return;
  double cents = 0;
  name_call *c = open_call(t_tu, &t_tu_next, sp);
  c->disp = out;  // the return's too: paths without TUT reach it
  c->cents = json_number(attributes, g_keys[KEY_CENTS], &cents) && cents > -1200.0 && cents < 1200.0 ? (int)cents : 0;
  c->eight = eight;
  memcpy(c->t, t, sizeof t);
}

// Its return: the output named, or the game's name marked; eax (the output) back after the game's calls
static void on_tu_ret(rc_mid_frame *f, void *user) {
  if (!seven_on || !g_cfg.on) return;
  name_call c;
  uint8_t *out = PTR(f, "out");
  if (!take(t_tu, (uint32_t)SEVEN_GET(f, "sp"), &c) || !out || c.disp != out) return;
  char stock[TEXT_MAX], text[TEXT_MAX], loc[TEXT_MAX], s8[AFFIX_MAX + 2], offs[96];
  read_text(out, stock, sizeof stock);
  if (!known_display(text, sizeof text, c.t, c.eight, &g_cfg, c.cents, 1)) {
    localize(loc, sizeof loc, stock);
    mark_stock(text, sizeof text, loc, g_cfg.prefix, suffix_of(c.eight, s8, sizeof s8));
  }
  put_text(out, text);
  SEVEN_SET(f, "out", (uintptr_t)out);
  offsets_text(offs, sizeof offs, c.t, c.eight);
  nlog("an arrangement's page: %s, CentOffset %d: \"%s\" -> \"%s\"", offs, c.cents, stock, text);
}

// ---- TN: the tuner's resolver, at its two returns (its arguments are still above the return address): the output
// named for an offset array TK read Rs7String6 for, or the game's name marked; eax (the output) back after the
// game's calls. The tuner's names are 7-string ones, as rs7string's.
static void tn_rename(rc_mid_frame *f, int vout, int voffsets) {
  uint8_t *out = (uint8_t *)(uintptr_t)rc_mid_get(f, vout);
  const uint8_t *offsets = (const uint8_t *)(uintptr_t)rc_mid_get(f, voffsets);
  int t[8] = {0};
  if (!out || !offsets || !seven_tuner_seven_for(offsets, &t[6])) return;
  for (int s = 0; s < 6; s++) t[s] = RC_LD(int32_t, offsets + 4 * s);
  char stock[TEXT_MAX], text[TEXT_MAX], loc[TEXT_MAX], offs[96];
  read_text(out, stock, sizeof stock);
  if (!known_display(text, sizeof text, t, 0, &g_cfg, 0, 0)) {
    localize(loc, sizeof loc, stock);
    mark_stock(text, sizeof text, loc, g_cfg.prefix, g_cfg.suffix);
  }
  put_text(out, text);
  rc_mid_set(f, vout, (uintptr_t)out);
  offsets_text(offs, sizeof offs, t, 0);
  nlog("the tuner: %s: \"%s\" -> \"%s\"", offs, stock, text);
}

static void on_tn_ret(rc_mid_frame *f, void *user) {
  if (!seven_on || !g_cfg.on) return;
  tn_rename(f, SEVEN_VAL(f, "out"), SEVEN_VAL(f, "offsets"));
}

static void on_tn_ret_custom(rc_mid_frame *f, void *user) {
  if (!seven_on || !g_cfg.on) return;
  tn_rename(f, SEVEN_VAL(f, "out"), SEVEN_VAL(f, "offsets"));
}

int seven_names_init(void) {
  static const struct {
    const char *name;
    rc_mid_fn fn;
  } points[] = {
      {"seven_tj", on_tj},   {"seven_tj_ret", on_tj_ret}, {"seven_tut", on_tut},
      {"seven_tu_ret", on_tu_ret}, {"seven_tn_ret", on_tn_ret}, {"seven_tn_ret_custom", on_tn_ret_custom},
  };
  for (size_t k = 0; k < sizeof points / sizeof *points; k++)
    if (seven_mid(points[k].name, points[k].fn)) return -1;

  static const char keys[] = "Rs7String6\0Rs7String7\0CentOffset\0string0\0string1\0string2\0string3\0string4\0string5";
  char *mem = rc_guest_alloc(sizeof keys);
  if (!mem) {
    rsm_log("seven-string: names: no memory in the game for its keys");
    return -1;
  }
  memcpy(mem, keys, sizeof keys);
  for (int k = 0; k < KEY_STRING0 + 6; k++, mem += strlen(mem) + 1) g_keys[k] = mem;

  read_config(&g_cfg);
  if (g_cfg.on && (!RC_ADDR(efd_alloc) || !RC_ADDR(efd_str_destroy) || !RC_HAS_OFF(efd_str_size) ||
                   !RC_HAS_OFF(efd_str_inline) || !RC_HAS_OFF(json_number_value))) {
    rsm_log("seven-string: names: the game's strings are not known on %s: the game's tuning names", rc_target());
    g_cfg.on = 0;
  }
  if (g_cfg.on)
    rsm_log("seven-string: names on, the mark: prefix \"%s\", suffix \"%s\"", g_cfg.prefix, g_cfg.suffix);
  else
    rsm_log("seven-string: names off: the game's tuning names");
  return 0;
}
