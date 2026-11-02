# Transport

A music player that shows you what you hear. Play a track and watch meters, analyzers and stereo scopes run alongside it — built for people who make music, or just like to look under the hood.

## Download

Grab the newest **vX.Y.Z** release from the Releases page (a rolling *development build* of `main` is there too):

- **Windows** — `Transport-Windows-x64.exe`, just run it (no installer, no runtime needed).
- **macOS** — `Transport-macOS.zip` (Apple Silicon + Intel). Unzip, then right-click `Transport.app` → **Open** the first time, because the app isn't notarised yet.
- **Linux** — `Transport-Linux-x64.tar.gz`.

## Using it

- Drop an audio file on the window, or click the cover / waveform to open one (MP3, WAV, FLAC, AIFF, OGG; M4A/AAC/WMA on Windows and macOS).
- Click or drag on the waveform to seek. **Space** plays/pauses, **← →** seek 5 s (Shift: 30 s), **↑ ↓** volume, **N / P** next/previous track in the folder, **M** mono, **L** A–B loop.
- **Plugin slots:** click **+** to add a plugin, hover a slot for replace/remove, drag a slot to reorder, right-click for columns.
- **Quick-access buttons** (top right): left-click runs, right-click assigns another function or changes the grid size (2×2, 3×2, 4×2).

### Plugins

| Meters | Analyzers | Stereo | Fun |
|---|---|---|---|
| VU Meter | Spectrum Analyzer | Polar Stereo Field | Retro LED Bars |
| Peak / RMS | Spectrogram | Goniometer | |
| Loudness (EBU R128) | Oscilloscope | Phase Correlation | |

### Quick-access functions

Monitoring menu, playlist, add plugin, dim, open, mono, swap L/R, solo L/R, mid/side solo, bypass, A–B loop, always on top, compact mode, snapshot to PNG.

## Building

Requires CMake 3.22+ and a C++20 compiler. JUCE 8 is fetched automatically.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release      # analysis unit tests (EBU R128, FFT, stereo)
```

On Linux install the JUCE dependencies first (see `.github/workflows/build.yml`).

`Transport --snapshot out.png [--size 1400x900] [--slots spectrum,loudness] [--cols 4] [--browser]` renders the UI headlessly with a generated test track — used by CI to check the interface on every platform.

## Releasing

Bump `VERSION` in `CMakeLists.txt` (and add `.github/release-notes/vX.Y.Z.md`), push to `main`. CI builds Windows, macOS and Linux, runs the tests, and publishes **Transport vX.Y.Z** if that version isn't released yet. Pushing a `vX.Y.Z` tag works too.

## Architecture

```
file → decoder (read-ahead thread) → audio callback ─┬→ volume → output device
                                                     └→ lock-free FIFO → analyzer (UI thread, once per vblank)
                                                                           → AnalysisFrame → plugins → screen
```

- The audio callback never locks or allocates; monitoring (mono, swap, solo…) runs there, then a copy goes into a lock-free FIFO.
- `src/core` is pure DSP (FFT, K-weighted loudness with gating, correlation, polar field) and is unit-tested headlessly.
- The UI is driven by the display's vertical blank (`VBlankAttachment`), so every animation — needle springs, meter ballistics, slot layout, play/pause morph — is frame-rate independent and in sync with the screen. On macOS the UI is rendered through OpenGL; on Windows JUCE uses Direct2D.
- Plugins implement `Visualizer::update (const AnalysisFrame&, float dt)` + `paint()`. Add one to the registry in `src/app/Visualizers.cpp`.

## Licence

Built with [JUCE](https://juce.com), which is available under AGPLv3 or a commercial licence. Distributing a closed-source build requires a JUCE licence.
