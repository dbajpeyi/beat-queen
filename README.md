# Beat Queen

A software Maestro Rhythm King MRK-1 (1968), as a VST3/AU plugin and a
standalone app.

Beat Queen doesn't use samples. Every voice is the MRK-1's own circuit,
simulated at component level from the
[service manual](https://archive.org/details/sm_Maestro_MRK-1_Service_Manual):
- the twin-T ringing oscillators
- the gated-noise hi-hat, cymbal and snare
- the snare roll multivibrator
- the output preamp

The 18 rhythms come from the manual's rhythm diagrams, cross-checked against
the rhythm selector wiring and the 5-stage ring clock.

## Using it

- **Rhythm buttons** (red Latin, gold American, blue Traditional) latch. As
  on the original, you can combine several. Picking any triplet rhythm
  switches the whole clock to triplet timing, so straight rhythms played
  alongside it are re-timed, just as on the hardware.
- **START** is the reset foot switch: it starts the rhythm from the down beat.
- **SPEED** sets the tempo in BPM. The **DOWN BEAT** lamp flashes every 4
  beats, and the clock lights show the 20 pulses of the 8-beat cycle.
- **Voice buttons** play the eight instruments by hand, with or without a
  rhythm running. Holding the snare drum button rolls it, as on the
  original.
- **Host sync:** in a DAW, while the transport plays, the rhythm follows the
  host's tempo and bar position. When it is stopped, or in the standalone
  app, START runs the internal clock.
- **Service adjustments** opens the twelve internal trimmers:
  - VR1-VR8 set each drum's length, from "dead" to "rings continuously".
  - VR9 sets the snare roll speed.
  - VR10-VR12 set the noise voice volumes.
- **MIDI:** notes play the instruments, using the General MIDI drum map.

| Instrument | MIDI notes |
|---|---|
| Bass Drum | 35, 36 |
| Rim Shot | 37 |
| Snare Drum | 38, 40 |
| Hi-Hat | 42, 44, 46 |
| Cymbal | 49, 51, 52, 55, 57, 59 |
| Tom Tom | 41, 43 |
| Lo Drum | 45, 47 |
| Hi Drum | 48, 50 |
| Bongo | 60, 61 |
| Clave | 75 |
| Blocks | 76, 77 |

The MRK-1 has no accent dynamics, so velocity is ignored.

## Building

Requires CMake ≥ 3.22 and a C++17 compiler. JUCE 8 is downloaded
automatically at configure time.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Outputs are written to `build/BeatQueen_artefacts/Release/`:

- `Standalone/Beat Queen` (`.app` on macOS, `.exe` on Windows)
- `VST3/Beat Queen.vst3`
- `AU/Beat Queen.component` (macOS only)

On Linux, install JUCE's dependencies first (Debian/Ubuntu):

```sh
sudo apt install libasound2-dev libxrandr-dev libxinerama-dev libxcursor-dev \
  libxcomposite-dev libfreetype-dev libfontconfig1-dev libgl1-mesa-dev
```

### Core only (no JUCE)

The circuit simulator, voices, rhythms and clock live in `core/`, which does
not depend on JUCE:

```sh
cmake -S . -B build-core -DBEATQUEEN_BUILD_PLUGIN=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-core
ctest --test-dir build-core --output-on-failure

# Render every instrument and all 18 rhythms to WAV
./build-core/beatqueen_render /path/to/output-dir [sample-rate] [tempo-bpm]

# Reference simulations of the voice circuits (needs ngspice and numpy)
python3 tools/spice/generators.py /path/to/output-dir
```

## Layout

```
core/circuit/   real-time circuit simulator (nodal analysis + Newton on the junctions)
core/mrk1/      the MRK-1: instrumentation board netlists, rhythm bank, clock pulses
core/           DrumMachine: rhythm selection, clock, matrix
plugin/         JUCE processor and front panel (VST3 / AU / Standalone)
tests/          core tests, including comparisons with ngspice
tools/          offline WAV renderer, ngspice reference script
docs/DESIGN.md  how the circuits were modeled, sources, assumptions, validation
```

Beat Queen is an independent recreation. Maestro and Rhythm King are
trademarks of their respective owners. The service manual is not included in
this repository.
