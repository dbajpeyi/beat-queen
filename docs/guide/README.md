# Modeling a Vintage Drum Machine from Its Service Manual

This guide explains, from first principles, how Beat Queen turns a 1968
service manual into a playable software instrument. It is written for someone
who wants to do the same for another machine. It assumes you can program,
but not that you already know circuit simulation or analog electronics.

Beat Queen recreates the **Maestro Rhythm King MRK-1**. It has 18 preset
rhythms, eight hand-played voices and an all-transistor circuit. Nothing in
Beat Queen is sampled. Every sound comes from simulating the MRK-1's circuits
component by component, in real time.

## How to read this guide

The pages build on each other. Read them in order the first time.

| # | Page | What you will learn |
|---|---|---|
| 1 | [The machine and the plan](01-the-machine.md) | What the MRK-1 is, its block diagram, and three ways to model an analog instrument |
| 2 | [From scanned schematic to netlist](02-from-scan-to-netlist.md) | Reading a 1960s scan, resolving unreadable values, writing a netlist |
| 3 | [Simulating linear circuits](03-linear-circuits.md) | Nodal analysis, companion models, the trapezoidal rule, frequency warping |
| 4 | [Diodes, transistors and Newton's method](04-nonlinear-devices.md) | Device equations, Newton-Raphson, DC operating points, the K-method |
| 5 | [Making it real-time](05-real-time.md) | Precomputation, switches, sleeping voices, splitting circuits, profiling |
| 6 | [The drum voices](06-drum-voices.md) | Twin-T ringing oscillators, pulse amps, trimmers, the snare roll |
| 7 | [The noise voices](07-noise-voices.md) | Noise sources, transistor VCAs, gates, LC tanks, envelope modes |
| 8 | [Clock, matrix and rhythms](08-clock-and-rhythms.md) | Ring counters, triplet timing, decoding the rhythm wiring |
| 9 | [Validation and calibration](09-validation.md) | Checking against SPICE, measuring sounds, choosing unknown values |
| 10 | [The plugin](10-the-plugin.md) | JUCE structure, threads, sample-accurate events, host sync, the UI |
| 11 | [Glossary and further reading](11-glossary.md) | Terms, formulas and references in one place |

## Where things are in the code

```
core/include/beatqueen/circuit/Circuit.h   the circuit simulator   (pages 3-5)
core/src/mrk1/Instrumentation.cpp          the voice netlists      (pages 6-7)
core/src/mrk1/Rhythms.cpp                  the rhythm bank         (page 8)
core/include/beatqueen/DrumMachine.h       clock and matrix        (page 8)
tests/core_tests.cpp                       validation              (page 9)
tools/spice/generators.py                  ngspice reference runs  (page 9)
plugin/                                    JUCE plugin and UI      (page 10)
```

## The approach in one paragraph

Read the schematic and write every resistor, capacitor, inductor, diode and
transistor into a **netlist**. Turn the netlist into equations with **nodal
analysis**. Step those equations through time with the **trapezoidal rule**,
which makes each capacitor and inductor look like a resistor plus a current
source. Solve the few nonlinear junctions each sample with **Newton's
method**. Do as much of the work as possible ahead of time, so each sample
costs only a matrix-vector product and a tiny Newton solve. Check the
results against an independent simulator (**SPICE**). Pick the values the
manual doesn't give so the machine behaves the way the manual describes.
