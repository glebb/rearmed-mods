// RSMods: Custom Song List Titles (CustomSongTitles.cpp). The song lists get the player's own names, in [SongListTitles]
// SongListTitle_1 to SongListTitle_20: the profile's first six lists and the extra ones RSMods' app adds to a profile
// (Profile Edits, Add Song List). The menus that show the lists (Learn a Song's song list filter, Non-Stop Play's
// choice, ...) ask one function for a list's name, which builds it from the localized "SONG LIST" and the list's
// number. RSMods changes that function's code on Windows: the key "$[36969]SONG LIST" becomes one the game
// lacks, a hook in the missing-key path answers with the title, and the " " and the number aren't appended. Here the
// function is a hook point (song_list_name, from RS Re-ARMed 1.11.0: the Mac's 0x1005dd8d0, Learn & Play's 0x10d2470,
// where RSMods patches), and after it the mod puts the title in the name it built: in the string's own buffer when it
// fits, else in one from the game's allocator (efd_alloc), the old one freed by the game's string destructor
// (efd_str_destroy), as the game's own strings are.
//
// Different: RSMods has no switch, its titles are always on (a title not in RSMods.ini is "SONG LIST", the number
// gone); here CustomSongListTitles turns them on, and a list with no title keeps the game's name. All Songs and
// Favorites keep theirs, as in RSMods.
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "rsmods.h"

#define LISTS 20       // SongListTitle_1 .. _20, RSMods' app's
#define TITLE_MAX 255  // bytes, as RSMods (a title longer is cut at a character)
#define FIRST_LIST 2   // the name's id: 0 All Songs, 1 Favorites, 2 the first list

static atomic_int g_on;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static char g_titles[LISTS][TITLE_MAX + 1];  // "" = the game's name
static double g_read_at = -1;

static size_t utf8_cut(const char *s, size_t max) {
  size_t n = strlen(s);
  if (n <= max) return n;
  while (max > 0 && ((unsigned char)s[max] & 0xc0) == 0x80) max--;  // not inside a character
  return max;
}

// The titles, from RSMods.ini, at most once a second (it is re-read no more often)
static void tick(void) {
  int on = rsm_on("CustomSongListTitles");
  if (on != atomic_load(&g_on)) rsm_log("song list titles %s", on ? "on" : "off");
  atomic_store(&g_on, on);
  double now = rsm_now();
  if (!on) g_read_at = -1;  // read again when it is turned on
  if (!on || (g_read_at >= 0 && now - g_read_at < 1)) return;
  g_read_at = now;
  char titles[LISTS][TITLE_MAX + 1];
  for (int i = 0; i < LISTS; i++) {
    char key[32];
    snprintf(key, sizeof key, "SongListTitle_%d", i + 1);
    const char *t = rsm_get("SongListTitles", key);
    size_t n = t ? utf8_cut(t, TITLE_MAX) : 0;
    memcpy(titles[i], t ? t : "", n);
    titles[i][n] = 0;
  }
  pthread_mutex_lock(&g_lock);
  memcpy(g_titles, titles, sizeof titles);
  pthread_mutex_unlock(&g_lock);
}

// A guest pointer field: the game's width
static void put_ptr(uint8_t *field, const uint8_t *host) {
  if (rc_guest_ptr_size == 8)
    RC_ST(uint64_t, field, RC_GUEST(host));
  else
    RC_ST(uint32_t, field, (uint32_t)RC_GUEST(host));
}

// Makes the efd string at s hold text (n bytes). Its text is inline (16 bytes at its start) while the word at
// efd_str_inline is the address of the one at efd_str_size, else at the address at its start; efd_str_size is where the
// text ends, efd_str_inline where its buffer does.
static int put_text(uint8_t *s, const char *text, size_t n) {
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

// The call under way: the name it builds and the list's title (pre -> post hook)
static __thread struct {
  uint8_t *out;
  int list;  // 0: none
} t_call;

static int before_name(rc_ctx *c, void *user) {
  (void)user;
  t_call.out = NULL;
  if (!atomic_load(&g_on)) return 0;
  OV_ARGS(a, c);
  (void)OV_PTR(a, uint8_t);  // the song lists
  uint8_t *out = OV_PTR(a, uint8_t);
  int id = OV_I32(a);
  if (!out || id < FIRST_LIST || id >= FIRST_LIST + LISTS) return 0;
  t_call.out = out;
  t_call.list = id - FIRST_LIST + 1;
  return 0;
}

static void after_name(rc_ctx *c, void *user) {
  (void)user;
  uint8_t *out = t_call.out;
  int list = t_call.list;
  t_call.out = NULL;
  if (!out || !(uint8_t)c->r[0]) return;  // false: the profile hasn't that list, the name is empty
  char title[TITLE_MAX + 1];
  pthread_mutex_lock(&g_lock);
  memcpy(title, g_titles[list - 1], sizeof title);
  pthread_mutex_unlock(&g_lock);
  if (!*title) return;
  if (put_text(out, title, strlen(title)))
    rsm_log("song list %d: \"%s\"", list, title);
  else
    rsm_log("song list %d: its name couldn't be changed", list);
}

__attribute__((constructor)) static void init(void) {
  if (!RC_ADDR(efd_str_destroy) || !RC_ADDR(efd_alloc) || !RC_HAS_OFF(efd_str_size) || !RC_HAS_OFF(efd_str_inline)) {
    fprintf(stderr, "[rsmods] the song lists' names aren't known on %s: Custom Song List Titles needs RS Re-ARMed 1.11.0\n",
            rc_target());
    return;
  }
  rsm_hook("song_list_name", before_name, after_name);
}

const rsm_mod rsm_mod_song_list_titles = {"song_list_titles", tick, NULL, NULL, NULL};
