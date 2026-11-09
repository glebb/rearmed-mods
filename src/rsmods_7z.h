// The 7z archives inside the game's cache.psarc (cache0.7z ... cache8.7z), and the LZMA they are packed with. The game's
// have one file per folder, each packed with LZMA (RS Re-ARMed's overlay packs the files it changes with LZMA2), and
// the header packed with LZMA. This reads a file of one, and writes the archive again with some files changed: every
// other file keeps its packed bytes as they are. Nothing here touches the game (the tests use it as it is).
#ifndef RS_RSMODS_7Z_H
#define RS_RSMODS_7Z_H

#include <stddef.h>
#include <stdint.h>

// ------------------------------------------------------------------------------------------------
// LZMA. A raw LZMA stream (props: lc, lp and pb in one byte, then the dictionary size) of exactly out_len bytes; an
// LZMA2 stream (its chunks, to its end). 1 if it decoded whole.
// ------------------------------------------------------------------------------------------------
int rsm_lzma_decode(const uint8_t props[5], const uint8_t *in, size_t in_len, uint8_t *out, size_t out_len);
int rsm_lzma2_decode(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_len);
// An LZMA stream of data that any LZMA decoder reads: every byte a literal, so it is a little larger than the data
// (the files changed here are small). props gets lc 3, lp 0, pb 2 and a 1 MiB dictionary. malloc'ed; NULL without
// memory.
uint8_t *rsm_lzma_encode_literals(const uint8_t *data, size_t n, size_t *out_len, uint8_t props[5]);

// ------------------------------------------------------------------------------------------------
// 7z
// ------------------------------------------------------------------------------------------------
typedef struct rsm_7z rsm_7z;
// The archive in data (kept: it must outlive the rsm_7z). NULL, with the reason in why, if it isn't one this reads:
// a solid archive, a folder of more than one coder, a coder other than LZMA, LZMA2 or none.
rsm_7z *rsm_7z_open(const uint8_t *data, size_t n, char *why, size_t why_len);
int rsm_7z_files(const rsm_7z *z);
const char *rsm_7z_name(const rsm_7z *z, int file);  // with '/' (a name with '\' is given with '/')
int rsm_7z_find(const rsm_7z *z, const char *name);  // the file of that name (any case, '/' or '\'), or -1
// The file, malloc'ed with a NUL after it; NULL if it can't be decoded or doesn't match its CRC
uint8_t *rsm_7z_read(const rsm_7z *z, int file, size_t *len);
// The archive again, with file files[i] holding data[i] (lens[i] bytes) for i < n, packed as rsm_lzma_encode_literals
// packs it; the other files, their order, names, times and attributes as they are. malloc'ed; NULL on failure.
uint8_t *rsm_7z_rewrite(const rsm_7z *z, int n, const int *files, const uint8_t *const *data, const size_t *lens,
                        size_t *out_len);
void rsm_7z_close(rsm_7z *z);

#endif
