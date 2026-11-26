// Seven-String Guitar (seven.h): the finger markers on the neck (the "hand shape" markers), pure logic. Ported from
// rs7string (github.com/SomeeName/rs7string, MIT, by SomeeName): src/features/publish/grid.cpp (PhysicalRank,
// FingerSpan, FingerAnchor, BarreEntityScale, BarreClipScale), as of 1.0.0-beta.6. Used by seven_chdraw.c; no game
// access, so tests/test_seven_barre.c runs it.
//
// A chord template's strings are in slot order (0-5 the game's, 6 the low B), the neck's in physical order: on a guitar
// with n > 6 strings the extra strings lie beyond string 0, so string 6 is the lowest (rank 0), string 0 rank 1 ... and
// a barre runs up from its lowest string.
#ifndef RS_SEVEN_BARRE_H
#define RS_SEVEN_BARRE_H

#include <stdint.h>

// The physical rank of slot s (0 the lowest string) of an n-slot arrangement: n <= 6 as slots; else slots 0-5 are
// ranks n-6.., and the extra strings run down from n - 1 (slot 6 of 7 is rank 0, slots 6/7 of 8 are 1 and 0)
static inline unsigned seven_barre_rank(unsigned s, unsigned n) {
  if (n <= 6) return s;
  return s < 6 ? s + n - 6 : n - 1 - s;
}

// The lowest and highest rank of the strings whose finger (fingers[n]: int8, 0xff none) is `finger`: lo = n and hi = 0
// when there is none. The game's span is hi - lo + 1 and it tests hi != 0 (its no-match result is 0 - 6 + 1 = -5 for six
// slots; with seven, -6).
static inline void seven_barre_span(const uint8_t *fingers, unsigned n, unsigned finger, unsigned *lo, unsigned *hi) {
  unsigned l = n, h = 0;
  for (unsigned s = 0; s < n; s++)
    if (fingers[s] == finger) {
      const unsigned r = seven_barre_rank(s, n);
      if (r < l) l = r;
      if (r > h) h = r;
    }
  *lo = l;
  *hi = h;
}

// The slot a finger's marker is anchored on when the game's loop reaches slot s (the first slot in slot order that has
// the finger): the finger's lowest string. The game's barre runs up from its anchor, so one that includes a low string
// (6 / 7) must start there. s past n, or a slot with no finger, stays.
static inline unsigned seven_barre_anchor(const uint8_t *fingers, unsigned n, unsigned s) {
  if (s >= n || fingers[s] == 0xff) return s;
  unsigned anchor = s;
  for (unsigned i = 0; i < n; i++)
    if (fingers[i] == fingers[s] && seven_barre_rank(i, n) < seven_barre_rank(anchor, n)) anchor = i;
  return anchor;
}

// The barre capsule's scale: the barre of `span` strings needs span - 1 gaps of `gap`; the stock clip ends at five gaps
// of `clip_gap` (the game's guitar gap, 1.608) and scales up from there. Never below 1 (the stock capsule).
static inline float seven_barre_entity_scale(int span, float gap, float clip_gap) {
  if (span <= 1 || !(gap > 0.0f) || !(clip_gap > 0.0f)) return 1.0f;
  const float reach = (float)(span - 1) * gap / (5.0f * clip_gap);
  return reach > 1.0f ? reach : 1.0f;
}

#endif
