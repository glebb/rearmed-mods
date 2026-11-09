// Seven-String Guitar (seven.h): what the chdetect part shares with the others.
//
// A chord's targets are an int32[8] (strings 0-7, -1 none) wherever the game's matcher (Play_Matcher_DetectChord) is
// reached; seven or eight of them are judged by the game's algorithm over n targets (seven_chdetect.c), the rest by
// the game's own body.
#ifndef RS_SEVEN_CHDETECT_H
#define RS_SEVEN_CHDETECT_H

// [Seven String] ChordWideMisses (0-2, default 2): how many notes a seven- or eight-note chord may miss
int seven_chdetect_wide_misses(void);

#endif
