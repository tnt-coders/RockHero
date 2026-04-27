# RockHero Architecture Plan

My recommendation: **keep the current architecture, but tighten the seams before you build much more on top of it.** I would not do a reset. The repo already has the right broad split: a pure-C++ core, an audio layer that hides Tracktion implementation details, a UI layer, and separate editor/game apps. The top-level build also keeps those pieces separated cleanly.

## Keep as-is

- **Keep `rock_hero_core` framework-free.** Right now it is standard C++ only and explicitly avoids JUCE and Tracktion. That is your most valuable architectural asset, so keep song/chart/domain data there.
- **Keep `rock_hero_audio::Engine` as the only Tracktion-facing façade.** Its header explicitly says it is the isolation layer for the rest of the app, and the implementation already hides Tracktion objects behind `Impl` while exposing a small API (`loadFile`, `play`, `pause`, `stop`, `seek`, thumbnail creation). That is a good level of encapsulation.
- **Keep the plan to let the gameplay runtime grow beyond the current JUCE shell.** The game app is explicitly described as a temporary shell until SDL/bgfx content owns the view, which is a healthy direction for a rhythm game.

## Change next

- **Insert an app/service layer between the editor UI and `audio::Engine`.** Right now `MainWindow::ContentComponent` wires buttons and seek actions directly to `m_audio_engine`. That is fine for a prototype, but it will get messy once you add file/session state, charts, latency calibration, guitar input, and error handling. Add something like `EditorSession`, `PlaybackController`, or `SongSession` and let the UI talk to that instead.
- **Stop making UI own transport-cache updates.** `WaveformDisplay` runs a private 60 Hz timer, calls `audio::Engine::updateTransportPositionCache()`, and uses that for cursor painting; `engine.h` itself calls this a temporary shim. That is the single biggest architectural smell in the repo today. Move clock/state publishing into the audio layer, and let UI become a passive reader.
- **Narrow the UI dependency on audio.** `rock_hero_ui` publicly links `rock_hero_audio`, and `WaveformDisplay` takes `audio::Engine&`. That is acceptable early on, but the cleaner long-term boundary is for UI to depend on a tiny playback/thumbnail interface rather than the full engine façade.

A good target shape is:

- `core`: song/chart/domain/state
- `application`: session/controller/use-cases
- `audio`: Tracktion/JUCE integration
- `ui`: controls/views against slim interfaces
- `apps`: composition roots

## Defer until later

- **Do not over-abstract Tracktion further yet.** The current `audio::Engine` wrapper is enough for the repo’s present scope. Making a second abstraction layer now would probably just add indirection before the real requirements are known.
- **Do not design final gameplay timing around the editor waveform path.** The repo README describes a 3D note highway and live guitar/VST-style ambitions, while the current waveform/playhead path is plainly editor-oriented and timer-driven. Keep using it for tooling, but treat gameplay sync as a later dedicated subsystem.
- **Do not merge editor and game concerns too early.** The repo already has distinct `rock-hero-editor` and `rock-hero` apps. Preserve that separation. It gives you room to let the editor stay JUCE-centric while the game runtime becomes more specialized.

## What I would implement first

1. Add `SongSession` or `PlaybackController` in a new application layer.  
   It should own:
   - current file/song
   - transport commands
   - play state
   - current position
   - load failures / validation

   Then have `MainWindow` talk to that instead of directly to `audio::Engine`. The current direct wiring in `ContentComponent` is the place to refactor first.

2. Replace `updateTransportPositionCache()` with push or pull owned by audio, not UI.  
   Even a simple `PlaybackStateSnapshot` API would be better than “UI timer mutates audio cache.” The repo already marks the current approach as temporary, so this is a very natural cleanup.

3. Define core-owned types now before more code accumulates.  
   I would add things like:
   - `SongId`
   - `SongSource`
   - `Chart`
   - `PlaybackPosition`
   - `TransportState`
   - `LatencyCalibration`
   - `PracticeSegment`

   Those belong in `rock_hero_core`, not in JUCE or Tracktion-facing layers. The repo is early enough that doing this now will pay off.

## Simple verdict

**Keep the approach. Rework the boundaries, not the architecture.**  
The project is already pointed in a good direction. The main thing to fix is that the UI currently reaches a bit too far into playback state management. If you correct that now, you will have a much better base for both the JUCE editor and the eventual gameplay runtime.
