// RSMods: GuitarSpeak (GuitarSpeak.cpp, GuitarSpeakMod.cpp, from UKLooney's G2RS): the game's menus by playing notes.
// [Guitar Speak] gives each key a MIDI note (GuitarSpeakEnterWhen = 52: E3, the low E's 12th fret; 0 none): each new
// note presses the key set to it. GuitarSpeakCloseWhen's note stops it, and the same note starts it again. Off in songs,
// lessons and calibration, and in the tuners unless GuitarSpeakWhileTuning = on, as in RSMods.
//
// The note is the one the game hears, as RSMods reads it, and a new note is one as RSMods counts it (rsmods_game_note.c).
#include <stdlib.h>
#include <strings.h>

#include "rsmods.h"

static const struct {
  const char *setting;  // in [Guitar Speak]
  int keycode;          // macOS
  uint16_t ch;
} KEYS[] = {
    {"GuitarSpeakDeleteWhen", 0x75, 0xf728},    // forward delete, as Windows' VK_DELETE
    {"GuitarSpeakSpaceWhen", 0x31, ' '},
    {"GuitarSpeakEnterWhen", 0x24, '\r'},
    {"GuitarSpeakTabWhen", 0x30, '\t'},
    {"GuitarSpeakPGUPWhen", 0x74, 0xf72c},
    {"GuitarSpeakPGDNWhen", 0x79, 0xf72d},
    {"GuitarSpeakUPWhen", 0x7e, 0xf700},
    {"GuitarSpeakDNWhen", 0x7d, 0xf701},
    {"GuitarSpeakESCWhen", 0x35, 0x1b},
    {"GuitarSpeakOBracketWhen", 0x21, '['},
    {"GuitarSpeakCBracketWhen", 0x1e, ']'},
    {"GuitarSpeakTildeaWhen", 0x32, '`'},
    {"GuitarSpeakForSlashWhen", 0x2c, '/'},
    {"GuitarSpeakAltWhen", 0x3a, 0},  // Option
};

// GameState.hpp of RSMods: calibrationMenus, tuningMenus, lessonModes
static const char *const CALIBRATION[] = {"Guitarcade_Calibration", "Guitarcade_WRDCalibration", "GECalibrationMeter",
                                          "NonStopPlay_CalibrationMeter_PreGame", "CalibrationMeter",
                                          "CalibrationMeter_MP", "LearnASong_CalibrationMeter_PreGame", NULL};
static const char *const TUNING[] = {"SelectionListDialog", "LearnASong_PreSongTuner", "LearnASong_PreSongTunerMP",
                                     "NonStopPlay_PreSongTuner", "NonStopPlay_PreSongTunerMP", "ScoreAttack_PreSongTuner",
                                     "SessionMode_PreSMTunerMP", "SessionMode_PreSMTuner", "UIMenu_Tuner", "UIMenu_TunerMP",
                                     "Guitarcade_Tuner", "Tuner", "Duet_PreSongTuner", "H2H_PreSongTuner", "GETuner",
                                     "PreGame_GETuner", NULL};
static const char *const SONGS[] = {"SessionMode_Game", "SessionMode_PauseGame", "Guitarcade_Game", "Guitarcade_Pause",
                                    "GuidedExperience_Game", "GuidedExperience_Pause", NULL};

static unsigned g_plucks;
static int g_closed;  // stopped with the close note

static int note_of(const char *setting) {
  const char *v = rsm_get("Guitar Speak", setting);
  return v ? atoi(v) : 0;
}

static void tick(void) {
  unsigned plucks, was = g_plucks;
  int note = rsm_guitar_note(&plucks);
  g_plucks = plucks;  // followed while off too: turning it on presses nothing
  if (!rsm_on("GuitarSpeak") || note < 0 || plucks == was || note >= 96) return;
  const char *m = rsm_menu();
  if (!*m || rsm_menu_in(RSM_SONG_MODES) || rsm_menu_in(SONGS) || rsm_menu_in(CALIBRATION) ||
      (rsm_menu_in(TUNING) && !rsm_on_section("Guitar Speak", "GuitarSpeakWhileTuning")))
    return;
  char name[8];
  rsm_note_name(note, name, sizeof name);
  if (note == note_of("GuitarSpeakCloseWhen")) {
    g_closed = !g_closed;
    rsm_log("guitar speak: %s (%s)", g_closed ? "stopped" : "started again", name);
    return;
  }
  if (g_closed) return;
  int k = -1;  // two keys on a note: the later one, as RSMods' table keeps it
  for (int i = 0; i < (int)(sizeof KEYS / sizeof KEYS[0]); i++)
    if (note == note_of(KEYS[i].setting)) k = i;
  if (k < 0) return;
  rsm_send_key(KEYS[k].keycode, KEYS[k].ch);
  rsm_log("guitar speak: %s presses %s", name, KEYS[k].setting);
}

const rsm_mod rsm_mod_guitar_speak = {"guitar_speak", tick, NULL, NULL, NULL};
