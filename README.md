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

The front panel follows the original cabinet. It has a black plexi rhythm
panel in chrome trim, a brushed aluminium faceplate, chrome knurled knobs and
a jewel down beat lamp. The window can be resized.

- **Rhythm keys** (red Latin, yellow American, teal Traditional) latch. As
  on the original, you can combine several. Picking any triplet rhythm
  switches the whole clock to triplet timing, so straight rhythms played
  alongside it are re-timed, just as on the hardware.
- **START / STOP** (the rocker) stands in for the reset foot switch: it
  starts the rhythm from the down beat.
- **SPEED** sets the tempo in BPM; drag it to see the exact value. The
  **DOWN BEAT** lamp flashes every 4 beats.
- **Voice buttons** play the eight instruments by hand, with or without a
  rhythm running. Holding the snare drum button rolls it, as on the
  original.
- **Host sync** (the small toggle under the lamp): in a DAW, while the
  transport plays, the rhythm follows the host's tempo and bar position.
  When it is stopped, or in the standalone app, the rocker runs the internal
  clock.
- **SERVICE** opens the case. The drawer shows the instrumentation board,
  with lights for the clock's 20 pulses and the twelve trimmers:
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

## Downloading a build

Every push builds Beat Queen on GitHub Actions. Open a run under the
repository's **Actions** tab and download the archive for your system from
**Artifacts**:

| Archive | Contents |
|---|---|
| `beat-queen-macOS` | Standalone app, VST3 and AU (universal: Apple Silicon and Intel) |
| `beat-queen-Windows` | Standalone app and VST3 |
| `beat-queen-Linux` | Standalone app and VST3 |

### macOS: "Beat Queen is damaged and can't be opened"

The builds are signed ad hoc, not with an Apple Developer ID, and are not
notarized. macOS shows that message for any such app downloaded from the
internet. It means "not notarized", not "corrupt". After unzipping, remove
the download quarantine flag in Terminal:

```sh
xattr -dr com.apple.quarantine "/path/to/Beat Queen.app"
```

Do the same for `Beat Queen.vst3` and `Beat Queen.component` before copying
them to `~/Library/Audio/Plug-Ins/VST3` and `~/Library/Audio/Plug-Ins/Components`.

### Windows: "Windows protected your PC"

The builds are not code-signed, so SmartScreen may warn on first launch:

- Before extracting, right-click the zip, choose **Properties**, tick
  **Unblock**, then extract. This also clears the download mark from the
  plug-in.
- Click **More info**, then **Run anyway** to start the app.
- Copy `Beat Queen.vst3` to `C:\Program Files\Common Files\VST3\`.

### Linux

Extract with `tar -xzf`, so the executable permissions are kept. Copy
`Beat Queen.vst3` to `~/.vst3/`.

Every archive includes an `INSTALL.txt` with these steps for its platform.
If you build Beat Queen yourself (below), none of this applies.

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
docs/guide/     an 11-page tutorial on the techniques, from first principles
```

## Learn how it works

[`docs/guide/`](docs/guide/README.md) is a step-by-step tutorial for anyone
who wants to model a vintage instrument from its schematics. It covers:

- reading scanned schematics and writing netlists;
- nodal analysis and the trapezoidal rule;
- Newton's method and the K-method;
- real-time optimization;
- how each MRK-1 voice works;
- decoding the clock and rhythm wiring;
- validating against SPICE;
- building the plugin.

Beat Queen is an independent recreation. Maestro and Rhythm King are
trademarks of their respective owners. The service manual is not included in
this repository.
