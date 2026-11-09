# Chord diagram recipes

rs7string's three `.rs7da` files (github.com/SomeeName/rs7string, `data/chord-diagrams/` at 2edcf68, MIT: LICENSE
here), as it ships them. Seven-String Guitar's chord diagrams (`src/seven_diagrams.c`) compile them in with `#embed`.

Each one rebuilds one class tag of one of the game's movies (`commonlib.gfx`, `contentpanel.gfx`, `ge_game.gfx` in
the player's `static.psarc`) so its chord diagrams have seven rows. A file is a list of copies out of the player's
own tag (offset, length), 78 single new bytes, and the SHA-256 of the tag it expects and of the tag it makes. Nothing
in it is a byte of the game's movies (checked against Learn & Play's: no run of 12 bytes in common). A movie whose
tag doesn't match is left as the game has it.
