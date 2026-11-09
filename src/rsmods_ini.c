// RSMods settings: RSMods.ini in the game folder (the working directory), the same sections and
// keys as RSMods on Windows. Missing, it is written with the defaults of the mods this build has.
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#include "rsmods.h"

#define INI_PATH "RSMods.ini"

static const char DEFAULT_INI[] =
    "; RSMods for RS Re-ARMed. The same settings as RSMods on Windows (only the mods listed here exist on macOS).\n"
    "; Changes are picked up while the game runs. Keys are letters, digits, F1-F20 or Windows VK_ names.\n"
    "\n"
    "[Toggle Switches]\n"
    "; Riff Repeater speeds above 100%: in a song, the speed key raises the speed by RRSpeedInterval percent,\n"
    "; Ctrl + the speed key lowers it (6.25% to 400%)\n"
    "RRSpeedAboveOneHundred = off\n"
    "; Loops in Learn a Song and Non-Stop Play: the loop start key and loop end key set the loop at the song\n"
    "; position (Ctrl + start clears it, Ctrl + end clears its end); LoopingLeadUp ms of lead before the start\n"
    "AllowLooping = off\n"
    "; The rewind key moves the song back RewindBy + RewindLeadup ms\n"
    "AllowRewind = off\n"
    "; No song previews in the song lists\n"
    "SongPreviews = off\n"
    "; The song timer at the top right: in every song (ShowSongTimerWhen = automatic) or shown and hidden with\n"
    "; the song timer key (manual; the key works in both)\n"
    "ShowSongTimer = off\n"
    "ShowSongTimerWhen = manual\n"
    "; Volume keys ([Audio Keybindings]): each raises its channel by VolumeControlInterval, Ctrl + key lowers it;\n"
    "; hold the mixer key to see every channel. The mute keys work without this.\n"
    "VolumeControl = off\n"
    "; Enter the last used profile (the top of the list) at startup, or the one named in ProfileToLoad\n"
    "ForceProfileLoad = off\n"
    "ProfileToLoad = \n"
    "; Look for new songs without a restart: off, manual (the key) or automatic (when files in dlc/ change)\n"
    "ForceReEnumeration = off\n"
    "; Save the score screens as PNGs in ~/Pictures/RS Re-ARMed (8 s after they open)\n"
    "ScreenShotScores = off\n"
    "; No popup when two Real Tone cables are connected\n"
    "BypassTwoRTCMessageBox = off\n"
    "; Keep playing, with sound, while another app is active\n"
    "AllowAudioInBackground = off\n"
    "; The same in songs only: another app coming to the front doesn't pause the song\n"
    "PreventMidSongPause = off\n"
    "; A tone change that never ends doesn't leave the game on the clean tone (needs RS Re-ARMed 1.11.0)\n"
    "FixBrokenTones = off\n"
    "; Riff Repeater: the slider's percent is the song speed (default: 68% plays at about 50%)\n"
    "LinearRiffRepeater = off\n"
    "; The venue behind the highway black (for streams): ToggleLoftWhen = manual (the key), song or startup\n"
    "ToggleLoft = off\n"
    "ToggleLoftWhen = manual\n"
    "; No headstock: RemoveHeadstockWhen = song or startup\n"
    "Headstock = off\n"
    "RemoveHeadstockWhen = song\n"
    "; No lyrics: RemoveLyricsWhen = manual (the key) or startup\n"
    "Lyrics = off\n"
    "RemoveLyricsWhen = manual\n"
    "; No skyline (the phrase bars at the top): ToggleSkylineWhen = song or startup\n"
    "Skyline = off\n"
    "ToggleSkylineWhen = song\n"
    "; No fret wires / no inlays / no lane markers on the highway / no fingerprints (a chord's hand indicators)\n"
    "Fretless = off\n"
    "Inlays = off\n"
    "LaneMarkers = off\n"
    "RemoveFingerprints = off\n"
    "; Only the wall behind the stage black (the amps stay); with ToggleLoft, the loft's wall in lessons too\n"
    "GreenScreenWall = off\n"
    "; Non-Stop Play waits CustomNSPTimeLimit ms between songs (2000 to 60000) instead of 10.99 s\n"
    "UseCustomNSPTimer = off\n"
    "; Move the game's window, at startup, to the screen whose top left corner is SecondaryMonitorXPosition,\n"
    "; SecondaryMonitorYPosition (points, from the top left of the main screen, down)\n"
    "SecondaryMonitor = off\n"
    "; Extended range colors in songs whose lowest string is tuned at or below ExtendedRangeModeAt semitones from E\n"
    "; (-5: B): CustomStringColors = 1 gives ZZ's colors, 2 your colorblind colors (string0_CB and so on).\n"
    "; ExtendedRangeDropTuning: a drop tuning counts from that tuning, not two below. ExtendedRangeFixBassTuning: a\n"
    "; bass charted with its top two strings left at E counts by its four. The key turns it on or off for a song.\n"
    "ExtendedRange = off\n"
    "ExtendedRangeDropTuning = off\n"
    "ExtendedRangeFixBassTuning = off\n"
    "; The note you play, on screen (heard from the guitar's input)\n"
    "ShowCurrentNoteOnScreen = off\n"
    "; Play notes to press keys in the menus: the notes are in [Guitar Speak]\n"
    "GuitarSpeak = off\n"
    "; In a song, the notes hit out of the notes played so far, at the top right\n"
    "DisplayCurrentAccuracy = off\n"
    "; The song lists get your own names, from [SongListTitles]\n"
    "CustomSongListTitles = off\n"
    "; Every profile gets NumberOfSongLists song lists (6 to 20) when the game starts: empty ones after the last, or\n"
    "; the last ones removed while they are empty (a list with songs stays)\n"
    "MoreSongLists = off\n"
    "; The game knows the tunings of [Custom Tunings] by name, where it showed CUSTOM TUNING (from the next start)\n"
    "CustomTunings = off\n"
    "; The game's default tones (slot 1 of the tone stick, and each Guitarcade game's) are your saved tones named in\n"
    "; [Default Tones] (from the next start)\n"
    "DefaultTones = off\n"
    "; Keep an input device at OverrideInputVolume percent (17 is the game's default). The device: part of its\n"
    "; name as macOS shows it (Audio MIDI Setup), e.g. Rocksmith; empty = the default input. Several devices:\n"
    "; separated by ';', each optionally = its own percent: Rocksmith USB Guitar Adapter; Rocksmith Game (OBS) = 100\n"
    "OverrideInputVolumeEnabled = off\n"
    "OverrideInputVolumeDevice = \n"
    "; Your own view of the highway: in a song the zoom, height, side and tilt keys change it, Ctrl + key the other\n"
    "; way, the reset key goes back to the game's view. It is kept in CameraZoom (60 to 160), CameraHeight (-10 to\n"
    "; 20), CameraSide (-20 to 20) and CameraTilt (-15 to 15).\n"
    "CameraDirector = off\n"
    "; Your own plug-in in place of the game's tones: rs-tonehost plays the guitar and follows the game's tones, and\n"
    "; the game's guitar is silent while it runs. Without it the game keeps its tones.\n"
    "ToneHost = off\n"
    "; For a screen in the sun: the sunlight key goes through off, bright and daylight (a white highway with dark\n"
    "; notes, in songs), kept in SunlightMode (0, 1 or 2). In a song the venue goes too.\n"
    "Sunlight = off\n"
    "; CustomStringColors = 2 colors the strings and notes with string0_N and so on in [String Colors] (1: ZZ's colors\n"
    "; in extended range songs); SeparateNoteColors = on with SeparateNoteColorsMode = 2 gives the notes note0_N and so on.\n"
    "CustomStringColors = 0\n"
    "SeparateNoteColors = off\n"
    "; The notes cycle through the colors while the rainbow notes key has it on\n"
    "RainbowNotes = off\n"
    "; The strings do, with the rainbow strings key\n"
    "RainbowStrings = off\n"
    "\n"
    "[Mod Settings]\n"
    "RRSpeedInterval = 2\n"
    "LoopingLeadUp = 0\n"
    "RewindBy = 5000\n"
    "RewindLeadup = 2000\n"
    "VolumeControlInterval = 5\n"
    "CheckForNewSongsInterval = 5000\n"
    "OverrideInputVolume = 17\n"
    "CameraZoom = 100\n"
    "CameraHeight = 0\n"
    "CameraSide = 0\n"
    "CameraTilt = 0\n"
    "SunlightMode = 1\n"
    "SeparateNoteColorsMode = 0\n"
    "CustomNSPTimeLimit = 10000\n"
    "SecondaryMonitorXPosition = 0\n"
    "SecondaryMonitorYPosition = 0\n"
    "ExtendedRangeModeAt = -5\n"
    "NumberOfSongLists = 6\n"
    "\n"
    "[Guitar Speak]\n"
    "; The MIDI note (0-96; E3, the low E's 12th fret, is 52) that presses each key; 0 = none. The close note stops\n"
    "; GuitarSpeak and starts it again. In the tuners only with GuitarSpeakWhileTuning = on.\n"
    "GuitarSpeakDeleteWhen = 0\n"
    "GuitarSpeakSpaceWhen = 0\n"
    "GuitarSpeakEnterWhen = 0\n"
    "GuitarSpeakTabWhen = 0\n"
    "GuitarSpeakPGUPWhen = 0\n"
    "GuitarSpeakPGDNWhen = 0\n"
    "GuitarSpeakUPWhen = 0\n"
    "GuitarSpeakDNWhen = 0\n"
    "GuitarSpeakESCWhen = 0\n"
    "GuitarSpeakCloseWhen = 0\n"
    "GuitarSpeakOBracketWhen = 0\n"
    "GuitarSpeakCBracketWhen = 0\n"
    "GuitarSpeakTildeaWhen = 0\n"
    "GuitarSpeakForSlashWhen = 0\n"
    "GuitarSpeakAltWhen = 0\n"
    "GuitarSpeakWhileTuning = off\n"
    "\n"
    "[SongListTitles]\n"
    "; With CustomSongListTitles = on: the name of each of the profile's song lists (7 to 20: lists added to a profile\n"
    "; by MoreSongLists or RSMods' app on Windows). Empty = the game's name.\n"
    "SongListTitle_1 = \n"
    "SongListTitle_2 = \n"
    "SongListTitle_3 = \n"
    "SongListTitle_4 = \n"
    "SongListTitle_5 = \n"
    "SongListTitle_6 = \n"
    "SongListTitle_7 = \n"
    "SongListTitle_8 = \n"
    "SongListTitle_9 = \n"
    "SongListTitle_10 = \n"
    "SongListTitle_11 = \n"
    "SongListTitle_12 = \n"
    "SongListTitle_13 = \n"
    "SongListTitle_14 = \n"
    "SongListTitle_15 = \n"
    "SongListTitle_16 = \n"
    "SongListTitle_17 = \n"
    "SongListTitle_18 = \n"
    "SongListTitle_19 = \n"
    "SongListTitle_20 = \n"
    "\n"
    "[Custom Tunings]\n"
    "; With CustomTunings = on: a tuning's name = its strings' semitones from E standard, low string first. One with the\n"
    "; same strings as one of the game's tunings takes that one's place.\n"
    "F Standard = 1 1 1 1 1 1\n"
    "C# Drop B = -5 -3 -3 -3 -3 -3\n"
    "C Drop Bb = -6 -4 -4 -4 -4 -4\n"
    "B Standard = -5 -5 -5 -5 -5 -5\n"
    "B Drop A = -7 -5 -5 -5 -5 -5\n"
    "Bb Standard = -6 -6 -6 -6 -6 -6\n"
    "A Standard = -7 -7 -7 -7 -7 -7\n"
    "Double Drop D = -2 0 0 0 0 -2\n"
    "Open C = -4 -2 -2 0 1 0\n"
    "\n"
    "[Default Tones]\n"
    "; With DefaultTones = on: the name of a tone saved in one of your profiles (the tone designer's Save), for each of\n"
    "; the game's tones; empty keeps the game's. The Guitarcade games' are by their tones' names.\n"
    "Rhythm = \n"
    "Lead = \n"
    "Bass = \n"
    "TempleOfBends = \n"
    "ScaleWarriors = \n"
    "StringSkipSaloon = \n"
    "ScaleRacer = \n"
    "StarChordsLaser = \n"
    "NinjaSlideN = \n"
    "HurtlinHurdles = \n"
    "HarmonicHeist = \n"
    "DucksRedux = \n"
    "RainbowLaser = \n"
    "GoneWailin = \n"
    "\n"
    "[GUI Settings]\n"
    "; Back up the profiles to Profile_AutoBackups in the game folder while the game runs (when they changed, at most\n"
    "; every 10 minutes), and when the app opens; NumberOfBackups: how many of the app's it keeps\n"
    "BackupProfile = on\n"
    "NumberOfBackups = 50\n"
    "\n"
    "[Highway Colors]\n"
    "; RRGGBB; a color left empty stays the game's. Numbered lanes and unnumbered ones go together. (The last key is spelled so on Windows too.)\n"
    "CustomHighwayColors = off\n"
    "CustomHighwayNumbered = \n"
    "CustomHighwayUnNumbered = \n"
    "CustomHighwayGutter = \n"
    "CustomFretNubmers = \n"
    "\n"
    "[Keybinds]\n"
    "RRSpeedKey = R\n"
    "LoopStartKey = Y\n"
    "LoopEndKey = U\n"
    "RewindKey = Z\n"
    "ShowSongTimerKey = I\n"
    "ForceReEnumerationKey = F\n"
    "ToggleLoftKey = T\n"
    "RemoveLyricsKey = L\n"
    "CameraZoomKey = K\n"
    "CameraHeightKey = H\n"
    "CameraSideKey = D\n"
    "CameraTiltKey = M\n"
    "CameraResetKey = J\n"
    "SunlightKey = G\n"
    "RainbowNotesKey = N\n"
    "RainbowStringsKey = V\n"
    "ToggleExtendedRangeKey = E\n"
    "\n"
    "[Audio Keybindings]\n"
    "MasterVolumeKey = 5\n"
    "SongVolumeKey = 6\n"
    "Player1VolumeKey = 7\n"
    "Player2VolumeKey = 8\n"
    "MicrophoneVolumeKey = 9\n"
    "VoiceOverVolumeKey = 0\n"
    "SFXVolumeKey = S\n"
    "DisplayMixerKey = P\n"
    "MutePlayer1Key = X\n"
    "MutePlayer2Key = C\n"
    "ChangedSelectedVolumeKey = B\n";

