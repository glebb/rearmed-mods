// Wwise Vorbis (.wem) decoding for rs-stems: the game's streamed songs are RIFF files with Wwise's own Vorbis, whose
// setup header names its codebooks by number in a library the game carries (codebooks.bin, stems/README.md) and whose
// packets leave out the bits a standard Vorbis packet starts with.
#ifndef WWISE_VORBIS_H
#define WWISE_VORBIS_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint32_t rate, channels;
  uint32_t frames;      // the song's length as the game plays it (the header's sample count)
  float *pcm;           // interleaved, frames x channels, full scale 1.0 (NULL from ww_probe)
} ww_audio;

// The header alone: rate, channels and frames. 0 on success, else err says why.
int ww_probe(const uint8_t *wem, size_t len, ww_audio *out, char *err, size_t errlen);

// The whole song decoded. 0 on success, else err says why. ww_free frees out->pcm.
int ww_decode(const uint8_t *wem, size_t len, const uint8_t *codebooks, size_t cblen, ww_audio *out, char *err,
              size_t errlen);
void ww_free(ww_audio *a);
#endif
