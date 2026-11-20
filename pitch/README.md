# pitch

The shifter behind the Pitch Shifter mod ([`src/mod_pitch_shifter.c`](../src/mod_pitch_shifter.c)): the live guitar
a few semitones down, chords included, to be judged against a DigiTech Drop pedal for delay and sound.
`splice_shift.c` is built into the library; the rest is how it was arrived at and measured.

| | |
|---|---|
| `splice_shift.c` | the shifter that came out ahead: reads the guitar back a little slower, takes the lag back in few, long jumps where the sound repeats and at once when a note starts, with a plain crossfade of 5 ms at each of those seams; a note high up is taken one repeat at a time |
| `pitch_shift.c` | the first design, a row of narrow bands each moved in frequency. Kept for the comparison: it needs 25 to 40 ms of delay before chords come out clean |
| `bench/` | the bench that measures them |

## The bench

```bash
pitch/bench/run.sh
```

It plays made-up guitar (single notes, notes high up with their overtones off whole multiples as a real guitar's
are, power chords, strummed open chords, two low notes close together, a high note with a quiet one under it, a
note that stops as the next one starts, palm mutes, a run, bends, vibratos) through each shifter and measures the result against the same guitar made at the
lower pitch.
`bench/bench.c` says what each column is. Arguments: `-s -1,-2` the shifts, `-v` every case, `-l 0.05` the peak the
made-up guitar comes in at (0.5 without it), `-w DIR` writes what each shifter made as `.wav`, clean and through a
distortion, for listening.

Recorded guitar goes through the same shifters with `-r`. The bench times how late each note's start comes out,
measures the stretches that ring on, and writes the results for listening:

```bash
pitch/bench/prepare.sh RECORDINGS_DIR some-guitar.wav P1_chords.zip
```

```bash
pitch/bench/run.sh -w OUT_DIR -r RECORDINGS_DIR/*.wav
```

