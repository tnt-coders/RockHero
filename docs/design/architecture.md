# Project Architecture

## Overview

Rock Hero is an open-source guitar game built in C++. Players plug in a real guitar, hear it processed through real-time VST effects, and play along to songs via a scrolling note highway — while their tone automatically shifts, sweeps, and evolves with the music.

The application ships as executables built on shared static libraries, with
`rock-hero-audio-engine` providing the Tracktion adapter layer, `rock-hero-core` owning pure data
and domain logic, and `rock-hero-ui` owning JUCE-facing presentation components. No external DAW
required. It should feel like a game, not a production tool.

For the structural engineering rules that govern how new code should be organized, see
`architectural-principles.md`.

---

## What It Does

- Plays back a backing track synchronized with a scrolling note highway.
- Takes real guitar input through ASIO with sub-10ms latency.
- Processes guitar signal through VST plugins (amp sims, effects, cabinets).
- Automates tone changes over a song's duration — plugin bypass/activation, parameter sweeps (wah, whammy), and crossfading between plugin chains.
- Detects what the player is playing and scores note accuracy and timing.

---

## Technology Stack

| Layer | Technology | Role |
|-------|-----------|------|
| Audio engine | Tracktion Engine | Transport, plugin hosting, automation, backing track playback |
| Audio framework | JUCE | ASIO device management, audio primitives, UI components |
| Plugin format | VST3 (MIT licensed) | Guitar amp sims, effects, cabinets |
| Audio I/O | ASIO (GPL3 licensed) | Low-latency guitar input on Windows |
| Song format | open-psarc (Conan) | PSARC format read/write for song packages |
| Game rendering | SDL3 + bgfx | 3D note highway, visual feedback |
| Editor UI | JUCE Components | Waveform display, automation curves, plugin management |
| License | AGPLv3 | Compatible with all dependencies at zero cost |

---

## Repository Layout

\code{.txt}
RockHero/
  apps/
    rock-hero-editor/   — Editor executable: flat app layout while the target is small
    rock-hero/          — Game executable: flat app layout while the target is small
  libs/
    rock-hero-audio-engine/ — Tracktion Engine isolation adapter (static library)
    rock-hero-core/     — Song data model + format serialization (static library, no JUCE)
    rock-hero-ui/       — JUCE UI components (static library)
  docs/                 — Doxygen configuration
  external/
    tracktion_engine/   — Git submodule: Tracktion Engine + JUCE 8
  project-config/       — Git submodule: CMake presets, Conan 2.x, Doxygen theme, lint
\endcode

App targets keep their implementation files at the target root until they grow enough to justify
feature folders. Reusable libraries keep `src/` plus namespaced public headers under
`include/<library_name>/` so consumer includes stay explicit and collision-resistant.

**Dependency rules:**

- `libs/rock-hero-audio-engine` depends on Tracktion and JUCE audio modules.
- `libs/rock-hero-core` depends on standard C++ only; no JUCE, no Tracktion. May use
  format-specific Conan packages (e.g. `open-psarc`).
- Neither library depends on the other.
- App executables may depend on both libraries and on `rock-hero-ui`.
- Public library headers are included as `<rock_hero_audio_engine/...>`,
  `<rock_hero_core/...>`, and `<rock_hero_ui/...>`.
- Tracktion headers are isolated to `rock-hero-audio-engine` implementation files.

**Architectural principles:**

- Keep most behavior in pure or near-pure libraries rather than in UI or app targets.
- Treat `rock-hero-audio-engine` as an adapter around Tracktion/JUCE, not as a home for general
  business logic.
- Keep `rock-hero-ui` focused on presentation and intent emission rather than policy.
- Keep time, threading, hardware, and IO concerns at the boundary where they can be simulated or
  replaced in tests.
- Prefer project-owned abstractions and replayable simulations over framework-heavy test strategies.

See `architectural-principles.md` for the full guidance.

---

## Two-Track Design

Rock Hero is not a general-purpose DAW. It uses exactly two tracks:

**Track 1 — Backing Track**: A `WaveAudioClip` containing the song's audio file. Displayed as a
waveform with a playhead in the editor.

**Track 2 — Guitar Input**: Receives live guitar through ASIO. Routed through a chain of VST
plugins. Automation envelopes drive plugin parameters over the song's duration — bypass, wet/dry
mix, wah position, and any other exposed parameter. Mid-song tone changes are achieved by loading
multiple plugins and automating their bypass and mix parameters, with short ramps (5-10ms) to avoid
clicks. Tracktion Engine's `RackType` system is available for more complex routing needs.

Both tracks share a single transport, tempo map, and time signature sequence managed by Tracktion Engine's `Edit` data model.

