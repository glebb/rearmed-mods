// Seven-String Guitar (seven.h): extended bass, a 5-string (low B) or 6-string (low B, high C) bass, as rs7string
// (github.com/SomeeName/rs7string, MIT, by SomeeName: src/features/bass, ARCHITECTURE section 6) has it: single notes
// on bass strings 4 and 5 expected at the right pitch and scored, the detector's open and lowest notes following them,
// the low B's bend held over the pitch tracker's dropouts.
//
// The bass keeps all its strings in the game's six slots (seven_model.h: native 0 = the low B -> slot 4, 1-4 -> 0-3,
// 5 = the high C -> slot 5). The game lays out lanes, strings and pegs itself from the strings part's build count; what
// it cannot know is the expected pitch of slots 4 / 5 (base 23 / 48: the pitch part, the PE0 edits, S1 and I1 here and
// there), the detector's open notes and lowest open note of such a song (the pitch part), and the tuner's table of
// bass open notes (seven_bass.c writes [4] = 23, [5] = 48 into it once, so the tuner shows and tunes them).
//
// An extended bass song is one whose SNG has an RS7X block of instrument 1 (the chords part's
// seven_chords_block_instrument / _strings) played on a bass detector (instrument 2). The per-detector setups of the
// song (seven_bass_setup, seven_model.h) say which detector plays which; everything else, a 4-string bass and any
// guitar, is the game's.
//
// This header and seven_bass_logic.c are pure values in, values out (tests/test_seven_bass.c); seven_bass.c is the
// game side, which switches SEVEN_F_BASS on when all its points were taken.
#ifndef RS_SEVEN_BASS_H
#define RS_SEVEN_BASS_H

#include <stdint.h>

#define SEVEN_BASS_NO_BASE (-1)
#define SEVEN_BASS_BLOCK_INSTRUMENT 1  // the RS7X block's STRS instrument of a bass
#define SEVEN_DET_BASS 2               // the detector's instrument of a bass

// The base MIDI note of bass string 4 (23, B0) or 5 (48, C3) when `strings` (0, 5 or 6) includes it, else
// SEVEN_BASS_NO_BASE (rsxrbass::ExtraBase): a 5-string bass has no string 5
int seven_bass_extra_base(unsigned string, unsigned strings);
// The song's bass string count: 5 or 6 for a bass block (instrument 1, 5 or 6 strings) on a bass detector, else 0
unsigned seven_bass_song_strings(int has_block, int block_instrument, int block_strings, int det_instrument);
// The lowest open MIDI note of an active song: `stock` lowered to base + tuning of every extra string
int seven_bass_lowest(int stock, const int16_t tuning[6], unsigned strings);

// The low B's main-string bend follows the detected pitch (Rnd_Note_UpdateInProgress reads it); at B0..F#1 the pitch
// tracker drops out (-1) or falls back to the unbent note for single frames, so the string snapped back and the bend
// never showed. The hold keeps the last good reading of the note over every frame without one for as long as the note
// lasts; a reading more than SEVEN_BEND_DROP below it counts only once it has stayed down for SEVEN_BEND_RELEASE_SEC
// (a real release); rises are taken at once (rsxrbass::HoldBend).
#define SEVEN_BEND_RELEASE_SEC 0.15f  // how long a lower reading must last to release (song seconds)
#define SEVEN_BEND_DROP 0.75f         // a fall larger than this is a dropout until it lasts (semitones)
typedef struct {
  const void *note;  // the note the reading belongs to (a new note starts over)
  float semis;       // the held reading
  float time;        // the song time of the last read
  float low_since;   // since when the readings are below the held one (-1: they are not)
  int valid;
  uint32_t reads, missed, low;  // per-note counts for the log: reads, reads without a pitch, low reads held
  float gap_since, longest_gap, peak;
} seven_bend_hold;
// One read: `ok` = the game's read succeeded, `semis` its value, `now` the song time. Returns whether the read counts as
// successful; *semis may be replaced by the held value.
int seven_bass_hold_bend(seven_bend_hold *h, const void *note, int ok, float *semis, float now);

// ---- the game side (seven_bass.c)
// The song's setup of the detector `det` (a host pointer; instrument = its detection_instrument), recorded for every
// player, a guitar's too (the colours need to know whether one plays): the pitch part's S4 calls it with the song's SNG
// tuning vector (host pointer) before the game's SetTuning. Returns the song's bass string count, 5 or 6 for an extended
// bass on this detector, else 0 (stock; also without SEVEN_F_BASS). rs7string's RsxrBass_OnSong.
unsigned seven_bass_on_song(const void *det, const void *tuning, int det_instrument);

#endif
