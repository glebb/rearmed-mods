// Seven-String Guitar (seven.h): the diagrams part, the chord book's, song panel's and lesson scene's chord diagrams
// with a row for the seventh string. Ported from rs7string (github.com/SomeeName/rs7string, MIT, by SomeeName):
// src/features/diagrams (its ARCHITECTURE section 9) and its recipes data/chord-diagrams/*.rs7da (MIT, made by its
// tools/diagram_patch.py).
//
// A chord diagram is a Scaleform movie's object the game fills with numbers by name: StartFret, LowEFret ... HighEFret,
// LowE_Finger ... HighE_Finger. Two serializers make it: one from the manifest's chord (its Frets and Fingers arrays,
// the arrangement's CapoFret), one from the loaded song's chord template. After each has run, the shape's seventh
// (and eighth) string goes in too: LowBFret, LowB_Finger and LowB_Color (LowFs* for the eighth), and StartFret and the
// six frets again when the fret window over all the strings differs from the one over six. A manifest chord with six
// entries (DotCharter's manifests) takes its seventh string from the loaded song's T8 record of the same ChordId, when
// the arrangement's attributes are the ones that song's post-processing looked up (seven_dgb binds the two). Six-string
// shapes and bass are left as the game makes them.
//
// The names go in as the game's serializers put them: the name interned by the game (json_key_intern; the Mac's
// json_key_make takes the same two arguments the other way round) and set on the
// object (json_object_set). A number object the game would get from num_intern, which takes its result's address in a
// register a mod can't give, is made here as num_intern makes one (the game's allocator, its vtable, the double), one
// for each value, held by this library for the run: no number made here is ever freed, so the game's table of numbers
// never meets one. The interned names are held for the run as well.
//
// The movies that draw the diagrams (gfxassets/libs/commonlib.gfx, gfxassets/views/contentpanel.gfx and
// gfxassets/views/ge_game.gfx in static.psarc) need a class that draws the extra rows. rs7string's recipes make it
// from the player's own movie: copies of its class tag and rs7string's own bytes, the old and the new tag checked by
// SHA-256; Learn & Play's movies are the ones they were made for. The game opens a movie through Scaleform's file
// opener, whose file function (the game's, 0x101a140) opens it through FrontEnd_OpenFile, the hook point
// frontend_open_file. At the first open of one of the three in a run, the library makes all three from static.psarc
// (rsm_psarc), each with its class rebuilt, and writes them where the game opens files from disk, as mod_cache.c
// writes its archives: Update/rearmed-mods-<movie>.gfx, given to the game as c:\rocksmith2014\update\...; on the Mac
// (whose file function 0x1003c7510 calls FrontEnd_OpenFile the same way) rearmed-mods/<movie>.gfx, which the hook
// opens through FrontEnd_OpenFile itself, as mod_cache.c does there. When a hash
// doesn't match (another movie), the game opens its own and the log says why. rs7string instead wraps the opener's
// vtable slot and gives Scaleform an in-memory file.
//
// The recipes are compiled in from data/rs7string/chord-diagrams/ (rs7string's three files, MIT: copy instructions
// over the player's own class tag, its hashes and 78 single new bytes; no bytes of the game's movies).
//
// What rs7string has and this part hasn't: its settings ([chords] diagrams7, [debug] diagrams_trace: the defaults are
// the behaviour), the FileOpener hook (above), compressed movies (refused there too).
#include <CommonCrypto/CommonDigest.h>
#include <ctype.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rsmods_tuning.h"
#include "seven.h"

#define PTR(f, name) ((uint8_t *)(uintptr_t)SEVEN_GET(f, name))
#define FIELD(p, name) ((uint8_t *)(p) + RC_OFF(name))

// ---- the log: at most this many lines of this part a run
static int g_logs = 60;

static void dlog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void dlog(const char *fmt, ...) {
  if (__atomic_sub_fetch(&g_logs, 1, __ATOMIC_RELAXED) < 0) return;
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  rsm_log("seven-string: diagrams: %s", buf);
}

