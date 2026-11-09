// rsmods_cache.c: Custom Tunings' and the default tones' edits of the game's files, and a profile's tones, on files
// shaped as the game's are
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "rsmods_cache.h"

static const char TUNINGS[] =
    "{ \"Static\" : \n{ \n\t\"Tuner\" :\n\t{ \n\t\t\"Entries\" :\n\t\t[\n\t\t\t{ \"Name\" : \"EStandard\", "
    "\"UIName\" : \"$[-1]E Standard\" }\n\t\t]\n\t},\n\n"
    "\t\"TuningDefinitions\":\n\t{\n"
    "\t\t\"EStandard\" :\n\t\t{\n\t\t\t\"UIName\" : \"$[17874]E Standard\",\n\t\t\t\"Strings\": \n\t\t\t{\n"
    "\t\t\t  \"string0\": 0,\n\t\t\t  \"string1\": 0,\n\t\t\t  \"string2\": 0,\n\t\t\t  \"string3\": 0,\n"
    "\t\t\t  \"string4\": 0,\n\t\t\t  \"string5\": 0\n\t\t\t}\n\t\t},\n"
    "\t\t\"EbStandard\" : { \"UIName\" : \"$[36578]Eb Standard\", \"Strings\": { \"string0\": -1, \"string1\": -1, "
    "\"string2\": -1, \"string3\": -1, \"string4\": -1, \"string5\": -1 } },\n"
    "\t\t\"DADGAD\" : { \"UIName\" : \"$[23458]DADGAD\", \"Strings\": { \"string0\": -2, \"string1\": 0, "
    "\"string2\": 0, \"string3\": 0, \"string4\": -2, \"string5\": -2 } }\n"
    "\t}\n}\n}\n";

static int count(const char *s, const char *what) {
  int n = 0;
  for (const char *p = s; (p = strstr(p, what)); p += strlen(what)) n++;
  return n;
}

static void test_tunings(void) {
  rsm_tuning t[8];
  CHECK(rsm_tuning_parse("  B Standard ", "-5 -5 -5 -5 -5 -5", &t[0]) && !strcmp(t[0].name, "B Standard"));
  CHECK(t[0].strings[0] == -5 && t[0].strings[5] == -5);
  CHECK(rsm_tuning_parse("Eb \"Std\"", "-1,-1, -1 ,-1,-1,-1", &t[1]) && t[1].strings[3] == -1);
  CHECK(rsm_tuning_parse("dadgad", "1 1 1 1 1 1", &t[2]));  // the game's key, in another case
  CHECK(rsm_tuning_parse("Open C", "-4 -2 -2 0 1 0 ", &t[3]) && t[3].strings[4] == 1);
  CHECK(rsm_tuning_parse("Twice", "-5 -5 -5 -5 -5 -5", &t[4]));  // B Standard's strings again
  rsm_tuning bad;
  CHECK(!rsm_tuning_parse("Short", "1 2 3", &bad));
  CHECK(!rsm_tuning_parse("Long", "1 2 3 4 5 6 7", &bad));
  CHECK(!rsm_tuning_parse("Words", "a b c d e f", &bad));
  CHECK(!rsm_tuning_parse("Far", "-30 0 0 0 0 0", &bad));
  CHECK(!rsm_tuning_parse("   ", "0 0 0 0 0 0", &bad));

  char *out = rsm_tunings_edit(TUNINGS, t, 5);
  CHECK(out != NULL);
  if (!out) return;
  CHECK(strstr(out, "\"EStandard\" :\n\t\t{\n\t\t\t\"UIName\" : \"$[17874]E Standard\"") != NULL);  // as it was
  CHECK(!strstr(out, "$[36578]Eb Standard"));  // the same strings as Eb "Std"
  CHECK(!strstr(out, "$[23458]DADGAD"));       // the same key as dadgad
  CHECK(strstr(out, "\"B Standard\" : { \"UIName\" : \"$[-1]B Standard\", \"Strings\" : { \"string0\" : -5,") != NULL);
  CHECK(strstr(out, "\"Eb \\\"Std\\\"\" : { \"UIName\" : \"$[-1]Eb \\\"Std\\\"\"") != NULL);
  CHECK(strstr(out, "\"string4\" : 1, \"string5\" : 0 } }") != NULL);  // Open C
  CHECK(!strstr(out, "Twice"));
  CHECK(count(out, "\"UIName\"") == 6);  // the tuner's entry, E Standard and the four new
  CHECK(strstr(out, "\"Entries\"") != NULL && strstr(out, "\n}\n}\n") != NULL);
  // the result reads as the file did: edited again with nothing, the same text
  char *again = rsm_tunings_edit(out, NULL, 0);
  CHECK(again && count(again, "\"UIName\"") == 6 && strstr(again, "$[-1]Open C"));
  free(again);
  free(out);

  // with ids from the localization, the names are looked up by them
  t[0].id = 37500;
  out = rsm_tunings_edit(TUNINGS, t, 1);
  CHECK(out && strstr(out, "\"B Standard\" : { \"UIName\" : \"$[37500]B Standard\""));
  free(out);

  CHECK(rsm_tunings_edit("{ \"Static\" : { \"Tuner\" : {} } }", t, 1) == NULL);
  CHECK(rsm_tunings_edit("{ \"Static\" : { \"TuningDefinitions\" : { \"X\" : { ", t, 1) == NULL);
  CHECK(rsm_tunings_edit("", t, 1) == NULL);
}

