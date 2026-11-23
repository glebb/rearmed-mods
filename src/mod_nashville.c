// Nashville Numbers (NashvilleNumbers = on): in a song, the chord of the moment as a numeral of the song's key, "IV"
// or "♭VII" ([Mod Settings] NashvilleStyle = roman), or "4", "♭7" (numbers), the next chord and the key under it;
// right of the middle, where Show Current Note is on the left. Not an RSMods feature.
//
// The song's key is guessed from the notes of all its arrangements (song_key.h): when the game has loaded a song
// (seven_pp1, as Tone Host), a thread reads every arrangement's SNG from the song's archive (song_sng.h; a Rocksmith
// 1 song's from the compatibility pack). NashvilleChords = song (the default): the song's harmony through the whole
// song, from all its parts over its beat grid (song_harmony.h), whatever the part played plays, a riff or a solo.
// charted: the chords of the arrangement played (the one whose levels have as many notes as the loaded song's), named
// from their notes, each while it rings and a moment after. [Keybinds] NashvilleKeyKey (Q) gives the song the next of the
// guessed keys, written to [Nashville Keys] as <SongKey> = <key> ("D", "F#m", "Bb"); Control + the key goes back to
// the guess. NashvilleMinor = relative counts a minor key from its relative major (Am: C's 6m) and not from 1m.
// NashvilleDetail = simple (the default) shows a chord as a chart writes it, full as its notes name it.
//
// Pitch Shifter asks which arrangement is played, for its tuning (rsm_song_played): the thread reads a loaded song
// while either wants it.
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "rsmods.h"
#include "rsmods_files.h"
#include "song_harmony.h"
#include "song_key.h"
#include "song_sng.h"
#include "stems_index.h"

enum { MAX_LEVELS = 64, CANDIDATES = 4 };
static const double HOLD = 1.5, EARLY = 0.03;  // a chord shows this long after it ends, and this early

typedef struct {
  float time, end;
  sk_chord chord;
} chord_at;

// What the thread found for a song, under g_lock
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_wake = PTHREAD_COND_INITIALIZER;
static struct {
  char song[160];
  sk_key keys[CANDIDATES];
  int nkeys;
  chord_at *chords;  // the arrangement played's
  int nchords;
  sh_chord *harmony;  // the song's
  int nharmony;
  rsm_arrangement played;  // the arrangement played's manifest,
  unsigned load;           // of which load of a song (g_loaded then); 0: it wasn't found
} g_found;
static atomic_llong g_asked = -100000;  // when another mod last asked for the arrangement played, in ms
// The loaded song, from the game: its levels' note counts
static int g_levels, g_level_notes[MAX_LEVELS];
static unsigned g_loaded, g_done;
static int g_song_value = -1, g_on;
// A key set with the key in this game, until the settings say it too
static char g_set_song[160], g_set_key[16];

static size_t vector_of(const uint8_t *at, const uint8_t **first) {
  const uint8_t *b = rc_mod_ptr(at + RC_OFF(std_vector_begin)), *e = rc_mod_ptr(at + RC_OFF(std_vector_end));
  *first = b;
  return b && e > b ? (size_t)(e - b) : 0;
}

static void song_loaded(rc_mid_frame *f, void *user) {
  (void)user;
  const uint8_t *song = (const uint8_t *)(uintptr_t)rc_mid_get(f, g_song_value), *levels;
  if (!song || !(g_on || rsm_now() * 1000 - (double)atomic_load(&g_asked) < 2000)) return;
  size_t bytes = vector_of(song + RC_OFF(sng_song_levels), &levels), size = (size_t)RC_OFF(sng_level_size);
  if (!bytes || !size) return;  // the vocals
  pthread_mutex_lock(&g_lock);
  g_levels = (int)(bytes / size);
  if (g_levels > MAX_LEVELS) g_levels = MAX_LEVELS;
  for (int l = 0; l < g_levels; l++) {
    const uint8_t *notes;
    g_level_notes[l] = (int)(vector_of(levels + l * size + RC_OFF(sng_level_notes), &notes) / (size_t)RC_OFF(sng_note_size));
  }
  g_loaded++;
  pthread_cond_signal(&g_wake);
  pthread_mutex_unlock(&g_lock);
}

