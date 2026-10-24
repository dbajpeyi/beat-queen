# 10. The plugin: JUCE, threads, timing and the front panel

## Project structure

```
core/    plain C++17: simulator, instrument, rhythms, clock (no dependencies)
plugin/  JUCE AudioProcessor + editor, built as VST3 / AU / Standalone
tests/   core tests (ctest)
tools/   offline renderer, ngspice script
```

CMake fetches JUCE 8 with `FetchContent`. `juce_add_plugin(... FORMATS VST3
Standalone [AU])` produces the plugin formats and a standalone app from the
same code. The core builds and tests without JUCE
(`-DBEATQUEEN_BUILD_PLUGIN=OFF`). That kept the development loop fast: the
circuit work never needed a DAW.

## Parameters

Everything a user or host can automate is a parameter in an
`AudioProcessorValueTreeState`:

| Parameters | Kind |
|---|---|
| 18 rhythm switches | booleans, so they can be combined |
| speed | float |
| start | boolean (latching) |
| host sync | boolean |
| volume | float |
| VR1-VR12 | floats |

The audio thread reads them through the atomic pointers returned by
`getRawParameterValue`. State save and restore come free with the value
tree.

The **voice buttons are not parameters**. They're momentary performance
gestures, so they go straight to the engine through an atomic bit mask
(`DrumMachine::requestButton`).

## Threads

| Thread | Does | Talks to the other via |
|---|---|---|
| Audio (`processBlock`) | Clock, matrix, all circuit simulation | Reads parameter atomics and the button mask; writes the current pulse and lamp atomics |
| Message (UI) | Drawing, mouse handling | Writes parameters and the button mask; polls pulse and lamp at 30 Hz |

Nothing in the audio thread allocates, locks or waits. The one expensive
reaction, a trimmer change that re-solves a circuit's operating point, takes
microseconds and is rate-limited to one per block.

## Sample-accurate events

A drum machine must place hits on exact samples, not on block boundaries
(a 512-sample block is 10 ms, an audible flam). Two mechanisms do this:

- **Clock pulses.** `Clock::advance(n, onPulse)` reports each pulse with its
  sample offset in the block. `DrumMachine::render` steps the circuits
  sample by sample and fires each pulse exactly at its offset.
- **MIDI notes.** `processBlock` renders in segments split at each note-on's
  sample position, then keys the instrument before the next segment.

## Following the host

When host sync is on and the DAW is playing, the plugin reads the host's
**PPQ position** (quarter notes since the song start) and tempo. It fires
pulse $k$ when the block's PPQ range crosses its beat time:

$$t_k = 8\left\lfloor k/20 \right\rfloor + \text{pulseBeat}(k \bmod 20)$$

It remembers the last pulse fired to avoid double triggers, and resets when
the transport restarts or jumps backwards. When the host stops, or in the
standalone app (which has no host), the internal clock runs from the
start/stop rocker.

## MIDI mapping

General MIDI drum notes key the instruments directly (bass drum 35/36,
snare 38/40, hi-hat 42/44/46, and so on; see the README). Velocity is
ignored: the MRK-1 has no dynamics, and pretending otherwise would no longer
be an emulation.

## Drawing the front panel

The editor recreates the cabinet from a photograph using only code. There
are no image files, which keeps the build simple.

| Element | Technique |
|---|---|
| Tolex covering | Procedural texture: bilinear-interpolated value noise for the pebble grain, plus fine per-pixel grain. Generated once into a `juce::Image`. |
| Brushed aluminium | Per-row brightness plus a horizontally smoothed random walk (the "brush lines"), and a broad sinusoidal sheen |
| Chrome trim, knobs, screws | Linear and radial gradients from near-white to grey. Knurling is 48 short radial lines. |
| Plexi | Near-black fill with a faint diagonal reflection gradient |
| Keys | Bevelled bodies: lighter top edge, darker sides, cap shifted down when latched, plus a soft glow for latched rhythm keys |
| Emblem | An original crown-like badge drawn with paths in the style of the period, *not* the manufacturer's trademarked logo |

The whole panel is laid out at one fixed base size (1120 × 540, or × 740
with the service drawer open). The editor scales it with an
`AffineTransform` and a fixed-aspect-ratio constrainer, so the window
resizes cleanly without any layout code changing.

## Continuous integration

`.github/workflows/build.yml` builds and runs the tests on Linux, macOS and
Windows for every push, and uploads the VST3, AU and standalone builds as
artifacts. It's the cheapest way to catch platform-specific compile errors,
and floating-point differences in the circuit tests.
