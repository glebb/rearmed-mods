// A check of Nashville Numbers on the real songs, not a test: it needs the game's archives. For every song in the
// archives given: the key (song_key.h), and for each guitar part with chords, the harmony of the song's other parts
// (song_harmony.h) at each of its chords that the chart names or that has three notes or more. The root is right, or
// right in the chord before or after (a change half a bar off), or wrong. With -c SONG, the song's chart, a bar a cell.
//   cc -O1 -Isrc tests/probe_harmony.c src/song_harmony.c src/song_key.c src/song_sng.c src/rsmods_tuning.c -lz
//   ./a.out <the archives' key, 64 hex digits> [-c song] <game>/songs.psarc <game>/dlc/*.psarc
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "../src/song_harmony.h"

// The root a chart's chord name says ("B7_", "F#m/C#", "Bb5"), or -1
static int name_root(const char *name) {
  static const int LETTER[7] = {9, 11, 0, 2, 4, 5, 7};
  if (name[0] < 'A' || name[0] > 'G') return -1;
  int r = LETTER[name[0] - 'A'];
  if (name[1] == '#') r++;
  else if (name[1] == 'b') r--;
  return (r + 12) % 12;
}

static const char *const NAMES[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};

static void chart(const char *song, sng_arrangement *arr, int n, const sh_chord *c, int nc, const sk_key *key) {
  char kn[16];
  sk_key_name(key, kn, sizeof kn);
  printf("== %s, key %s\n", song, kn);
  const sng_beat *beats = arr[0].beats;
  int nb = arr[0].nbeats, cells = 0, k = 0;
  float first = 1e9;
  for (int i = 0; i < n; i++)
    if (arr[i].n && arr[i].ev[0].time < first) first = arr[i].ev[0].time;
  for (int i = 0; i < nb && cells < 48; i++) {
    if (beats[i].beat != 0 || beats[i].time < first - 0.1f) continue;
    int j = i + 1;
    while (j < nb && beats[j].beat != 0) j++;
    float end = j < nb ? beats[j].time : 1e9f;
    char cell[64] = "", last[24] = "";
    while (k < nc && c[k].end <= beats[i].time + 1e-3f) k++;
    for (int m = k; m < nc && c[m].start < end - 1e-3f; m++) {
      char num[24];
      sk_numeral(&c[m].chord, key, 0, 0, 1, num, sizeof num);
      if (strcmp(num, last)) snprintf(cell + strlen(cell), sizeof cell - strlen(cell), "%s%s", *cell ? " " : "", num);
      snprintf(last, sizeof last, "%s", num);
    }
    if (cells % 4 == 0) printf("%s%d:%02d  |", cells ? "\n" : "", (int)beats[i].time / 60, (int)beats[i].time % 60);
    printf(" %-10s|", *cell ? cell : "-");
    cells++;
  }
  printf("\n");
}

int main(int argc, char **argv) {
  uint8_t key[32];
  if (argc < 3 || !rsm_psarc_key_from_hex(argv[1], key)) return 2;
  const char *only = NULL;
  int first = 2;
  if (!strcmp(argv[2], "-c") && argc > 4) only = argv[3], first = 4;
  long ok = 0, near = 0, total = 0, changes = 0, bars = 0;
  for (int ai = first; ai < argc; ai++) {
    rsm_psarc *a = rsm_psarc_open(argv[ai], key);
    if (!a) continue;
    static char seen[1024][64];
    int ns = 0;
    for (int i = 0; i < rsm_psarc_files(a); i++) {
      const char *nm = rsm_psarc_name(a, i), *p = strstr(nm, "/bin/");
      if (!p || !(p = strchr(p + 5, '/'))) continue;
      const char *u = strrchr(++p, '_');
      if (!u) continue;
      char song[64];
      snprintf(song, sizeof song, "%.*s", (int)(u - p), p);
      int dup = 0;
      for (int j = 0; j < ns; j++) dup |= !strcmp(seen[j], song);
      if (!dup && ns < 1024 && (!only || !strcasecmp(only, song))) snprintf(seen[ns++], 64, "%s", song);
    }
    rsm_psarc_close(a);
    for (int s = 0; s < ns; s++) {
      sng_arrangement *arr;
      int n = sng_read_song(argv[ai], seen[s], key, NULL, &arr);
      if (!n) continue;
      sk_stats st;
      sk_stats_init(&st);
      for (int i = 0; i < n; i++) sk_stats_add(&st, arr[i].ev, arr[i].n);
      sk_key k;
      if (!sk_guess(&st, &k, 1)) {
        sng_free_all(arr, n);
        continue;
      }
      sh_part parts[16];
      int np = n < 16 ? n : 16;
      {  // the whole song's chart: how often it changes, as the mod shows it
        for (int i = 0; i < np; i++) parts[i] = (sh_part){arr[i].ev, arr[i].n, arr[i].bass};
        sh_chord *c;
        int nc = sh_infer(parts, np, arr[0].beats, arr[0].nbeats, &k, &c);
        if (only) chart(seen[s], arr, n, c, nc, &k);
        char last[24] = "", num[24];
        for (int i = 0; i < nc; i++) {
          sk_numeral(&c[i].chord, &k, 0, 0, 1, num, sizeof num);
          changes += strcmp(num, last) != 0;
          snprintf(last, sizeof last, "%s", num);
        }
        if (nc)
          for (int i = 0; i < arr[0].nbeats; i++) bars += arr[0].beats[i].beat == 0 && arr[0].beats[i].time >= c[0].start && arr[0].beats[i].time < c[nc - 1].end;
        free(c);
      }
      for (int t = 0; t < np && n > 1; t++) {
        if (arr[t].bass) continue;
        int m = 0;
        for (int i = 0; i < np; i++)
          if (i != t) parts[m++] = (sh_part){arr[i].ev, arr[i].n, arr[i].bass};
        sh_chord *c;
        int nc = sh_infer(parts, m, arr[t].beats, arr[t].nbeats, &k, &c);
        for (int e = 0, at = 0; e < arr[t].n && nc; e++) {
          const sk_event *ev = &arr[t].ev[e];
          if (ev->chord < 0 || ev->chord >= arr[t].nchords || ev->n < 2) continue;
          const char *name = arr[t].chord_names[ev->chord];
          int truth = name_root(name), set = 0;
          for (int i = 0; i < ev->n; i++) set |= 1 << (ev->midi[i] % 12);
          if (truth < 0) {
            sk_chord ch;
            if (*name || __builtin_popcount(set) < 3 || !sk_chord_of(ev->midi, ev->n, &ch)) continue;  // a riff's two notes
            truth = ch.root;
          }
          while (at + 1 < nc && c[at + 1].start <= ev->time + 0.02f) at++;
          if (c[at].start > ev->time + 0.02f || c[at].end < ev->time) continue;  // no harmony there
          total++;
          if (c[at].chord.root == truth) ok++;
          else if ((at && c[at - 1].chord.root == truth) || (at + 1 < nc && c[at + 1].chord.root == truth)) near++;
        }
        free(c);
      }
      sng_free_all(arr, n);
    }
  }
  if (bars) printf("a change every %.2f bars; ", (double)bars / changes);
  if (total)
    printf("root right %ld/%ld = %.1f%%, with a change half a bar off %.1f%%\n", ok, total, 100.0 * ok / total,
           100.0 * (ok + near) / total);
  (void)NAMES;
  return 0;
}
