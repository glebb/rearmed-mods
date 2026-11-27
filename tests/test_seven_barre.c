// The finger markers' barre logic of Seven-String Guitar (src/seven_barre.h): physical ranks, a finger's span, its
// anchor string, and the barre capsule's scale
#include <stdint.h>
#include <string.h>
#include "../src/seven_barre.h"
#include "check.h"

#define N 0xff

static unsigned span_of(const uint8_t *fingers, unsigned n, unsigned finger, int *ok) {
  unsigned lo, hi;
  seven_barre_span(fingers, n, finger, &lo, &hi);
  *ok = hi != 0;
  return hi - lo + 1;  // as the game computes it: the unsigned wrap is the -5 / -6 of no match
}

int main(void) {
  // ranks: six slots as slots; seven: string 6 is the lowest, 0-5 above it; eight: 7 lowest, then 6, then 0-5
  for (unsigned s = 0; s < 6; s++) CHECK(seven_barre_rank(s, 6) == s);
  CHECK(seven_barre_rank(6, 7) == 0 && seven_barre_rank(0, 7) == 1 && seven_barre_rank(5, 7) == 6);
  CHECK(seven_barre_rank(7, 8) == 0 && seven_barre_rank(6, 8) == 1 && seven_barre_rank(0, 8) == 2);

  int ok;
  // a barre of finger 1 over strings 6, 0, 1 (the low B, E, A): three strings, not 6-0+1
  const uint8_t barre[8] = {1, 1, N, N, N, N, 1, N};
  CHECK(span_of(barre, 7, 1, &ok) == 3 && ok);
  // the low B alone: span 1, found (rank 0 is its lowest: hi == 0 is the game's "no finger" test, so it reads as
  // not found; the span is still 1 as the game's own loop gives a finger on string 0 of six)
  const uint8_t low[8] = {N, N, N, N, N, N, 2, N};
  CHECK(span_of(low, 7, 2, &ok) == 1 && !ok);
  // strings 0 and 5 of a seven-slot template: ranks 1 and 6
  const uint8_t wide[8] = {3, N, N, N, N, 3, N, N};
  CHECK(span_of(wide, 7, 3, &ok) == 6 && ok);
  // no such finger: lo = 7, hi = 0: -6; six slots: -5
  CHECK((int)span_of(wide, 7, 4, &ok) == -6 && !ok);
  CHECK((int)span_of(wide, 6, 4, &ok) == -5 && !ok);
  // six slots equal the game's: strings 1..3 -> 3
  const uint8_t six[8] = {N, 1, 1, 1, N, N, N, N};
  CHECK(span_of(six, 6, 1, &ok) == 3 && ok);

  // the anchor: the finger's lowest string, whichever the loop reached first
  CHECK(seven_barre_anchor(barre, 7, 0) == 6);  // loop reaches string 0 first, the barre starts at the low B
  CHECK(seven_barre_anchor(barre, 7, 1) == 6);
  CHECK(seven_barre_anchor(barre, 7, 6) == 6);
  CHECK(seven_barre_anchor(barre, 7, 2) == 2);  // no finger there: unchanged
  CHECK(seven_barre_anchor(wide, 7, 0) == 0);   // strings 0 and 5: the lower is 0
  CHECK(seven_barre_anchor(wide, 7, 5) == 0);
  CHECK(seven_barre_anchor(six, 6, 2) == 1);    // six slots: the lowest slot, as the game's
  CHECK(seven_barre_anchor(barre, 7, 9) == 9);  // past the slots: unchanged
  const uint8_t eight[8] = {1, N, N, N, N, N, 1, 1};  // strings 0, 6, 7: the lowest is 7
  CHECK(seven_barre_anchor(eight, 8, 0) == 7);

  // the capsule's scale: up to five gaps the stock capsule; a barre of seven is 6 / 5
  CHECK(seven_barre_entity_scale(1, 1.608f, 1.608f) == 1.0f);
  CHECK(seven_barre_entity_scale(6, 1.608f, 1.608f) == 1.0f);
  CHECK(seven_barre_entity_scale(7, 1.608f, 1.608f) > 1.1999f && seven_barre_entity_scale(7, 1.608f, 1.608f) < 1.2001f);
  CHECK(seven_barre_entity_scale(8, 1.608f, 1.608f) > 1.3999f && seven_barre_entity_scale(8, 1.608f, 1.608f) < 1.4001f);
  CHECK(seven_barre_entity_scale(7, 0.0f, 1.608f) == 1.0f && seven_barre_entity_scale(0, 1.608f, 1.608f) == 1.0f);
  // a spaced guitar (spacing_guitar7 = 2 x the stock gap): six of its gaps over the clip's five stock ones
  CHECK(seven_barre_entity_scale(7, 2 * 1.608f, 1.608f) > 2.3999f && seven_barre_entity_scale(7, 2 * 1.608f, 1.608f) < 2.4001f);
  CHECK(seven_barre_entity_scale(7, 0.5f * 1.608f, 1.608f) == 1.0f);  // narrower: never below the stock capsule
  return check_done("seven_barre");
}
