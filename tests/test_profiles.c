// The profiles (src/rsmods_profiles.c): LocalProfiles.json made here with a key of its own, the save folder of the
// account the launcher names, and the profile backups (src/mod_profile_backups.c) in a game folder made here
#include <CommonCrypto/CommonCryptor.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>
#include <zlib.h>

#include "../src/mod_profile_backups.c"
#include "check.h"

// What the game would give: the key at the address of its name (rc_slide 0, so the address is the host's)
static uint8_t g_test_key[32];
uint64_t rc_slide;
uint64_t rc_addr(const char *name) { return strcmp(name, "key_profile") ? 0 : (uint64_t)(uintptr_t)g_test_key; }
void rsm_log(const char *fmt, ...) { (void)fmt; }
int rsm_on_section(const char *section, const char *key) { (void)section, (void)key; return 1; }

// "EVAS", a header with the plain size last, AES-256-ECB of zlib data padded with zeros
static size_t make_profiles(const char *json, const uint8_t key[32], uint8_t *out) {
  uint8_t z[4096] = {0};
  uLongf zn = sizeof z;
  compress2(z, &zn, (const Bytef *)json, (uLong)strlen(json), 9);
  size_t whole = (zn + 15) & ~(size_t)15, moved = 0;
  memcpy(out, "EVAS\1\0\0\0", 8);
  memset(out + 8, 0, 8);
  uint32_t plain = (uint32_t)strlen(json);
  memcpy(out + 16, &plain, 4);  // little endian
  CCCrypt(kCCEncrypt, kCCAlgorithmAES, kCCOptionECBMode, key, 32, NULL, z, whole, out + 20, whole, &moved);
  return 20 + whole;  // whole blocks, as the game's are (the plain size in the header says where the data ends)
}

static void write_file(const char *path, const void *data, size_t n) {
  FILE *f = fopen(path, "wb");
  fwrite(data, 1, n, f);
  fclose(f);
}

static int count_dirs(const char *path) {
  int n = 0;
  DIR *d = opendir(path);
  for (struct dirent *e; d && (e = readdir(d));) n += e->d_name[0] != '.';
  if (d) closedir(d);
  return n;
}

static void touch(const char *path, time_t t) {
  struct utimbuf u = {t, t};
  utime(path, &u);
}

