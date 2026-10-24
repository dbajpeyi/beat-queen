# Beat Queen design

Beat Queen recreates the Maestro Rhythm King MRK-1 by simulating its circuits
from the
[service manual](https://archive.org/details/sm_Maestro_MRK-1_Service_Manual),
not by playing samples. Every drum voice is a component-level netlist using
the manual's part values, run by a small real-time circuit simulator. The
rhythms come from the manual's rhythm diagrams, cross-checked against the
selector-switch wiring and the clock logic.

## Sources in the manual

| Manual page | Used for |
|---|---|
| p. 2 Specifications | Rhythm names and groups, voice buttons, controls |
| pp. 3-4 Description | Clock operation step by step: when each logic gate fires |
| p. 4 Rhythm selector switches | Control functions (Y, Z, A, med, long, (2), (17)) |
| pp. 5-6 Instrumentation, adjustments | Voice behaviour and VR1-VR12 trimmer effects |
| p. 7 Clock voltage chart | Which clock transistors idle on or off |
| p. 9 Clock diagram | Pulse sequence of the 5-stage ring and logic sequencer |
| pp. 10-11 Rhythm pattern diagrams | The instruments on every beat of all 18 rhythms |
| Dwg 1 Clock (both versions) | Ring timing resistors, triplet control, tempo regulator, Q17 |
| Dwg 3 Rhythm selector switches | Which clock pulses key which instrument |
| Dwg 4 Instrumentation and power supply | All voice circuits, output preamp, supply rails |
| pp. 26-28 Parts list | Capacitor values the scan shows without decimal points |

## Architecture

```
plugin/ (JUCE)          BeatQueenProcessor: parameters, MIDI, host transport
                        BeatQueenEditor: front panel + service panel
        |
core/   DrumMachine     rhythm switch selection -> control lines, triplet timing
        |   Clock       20 pulses per 8-beat cycle, free-running or host-synced
        |   Rhythms     the 18 rhythms as keying events on the 20 pulses (the matrix)
        |   mrk1::Instrumentation
        |       8 twin-T generators, snare trigger, 3 noise voices, output preamp
        |   circuit::Circuit
        |       nodal analysis, trapezoidal rule, Newton on the junctions only
```

`core/` has no JUCE dependency. It is unit tested, and `tools/render_wav.cpp`
renders it offline.

## The circuit simulator (`core/src/circuit/Circuit.cpp`)

A netlist contains resistors, capacitors, inductors, ideal switches, current
sources, diodes and NPN transistors. `prepare()` does four things:

1. **Operating point.** Newton-Raphson with source stepping finds the DC
   bias, the same way SPICE's `.op` does.
2. **Discretization.** Capacitors and inductors become trapezoidal companion
   models. The trapezoidal rule compresses high frequencies (a 9.7 kHz tank
   would ring 11% flat at 48 kHz), so each circuit is pre-warped at its main
   resonance.
3. **One matrix per switch state.** The linear part is inverted once for each
   combination of switch states. Each inverse is built the first time that
   combination is used; storage for all of them is allocated in `prepare()`.
4. **K-method.** The nonlinear devices reduce to a few junction voltages
   p = p0 + K·i(p). Each sample is then one matrix-vector product plus a
   Newton solve over 2-6 junctions, not over the whole circuit.

Transistors use the Ebers-Moll transport model with the Early effect.
Junction limiting follows SPICE's `pnjlim`.

Circuits that are not sounding **sleep**. Once a voice's output and envelope
are silent, it stops simulating and snaps back to its operating point. The
noise voices are split into an envelope stage and an audio stage. The two
stages connect only through 560K-10M resistors, which are bridged with a
one-sample lag; every node involved has a time constant of milliseconds.

## The voices (Dwg 4)

### Twin-T generators: bass drum, tom tom, bongo, blocks, clave, rim shot, hi drum, lo drum

Each generator is a common-emitter transistor with a twin-T network from its
collector back to its base:

- **Twin-T network:** two 56K resistors with a shunt capacitor, two equal
  series capacitors whose centre goes to the pulse amp.
- **Length trimmer:** VR1-VR8 sits in the emitter and sets the loop gain. It
  takes the voice from dead to ringing to continuous oscillation, exactly as
  the manual's adjustment notes describe.
- **Pulse amp:** a keying pulse saturates the pulse amp, which dumps its
  collector capacitor and kicks the twin-T centre.

Simulated with the same transistor model, the full instrument matches
ngspice runs of the same netlists:

| Voice | Pitch (Beat Queen) | Pitch (ngspice) | -40 dB decay (Beat Queen / ngspice) |
|---|---|---|---|
| Bass drum | 52.5 Hz | 53.2 Hz | 0.11 / 0.10 s |
| Tom tom | 159.8 Hz | 159.8 Hz | 0.22 / 0.23 s |
| Bongo | 237.7 Hz | 237.9 Hz | 0.09 / 0.09 s |
| Blocks | 1022 Hz | 1027 Hz | 0.03 / 0.03 s |
| Clave | 2231 Hz | 2217 Hz | 0.01 / 0.01 s |
| Rim shot | 2005 Hz | 2004 Hz | 0.03 / 0.02 s |
| Hi drum | 485 Hz | 482 Hz | 0.06 / 0.06 s |
| Lo drum | 320 Hz | 318 Hz | 0.14 / 0.15 s |

`tools/spice/generators.py` regenerates the ngspice column.

### Snare drum

The snare is three circuits keyed at once, as the manual describes:

- **Hi drum and lo drum generators**, pulled through D19 by the snare drum
  trigger.
- **Gated noise:** noise amp Q56 is biased by a 0.068 uF envelope capacitor,
  and gate Q57 has an L3 tank at 3.4 kHz with Q of about 0.7.

The trigger is the Q52/Q53 astable, enabled by Q54:

- Phase A lasts 0.693 x (120K + VR9) x 0.1 uF.
- Phase B lasts 0.693 x 100K x 0.56 uF.

A short keying pulse gives one stroke. A held one rolls at 15.6-21 Hz,
matching the manual's "approximately 15 to 20 beats per second". Holding the
snare button also rolls.

### Hi-hat and cymbal

Each is a gated-noise voice:

- **Trigger:** a one-shot charges an envelope line through a diode.
- **Noise amp:** a transistor biased only by that line through 10M. It is
  fed by the shared Q55 noise generator and acts as a VCA.
- **Gate:** the envelope and the amplified noise meet at a diode into a gate
  transistor, which conducts only while the envelope is high. The gate's
  collector tank is 3.7 kHz (Q about 5) for the hi-hat and 9.7 kHz for the
  cymbal.

The rhythm switches change the envelope **physically**, by connecting parts
to the line:

| Control | Circuit | Effect | Rhythms |
|---|---|---|---|
| Y | +6 V through 47K and D17, as a clamp | Cymbal "maracas" (short) | Bolero, Rumba |
| Z | 0.1 uF to ground | Cymbal "long" | Waltz |
| Y+Z | both | Cymbal "choked": quick drop, then a slow tail | Bossa Nova, Slow Rock, Western |
| (2) on D | Clock pulse into the cymbal gate and noise amp | Cymbal accent | Samba, Mambo |
| med | 0.068 uF to ground | Hi-hat "med." | Dixie, Shuffle, Swing |
| long | 0.22 uF to ground | Hi-hat "long" | Slow Fox |
| +6 V on A | 47K/D25 clamp plus 0.1 uF | Hi-hat "choked": drops to about 1/3, then decays | Discotheque |
| med + A | both | Hi-hat "med. choked" | Go-Go |
| (17) on C | Q17 drains the line through 390K after each ring cycle | Hi-hat "dampened" | Swing |

### Output

All voices sum on a bus into the Q62 common-emitter preamp, which clips
naturally at about ±8.7 V, and then the VR13 volume control. 10 V at the
output plug is digital full scale.

## The clock and the rhythms (Dwg 1, Dwg 3)

### The clock

The clock is a 5-stage ring multivibrator Q1-Q5. Each stage's timing
resistor sets its length:

- **Straight timing:** 470K, 240K, 240K, 470K and 470K give stage lengths of
  2:1:1:2:2, so pulses fall at sixteenths **0, 2, 3, 4 and 6** of each
  8-sixteenth ring cycle.
- **Triplet timing:** the triplet contacts add 470K in parallel to stages 4
  and 5. Pulses then fall at triplet-eighths **0, 2, 3, 4 and 5** of 6. The
  tempo regulator (Q64/Q65) lowers Tempo B+ so the beat length stays the
  same.
- **The cycle:** the ring goes round four times per cycle, giving 20 pulses
  over 8 beats.
- **Logic:** the logic sequencer Q11-Q15 and gates Q18-Q29 select particular
  ring cycles. The manual's step-by-step clock description gives, for
  example, Q28 firing at pulses (0,0) and (2,0), which also drive the down
  beat lamp.

### How the rhythms were transcribed

The rhythm diagrams are the main source. Each was placed on the pulse grid,
and the selector wiring was decoded against the clock to check it. Two rules
make the wiring decode:

- A sequencer number listed with a pulse gates that pulse. "19,15" means
  pulse 19 only while stage 15 is active, which gives Go-Go's extra snare on
  the "and" of beat 4.
- The snare rolls are gated by (2).

The decoding agrees with the diagrams almost everywhere. For example, Bossa
Nova's "23,29,18" lands exactly on the bossa clave positions 0, 3, 6, 10 and
13. It also settled each rhythm's grid; March/Polka, for instance, is
sixteenths repeating every half cycle. Where the wiring clearly adds a hit
the diagram leaves out, Beat Queen follows the wiring:

| Rhythm | Change from the diagram | Wiring (Dwg 3) |
|---|---|---|
| Bossa Nova | Clave plays with blocks/lo drum | LoD, BL, CL all keyed by 23,29,18 |
| Samba | Bongo on the "and" of beat 2 (2nd bar); blocks not there | Bon 28,20; BL 23,29,18 |
| Cha Cha | Cymbal also on beat 4 | CY 6,7,9,10 |
| Rumba | Maracas also on beat 3 of bar 2 | CY 6,7,9,10 + 24 |
| Shuffle | Snare also on the last triplet of beat 2 | SD 8,20 |
| Slow Fox | Cymbal with the snare on beat 2 | CY 27 |

Where a scan label was ambiguous ("6" against "8"), the diagram decided.
Bolero's hi drum is one example; Mambo's blocks and clave follow the diagram.

## Assumptions (not in the manual)

| What | Value | Why |
|---|---|---|
| Transistors | 2N3904-class Gummel-Poon parameters | The MRK-1 used house-numbered parts |
| Diodes | 1N4148-class | As above |
| Clock pulse at a keying point | 15 V through 2.2K, 5 ms | Pulse amp Q6-Q10 has a 2.2K collector load; the width only sets how long the voice's pulse amp stays saturated |
| Summing bus seen by each voice | 10K | The 47K bus resistor, the preamp input and the other voices in parallel |
| Noise generator Q55 | 7 V breakdown, 4.7K, 10 mV rms white noise | Calibrated so VR10/VR11 can reach the manual's balance |
| Default VR10/11/12 | 12.5% / 90% / 70% | Manual: cymbal "balanced", hi-hat "quite a bit louder" than the cymbal, snare noise blending with the drums |
| Default VR1-VR8 | Mid travel, except bass drum (full), blocks (25%) and clave (30%) | Blocks and clave oscillate continuously at mid travel with this transistor model |
| Q17 on-time | 0.1 s | 0.1 uF base capacitor discharging through 220K |
| Accent pulse (2) | 15 V through 10K for one pulse interval | Q16 stabilizer output |
| Tempo range | 40-240 BPM | Dwg 1 suggests roughly 128-199 BPM, but that depends on the tempo regulator's B+, which can't be pinned down |
| Lo drum SOFT | Keyed at the emitter through 47K | The version after serial 09001 |
| Snare trigger branch | Each of hi drum and lo drum gets its own copy of the 33K/0.12 uF/D19 branch | The real ones share the 0.12 uF node; the coupling between them is small |

## Validation

`tests/core_tests.cpp` checks:

- **Engine:** RC, LC and diode circuits against analytic results.
- **Operating point:** the bass drum's DC operating point against ngspice.
- **Generators:** all eight against ngspice for pitch (±2%) and decay.
- **Trimmers:** the length trimmer takes the tom tom from dead to continuous,
  as the manual says.
- **Envelope modes:** hi-hat and cymbal modes order as described; the choked
  hi-hat drops to about one third.
- **Snare roll:** rolls at 15-20 strokes per second.
- **Rhythm bank:** every rhythm event lands on a clock pulse, and the group
  and triplet counts match the spec sheet.
- **Clock:** pulse timing straight, triplet and host-synced.
- **Whole machine:** it plays, stops and sleeps.

## Performance

These figures are from a 2.1 GHz cloud Xeon at 48 kHz; a desktop CPU should
be roughly twice as fast. An idle machine costs about 1% of a core.

| Load | Cost (share of one core) |
|---|---|
| One sounding generator | ~2.5% |
| One sounding noise voice | 8-11% |
| Busy rhythm (bass drum, hi-hat, cymbal and a couple of drums ringing) | ~20-25% |
| Average over rendering all 18 rhythms | ~18% |

## Future work

- **Recordings:** compare against a real unit to refine the noise level,
  keying pulse and transistor parameters.
- **Hardware tempo knob mode:** reproduce the real speed range and taper.
- **Performance:** SIMD or lower-rate simulation of the low-pitched
  generators.
