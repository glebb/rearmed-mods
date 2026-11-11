// A song's title, artist and album from its archive. See song_info.h.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "rsmods_files.h"
#include "song_info.h"

static int hex4(const char *p, unsigned *v) {
  *v = 0;
  for (int i = 0; i < 4; i++) {
    char c = p[i];
    unsigned d = c >= '0' && c <= '9' ? (unsigned)(c - '0')
                 : c >= 'a' && c <= 'f' ? (unsigned)(c - 'a' + 10)
                 : c >= 'A' && c <= 'F' ? (unsigned)(c - 'A' + 10)
                 : 16;
    if (d > 15) return 0;
    *v = *v << 4 | d;
  }
  return 1;
}

// A JSON string from its opening quote, unescaped into out (n bytes; what doesn't fit is left out, at a whole
// character; out may be NULL). The position after the closing quote, or NULL when the string doesn't end.
static const char *string(const char *p, char *out, size_t n) {
  size_t used = 0;
  int full = !out || !n;
  for (p++; *p && *p != '"';) {
    char utf[4];
    int len = 1;
    unsigned c = (unsigned char)*p++;
    if (c == '\\') {
      c = (unsigned char)*p++;
      switch (c) {
        case 0: return NULL;
        case 'n': c = '\n'; break;
        case 't': c = '\t'; break;
        case 'r': c = '\r'; break;
        case 'b': c = '\b'; break;
        case 'f': c = '\f'; break;
        case 'u': {
          unsigned low;
          if (!hex4(p, &c)) return NULL;
          p += 4;
          if (c >= 0xd800 && c < 0xdc00 && p[0] == '\\' && p[1] == 'u' && hex4(p + 2, &low) && low >= 0xdc00 &&
              low < 0xe000) {
            c = 0x10000 + ((c - 0xd800) << 10) + (low - 0xdc00);
            p += 6;
          } else if (c >= 0xd800 && c < 0xe000) {
            c = 0xfffd;  // half a pair
          }
          break;
        }
        default: break;  // the character itself: \" \\ \/
      }
      if (c < 0x80) {
        utf[0] = (char)c;
      } else if (c < 0x800) {
        utf[0] = (char)(0xc0 | c >> 6), utf[1] = (char)(0x80 | (c & 0x3f)), len = 2;
      } else if (c < 0x10000) {
        utf[0] = (char)(0xe0 | c >> 12), utf[1] = (char)(0x80 | (c >> 6 & 0x3f)), utf[2] = (char)(0x80 | (c & 0x3f));
        len = 3;
      } else {
        utf[0] = (char)(0xf0 | c >> 18), utf[1] = (char)(0x80 | (c >> 12 & 0x3f));
        utf[2] = (char)(0x80 | (c >> 6 & 0x3f)), utf[3] = (char)(0x80 | (c & 0x3f)), len = 4;
      }
    } else {
      // the text's own UTF-8: a whole character at a time, so a cut never lands inside one
      utf[0] = (char)c;
      if (c >= 0xc0)
        while (len < 4 && ((unsigned char)*p & 0xc0) == 0x80) utf[len++] = *p++;
    }
    if (!full && used + (size_t)len < n) {
      memcpy(out + used, utf, (size_t)len);
      used += (size_t)len;
    } else {
      full = 1;
    }
  }
  if (out && n) out[used] = 0;
  return *p == '"' ? p + 1 : NULL;
}

static const char *space(const char *p) {
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
  return p;
}

int song_info_parse(const char *json, const char *song_key, song_info *out) {
  int depth = 0, attrs = -1;  // attrs: the depth inside the Attributes object being read, or -1
  song_info cur;
  char key[160], event[200], want_event[200];
  snprintf(want_event, sizeof want_event, "Play_%s", song_key);
  for (const char *p = json; *p;) {
    if (*p == '{' || *p == '[') {
      depth++, p++;
    } else if (*p == '}' || *p == ']') {
      if (*p == '}' && attrs >= 0 && depth == attrs) {
        if (*cur.title && (!strcasecmp(key, song_key) || !strcasecmp(event, want_event))) {
          *out = cur;
          return 1;
        }
        attrs = -1;
      }
      depth--, p++;
    } else if (*p == '"') {
      char name[32];
      const char *q = string(p, name, sizeof name);
      if (!q) return 0;
      p = space(q);
      if (*p != ':') continue;  // a value
      p = space(p + 1);
      if (attrs < 0 && *p == '{' && !strcmp(name, "Attributes")) {
        attrs = ++depth, p++;
        memset(&cur, 0, sizeof cur);
        *key = *event = 0;
      } else if (depth == attrs && *p == '"') {
        char *to = !strcmp(name, "SongName") ? cur.title
                   : !strcmp(name, "ArtistName") ? cur.artist
                   : !strcmp(name, "AlbumName") ? cur.album
                   : !strcmp(name, "SongKey") ? key
                   : !strcmp(name, "SongEvent") ? event
                   : NULL;
        size_t n = to == key ? sizeof key : to == event ? sizeof event : sizeof cur.title;
        if (!(p = string(p, to, to ? n : 0))) return 0;
      }
    } else {
      p++;
    }
  }
  return 0;
}

static int ends_with(const char *s, const char *end) {
  size_t n = strlen(s), m = strlen(end);
  return n >= m && !strcasecmp(s + n - m, end);
}

int song_info_read(const char *archive, const char *song_key, const uint8_t key[32], song_info *out) {
  rsm_psarc *a = rsm_psarc_open_io(archive, key, &rsm_file_io);
  if (!a) return 0;
  int found = 0, files = rsm_psarc_files(a);
  for (int pass = 0; pass < 2 && !found; pass++)
    for (int i = 0; i < files && !found; i++) {
      const char *name = rsm_psarc_name(a, i);
      if (strncasecmp(name, "manifests/", 10)) continue;
      if (pass ? !rsm_is_song_manifest(name, song_key) : !ends_with(name, ".hsan")) continue;
      char *json = rsm_psarc_read(a, i, NULL);
      if (!json) continue;
      found = song_info_parse(json, song_key, out);
      free(json);
    }
  rsm_psarc_close(a);
  return found;
}