---

## Song Data Model

The `rock-hero-core` library owns all persistent song data. It depends on standard C++ only.

\code{.txt}
Song
  metadata          (title, artist, album, year)
  audio_asset_ref   (path/identifier for the backing track audio file)
  tone_timeline_ref (opaque blob — serialized tone automation; interpreted exclusively by
                     rock-hero-audio-engine)
  chart
    arrangements[*]
      part          (Lead | Rhythm | Bass)
      difficulty    (Easy | Medium | Hard | Expert)
      note_events[*]
        time_seconds
        duration_seconds
        string_number (1–6)
        fret
\endcode

`Song` is the persistence and session root. When a session opens, the application loads a `Song`
from disk, passes `audio_asset_ref` to `rock-hero-audio-engine` for playback, and passes
`tone_timeline_ref` to `rock-hero-audio-engine` as an opaque blob. The game or editor reads
`chart` to drive gameplay or authoring. `rock-hero-core` never interprets tone automation data —
that belongs entirely to `rock-hero-audio-engine`.

---

## Application Responsibilities

### Rock Hero Editor

Hosts tone design and chart authoring in one process. Loads and mutates `Song`/`Chart`/`Arrangement` while auditioning playback. Disables structural model edits while transport is running. Tone and chart decisions are kept in one executable by design — they are too tightly coupled to author in separate tools.

### Rock Hero Game

Loads a `Song` and starts a playback session. Displays the note highway and scoring UX. Owns scoring logic — evaluates note hit/miss events against audio-derived timing. Treats the `Song` model as read-only during gameplay.

---

## Architecture Diagram

\code{.txt}
┌──────────────────────────────┐   ┌───────────────────────────────┐
│     rock-hero-editor         │   │     rock-hero                 │
│                              │   │                               │
│  ┌────────────────────────┐  │   │  ┌─────────────────────────┐  │
│  │    Editor Window       │  │   │  │    Game Window          │  │
│  │    (JUCE Components)   │  │   │  │    (SDL3 + bgfx)        │  │
│  │                        │  │   │  │                         │  │
│  │  • Waveform display    │  │   │  │  • 3D note highway      │  │
│  │  • Plugin chain view   │  │   │  │  • Score display        │  │
│  │  • Automation envelopes│  │   │  │  • Hit feedback/effects │  │
│  │  • Transport controls  │  │   │  │  • Fretboard view       │  │
│  └───────────┬────────────┘  │   │  └──────────┬──────────────┘  │
│              │               │   │             │                 │
│  ┌───────────┴────────────┐  │   │  ┌──────────┴──────────────┐  │
│  │  libs/rock-hero-       │  │   │  │  libs/rock-hero-        │  │
│  │  audio-engine          │  │   │  │  audio-engine           │  │
│  │  (Tracktion Engine)    │  │   │  │  (Tracktion Engine)     │  │
│  │                        │  │   │  │                         │  │
│  │  Track 1: Backing Track│  │   │  │  Track 1: Backing Track │  │
│  │  Track 2: Guitar Input │  │   │  │  Track 2: Guitar Input  │  │
│  │  Transport + Automation│  │   │  │  Transport + Automation │  │
│  └────────────────────────┘  │   │  └─────────────────────────┘  │
│                              │   │                               │
│  ┌────────────────────────┐  │   │  ┌─────────────────────────┐  │
│  │  libs/rock-hero-core   │  │   │  │  libs/rock-hero-core    │  │
│  │  Song/Chart/Arrangement│  │   │  │  Song/Chart/Arrangement │  │
│  └────────────────────────┘  │   │  │  + Scoring logic        │  │
│                              │   │  └─────────────────────────┘  │
└──────────────────────────────┘   │                               │
                                   │  ┌─────────────────────────┐  │
                                   │  │  Gameplay Systems       │  │
                                   │  │  • Pitch detection      │  │
                                   │  │  • Note matching        │  │
                                   │  │  • Latency calibration  │  │
                                   │  └─────────────────────────┘  │
                                   └───────────────────────────────┘
\endcode

Both executables link `rock-hero-audio-engine` and `rock-hero-core` as static libraries. Static
linking avoids singleton aliasing issues and simplifies deployment.

---

## Threading Model

Each executable runs its own set of threads. The threading rules are identical in both — what differs is which threads each process actually uses at a given development stage.

**Audio thread** (Tracktion Engine / JUCE): Highest priority. Runs the ASIO callback, processes the
VST plugin chain, plays back the backing track, evaluates automation curves. Copies raw guitar input
samples (pre-effects) into a lock-free ring buffer for the analysis thread. Never touches UI, never
allocates memory, never blocks.

