// Seven-String Guitar (seven.h): what the chdraw part shares with the others.
//
// Nothing to call: a chord is drawn by the game's own chord code, which the edits of seven_string make read the chords
// part's records (seven_chords.h). They read, of a T8: frets[8] +0x48, fingers[8] +0x50; of a C8: mask[8] +0x948,
// slideTo[8] +0x968, slideUnpitchTo[8] +0x970, vibrato[8] +0x978, bends[8] +0x988 (0x184 each, used count +0x180),
// with no check that the record is one: every template and ChordNotes pointer the game's chord code gets while a song
// plays must be a T8 / C8 (the chords part's stride sites, rs7string's TP1-TP8, TP11, TP12, CP1, CP2).
#ifndef RS_SEVEN_CHDRAW_H
#define RS_SEVEN_CHDRAW_H
#endif
