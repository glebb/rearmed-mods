// A song's title, artist and album from its archive, for what shows the song by name (Discord Rich Presence). They are
// in the archive's manifests: the header file (manifests/<dir>/<name>.hsan) and each arrangement's own .json have
// Entries, one per arrangement, each with Attributes: SongKey, SongName, ArtistName, AlbumName. A song pack's header
// has every song of the pack. Without the game (tests/test_song_info.c).
#ifndef SONG_INFO_H
#define SONG_INFO_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
  char title[160], artist[160], album[160];  // UTF-8, cut at a whole character; "" when the manifest has none
} song_info;

// The first Attributes in a manifest's JSON whose SongKey is song_key (any case), or whose SongEvent is
// "Play_<song_key>", and which has a SongName: 1 and its names, or 0
int song_info_parse(const char *json, const char *song_key, song_info *out);
// The same from the song's archive (as the game finds it, rsmods_files.h): its header files first, then the
// arrangements' manifests. `key`: the archives' key.
int song_info_read(const char *archive, const char *song_key, const uint8_t key[32], song_info *out);

#endif
