# Headstock programs

rs7string's four headstock programs (github.com/SomeeName/rs7string, now RS Extended Range, `data/headstock/` at
v1.0.0-beta.6, MIT: LICENSE.md here), gzipped (`gzip -9 -n`). Seven-String Guitar's headstock (`src/seven_headstock.c`)
compiles them in with `#embed` and unpacks them when it builds the models, each checked against rs7string's size and
SHA-256 of it:

| File | Made from (the player's own game files) | Unpacked | SHA-256 of the unpacked file |
|---|---|---|---|
| rsxr_headstock.bin.gz | `epi_wilshire_cherry.nif`, `epi_whilshire_peg.nif` (`base/dependency_guitarstandardinlinedrh.psarc`) | 14816399 | dfa2e321ba9190ad30218468b947a6c8238b37826437287f6d876acfb158cb10 |
| rsxr_headstock_lh.bin.gz | `epi_whilshire_cherry_lh.nif`, `epi_whilshire_peg.nif` (`...inlinedlh.psarc`) | 5922719 | 409bc5bddb214ed672be8acf3edea100e290e0eade136de1a637bd8df6a1ad5a |
| rsxr_headstock_bass.bin.gz | `bass_thunder.nif`, `peg_2clove.nif` (`base/dependency_guitarbassinlinedrh.psarc`) | 7574973 | 61d80e826c4c278017fc8b2e47735c08dc71659f7594825064331c8d5a579ae7 |
| rsxr_headstock_bass_lh.bin.gz | `bass_thunder_lh.nif`, `peg_2clove.nif` (`...bassinlinedlh.psarc`) | 6422701 | f2bafc1191b591faed32faf93c0179d0bdbc8656010778a2a30ff7dd8ed45a19 |

A program (RSXRH1, hex text) builds rs7string's headstock models and textures from the player's own game files (refused
unless their SHA-256 is the program's): copies out of those files and rs7string's own bytes. The guitar program makes
`rsxr_inline7.nif` (7 pegs), `rsxr_inline8.nif` (8 pegs), `rsxr_peg.nif` and the six `rsxrhs_*.dds` textures; the
left-handed one the two left-handed frames; the bass programs `rsxr_bass5.nif` and `rsxr_bass6.nif` (and the `_lh` pair),
which name the guitar program's textures. They replace the two RS7H1 programs of the first port (rs7string 2edcf68),
whose seven-peg model they make again, now with an eight-peg and the bass models beside it.

Nothing in them is a run of the game's bytes. Checked on this folder's four programs against every file of the four guitar
and bass archives they are made from (6.1 MB; every literal run of the programs, 12-byte windows): the stretches the
programs have in common with the game's files are all DDS headers (the format's own fields), runs of zero bytes and
near-uniform texture blocks (the flat normal map), tables of consecutive small integers and format strings. None is
model or texture data of the game's. (rs7string's own audit: no 16-byte run in common with the templates.)