// ---- guest memory
static uint8_t *ptr_at(const void *base, int64_t off) { return base ? rc_mod_ptr((const uint8_t *)base + off) : NULL; }
static void ptr_put(uint8_t *at, const void *host) {
  uint64_t g = RC_GUEST(host);
  memcpy(at, &g, (size_t)rc_guest_ptr_size);
}
// A string in the game's memory, for the rest of the run
static char *guest_text(const char *s) {
  char *g = rc_guest_alloc(strlen(s) + 1);
  if (g) strcpy(g, s);
  return g;
}

// ---- the game's JSON values
static uint8_t *vcall(const void *obj, int64_t slot) { return obj ? RC_HOST(OV_VCALL((uintptr_t)obj, (int)slot)) : NULL; }
static void add_ref(const void *v) {
  if (v) OV_VCALL((uintptr_t)v, (int)RC_SLOT(json_node_add_ref));
}
static void release(const void *v) {
  if (v) OV_VCALL((uintptr_t)v, (int)RC_SLOT(json_node_release));
}
// The object (dictionary) a value is (rs7string's Dictionary), NULL for none
static uint8_t *dictionary_of(const void *v) { return vcall(v, RC_SLOT(json_node_object)); }
// A value's number, when it is a whole number in [lo, hi]
static int whole_number(const void *v, double lo, double hi, int *out) {
  uint8_t *num = vcall(v, RC_SLOT(json_node_number));
  if (!num) return 0;
  double d = RC_LD(double, FIELD(num, json_number_value));
  if (!(d >= lo && d <= hi) || d != (double)(int)d) return 0;
  *out = (int)d;
  return 1;
}
// An array's values (rs7string's ArrayCount): 0 for none or a broken vector
static int array_count(const void *array) {
  const uint8_t *b = ptr_at(array, RC_OFF(json_array_items) + RC_OFF(std_vector_begin));
  const uint8_t *e = ptr_at(array, RC_OFF(json_array_items) + RC_OFF(std_vector_end));
  if (!b || e < b || (e - b) % rc_guest_ptr_size || e - b > 64 * rc_guest_ptr_size) return 0;
  return (int)((e - b) / rc_guest_ptr_size);
}
// Value i of an array as a whole number in [-1, 255] (rs7string's ArrayNumber)
static int array_number(const void *array, int i, int *out) {
  if (i < 0 || i >= array_count(array)) return 0;
  uint8_t *begin = ptr_at(array, RC_OFF(json_array_items) + RC_OFF(std_vector_begin));
  return whole_number(ptr_at(begin, (int64_t)i * rc_guest_ptr_size), -1, 255, out);
}

// ---- putting a number on a diagram object under a name, as the serializers do
// The names (the keys of the six strings are the game's, the rest the extended class's)
enum { KEY_START, KEY_FRET, KEY_FINGER6 = KEY_FRET + SEVEN_CHORD_SLOTS, KEY_COLOR6 = KEY_FINGER6 + 2, NKEYS = KEY_COLOR6 + 2 };
static const char *const KEY_TEXT[NKEYS] = {"StartFret", "LowEFret",  "AFret",       "DFret",        "GFret",
                                            "BFret",     "HighEFret", "LowBFret",    "LowFsFret",    "LowB_Finger",
                                            "LowFs_Finger", "LowB_Color", "LowFs_Color"};
static const int STRING_COLOR[2] = {0x808080, 0x00ffff};  // LowB_Color, LowFs_Color (the eighth's provisional)
#define NNUMBERS 96  // the numbers made: frets, fingers, StartFret, the two colours
#define CHORD_ID "ChordId"

static struct {
  char *text[NKEYS];   // the names, in the game's memory
  uint8_t *keys;       // their interned keys, a pointer each (held for the run; 0 until first used)
  uint8_t *numbers;    // the numbers made, a pointer each
  int value[NNUMBERS]; // their values
  int nnumbers;
  char *chord_id;      // "ChordId"
} g;
static pthread_mutex_t g_put_lock = PTHREAD_MUTEX_INITIALIZER;