**Analysis thread** (pitch detection, `rock-hero-game`): Reads guitar input from the ring buffer.
Runs pitch detection on overlapping windows (e.g. 2048-sample window, 512-sample hop, ~86
detections/second at 44.1kHz). Writes results (pitch, confidence, onset timing) to a lock-free
output structure. Runs on the clean input signal before the VST plugin chain — distortion and
modulation make pitch detection dramatically harder.

**UI thread** (JUCE message loop): Handles editor/game window repaints, mouse interaction,
transport controls. Also ticks SDL event polling and triggers bgfx frame submissions for the game
window. Reads pitch detection results for scoring and visual feedback. JUCE owns this loop; SDL is
polled manually from within it.

**Render thread** (optional): bgfx can submit GPU work separately if needed. For the note highway's
geometric simplicity, single-threaded rendering from the UI thread is likely sufficient.

### Rules

- No locks, heap allocation, or file IO on the audio thread.
- Cross-thread communication uses atomics or lock-free queues.
- Mutations that can rebuild processing graphs are message-thread only.

### Thread Communication

\code{.txt}
Audio Thread                Analysis Thread           UI / Game Thread
     │                           │                          │
     │  raw input samples        │                          │
     ├──► [lock-free ring buf] ──►│                          │
     │                           │  pitch/onset results     │
     │                           ├──► [lock-free struct] ───►│
     │  transport position       │                          │
     ├──► [std::atomic] ─────────┼─────────────────────────►│
     │                           │                          │
\endcode

All communication from the audio thread is lock-free. The audio thread's only outputs are the ring buffer (samples), the atomic transport position, and its normal audio output to the speakers. No mutexes, no blocking, no exceptions.

---

## Timing and Latency

The audio thread is the single source of truth for timing. The game view and scoring system derive all state from the transport position — they never maintain independent clocks.

The full latency chain that must be accounted for:

- **ASIO buffer**: 64–256 samples (1.5–5.8ms at 44.1kHz)
- **Plugin chain processing**: usually negligible for guitar effects
- **Render frame delay**: up to 16ms at 60fps
- **Display latency**: monitor-dependent, typically 5–15ms

A **calibration system** measures round-trip latency (player strums, system measures delay). This offset is applied to scoring so note hit detection aligns with what the player perceives, not the raw audio timeline.

All scoring comparisons happen in audio-sample time with calibration offsets applied consistently. Mixing audio-thread time with render-thread time in scoring logic is explicitly prohibited — it produces inconsistent hit windows.

---

## Gameplay Systems

**Pitch detection**: YIN or autocorrelation on the clean pre-effects signal. Combined with onset
detection (detecting *when* the player plays) which is often more reliable than pitch detection for
scoring. Polyphonic detection needed for chords.

**Note matching and scoring**: Configurable tolerance windows for timing and pitch accuracy. Must
handle string bends, hammer-ons, pull-offs, and slides as valid techniques rather than missed notes.

**Latency calibration**: Built into the timing architecture from day one, not bolted on later.

---

## Editor UI

Built with JUCE components — the same framework Waveform (the DAW built on Tracktion Engine) uses for its entire interface.

The editor (`rock-hero-editor`) consists of:

- **Waveform display**: `juce::AudioThumbnail` showing the backing track with a playhead overlay.
- **Plugin chain view**: Horizontal row of loaded effects with add/remove/reorder controls.
- **Automation area**: Shares the waveform's horizontal time axis and zoom/scroll state. Breakpoint envelopes control plugin parameters over the song's duration.
- **Transport controls**: Play, stop, seek.

The waveform and automation area share a single zoom/scroll controller. Coordinate conversion from time-to-pixels uses Tracktion Engine's tempo sequence for beat-accurate positioning.

UI theming is planned as a distinct later phase — functionality first, polish second.

---

## Game View

Built with SDL3 (window management, input) and bgfx (rendering abstraction over Vulkan, Metal, D3D11/D3D12, OpenGL). Lives in `rock-hero`.

The note highway is geometrically simple — textured quads on lanes with perspective projection. bgfx handles this easily with room for glow effects, particles on note hits, and other visual feedback.

SDL is initialized without its own event pump. SDL events are polled manually from JUCE's message loop. The game window gets its own native window handle that bgfx renders into.

---

## VST Plugin Safety

- **Performance monitoring**: Per-buffer processing time tracked for each plugin. Automatic bypass if a plugin consistently overruns.
- **Fallback chains**: Safe-mode effects during gameplay; full plugin freedom in practice/editor mode.
- **Smooth transitions**: Never hard-switch bypass. Always use 5-10ms parameter ramps. Pre-activate plugins (loaded with mix at zero) before they're needed to avoid CPU spikes.
- **Plugin validation**: Basic stability testing at load time.

