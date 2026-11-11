// Song Stems' way from a song's key to its .wem, without the game (tests/test_stems_index.c): which archive has the
// song, and the audio its bank plays. The archives are found and read as the game finds them (rsmods_files.h), so a
// song whose archive a mod serves, and which is no file on disk, is there like any other, under the name the game
// opens it by; an archive a mod hides (the one a served archive stands for) is not. An archive that can't be read is
// left out. Two mods use it, each on a thread of its own (Song Stems, Discord Rich Presence): the list is built and asked
// under a lock by stems_index_build and stems_index_path; stems_index_find is for one thread alone.
#ifndef STEMS_INDEX_H
#define STEMS_INDEX_H

#include <stddef.h>
#include <stdint.h>

// Lists the archives again: dlc and all below it, and songs.psarc, from the current folder (the game's). `key`: the
// archives' key. The number of archives with a song.
int stems_index_build(const uint8_t key[32]);
// The archives of the last build, or -1 before the first
int stems_index_listed(void);
// The archive with the song's bank (song_<key>.bnk, any case), or NULL. Valid until the next build.
const char *stems_index_find(const char *song);
// The same, copied to out: 1, or 0 when there is none or it doesn't fit
int stems_index_path(const char *song, char *out, size_t n);
// The .wem the song's bank plays, from that archive: malloc'ed, with its file ID and length; NULL when there is none
void *stems_index_wem(const char *archive, const char *song, const uint8_t key[32], uint32_t *id, size_t *len);
void stems_index_free(void);

#endif