// The song's arrangements, from its archive; a Rocksmith 1 DLC song's archive has its audio alone
static int read_song(const char *song, const uint8_t key[32], sng_arrangement **out) {
  char path[2048] = "";
  static double listed_at;
  if (stems_index_listed() < 0) stems_index_build(key), listed_at = rsm_now();
  if (!stems_index_path(song, path, sizeof path) && rsm_now() - listed_at >= 60) {
    stems_index_build(key), listed_at = rsm_now();  // a song added since
    stems_index_path(song, path, sizeof path);
  }
  const char *const archives[] = {path, "dlc/rs1compatibilitydlc_m.psarc", "dlc/rs1compatibilitydlc_p.psarc",
                                  "dlc/rs1compatibilitydisc_m.psarc", "dlc/rs1compatibilitydisc_p.psarc"};
  for (size_t i = 0; i < sizeof archives / sizeof *archives; i++) {
    if (!*archives[i]) continue;
    int n = sng_read_song(archives[i], song, key, &rsm_file_io, out);
    if (n) return n;
  }
  return 0;
}

static void find(const char *song, const uint8_t key[32], unsigned load, int levels, const int *level_notes) {
  sng_arrangement *arr = NULL;
  int n = read_song(song, key, &arr), match = -1;
  sk_stats stats;
  sk_stats_init(&stats);
  for (int i = 0; i < n; i++) {
    sk_stats_add(&stats, arr[i].ev, arr[i].n);
    int same = arr[i].levels == levels;
    for (int l = 0; same && l < levels; l++) same = arr[i].level_notes[l] == level_notes[l];
    if (same && match < 0) match = i;
  }
  sk_key keys[CANDIDATES];
  int nkeys = sk_guess(&stats, keys, CANDIDATES), nchords = 0;
  chord_at *chords = NULL;
  if (match >= 0) {
    chords = malloc((size_t)(arr[match].n ? arr[match].n : 1) * sizeof *chords);
    for (int i = 0; chords && i < arr[match].n; i++) {
      const sk_event *e = &arr[match].ev[i];
      if (e->n < 2 || !sk_chord_of(e->midi, e->n, &chords[nchords].chord)) continue;
      chords[nchords].time = e->time;
      chords[nchords++].end = e->time + (e->sustain > 0 ? e->sustain : 0);
    }
  }
  // the harmony, over the beat grid of the arrangement played (they're one song's: the same)
  sh_part parts[16];
  int np = 0, grid = match >= 0 ? match : 0, nharmony = 0;
  for (int i = 0; i < n && np < 16; i++) parts[np++] = (sh_part){arr[i].ev, arr[i].n, arr[i].bass};
  sh_chord *harmony = NULL;
  if (nkeys) nharmony = sh_infer(parts, np, arr[grid].beats, arr[grid].nbeats, &keys[0], &harmony);
  char name[16] = "?";
  if (nkeys) sk_key_name(&keys[0], name, sizeof name);
  fprintf(stderr, "[rsmods] nashville numbers: %s: %d arrangements, key %s, %s (%d chords), the song's harmony %d chords\n", song, n,
          name, match >= 0 ? arr[match].name : "the loaded one not found", nchords, nharmony);
  rsm_arrangement played = {.name = ""};
  if (match >= 0) played = arr[match].manifest;
  sng_free_all(arr, n);
  pthread_mutex_lock(&g_lock);
  g_found.played = played;
  g_found.load = match >= 0 ? load : 0;
  free(g_found.chords);
  free(g_found.harmony);
  g_found.harmony = harmony;
  g_found.nharmony = nharmony;
  snprintf(g_found.song, sizeof g_found.song, "%s", song);
  memcpy(g_found.keys, keys, sizeof keys);
  g_found.nkeys = nkeys;
  g_found.chords = chords;
  g_found.nchords = nchords;
  pthread_mutex_unlock(&g_lock);
}

