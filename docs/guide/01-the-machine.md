# 1. The machine and the plan

## The Maestro Rhythm King MRK-1

The MRK-1 is a 1968 "automatic rhythm" box sold by Maestro, Gibson's
electronics brand. It was made to accompany organists and guitarists.

| Feature | What it has |
|---|---|
| Preset rhythms | 18 latching buttons, in three colour-coded groups: Latin (red), American (gold) and Traditional (blue). You can press several at once to combine rhythms. |
| Voice buttons | 8 black buttons for bass drum, snare drum, hi-hat, cymbal, tom tom, bongo, blocks and clave. They play the sound by hand, with or without a rhythm. |
| Controls | On/off, volume, speed, a down-beat lamp, and a reset foot switch that starts the rhythm. |
| Circuitry | About 65 discrete transistors. No integrated circuits, no memory, no microprocessor. |

That last row matters. Every rhythm is wired into the machine as connections
between clock outputs and instrument inputs. Every sound comes from a small
transistor circuit. If you have the schematics, you have the machine.

## The block diagram

The manual's block diagram (page 8) shows the signal path:

```
 speed knob ─┐
 foot switch ┤
             ▼
   ┌────────────────────┐     ┌─────────┐    ┌──────────┐    ┌──────────────┐    ┌──────────┐
   │ 5-stage            │ ──▶ │ 5 pulse │ ─▶ │ logic    │ ─▶ │   MATRIX     │ ─▶ │ RHYTHM   │
   │ multivibrator (Q1-5)│    │ amps    │    │ sequencer│    │ (diodes,     │    │ SELECTOR │
   └────────────────────┘     │ (Q6-10) │    │ + gates  │    │  resistors)  │    │ SWITCHES │
                              └─────────┘    └──────────┘    └──────────────┘    └────┬─────┘
                                                                                      │ keying pulses
                                                                                      ▼
   voice push buttons ─────────────────────────────────────────────────▶ ┌────────────────────┐
                                                                          │ INSTRUMENTATION    │
                                                                          │ 8 ringing oscillators,
                                                                          │ 3 noise voices     │
                                                                          └─────────┬──────────┘
                                                                                    ▼
                                                                          output preamp ─▶ amplifier
```

- The **clock** is a ring of five transistors that switch on one after
  another (page 8 of this guide).
- **Pulse amps, logic sequencer and gates** turn the ring's steps into about
  30 distinct timing pulses. Each one fires at specific moments within an
  8-beat cycle.
- The **matrix** combines those pulses with diodes and resistors.
- The **rhythm selector switches** connect chosen pulses to chosen
  instruments. The **keying points**, boxed on the schematic, are the
  instrument inputs.
- The **instrumentation board** makes the sounds. Each instrument turns a
  keying pulse into a burst of sound.

## Three ways to model an analog instrument

| Approach | How it works | Pros | Cons |
|---|---|---|---|
| **Sampling** | Record each sound and play it back | Simple and cheap | Needs a working unit; every hit is identical; no trimmers; no interactions |
| **Behavioral modeling** | Design DSP blocks (oscillators, filters, envelopes) that imitate each sound, tuned by ear or measurement | Cheap at run time, flexible | Only as good as your ear and measurements; you must understand every effect in advance |
| **Circuit simulation** (this project) | Write the real circuit as equations and solve them every sample | Behavior emerges from the parts: trimmers, envelope modes and interactions all come for free; works from schematics alone | Costs more CPU; needs careful numerics; needs part values you may have to infer |

Beat Queen uses circuit simulation because it only has the manual. There's
no unit to record, so the schematic is the most accurate description
available. Circuit simulation also turns the manual's descriptions into
something you can test. When the manual says "too much and the Tom Tom will
sound continuously", a correct simulation must do exactly that when you turn
the trimmer up. Page 9 shows that it does.

## Plan of work

1. **Read the manual end to end.** Read the description pages first, not
   just the schematics: they explain intent ("the noise generator causes the
   internal base-emitter junction to produce continual random noise").
2. **Transcribe the circuits** into netlists (page 2).
3. **Build a simulator** that can run those netlists in real time
   (pages 3-5).
4. **Check each voice** against an independent simulator (page 9).
5. **Decode the rhythms** from the clock and switch wiring (page 8).
6. **Wrap it in a plugin** with a sequencer and a UI (page 10).

Keep the core of the instrument free of any plugin framework. Beat Queen's
`core/` library is plain C++, so the tests and the offline renderer run
without a DAW. That made every step above much faster to iterate on.