// The interned key of name k (its slot in the game's memory), NULL when the game gave none
static uint8_t *key_slot(int k) {
  uint8_t *slot = g.keys + (size_t)k * rc_guest_ptr_size;
  if (rc_mod_ptr(slot)) return slot;
  pthread_mutex_lock(&g_put_lock);
  if (!rc_mod_ptr(slot)) {
    // The game's call takes the text first (Learn & Play's json_key_intern) or the key's place first (the Mac's
    // json_key_make): whichever this game has
    if (RC_ADDR(json_key_make))
      OV_GUEST(json_key_make, (uintptr_t)slot, (uintptr_t)g.text[k]);
    else
      OV_GUEST(json_key_intern, (uintptr_t)g.text[k], (uintptr_t)slot);
  }
  pthread_mutex_unlock(&g_put_lock);
  return rc_mod_ptr(slot) ? slot : NULL;
}

// The number object of value v (its slot), NULL when there is no room or no memory
static uint8_t *number_slot(int v) {
  uint8_t *slot = NULL;
  pthread_mutex_lock(&g_put_lock);
  for (int i = 0; i < g.nnumbers && !slot; i++)
    if (g.value[i] == v) slot = g.numbers + (size_t)i * rc_guest_ptr_size;
  if (!slot && g.nnumbers < NNUMBERS) {
    uint8_t *num = RC_HOST(OV_GUEST(efd_alloc, (uint64_t)RC_OFF(json_number_size), 0));
    if (num) {
      ptr_put(num, (const void *)(uintptr_t)RC_GA(json_number_vtable));
      RC_ST(int32_t, FIELD(num, json_number_refs), 1);  // this library's reference, never given back
      RC_ST(double, FIELD(num, json_number_value), (double)v);
      slot = g.numbers + (size_t)g.nnumbers * rc_guest_ptr_size;
      ptr_put(slot, num);
      g.value[g.nnumbers++] = v;
    }
  }
  pthread_mutex_unlock(&g_put_lock);
  return slot;
}

static void put(uint8_t *object, int k, int v) {
  uint8_t *key = key_slot(k), *num = number_slot(v);
  if (key && num) OV_GUEST(json_object_set, (uintptr_t)object, (uintptr_t)key, (uintptr_t)num);
  else dlog("%s = %d not set: no %s", KEY_TEXT[k], v, key ? "number object" : "key");
}

// ---- a chord's shape (rs7string's rs7diagram::Shape)
typedef struct {
  int count;  // its strings: 6, 7 or 8
  int frets[SEVEN_CHORD_SLOTS], fingers[SEVEN_CHORD_SLOTS];  // -1: not in the chord; fingers 0 thumb, 1-4
  int capo;      // a manifest's capo fret: a fret equal to it is open
  int manifest;  // from the manifest (its frets count the capo), else from a T8 (open is 0)
} shape;

// A T8 record of a 7- or 8-string arrangement (rs7string's FromT8)
static int from_t8(const uint8_t *t8, shape *s) {
  int n = t8 ? seven_chords_t8_slots(t8) : 0;
  if (n < 7 || n > SEVEN_CHORD_SLOTS) return 0;
  *s = (shape){.count = n};
  for (int i = 0; i < SEVEN_CHORD_SLOTS; i++) {
    s->frets[i] = seven_chords_t8_fret(t8, i);
    s->fingers[i] = seven_chords_t8_finger(t8, i);
  }
  return 1;
}

// A manifest's six-string shape with the extra strings of its T8, when the six are the T8's (rs7string's
// MergeManifestT8)
static int merge_t8(const uint8_t *t8, shape *s) {
  shape t;
  if (!from_t8(t8, &t)) return 0;
  for (int i = 0; i < SEVEN_STOCK; i++) {
    int fret = s->frets[i] == s->capo ? 0 : s->frets[i], finger = s->fingers[i] == 255 ? -1 : s->fingers[i];
    if (fret != t.frets[i] || finger != t.fingers[i]) return 0;
  }
  s->count = t.count;
  for (int i = SEVEN_STOCK; i < t.count; i++) {
    // the T8 has open as 0 (the capo rule applied): the manifest's way back, so the window sees no false lowest fret
    s->frets[i] = t.frets[i] == 0 ? s->capo : t.frets[i];
    s->fingers[i] = t.fingers[i];
  }
  return 1;
}

