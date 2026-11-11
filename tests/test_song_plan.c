// A manifest's tone slots as Tone Host sends a tone (src/song_plan.c): each slot's tone by its key, every knob of its
// gear, its volume as the balance, numbers as the game's floats print, an empty slot, and text that is no manifest.
#include <string.h>

#include "../src/song_plan.h"
#include "check.h"

int main(void) {
  static const char manifest[] =
      "{\"Entries\": {\"0A\": {\"Attributes\": {\"ArrangementName\": \"Lead\", \"MaxPhraseDifficulty\": 22,\n"
      " \"Phrases\": [{\"MaxDifficulty\": 0, \"Name\": \"default\"}], \"Tone_A\": \"song_clean\", \"Tone_B\": \"Song_Dist\",\n"
      " \"Tone_Base\": \"song_clean\", \"Tone_C\": \"\", \"Tone_D\": \"\", \"Tone_Multiplayer\": \"\",\n"
      " \"Tones\": [\n"
      "  {\"GearList\": {\"Rack1\": {\"Type\": \"Racks\", \"KnobValues\": {\"Rack_Test_Freq\": 0.900005, \"Rack_Test_Q\": 0},\n"
      "     \"Key\": \"Rack_Test\"}, \"Amp\": {\"Type\": \"Amps\", \"KnobValues\": {\"Amp_Test_Gain\": 1, \"Amp_Test_Bass\": 22},\n"
      "     \"Key\": \"Amp_Test\"}, \"Cabinet\": {\"Type\": \"Cabinets\", \"KnobValues\": {}, \"Key\": \"Cab_Test\"}},\n"
      "   \"ToneDescriptors\": [\"$[1]CLEAN\"], \"NameSeparator\": \" - \", \"IsCustom\": true, \"Volume\": \"-24.750\",\n"
      "   \"Key\": \"song_clean\", \"Name\": \"A \\\"clean\\\" one\", \"SortOrder\": 22},\n"
      "  {\"GearList\": {\"PrePedal1\": {\"KnobValues\": {\"Pedal_Test_63\": -3, \"Other_Knob\": 5}, \"Key\": \"Pedal_Test\"},\n"
      "     \"Amp\": {\"KnobValues\": {\"Amp_Test_Gain\": 79}, \"Key\": \"Amp_Test\"}},\n"
      "   \"Volume\": -19.4793, \"Key\": \"song_dist\", \"Name\": \"dist\"}],\n"
      " \"Tuning\": {\"string0\": 0}}}}}";
  static song_plan p;
  CHECK(song_plan_parse(manifest, &p));
  CHECK(p.max_difficulty == 22);
  CHECK(!strcmp(p.gear[0], " Rack_Test_Freq=0.900005 Rack_Test_Q=0 Amp_Test_Gain=1 Amp_Test_Bass=22 Meter_Tone_Balance_Return=-24.75"));
  CHECK(!strcmp(p.gear[1], p.gear[0]));
  // a slot's name in another case, a knob that is no gear's left out, a volume written as a number
  CHECK(!strcmp(p.gear[2], " Pedal_Test_63=-3 Amp_Test_Gain=79 Meter_Tone_Balance_Return=-19.4793"));
  CHECK(!*p.gear[3] && !*p.gear[4]);
  CHECK(!song_plan_parse("{\"Entries\": {}}", &p) && p.max_difficulty == -1);
  CHECK(!song_plan_parse("{\"Tones\": [{\"Key\": \"a\", \"Volume\": \"1\"", &p));  // cut short
  CHECK(!song_plan_parse("{\"Tone_Base\": \"missing\", \"Tones\": [{\"Key\": \"other\", \"Volume\": \"1\"}]}", &p));
  return check_done("test_song_plan");
}
