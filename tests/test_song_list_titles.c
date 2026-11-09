// Custom Song List Titles (src/mod_song_list_titles.c): the title put in the game's string, with the game's runtime
// stubbed. Built twice: the Mac's strings (8-byte pointers) and, with -DGUEST32, Learn & Play's (4-byte pointers in a
// guest window).
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "mod_song_list_titles.c"

// --- the game's side ---------------------------------------------------------------------------------------------
#ifdef GUEST32
const int rc_guest_ptr_size = 4;
enum { END_AT = 0x10, BUF_END_AT = 0x14, STR_SIZE = 0x18 };
#else
const int rc_guest_ptr_size = 8;
enum { END_AT = 0x10, BUF_END_AT = 0x18, STR_SIZE = 0x20 };
#endif
uint64_t rc_slide;
static _Alignas(16) uint8_t window[1 << 16];  // the guest's memory (Learn & Play: its pointers are offsets in it)
static size_t window_used = 0x100;

static uint8_t *game_alloc(size_t n) {
  uint8_t *p = window + window_used;
  window_used += (n + 15) & ~(size_t)15;
  return p;
}
static uint64_t ptr_at(const uint8_t *p) { return rc_guest_ptr_size == 8 ? RC_LD(uint64_t, p) : RC_LD(uint32_t, p); }
static uint8_t *host(uint64_t g) { return RC_HOST(g); }
static void set_ptr(uint8_t *p, const uint8_t *h) {
  if (rc_guest_ptr_size == 8)
    RC_ST(uint64_t, p, RC_GUEST(h));
  else
    RC_ST(uint32_t, p, (uint32_t)RC_GUEST(h));
}

static int is_inline(const uint8_t *s) { return host(ptr_at(s + BUF_END_AT)) == s + END_AT; }
static uint8_t *text_of(uint8_t *s) { return is_inline(s) ? s : host(ptr_at(s)); }
static size_t buf_size(uint8_t *s) { return (size_t)(host(ptr_at(s + BUF_END_AT)) - text_of(s)); }
static size_t len_of(uint8_t *s) { return (size_t)(host(ptr_at(s + END_AT)) - text_of(s)); }

// A string as the game makes one: inline, or with a buffer of `size` bytes
static uint8_t *new_string(const char *text, size_t size) {
  uint8_t *s = game_alloc(STR_SIZE), *data = s;
  if (size) {
    data = game_alloc(size);
    set_ptr(s, data);
    set_ptr(s + BUF_END_AT, data + size);
  } else {
    set_ptr(s + BUF_END_AT, s + END_AT);
  }
  strcpy((char *)data, text);
  set_ptr(s + END_AT, data + strlen(text));
  return s;
}

static int allocs, frees;
static size_t freed_size;
static uint8_t *freed;

int64_t rc_off(const char *name) {
  if (!strcmp(name, "efd_str_size")) return END_AT;
  if (!strcmp(name, "efd_str_inline")) return BUF_END_AT;
  return RC_NO_OFF;
}
uint64_t rc_addr(const char *name) { return !strcmp(name, "efd_alloc") || !strcmp(name, "efd_str_destroy"); }
uint64_t rc_call_name(const char *name, const uint64_t *a, int n) {
  if (!strcmp(name, "efd_alloc") && n == 2) {
    allocs++;
    return RC_GUEST(game_alloc(a[0]));
  }
  if (!strcmp(name, "efd_str_destroy") && n == 1) {  // the game's: frees a buffer, the fields stay
    uint8_t *s = (uint8_t *)(uintptr_t)a[0];
    if (!is_inline(s)) {
      frees++;
      freed = text_of(s);
      freed_size = buf_size(s);
    }
    return 0;
  }
  return 0;
}
uint64_t rc_call_name_f(const char *name, const uint64_t *a, int n, uint32_t f) { return f ? 0 : rc_call_name(name, a, n); }
const char *rc_target(void) { return "test"; }

// --- the library's side ------------------------------------------------------------------------------------------
static int on = 1;
static const char *titles[LISTS];
static double clock_now;
static rc_hook_pre_fn pre;
static rc_hook_post_fn post;

int rsm_on(const char *key) { return !strcmp(key, "CustomSongListTitles") && on; }
const char *rsm_get(const char *section, const char *key) {
  int n;
  if (strcmp(section, "SongListTitles") || sscanf(key, "SongListTitle_%d", &n) != 1 || n < 1 || n > LISTS) return NULL;
  return titles[n - 1];
}
double rsm_now(void) { return clock_now; }
void rsm_log(const char *fmt, ...) { (void)fmt; }
int rsm_hook(const char *name, rc_hook_pre_fn p, rc_hook_post_fn q) {
  if (strcmp(name, "song_list_name")) return -1;
  pre = p;
  post = q;
  return 0;
}