---

## Licensing

All dependencies are compatible with AGPLv3 at zero cost:

| Dependency | License | Compatibility |
|-----------|---------|--------------|
| Tracktion Engine | GPL3 | Compatible (AGPLv3 is more restrictive, governs combined work) |
| JUCE | AGPLv3 | Requires project to use AGPLv3 |
| VST3 SDK | MIT (as of October 2025) | Permissive, compatible with everything |
| ASIO SDK | Dual GPL3/proprietary (as of October 2025) | GPL3 option compatible |
| open-psarc | MIT | Permissive, compatible with everything |
| SDL3 | zlib | Permissive |
| bgfx | BSD 2-Clause | Permissive |

The project is licensed under **AGPLv3** because JUCE requires it. For a desktop application run locally, AGPLv3 is functionally identical to GPLv3 — the additional network clause only applies to software accessed over a network.

Charging for the software is permitted. Anyone can fork, modify, and redistribute (including selling their own version). Source code must be made available to recipients. If the project ever moves to closed source, commercial licenses for both Tracktion Engine and JUCE would be required.

---

## Development Approach

Build the real architecture from the start but keep visuals simple. Start with rectangles scrolling down the screen, not a polished 3D fretboard. Validate the hard unknowns first.

### Build First: Timing Instrumentation

Before gameplay, add permanent debug logging that records:

- Transport position on each audio callback
- Sample positions written to the ring buffer
- Analysis thread detection results with timestamps
- Transport position read by the game view each frame
- Scoring inputs: position compared, calibration offset applied, result

When gameplay feels "off," these logs reveal whether the issue is transport drift, pitch detection latency, calibration error, or render jitter. Without this data, debugging timing is guesswork.

### Prototyping Priorities

1. Timing instrumentation framework
2. ASIO input through Tracktion Engine with VST plugin chain producing audio
3. Backing track playback synced to transport
4. Lock-free transport position shared with SDL/bgfx window (scrolling rectangles)
5. Pitch detection on guitar input
6. Note matching and scoring with latency calibration
7. Plugin parameter automation (bypass, mix) over song timeline

Visual polish, 3D fretboard, particles, and UI theming come after gameplay fundamentals are proven.

### Testing Infrastructure

Catch2 (v3) is declared in `conanfile.txt` and integrated into the build. Per-library test targets
live alongside each library:

- `libs/rock-hero-core/tests/` — active; covers `Song`, `Chart`, and `Arrangement` construction
  and field access
- `libs/rock-hero-audio-engine/tests/` — not yet added
- `libs/rock-hero-ui/tests/` — not yet added

Tests are registered with CTest via `catch_discover_tests`. See `architectural-principles.md` for
the full testing strategy.

---

## Known Risks

| Risk | Severity | Mitigation |
|------|----------|------------|
| Tracktion Engine timing doesn't fit gameplay | Moderate | Validate early; architecture supports fallback to raw JUCE |
| Dual timing systems cause drift/scoring errors | High (conceptual) | Audio thread is single source of truth; timing instrumentation catches drift |
| VST plugins cause CPU spikes | Low | Performance monitoring, automatic bypass, fallback chains |
| Pitch detection unreliable | High (algorithmic) | Prototype early; clean pre-effects signal; combine with onset detection |
| Ring buffer overruns | Low | SPSC design; drop oldest on overflow; never block audio thread |
| Plugin automation clicks/pops | Moderate | Ramp-based transitions; pre-activate plugins |
| Debugging through Tracktion abstraction | Moderate | Timing instrumentation; clean interfaces; modular architecture |
| Audio thread contention | Low | Audio thread highest priority, never blocks; other threads tolerate drops |
| AGPLv3 limits commercial options | Strategic | Acknowledged tradeoff; commercial licenses available if needed |

---

## Fallback Strategy

If Tracktion Engine proves unsuitable (timing issues, API incompatibilities, debugging friction), the fallback is **JUCE alone** — building a custom transport, automation system, and audio clip playback on JUCE's primitives (`AudioProcessorGraph`, `AudioPluginFormatManager`, `AudioPlayHead`). This is significant work (several months) but preserves all other architectural decisions.

The architecture is designed for this: Tracktion Engine is isolated behind
`libs/rock-hero-audio-engine`. The game view, pitch detection, scoring, and editor UI depend on a
transport position and a data model, not on Tracktion directly. Either Tracktion or a custom JUCE
implementation can back those interfaces.
