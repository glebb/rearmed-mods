// RSMods: profile backups (ProfileBackups.cpp; [GUI Settings] BackupProfile, on unless set off, as in RSMods). While the
// game runs, the save folder (the profiles and LocalProfiles.json, rsmods_profiles.c) is copied to
// Profile_AutoBackups/<account>/<tier>/<UTC time> in the game folder, at most once every 10 minutes and only when
// something in it changed. Each tier keeps its newest: Every 10 Minutes 6, Hourly 6, Quarter Daily 4, Daily 30. A new
// backup goes into each tier that has none for its period yet, as hard links to the same files; a file that didn't
// change since the last backup is a hard link to that one's copy. A copy is made in a ".partial_" folder and renamed when
// it is whole, and thrown away if the folder changed meanwhile (the game saved). The same folders and names as RSMods,
// so its app can restore them. RSMods' app adds a "Before GUI" tier when it opens; that one is the Mac app's.
//
// It runs on a thread of its own, every 30 seconds: copying is file work the game's frame shouldn't wait for.
#include <copyfile.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "rsmods.h"

#define ROOT "Profile_AutoBackups"
#define PARTIAL ".partial_"
#define MAX_FILES 64
#define MAX_BACKUPS 128

static const struct {
  const char *folder;
  long long seconds;  // one period
  int keep;
} TIERS[] = {
    {"Every 10 Minutes", 10 * 60, 6},
    {"Hourly", 60 * 60, 6},
    {"Quarter Daily", 6 * 60 * 60, 4},
    {"Daily", 24 * 60 * 60, 30},
};
#define NTIERS ((int)(sizeof TIERS / sizeof TIERS[0]))

typedef struct {
  char name[256];
  long long size;
  struct timespec written;
} file_state;
typedef struct {
  int n;
  file_state f[MAX_FILES];
} folder_state;

static atomic_int g_on;

static int cmp_name(const void *a, const void *b) {
  return strcmp(((const file_state *)a)->name, ((const file_state *)b)->name);
}

// The regular files of a folder, by name; 0 if it can't be read or has too many
static int read_folder(const char *dir, folder_state *s) {
  s->n = 0;
  DIR *d = opendir(dir);
  if (!d) return 0;
  int ok = 1;
  for (struct dirent *e; (e = readdir(d));) {
    if (e->d_name[0] == '.') continue;
    char path[2048];
    struct stat st;
    snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
    if (lstat(path, &st) || !S_ISREG(st.st_mode)) continue;
    if (s->n == MAX_FILES) {
      ok = 0;
      break;
    }
    file_state *f = &s->f[s->n++];
    snprintf(f->name, sizeof f->name, "%s", e->d_name);
    f->size = st.st_size;
    f->written = st.st_mtimespec;
  }
  closedir(d);
  qsort(s->f, (size_t)s->n, sizeof s->f[0], cmp_name);
  return ok;
}

static int same_file(const file_state *a, const file_state *b) {
  return !strcmp(a->name, b->name) && a->size == b->size && a->written.tv_sec == b->written.tv_sec &&
         a->written.tv_nsec == b->written.tv_nsec;
}

static int same_folder(const folder_state *a, const folder_state *b) {
  if (a->n != b->n) return 0;
  for (int i = 0; i < a->n; i++)
    if (!same_file(&a->f[i], &b->f[i])) return 0;
  return 1;
}

static const file_state *find_file(const folder_state *s, const char *name) {
  for (int i = 0; i < s->n; i++)
    if (!strcmp(s->f[i].name, name)) return &s->f[i];
  return NULL;
}

static void name_for(long long t, char *out, size_t n) {
  time_t tt = (time_t)t;
  struct tm utc;
  gmtime_r(&tt, &utc);
  strftime(out, n, "%Y-%m-%d_%H-%M-%S", &utc);
}

