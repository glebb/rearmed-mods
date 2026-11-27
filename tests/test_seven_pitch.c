// The detector's raw candidate floor of a low tuning (src/seven_pitch.h: seven_pitch_floor, rs7string's rsxrp::Floor)
#include <stdint.h>
#include "../src/seven_pitch.h"
#include "../src/seven_tuner.h"
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
  // the tuner's string count: a 7/8-string target, no more than the guitar was built with
  CHECK(seven_tuner_count(7, 7, 6) == 7 && seven_tuner_count(8, 8, 6) == 8);
  CHECK(seven_tuner_count(8, 7, 6) == 7);  // an 8-string tuning on a guitar built with 7 (no 8-string support)
  CHECK(seven_tuner_count(7, 0, 6) == 7 && seven_tuner_count(8, 0, 6) == 8);  // the guitar not known yet
  CHECK(seven_tuner_count(7, 6, 6) == 7);  // a guitar still being rebuilt: the target's count
  // no target: the build count, six on an extended guitar (a 6-string tuning), a bass's 4 / 5 / 6 kept
  CHECK(seven_tuner_count(0, 6, 7) == 6 && seven_tuner_count(0, 7, 7) == 6 && seven_tuner_count(0, 8, 6) == 6);
  CHECK(seven_tuner_count(0, 4, 6) == 4 && seven_tuner_count(0, 5, 6) == 5 && seven_tuner_count(0, 6, 6) == 6);
  CHECK(seven_tuner_count(0, 0, 5) == 5 && seven_tuner_count(0, 0, 8) == 6);
  return check_done("seven_pitch");
}
