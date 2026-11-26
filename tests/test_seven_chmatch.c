// Two rules of Seven-String Guitar's chord matcher (src/seven_chmatch.h)
#include "../src/seven_chmatch.h"
#include "check.h"

int main(void) {
  // the table: MIDI 22..93
  CHECK(!seven_table_has_row(21) && seven_table_has_row(22) && seven_table_has_row(35) && seven_table_has_row(93));
  CHECK(!seven_table_has_row(94) && !seven_table_has_row(-1) && !seven_table_has_row(200));

  // a low chord: two or three different notes, the lowest below 28
  const int32_t power[8] = {-1, -1, -1, -1, -1, -1, 26, 33};           // a low dyad
  CHECK(seven_low_chord(power, 8));
  const int32_t triad[8] = {-1, 40, -1, 47, -1, -1, 26, -1};           // three notes
  CHECK(seven_low_chord(triad, 8));
  const int32_t four[8] = {-1, 40, 45, 47, -1, -1, 26, -1};            // four: not
  CHECK(!seven_low_chord(four, 8));
  const int32_t one[8] = {-1, -1, -1, -1, -1, -1, 26, -1};             // one: not
  CHECK(!seven_low_chord(one, 8));
  const int32_t twice[8] = {26, -1, -1, -1, -1, -1, 26, -1};           // the same note twice is one
  CHECK(!seven_low_chord(twice, 8));
  const int32_t dyad_twice[8] = {26, 33, -1, -1, -1, -1, 26, -1};      // 26, 33, 26: two different
  CHECK(seven_low_chord(dyad_twice, 8));
  const int32_t high[8] = {-1, 40, -1, 47, -1, -1, 35, -1};            // the lowest is 35 (a 7-string's B1): not
  CHECK(!seven_low_chord(high, 8));
  const int32_t edge[8] = {28, 40, -1, -1, -1, -1, -1, -1};            // 28 is not below 28
  CHECK(!seven_low_chord(edge, 8));
  const int32_t zero[8] = {0, 27, -1, -1, -1, -1, -1, -1};             // a 0 target counts, but is not the low one
  CHECK(seven_low_chord(zero, 8));
  return check_done("seven_chmatch");
}