// The UTC time of a backup's name; 0 if it isn't exactly a name this would write
static int time_of(const char *name, long long *t) {
  struct tm utc = {0};
  char extra;
  if (sscanf(name, "%4d-%2d-%2d_%2d-%2d-%2d%c", &utc.tm_year, &utc.tm_mon, &utc.tm_mday, &utc.tm_hour, &utc.tm_min,
             &utc.tm_sec, &extra) != 6)
    return 0;
  utc.tm_year -= 1900;
  utc.tm_mon -= 1;
  time_t tt = timegm(&utc);
  if (tt == -1) return 0;
  char back[32];
  name_for((long long)tt, back, sizeof back);
  if (strcmp(back, name)) return 0;
  *t = (long long)tt;
  return 1;
}

static void remove_tree(const char *path) {
  DIR *d = opendir(path);
  if (d) {
    for (struct dirent *e; (e = readdir(d));) {
      if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
      char p[2048];
      snprintf(p, sizeof p, "%s/%s", path, e->d_name);
      struct stat st;
      if (!lstat(p, &st) && S_ISDIR(st.st_mode))
        remove_tree(p);
      else
        unlink(p);
    }
    closedir(d);
  }
  rmdir(path);
}

static void make_dirs(const char *path) {
  char p[2048];
  snprintf(p, sizeof p, "%s", path);
  for (char *s = p + 1; *s; s++)
    if (*s == '/') {
      *s = 0;
      mkdir(p, 0755);
      *s = '/';
    }
  mkdir(p, 0755);
}

typedef struct {
  char path[1024];
  long long time;
} backup;

static int cmp_backup(const void *a, const void *b) {
  long long x = ((const backup *)a)->time, y = ((const backup *)b)->time;
  return x < y ? -1 : x > y;
}

// A tier's backups, oldest first. Copies a run didn't finish are cleared out; anything else is left alone.
static int list_backups(const char *tier, backup *out, int max) {
  int n = 0;
  DIR *d = opendir(tier);
  if (!d) return 0;
  for (struct dirent *e; (e = readdir(d));) {
    char p[1024];
    struct stat st;
    snprintf(p, sizeof p, "%s/%s", tier, e->d_name);
    if (e->d_name[0] == '.' && strncmp(e->d_name, PARTIAL, strlen(PARTIAL))) continue;
    if (lstat(p, &st) || !S_ISDIR(st.st_mode)) continue;
    long long t;
    if (!strncmp(e->d_name, PARTIAL, strlen(PARTIAL)))
      remove_tree(p);
    else if (n < max && time_of(e->d_name, &t)) {
      snprintf(out[n].path, sizeof out[n].path, "%s", p);
      out[n++].time = t;
    }
  }
  closedir(d);
  qsort(out, (size_t)n, sizeof *out, cmp_backup);
  return n;
}

// A file's bytes and its modified time (to the nanosecond, as the comparison wants)
static int copy_file(const char *from, const char *to, const struct timespec *written) {
  if (copyfile(from, to, NULL, COPYFILE_DATA | COPYFILE_EXCL)) return 0;
  struct timespec times[2] = {*written, *written};
  return !utimensat(AT_FDCWD, to, times, 0);
}

// Copies source into tier/name; files the same as in `same_in` are hard links to its copy. 1 and the new backup's path.
static int copy_backup(const char *source, const char *tier, const char *name, const char *same_in, char *made,
                       size_t made_n) {
  static folder_state before, same, after, copy;
  if (!read_folder(source, &before) || !before.n) return 0;
  if (!same_in || !read_folder(same_in, &same)) same.n = 0;
  char partial[1024];
  snprintf(partial, sizeof partial, "%s/%s%s", tier, PARTIAL, name);
  make_dirs(tier);
  remove_tree(partial);
  if (mkdir(partial, 0755)) return 0;
  int copied = 1;
  for (int i = 0; i < before.n && copied; i++) {
    const file_state *f = &before.f[i];
    char from[2048], to[2048];
    snprintf(to, sizeof to, "%s/%s", partial, f->name);
    const file_state *old = find_file(&same, f->name);
    if (old && same_file(old, f)) {
      snprintf(from, sizeof from, "%s/%s", same_in, f->name);
      if (!link(from, to)) continue;
    }
    snprintf(from, sizeof from, "%s/%s", source, f->name);
    copied = copy_file(from, to, &f->written);
  }
  // the game may have saved while this copied
  copied = copied && read_folder(source, &after) && same_folder(&after, &before) && read_folder(partial, &copy) &&
           same_folder(&copy, &before);
  snprintf(made, made_n, "%s/%s", tier, name);
  struct stat st;
  if (!copied || !stat(made, &st) || rename(partial, made)) {
    remove_tree(partial);
    return 0;
  }
  return 1;
}