static void *finder(void *arg) {
  (void)arg;
  static uint8_t key[32];
  for (int have_key = 0;;) {
    pthread_mutex_lock(&g_lock);
    while (g_done == g_loaded) pthread_cond_wait(&g_wake, &g_lock);
    unsigned load = g_done = g_loaded;
    int levels = g_levels, level_notes[MAX_LEVELS];
    memcpy(level_notes, g_level_notes, sizeof level_notes);
    pthread_mutex_unlock(&g_lock);
    if (!have_key) {  // read once a song has loaded: the game's memory isn't there yet when the mods load
      uint64_t hex = RC_ADDR(key_psarc_hex);
      have_key = hex && rsm_psarc_key_from_hex((const char *)(uintptr_t)RC_A(hex), key);
      if (!have_key) {
        fprintf(stderr, "[rsmods] nashville numbers: the archives' key isn't known on %s\n", rc_target());
        return NULL;
      }
    }
    char song[160];
    snprintf(song, sizeof song, "%s", rsm_song_key());
    if (*song) find(song, key, load, levels, level_notes);
  }
  return NULL;
}

int rsm_song_played(const char *song, rsm_arrangement *out) {
  atomic_store(&g_asked, (long long)(rsm_now() * 1000));
  pthread_mutex_lock(&g_lock);
  // not while a later load is still being read: that may be another arrangement of the song
  int have = g_found.load && g_found.load == g_loaded && *song && !strcasecmp(g_found.song, song);
  if (have) *out = g_found.played;
  unsigned load = g_loaded;
  pthread_mutex_unlock(&g_lock);
  return have ? (int)(load & 0x7fffffff) | 1 << 30 : 0;
}

// The song's key: one set with the key or in [Nashville Keys], else the best guess. 1 if it was set.
static int song_key_of(const char *song, sk_key *k) {
  if (*g_set_song && !strcasecmp(g_set_song, song)) {
    if (!*g_set_key) return *k = g_found.keys[0], 0;
    if (sk_key_parse(g_set_key, k)) return 1;
  }
  const char *v = rsm_get("Nashville Keys", song);
  if (v && sk_key_parse(v, k)) return 1;
  *k = g_found.keys[0];
  return 0;
}

