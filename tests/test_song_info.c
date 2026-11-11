// A song's names from a manifest's JSON (src/song_info.c): the entry of the song among a pack's, by its key or its
// event, escapes, names longer than their field, and text that is no manifest. From an archive: test_stems_index.c.
#include <stdlib.h>
#include <string.h>

#include "../src/song_info.h"
#include "check.h"

int main(void) {
  song_info s;
  // a header as the game's are: entries by arrangement ID, nested values before and after the names
  static const char pack[] =
      "\xef\xbb\xbf{\n \"Entries\": {\n"
      "  \"0A\": {\"Attributes\": {\"AlbumArt\": \"urn:image:dds:album_one\", \"AlbumName\": \"First\",\n"
      "    \"ArtistName\": \"The \\\"Quoted\\\" Band\", \"Tuning\": {\"String0\": 0, \"String1\": -2},\n"
      "    \"SongKey\": \"SongOne\", \"SongName\": \"One \\\\ Two\", \"Sections\": [{\"Name\": \"intro\"}]}},\n"
      "  \"0B\": {\"Attributes\": {\"ArtistName\": \"Mot\\u00f6rhead\", \"SongKey\": \"SongTwo\",\n"
      "    \"SongName\": \"Ace \\ud83c\\udfb8 Spades\", \"AlbumName\": \"caf\xc3\xa9\"}},\n"
      "  \"0C\": {\"Attributes\": {\"SongKey\": \"SongTwo\", \"SongName\": \"Later Entry\"}},\n"
      "  \"0D\": {\"Attributes\": {\"SongKey\": \"NoName\", \"ArtistName\": \"Somebody\"}},\n"
      "  \"0E\": {\"Attributes\": {\"SongEvent\": \"Play_ByEvent\", \"SongKey\": \"other\", \"SongName\": \"Evented\"}}\n"
      " },\n \"InsertRoot\": \"Static.Songs.Headers\"\n}";
  CHECK(song_info_parse(pack, "SongOne", &s));
  CHECK(!strcmp(s.title, "One \\ Two") && !strcmp(s.artist, "The \"Quoted\" Band") && !strcmp(s.album, "First"));
  CHECK(song_info_parse(pack, "songtwo", &s));  // any case, and the first entry of the song
  CHECK(!strcmp(s.title, "Ace \xf0\x9f\x8e\xb8 Spades") && !strcmp(s.artist, "Mot\xc3\xb6rhead"));
  CHECK(!strcmp(s.album, "caf\xc3\xa9"));
  CHECK(song_info_parse(pack, "ByEvent", &s) && !strcmp(s.title, "Evented") && !*s.artist);
  memset(&s, 'x', sizeof s);
  CHECK(!song_info_parse(pack, "NoName", &s) && !song_info_parse(pack, "Song", &s));
  CHECK(!song_info_parse(pack, "", &s));

  // a name of a song key outside Attributes, or deeper inside them, is not the song's
  CHECK(!song_info_parse("{\"SongKey\":\"k\",\"SongName\":\"n\"}", "k", &s));
  CHECK(!song_info_parse("{\"Attributes\":{\"Deep\":{\"SongKey\":\"k\",\"SongName\":\"n\"}}}", "k", &s));
  CHECK(song_info_parse("{\"Attributes\":{\"Deep\":{\"SongName\":\"no\"},\"SongKey\":\"k\",\"SongName\":\"n\"}}", "k", &s));
  CHECK(!strcmp(s.title, "n"));
  // a value that happens to be "Attributes", and names that are no strings
  CHECK(song_info_parse("{\"a\":\"Attributes\",\"Attributes\":{\"SongKey\":\"k\",\"SongName\":\"n\",\"ArtistName\":7}}", "k", &s));
  CHECK(!*s.artist);

  // a name longer than its field is cut at a whole character
  char json[1200], name[600] = "";
  for (int i = 0; i < 100; i++) strcat(name, "\xc3\xa9\xe2\x82\xac");  // 2 + 3 bytes, 500 in all
  snprintf(json, sizeof json, "{\"Attributes\":{\"SongKey\":\"long\",\"SongName\":\"%s\",\"ArtistName\":\"A\"}}", name);
  CHECK(song_info_parse(json, "long", &s) && !strcmp(s.artist, "A"));
  size_t l = strlen(s.title);
  CHECK(l >= sizeof s.title - 4 && l < sizeof s.title && !strncmp(s.title, name, l));
  CHECK(((unsigned char)name[l] & 0xc0) != 0x80);  // the cut is before a character's first byte

  // text that ends early or is no JSON gives nothing, and reads nothing past its end
  static const char *const broken[] = {"", "{", "{\"Attributes\":{\"SongKey\":\"k\",\"SongName\":\"n",
                                       "{\"Attributes\":{\"SongKey\":\"k\",\"SongName\":\"n\\", "\"", "\"\\u12",
                                       "{\"Attributes\":{\"SongKey\":\"k\",\"SongName\":\"n\"", "}}}}", "[[[[", NULL};
  for (int i = 0; broken[i]; i++) {
    char *copy = strdup(broken[i]);  // its own block, for a checker of reads past it
    CHECK(!song_info_parse(copy, "k", &s));
    free(copy);
  }
  for (size_t cut = 0; cut < sizeof pack - 1; cut += 3) {
    char *part = strndup(pack, cut);
    song_info_parse(part, "ByEvent", &s);
    free(part);
  }
  return check_done("song info");
}
