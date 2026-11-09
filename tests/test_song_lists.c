// More Song Lists (src/mod_song_lists.c): the profile's song lists added and removed in its JSON, a profile written as
// the game writes one (src/rsmods_profiles.c), and the first frame on a save folder made here
#include <CommonCrypto/CommonCryptor.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#include "../src/mod_song_lists.c"
#include "check.h"

// What the game would give: the key at the address of its name (rc_slide 0, so the address is the host's)
static uint8_t g_test_key[32];
static const char *g_on = "on", *g_count = "8";
uint64_t rc_slide;
uint64_t rc_addr(const char *name) { return strcmp(name, "key_profile") ? 0 : (uint64_t)(uintptr_t)g_test_key; }
const char *rc_target(void) { return "test"; }
void rsm_log(const char *fmt, ...) { (void)fmt; }
int rsm_on(const char *key) { return !strcmp(key, "MoreSongLists") && !strcmp(g_on, "on"); }
const char *rsm_get(const char *section, const char *key) {
  return !strcmp(section, "Mod Settings") && !strcmp(key, "NumberOfSongLists") ? g_count : NULL;
}

// The lists a profile's JSON has, as the edit finds them
static int lists(const char *json) {
  int had, now;
  char *e = rsm_song_lists_edit(json, 1000, &had, &now);  // more than it can have: counted, not changed
  free(e);
  return had;
}

static uint8_t *read_all(const char *path, size_t *n) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  static uint8_t buf[1 << 16];
  *n = fread(buf, 1, sizeof buf, f);
  fclose(f);
  return buf;
}

static void write_file(const char *path, const void *data, size_t n) {
  FILE *f = fopen(path, "wb");
  fwrite(data, 1, n, f);
  fclose(f);
}

static const uint8_t HEADER[16] = {'E', 'V', 'A', 'S', 1, 0, 0, 0, 0x6b, 0x9e, 0x13, 0x02, 0, 0, 0x10, 0x01};

// A profile file of the JSON, in the save folder made here
static void write_profile_file(const char *path, const char *json) {
  size_t n;
  uint8_t *f = rsm_profiles_encode(json, HEADER, g_test_key, &n);
  write_file(path, f, n);
  free(f);
}

static char *read_profile_json(const char *path) {
  size_t n;
  uint8_t *d = read_all(path, &n);
  return d ? rsm_profiles_decode(d, n, g_test_key) : NULL;
}

