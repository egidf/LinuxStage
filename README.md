# LSP Audio Player

Desktop audio player with a dual LV2 effects rack: a **Main Player** and an independent **BGM Player** (background music), each with its own playlist, transport, volume, fades and effects rack. Built to the requirements in [`PRD_LSP_Audio_Player.md`](PRD_LSP_Audio_Player.md).

![UI mockup](LSP%20Audio%20Player%20Effects%20Rack.png)

## Status

Implemented for Linux (Milestone 0-3 of the PRD):

| Area | State |
|---|---|
| Dual players, playlists, transport, seek, volume, mute (FR-01/02) | done |
| Fade in/out per player, applied on stop/track change (FR-03) | done |
| LV2 discovery via Lilv, catalog with refresh (FR-04) | done |
| Rack per player: add / remove / reorder / enable-bypass (FR-05) | done |
| Native plugin UI via Suil, generic parameter fallback (FR-06) | done (X11 embedding is best effort) |
| Audio engine, master mixer, meters, xrun recovery (FR-07) | done |
| Session save/load with schema version + missing-file marking (FR-08) | done |
| Logging + in-app log view (FR-09) | done |
| Rack presets (FR-10) | done (basic) |
| Windows VST3 (FR-12) | not started (post-MVP per PRD) |

## Build

Dependencies (Debian/Ubuntu package names):

```
build-essential cmake ninja-build pkg-config
qt6-base-dev                       # Qt 6 Widgets
liblilv-dev libsuil-dev libx11-dev # LV2 host + plugin UIs
libasound2-dev                     # ALSA output
libsndfile1-dev                    # decoding + WAV rendering
lv2 plugins (e.g. lsp-plugins-lv2, swh-plugins, calf-plugins)
```

```
cmake -S . -B build -G Ninja
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure     # unit tests, no audio device needed
```

Run:

```
./build/lsp-audio-player
```

Useful command line options:

| Option | Meaning |
|---|---|
| `--session FILE` | Session file to load (default `~/.config/lsp-audio-player/session.json`) |
| `--device NAME` | ALSA device: `default`, `pulse`, `hw:0,0`, `null` |
| `--sample-rate HZ`, `--buffer FRAMES` | Engine configuration |
| `--render SECONDS OUT.wav` | Headless offline render of the session (no GUI, no audio device) |
| `--verbose` | Debug logging |

Diagnostic tools:

```
./build/lv2probe                       # list discovered LV2 plugins
./build/lv2probe --filter limiter      # filter by substring
./build/lv2probe --selftest URI        # instantiate + process a test signal
./build/lv2probe --uitest URI          # open/close the native plugin UI
./build/lv2probe --stress URI 20       # repeated instantiate/destroy
```

`URI` may be a full LV2 URI or an unambiguous short suffix such as
`para_equalizer_x16_stereo`; if several plugins share the suffix the tool
asks for the full URI instead.

## Keyboard shortcuts

| Key | Action |
|---|---|
| `Space` | Main player: play / pause |
| `S` / `N` / `P` | Main player: stop / next / previous |
| `Ctrl+Space`, `Ctrl+S` | BGM player: play / pause, stop |
| `Ctrl+1..4` | Switch tab (Player, Effects Rack, Playlist, Settings) |

## Architecture

```
src/core       session model, JSON persistence, logging, paths   (Qt Core only)
src/engine     decoder (libsndfile), player lanes, fade envelope,
               rack chains, master mixer, ALSA/offline sinks     (no Qt)
src/plugins    IPluginHost contract + LV2 adapter (Lilv discovery,
               instancing, worker:schedule, Suil editor)
src/ui         Qt Widgets UI: panels, rack cards, browser, editor
tests          headless tests (ring buffer, fade, session, offline render)
tools          lv2probe diagnostics
```

Signal order (documented in `src/engine/AudioEngine.h`):

```
decoder -> rack (EffectChain, in rack order) -> player fade/volume -> master mix -> device
```

Threading:

* one real-time thread runs `processBlock()` and writes to the sink; it never allocates, locks or logs,
* per-player feeder threads own the decoders and feed lock-free ring buffers (no disk I/O in the audio callback),
* rack changes are built off-thread and swapped in atomically at a block boundary; superseded chains are deleted by a housekeeping thread,
* parameter and enable changes travel through a fixed-size command queue.

## Known limitations

* Files are played at the engine sample rate - there is no resampler yet, so files with a different rate play at the wrong speed (a warning is logged).
* Gapless playback and crossfade are post-MVP (PRD 7.2).
* Native plugin UI embedding targets X11; when embedding fails the editor opens generically (parameter knobs) with a visible explanation.
* Parameter changes made inside a *native* plugin UI are not written back into the session file (generic editor changes are).
* VST3/Windows support is not implemented.

## Session file

`~/.config/lsp-audio-player/session.json` holds both playlists, volumes, fades, master settings and both racks (plugin format + identifier + parameter snapshot). Missing files and unavailable plugins are preserved and shown as *Missing* instead of being dropped. Logs go to `~/.local/share/lsp-audio-player/lsp-audio-player.log`.