// The diagram's numbers for a 7- or 8-string shape, on the object the game has filled (rs7string's Emit): the fret
// window over all its strings, the six frets in it again, the extra strings' fret, finger and colour
static void emit(const shape *s, uint8_t *object, const char *from) {
  if (!object || s->count < 7 || s->count > SEVEN_CHORD_SLOTS) return;
  int lo = 255, hi = -1, fret[SEVEN_CHORD_SLOTS];
  for (int i = 0; i < s->count; i++) {
    fret[i] = s->manifest && s->frets[i] == s->capo ? 0 : s->frets[i];  // a capo-only shape has no lowest fret
    if (fret[i] > 0) {
      if (fret[i] < lo) lo = fret[i];
      if (fret[i] > hi) hi = fret[i];
    }
  }
  int start = hi < 4 ? 1 : lo, offset = hi < 4 ? 0 : lo - 1;
  put(object, KEY_START, start);
  for (int i = 0; i < s->count; i++) {
    put(object, KEY_FRET + i, fret[i] > 0 ? fret[i] - offset : fret[i]);
    if (i < SEVEN_STOCK) continue;
    if (s->frets[i] > s->capo && s->fingers[i] >= 0 && s->fingers[i] <= 4)
      put(object, KEY_FINGER6 + i - SEVEN_STOCK, s->fingers[i]);
    put(object, KEY_COLOR6 + i - SEVEN_STOCK, STRING_COLOR[i - SEVEN_STOCK]);
  }
  static int shown;
  if (__atomic_fetch_add(&shown, 1, __ATOMIC_RELAXED) < 8)
    dlog("%d-string diagram from the %s: StartFret %d, string 7 at %d (finger %d)", s->count, from, start, s->frets[6],
         s->fingers[6]);
}

// ---- the songs' attributes (rs7string's bindings): the dictionary SNG_Song_PostProcess looked up for a song, with a
// reference held until the song is freed
#define NBINDINGS 64
static struct {
  const void *song;
  uint8_t *dictionary;
} g_bind[NBINDINGS];
static pthread_mutex_t g_bind_lock = PTHREAD_MUTEX_INITIALIZER;

static void bind(const void *song, const void *attributes) {
  uint8_t *dict = dictionary_of(attributes), *old = NULL;
  int k = -1;
  if (!song || !dict) return;
  add_ref(dict);
  pthread_mutex_lock(&g_bind_lock);
  for (int i = 0; i < NBINDINGS && (k < 0 || g_bind[k].song != song); i++)
    if (g_bind[i].song == song || (k < 0 && !g_bind[i].song)) k = i;
  if (k >= 0) {
    old = g_bind[k].dictionary;
    g_bind[k].song = song;
    g_bind[k].dictionary = dict;
  }
  pthread_mutex_unlock(&g_bind_lock);
  release(k >= 0 ? old : dict);
  if (k < 0) dlog("no room to bind song %p's attributes", song);
}

void seven_diagrams_song_freed(const void *song) {
  uint8_t *old[NBINDINGS];
  int n = 0;
  if (!song) return;
  pthread_mutex_lock(&g_bind_lock);
  for (int i = 0; i < NBINDINGS; i++)
    if (g_bind[i].song == song) {
      old[n++] = g_bind[i].dictionary;
      g_bind[i].song = NULL;
      g_bind[i].dictionary = NULL;
    }
  pthread_mutex_unlock(&g_bind_lock);
  for (int i = 0; i < n; i++) release(old[i]);
}

// The extra strings of a manifest's six-string shape from the T8 record of its ChordId in the song whose attributes
// these are, when that song has a 7- or 8-string RS7X block (rs7string's LoadedManifest)
static int from_loaded_song(const void *attributes, const void *chord, shape *s) {
  uint8_t *dict = dictionary_of(attributes);
  int id = -1, ok = 0;
  if (!dict || !chord) return 0;
  uint8_t *node = RC_HOST(OV_VCALL((uintptr_t)chord, (int)RC_SLOT(json_node_member), (uintptr_t)g.chord_id));
  if (!whole_number(node, -1, 0x7fffffff, &id) || id < 0) return 0;
  pthread_mutex_lock(&g_bind_lock);  // a song's records are freed after its binding goes
  for (int i = 0; i < NBINDINGS; i++) {
    if (!g_bind[i].song || g_bind[i].dictionary != dict) continue;
    uint8_t *t8 = NULL;
    uint32_t count = 0;
    int strings = seven_chords_block_strings(g_bind[i].song);
    if (strings >= 7 && strings <= SEVEN_CHORD_SLOTS && seven_chords_song_templates(g_bind[i].song, &t8, &count) &&
        t8 && (uint32_t)id < count)
      ok = merge_t8(t8 + (size_t)id * RC_OFF(seven_t8_size), s);
    break;
  }
  pthread_mutex_unlock(&g_bind_lock);
  return ok;
}

