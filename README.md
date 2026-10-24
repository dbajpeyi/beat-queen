# Beat Queen

A software recreation of the **Maestro Rhythm King MRK-1**, the late-1960s analog
rhythm machine, as a VST3/AU plugin and a standalone app.

> **Status: scaffold.** The sequencer, host sync, MIDI input, mixer, UI and
> build are working. The eight drum voices and the rhythm presets are
> **placeholders**, not yet derived from the MRK-1. They will be rebuilt from
> the circuits in the
> [service manual](https://archive.org/details/sm_Maestro_MRK-1_Service_Manual).
> See [docs/DESIGN.md](docs/DESIGN.md).

## Features

- Eight instruments, as on the MRK-1: bass drum, snare, hi-hat, cymbal, tom,
  bongo, blocks and clave.
- Preset rhythm selector, tempo, start/stop and master volume.
- A tap button and a level fader for each instrument.
- **Host sync:** while the DAW is playing, the rhythm locks to the host's
  tempo and bar position. When it is stopped, or in the standalone app, the
  internal clock runs from the START button.
- **MIDI input** using the General MIDI drum map. Steps and notes are
  sample-accurate.

| Voice     | MIDI notes                 |
|-----------|----------------------------|
| Bass Drum | 35, 36                     |
| Snare     | 37, 38, 40                 |
| Hi-Hat    | 42, 44, 46                 |
| Cymbal    | 49, 51, 52, 55, 57, 59     |
| Tom       | 41, 43, 45, 47, 48, 50     |
| Bongo     | 60, 61                     |
| Blocks    | 76, 77                     |
| Clave     | 75                         |

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

The DSP and sequencer live in a JUCE-free library, `core/`. It can be built
and tested on its own:

```sh
cmake -S . -B build-core -DBEATQUEEN_BUILD_PLUGIN=OFF
cmake --build build-core
ctest --test-dir build-core --output-on-failure

# Render every voice and preset to WAV files for listening and comparison
./build-core/beatqueen_render /path/to/output-dir
```

## Layout

```
core/      JUCE-free engine: voices, DSP primitives, patterns, sequencer, mixer
plugin/    JUCE AudioProcessor + editor (VST3 / AU / Standalone)
tests/     Core unit tests (ctest)
tools/     Offline WAV renderer
docs/      Design notes and the plan for modeling the MRK-1 circuits
```