static const char CSV[] =
    "5857,Invite a Friend,,,,,\xe3\x83\x95,,\r\n"
    "37381,This song is in [1] tuning{C} or so.,Fr,Es,It,De,Ja,En,\r\n"
    "37500,Taken already,,,,,,,\r\n"
    "37502,Taken too,,,,,,,\r\n";

static void test_localization(void) {
  rsm_tuning t[3];
  CHECK(rsm_tuning_parse("B Standard", "-5 -5 -5 -5 -5 -5", &t[0]) && t[0].id == 0);
  CHECK(rsm_tuning_parse("Drop A, low", "-7 -5 -5 -5 -5 -5", &t[1]));
  CHECK(rsm_tuning_parse("Open C", "-4 -2 -2 0 1 0", &t[2]));
  size_t n = 0;
  char *out = rsm_localization_add(CSV, sizeof CSV - 1, t, 3, &n);
  CHECK(out && n == strlen(out));
  if (!out) return;
  CHECK(t[0].id == 37501 && t[1].id == 37503 && t[2].id == 37504);  // past the ids the file has
  CHECK(!strncmp(out, CSV, sizeof CSV - 1));                         // the file's lines as they were
  CHECK(!strcmp(out + sizeof CSV - 1,
                "37501,B Standard,B Standard,B Standard,B Standard,B Standard,B Standard,B Standard,\r\n"
                "37503,Drop A{C} low,Drop A{C} low,Drop A{C} low,Drop A{C} low,Drop A{C} low,Drop A{C} low,"
                "Drop A{C} low,\r\n"
                "37504,Open C,Open C,Open C,Open C,Open C,Open C,Open C,\r\n"));
  free(out);
  // a file without its last line's end gets one first
  out = rsm_localization_add("1,a,,,,,,,", 10, t, 1, &n);
  CHECK(out && !strcmp(out, "1,a,,,,,,,\r\n37500,B Standard,B Standard,B Standard,B Standard,B Standard,B Standard,"
                            "B Standard,\r\n"));
  free(out);
}

static const char PROFILE[] =
    "{\"Achievements\":[1,2],\"CustomTones\":[null,"
    "{\"GearList\":{\"Amp\":{\"Type\":\"Amps\",\"KnobValues\":{\"Amp_X_Gain\":50.0},\"Key\":\"Amp_X\"},"
    "\"Cabinet\":{\"Key\":\"Cab_X\"}},\"IsCustom\":true,\"Key\":\"crunch\",\"Name\":\"My Crunch\",\"SortOrder\":0.0,"
    "\"Volume\":\"-12.000000\"},"
    "{\"GearList\":{\"Amp\":{\"Key\":\"Bass_Amp_Y\",\"Note\":\"a } in a string\"}},\"Name\":\"L\\u00e4hde \\\"B\\\"\","
    "\"Volume\":\"-20.5\"},"
    "{\"Name\":\"No gear\"},"
    "{\"GearList\":{},\"Name\":\"\"}"
    "],\"Tones\":[null,null]}";