// ---- the points
// DGB: SNG_Song_PostProcess has the arrangement's attributes (looked up by its UUID): bound to the song
static void on_dgb(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  bind(PTR(f, "song"), PTR(f, "attributes"));
}

// DGM: the manifest chord's diagram is made: its Frets and Fingers (7 or 8 entries: those; 6: with the loaded song's
// T8 of its ChordId)
static void on_dgm(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  uint8_t *object = PTR(f, "object"), *frets = PTR(f, "frets"), *fingers = PTR(f, "fingers");
  if (!object) return;
  int a = array_count(frets), b = array_count(fingers);
  shape s = {.count = a < b ? a : b, .capo = (uint8_t)SEVEN_GET(f, "capo"), .manifest = 1};
  if (s.count < SEVEN_STOCK || s.count > SEVEN_CHORD_SLOTS) return;
  for (int i = 0; i < s.count; i++) {
    if (!array_number(frets, i, &s.frets[i]) || !array_number(fingers, i, &s.fingers[i])) return;
    if (s.frets[i] == 255) s.frets[i] = -1;
  }
  int six = s.count == SEVEN_STOCK;
  if (six && !from_loaded_song(PTR(f, "attributes"), PTR(f, "chord"), &s)) return;
  emit(&s, object, six ? "manifest and the song's T8" : "manifest");
}

// DGS: the loaded song's chord diagram is made from a template: a T8 (TP13 gave it), or a template of a song's own
// vector (a lesson's caller passes one directly)
static void on_dgs(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  uint8_t *holder = PTR(f, "holder"), *tpl = PTR(f, "template");
  uint8_t *object = holder ? rc_mod_ptr(holder) : NULL, *t8 = tpl ? seven_chords_as_t8(tpl) : NULL;
  if (!t8 && tpl) t8 = seven_chords_t8_of(tpl);
  shape s;
  if (object && from_t8(t8, &s)) emit(&s, object, "song");
}

// TP13: the loaded song's template of an id, for its diagram: its T8 record
static void on_tp13(rc_mid_frame *f, void *user) {
  if (!seven_on) return;
  uint8_t *t8 = seven_chords_template(PTR(f, "templates"), (uint32_t)SEVEN_GET(f, "id"));
  if (t8) SEVEN_SET(f, "template", (uintptr_t)t8);
}

// ---- the movies
#define MOVIE_LIMIT (16u << 20)  // the largest movie or recipe taken
#define RECIPE_HEAD 84           // "R7DA" 1 0 0 0, the old and new tag's size, the count of operations, two SHA-256
#define ARCHIVE "static.psarc"
#define MOVIE_DIR "rearmed-mods"  // the Mac's folder of the movies, mod_cache.c's

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"
#if defined(__has_embed)
#if __has_embed("../data/rs7string/chord-diagrams/commonlib.rs7da") && __has_embed("../data/rs7string/chord-diagrams/contentpanel.rs7da") && \
    __has_embed("../data/rs7string/chord-diagrams/ge_game.rs7da")
static const uint8_t RECIPE_COMMONLIB[] = {
#embed "../data/rs7string/chord-diagrams/commonlib.rs7da"
};
static const uint8_t RECIPE_CONTENTPANEL[] = {
#embed "../data/rs7string/chord-diagrams/contentpanel.rs7da"
};
static const uint8_t RECIPE_GE_GAME[] = {
#embed "../data/rs7string/chord-diagrams/ge_game.rs7da"
};
#else
#error "data/rs7string/chord-diagrams/*.rs7da are missing"
#endif
#else
#error "this compiler has no #embed"
#endif
#pragma clang diagnostic pop