static void tick(void) {
  g_on = rsm_on("NashvilleNumbers");
  if (!g_on || !rsm_menu_in(RSM_SONG_MODES)) return;
  double now = rsm_song_time();
  const char *song = rsm_song_key();
  pthread_mutex_lock(&g_lock);
  if (now < 0 || !g_found.nkeys || strcasecmp(g_found.song, song)) {
    pthread_mutex_unlock(&g_lock);
    return;
  }
  sk_key k;
  int set = song_key_of(song, &k);
  const char *style = rsm_get("Mod Settings", "NashvilleStyle"), *minor = rsm_get("Mod Settings", "NashvilleMinor"),
             *detail = rsm_get("Mod Settings", "NashvilleDetail"), *which = rsm_get("Mod Settings", "NashvilleChords");
  int roman = !style || strcasecmp(style, "numbers"), relative = minor && !strcasecmp(minor, "relative"),
      simple = !detail || strcasecmp(detail, "full"), charted = which && !strcasecmp(which, "charted");
  char text[32] = "", next[32] = "";
  if (charted) {
    // the last chord that started
    int lo = 0, hi = g_found.nchords - 1, at = -1;
    while (lo <= hi) {
      int mid = (lo + hi) / 2;
      if (g_found.chords[mid].time <= now + EARLY) at = mid, lo = mid + 1;
      else hi = mid - 1;
    }
    if (at >= 0 && now <= g_found.chords[at].end + HOLD) sk_numeral(&g_found.chords[at].chord, &k, roman, relative, simple, text, sizeof text);
  } else {
    // the song's chord now, and the next that reads differently
    int lo = 0, hi = g_found.nharmony - 1, at = -1;
    while (lo <= hi) {
      int mid = (lo + hi) / 2;
      if (g_found.harmony[mid].start <= now + EARLY) at = mid, lo = mid + 1;
      else hi = mid - 1;
    }
    if (at >= 0 && now < g_found.harmony[at].end) sk_numeral(&g_found.harmony[at].chord, &k, roman, relative, simple, text, sizeof text);
    for (int i = at + 1; i < g_found.nharmony; i++) {
      sk_numeral(&g_found.harmony[i].chord, &k, roman, relative, simple, next, sizeof next);
      if (strcmp(next, text)) break;
      *next = 0;
    }
  }
  pthread_mutex_unlock(&g_lock);

  float x = 0.5f + 349.f / 1920, y = 617.f / 1080;
  if (*text) rsm_text(text, x, y, 0.045f, RSM_ALIGN_CENTER, 0xffffffff);
  if (*next) {
    char line[48];
    snprintf(line, sizeof line, "next: %s", next);
    rsm_text(line, x, y + 64.f / 1080, 0.024f, RSM_ALIGN_CENTER, 0xffffffd0);
  }
  char name[16], label[48];
  sk_key_name(&k, name, sizeof name);
  snprintf(label, sizeof label, "Key: %s%s", name, set ? " (set)" : "");
  rsm_text(label, x, y + (charted ? 68.f : 100.f) / 1080, 0.02f, RSM_ALIGN_CENTER, 0xffffffb0);
}

// The key gives the song the next of the guessed keys; Control + the key, the guess again
static void key(const rsm_key_event *e) {
  if (e->down || e->keycode != rsm_key("NashvilleKeyKey") || !rsm_on("NashvilleNumbers") || !rsm_menu_in(RSM_SONG_MODES)) return;
  const char *song = rsm_song_key();
  pthread_mutex_lock(&g_lock);
  if (!g_found.nkeys || strcasecmp(g_found.song, song)) {
    pthread_mutex_unlock(&g_lock);
    return;
  }
  char value[16] = "";
  if (!e->ctrl) {
    sk_key k;
    int set = song_key_of(song, &k), next = 0;
    for (int i = 0; set && i < g_found.nkeys; i++)
      if (g_found.keys[i].tonic == k.tonic) next = i + 1;
    if (next >= g_found.nkeys) next = 0;
    sk_key_name(&g_found.keys[next], value, sizeof value);
    char *flat = strstr(value, "\xe2\x99\xad");  // B♭ as Bb in the file
    if (flat) memmove(flat + 1, flat + 3, strlen(flat + 3) + 1), *flat = 'b';
  }
  snprintf(g_set_song, sizeof g_set_song, "%s", song);
  snprintf(g_set_key, sizeof g_set_key, "%s", value);
  pthread_mutex_unlock(&g_lock);
  rsm_set("Nashville Keys", song, value);
}

__attribute__((constructor)) static void init(void) {
  int point = RC_HAS_OFF(sng_song_levels) && RC_HAS_OFF(sng_level_size) && RC_HAS_OFF(sng_level_notes) && RC_HAS_OFF(sng_note_size)
                  ? rc_mid_add_name("seven_pp1", song_loaded, NULL)
                  : -1;
  if (point >= 0 && (g_song_value = rc_mid_value(point, "song")) >= 0) {
    pthread_t t;
    if (!pthread_create(&t, NULL, finder, NULL)) pthread_detach(t);
  } else
    fprintf(stderr, "[rsmods] nashville numbers: the loaded song isn't known on %s: Nashville Numbers needs RS Re-ARMed 1.13.0 (Learn & Play 1.12.0)\n", rc_target());
}

const rsm_mod rsm_mod_nashville = {"nashville", tick, key, NULL, NULL};