static void prune(int k, const char *tier) {
  static backup b[MAX_BACKUPS];
  int n = list_backups(tier, b, MAX_BACKUPS);
  for (int i = 0; i + TIERS[k].keep < n; i++) {
    remove_tree(b[i].path);
    rsm_log("profile backup: deleted the old %s/%s", TIERS[k].folder, strrchr(b[i].path, '/') + 1);
  }
}

static void write_howto(const char *root, const char *save) {
  char path[1100];
  snprintf(path, sizeof path, "%s/howto.txt", root);
  FILE *f = fopen(path, "w");
  if (!f) return;
  fprintf(f,
          "rearmed-mods backs up your Rocksmith profiles here while the game runs (at most once every 10 minutes, only "
          "when they changed).\n"
          "Each folder keeps its newest backups: Every 10 Minutes 6, Hourly 6, Quarter Daily 4, Daily 30.\n"
          "Before GUI has the backups Re-ARMed Mods.app makes when it opens (NumberOfBackups in RSMods.ini sets how many "
          "are kept).\n"
          "Backup names are the time of the backup in UTC.\n"
          "Backups share unchanged files with each other, so don't edit files in here. Copy them out first.\n\n"
          "To restore one: quit the game, then copy every file from one of the backups into:\n%s\n",
          save);
  fclose(f);
}

static void backup_if_due(void) {
  char save[1024], root[256], tier[512], made[1100];
  unsigned long long account;
  if (!rsm_save_folder(save, sizeof save, &account)) return;
  snprintf(root, sizeof root, ROOT "/%llu", account);
  make_dirs(root);
  long long now = (long long)time(NULL);
  static backup recent[MAX_BACKUPS];
  snprintf(tier, sizeof tier, "%s/%s", root, TIERS[0].folder);
  int n = list_backups(tier, recent, MAX_BACKUPS);
  const backup *newest = n ? &recent[n - 1] : NULL;
  if (newest) {
    if (newest->time / TIERS[0].seconds == now / TIERS[0].seconds) return;  // these 10 minutes have one
    static folder_state current, last;
    if (read_folder(save, &current) && read_folder(newest->path, &last) && same_folder(&current, &last)) return;
  }
  char name[32];
  name_for(now, name, sizeof name);
  if (!copy_backup(save, tier, name, newest ? newest->path : NULL, made, sizeof made)) {
    rsm_log("profile backup: couldn't back up %s this time, will try again", save);
    return;
  }
  rsm_log("profile backup: %s", made);
  for (int k = 0; k < NTIERS; k++) {
    snprintf(tier, sizeof tier, "%s/%s", root, TIERS[k].folder);
    if (k) {
      static backup b[MAX_BACKUPS];
      int m = list_backups(tier, b, MAX_BACKUPS);
      if (m && b[m - 1].time / TIERS[k].seconds >= now / TIERS[k].seconds) continue;  // has this period
      char linked[1100];
      if (!copy_backup(made, tier, name, made, linked, sizeof linked)) continue;  // not pruned without the new one
    }
    prune(k, tier);
  }
  write_howto(root, save);
}

static void *backup_thread(void *arg) {
  (void)arg;
  for (;;) {
    if (atomic_load(&g_on)) backup_if_due();
    sleep(30);
  }
  return NULL;
}

static void tick(void) {
  static int started;
  atomic_store(&g_on, rsm_on_section("GUI Settings", "BackupProfile"));
  if (!started && atomic_load(&g_on)) {
    pthread_t t;
    if (pthread_create(&t, NULL, backup_thread, NULL) == 0) pthread_detach(t);
    started = 1;
  }
}

const rsm_mod rsm_mod_profile_backups = {"profile_backups", tick, NULL, NULL, NULL};