`prepare.sh` makes 48 kHz mono files, and takes the dry tracks out of
[Guitar-TECHS](https://zenodo.org/records/14963133) archives (CC BY 4.0; not in this repository). A recording with its
notes beside it as a MIDI file of the same name (the dataset's `midi_NN.mid` copied to `P3_music-NN.mid`) also gets
every note's start looked at, those under ringing notes included, and its ringing stretches measured by pitch, an
octave to a row. `-f` and `-t` say where in each recording to start and how many seconds to take (0 and 60).

There is an ideal for all of a recording after all: the recording played back slower, which lowers every sound by
the shift and changes nothing else but its length. Each moment of what a shifter made is compared with the same
moment there, both driven hard as an amplifier would: `extra` is what the shifter's sound has that the true one has
not, `lost` the other way round.

[RocksmithAudioBridge](https://github.com/Cheesewizard/RocksmithAudioBridge)'s Drop Pedal shifter was the benchmark
while this one was written. Its source is not in this repository; with a checkout of it the bench runs it too, as
`rab`:

```bash
RAB_DIR=/path/to/RocksmithAudioBridge pitch/bench/run.sh
```

## What it found (2026-11-18, one and two semitones down)

These are figures of the shifter as it was then, with its seams mended, and of its own sound, with no amplifier
after it. The mend is gone since 2026-11-20: [Through an amplifier](#through-an-amplifier-2026-11-20), below, says why
and what the figures are without it.

`splice` on recorded guitar (Guitar-TECHS: player 1's dry tracks and the twelve music excerpts):

| | one semitone | two semitones |
|---|---|---|
| A note's start after a quiet moment comes out late by, in the middle | 0.6 to 1.0 ms | 0.6 to 1.2 ms |
| Nine such starts in ten are within | 6.9 ms | 6.0 ms |
| Chords (ten recordings): an overtone's level moves about by | 0.22 dB | 0.26 dB |
| Chords: what lands off the overtones is down by | 47 dB | 43 dB |
| Music excerpts: the same two | 0.09 dB, 36 dB | 0.07 dB, 34 dB |

- Single notes, bends and palm mutes are clean: a single note's level moves about by 0.03 to 0.05 dB.
- While notes ring, `splice` is 12 to 20 ms behind on average, 30 at most as a rule and up to 62 for a sound that
  only repeats that late (since 2026-11-19 a note from F3 up is 1 to 6 ms behind: High notes, below). It makes a seam only when its lag has reached 30 ms (about four a second), and a new note
  takes the lag back. `most_ms = 20` with `nearly = 0.2` brings that to 10 to 13 ms and halves the late notes of the
  next point, with chords and music as clean; single notes, palm mutes and bends then have their first seam sooner,
  and what lands off the overtones is 4 to 8 dB more at one semitone. `patient = 0` is the short-lag end: 5 to 8 ms,
  chords 4 dB worse.
- Every note, not only those after a quiet moment: the music excerpts come with their notes written down (1923 of
  them). When a note starts, `splice` is 1 ms behind in the middle, and more than 10 ms behind for 14% of them at one
  semitone and 19% at two. Those are soft notes under louder ringing ones: where the recording shows them, the level
  does not rise at all. Some of the written notes may not be audible starts.
- `splice` takes 1.1 to 1.2% of one processor core on the recordings (1.5% on the made-up guitar). Its slowest block
  of 128 samples took 0.6 to 0.8 ms of the 2.7 ms a block has, and 19 blocks in 20 take under 0.1 ms.

On the made-up guitar, where the ideal is known: open chords' levels move about by 0.21 dB, two low notes a third
apart by 0.28, a ringing chord through the start of a new note by 0.2 to 0.3, and single notes by 0.11.

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
- Mending the seams was the largest gain in the shifter's own sound (and a loss through an amplifier, found two days
  later: off now). At a seam every overtone that the jump does not fit steps in phase. A plain
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

Nothing falls apart. On the recordings:

| | three | five | seven |
|---|---|---|---|
| Chords: an overtone's level moves about by | 0.31 dB | 0.34 dB | 0.42 dB |
| Chords: what lands off the overtones is down by | 39 dB | 37 dB | 35 dB |
| Music excerpts: the same two | 0.10 dB, 35 dB | 0.10 dB, 34 dB | 0.12 dB, 34 dB |
| A note's start comes out late by, in the middle | 0.7 to 1.2 ms | 0.8 to 1.7 ms | 0.9 to 2.1 ms |

- Single notes and palm mutes stay about as clean as at one and two semitones.
- Bends and vibrato are the weakest kind of playing from three semitones on: a jump of 20 ms lands a moving pitch
  somewhere else. An overtone found more than 3 Hz from where the model has it is no longer mended, which gave back
  1 dB.
- An overtone's tuning is off by more as the shift grows: in the music excerpts 2, 4 and 5 cents in the middle and
  4, 8 and 10 for nine in ten.

`eager` (the bench's `splice-e`) lets a sound that repeats cleanly and soon, a single note or a power chord, jump as
soon as its lag holds one repeat, while chords still wait. While single notes ring the lag is then 9 to 10 ms where
it was 18, in the music excerpts 7 to 13 where it was 12 to 17, and the notes found more than 10 ms behind are 7, 11
and 15% (at one, two and five semitones) where they were 14, 19 and 20%. Single notes' levels are steadier for it
(0.01 dB), chords are unchanged, and what lands off the overtones is 2 to 3 dB more on palm
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

### High notes (2026-11-19)

Played in the game: "It works great on low notes, but the higher you go, the worse it gets." The bench had not
shown it: its made-up guitar's overtones sat almost on whole multiples of the first, its highest single note was E5,
it held every note still, and no case had a note stop. On the recording of every note of the neck (Guitar-TECHS,
134 notes) it was there. How unsteady an overtone's level came out while a note rang, at two semitones down (the
root of the mean square of its level against the recording's own, in dB):

| Notes | first overtone | second | third | fifth |
|---|---|---|---|---|
| E2 to A2 | 0.07 | 0.17 | 0.25 | 0.30 |
| E3 to A3 | 0.25 | 0.30 | 0.19 | 0.49 |
| E4 to A4 | 0.20 | 0.68 | 0.55 | 0.49 |
| A#4 to D#5 | 0.18 | 0.60 | 0.74 | 0.71 |
| E5 to A5 | 0.26 | 0.81 | 1.03 | 1.21 |

Two things made it so:

- **A high note does not repeat over a long jump.** A real string's overtones are not whole multiples of its first.
  On that guitar the second is 4 to 5 cents sharp of twice the first up to A3 and 9 to 13 cents from there to A5
  (the first is the one that is flat). At 880 Hz that is 18 Hz: over a jump of 25 ms the two have slid half a turn
  against each other, and no place fits. So every seam of a high note had its overtones far out of step, the mend
  had the most to turn, and what it assumed (each overtone as loud and as high as fitted, for 85 ms more) held
  least: a high note dies away faster, and a few cents are more Hz up there. With a vibrato it was worst: on the
  recorded vibratos, from A#4 up, a seam put the first overtone's level off by 0.9 to 1.9 dB in the middle and 4 to
  5 for one seam in ten, and its pitch by 9 to 10 cents in the middle and 20 to 45 for one in ten.
- **The note before was sounded on.** At a note's start the overtones of what rang before were carried across the
  seam, on trust that they ring on. A note that stops as the next one starts, as on one string, does not: its
  overtones came out for 80 ms more, the old note under the new one, at times louder than it had been. A semitone
  or two apart at 600 Hz the two beat 35 to 70 times a second. The unit test for it fails on the shifter as it was
  in 15 of 16 cases.

What was done:

- **A close repeat.** A sound that comes round cleanly within 6 ms (a single note from about F3 up, or two that
  share so short a period) and is all there is, is taken one repeat at a time, as soon as the lag holds one: placed
  at the read point over a few periods, and looked for again when the lag has grown by it. Its crossfade is a
  straight line as long as the lag takes to grow by the repeat (25 ms at most), so one seam runs into the next, and
  an overtone a little off the repeat comes out turning evenly and not in steps. There is no mend: nothing is left
  for one to carry.
- **An overtone that is no longer there is given up.** While overtones are carried across a seam, what the guitar
  really has of each is kept track of. One that has gone (a muted string, a note that stopped) is let go within a
  few milliseconds. After a start more is asked: an overtone that is not there as it was fitted, quieter, louder or
  at another phase, is let go too, since a start is where notes are replaced and its attack covers a quick turn.

#### "Like octaver on"

The first version of the close repeat asked only that the two places be 0.99 alike. Played in the game the same
evening, through a Plexi at two semitones down: "High notes sound bad as single notes, it's like octaver on." On
the recordings nothing of the kind showed: single notes, a melody cut together from them and the music excerpts all
came out cleaner than before, driven hard or not. It was not the shifter: played without headphones, the guitar's
own strings are heard beside the speakers, two semitones over what comes out of them ("there wasn't much wrong in
the sound after all"). Looking for it found a weakness all the same:

- Jumps of one repeat shift only what comes round with the repeat. Any other sound under the note is left at its own
  pitch and chopped as often as there are seams. With a recorded E4 mixed 30 dB under a recorded A5 and the result
  driven hard, wrong tones stood 23 dB under the note where the shifter as it was had none; 40 dB under, 33 dB.
- 0.99 alike leaves room for a sound 20 dB down. The recorded notes are each played alone into silence. A guitar
  being played has the note before, or a string left ringing, under most of its notes.

So the model of the ringing sound is asked as well: every overtone it has found, down to 45 dB under the loudest,
has to sit at a whole multiple of the repeat or a string's own little way off it, to within 46 dB of the whole
(`ALONE`), and each new model is asked again. With the E4 40 dB under, the A5 now waits like a chord and comes out
as clean as before; 46 dB under it is still taken a repeat at a time, and what is wrong is 38 dB down, where the
guitar's own is 40. Until there is a model, 70 ms into a note, nothing is taken so. The version with this has not
been played.

To hold a shifter against something on real playing, the bench has the true thing now: the recording played back
slower, which lowers every sound and changes nothing else, compared with the shifter's sound moment by moment, both
driven hard (`extra` and `lost`, `bench.c` says how). What the shifter's sound has that the true one has not, in dB
against all of it, before and now:

| | one semitone | two | five |
|---|---|---|---|
| Twelve music excerpts | 24.4, 29.9 | 22.9, 29.3 | 20.8, 27.0 |
| Ten chord recordings | 35.8, 36.7 | 33.0, 35.1 | 30.8, 31.7 |
| Vibratos and bends | 28.3, 29.5 | 27.4, 29.2 | 26.9, 28.6 |
| Single notes | 29.2, 29.2 | 29.0, 29.1 | 28.1, 28.9 |
| The player's own guitar, three takes of a song | 23.0, 34.8 | 21.8, 33.1 | 22.1, 30.8 |

Most of what the music gains is notes no longer sounded on after they have stopped (the next parts): the close
repeat by itself is worth half a dB there. What the true one has and the shifter's has not (`lost`) is, on the
music, 34.3, 33.2 and 31.8 dB down where it was 30.1, 28.4 and 27.4.

#### A burst in a gap

Played again, with that build: "I hear kind of rapid single static sound when playing ... It almost sounds like
it's clipping", in one song above all, and only with the shift on. Tone Host kept the guitar of that session
(`keep_guitar_s`, its README), and there it was: the song has short gaps between notes, and a seam that falls just
after a note has stopped carried the note's overtones into the gap. A fit is of the 85 ms before a read point: it
says what rang over them, not what still rings at their end, so a note that stopped 20 ms before passed for one
that rings. The shifter as it was sounded it on for 85 ms; the giving up above had cut that to a burst of 20 ms, 26
dB under full scale in a gap where the guitar is 40 under, which is what was heard as static.

So before a seam the new read point's last 10 ms are gone through as the glide goes through what follows, and an
overtone it has not had half of is not carried at all. And while overtones turn, one the read points have less of
than was fitted is mended that much less, so that a string muted during a turn takes its mend down with it and is
not cut off 15 ms later.

- On that recording, at two semitones down: stretches of 5 ms where the shifter's sound is 10 dB over anything the
  guitar has within 15 ms of where it is reading: nine as it was, four in the build that was played, one now, which
  is a note's start. Against the true thing: the table's last row.
- The made-up notes with a gap after each (`left`, 70 to 110 ms into the gap, the worst gap, against the note's
  last 60 ms): 44, 42 and 42 dB down at one, two and five semitones, where it was 41, 23 and 20 (at one semitone
  no seam falls in a gap of these notes); the made-up note itself is 60 down by then.
- The unit test for it fails on the shifter as it was for 7 lengths of note in 24, and on the build that was played
  for one.

#### Clicks at a note's start

Played with that in: "There is still clear static." The second take Tone Host kept showed something else, and
older than all of the above: at a note's start the output stepped from one sample to the next by as much as a tenth
of full scale, where the guitar itself never moves by more than a quarter of that. 19 such steps in 105 s of that
take, 46 through the shifter as it was. Two ways to one:

- A start takes the read point at once, whatever the shifter is in the middle of. If that is a fade from one read
  point to another, the one being faded out was dropped on the spot, and with it whatever it still gave. That
  happened at ordinary picks: the first of a pick's rise passes for "may be a soft note", which starts an ordinary
  jump with a fade of 10 ms, and a few samples later the same pick is a start.
- Overtones being carried were dropped as abruptly, when a start came within 85 ms of a mended seam and there was
  no model yet to carry them on with.

What was done:

- What the output loses at such a moment, against where its last two samples were heading, is put back and let out
  over 2 ms: the same change without the step (`PATCH`).
- A small rise waits 3 ms before it shortens the lag, by when a pick has become a start (`SOFT_WAIT`).

- Steps of more than two and a half times the guitar's largest nearby: none in the second take and two in the
  first, both a fade doing what it should, where there were 19 and 10, and 46 and 42 through the shifter as it
  was.
- The unit test for it (notes starting every 150 ms over the ones before) fails at all three shifts on the shifter
  as it was and on both builds played before.

That build also left a start's seam unmended (`start_mend_ms` 0), for what the read point's last 10 ms cannot
show: a note that stops at the start itself, the same string picked or fretted again, is carried into the new one
for the 10 ms it takes to find out. Played: "The clicking has reduced. There still a little. I also observed some
wobbling when playing slow chords", "also some noise". The wobble is that: with no mend at a start, every pick
into a ringing chord steps the chord's overtones in phase (the bench's `waver` on the ten chord recordings: 0.47,
0.51 and 0.57 dB at one, two and five semitones, against 0.31, 0.35 and 0.41 as it was). So a start's seam is
mended again, and the note before overlapping the new one by 10 ms is the lesser of the two.

#### Heavy blocks

Of the clicks that were left, the third take showed none in what the shifter makes of it: no step, and one
millisecond in 138 s with more high end than the guitar has there. What was left to suspect is time. Tone Host
runs the shifter in buffers of 64 samples, 1.33 ms each, with the plug-in in the same 1.33 ms, and ten times a
second the shifter took 0.3 to 0.6 ms of one buffer: the spectrum of a model in one go, and the matrix of a fit of
a full chord in another. A buffer that is not ready in time is a gap in the sound, and only with the shift on.

- A model is made in pieces of a tenth of a millisecond now, one every 64 samples (`model_step`): the transform in
  five, the matrix in up to seven, each fit in two.
- On the three takes, in buffers of 64: 0.5 to 0.9 buffers a second take over 0.3 ms where 9.6 to 9.8 did, and 3
  over 0.2 where 12 did (those left are seams, whose two fits are not spread). The slowest takes 0.4 to 0.5 ms, as
  before. 1.3% of a core.
- Tone Host's log says, every five seconds, how long its slowest buffer took and the shifter's share, and counts
  the overloads the system tells of (its README). Whether buffers were late in the game is not known: that log did
  not exist when it was played.

Not found: the noise. Nothing in the three takes through the shifter shows it.

#### After both

On the recorded single notes, the measure of the first table at two semitones down:

| Notes | first overtone | second | third | fifth |
|---|---|---|---|---|
| E2 to A2 | 0.07 | 0.17 | 0.25 | 0.30 |
| E3 to A3 | 0.08 | 0.11 | 0.13 | 0.25 |
| E4 to A4 | 0.13 | 0.15 | 0.30 | 0.23 |
| A#4 to D#5 | 0.02 | 0.14 | 0.44 | 0.16 |
| E5 to A5 | 0.02 | 0.15 | 0.26 | 0.10 |

By the bench (`-t 600 -r` on the single notes recording with its notes beside it), what lands off the overtones,
in dB, before and now; the recording's own is 47.8 down:

| | E2 to D#3 | E3 to D#4 | E4 to D#5 | E5 and up |
|---|---|---|---|---|
| One semitone down | 46.7, 46.7 | 47.3, 47.4 | 47.6, 47.5 | 47.3, 48.9 |
| Two | 44.9, 44.8 | 44.9, 47.4 | 43.2, 48.7 | 41.7, 48.5 |
| Five | 44.6, 44.5 | 43.2, 47.4 | 40.0, 46.6 | 37.1, 47.4 |

- An overtone's level while such a note rings moves about by 0.01 to 0.02 dB where it was 0.05 to 0.17.
- The lag while a note from F3 up rings alone is 1 to 5 ms at one and two semitones down and 1 to 7 at five, where
  it was 16. Of the written notes of the vibrato and bend recordings 7, 13 and 13% find the shifter more than 10 ms
  behind at one, two and five semitones, where it was 18, 21 and 20%.
- Recorded vibratos at two semitones down, from E4 up: a seam puts the first overtone's level off by 0.1 dB in the
  middle and 0.2 for one seam in ten (it was 0.6 to 1.9 and 1.6 to 5.2), and its pitch by 0.4 to 2.6 cents and 1
  to 6 (it was 9 to 16 and 22 to 47). Over the whole of such a note its level is off by 0.03 to 0.5 dB where it was
  0.3 to 1.1: a vibrato smears the model, which then at times finds the note not alone.
- A note that stops as the next one starts (the bench's `left`, 30 to 80 ms on, against while it rang): 38, 35 and
  26 dB down at one, two and five semitones, where it was 14, 19 and 9; the made-up note itself dies away to 39, 35
  and 32.
- The made-up high notes: what lands off the overtones is 70 dB down at two semitones and 66 at five, where it was
  45 and 35. With a quiet note under them: 58, 44 and 38 down at one, two and five, where it was 62, 45 and 34. The
  cases that move (palm mutes, a run, bends, vibratos) differ from the ideal by 1.6, 2.0 and 2.7 dB where they did
  by 3.1, 3.4 and 3.8.
- Low notes, power chords, open chords and two low notes close together measure as they did.
- What it costs: on the ten chord recordings an overtone's level moves about by 0.37, 0.42 and 0.48 dB at one, two
  and five semitones where it did by 0.31, 0.35 and 0.41, and what lands off the overtones is 45.0, 40.7 and 36.0
  dB down where it was 45.1, 42.0 and 36.5. That is the giving up after a start: a string picked into a ringing
  chord puts its overtones where the chord's are, those are then not as fitted, and their turn is made quickly,
  under the pick. Whether an overtone was played over or joined cannot be told in the first milliseconds. Against
  the true thing the same chords are nearer than before (the table above).
- The processor time is 1.3% of a core on the player's takes, as it was; how it falls on single buffers is under
  "Heavy blocks".
- How far down: with 4 ms the notes from B3 up are taken so, with 8 ms those from B2. From E3 to A3 it is better
  on every overtone, from A#2 to D#3 on some and worse on the fifth, so 6 ms.
- An overtone a little off the repeat keeps its distance from the whole multiple in Hz where the ideal would
  shrink it with the pitch: on the made-up high notes the worst overtone is 0.6, 1.2 and 3.3 cents off its place
  at one, two and five semitones, where it was 0.3, 0.9 and 3.2.
- Tried: a carried overtone made to follow what the guitar has, in level and phase, in place of being given up. A
  new note's overtone 40 Hz from a carried one was then taken for it and turned with it: a hole of 18 dB in the
  new note for 10 ms. And giving up at any seam on the measure asked after a start: the made-up chords lost 3 to
  7 dB.

Not looked at: upward shifts, which have none of this. Looked at with programs that are not in the bench: the
tables by overtone, what a single seam does, the note with another mixed under it, the bursts and steps, and the
time a buffer takes. None of it has been played with the last part in.

### Through an amplifier (2026-11-20)

Played with all of the above in: "There is very clear artifacts when playing chords. Also, when the pick hits the
string, that's when the 'static' noise comes from", and none of it with the shift at 0. Tone Host's new log line
settled what "Heavy blocks" had suspected: the slowest buffer of that session took 0.65 ms of its 1.33, none was
long, the system told of no overload. It was not time.

What had not been looked at is what is heard. Every figure above is of the shifter's own sound, and Pitch Shifter is
never heard that way: an amplifier comes after it, in that session a Plexi with a drive pedal in front for most of
the song and a clean preamp for the rest. So the takes Tone Host had kept were put through the shifter and then
through those two tones (Helix Native's engine run by Tone Host's own code, no device, delay and reverb left out),
and held against the true thing through the same tone: the take played back slower. Moment against the same
moment, spectrum against spectrum, in windows of 85 ms (what the shifter's sound has that the true one has not, a
bin 10 dB over the true one's loudest within 25 Hz, as power against all of it) and of 5 ms (6 dB over).

Through the driven tone at two semitones down, in dB down, 85 ms:

| | as it was at first | the build that was played | mended on trust | plain seams |
|---|---|---|---|---|
| The player's four takes | 21 to 23 | 31 to 34 | 27 to 31 | 35 to 41 |
| Three chord recordings | 24 to 29 | 30 to 33 | 28 to 31 | 38 to 40 |
| Single notes | 31 | 37 | 35 | 43 |
| Vibratos | 33 | 38 | 36 | 42 |
| Three music excerpts | 24 to 28 | 32 | 29 to 32 | 36 to 37 |

"Mended on trust" is the mend without the giving up that was added on the way; "plain seams" has no mend at all:
a seam is its crossfade and nothing else.

- **A mend is heard for as long as it lasts.** By time since a seam, 150 ms and more after a start, on the fourth
  take: a plain seam is 18 dB down for its first 10 ms and 37 and more after that. A mended one is 15 to 18 down
  for 60 ms. And there is a seam three to four times a second.
- **Why.** A mended overtone takes 85 ms to turn from where the old read point had it to where the new one has it,
  and each overtone turns its own way. For those 85 ms the overtones are out of step with one another, which the
  ear does not mind in the guitar's own sound: there a mended chord is the steadier one (0.35 dB against 0.59
  below). A distortion makes its sound out of the waveform, and a waveform whose overtones are out of step is
  another waveform. After a plain seam what comes out is the guitar's own again, exactly.
- **At a pick**, the 30 ms after each start the shifter acts on, in windows of 5 ms, on the four takes: 19 to 23
  dB down with plain seams, 15 to 19 in the build that was played, 12 to 14 as it was at first. What was carried
  across a start on trust is what rang before, sounded on under the new note.
- At one and five semitones down, the second and fourth take: 38 and 32 to 33 dB down with plain seams, 33 to 36
  and 27 to 28 in the build that was played.
- Through the clean tone the two are level in windows of 85 ms (the four takes 31 to 37 dB down both ways, chords
  25 to 29 both ways, the music 31, 37 and 25 plain where the played build has 31, 39 and 30), and in the 5 ms
  windows after a start plain is the nearer by 1 to 9 dB.

What was done:

- **Seams are not mended.** At first by a setting (`mend` 0) with the code left in; once it had been played, the
  code was taken out, and what it made is the same sample for sample. The model of the ringing sound is still
  made, since the close repeat asks it whether a note is alone.
- **Every seam fades over 5 ms.** A less alike one had twice that: 2 dB worse on the fourth take through both
  tones, and no better anywhere. 2.5 ms and 10 and 20 ms were tried too, and at a start half a millisecond, two
  and four in place of one: nothing gained.
- **A start during a fade lets it fade on.** The read point that was being faded out is not dropped and not
  smoothed over (`PATCH` had left a bump of 2 ms as large as what was cut, a seventh of full scale once in the
  fourth take): it goes on fading out, under the new fade.

What it gives and costs in the shifter's own sound, at one, two and five semitones down (mended in brackets):

| | | |
|---|---|---|
| Ten chord recordings | an overtone's level moves about by | 0.45, 0.59, 0.77 dB (0.27, 0.35, 0.43) |
| | what lands off the overtones is down by | 41, 35, 30 dB (45, 41, 37) |
| | against the true thing, driven (`extra`) | 38, 36, 34 dB down (35, 33, 30) |
| Twelve music excerpts | an overtone's level | 0.35, 0.19, 0.31 dB (0.17, 0.09, 0.15) |
| | off the overtones | 35, 30, 30 dB (35, 34, 34) |
| | `extra` | 31, 30, 30 dB down (28, 28, 26) |
| The player's four takes | `extra` | 38, 37, 36 dB down (31, 30, 29) |
| Single notes, vibratos, bends | all three | as they were |

- The steadiness of a ringing chord's overtones is what the mend was made for and what is given back: a chord
  through a clean tone is where to listen for it. An overtone's tuning is the nearer without the mend (nine in ten
  within 1.5, 2.6 and 6.3 cents on the chords, where they were within 2.4, 4.0 and 7.5).
- The four takes through the shifter alone at two semitones down: steps of over two and a half times the guitar's
  largest nearby: 0, 0, 0 and 2 (42, 46, 37 and 60 as it was at first).
- A note's start comes as soon as it did, and the lag while notes ring is what it was.
- 1.1% of a core, and the slowest buffer of 64 samples takes 0.18 ms where it took 0.4 to 0.5: the two fits of a
  mended seam are gone.

Played with a guitar in the game on 2026-11-20, with plain seams: "sounds good". One reading above is in doubt: "Clicks at a note's start" put the wobbling
of slow chords down to a start's seam not being mended. Every other seam of that build was mended, and what a mend
does to a chord through an amplifier was not known then.

Looked at with programs that are not in the bench, since they need the plug-in: the takes through each build and
a tone, and the two measures against the true thing after it.

### Upward (2026-11-19)

A note's start takes the lag back under a downward shift alone, and it shows. On the made-up guitar, what lands off
the overtones is down by:

| | two semitones up | five up |
|---|---|---|
| Single notes | 48 dB | 31 dB, and 23 cents off |
| Power chords | 53 dB | 44 dB |
| Open chords | 47 dB | 33 dB |
| Two low notes close together | 31 dB | 16 dB |

- A note after a quiet moment comes through at once (0.1 to 1 ms), but at its own pitch: the shifted overtones come
  up 3 to 29 ms later. A note while others ring is as late as the lag is then.
- Twelve up is past what `splice` does: single notes 12 dB down.
- Tried: the longest repeat in place of the shortest when the lag runs out. Single notes gain 7 to 19 dB (48 dB down
  at five up, 31 at twelve) and are then 14 to 24 ms behind while they ring where they were 4 to 6, and an upward
  shift has no start to take that back at. Not built in.

**The lag ran out (found 2026-11-20, in review).** Upward a seam costs lag itself: the read point goes on gaining
on the input for the 5 ms the crossfade lasts and the 1.3 ms to the next look, by 107 samples at five up and 320 at
twelve. A note whose repeat is shorter than that (from about A4 at five up, D4 at seven, D3 at twelve) gave back less at each seam than the seam took, the lag ran out, and the note came out at its own pitch:
that was the "23 cents off" above and all of "twelve up is past what `splice` does". Now as many of the shortest
repeat are taken as cover a seam, placed again where they land. Two up is as it was; what lands off the overtones:

| | five up, before | now | twelve up, before | now |
|---|---|---|---|---|
| Single notes | 37 dB, 25 cents off | 44 dB, 5 cents | 15 dB | 38 dB |
| High notes | 13 dB, 50 cents off | 36 dB, 2 cents | 1 dB | 30 dB |
| A quiet note under | 12 dB | 33 dB | 2 dB | 30 dB |
| Power chords | 39 dB | 39 dB | 35 dB | 35 dB |

The lag while a note rings is what the shift needs and no more: 6 to 7 ms at five up, 15 at twelve (it read 0.4 to
6 ms before, where the lag had run out). The longest repeat, tried above, is still not built in. Not played in the
game: upward shifts have only been measured.

A correction to the bench: a start in the shifted sound is timed at the level the recording passes there, not at a
share of what follows. A pick touching the string 40 ms before a loud note had made the shifter look 41 to 47 ms
late once.

What the bench does not say: how any of it sounds (listen to the files `-w` writes), and how a DigiTech Drop would
measure.