static struct movie {
  const char *name;     // in the archive, as the game opens it
  uint16_t sprite;      // the class's sprite
  const char *path;     // Learn & Play: written there, in the game folder
  const char *game_path;  // and given to the game so
  const uint8_t *recipe;
  size_t recipe_len;
  char *guest_path;
  int built, told;
  const char *mac_path;  // the Mac: written there and given to the game as that (relative to the game folder, as
                         // mod_cache.c's archives)
} g_movies[] = {
    {"gfxassets/libs/commonlib.gfx", 1103, "Update/rearmed-mods-commonlib.gfx",
     "c:\\rocksmith2014\\update\\rearmed-mods-commonlib.gfx", NULL, 0, NULL, 0, 0, MOVIE_DIR "/commonlib.gfx"},
    {"gfxassets/views/contentpanel.gfx", 28, "Update/rearmed-mods-contentpanel.gfx",
     "c:\\rocksmith2014\\update\\rearmed-mods-contentpanel.gfx", NULL, 0, NULL, 0, 0, MOVIE_DIR "/contentpanel.gfx"},
    {"gfxassets/views/ge_game.gfx", 26, "Update/rearmed-mods-ge_game.gfx",
     "c:\\rocksmith2014\\update\\rearmed-mods-ge_game.gfx", NULL, 0, NULL, 0, 0, MOVIE_DIR "/ge_game.gfx"},
};
static int g_learn_play;  // win-x86: the path the game opens is changed (a mod can't call FrontEnd_OpenFile there)
#define NMOVIES ((int)(sizeof g_movies / sizeof *g_movies))
static pthread_once_t g_movies_once = PTHREAD_ONCE_INIT;

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static int sha_is(const uint8_t *p, size_t n, const uint8_t *want) {
  uint8_t h[CC_SHA256_DIGEST_LENGTH];
  CC_SHA256(p, (CC_LONG)n, h);
  return !memcmp(h, want, sizeof h);
}

// The one DoInitAction tag (type 59) of class sprite `sprite` in an uncompressed movie (GFX or FWS): its start and end,
// header included. 0 if the movie isn't one, is cut short, or has none or two.
static int class_tag(const uint8_t *p, size_t n, uint16_t sprite, size_t *a, size_t *b) {
  if (!p || n < 13 || n > MOVIE_LIMIT || (memcmp(p, "GFX", 3) && memcmp(p, "FWS", 3)) || le32(p + 4) != n) return 0;
  int bits = p[8] >> 3, found = 0;
  size_t pos = 8 + (5 + 4 * (size_t)bits + 7) / 8 + 4;  // the frame rectangle, the frame rate and count
  if (!bits || pos > n) return 0;
  while (pos < n) {
    size_t start = pos, len;
    if (n - pos < 2) return 0;
    uint16_t word = le16(p + pos);
    len = word & 63;
    pos += 2;
    if (len == 63) {
      if (n - pos < 4) return 0;
      len = le32(p + pos);
      pos += 4;
    }
    if (len > n - pos) return 0;
    if (word >> 6 == 59 && len >= 2 && le16(p + pos) == sprite) {
      *a = start;
      *b = pos + len;
      found++;
    }
    if (word >> 6 == 0) return len == 0 && pos == n && found == 1;  // End, the movie's last tag
    pos += len;
  }
  return 0;
}

