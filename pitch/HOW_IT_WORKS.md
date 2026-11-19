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
crossfades from the old read point to the new one over 5 to 10 ms.

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

## Mending the seams

Even at the best jump, some overtones of a chord do not fit: they arrive at the seam out of step. A plain crossfade
makes such an overtone dip, and its sudden step in phase is heard as a small blip in pitch. This was the largest
remaining fault.

The fix is to handle each overtone by itself:

1. In the background, the shifter keeps a **model of the ringing sound**: a list of up to 48 overtones (70 Hz to
   5 kHz), found as peaks in a spectrum, with their levels and phases fitted to the actual samples all at once (least
   squares).
2. At a seam, each overtone is measured at both read points. Its two out-of-step copies are taken out of the
   crossfade, and one clean copy is put in that keeps its level and turns its phase **slowly, over 85 ms**, from the
   old value to the new one.

Whatever the model does not know about is still crossfaded quickly, so a new note is never held back.

## What it is based on

The code names no sources; these are the known techniques it belongs with.

| Part | Family of technique |
|---|---|
| Reading a delay line at a different speed and crossfading at jumps | Delay-line (splicing) pitch shifting, the method of the early harmonizers (Eventide H910, 1975) and of most pitch pedals |
| Choosing the jump by how alike two stretches are | Waveform-similarity splicing: SOLA and WSOLA (Verhelst and Roelands, 1993) |
| Treating a ringing sound as a sum of overtones with level, frequency and phase | Sinusoidal modelling (McAulay and Quatieri, 1986) |
| Finding note starts from high-frequency bursts and per-range level rises | Standard onset detection |

What is its own is the combination: long, rare jumps for the sake of chords, the lag taken back at every note start,
and seams mended overtone by overtone.

Two other things it was measured against:

- **RocksmithAudioBridge's drop pedal shifter** (MIT, by Cheesewizard) is the baseline on the bench. It is also a
  delay-line shifter, with short, frequent jumps: about ten seams a second against four.
- **`pitch_shift.c`**, the first design, follows a published low-latency approach (Juillerat, Schubiger-Banz and
  Müller Arisona, 2008): split the guitar into many narrow bands and shift each band's frequency directly, with no
  seams at all. It lost: chords only come out clean at 25 to 40 ms of delay, for every note including new ones.

## Limits

- **Bends and vibrato** are the one kind of playing where the baseline is cleaner: a long jump lands a moving pitch
  somewhere else.
- **Soft notes under a loud ringing chord** are sometimes not detected as new, and then come out as late as the lag
  (14 to 19% of notes in the music excerpts are more than 10 ms behind).
- The note-start trick only works for downward shifts.
- It has never been measured against a real DigiTech Drop, and not yet played with a guitar in the game.
