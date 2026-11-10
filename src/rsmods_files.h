// Files as the game finds them: RS Re-ARMed's file layer (rc_file.h, 1.14.0 and later), which the game's own opens,
// sizes and folder listings go through in both editions. A mod can serve files there that are no files on disk (a song
// archive made from another one), list them in a folder and hide the ones they stand for; what reads the game's files
// by these sees what the game sees. On a runtime without the layer, and for every file no mod serves, they are the
// system's functions. Nothing here touches the game (rsmods_files.c: the layer is found by name), and any thread may
// call them: a provider's functions then run on that thread, as they do on the game's loading threads.
#ifndef RS_RSMODS_FILES_H
#define RS_RSMODS_FILES_H

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "rsmods_tuning.h"

int rsm_files_layer(void);  // 1 when the runtime has the file layer
// A file for reading: a descriptor for the three below and nothing else (a served file's is a placeholder), or -1
int rsm_file_open(const char *path);
ssize_t rsm_file_pread(int fd, void *buf, size_t n, off_t off);
int rsm_file_close(int fd);
int rsm_file_stat(const char *path, struct stat *st);
// A folder's names with the served ones added and the hidden ones left out
DIR *rsm_file_opendir(const char *path);
struct dirent *rsm_file_readdir(DIR *d);
int rsm_file_closedir(DIR *d);
// The first three, for rsm_psarc_open_io
extern const rsm_psarc_io rsm_file_io;

#endif