// The movie with its class tag rebuilt from the recipe (rs7string's SwapMovie): copies of the player's own tag and the
// recipe's literals, checked against the recipe's SHA-256 of the old tag and of the new one. malloc'ed, its size in
// *out_n; NULL, with the reason in *why, when anything doesn't match.
static uint8_t *swap_movie(const uint8_t *p, size_t n, const uint8_t *r, size_t rn, uint16_t sprite, size_t *out_n,
                           const char **why) {
  size_t a, b;
  if (!r || rn < RECIPE_HEAD || rn > MOVIE_LIMIT || memcmp(r, "R7DA\1\0\0\0", 8)) return *why = "not a recipe", NULL;
  if (!class_tag(p, n, sprite, &a, &b)) return *why = "not an uncompressed movie with that class once", NULL;
  size_t old_size = le32(r + 8), new_size = le32(r + 12), count = le32(r + 16);
  if (old_size != b - a || !sha_is(p + a, b - a, r + 20)) return *why = "its class isn't the one the recipe is for", NULL;
  if (new_size > MOVIE_LIMIT || count > rn - RECIPE_HEAD || n - old_size + new_size > MOVIE_LIMIT)
    return *why = "the recipe is out of range", NULL;
  size_t total = n - old_size + new_size, made = 0, pos = RECIPE_HEAD, x, y;
  uint8_t *out = malloc(total), *tag;
  if (!out) return *why = "no memory", NULL;
  tag = out + a;
  memcpy(out, p, a);
  for (size_t i = 0; i < count; i++) {
    size_t at, len;
    if (pos >= rn) goto bad;
    uint8_t op = r[pos++];
    if (op == 0) {  // bytes of the old tag
      if (rn - pos < 8) goto bad;
      at = le32(r + pos), len = le32(r + pos + 4);
      pos += 8;
      if (!len || at > old_size || len > old_size - at || len > new_size - made) goto bad;
      memcpy(tag + made, p + a + at, len);
    } else if (op == 1) {  // the recipe's own bytes
      if (rn - pos < 4) goto bad;
      len = le32(r + pos);
      pos += 4;
      if (!len || len > rn - pos || len > new_size - made) goto bad;
      memcpy(tag + made, r + pos, len);
      pos += len;
    } else {
      goto bad;
    }
    made += len;
  }
  if (pos != rn || made != new_size || !sha_is(tag, new_size, r + 52)) goto bad;
  memcpy(tag + new_size, p + b, n - b);
  for (int i = 0; i < 4; i++) out[4 + i] = (uint8_t)(total >> (8 * i));  // the movie's length
  if (!class_tag(out, total, sprite, &x, &y) || x != a || y != a + new_size) goto bad;
  *out_n = total;
  return out;
bad:
  free(out);
  return *why = "the recipe doesn't make the class it promises", NULL;
}

// Which of the movies a path the game opens is: its name at the path's end, after a '/' or '\' or alone, in any case
// (rs7string matches the Scaleform url with or without "data/"); -1 for none
static int movie_of(const char *path) {
  char s[160];
  size_t n = strlen(path);
  if (n < 4 || n >= sizeof s || strcasecmp(path + n - 4, ".gfx")) return -1;
  for (size_t i = 0; i <= n; i++) s[i] = path[i] == '\\' ? '/' : (char)tolower((unsigned char)path[i]);
  for (int i = 0; i < NMOVIES; i++) {
    size_t k = strlen(g_movies[i].name);
    if (n >= k && !strcmp(s + n - k, g_movies[i].name) && (n == k || s[n - k - 1] == '/')) return i;
  }
  return -1;
}

static int write_file(const char *path, const uint8_t *d, size_t n) {
  char part[128];
  snprintf(part, sizeof part, "%s.part", path);
  FILE *f = fopen(part, "wb");
  int ok = f && fwrite(d, 1, n, f) == n;
  if (f && fclose(f)) ok = 0;
  if (ok && !rename(part, path)) return 1;
  unlink(part);
  return 0;
}

// The three movies with the class that draws seven rows, from the player's static.psarc, in the Update folder
static void build_movies(void) {
  uint8_t key[32];
  uint64_t hex = RC_ADDR(key_psarc_hex);
  rsm_psarc *arc = hex && rsm_psarc_key_from_hex((const char *)(uintptr_t)RC_A(hex), key) ? rsm_psarc_open(ARCHIVE, key)
                                                                                          : NULL;
  if (!arc) dlog("%s can't be read: the game's own movies (six rows)", ARCHIVE);
  mkdir(g_learn_play ? "Update" : MOVIE_DIR, 0755);
  for (int m = 0; arc && m < NMOVIES; m++) {
    struct movie *mv = &g_movies[m];
    uint8_t *data = NULL, *out = NULL;
    size_t n = 0, out_n = 0;
    const char *why = "not in the archive";
    for (int i = 0; i < rsm_psarc_files(arc) && !data; i++)
      if (!strcasecmp(rsm_psarc_name(arc, i), mv->name)) data = rsm_psarc_read(arc, i, &n);
    if (data && (out = swap_movie(data, n, mv->recipe, mv->recipe_len, mv->sprite, &out_n, &why)) && !write_file(mv->path, out, out_n))
      why = "can't be written";
    else if (out)
      mv->built = 1;
    if (mv->built)
      dlog("%s: the class with the extra rows, %zu -> %zu bytes, in %s", mv->name, n, out_n, mv->path);
    else
      dlog("%s: %s: the game's own (six rows)", mv->name, why);
    free(out);
    free(data);
  }
  if (arc) rsm_psarc_close(arc);
  for (int m = 0; m < NMOVIES; m++)
    if (!g_movies[m].built) unlink(g_movies[m].path);  // none of an earlier start's left to look current
}