typedef struct {
  char *section, *key, *value;
} entry;
static entry *g_e;
static int g_n;
static time_t g_mtime;
static long g_mtime_ns;
static int g_loaded;
static time_t g_checked;

// Defaults where RSMods.ini lacks a key (Settings.cpp of RSMods)
static const struct {
  const char *section, *key, *value;
} DEFAULTS[] = {
    {"Toggle Switches", "RRSpeedAboveOneHundred", "off"},
    {"Mod Settings", "RRSpeedInterval", "2"},
    {"Keybinds", "RRSpeedKey", "R"},
    {"Toggle Switches", "AllowLooping", "off"},
    {"Toggle Switches", "AllowRewind", "off"},
    {"Toggle Switches", "SongPreviews", "off"},
    {"Toggle Switches", "ShowSongTimer", "off"},
    {"Toggle Switches", "ShowSongTimerWhen", "manual"},
    {"Keybinds", "ShowSongTimerKey", "I"},
    {"Toggle Switches", "VolumeControl", "off"},
    {"Toggle Switches", "ForceProfileLoad", "off"},
    {"Toggle Switches", "ForceReEnumeration", "off"},
    {"Toggle Switches", "OverrideInputVolumeEnabled", "off"},
    {"Toggle Switches", "ScreenShotScores", "off"},
    {"Toggle Switches", "BypassTwoRTCMessageBox", "off"},
    {"Toggle Switches", "AllowAudioInBackground", "off"},
    {"Toggle Switches", "LinearRiffRepeater", "off"},
    {"Toggle Switches", "ToggleLoft", "off"},
    {"Toggle Switches", "ToggleLoftWhen", "manual"},
    {"Keybinds", "ToggleLoftKey", "T"},
    {"Toggle Switches", "Headstock", "off"},
    {"Toggle Switches", "RemoveHeadstockWhen", "song"},
    {"Toggle Switches", "Lyrics", "off"},
    {"Toggle Switches", "RemoveLyricsWhen", "manual"},
    {"Toggle Switches", "Skyline", "off"},
    {"Toggle Switches", "ToggleSkylineWhen", "song"},
    {"Toggle Switches", "Fretless", "off"},
    {"Toggle Switches", "Inlays", "off"},
    {"Toggle Switches", "LaneMarkers", "off"},
    {"Keybinds", "RemoveLyricsKey", "L"},
    {"Toggle Switches", "OverrideInputVolumeDevice", ""},
    {"Mod Settings", "OverrideInputVolume", "17"},
    {"Mod Settings", "CheckForNewSongsInterval", "5000"},
    {"Keybinds", "ForceReEnumerationKey", "F"},
    {"Mod Settings", "VolumeControlInterval", "5"},
    {"Audio Keybindings", "MasterVolumeKey", "5"},
    {"Audio Keybindings", "SongVolumeKey", "6"},
    {"Audio Keybindings", "Player1VolumeKey", "7"},
    {"Audio Keybindings", "Player2VolumeKey", "8"},
    {"Audio Keybindings", "MicrophoneVolumeKey", "9"},
    {"Audio Keybindings", "VoiceOverVolumeKey", "0"},
    {"Audio Keybindings", "SFXVolumeKey", "S"},
    {"Audio Keybindings", "DisplayMixerKey", "P"},
    {"Audio Keybindings", "MutePlayer1Key", "X"},
    {"Audio Keybindings", "MutePlayer2Key", "C"},
    {"Audio Keybindings", "ChangedSelectedVolumeKey", "B"},
    {"Mod Settings", "LoopingLeadUp", "0"},
    {"Mod Settings", "RewindBy", "5000"},
    {"Mod Settings", "RewindLeadup", "2000"},
    {"Keybinds", "LoopStartKey", "Y"},
    {"Keybinds", "LoopEndKey", "U"},
    {"Keybinds", "RewindKey", "Z"},
    {"Toggle Switches", "CameraDirector", "off"},
    {"Toggle Switches", "ToneHost", "off"},
    {"Toggle Switches", "Sunlight", "off"},
    {"Mod Settings", "SunlightMode", "1"},
    {"Keybinds", "SunlightKey", "G"},
    {"Mod Settings", "CameraZoom", "100"},
    {"Mod Settings", "CameraHeight", "0"},
    {"Mod Settings", "CameraSide", "0"},
    {"Mod Settings", "CameraTilt", "0"},
    {"Keybinds", "CameraZoomKey", "K"},
    {"Keybinds", "CameraHeightKey", "H"},
    {"Keybinds", "CameraSideKey", "D"},
    {"Keybinds", "CameraTiltKey", "M"},
    {"Keybinds", "CameraResetKey", "J"},
    {"Toggle Switches", "CustomStringColors", "0"},
    {"Toggle Switches", "SeparateNoteColors", "off"},
    {"Toggle Switches", "RainbowNotes", "off"},
    {"Toggle Switches", "RainbowStrings", "off"},
    {"Keybinds", "RainbowStringsKey", "V"},
    {"Mod Settings", "SeparateNoteColorsMode", "0"},
    {"Highway Colors", "CustomHighwayColors", "off"},
    {"Highway Colors", "CustomHighwayNumbered", ""},
    {"Highway Colors", "CustomHighwayUnNumbered", ""},
    {"Highway Colors", "CustomHighwayGutter", ""},
    {"Highway Colors", "CustomFretNubmers", ""},
    {"Keybinds", "RainbowNotesKey", "N"},
    {"Toggle Switches", "ProfileToLoad", ""},
    {"Toggle Switches", "RemoveFingerprints", "off"},
    {"Toggle Switches", "GreenScreenWall", "off"},
    {"Toggle Switches", "UseCustomNSPTimer", "off"},
    {"Mod Settings", "CustomNSPTimeLimit", "10000"},
    {"Toggle Switches", "SecondaryMonitor", "off"},
    {"Mod Settings", "SecondaryMonitorXPosition", "0"},
    {"Mod Settings", "SecondaryMonitorYPosition", "0"},
    {"Toggle Switches", "ExtendedRange", "off"},
    {"Toggle Switches", "ExtendedRangeDropTuning", "off"},
    {"Toggle Switches", "ExtendedRangeFixBassTuning", "off"},
    {"Mod Settings", "ExtendedRangeModeAt", "-5"},
    {"Keybinds", "ToggleExtendedRangeKey", "E"},
    {"Toggle Switches", "ShowCurrentNoteOnScreen", "off"},
    {"Toggle Switches", "GuitarSpeak", "off"},
    {"Toggle Switches", "DisplayCurrentAccuracy", "off"},
    {"Guitar Speak", "GuitarSpeakDeleteWhen", "0"},
    {"Guitar Speak", "GuitarSpeakSpaceWhen", "0"},
    {"Guitar Speak", "GuitarSpeakEnterWhen", "0"},
    {"Guitar Speak", "GuitarSpeakTabWhen", "0"},
    {"Guitar Speak", "GuitarSpeakPGUPWhen", "0"},
    {"Guitar Speak", "GuitarSpeakPGDNWhen", "0"},
    {"Guitar Speak", "GuitarSpeakUPWhen", "0"},
    {"Guitar Speak", "GuitarSpeakDNWhen", "0"},
    {"Guitar Speak", "GuitarSpeakESCWhen", "0"},
    {"Guitar Speak", "GuitarSpeakCloseWhen", "0"},
    {"Guitar Speak", "GuitarSpeakOBracketWhen", "0"},
    {"Guitar Speak", "GuitarSpeakCBracketWhen", "0"},
    {"Guitar Speak", "GuitarSpeakTildeaWhen", "0"},
    {"Guitar Speak", "GuitarSpeakForSlashWhen", "0"},
    {"Guitar Speak", "GuitarSpeakAltWhen", "0"},
    {"Guitar Speak", "GuitarSpeakWhileTuning", "off"},
    {"GUI Settings", "BackupProfile", "on"},
    {"GUI Settings", "NumberOfBackups", "50"},
    {"Toggle Switches", "PreventMidSongPause", "off"},
    {"Toggle Switches", "FixBrokenTones", "off"},
    {"Toggle Switches", "CustomSongListTitles", "off"},
    {"Toggle Switches", "MoreSongLists", "off"},
    {"Mod Settings", "NumberOfSongLists", "6"},
    {"Toggle Switches", "CustomTunings", "off"},
    {"Toggle Switches", "DefaultTones", "off"},
    {"Default Tones", "Rhythm", ""},
    {"Default Tones", "Lead", ""},
    {"Default Tones", "Bass", ""},
    {"Default Tones", "TempleOfBends", ""},
    {"Default Tones", "ScaleWarriors", ""},
    {"Default Tones", "StringSkipSaloon", ""},
    {"Default Tones", "ScaleRacer", ""},
    {"Default Tones", "StarChordsLaser", ""},
    {"Default Tones", "NinjaSlideN", ""},
    {"Default Tones", "HurtlinHurdles", ""},
    {"Default Tones", "HarmonicHeist", ""},
    {"Default Tones", "DucksRedux", ""},
    {"Default Tones", "RainbowLaser", ""},
    {"Default Tones", "GoneWailin", ""},
};