int main(void) {
  for (int i = 0; i < 32; i++) g_test_key[i] = (uint8_t)(i * 5 + 1);
  int had, now;

  // As the game writes a profile: a string with brackets and the name before the lists, and a nested SongListsRoot
  const char *game =
      "{\n\"NSP_PlayListRoot2\" : {\n},\n\"Name\" : \"a ] \\\" [ } \\\"SongListsRoot\\\"\",\n"
      "\"Other\" : {\n\"SongListsRoot\" : {\n\"SongLists\" : \n[\n]\n}\n},\n"
      "\"SongListsRoot\" : {\n\"SongLists\" : \n[\n\n[\n\"Seventeen\",\n\"StonThro\"\n],\n\n[\n\"a[b]\\\"\"\n],\n\n[\n],"
      "\n\n[\n],\n\n[\n],\n\n[\n]\n]\n},\n\"UI\" : {\n},\n\"Stats\" : \n[\n],\n\"Num\" : 1.000000\n}";
  CHECK(lists(game) == 6);

  // added after the last list, as the game writes them, and removed again while empty: the same JSON
  char *eight = rsm_song_lists_edit(game, 8, &had, &now);
  CHECK(eight && had == 6 && now == 8 && lists(eight) == 8);
  const char *at = strstr(game, "\n]\n},\n\"UI\"");
  char want[2048];
  snprintf(want, sizeof want, "%.*s,\n\n[\n],\n\n[\n]%s", (int)(at - game), game, at);
  CHECK(eight && !strcmp(eight, want));
  char *back = eight ? rsm_song_lists_edit(eight, 6, &had, &now) : NULL;
  CHECK(back && had == 8 && now == 6 && !strcmp(back, game));
  free(back);
  CHECK(rsm_song_lists_edit(eight, 8, &had, &now) == NULL && had == 8 && now == 8);  // as it is
  free(eight);
  CHECK(rsm_song_lists_edit(game, 6, &had, &now) == NULL && had == 6);

  // fewer: the last lists go while they're empty, a list with songs stays (and the lists before it)
  char *four = rsm_song_lists_edit(game, 4, &had, &now);
  CHECK(four && now == 4 && lists(four) == 4 && strstr(four, "\"a[b]\\\"\""));
  free(four);
  const char *compact = "{\"SongListsRoot\":{\"SongLists\":[[\"a\"],[],[],[],[],[],[],[\"b\"],[],[ ]]},\"X\":1}";
  char *kept = rsm_song_lists_edit(compact, 6, &had, &now);
  CHECK(kept && had == 10 && now == 8 &&
        !strcmp(kept, "{\"SongListsRoot\":{\"SongLists\":[[\"a\"],[],[],[],[],[],[],[\"b\"]]},\"X\":1}"));
  CHECK(kept && rsm_song_lists_edit(kept, 6, &had, &now) == NULL && had == 8);
  free(kept);

  // none yet: the first added without a comma
  char *two = rsm_song_lists_edit("{\"SongListsRoot\":{\"SongLists\":[]}}", 2, &had, &now);
  CHECK(two && had == 0 && now == 2 && !strcmp(two, "{\"SongListsRoot\":{\"SongLists\":[\n\n[\n],\n\n[\n]]}}"));
  free(two);

  // not a profile's lists: nothing changes
  CHECK(rsm_song_lists_edit("{\"Other\":{\"SongListsRoot\":{\"SongLists\":[[]]}}}", 8, &had, &now) == NULL &&
        had == -1);
  CHECK(rsm_song_lists_edit("{\"SongListsRoot\":{\"SongLists\":[[\"a\"],\"b\"]}}", 8, &had, &now) == NULL &&
        had == -1);
  CHECK(rsm_song_lists_edit("{\"SongListsRoot\":{\"SongLists\":[[\"a\"],[\"b\"]", 8, &had, &now) == NULL &&
        had == -1);
  CHECK(rsm_song_lists_edit("{\"SongListsRoot\":{\"SongLists\":[[\"a\"]]}}", 0, &had, &now) == NULL);

  // a profile as the game writes one: the header kept, the size with the zero, whole blocks, zlib at level 9
  size_t n;
  uint8_t *file = rsm_profiles_encode(game, HEADER, g_test_key, &n);
  CHECK(file && !memcmp(file, HEADER, 16) && (n - 20) % 16 == 0);
  uint32_t size = (uint32_t)file[16] | (uint32_t)file[17] << 8 | (uint32_t)file[18] << 16 | (uint32_t)file[19] << 24;
  CHECK(size == strlen(game) + 1);
  uint8_t plain[16];
  size_t moved;
  CCCrypt(kCCDecrypt, kCCAlgorithmAES, kCCOptionECBMode, g_test_key, 32, NULL, file + 20, 16, plain, 16, &moved);
  CHECK(plain[0] == 0x78 && plain[1] == 0xda);
  char *decoded = rsm_profiles_decode(file, n, g_test_key);
  CHECK(decoded && !strcmp(decoded, game));
  free(decoded);
  free(file);

  // the first frame: every profile of the save folder, once
  char dir[] = "/tmp/rsmods-song-lists-XXXXXX", path[1024];
  CHECK(mkdtemp(dir) != NULL);
  setenv("HOME", dir, 1);  // no Steam userdata there
  unsetenv("RS_STEAM_DATA_DIR");
  unsetenv("RS_STEAM_ID");
  snprintf(path, sizeof path, "%s/game", dir);
  mkdir(path, 0755);
  CHECK(!chdir(path));
  mkdir("../steam", 0755);
  mkdir("../steam/111", 0755);
  mkdir("../steam/111/221680", 0755);
  mkdir("../steam/111/221680/remote", 0755);
  write_profile_file("../steam/111/221680/remote/LocalProfiles.json", "{\"Profiles\":[]}");
  write_profile_file("../steam/111/221680/remote/AAAA_PRFLDB", game);
  const char *full = "{\"SongListsRoot\":{\"SongLists\":[[],[],[],[],[],[],[],[],[],[\"c\"]]}}";
  write_profile_file("../steam/111/221680/remote/BBBB_PRFLDB", full);
  write_file("../steam/111/221680/remote/CCCC_PRFLDB", "not a profile", 13);
  uint8_t before[1024];
  size_t before_n;
  memcpy(before, read_all("../steam/111/221680/remote/BBBB_PRFLDB", &before_n), before_n);

  tick();
  char *a = read_profile_json("../steam/111/221680/remote/AAAA_PRFLDB");
  CHECK(a && lists(a) == 8 && !strncmp(a, game, (size_t)(at - game)));
  free(a);
  size_t bn;
  uint8_t *b = read_all("../steam/111/221680/remote/BBBB_PRFLDB", &bn);
  CHECK(b && bn == before_n && !memcmp(b, before, bn));  // its 10th list has a song: as it was
  uint8_t *c = read_all("../steam/111/221680/remote/CCCC_PRFLDB", &bn);
  CHECK(c && bn == 13 && !memcmp(c, "not a profile", 13));
  CHECK(access("../steam/111/221680/remote/.AAAA_PRFLDB.rearmed-mods", F_OK) != 0);

  g_count = "6";  // the first frame only
  tick();
  a = read_profile_json("../steam/111/221680/remote/AAAA_PRFLDB");
  CHECK(a && lists(a) == 8);
  free(a);

  snprintf(path, sizeof path, "rm -rf '%s'", dir);
  CHECK(system(path) == 0);
  return check_done("song lists");
}