static void settings(void) {
  clock_now += 2;  // past the once-a-second reading
  tick();
}

// The game names list `id` into s, and answers `found`
static void name(uint8_t *s, int id, int found) {
  rc_ctx c;
  memset(&c, 0, sizeof c);
  uint64_t stack[2] = {0};
  c.r[4] = (uint64_t)(uintptr_t)stack;
  c.r[7] = 0x1234;  // the song lists
  c.r[6] = (uint64_t)(uintptr_t)s;
  c.r[2] = (uint64_t)id;
  if (pre(&c, NULL)) return;
  c.r[0] = found;
  post(&c, NULL);
}

static int holds(uint8_t *s, const char *text) {
  return len_of(s) == strlen(text) && !memcmp(text_of(s), text, strlen(text)) && text_of(s)[strlen(text)] == 0;
}

int main(void) {
#ifdef GUEST32
  rc_slide = (uint64_t)(uintptr_t)window;
#endif
  CHECK(pre && post);  // the mod's constructor has run
  titles[0] = "Metal";
  titles[1] = "Songs to learn before the summer";  // longer than an inline string's 15
  titles[2] = "Warm-ups";
  titles[3] = "Twenty bytes, nearly";
  titles[5] = "";
  settings();

  // short: in the string's own 16 bytes
  uint8_t *s = new_string("SONG LIST 1", 0);
  name(s, 2, 1);
  CHECK(is_inline(s) && holds(s, "Metal") && allocs == 0 && frees == 0);

  // longer than its buffer: a new one from the game's allocator, the old (inline) one nothing to free
  s = new_string("SONG LIST 2", 0);
  name(s, 3, 1);
  CHECK(!is_inline(s) && holds(s, titles[1]) && buf_size(s) == strlen(titles[1]) + 1);
  CHECK(allocs == 1 && frees == 0);

  // a buffer of the game's that is big enough: kept
  s = new_string("SONG LIST 3", 64);
  uint8_t *old = text_of(s);
  name(s, 4, 1);
  CHECK(holds(s, "Warm-ups") && text_of(s) == old && buf_size(s) == 64 && allocs == 1);

  // one too small: a new one, and the old one freed with its size
  s = new_string("SONG LIST 4", 12);
  old = text_of(s);
  name(s, 5, 1);
  CHECK(holds(s, titles[3]) && text_of(s) != old && allocs == 2 && frees == 1 && freed == old && freed_size == 12);

  // the game's name stays: no such list, All Songs and Favorites, no title, beyond the 20, the mod off
  s = new_string("SONG LIST 1", 0);
  name(s, 2, 0);
  CHECK(holds(s, "SONG LIST 1"));
  s = new_string("ALL SONGS", 0);
  name(s, 0, 1);
  name(s, 1, 1);
  CHECK(holds(s, "ALL SONGS"));
  s = new_string("SONG LIST 6", 0);
  name(s, 7, 1);
  CHECK(holds(s, "SONG LIST 6"));
  s = new_string("SONG LIST 5", 0);
  name(s, 6, 1);
  CHECK(holds(s, "SONG LIST 5"));
  s = new_string("SONG LIST 21", 0);
  name(s, 22, 1);
  CHECK(holds(s, "SONG LIST 21"));
  titles[19] = "The last";
  settings();
  s = new_string("SONG LIST 20", 0);
  name(s, 21, 1);
  CHECK(holds(s, "The last"));
  on = 0;
  settings();
  s = new_string("SONG LIST 1", 0);
  name(s, 2, 1);
  CHECK(holds(s, "SONG LIST 1"));

  // a title is cut at TITLE_MAX bytes, at a character
  on = 1;
  static char longer[400] = "xx";
  for (int i = 0; i < 150; i++) strcat(longer, "\xc3\xa4");  // ä: bytes 254 and 255 are one
  titles[0] = longer;
  settings();
  s = new_string("SONG LIST 1", 0);
  name(s, 2, 1);
  CHECK(len_of(s) == 254 && text_of(s)[252] == 0xc3 && text_of(s)[253] == 0xa4);

  return check_done(rc_guest_ptr_size == 8 ? "song_list_titles" : "song_list_titles (4-byte pointers)");
}
