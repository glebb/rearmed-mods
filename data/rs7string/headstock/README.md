# Headstock programs

rs7string's two headstock programs (github.com/SomeeName/rs7string, `data/headstock/` at 2edcf68, MIT: LICENSE.md
here), gzipped (`gzip -9 -n`). Seven-String Guitar's headstock (`src/seven_headstock.c`) compiles them in with
`#embed` and unpacks them when it builds the model, each checked against rs7string's size and SHA-256 of it:

| File | Unpacked | SHA-256 of the unpacked file |
|---|---|---|
| rs7string_headstock.bin.gz | 14873158 | 8f5ba0cbf69de3f01b63d45e135365dfc586341e6e8e7cabbe82f5d6f56aad69 |
| rs7string_headstock_lh.bin.gz | 2863876 | e49b718c20b19695a9ae942f9942f17592d69f7e5bc542dc839e21edabf5a07e |

A program (RS7H1, hex text) builds rs7string's seven-peg headstock model and its six textures from the player's own
guitar files (in `base/dependency_guitarstandardinlinedrh.psarc` and `...inlinedlh.psarc`, refused unless their
SHA-256 is the program's): copies out of those files and rs7string's own bytes. Nothing in it is a run of the game's
bytes (checked against the 53 files of the guitar archives: no 12-byte run in common, as text; decoded, only format
strings, node names and near-uniform texture blocks of up to 19 bytes).
