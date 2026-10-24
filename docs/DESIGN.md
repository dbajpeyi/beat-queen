# Beat Queen design

## Goal

Recreate the sound and behavior of the Maestro Rhythm King MRK-1 by modeling
its circuits from the
[service manual](https://archive.org/details/sm_Maestro_MRK-1_Service_Manual),
not by playing back samples.

## Architecture

```
                 ┌──────────────── plugin/ (JUCE) ────────────────┐
 host transport ─┤ BeatQueenProcessor                              │
 MIDI notes ─────┤   parameters → DrumMachine, MIDI → triggers     ├─→ audio out
 UI taps ────────┤ BeatQueenEditor                                 │
                 └───────────────────────┬────────────────────────┘
                                         │
                 ┌──────────────── core/ (plain C++17) ───────────┐
                 │ DrumMachine                                     │
                 │   Sequencer ──step masks──▶ Voice × 8 ──▶ mixer │
                 │   (Pattern bank = diode matrix)                 │
                 └─────────────────────────────────────────────────┘
```

- **`core/` has no JUCE dependency**, so the circuit models can be unit
  tested, rendered offline (`tools/render_wav.cpp`) and compared against
  SPICE or recordings without a plugin host.
- **`Voice`** (`core/include/beatqueen/Voices.h`) is the interface for one
  instrument circuit. It is triggered with a level and asked for one sample
  at a time. Each MRK-1 circuit becomes one `Voice` subclass.
- **`Pattern`** is one rhythm. Each step is an 8-bit mask of which
  instruments fire, which mirrors a diode matrix: a counter state drives a
  row of diodes into the instrument trigger lines. `makePattern()` takes
  readable `x...x...` grids so the matrix can be transcribed by hand.
- **`Sequencer`** places steps with sample accuracy. It runs either from its
  own clock or locked to the host's beat position.

## Current state

| Area                           | State                                       |
|--------------------------------|---------------------------------------------|
| Sequencer, free-running clock  | Done, tested                                |
| Host sync (PPQ lock)           | Done, tested                                |
| MIDI input (GM drum map)       | Done                                        |
| Mixer, levels, state save      | Done                                        |
| UI                             | Done (functional layout)                    |
| VST3 / Standalone / AU targets | Build (AU on macOS only)                    |
| **Voice circuits**             | **Placeholder**: generic resonator/noise    |
| **18 preset rhythms**          | **Missing**: one non-MRK-1 test beat        |
| **Tempo range**                | **Guess** (40–240 BPM)                      |

Everything that needs the manual is marked `TODO(manual)` in the source.

## What to take from the manual

1. **Block diagram and power rails.** These give the overall signal flow and
   the supply voltages, which set the signal levels and clipping points.
2. **Clock oscillator.** Its tempo range and the taper of the tempo pot set
   the range of the `tempo` parameter. If the oscillator drifts or is
   uneven, note that too.
3. **Counter / divider chain.** This sets the steps per bar and the steps
   per beat for each rhythm (straight or triplet), and how the 18 rhythms
   switch between them.
4. **Rhythm matrix.** Transcribe all 18 rhythms (Latin / American /
   Traditional) into `presetPatterns()` in `core/src/Pattern.cpp`. Each
   rhythm becomes one `makePattern(...)` grid. The names and categories
   should match the front panel.
5. **Trigger pulse shaping.** Note the width and amplitude of the pulse that
   reaches each voice. This sets how hard each resonant circuit is excited,
   and it is part of each voice's character.
6. **Each instrument circuit.** For each of the eight voices, record the
   topology and component values. This could be a bridged-T or twin-T
   resonator, a transistor ringing stage, a noise source with a filter and
   gate, and so on. Note any instruments that share circuitry (for example,
   one noise source feeding both the hi-hat and the cymbal).
7. **Noise source.** Usually a reverse-biased transistor junction plus a
   gain stage. Its spectrum and level matter for the snare, hi-hat and
   cymbal.
8. **Mixer and output stage.** The fixed mix ratios between voices and any
   tone shaping or saturation at the output.

## How each circuit will be modeled

1. **Transcribe** the circuit into a SPICE netlist (`docs/circuits/*.cir`,
   for ngspice). This is the reference behavior.
2. **Derive a model.** For mostly linear sections, such as passive or
   op-amp-like resonators and filters, work out the transfer function from
   the component values. Discretize it with a bilinear transform,
   pre-warped at the resonant frequency. For sections where nonlinearity is
   audible, such as transistor ringers and envelope/gate stages, use a
   wave-digital-filter or nodal model with the transistor's nonlinearity.
3. **Keep component values as named constants** in each `Voice` subclass,
   so the model can be read against the schematic and checked part by part.
4. **Validate.** Render the model's response to a trigger pulse with
   `beatqueen_render` and compare its frequency, decay time and spectrum
   with the SPICE simulation. A core test then checks those numbers.
   Recordings of a real unit serve as a final check by ear.