int main(void) {
  for (int i = 0; i < 32; i++) g_test_key[i] = (uint8_t)(i * 7 + 3);
  const char *json = "{\n\"Profiles\" : \n[\n{\n\"PlayerName\" : \"Bodhi\",\n\"UniqueID\" : \"C0A3\",\n"
                     "\"LastModified\" : 1794163806.000000\n},\n{\n\"PlayerName\" : \"Sam \\\"Two\\\"\",\n"
                     "\"UniqueID\" : \"DFB4\",\n\"LastModified\" : 1793997525.000000\n},\n"
                     "{\"PlayerName\":\"inka\",\"UniqueID\":\"BB89\"}\n]\n}";
  uint8_t file[8192];
  size_t n = make_profiles(json, g_test_key, file);

  // decoded with the key; not with another, nor a file that isn't one
  char *got = rsm_profiles_decode(file, n, g_test_key);
  CHECK(got && !strcmp(got, json));
  free(got);
  uint8_t other[32];
  memcpy(other, g_test_key, 32);
  other[0] ^= 0xff;
  CHECK(rsm_profiles_decode(file, n, other) == NULL);
  CHECK(rsm_profiles_decode((const uint8_t *)"PRFL and more bytes than a header", 33, g_test_key) == NULL);

  char names[8][64];
  CHECK(rsm_profiles_names(json, names, 8) == 3);
  CHECK(!strcmp(names[0], "Bodhi") && !strcmp(names[1], "Sam \"Two\"") && !strcmp(names[2], "inka"));
  CHECK(rsm_profiles_names(json, names, 1) == 1);

  // the save folder: the account the launcher gives, else the folder written last
  char dir[] = "/tmp/rsmods-profiles-XXXXXX", path[1024], folder[1024];
  CHECK(mkdtemp(dir) != NULL);
  setenv("HOME", dir, 1);  // no Steam userdata there
  unsetenv("RS_STEAM_DATA_DIR");
  snprintf(path, sizeof path, "%s/game", dir);
  mkdir(path, 0755);
  CHECK(!chdir(path));
  unsigned long long account = 0;
  unsetenv("RS_STEAM_ID");
  CHECK(!rsm_save_folder(folder, sizeof folder, &account));
  const unsigned long long ids[] = {111, 222};
  for (int i = 0; i < 2; i++) {
    snprintf(path, sizeof path, "../steam/%llu/221680/remote", ids[i]);
    make_dirs(path);
    snprintf(path, sizeof path, "../steam/%llu/221680/remote/LocalProfiles.json", ids[i]);
    write_file(path, file, n);
    touch(path, 1700000000 + i * 100);
    snprintf(path, sizeof path, "../steam/%llu/221680/remote/ABCD_PRFLDB", ids[i]);
    write_file(path, "profile", 7);
  }
  CHECK(rsm_save_folder(folder, sizeof folder, &account) && account == 222 &&
        !strcmp(folder, "../steam/222/221680/remote"));
  setenv("RS_STEAM_ID", "76561197960265839", 1);  // account 111
  CHECK(rsm_save_folder(folder, sizeof folder, &account) && account == 111);
  char *listed = rsm_profiles_json(folder);
  CHECK(listed && !strcmp(listed, json));
  free(listed);

  // the backups: a first one in every tier, the files shared by hard links
  backup_if_due();
  CHECK(count_dirs(ROOT "/111/Every 10 Minutes") == 1 && count_dirs(ROOT "/111/Hourly") == 1 &&
        count_dirs(ROOT "/111/Quarter Daily") == 1 && count_dirs(ROOT "/111/Daily") == 1);
  static backup b[MAX_BACKUPS];
  CHECK(list_backups(ROOT "/111/Every 10 Minutes", b, MAX_BACKUPS) == 1);
  struct stat original, copied, linked;
  char name[32];
  snprintf(name, sizeof name, "%s", strrchr(b[0].path, '/') + 1);
  snprintf(path, sizeof path, "%s/ABCD_PRFLDB", b[0].path);
  CHECK(!stat("../steam/111/221680/remote/ABCD_PRFLDB", &original) && !stat(path, &copied));
  CHECK(copied.st_size == 7 && copied.st_mtimespec.tv_sec == original.st_mtimespec.tv_sec &&
        copied.st_mtimespec.tv_nsec == original.st_mtimespec.tv_nsec);
  snprintf(path, sizeof path, ROOT "/111/Daily/%s/ABCD_PRFLDB", name);
  CHECK(!stat(path, &linked) && linked.st_ino == copied.st_ino);
  CHECK(!access(ROOT "/111/howto.txt", R_OK));

  // none again in the same 10 minutes, nor for profiles that didn't change
  backup_if_due();
  CHECK(count_dirs(ROOT "/111/Every 10 Minutes") == 1);

  // old ones beyond a tier's count go when a new one comes; an unfinished copy is cleared out
  long long now = (long long)time(NULL);
  for (int i = 1; i <= 7; i++) {
    char old[32];
    name_for(now - 3600LL * 24 * i, old, sizeof old);
    snprintf(path, sizeof path, ROOT "/111/Every 10 Minutes/%s", old);
    mkdir(path, 0755);
  }
  mkdir(ROOT "/111/Every 10 Minutes/.partial_2020-01-01_00-00-00", 0755);
  mkdir(ROOT "/111/Every 10 Minutes/not a backup", 0755);
  remove_tree(b[0].path);  // the newest is now days old: due
  backup_if_due();
  CHECK(list_backups(ROOT "/111/Every 10 Minutes", b, MAX_BACKUPS) == 6);
  CHECK(access(ROOT "/111/Every 10 Minutes/.partial_2020-01-01_00-00-00", F_OK) != 0);
  CHECK(!access(ROOT "/111/Every 10 Minutes/not a backup", F_OK));  // left alone
  CHECK(b[5].time >= now - 5);  // the new one kept

  long long t;
  CHECK(time_of("2026-11-08_20-07-05", &t) && !time_of("2026-11-08_20-07-05x", &t) && !time_of("2026-13-08_20-07-05", &t));

  return check_done("rsmods_profiles");
}