char *rsm_trim(char *s) {
  while (isspace((unsigned char)*s)) s++;
  char *e = s + strlen(s);
  while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
  return s;
}

static void clear(void) {
  for (int i = 0; i < g_n; i++) {
    free(g_e[i].section);
    free(g_e[i].key);
    free(g_e[i].value);
  }
  free(g_e);
  g_e = NULL;
  g_n = 0;
}

static void load(FILE *f) {
  clear();
  char line[1024], section[128] = "";
  int cap = 0;
  while (fgets(line, sizeof line, f)) {
    char *s = rsm_trim(line);
    if (!*s || *s == ';' || *s == '#') continue;
    if (*s == '[') {
      char *e = strchr(s, ']');
      if (e) *e = 0;
      snprintf(section, sizeof section, "%s", rsm_trim(s + 1));
      continue;
    }
    char *eq = strchr(s, '=');
    if (!eq) continue;
    *eq = 0;
    if (g_n == cap) {
      cap = cap ? cap * 2 : 64;
      entry *ne = realloc(g_e, (size_t)cap * sizeof *ne);
      if (!ne) return;
      g_e = ne;
    }
    g_e[g_n++] = (entry){strdup(section), strdup(rsm_trim(s)), strdup(rsm_trim(eq + 1))};
  }
}

