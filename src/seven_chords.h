// Seven-String Guitar (seven.h): what the chords part shares with the others: a song's chord records over eight
// strings (rs7string's N-slot records, its ARCHITECTURE section 7) and the song's RS7X block.
//
// Every song the game loads from an SNG gets, at its load, a T8 record for each chord template and a C8 record for
// each ChordNotes record, in the game's memory (its allocator), freed with the song:
//   T8 (seven_t8_size 0x80): the game's template (0x48 bytes: u32 mask, int8 frets[6] +0x4, int8 fingers[6] +0xa,
//      int32 midi[6] +0x10, char name[32] +0x28) unchanged, then int8 frets[8] (seven_t8_frets +0x48), int8
//      fingers[8] (seven_t8_fingers +0x50), int32 midi[8] (seven_t8_midi +0x58), u8 the arrangement's slots
//      (seven_t8_slots +0x78: 6, 7 or 8), u8 the strings in the chord as bits (seven_t8_used +0x79), zeros to +0x80.
//   C8 (seven_c8_size 0x15a8): the game's ChordNotes record (0x948 bytes: u32 mask[6], BendData32[6] +0x18, int8
//      slideTo[6] +0x930, int8 slideUnpitchTo[6] +0x936, int16 vibrato[6] +0x93c) unchanged, then u32 mask[8]
//      (seven_c8_mask +0x948), int8 slideTo[8] (seven_c8_slide_to +0x968), int8 slideUnpitchTo[8]
//      (seven_c8_slide_unpitch_to +0x970), int16 vibrato[8] (seven_c8_vibrato +0x978), BendData32 bends[8]
//      (seven_c8_bends +0x988; seven_bend_data_size 0x184 each: 32 x {float time, float step, u32}, u32 used +0x180).
// Slots 0-5 are the game's six; slots 6 and 7 (the 7th and 8th string) come from the song's RS7X block, or, for a
// 7-string package without one, from the template's last name byte (the old format); a six-string song or a bass has
// them empty (fret, finger, MIDI -1; mask 0, slides -1, no bends). A fret equal to the song's capo is 0 in slots 6/7,
// as the game makes it in 0-5.
//
// The game's own code reaches the records through the stride sites (a template id -> its T8, a ChordNotes id -> its
// C8: rs7string's TP*, CP*), so a template or ChordNotes pointer the game holds at play time is a T8 / C8, and an edit
// may read the eight-slot arrays at those offsets. All pointers here are host pointers; the lookups are lock-free.
//
// The tracker's note evaluation (Play_NoteEval, 0xc28 bytes) keeps a copy of the template at +0x274, whose name
// (+0x29c..+0x2bb) has no reader once copied: the chords part keeps strings 6/7 there (rs7string's eval tails), so
// +0x284 is an int32 midi[8] (seven_eval_midi8: the copy's midi[6], then the tails' two):
//   seven_eval_tail_midi +0x29c int32[2], seven_eval_tail_mask +0x2a4 u32[2], seven_eval_tail_fret +0x2ac int8[2],
//   seven_eval_tail_finger +0x2ae int8[2], seven_eval_tail_slide +0x2b0 int8[2], seven_eval_tail_unpitch +0x2b2
//   int8[2], seven_eval_tail_vibrato +0x2b4 int16[2]; and seven_eval_child67 +0xc1e u8[2], the child eval slots of
//   strings 6/7 (0xff none). Play_Eval_Build writes the tails (seven_bt1), Play_Eval_ClearState empties them
//   (seven_cs1).
#ifndef RS_SEVEN_CHORDS_H
#define RS_SEVEN_CHORDS_H

#include <stdint.h>

#define SEVEN_CHORD_SLOTS 8   // the record slots: strings 0-7
#define SEVEN_CHORD_NATIVE 7  // the strings the widened chord loops cover (0-6)

// The T8 record of template `id` of the template vector whose first entry is `templates` (the song's +0x94 begin),
// NULL when that vector has no T8 array (the build failed, or not a loaded song's) or `id` is past it
uint8_t *seven_chords_template(const void *templates, uint32_t id);
// The C8 record of ChordNotes record `id` of the vector whose first entry is `chord_notes` (the song's +0xac begin),
// NULL likewise (also before the song's post-processing, when the C8 array is built)
uint8_t *seven_chords_notes(const void *chord_notes, uint32_t id);
// `record` itself when it is a T8 (C8) record of a loaded song, at a record's start; else NULL (rs7string's
// Rs7Ns_AsT8 / Rs7Ns_AsC8)
uint8_t *seven_chords_as_t8(const void *record);
uint8_t *seven_chords_as_c8(const void *record);
// The T8 of one of the game's own template records (a pointer into a song's template vector), NULL for none
uint8_t *seven_chords_t8_of(const void *game_template);
// The song's (SNG_Song, host pointer) T8 array and its count: 1, or 0 when it has none
int seven_chords_song_templates(const void *song, uint8_t **t8, uint32_t *count);
// The string count of the song's RS7X block (7, 8, ... as its STRS says), 0 when the song has no valid block
int seven_chords_block_strings(const void *song);

// A T8's slots (6, 7 or 8: the arrangement's strings, 6 for a six-string song or bass) and its string s (0-7):
// fret, finger (-1 none, 0 thumb, 1-4), expected MIDI note (-1 none). s past 7 gives -1.
int seven_chords_t8_slots(const uint8_t *t8);
int seven_chords_t8_fret(const uint8_t *t8, int s);
int seven_chords_t8_finger(const uint8_t *t8, int s);
int32_t seven_chords_t8_midi(const uint8_t *t8, int s);
// A C8's string s (0-7): its note mask (0 past 7), slide target and unpitched slide target (-1 none), vibrato, and its
// BendData32 (host pointer; NULL past 7)
uint32_t seven_chords_c8_mask(const uint8_t *c8, int s);
int seven_chords_c8_slide_to(const uint8_t *c8, int s);
int seven_chords_c8_slide_unpitch_to(const uint8_t *c8, int s);
int seven_chords_c8_vibrato(const uint8_t *c8, int s);
uint8_t *seven_chords_c8_bends(const uint8_t *c8, int s);

// The eval's tails of strings 6/7 from its T8 / C8 (either may be NULL: that half empty), and empty tails
void seven_chords_eval_tails(uint8_t *eval, const uint8_t *t8, const uint8_t *c8);
void seven_chords_eval_clear(uint8_t *eval);

#endif
