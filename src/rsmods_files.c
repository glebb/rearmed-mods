// Files as the game finds them. See rsmods_files.h.
//
// The layer's functions are the game executable's (rc_file.h) and are looked up by name, as the other parts of the
// runtime newer than 1.5.1 are: a library that named them outright would not load in a game without them. All of them
// or none: a descriptor the layer opened is the layer's to read and close.
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>

#include "rc_file.h"
#include "rsmods_files.h"

static struct {
  __typeof__(rc_file_open) *open;
  __typeof__(rc_file_pread) *pread;
  __typeof__(rc_file_close) *close;
  __typeof__(rc_file_stat) *stat;
  __typeof__(rc_file_opendir) *opendir;
  __typeof__(rc_file_readdir) *readdir;
  __typeof__(rc_file_closedir) *closedir;
} g_rc;
static int g_layer;
static pthread_once_t g_once = PTHREAD_ONCE_INIT;

static void find(void) {
  *(void **)&g_rc.open = dlsym(RTLD_DEFAULT, "rc_file_open");
  *(void **)&g_rc.pread = dlsym(RTLD_DEFAULT, "rc_file_pread");
  *(void **)&g_rc.close = dlsym(RTLD_DEFAULT, "rc_file_close");
  *(void **)&g_rc.stat = dlsym(RTLD_DEFAULT, "rc_file_stat");
  *(void **)&g_rc.opendir = dlsym(RTLD_DEFAULT, "rc_file_opendir");
  *(void **)&g_rc.readdir = dlsym(RTLD_DEFAULT, "rc_file_readdir");
  *(void **)&g_rc.closedir = dlsym(RTLD_DEFAULT, "rc_file_closedir");
  g_layer = g_rc.open && g_rc.pread && g_rc.close && g_rc.stat && g_rc.opendir && g_rc.readdir && g_rc.closedir;
}

int rsm_files_layer(void) {
  pthread_once(&g_once, find);
  return g_layer;
}

int rsm_file_open(const char *path) {
  return rsm_files_layer() ? g_rc.open(path, O_RDONLY | O_CLOEXEC, 0) : open(path, O_RDONLY | O_CLOEXEC);
}
ssize_t rsm_file_pread(int fd, void *buf, size_t n, off_t off) {
  return rsm_files_layer() ? g_rc.pread(fd, buf, n, off) : pread(fd, buf, n, off);
}
int rsm_file_close(int fd) { return rsm_files_layer() ? g_rc.close(fd) : close(fd); }
int rsm_file_stat(const char *path, struct stat *st) { return rsm_files_layer() ? g_rc.stat(path, st) : stat(path, st); }
DIR *rsm_file_opendir(const char *path) { return rsm_files_layer() ? g_rc.opendir(path) : opendir(path); }
struct dirent *rsm_file_readdir(DIR *d) { return rsm_files_layer() ? g_rc.readdir(d) : readdir(d); }
int rsm_file_closedir(DIR *d) { return rsm_files_layer() ? g_rc.closedir(d) : closedir(d); }

const rsm_psarc_io rsm_file_io = {rsm_file_open, rsm_file_pread, rsm_file_close};