int rsm_ini_poll(void) {
  time_t now = time(NULL);
  if (g_loaded && now == g_checked) return 0;
  g_checked = now;
  struct stat st;
  if (stat(INI_PATH, &st) != 0) {
    FILE *f = fopen(INI_PATH, "wx");
    if (f) {
      fputs(DEFAULT_INI, f);
      fclose(f);
      rsm_log("wrote a default %s", INI_PATH);
    }
    if (stat(INI_PATH, &st) != 0) {
      if (!g_loaded) clear();
      g_loaded = 1;
      return 0;
    }
  }
  if (g_loaded && st.st_mtimespec.tv_sec == g_mtime && st.st_mtimespec.tv_nsec == g_mtime_ns) return 0;
  FILE *f = fopen(INI_PATH, "r");
  if (!f) return 0;
  load(f);
  fclose(f);
  g_mtime = st.st_mtimespec.tv_sec;
  g_mtime_ns = st.st_mtimespec.tv_nsec;
  g_loaded = 1;
  return 1;
}

const char *rsm_get(const char *section, const char *key) {
  for (int i = g_n - 1; i >= 0; i--)
    if (!strcasecmp(g_e[i].section, section) && !strcasecmp(g_e[i].key, key)) return g_e[i].value;
  for (size_t i = 0; i < sizeof DEFAULTS / sizeof DEFAULTS[0]; i++)
    if (!strcasecmp(DEFAULTS[i].section, section) && !strcasecmp(DEFAULTS[i].key, key)) return DEFAULTS[i].value;
  return NULL;
}

