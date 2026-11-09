# rs-stems

Takes the guitar or the bass out of the song you play. It is an add-on to the Song Stems mod
([`src/mod_song_stems.c`](../src/mod_song_stems.c), `SongStems = on` in `RSMods.ini`), in both editions.

Two pieces:

- **The mod**, in the game. When a song is chosen it hands its audio to the helper. While the song plays, it mixes each
  block the game decodes with the helper's version of the song without the part, at the level you set (a line of the
  volume mod: 0% takes the part out, 100% is the song as it is). It reaches the game's decoded audio through RS
  Re-ARMed's hook point `ak_src_submit` (RS Re-ARMed 1.12 and later).
- **The helper**, `rs-stems`, a separate process. It decodes the song's Wwise Vorbis audio, separates it with Demucs
  (htdemucs_6s, converted to Core ML) on the Neural Engine, never the GPU, and keeps the result in a cache of a fixed size.

## Folders

| | |
|---|---|
| `~/Library/Application Support/RS Re-ARMed/stems/` | the helper: `rs-stems` and the compiled model `htdemucs_6s.mlmodelc` (both editions use it) |
| `~/Library/Caches/RS Re-ARMed/stems/` | the cache: `<file id>.<part>.m4a`, `index.tsv`, `codebooks.bin`, `rs-stems.log`, `rs-stems.lock`, the FIFO `commands`, and `work/` |

`RS_STEMS_HOME` and `RS_STEMS_CACHE` override the two.

## The mod and the helper

The game starts the helper (`rs-stems serve`) when `SongStems` is on and the helper is installed. The mod writes
commands to the FIFO `commands`, one a line, fields separated by a tab; the helper answers through files.

| Line | |
|---|---|
| (empty) | once a second while the game runs; the helper ends 10 s after the last one, so also after a crash |
| `separate <wem> <file id> <part> <song key>` | make the song without `<part>` (`guitar` or `bass`) from the `.wem` file at path `<wem>` |
| `budget <MB>` | the cache's size (`SongStemsCacheMB`, default 500) |

The helper makes the FIFO when it starts and keeps it open for reading, so the mod's `open` fails until a helper is
there. Any line counts as a heartbeat. One helper runs at a time: a second one waits until the first has ended. A
`separate` for the song and part already under way is ignored.

The mod writes the song's `.wem` to `work/<file id>.wem` (it reads it from the song's archive) and the game's Vorbis
codebook library to `codebooks.bin` once (read from the game by name, `ak_vorbis_codebooks`: nothing of the game is in
this repository).

`codebooks.bin`: `"RSCB"`, u32 count, then count u32 offsets of each codebook from the start of the data, then the data.
The game's table has only where each codebook starts; the last one ends where the data ends.

For each `separate`, the helper writes `work/<file id>.<part>.pcm`, the song without the part, from its start, and the
mod reads it while the helper writes it:

| Offset | |
|---|---|
| 0 | `"RSSTEMS1"` |
| 8 | u32 file ID |
| 12 | u32 part: 0 guitar, 1 bass |
| 16 | u32 sample rate (the song's own) |
| 20 | u32 channels (the song's: 2, or 1 for a mono song) |
| 24 | u32 total frames (the song's, as the game decodes it) |
| 28 | u32 state: 0 working, 1 done, 2 failed |
| 32 | u32 ready frames: every frame before it is written; it only grows (atomic, written after the frames) |
| 64 | int16 interleaved, total frames × channels |

The samples are the song as the game's decoder makes it 16-bit (× 32767, truncated, as vgmstream does it), less the
part (× 32768), rounded and clipped.

The mod maps the file into the game's memory, so it must never be shorter than the mapping: the helper gives the file
its full size (64 bytes + total frames × channels × 2) before it sets the state or any ready frames, and never truncates
or rewrites a `.pcm` in place. A new one is made under a temporary name (`.pcm.new`) at its full size and with its
header, then renamed over the old one, which removes the old one in the same step; the mod sees the new file by its
inode and maps it again, while a mapping of the old one stays readable.

Frame N of the file is frame N of the song as the game's decoder gives it (the hook's position), so the mod mixes a
block at its position without searching. A song in the cache is decoded from its `.m4a` into the same file, ready at
once (a copy that ends before the song does leaves the ready frames where it ends, and the state failed). A new
`separate` stops the one under way; the helper keeps one `.pcm` (the mod may still have the last one mapped: the helper
removes it, and a mapped file stays readable), and removes any left in `work/` when it starts. The `.wem` files are the
mod's: the helper only reads them.

The cache keeps the song without the part (the backing), not the part: lossy coding hides its noise under what it codes,
and the backing hides its own. It is AAC at 256 kbps (128 kbps mono), stored once the separation is done. Each
`separate` counts as a play (`index.tsv`, a line an entry, tab-separated: file ID, part (`guitar` or `bass`), bytes,
plays, last played in Unix seconds). The budget counts the `.m4a` files, in MB of 1,000,000 bytes. Over the budget, the
entry with the lowest score goes first: its plays, halved for every 30 days since it was last played. The song being
played (the last `separate`'s, both parts) is never removed.

## Build and install

The helper runs on macOS 15 or later on Apple silicon (the model is converted for macOS 15), and needs only the Xcode
tools to build: Foundation, Core ML, Accelerate and AudioToolbox.

```bash
stems/build.sh            # build/stems/rs-stems
stems/install.sh          # the helper and the model into RS Re-ARMed/stems, and the model compiled once (20-50 s)
stems/test.sh             # the tests: decoding, separation, the protocol (each skipped without its files)
```

The model isn't in the repository: [`convert/`](convert/README.md) makes it, and `install.sh` takes it from
`RS_STEMS_MODEL` (default `build/stems/htdemucs_6s.mlpackage`). The app's release build (`gui/build-app.sh`) takes it the
same way and carries the helper, the model compiled and [Demucs' licence](LICENSE-demucs) in
`Contents/Resources/stems`; the app installs them with the mods on macOS 15 and later and starts `rs-stems warm`. The first load of a model compiles it for the Neural
Engine; `install.sh` does it, so a song doesn't wait for it.

By hand, `rs-stems separate <wem> <file id> <part> <out.pcm>` separates one song without the cache and prints how long
it took, `rs-stems decode <wem> <out.wav>` writes the song as the helper decodes it, and `rs-stems warm` loads the model.
`RS_STEMS_OVERLAP` (default 0.25) is how much the 7.8 s segments overlap: less is faster, more is smoother. The tests'
`codebooks.bin` comes from the Mac game's binary (`tools/codebooks_from_mac.py`); like everything of the game, keep it
out of the repository.

## How it works

The `.wem` is Wwise's Vorbis: the setup header names its codebooks by number in the game's library, and its packets leave
out the bits a standard Vorbis packet starts with. `wwise_vorbis.c` decodes it directly (written from the Vorbis I
specification, doing what libvorbis does where the specification leaves room), to within 1 LSB of vgmstream, the same
length and the same frames.

The separation is demucs' own, `apply_model` with `shifts=0`: the song normalised by its mono mix, 7.8 s segments cut
and cross-faded as demucs does, the spectrogram and the normalisations of each segment (`stems_dsp.c`) on the CPU with
Accelerate, the network on the Neural Engine (compute units CPU and Neural Engine, which puts all of it there). While the
Neural Engine runs one segment, the CPU prepares the next and finishes the one before, at utility priority. A song at
another rate than 44.1 kHz is resampled for the model and back. Frames are ready once no later segment changes them.
