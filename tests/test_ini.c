// RSMods.ini: reading, writing and key names (src/rsmods_ini.c), in a folder of its own
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "check.h"
#include "rsmods.h"

void rsm_log(const char *fmt, ...) { (void)fmt; }

static void write_ini(const char *text) {
  FILE *f = fopen("RSMods.ini", "w");
  fputs(text, f);
  fclose(f);
  sleep(1);  // the reader looks once a second
  rsm_ini_poll();
}

static char *read_ini(void) {
  static char text[65536];
  FILE *f = fopen("RSMods.ini", "r");
  text[fread(text, 1, sizeof text - 1, f)] = 0;
  fclose(f);
  return text;
}

int main(void) {
  char dir[] = "/tmp/rsmods-test-XXXXXX";
  CHECK(mkdtemp(dir) && !chdir(dir));

  // a first start writes the defaults, every mod off
  CHECK(rsm_ini_poll());
  CHECK(strstr(read_ini(), "[Toggle Switches]") != NULL);
  CHECK(!rsm_on("ToneHost") && !rsm_on("Sunlight") && !rsm_on("CameraDirector"));
  CHECK(rsm_long("SunlightMode", -1) == 1);
  CHECK(!rsm_on("CustomSongListTitles") && !strcmp(rsm_get("SongListTitles", "SongListTitle_20"), ""));
  CHECK(!rsm_on("CustomTunings") && !rsm_on("DefaultTones") && !strcmp(rsm_get("Default Tones", "Lead"), ""));

  // a section's lines in order (a '#' inside a key is no comment); past the last, none
  const char *k, *v;
  CHECK(rsm_ini_entry("Custom Tunings", 0, &k, &v) && !strcmp(k, "F Standard") && !strcmp(v, "1 1 1 1 1 1"));
  CHECK(rsm_ini_entry("custom tunings", 1, &k, &v) && !strcmp(k, "C# Drop B") && !strcmp(v, "-5 -3 -3 -3 -3 -3"));
  CHECK(rsm_ini_entry("Custom Tunings", 8, &k, &v) && !strcmp(k, "Open C"));
  CHECK(!rsm_ini_entry("Custom Tunings", 9, &k, &v));
  CHECK(!rsm_ini_entry("No Such Section", 0, &k, &v));

  // what the file lacks comes from the defaults; names ignore case; the last of a key wins
  write_ini("[toggle switches]\nsunlight = ON\nToneHost = off\nToneHost = on\n[Mod Settings]\nCameraZoom = 120\nBad = x\n");
  CHECK(rsm_on("Sunlight") && rsm_on("ToneHost"));
  CHECK(rsm_long("CameraZoom", 100) == 120 && rsm_long("CameraHeight", 7) == 0);
  CHECK(rsm_long("Bad", 7) == 7 && rsm_long("Missing", 7) == 7);
  CHECK(!strcmp(rsm_get("Keybinds", "SunlightKey"), "G"));
  CHECK(rsm_get("Keybinds", "Missing") == NULL);

  // a key's code: letters and VK_ names, any case; unknown or empty is none
  write_ini("[Keybinds]\nRewindKey = VK_F5\nLoopStartKey = vk_next\nLoopEndKey = nope\nRRSpeedKey = \n");
  CHECK(rsm_key("RewindKey") == 0x60 && rsm_key("LoopStartKey") == 0x79);
  CHECK(rsm_key("LoopEndKey") == -1 && rsm_key("RRSpeedKey") == -1);
  CHECK(rsm_key("SunlightKey") == 0x05);

  // writing keeps the rest of the file
  write_ini("; a comment\n[Mod Settings]\nSunlightMode = 1\nOther = 2\n\n[Keybinds]\nRewindKey = Z\n");
  rsm_set("Mod Settings", "SunlightMode", "2");
  CHECK(!strcmp(read_ini(), "; a comment\n[Mod Settings]\nSunlightMode = 2\nOther = 2\n\n[Keybinds]\nRewindKey = Z\n"));

  // a new key goes in its section, a new section at the end
  rsm_set("Mod Settings", "CameraZoom", "90");
  CHECK(strstr(read_ini(), "CameraZoom = 90") < strstr(read_ini(), "[Keybinds]"));
  rsm_set("Highway Colors", "CustomHighwayColors", "on");
  CHECK(strstr(read_ini(), "\n[Highway Colors]\nCustomHighwayColors = on\n") != NULL);

  // a key written twice: every line changes, as the last is what is read
  write_ini("[Mod Settings]\nSunlightMode = 1\nSunlightMode = 0\n");
  rsm_set("Mod Settings", "SunlightMode", "2");
  sleep(1);
  rsm_ini_poll();
  CHECK(rsm_long("SunlightMode", -1) == 2);

  // the same key name in another section is left alone
  write_ini("[Keybinds]\nX = 1\n[Mod Settings]\nX = 2\n");
  rsm_set("Mod Settings", "X", "3");
  CHECK(strstr(read_ini(), "[Keybinds]\nX = 1\n") != NULL);

  return check_done("rsmods_ini");
}