// Replaces every line of the key, as the reader takes the last, or adds it at the end of its section (the section at the end of the file if it has none).
// The rest of the file stays as it is. Written to a new file first: the game may stop at any moment.
void rsm_set(const char *section, const char *key, const char *value) {
  FILE *in = fopen(INI_PATH, "r"), *out = fopen(INI_PATH ".new", "w");
  if (!out) {
    if (in) fclose(in);
    return;
  }
  char line[1024], cur[128] = "";
  int done = 0;
  while (in && fgets(line, sizeof line, in)) {
    char copy[sizeof line];
    snprintf(copy, sizeof copy, "%s", line);
    char *s = rsm_trim(copy);
    if (*s == '[') {
      if (!done && !strcasecmp(cur, section)) fprintf(out, "%s = %s\n", key, value), done = 1;
      char *e = strchr(s, ']');
      if (e) *e = 0;
      snprintf(cur, sizeof cur, "%s", rsm_trim(s + 1));
    } else if (*s != ';' && *s != '#' && !strcasecmp(cur, section)) {
      char *eq = strchr(s, '=');
      if (eq) {
        *eq = 0;
        if (!strcasecmp(rsm_trim(s), key)) {
          fprintf(out, "%s = %s\n", key, value);
          done = 1;
          continue;
        }
      }
    }
    fputs(line, out);
  }
  if (!done) {
    if (strcasecmp(cur, section)) fprintf(out, "\n[%s]\n", section);
    fprintf(out, "%s = %s\n", key, value);
  }
  if (in) fclose(in);
  fclose(out);
  rename(INI_PATH ".new", INI_PATH);
}

