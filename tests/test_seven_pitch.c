// The detector's raw candidate floor of a low tuning (src/seven_pitch.h: seven_pitch_floor, rs7string's rsxrp::Floor)
#include <stdint.h>
#include "../src/seven_pitch.h"
#include "check.h"

int main(void) {
  // a song at or above the floor leaves the game's
  CHECK(seven_pitch_floor(24, 40) == 24);  // E standard
  CHECK(seven_pitch_floor(24, 35) == 24);  // B standard, 7 strings
  CHECK(seven_pitch_floor(24, 24) == 24);
  // below it: one semitone under the lowest open note
  CHECK(seven_pitch_floor(24, 23) == 22);  // B0, a five-string bass
  CHECK(seven_pitch_floor(24, 21) == 20);  // A0
  CHECK(seven_pitch_floor(24, 19) == 18);  // G0, the lowest that is heard
  // never below 18
  CHECK(seven_pitch_floor(24, 18) == 18);
  CHECK(seven_pitch_floor(24, 12) == 18);
  CHECK(seven_pitch_floor(24, 0) == 18);
  // another mod's floor is the reset's, and kept when it is already lower
  CHECK(seven_pitch_floor(20, 30) == 20);
  CHECK(seven_pitch_floor(20, 19) == 18);
  CHECK(seven_pitch_floor(16, 10) == 16);
  return check_done("seven_pitch");
}
