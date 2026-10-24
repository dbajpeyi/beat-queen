# 8. Clock, matrix and rhythms

The MRK-1 has no memory: every rhythm is wiring. To reproduce the rhythms
you have to understand the clock well enough to know **when each of its
outputs fires**. Then you can read the wiring as music.

## The ring multivibrator

The clock's heart is Q1-Q5, a **5-stage ring**. Exactly one transistor is on
at a time. When stage $k$ turns on, a capacitor couples a negative step into
stage $k+1$'s timing node. That node then charges back up through its timing
resistor from the "Tempo B+" supply, until stage $k+1$ turns on.

The time a node takes to charge from $-\Delta V$ to the turn-on voltage
$V_{th}$, heading towards $V_B$, is

$$t = RC\,\ln\frac{V_B + \Delta V}{V_B - V_{th}}$$

The speed knob changes $V_B$, which changes every stage time in proportion.

The timing resistors are **not equal**: 470K, 240K, 240K, 470K, 470K. So
the stages have lengths 2:1:1:2:2, and one ring cycle is 8 units long.
Taking a unit to be a sixteenth note, the five stages start at sixteenths

```
straight ring cycle (8 sixteenths = 2 beats):   stages at 0, 2, 3, 4, 6
```

**Triplet timing.** Eight of the rhythm switches have "TRIP" contacts. They
put an extra 470K in parallel with stages 4 and 5, halving those stages:
2:1:1:1:1, a 6-unit cycle with a unit of a triplet-eighth:

```
triplet ring cycle (6 triplet-eighths = 2 beats): stages at 0, 2, 3, 4, 5
```

A 6-unit cycle would run faster than an 8-unit one, so the triplet contacts
also tell the **tempo regulator** (Q64/Q65) to lower Tempo B+. That slows
every stage, keeping the beat the same length. It's a neat analog trick,
and it means combining a straight rhythm with a triplet one **re-times the
straight one**. Beat Queen reproduces this.

## Logic sequencer and gates: from 5 stages to an 8-beat cycle

- **The ring repeats 4 times per clock cycle,** giving 20 pulses over 8
  beats, indexed `ring × 5 + stage`.
- **A second 5-stage chain, the logic sequencer (Q11-Q15),** advances at
  particular moments, so at any time exactly one of its stages is "open".
- **Twelve logic gates (Q18-Q29)** each fire on a particular stage pulse,
  but only while a particular sequencer stage is open. Each gate therefore
  fires once or twice per cycle, at a fixed point.

The manual's Description section walks through all 21 steps of a clock
cycle ("Q9 triggers Q12 opening its gate which triggers Q29 and Q13..."). It
reads like a riddle. Tabulated, it is a schedule (pulses written as
(ring, stage)):

| Output | Fires at | | Output | Fires at |
|---|---|---|---|---|
| 6-10 (pulse amps) | stage 0-4 of every ring | | 24 | (0,2) (2,2) |
| 18 | (0,4) (2,3) | | 25 | (1,1) (3,1) |
| 19 | (1,4) (3,4) | | 26 | (0,1) (2,1) |
| 20 | (0,4) (2,4) | | 27 | (1,0) (3,0) |
| 21 | (1,3) (3,3) | | 28 | (0,0) (2,0): also lights the down-beat lamp |
| 22 | (0,3) (2,3) | | 29 | (1,3) (0,0) |
| 23 | (3,1) | | 17 | stage 4 of every ring (hi-hat dampen, Q17) |

## Reading the matrix and selector wiring

Drawing No. 3 shows, for each rhythm switch, which clock outputs feed which
keying point. For example, Bossa Nova has `23,29,18 → LoD, BL, CL`. Using
the table above, those outputs fire at (3,1), (1,3), (0,0), (0,4) and (2,3).
Converted to eighth notes with the straight timing, that's positions
**0, 3, 6, 10, 13**: exactly the bossa nova clave pattern on the rhythm
diagram.

Two notation rules show up when you decode everything this way:

- **A sequencer number gates the pulse before it.** `19,15` means pulse 19
  *only while sequencer stage 15 is open*. That gives Go-Go its snare on the
  last eighth of the second bar but not the first. The manual's diode chart
  confirms it: an open D15 makes the Go-Go snare "always the same".
- **Rolls and accents are levels, not pulses.** The stabilizer output (2) is
  held over the turn of each ring cycle. Gated with sequencer stages, it
  keeps the snare trigger enabled for a short window, and the trigger rolls
  through it (Tango, March/Polka).

## Rhythm diagrams versus the wiring

The manual also draws every rhythm as a clock face (pages 10-11). Position 1
is at the top, the pattern runs clockwise, and each spoke lists the
instruments on that beat. Big circled numbers are beats; small ones are
subdivisions. The diagrams are easier to read, but they don't say how much
time a spoke represents.

Beat Queen uses the diagrams as the main source and the wiring as a cross
check:

1. **Place each diagram on the clock grid.** A diagram covers either a full
   8-beat cycle or half of one (repeated). Each spoke spans 1, 2 or 4 clock
   slots. The right choice is the one where **every hit lands on a real
   clock pulse**. `makeRhythm()` rejects anything that doesn't, and the test
   `rhythm bank` checks every rhythm. This caught March/Polka: its diagram
   looks like eighths over a full cycle, but the wiring proves it's
   sixteenths over a half cycle.
2. **Decode the wiring** and compare. They agree almost everywhere. Where
   the wiring clearly adds a hit the diagram omits, follow the wiring,
   because it's what the machine actually plays. Six places:

   | Rhythm | Added hit |
   |---|---|
   | Bossa Nova | clave |
   | Samba | bongo |
   | Cha Cha | cymbal |
   | Rumba | maracas |
   | Shuffle | snare |
   | Slow Fox | cymbal |

3. **Where a scan is ambiguous, let the diagram decide.** A blurry "6" and
   "8" look alike. Bolero's hi drum keyed from "8" would contradict the
   diagram; from "6" it matches.

Each rhythm in `core/src/mrk1/Rhythms.cpp` is a grid you can read like a drum
chart:

```cpp
makeRhythm("Bossa Nova", latin, S, kChokedCymbal, 2, {   // straight; 2 sixteenths per position
    row(K::BassDrum, "x...x..xx...x..x"),
    row(K::Cymbal,   "xxxxxxxxxxxxxxxx"),
    row(K::HiHat,    "..x...x...x...x."),
    row(K::Blocks,   "x..x..x...x..x.."),
    ...
```

## Playing it

At run time, `Clock` (in `DrumMachine.h`) converts a tempo into the beat
position of each of the 20 pulses, using straight or triplet timing. It
calls back on each pulse with a **sample offset** inside the current audio
block. `DrumMachine::firePulse` then acts as the matrix:

1. It ORs together the events of every selected rhythm (that's how
   combinations work on the real panel).
2. It keys the instruments.
3. It holds the snare trigger for a roll, or the cymbal "D" line for an
   accent.
4. It fires Q17's dampen pulse on every stage 4, and lights the down-beat
   lamp on pulses 0 and 10.

## What's still uncertain

The speed knob's real range depends on the regulated Tempo B+ voltage. The
schematic suggests roughly 128-199 BPM, taking a beat as four sixteenths
(half a ring cycle), but the regulator's exact operating point
isn't documented. Beat Queen offers 40-240 BPM. Someone with a real unit
could pin this down in one evening with a stopwatch.