int rsm_ini_entry(const char *section, int i, const char **key, const char **value) {
  for (int k = 0; k < g_n; k++)
    if (!strcasecmp(g_e[k].section, section) && i-- == 0) {
      *key = g_e[k].key;
      *value = g_e[k].value;
      return 1;
    }
  return 0;
}

int rsm_on(const char *key) {
  const char *v = rsm_get("Toggle Switches", key);
  return v && !strcasecmp(v, "on");
}

int rsm_on_section(const char *section, const char *key) {
  const char *v = rsm_get(section, key);
  return v && !strcasecmp(v, "on");
}

long rsm_long(const char *key, long dflt) {
  const char *v = rsm_get("Mod Settings", key);
  if (!v || !*v) return dflt;
  char *end;
  long n = strtol(v, &end, 10);
  return end == v ? dflt : n;
}

// ------------------------------------------------------------------------------------------------
// Key names (RSMods writes letters and digits as themselves and other keys as Windows VK_ names) to
// macOS virtual key codes (HIToolbox Events.h)
// ------------------------------------------------------------------------------------------------
static const struct {
  const char *name;
  int code;
} KEYS[] = {
    {"A", 0x00}, {"S", 0x01}, {"D", 0x02}, {"F", 0x03}, {"H", 0x04}, {"G", 0x05}, {"Z", 0x06}, {"X", 0x07},
    {"C", 0x08}, {"V", 0x09}, {"B", 0x0b}, {"Q", 0x0c}, {"W", 0x0d}, {"E", 0x0e}, {"R", 0x0f}, {"Y", 0x10},
    {"T", 0x11}, {"1", 0x12}, {"2", 0x13}, {"3", 0x14}, {"4", 0x15}, {"6", 0x16}, {"5", 0x17}, {"9", 0x19},
    {"7", 0x1a}, {"8", 0x1c}, {"0", 0x1d}, {"O", 0x1f}, {"U", 0x20}, {"I", 0x22}, {"P", 0x23}, {"L", 0x25},
    {"J", 0x26}, {"K", 0x28}, {"N", 0x2d}, {"M", 0x2e},
    {"OEM_PLUS", 0x18}, {"OEM_MINUS", 0x1b}, {"OEM_6", 0x1e}, {"OEM_4", 0x21}, {"OEM_7", 0x27},
    {"OEM_1", 0x29}, {"OEM_5", 0x2a}, {"OEM_COMMA", 0x2b}, {"OEM_2", 0x2c}, {"OEM_PERIOD", 0x2f},
    {"OEM_3", 0x32},
    {"RETURN", 0x24}, {"TAB", 0x30}, {"SPACE", 0x31}, {"BACK", 0x33}, {"ESCAPE", 0x35},
    {"F1", 0x7a}, {"F2", 0x78}, {"F3", 0x63}, {"F4", 0x76}, {"F5", 0x60}, {"F6", 0x61}, {"F7", 0x62},
    {"F8", 0x64}, {"F9", 0x65}, {"F10", 0x6d}, {"F11", 0x67}, {"F12", 0x6f}, {"F13", 0x69}, {"F14", 0x6b},
    {"F15", 0x71}, {"F16", 0x6a}, {"F17", 0x40}, {"F18", 0x4f}, {"F19", 0x50}, {"F20", 0x5a},
    {"INSERT", 0x72}, {"HELP", 0x72}, {"HOME", 0x73}, {"PRIOR", 0x74}, {"DELETE", 0x75}, {"END", 0x77},
    {"NEXT", 0x79}, {"LEFT", 0x7b}, {"RIGHT", 0x7c}, {"DOWN", 0x7d}, {"UP", 0x7e},
    {"DECIMAL", 0x41}, {"MULTIPLY", 0x43}, {"ADD", 0x45}, {"NUMLOCK", 0x47}, {"CLEAR", 0x47},
    {"DIVIDE", 0x4b}, {"SUBTRACT", 0x4e},
    {"NUMPAD0", 0x52}, {"NUMPAD1", 0x53}, {"NUMPAD2", 0x54}, {"NUMPAD3", 0x55}, {"NUMPAD4", 0x56},
    {"NUMPAD5", 0x57}, {"NUMPAD6", 0x58}, {"NUMPAD7", 0x59}, {"NUMPAD8", 0x5b}, {"NUMPAD9", 0x5c},
};

int rsm_key(const char *key) { return rsm_key_in("Keybinds", key); }

int rsm_key_in(const char *section, const char *key) {
  const char *v = rsm_get(section, key);
  if (!v || !*v) return -1;
  if (!strncasecmp(v, "VK_", 3)) v += 3;
  for (size_t i = 0; i < sizeof KEYS / sizeof KEYS[0]; i++)
    if (!strcasecmp(KEYS[i].name, v)) return KEYS[i].code;
  return -1;
}
