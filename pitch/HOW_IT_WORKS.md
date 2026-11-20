# How the pitch shifter works

This is about `splice_shift.c`, the shifter that came out ahead on the bench. [README.md](README.md) has the
measurements; this file explains the idea.

## The goal

Make a live guitar sound as if it were tuned a few semitones lower, like a DigiTech Drop pedal does. Two things are
hard to have together:

- **Little delay.** The player must not feel a note coming late.
- **Clean chords.** Several strings ringing at once must not wobble or turn chorus-like.

## The basic idea: read slower, then catch up

The incoming guitar is written into a short memory (a ring of about a third of a second). The output is read from
that memory a little slower than the input is written. Playing a sound back slower lowers its pitch, exactly as a
slowed tape does.

The catch is that the read point falls further and further behind. One semitone down, the lag grows by about 56 ms
every second. So now and then the read point has to **jump forward** to catch up. Each jump is a **seam**: the output
crossfades from the old read point to the new one over 5 ms.

Everything else in the shifter is about making those seams rare and hard to hear.

## Where to jump

A ringing note repeats itself: every period looks like the one before. If the jump is exactly a whole number of
periods long, the sound at the new read point looks the same as at the old one, and the seam is nearly inaudible.

The shifter finds such a jump by comparing the newest 32 ms of sound with the same stretch a little earlier, for
every candidate jump length (a normalised correlation: 1 means identical). For a chord it looks for the stretch after
which *all* the notes come round together, which is why it looks back so far.

## When to jump

Real chords never repeat exactly (the best match is about 0.8 alike), so every seam costs a chord something. The
shifter therefore makes **few, long jumps**: it lets the lag grow to 30 ms and then takes back as much as one good
repeat allows. Two low notes close together only line up after 40 to 60 ms, so for those the lag may grow to 62 ms.

## High notes: one repeat at a time

Few, long jumps are right for chords and for low notes. For a note high up they are wrong, for two reasons.

A real string's overtones are not exact multiples of its lowest one, and high up the neck they are further off: on a
recorded guitar the second overtone is 9 to 13 cents sharp of twice the first from the middle of the neck up. Over
one period that hardly matters. Over a jump of 25 ms, which is 22 periods of a note at 880 Hz, the overtones have
slid half a turn against each other, and no jump fits them all. And high notes are where bends and vibrato happen:
25 ms later a moving pitch is somewhere else.

So a single note from about F3 up that rings **alone** is treated the other way round: the read point jumps **one
period at a time**, as soon as it has fallen one period behind. Each jump is so short that everything still fits,
bent notes included. The crossfade of each jump lasts until the next jump is due, so the seams run into one another
and the sound is never switched, only blended. Such a note is only 1 to 6 ms behind while it rings.

"Alone" matters, and is checked twice. One-period jumps only work for sound that repeats with the note. Anything
else under it, say a string left ringing quietly, is not shifted by them at all: it comes out at its old pitch,
chopped up, and through a distorted amp that is a second, wrong note under the played one. So the note has to
repeat 99% alike from one period to the next, and the model of the ringing sound (below) must show nothing
that does not belong to the note, down to 46 dB under it. Otherwise the note is handled the patient way, as a chord.
That check needs the model, which exists from about 70 ms into a note.

## Why the lag is not felt: note starts

A 30 ms lag would feel late, if it applied to the start of a note. It does not. When a new note starts, the read
point jumps almost all the way to the input at once (half a millisecond behind), whatever was ringing before. The
pick attack hides that seam.

So the long lag only ever applies to notes that are already ringing, where lateness cannot be felt. A new note comes
out about 1 ms late.

A note start is detected in two ways:

- **A pick**: a sudden burst of high frequencies.
- **A note new in its own frequency range**: the guitar is split into 35 ranges (5 wide ones, 30 narrow ones of a
  sixth of an octave), and a range that is suddenly louder than it has been for the last 45 to 90 ms means a new note.
  This catches hammer-ons, pull-offs and soft notes that have no clear pick.

Both need a level under which nothing counts, or noise would start notes. That level follows how loud the guitar
itself comes in: its peak, let fall slowly while it plays and kept through a pause. A quiet guitar's hammer-ons are
then found as a loud one's are.

For 40 ms after a start no other jump is made: the lag is short then anyway. All of this belongs to a downward shift.
Shifting up, the lag shrinks by itself and starts are not looked for, so a pause left over from a downward shift
ends when the shift changes.

## Seams are plain crossfades

At a seam the output simply crossfades, over 5 ms, from the old read point to the new one. Between seams, what comes
out is exactly the guitar's own waveform, played slower.

