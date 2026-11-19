# pitch

The pitch shifter of the Drop Pedal mod ([`src/mod_drop_pedal.c`](../src/mod_drop_pedal.c)): the live guitar a few
semitones down, chords included, to be judged against a DigiTech Drop pedal for delay and sound. `splice_shift.c` is
built into the library; the rest is how it was arrived at and measured.

| | |
|---|---|
| `splice_shift.c` | the shifter that came out ahead: reads the guitar back a little slower, takes the lag back in few, long jumps where the sound repeats and at once when a note starts, and carries each overtone across those seams by itself |
| `pitch_shift.c` | the first design, a row of narrow bands each moved in frequency. Kept for the comparison: it needs 25 to 40 ms of delay before chords come out clean |
| `bench/` | the bench that measures them |

## The bench

```bash
pitch/bench/run.sh
```

It plays made-up guitar (single notes, power chords, strummed open chords, two low notes close together, palm mutes, a
run, a bend, a vibrato) through each shifter and measures the result against the same guitar made at the lower pitch.
`bench/bench.c` says what each column is. Arguments: `-s -1,-2` the shifts, `-v` every case, `-l 0.05` the peak the
made-up guitar comes in at (0.5 without it), `-w DIR` writes what each shifter made as `.wav`, clean and through a
distortion, for listening.

Recorded guitar goes through the same shifters with `-r`. There is no ideal to compare a recording with, so the bench
times how late each note's start comes out and writes the results for listening:

```bash
pitch/bench/prepare.sh RECORDINGS_DIR some-guitar.wav P1_chords.zip
```

```bash
pitch/bench/run.sh -w OUT_DIR -r RECORDINGS_DIR/*.wav
```

