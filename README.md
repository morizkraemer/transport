# Transport

A music player that shows you what you hear. Play a track and watch meters, analyzers and stereo scopes run alongside it — built for people who make music, or just like to look under the hood.

## Download

Grab the newest **vX.Y.Z** release from the Releases page (a rolling *development build* of `main` is there too):

- **Windows** — `Transport-Windows-x64.exe`, just run it (no installer, no runtime needed).
- **macOS** — `Transport-macOS.zip` (Apple Silicon + Intel). Unzip, then right-click `Transport.app` → **Open** the first time, because the app isn't notarised yet.
- **Linux** — `Transport-Linux-x64.tar.gz`. Video needs the system FFmpeg libraries (`sudo apt install ffmpeg` on Ubuntu/Debian).

## Using it

- Drop an audio file on the window, or click the cover / waveform to open one (MP3, WAV, FLAC, AIFF, OGG, M4A/AAC, WMA on Windows).
- **Video:** open a video the same way. The picture takes the place of the plugins; its sound runs through Transport like any track, so waveform, meters and effects work as usual. Click the picture to play/pause, double-click (or **F**) for full screen, **V** or the ▦ button switches between video and plugins.
  - Windows (Media Foundation): MP4, MOV, M4V, MKV, AVI, WMV, 3GP
  - macOS (AVFoundation): MP4, MOV, M4V, 3GP
  - Linux (FFmpeg): all of the above plus WebM, FLV, MPEG, TS, OGV
  - Codecs are whatever the system decodes: H.264 everywhere; HEVC, VP9, AV1 depend on OS version and installed extensions.
- Click or drag on the waveform to seek. **Space** plays/pauses, **← →** seek 5 s (Shift: 30 s), **↑ ↓** volume, **N / P** next/previous track in the folder, **M** mono, **L** A–B loop.
- **Customize layout** (MENU or the grid quick-access button): drag slots to reorder, set each slot to narrow/normal/wide/extra wide, add or replace plugins, and drag the orange handles to set the height of the header and waveform. The plugins pack themselves into rows that always fill the space. **Done** or Esc leaves the mode.
- **Maximize a plugin:** double-click it (or the ⤢ button on hover) to fill the whole plugin area; Esc restores.
- **Quick-access buttons** (top right): left-click runs, right-click assigns another function or changes the grid size (2×2, 3×2, 4×2). A short header shows one row.
- **Display size:** MENU → Display size (Small to Extra large).
- **Windows taskbar mini player:** hover Transport's taskbar button for cover, title, progress and ⏮ ⏯ ⏭ (MENU → Taskbar mini player to turn off).
- **Default player:** on first start Transport offers to become your default audio player (Windows opens Settings → Default apps for you to confirm; macOS switches directly). Also in MENU.

### Plugins (26)

| Meters | Analyzers | Stereo | Musical | Effects | Fun |
|---|---|---|---|---|---|
| VU Meter | Spectrum Analyzer | Polar Stereo Field | Tempo (BPM) | Equalizer (5-band) | Retro LED Bars |
| Peak / RMS | Spectrogram | Goniometer | Key + Camelot | Compressor | Radial Spectrum |
| Loudness (EBU R128) | Waterfall 3D | Phase Correlation | Pitch / Tuner | Maximizer (limiter) | Particles |
| True Peak & Clips | Tonal Balance | Stereo Imager | | | Cover Glow |
| Dynamics | Oscilloscope | | | | Vinyl Deck, Tape Reels |

**Effects change the sound.** They run in slot order (drag to reorder the chain), before the meters, so every analyzer shows the processed signal. The power button in a slot's header bypasses that effect; **Bypass** in quick access turns off all effects and monitoring at once. Settings are saved.

### Waveform

Right-click the waveform (or MENU → Waveform style): Bars, Mirror, Reflection, Line, Spectral colour (orange lows, white mids, blue highs), 3-Band.

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

`Transport --snapshot out.png [--size 1400x900] [--slots spectrum,loudness] [--cols 4] [--browser] [--file clip.mp4] [--fullscreen]` renders the UI headlessly with a generated test track — used by CI to check the interface on every platform.

`Transport --probe-media clip.mp4 --out result.json` reports what the platform video decoder sees (size, rotation, codecs, decoded colours, sound levels, real-time sync); CI runs it on the clips in `tests/media` and checks the result with `tests/check_media.py`.

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
- Video: `src/app/media` wraps the platform decoder (Media Foundation / AVFoundation / FFmpeg). The sound is exposed as a normal `AudioFormatReader`, so the audio engine is the clock; `VideoPlayer` decodes pictures a few frames ahead on its own thread and shows the one matching the audible position (output latency compensated).
- Plugins implement `Visualizer::update (const AnalysisFrame&, float dt)` + `paint()`. Add one to the registry in `src/app/Visualizers.cpp`.

## Licence

Built with [JUCE](https://juce.com), which is available under AGPLv3 or a commercial licence. Distributing a closed-source build requires a JUCE licence.
