// RSMods: More Song Lists (its app's Profile Edits: Add New SongList and Remove Newest SongList, UI.cs). A profile keeps
// its song lists in SongListsRoot.SongLists, an array of the lists' song keys; the game makes six. Most of the game sizes
// itself from that array (RSMods' ExtraSongLists.hpp): Learn a Song's and Song Arcade's filters, the song list editor
// and adding songs to a list, and the names (song_list_name, Custom Song List Titles). RSMods' app adds an empty list
// to the profile chosen, or removes the newest, from 6 up to 20.
//
// Here the library does it, as the app can't read the profiles (they are encrypted with the game's key). With
// MoreSongLists on, every profile of the save folder in use (rsmods_profiles.c) gets [Mod Settings] NumberOfSongLists
// lists (6 to 20) on the game's first frame, before the game reads a profile: empty lists are added after the last;
// lists past the number are removed from the end while they're empty, and a list with songs stays (RSMods' app
// removes the newest whatever it has). A profile is written only when it changes, as the game writes one
// (rsm_profiles_encode), and only once the new file decodes to the edited JSON: to a new file in the save folder,
// renamed over the profile. The game then has the lists, and saves them as its own. Off, nothing is changed: the
// lists stay as they are. Non-Stop Play offers the first six only (README: Not on macOS yet).
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "rsmods.h"

#define MIN_LISTS 6
#define MAX_LISTS 20
#define MAX_PROFILES 64

static uint8_t *read_profile(const char *path, size_t *n) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  uint8_t *d = NULL;
  if (!fseek(f, 0, SEEK_END)) {
    long len = ftell(f);
    rewind(f);
    if (len > 20 && len < (64l << 20) && (d = malloc((size_t)len)) && fread(d, 1, (size_t)len, f) != (size_t)len) {
      free(d);
      d = NULL;
    }
    if (d) *n = (size_t)len;
  }
  fclose(f);
  return d;
}

// To a new file, then renamed over the profile: the game may stop at any moment
static int write_profile(const char *folder, const char *name, const uint8_t *data, size_t n) {
  char path[1300], part[1400];
  snprintf(path, sizeof path, "%s/%s", folder, name);
  snprintf(part, sizeof part, "%s/.%s.rearmed-mods", folder, name);
  FILE *f = fopen(part, "wb");
  if (!f) return 0;
  int ok = fwrite(data, 1, n, f) == n;
  ok = !fclose(f) && ok;
  if (ok && !rename(part, path)) return 1;
  unlink(part);
  return 0;
}

static void apply(const char *folder, const char *name, const uint8_t key[32], int want) {
  char path[1300];
  snprintf(path, sizeof path, "%s/%s", folder, name);
  size_t n = 0, m = 0;
  uint8_t *data = read_profile(path, &n), *out = NULL;
  char *json = data ? rsm_profiles_decode(data, n, key) : NULL, *edited = NULL, *back = NULL;
  int had, now;
  if (!json) {
    rsm_log("more song lists: %s can't be read", name);
    goto done;
  }
  if (!(edited = rsm_song_lists_edit(json, want, &had, &now))) {
    if (had < 0)
      rsm_log("more song lists: %s has no song lists", name);
    else if (had > want)
      rsm_log("more song lists: %s keeps %d song lists: the lists after %d have songs", name, had, want);
    goto done;
  }
  out = rsm_profiles_encode(edited, data, key, &m);
  back = out ? rsm_profiles_decode(out, m, key) : NULL;
  if (!back || strcmp(back, edited) || !write_profile(folder, name, out, m)) {
    fprintf(stderr, "[rsmods] more song lists: couldn't write %s/%s: it stays as it was\n", folder, name);
    goto done;
  }
  if (now != want)
    rsm_log("more song lists: %s has %d song lists (was %d): the lists after %d have songs", name, now, had, want);
  else
    rsm_log("more song lists: %s has %d song lists (was %d)", name, now, had);
done:
  free(back);
  free(out);
  free(edited);
  free(json);
  free(data);
}

static void tick(void) {
  static int done;
  if (done) return;
  done = 1;  // on the first frame only: before the game reads a profile, which it then saves with the lists
  if (!rsm_on("MoreSongLists")) return;
  const char *v = rsm_get("Mod Settings", "NumberOfSongLists");
  int want = v ? atoi(v) : MIN_LISTS;
  want = want < MIN_LISTS ? MIN_LISTS : want > MAX_LISTS ? MAX_LISTS : want;
  uint64_t a = RC_ADDR(key_profile);
  if (!a) {
    fprintf(stderr, "[rsmods] more song lists: the profiles' key isn't known on %s: the lists stay as they are\n",
            rc_target());
    return;
  }
  char folder[1024];
  unsigned long long account;
  if (!rsm_save_folder(folder, sizeof folder, &account)) {
    rsm_log("more song lists: no save folder found");
    return;
  }
  static char names[MAX_PROFILES][256];  // listed first: the folder changes as they are written
  int np = 0;
  DIR *d = opendir(folder);
  for (struct dirent *e; d && (e = readdir(d)) && np < MAX_PROFILES;) {
    size_t len = strlen(e->d_name);
    if (len > 7 && len < sizeof names[0] && e->d_name[0] != '.' && !strcmp(e->d_name + len - 7, "_PRFLDB"))
      snprintf(names[np++], sizeof names[0], "%s", e->d_name);
  }
  if (d) closedir(d);
  for (int i = 0; i < np; i++) apply(folder, names[i], (const uint8_t *)(uintptr_t)RC_A(a), want);
}

const rsm_mod rsm_mod_song_lists = {"song_lists", tick, NULL, NULL, NULL};