`prepare.sh` makes 48 kHz mono files, and takes the dry tracks out of
[Guitar-TECHS](https://zenodo.org/records/14963133) archives (CC BY 4.0; not in this repository). A recording with its
notes beside it as a MIDI file of the same name (the dataset's `midi_NN.mid` copied to `P3_music-NN.mid`) also gets
every note's start looked at, those under ringing notes included.

RocksmithAudioBridge's drop pedal shifter is the baseline (`rab`). Its source is not in this repository:

```bash
RAB_DIR=/path/to/RocksmithAudioBridge pitch/bench/run.sh
```

## What it found (2026-11-18, one and two semitones down)

`splice` next to the baseline, on recorded guitar (Guitar-TECHS: player 1's dry tracks and the twelve music excerpts):

| | one semitone | two semitones |
|---|---|---|
| A note's start after a quiet moment comes out late by, in the middle | 0.6 to 1.0 ms against 5 to 11 | 0.6 to 1.2 ms against 5 to 11 |
| Nine such starts in ten are within | 6.9 ms against 18 | 6.0 ms against 19 |
| Chords (ten recordings): an overtone's level moves about by | 0.22 dB against 0.56 | 0.26 dB against 0.70 |
| Chords: what lands off the overtones is down by | 47 dB against 33 | 43 dB against 27 |
| Music excerpts: the same two | 0.09 against 0.20 dB, 36 against 31 dB | 0.07 against 0.27 dB, 34 against 30 dB |

- Single notes and bends: both are clean; the baseline's levels are a little steadier (0.01 dB against 0.03 to 0.05).
  Palm mutes: about the same.
- While notes ring, `splice` is 12 to 20 ms behind on average, 30 at most as a rule and up to 62 for a sound that
  only repeats that late; the baseline 7 to 11 and 27. `splice` makes a seam only when its lag has reached 30 ms
  (about four a second against ten), and a new note takes the lag back. `most_ms = 20` with `nearly = 0.2` brings
  that to 10 to 13 ms and halves the late notes of the next point, with chords and music as clean; single notes,
  palm mutes and bends then have their first seam sooner, and what lands off the overtones is 4 to 8 dB more at one
  semitone. `patient = 0` is the short-lag end: 5 to 8 ms, chords 4 dB worse.
- Every note, not only those after a quiet moment: the music excerpts come with their notes written down (1923 of
  them). When a note starts, `splice` is 1 ms behind in the middle, and more than 10 ms behind for 14% of them at one
  semitone and 19% at two. Those are soft notes under louder ringing ones: where the recording shows them, the level
  does not rise at all. Some of the written notes may not be audible starts.
- `splice` takes 1.1 to 1.2% of one processor core on the recordings, about what the baseline takes (1.0% on the
  made-up guitar, where `splice` is at 1.5%). Its slowest block of 128 samples took 0.6 to 0.8 ms of the 2.7 ms a
  block has, and 19 blocks in 20 take under 0.1 ms.

On the made-up guitar, where the ideal is known: open chords' levels move about by 0.21 dB against the baseline's 1.7
and 2.2, two low notes a third apart by 0.28 against 2.4 and 2.8, a ringing chord through the start of a new note by
0.2 to 0.3 against 0.9 to 1.2. Single notes: 0.11 against 0.07.

What made it better, in the order it was found:

- Real chords do not repeat: within 30 ms the best match is about 0.8 alike, where the made-up chords reach 0.97. So
  every seam costs something. Making fewer and longer ones was the first gain.
- That needs new notes to be noticed, or they come out as late as the lag. A burst of high end catches a clear pick
  only. A note being new in its own frequency range catches most others: wide ranges for a note after a quiet moment,
  ranges of a sixth of an octave for a note hammered on or pulled off from another (25 ms late before, 2 to 3 after).
- A soft note inside a full ringing chord raises no range enough: its overtones lie 30 to 40 Hz from the chord's, and
  telling them apart takes as long as the lag. A small rise shortens the lag by an ordinary jump.
- Two low notes a third apart come round together only after 40 to 60 ms: a sound that repeats clearly better a
  little later has its lag let grow that far.
- Mending the seams was the largest gain. At a seam every overtone that the jump does not fit steps in phase. A plain
  crossfade makes it dip, and even with its level kept, a phase stepped within 10 ms is a blip in pitch: that, not
  the dip, is most of what a seam adds. So each overtone is measured either side of the seam (all of them fitted at
  once, which tells close ones apart) and turned from the one phase to the other over 85 ms, while the read point
  itself changes over in 5 to 10 ms as before. The same at a note's start, for what rings on under it.
- A note's start lands half a millisecond behind the input, where it was a millisecond and a half: nothing measured
  got worse for it.
- The mend's work had all been done in the block of the seam, which then took up to 19 ms. Now the model of the
  ringing sound is made beforehand, a piece every 128 samples and ten times a second, and a seam itself costs two
  fits. The fits take every second sample, with the overtones side by side, and the repeat search runs over a
  straight copy of the ring. What had to stay: each overtone's phase is measured at both read points at the seam, and
  its level at both; worked out from the model alone, bends and palm mutes came out worse than unmended.
- Did not help: mending with a short turn (16 ms: under 2 dB), longer plain fades at poor seams (unnoticed notes come
  up slower), leaving a short lag alone at a start, the first design's narrow bands, and splitting the guitar in
  three ranges with a shifter each.

### Larger shifts (three, five and seven semitones down)

Nothing falls apart. On the recordings, `splice` against the baseline:

| | three | five | seven |
|---|---|---|---|
| Chords: an overtone's level moves about by | 0.31 dB against 0.75 | 0.34 against 1.02 | 0.42 against 1.29 |
| Chords: what lands off the overtones is down by | 39 dB against 26 | 37 against 24 | 35 against 23 |
| Music excerpts: the same two | 0.10 against 0.30 dB, 35 against 29 dB | 0.10 against 0.34, 34 against 29 | 0.12 against 0.28, 34 against 30 |
| A note's start comes out late by, in the middle | 0.7 to 1.2 ms against 5 to 8 | 0.8 to 1.7 against 5 to 11 | 0.9 to 2.1 against 5 to 10 |

- Single notes and palm mutes stay about as clean as at one and two semitones, in both.
- Bends and vibrato are the one kind of playing where the baseline is cleaner, by 3 to 5 dB from three semitones on:
  a jump of 20 ms lands a moving pitch somewhere else, a jump of a few ms does not. An overtone found more than 3 Hz
  from where the model has it is no longer mended, which gave back 1 dB.
- An overtone's tuning is off by more as the shift grows, in both: in the music excerpts 2, 4 and 5 cents in the
  middle and 4, 8 and 10 for nine in ten.

`eager` (the bench's `splice-e`) lets a sound that repeats cleanly and soon, a single note or a power chord, jump as
soon as its lag holds one repeat, while chords still wait. While single notes ring the lag is then 9 to 10 ms where
it was 18, in the music excerpts 7 to 13 where it was 12 to 17, and the notes found more than 10 ms behind are 7, 11
and 15% (at one, two and five semitones) where they were 14, 19 and 20%. Single notes' levels are steadier for it
(0.01 dB, the baseline's figure), chords are unchanged, and what lands off the overtones is 2 to 3 dB more on palm
mutes and bends at one semitone and within 1 dB elsewhere. Listened to on 2026-11-18, on the palm mute and bends
recordings: without it sounds better. It stays off.

### A quiet guitar (2026-11-19)

All of the above had the made-up guitar peak at half of full scale. The two levels that keep noise from counting as
a note's start (a range under one is never rising, a high end under the other never a pick) were fixed, and set for
that. A guitar that came in quieter had its notes without a pick fall under them, at two semitones down:

| The guitar's peak | A hammer-on or pull-off comes out late by | One hammered on inside a ringing chord | A picked note over a chord |
|---|---|---|---|
| 0.5 | 1.5 to 2.6 ms | 8.8 ms | 0.6 ms |
| 0.05 (26 dB under full scale) | 5 to 27 ms | 49 ms | 0.4 ms |
| 0.01 (40 dB under) | 19 to 22 ms | 49 ms | 30 ms |

Now the two levels follow the guitar: its own peak, which is let fall over ten seconds of playing and kept through a
pause, and they are as much lower as that peak is under 0.5 (never higher than they were, and no lower than a
hundredth). With that every row is the first one, the bench gives the same figures at `-l 0.5`, `0.05` and `0.01`,
and those at 0.5 are what they were.

Noise, which the levels are there for, was looked at on the made-up guitar at two semitones down, with programs that
are not in the bench:

- Hiss that peaks 60, 50 and 40 dB under the guitar's peak, and hum 40 and 30 dB under it, while the guitar plays:
  as many starts acted on as before, at 0.5 and at 0.05.
- Noise alone for four seconds after playing (hiss that peaks from 90 to 50 dB under full scale, hum at 50): no
  start, and the next note is as far behind as ever (0.8 ms in the middle, 1.3 at most).
- Noise alone before any guitar has been heard, where the levels are at their lowest: nothing up to hiss that peaks
  70 dB under full scale, a start every 12 seconds at 60 and 1.4 a second at 50. The first note after such a start
  can be 5 ms behind where 1.4 is the most otherwise, since no start is looked for in the 40 ms after one. From that
  note on the levels are the guitar's.

Not looked at on recorded guitar: the recordings were not at hand that day.

A correction to the bench: a start in the shifted sound is timed at the level the recording passes there, not at a
share of what follows. A pick touching the string 40 ms before a loud note had made both shifters look 41 to 47 ms
late once.

What the bench does not say: how any of it sounds (listen to the files `-w` writes), and how a DigiTech Drop would
measure.