static const char TONES[] =
    "{\n    \"Static\": {\n        \"ToneManager\": {\n            \"Tones\": [\n"
    "\t\t\t\t{\n\t\t\t\t\t\"GearList\" : {\n\t\t\t\t\t\t\"Amp\" : { \"Key\" : \"Amp_OrangeAD50\" }\n\t\t\t\t\t},\n"
    "\t\t\t\t\t\"Volume\" : \"-18.654076\",\n\t\t\t\t\t\"Key\" : \"ToneDefault\",\n\t\t\t\t\t\"Name\" : \"$[36465]Guitar\"\n"
    "\t\t\t\t},\n"
    "\t\t\t\t{ \"GearList\" : { \"Amp\" : { \"Key\" : \"Amp_MarshallPlexi\" } }, \"Volume\" : \"-17\", "
    "\"Key\" : \"ToneDefault_Lead\" },\n"
    "\t\t\t\t{ \"Key\" : \"gc_ns_01\", \"Name\" : \"Ninja Slide N\", \"GearList\" : { \"Amp\" : { \"Key\" : \"Amp_TW26\" } } }\n"
    "            ],\n            \"ScoreAttackAmp\": \"Amp_X\"\n        }\n    }\n}\n";

static void test_tones(void) {
  rsm_tone t[8];
  int n = rsm_profile_tones(PROFILE, t, 8);
  CHECK(n == 2);  // not the null, nor the one without gear or a name
  CHECK(!strcmp(t[0].name, "My Crunch") && !t[0].bass);
  CHECK(!strncmp(t[0].gear, "{\"Amp\":", 7) && t[0].gear[t[0].gear_len - 1] == '}');
  CHECK(t[0].volume_len == strlen("\"-12.000000\"") && !strncmp(t[0].volume, "\"-12.000000\"", t[0].volume_len));
  CHECK(!strcmp(t[1].name, "L\xc3\xa4hde \"B\"") && t[1].bass);
  CHECK(!strncmp(t[1].gear + t[1].gear_len - 3, "\"}}", 3));  // past the } in the string
  CHECK(rsm_profile_tones("{\"Tones\":[]}", t, 8) == 0);
  CHECK(rsm_profile_tones(PROFILE, t, 1) == 1);

  n = rsm_profile_tones(PROFILE, t, 8);
  rsm_tone_change c[] = {{"ToneDefault_Lead", &t[0]}, {"gc_ns_01", &t[1]}, {"NoSuchTone", &t[0]}};
  int applied = -1;
  char *out = rsm_tones_edit(TONES, c, 3, &applied);
  CHECK(out != NULL && applied == 2);
  if (!out) return;
  CHECK(strstr(out, "\"Amp_OrangeAD50\"") && strstr(out, "\"-18.654076\""));  // ToneDefault as it was
  CHECK(!strstr(out, "Amp_MarshallPlexi") && !strstr(out, "Amp_TW26"));
  CHECK(strstr(out, "{ \"GearList\" : {\"Amp\":{\"Type\":\"Amps\"") != NULL);
  CHECK(strstr(out, "\"Volume\" : \"-12.000000\", \"Key\" : \"ToneDefault_Lead\"") != NULL);
  CHECK(strstr(out, "\"Key\" : \"gc_ns_01\", \"Name\" : \"Ninja Slide N\", \"GearList\" : {\"Amp\":{\"Key\":\"Bass_Amp_Y\"") != NULL);
  CHECK(!strstr(out, "-20.5"));  // Ninja Slide N's tone has no Volume to take it
  CHECK(strstr(out, "\"ScoreAttackAmp\": \"Amp_X\"") != NULL);
  free(out);
  out = rsm_tones_edit(TONES, c + 2, 1, &applied);
  CHECK(out && applied == 0 && !strcmp(out, TONES));
  free(out);
  CHECK(rsm_tones_edit("{ \"Static\" : {} }", c, 1, &applied) == NULL);
}

int main(void) {
  test_tunings();
  test_localization();
  test_tones();
  return check_done("cache edits");
}
