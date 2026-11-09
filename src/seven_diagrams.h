// Seven-String Guitar (seven.h): what the diagrams part shares with the others.
#ifndef RS_SEVEN_DIAGRAMS_H
#define RS_SEVEN_DIAGRAMS_H

// The song (SNG_Song, host pointer) is being freed: its attributes' binding goes (the chords part's song-free point
// calls it before it frees the song's records, rs7string's Rs7Diagram_Drop from Rs7Ns_OnSongFree)
void seven_diagrams_song_freed(const void *song);

#endif
