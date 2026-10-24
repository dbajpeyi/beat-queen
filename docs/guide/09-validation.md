# 9. Validation and calibration

A circuit simulator that "sounds plausible" can still be badly wrong. Beat
Queen is checked three ways:

- against **mathematics** (circuits with known answers);
- against **another simulator** (ngspice);
- against **the manual's own descriptions**.

The few values the manual doesn't give are then **calibrated**, openly.

## 1. Known answers

The test file `tests/core_tests.cpp` begins with circuits you can solve on
paper:

| Test | Circuit | Expected |
|---|---|---|
| RC step | 10K and 1 µF, 10 V step | $10(1-e^{-1})$ V after 10 ms |
| LC resonance | 27 mH and 0.068 µF, pre-warped | 3714 Hz |
| Diode clamp | 10 V through 1K into a diode | 0.55-0.8 V |

If these fail, nothing downstream can be trusted.

## 2. An independent simulator

**ngspice** is a free SPICE: decades of validated device models and
numerics, with an algorithm completely independent of Beat Queen's.
`tools/spice/generators.py` writes the same netlist for each ringing voice,
runs a transient analysis, and measures it the same way.

Use the **same transistor model** in both. The comparison then tests only
the simulator, not your guess about which transistor Gibson used.

| Voice | Beat Queen | ngspice |
|---|---|---|
| Bass drum operating point | Vc 6.508 V, Vb 0.730 V | Vc 6.511 V, Vb 0.730 V |
| Bass drum ring / decay | 52.5 Hz / 0.11 s | 53.2 Hz / 0.10 s |
| Tom tom | 159.8 Hz / 0.22 s | 159.8 Hz / 0.23 s |
| Clave | 2231 Hz / 0.01 s | 2217 Hz / 0.01 s |

The test `generators match ngspice pitch and decay` checks all eight voices,
to ±2% in pitch.

What remaining differences there are have known causes:

- **The keying pulse.** Beat Queen's is longer, matching what the clock
  board drives; the SPICE script uses a short 1 ms pulse.
- **The measurement window.** Zero-crossing pitch measurement on a strongly
  decaying, initially clipped signal must start after the strike transient.
  The test skips the first quarter of the decay.

## 3. The manual as a specification

The manual describes behavior in words. Each description becomes a test:

| Manual says | Test |
|---|---|
| Tom trimmer: "too little ... dead, too much ... continuously" | Trimmer at 0: decays in under 0.15 s. Trimmer at 1: still ringing after 1.5 s. |
| Snare speed: "approximately 15 to 20 beats per second" | Count strokes in a 1 s roll: 14-22 |
| Choked hi-hat: "quickly drops to about one third intensity" | Level 40-60 ms after the hit is under 0.6× normal (it measures 0.37 of peak) |
| Hi-hat med/long, cymbal maracas/long | Decay times order correctly |
| 8 Latin, 7 American, 3 Traditional; 8 triplet rhythms | Counted from the rhythm bank |

These tests caught real mistakes during development. One example: an early
test assumed "choked" meant "shorter overall". It doesn't. The extra 0.1 µF
on the A line lengthens the tail. The manual's exact wording ("drops to
about one third") was the right specification.

## Measuring a sound

The tests use three simple, robust measurements:

- **Peak:** the largest absolute sample.
- **Pitch:** count zero crossings over a window starting after the strike
  transient; frequency = (crossings − 1) / 2 / duration.
- **Decay:** find the loudest 10 ms window, then the time until the 10 ms
  peak envelope falls 40 dB below it.

Plot as well as test. `tools/render_wav.cpp` writes every instrument and
every rhythm to WAV. Drawing waveforms and spectra side by side, and every
rhythm's first cycle on a beat grid, catches things no single number does:
a misplaced hit, a click, a resonance in the wrong place.

## Calibrating what the manual doesn't say

Some values simply aren't documented. The approach for each:

1. **Choose a physically reasonable starting value**, and mark it as an
   assumption.
2. **Find a statement in the manual that depends on it.**
3. **Adjust the value until the statement holds.** Then stop: don't tune
   anything else to taste.

**Noise source level.** The manual says to set VR10 so the cymbal sounds
"balanced" with the other instruments, and VR11 so the "Hi-Hat should be
quite a bit louder than Cymbal". Levels were measured at three noise
amplitudes, with every trimmer setting:

| Noise level | What happened |
|---|---|
| 20 mV rms | The cymbal gate saturates; even its minimum trimmer is louder than the drums |
| 5 mV rms | The hi-hat needs its trimmer at maximum just to match the cymbal |
| **10 mV rms** | Both statements can be met with sensible trimmer positions: cymbal 12.5%, hi-hat 90%, snare noise 70% |

At 10 mV the choked hi-hat also lands on the manual's "about one third" by
itself, which is independent evidence the level is right.

**Default trimmer positions** are a technician's job on the real machine
("adjust by ear"). Beat Queen starts the length trimmers at mid travel,
except where that oscillates with this transistor model (blocks, clave), and
calibrates the volume trimmers as above.

## A debugging story

During calibration, one setting produced a peak of **130 V** at the output.
That's impossible: the preamp can't exceed its supply. Printing samples
around the spike showed a single bad sample in otherwise normal audio. The
Newton solver had hit its iteration limit during a hard transient and
returned an unconverged answer. Raising the limit (it's only reached on
hard samples) and adding a hold-previous fallback fixed it.

Physical sanity bounds ("no node can exceed the supply rails") make
excellent automated checks for exactly this kind of bug.