// FrontEnd_OpenFile: one of the movies goes to the library's (Learn & Play: the path the game opens, as mod_cache.c
// gives its archives; a mod can't call the function there)
static int before_open(rc_ctx *c, void *user) {
  OV_ARGS(a, c);
  uint64_t out = OV_U64(a);
  const char *path = OV_PTR(a, const char);
  int m = seven_on && out && path ? movie_of(path) : -1;
  if (m < 0 || !g_movies[m].guest_path) return 0;
  pthread_once(&g_movies_once, build_movies);
  if (!g_movies[m].built) return 0;
  if (g_learn_play) {
    rc_mod_set_arg(c, 1, (uint64_t)(uintptr_t)g_movies[m].guest_path);
    if (!__atomic_exchange_n(&g_movies[m].told, 1, __ATOMIC_RELAXED)) dlog("the game opens %s", g_movies[m].path);
    return 0;
  }
  // The Mac: the game's FrontEnd_OpenFile opens the library's movie (a path relative to the game folder) and the game
  // gets that stream, as mod_cache.c gives its archives; when it fails, the game opens its own
  OV_GUEST(frontend_open_file, out, (uint64_t)(uintptr_t)g_movies[m].guest_path);
  if (!rc_mod_ptr((const void *)(uintptr_t)out)) {
    dlog("the game can't open %s: its own movie (six rows)", g_movies[m].path);
    return 0;
  }
  if (!__atomic_exchange_n(&g_movies[m].told, 1, __ATOMIC_RELAXED)) dlog("the game opens %s", g_movies[m].path);
  ov_ret_u64(c, out);
  return 1;
}

static void movies_init(void) {
  const struct {
    const uint8_t *d;
    size_t n;
  } r[] = {{RECIPE_COMMONLIB, sizeof RECIPE_COMMONLIB},
           {RECIPE_CONTENTPANEL, sizeof RECIPE_CONTENTPANEL},
           {RECIPE_GE_GAME, sizeof RECIPE_GE_GAME}};
  g_learn_play = !strcmp(rc_target(), "win-x86");
  for (int m = 0; m < NMOVIES; m++) {
    g_movies[m].recipe = r[m].d;
    g_movies[m].recipe_len = r[m].n;
    if (!g_learn_play) g_movies[m].path = g_movies[m].game_path = g_movies[m].mac_path;
    g_movies[m].guest_path = guest_text(g_movies[m].game_path);
  }
  if (rsm_hook("frontend_open_file", before_open, NULL)) dlog("no file hook: the game's own movies (six rows)");
}

int seven_diagrams_init(void) {
  for (int k = 0; k < NKEYS; k++)
    if (!(g.text[k] = guest_text(KEY_TEXT[k]))) return -1;
  g.keys = rc_guest_alloc((size_t)NKEYS * rc_guest_ptr_size);
  g.numbers = rc_guest_alloc((size_t)NNUMBERS * rc_guest_ptr_size);
  g.chord_id = guest_text(CHORD_ID);
  if (!g.keys || !g.numbers || !g.chord_id) {
    rsm_log("seven-string: diagrams: no game memory for the diagrams' names");
    return -1;
  }
  memset(g.keys, 0, (size_t)NKEYS * rc_guest_ptr_size);
  memset(g.numbers, 0, (size_t)NNUMBERS * rc_guest_ptr_size);
  static const struct {
    const char *name;
    rc_mid_fn fn;
  } points[] = {{"seven_dgb", on_dgb}, {"seven_dgm", on_dgm}, {"seven_dgs", on_dgs}, {"seven_tp13", on_tp13}};
  for (size_t i = 0; i < sizeof points / sizeof *points; i++)
    if (seven_mid(points[i].name, points[i].fn)) return -1;
  movies_init();
  return 0;
}
