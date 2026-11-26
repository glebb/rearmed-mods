// Seven-String Guitar (seven_headstock.c): the pure part of the headstock, no game memory and no game address: the
// copy/insert program (RSXRH1) that builds the headstock models from the player's own game files, the rules that name
// the game's frames and the models' nodes, the wood brightness of a texture, and the width math. After rs7string
// (github.com/SomeeName/rs7string, MIT, by SomeeName): src/features/headstock (hsbuild, assetload_paths, assetload_tex,
// assetload_head.h, hooks_width). tests/test_seven_headstock.c.
#ifndef RS_SEVEN_HEADSTOCK_BUILD_H
#define RS_SEVEN_HEADSTOCK_BUILD_H
#include <stddef.h>
#include <stdint.h>

#define HS_MAX_OUT 16      // the outputs of one program (rs7string allows 64; ours have 9 at most)
#define HS_NAME_MAX 72
#define HS_LIMIT (64u << 20)  // no program, template or output is larger

typedef struct {
  char name[HS_NAME_MAX];
  uint8_t *data;  // malloc'ed; NULL for an output not asked for
  uint32_t size;
  uint8_t sha[32];  // the program's SHA-256 of it
} hs_file;

typedef struct {
  const uint8_t *gz;  // the program, gzipped (the gzip stream of its RSXRH1 text)
  size_t gz_len;
  size_t size;  // its unpacked size and SHA-256
  uint8_t sha[32];
} hs_package;

// A program unpacked (malloc'ed, *len its size), or NULL with the reason in *why: the gzip stream must unpack to
// exactly the program's size and SHA-256
uint8_t *hs_package_unpack(const hs_package *pk, size_t *len, const char **why);

// Whether name is one a program may make: rsxr_*.nif or rsxrhs_*.dds, lower-case letters, digits and '_' only
int hs_name_ok(const char *name);

// Runs a program (RSXRH1: hex text; version 1, the SHA-256 of the two templates it is made from, then each output as
// literal runs and ranges copied from the templates, with its size and SHA-256) on the two template files. The
// templates are checked against the program's SHA-256 first. Outputs: out[i] for the program's i-th output (at most
// nout of them; *count its number); those whose bit is in `want` get their bytes (each checked against its SHA-256),
// the others only their name and size. Returns NULL, or why it refused (then no out[] holds anything).
const char *hs_build(const uint8_t *pkg, size_t pkg_len, const uint8_t *const tpl[2], const size_t tlen[2],
                     unsigned want, hs_file *out, int nout, int *count);
void hs_free(hs_file *out, int n);
const hs_file *hs_find(const hs_file *files, int n, const char *name);

// Darkens a DXT1 (BC1) .dds in place by k (0 < k < 1): both RGB565 end points of every block are scaled; a block whose
// end-point order (4-colour c0 > c1, 3-colour c0 <= c1) would flip gets them swapped and its indices remapped, so no
// block changes mode (rs7string's Rs7Asset_ScaleDxt1). 0 for anything but a plain DXT1 .dds.
int hs_scale_dxt1(uint8_t *dds, size_t size, float k);
// The base colour of the wood (rs7string's IsWoodColour: "wood" in the name, not a gloss or normal map)
int hs_is_wood_colour(const char *name);

// The texture names (base names of ".dds") in a file's bytes, in their first order (rs7string's DdsNames)
#define HS_MAX_TEX 32
int hs_dds_names(const uint8_t *buf, size_t size, char names[][64], int max);
int hs_name_in(const char *nm, char names[][64], int n);

// ---- the game's frames: what a resource name says
enum hs_frame_kind { HS_FRAME_NONE = -1, HS_FRAME_GUITAR_INLINE = 0, HS_FRAME_GUITAR_33 = 1, HS_FRAME_BASS_INLINE = 2, HS_FRAME_BASS_22 = 3 };
typedef struct {
  int kind;       // enum hs_frame_kind
  int lh;         // the base name ends in _lh
  int peg;        // 3+3's peg model (peg_bakelite.nif)
  char base[128]; // the base name without .nif, lower case
} hs_frame;
// A resource name (a .nif) directly in the folder of a headstock frame the models replace: the guitar's in-line frame
// (guitar_standard_inline/) and 3+3 frame (guitar_standard/), and a bass's in-line (guitar_bass_inline/) and 2+2
// (guitar_bass/) frames (base names bass_*); NONE for anything else. Case and slash style are free.
hs_frame hs_frame_of(const char *name);

// The model of a bass string slot s (rsxr-headstock design.py string_mesh_name): the stock E A D G are string_size_10
// .. 7, the added low B (slot 4) string_size_11 and the high C (slot 5) string_size_6
static inline int hs_bass_seg(int s) { return s == 4 ? 11 : s == 5 ? 6 : 10 - s; }
// The stock slot whose shader row an added bass string takes: the low B the E's, the high C the G's
static inline int hs_bass_neighbour(int s) { return s == 4 ? 0 : s == 5 ? 3 : s; }
// An extra guitar segment e (string_size_7 / _8 of a model) is shown only on a guitar built with more than 6 + e strings
static inline int hs_extra_shown(int e, int strings) { return strings > 6 + e; }

// The width: the headstock frame narrowed across the strings by k. The frame root's rotation (3x3, row-major) gets its
// string axis (the local z column: elements 2, 5 and 8) scaled by k (rs7string's RsxrHead_Width). out = in with it.
void hs_width_matrix(const float in[9], float k, float out[9]);
#endif