That matters because Pitch Shifter is always played through an amp, and **distortion makes its sound out of the
shape of the waveform**. Anything that changes the shape, even in a way an ear would not notice in the plain guitar
sound, comes out of a driven amp as a different sound.

An earlier version did more at a seam. Even at the best jump, some overtones of a chord arrive out of step, and in
a crossfade such an overtone dips for a moment. So that version kept a model of the ringing sound (up to 48
overtones) and, at each seam, carried every overtone across by itself, turning its phase slowly over 85 ms
("mending"). In the plain guitar sound that measured clearly steadier, and it sounded good in listening tests of
the plain sound.

Played in the game it did not: chords had clear artifacts and picks came with a static-like noise. The reason
showed when the player's own recorded guitar was run through the shifter and then through the actual amp tone,
and compared with the true thing (the same recording played back slower, through the same tone). For 85 ms after
every seam the mended overtones are out of step *with one another*, each on its own way to its new phase. The
waveform is then not a guitar's, and the amp turns that into a wrong sound for 60 ms or more, several times a
second. A plain seam is wrong for its 5 to 10 ms and then exactly right again. Through the driven tone the plain
seams came out 4 to 7 dB closer to the true thing than the build that was played, on every kind of playing, and
level with it through a clean one
([README.md](README.md) has the figures).

So the mend was switched off, played that way, and then taken out of the code (the commit before that one has
it). The model of the ringing sound is still built, because the check for a high note being "alone" needs it.

### No steps

A note start does not wait for anything. If the shifter is in the middle of a crossfade when one arrives, it does
not drop the read point that was being faded out, which would make the output jump from one sample to the next (a
click, as large as a tenth of full scale at ordinary picks). That read point keeps fading out underneath, for as
long as the new crossfade lasts.

### Small pieces of work

The model of the ringing sound is rebuilt ten times a second, and building it is the heaviest thing the shifter
does. It all runs where the sound is made, in buffers that may last only 1.3 ms and are shared with the amp
plug-in, so the building is cut into pieces of about a tenth of a millisecond, one per 64 samples.

## What it is based on

The code names no sources; these are the known techniques it belongs with.

| Part | Family of technique |
|---|---|
| Reading a delay line at a different speed and crossfading at jumps | Delay-line (splicing) pitch shifting, the method of the early harmonizers (Eventide H910, 1975) and of most pitch pedals |
| Choosing the jump by how alike two stretches are | Waveform-similarity splicing: SOLA and WSOLA (Verhelst and Roelands, 1993) |
| Treating a ringing sound as a sum of overtones with level, frequency and phase (the model that says whether a note is alone) | Sinusoidal modelling (McAulay and Quatieri, 1986) |
| Finding note starts from high-frequency bursts and per-range level rises | Standard onset detection |
| One-period jumps for a single note | Pitch-synchronous splicing (PSOLA, Moulines and Charpentier, 1990) |

What is its own is the combination: long, rare jumps for the sake of chords, short ones for high notes, and the lag
taken back at every note start.

The idea of the mod is from [RocksmithAudioBridge](https://github.com/Cheesewizard/RocksmithAudioBridge)'s Drop
Pedal (MIT, by Cheesewizard), and that mod's shifter was the benchmark on the bench while this one was written.

`pitch_shift.c`, the first design, follows a published low-latency approach (Juillerat, Schubiger-Banz and Müller
Arisona, 2008): split the guitar into many narrow bands and shift each band's frequency directly, with no seams at
all. It lost: chords only come out clean at 25 to 40 ms of delay, for every note including new ones.

## Limits

- **Bends and vibrato** are its weakest kind of playing wherever the long jumps are used, which is on low notes
  (below about F3) and on any note with something else ringing under it: a long jump lands a moving pitch somewhere
  else. A higher note that rings alone is taken one period at a time and follows them closely.
- **A ringing chord** never repeats exactly, so at each seam (three or four a second at two semitones down) some of
  its overtones dip for a few milliseconds. In the plain guitar sound an overtone's level moves about by half a dB
  because of it. And a pick into a ringing chord puts the chord itself, not only the new note, through a quick seam.
- **Soft notes under a loud ringing chord** are sometimes not detected as new, and then come out as late as the lag
  (14 to 19% of notes in the music excerpts are more than 10 ms behind).
- The note-start trick and the one-period jumps only work for downward shifts.
- It has never been measured against a real DigiTech Drop.
- The plain seams have been measured through the amp tones the player uses, on the player's own recorded guitar,
  and played in the game (2026-11-20): they sounded good, where the mended versions before them had the artifacts
  described above. That is one player, one guitar and two tones.
