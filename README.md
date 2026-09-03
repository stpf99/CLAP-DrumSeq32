# DrumSeq32 — CLAP drum sequencer

32-step × 4-instrument drum machine (CLAP instrument).
Sample playback only (WAV via dr_wav). No built-in FX.
Sequencer syncs to host transport (1 step = 1 sixteenth).
Minimal floating GTK window for loading the 4 samples; all other
parameters (128 step toggles, length, randomize, MIDI note map)
go through the host’s generic parameter UI.

## Dependencies

- C++17 compiler
- [CLAP headers](https://github.com/free-audio/clap) — already vendored under `clap/`
- [dr_wav](https://github.com/mackron/dr_libs) — already vendored under `third_party/dr_wav.h`
- GTK 3 development package (`libgtk-3-dev` on Debian/Ubuntu)

```bash
sudo apt install build-essential cmake pkg-config libgtk-3-dev
```

## Build

```bash
mkdir build && cd build
cmake ..
cmake --build .
# → DrumSeq32.clap
```

Install for your user:

```bash
mkdir -p ~/.clap
cp DrumSeq32.clap ~/.clap/
```

Then rescan plugins in your DAW (Bitwig, Reaper, Carla, …).

## Plugin ID

`com.tomasz.drumseq32`

## Layout

| File | Role |
|------|------|
| `plugin.h/cpp` | Main CLAP plugin class + factory/`clap_entry` |
| `sequencer.h/cpp` | Pattern + randomize modes |
| `sample.h/cpp` | WAV load (dr_wav) |
| `voice.h` | One-shot voice engine |
| `param_ids.h` | Stable parameter IDs |
| `gui_gtk.h/cpp` | Floating sample-loader window |
| `clap/` | Vendored CLAP SDK headers |
| `third_party/dr_wav.h` | WAV decoder |

## Notes

- GUI is floating-only (X11). Hosts that only support embedded GUI will still
  work — all sequencer controls are available as CLAP params.
- State saves pattern, length, randomize settings, MIDI note map and sample paths.
- Randomize trigger / clear trigger are momentary (rising-edge) params.
