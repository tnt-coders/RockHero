\page design_architecture Architecture

# Overview

Rock Hero is an open-source guitar game built in C++. Players plug in a real guitar, hear it processed through real-time VST effects, and play along to songs via a scrolling note highway — while their tone automatically shifts, sweeps, and evolves with the music.

The durable source layout is organized by product scope at the repository root:
`rock-hero-common`, `rock-hero-editor`, and `rock-hero-game`. Each scope owns `core`, `audio`, and
`ui` submodules when that layer is needed, and executable startup lives under the matching
product's `app/` folder. No external DAW required. It should feel like a game, not a production
tool.

For the structural engineering rules that govern how new code should be organized, see
\ref design_architectural_principles.

---

# What It Does

- Plays back a backing track synchronized with a scrolling note highway.
- Takes real guitar input through ASIO with sub-10ms latency.
- Processes guitar signal through VST plugins (amp sims, effects, cabinets).
- Automates tone changes over a song's duration — plugin bypass/activation, parameter sweeps (wah, whammy), and crossfading between plugin chains.
- Detects what the player is playing and scores note accuracy and timing.

---

# Technology Stack

| Layer | Technology | Role |
|-------|-----------|------|
| Audio engine | Tracktion Engine | Transport, plugin hosting, automation, backing track playback |
| Audio framework | JUCE | ASIO device management, audio primitives, UI components |
| Plugin format | VST3 (MIT licensed) | Guitar amp sims, effects, cabinets |
| Audio I/O | ASIO (GPL3 licensed) | Low-latency guitar input on Windows |
| Rock Hero package formats | libzip + nlohmann_json | `.rhp` editor project packages; `.rock` native song packages |
| Rocksmith import format | open-psarc (Conan) | PSARC format read/write for Rocksmith packages |
| Game rendering | SDL3 + bgfx | 3D note highway, visual feedback |
| Editor UI | JUCE Components | Waveform display, automation curves, plugin management |
| License | AGPLv3 | Compatible with all dependencies at zero cost |

`.rhp` packages store `project.json` at the root and song content under `song/`. `.rock`
packages store native song content directly at the archive root and can be imported by the
editor as unsaved projects.

---

# Repository Layout

\code{.txt}
RockHero/
  rock-hero-common/
    core/               - shared framework-free domain and data behavior
    audio/              - shared audio contracts plus default implementation
    ui/                 - shared UI used by both products when needed
  rock-hero-editor/
    app/                - editor executable startup
    core/               - editor-specific headless workflow and policy
    audio/              - editor-specific audio behavior outside the shared engine
    ui/                 - editor-specific JUCE presentation
  rock-hero-game/
    app/                - game executable startup and resources
    core/               - game-specific pure gameplay behavior
    audio/              - game-specific audio analysis and gameplay plumbing
    ui/                 - game-specific presentation and rendering
  docs/                 - Doxygen configuration
  external/
    tracktion_engine/   - Git submodule: Tracktion Engine + JUCE 8
  project-config/       - Git submodule: CMake presets, Conan 2.x, Doxygen theme, lint
\endcode

The old root `apps/` and `libs/` folders have been removed and should not be recreated for product
code.

Executable targets live under `app/` and should remain thin composition roots. Reusable libraries
keep `src/` plus namespaced public headers under `include/rock_hero/<scope>/<module>/` so consumer
includes stay explicit and collision-resistant.

**Dependency rules:**

- `common` code must not depend on `editor` or `game` code.
- `editor` code may depend on `common` code, but not on `game` code.
- `game` code may depend on `common` code, but not on `editor` code.
- `core` submodules must not depend on sibling `ui` submodules.
- App executables may link the matching product umbrella plus the common umbrella.
- Libraries and tests should link narrow submodule targets rather than parent umbrellas.
- Tracktion headers are isolated to `rock-hero-common/audio` implementation files and private
  implementation headers.
- Normal library code should use project-owned audio ports from `rock_hero::common::audio`; app
  composition code and concrete adapter tests may construct `common::audio::Engine`.

## JUCE and Tracktion CMake linkage

Rock Hero uses project-owned static wrapper targets for the JUCE and Tracktion modules it consumes.
Consumers link their `rock_hero::` aliases (`rock_hero::juce_core`,
`rock_hero::tracktion_engine`, and so on). Each wrapper links the raw module target privately, then
forwards the module's compile definitions and include paths to consumers.

For Rock Hero, that means:

- Rock Hero libraries and apps link the project-owned wrapper aliases, not raw `juce::juce_*` or
  `tracktion::tracktion_*` module targets.
- Raw JUCE and Tracktion module targets stay behind the wrapper layer rather than being re-exported
  through the rest of the project graph.

This departs from JUCE's default recommendation of target-local private module links, but it serves
the modular structure of this repository better by compiling each third-party module through one
project-owned static target instead of recompiling the same modules separately in multiple Rock Hero
libraries and apps.

This is still a build-system rule, not a blanket ban on JUCE in public headers. Some public
interfaces may still mention JUCE types where that is the pragmatic design choice.

### Build-policy exception for framework-free core modules

`rock-hero-common/core` remains a source-level and API-level framework-free module: its public
headers and implementation must not include JUCE or Tracktion, and its domain behavior must not
depend on framework runtime semantics.

There is one deliberate build-system exception. First-party targets, including framework-free core
targets, link `rock_hero::build_policy`. That target is defined only in
`cmake/RockHeroBuildPolicy.cmake` and currently forwards JUCE's recommended warning,
configuration, and Release LTO helper targets. This is accepted because Rock Hero's normal
configure already brings in JUCE/Tracktion before first-party libraries are declared, and JUCE's
defaults are a practical shared compiler policy for the current project.

This exception must stay localized to `cmake/RockHeroBuildPolicy.cmake`. No framework-free core
CMake file may link JUCE targets directly, and no core source or header may include JUCE or
Tracktion. If the build-time dependency ever blocks a core-only package, faster core-only tests, or
a future non-JUCE build, replace the implementation of `rock_hero::build_policy` in that one file
with project-owned flags and leave first-party call sites unchanged.

## Include-path convention

Each library exposes its public headers through a **PUBLIC** include directory at
`rock-hero-<scope>/<module>/include/`. External consumers - other libraries, apps, and tests -
always reference first-party headers through the full nested path:

\code{.cpp}
#include <rock_hero/common/audio/engine.h>
#include <rock_hero/common/core/song.h>
#include <rock_hero/editor/ui/editor_view.h>
#include <rock_hero/game/core/scoring.h>
\endcode

Product-scope code should use the full nested product path.

Each library additionally adds a **PRIVATE** include directory pointing at its own nested module
folder (`rock-hero-<scope>/<module>/include/rock_hero/<scope>/<module>/`). This lets a library's
own translation units reach their main header by basename with a quoted include. The PRIVATE dir
is not propagated to consumers, so the short form is only visible inside the owning library.

The project-wide convention for `#include` form is:

| Form | Meaning |
|------|---------|
| `"foo.h"` | The **main header** of the current translation unit (same-module, short form via the PRIVATE dir). Reserving quotes for the main header lets clang-format auto-promote it to the top of the file with a blank-line separator. |
| `<rock_hero/<scope>/<module>/foo.h>` | Any other first-party header, including sibling headers in the *same* module. Keeps cross-file references uniform regardless of where they are read from. |
| `<tracktion_engine/...>`, `<juce_*/...>`, `<catch2/...>`, `<BinaryData.h>`, `<JuceHeader.h>` | Third-party. |
| `<atomic>`, `<algorithm>`, `<filesystem>` | C++ standard library (angle-bracket, no path separator, no extension). |

Example from a product-scope library source file:

\code{.cpp}
#include "engine.h"

#include <rock_hero/common/audio/i_thumbnail.h>

#include <tracktion_engine/tracktion_engine.h>

#include <atomic>
\endcode

The four blocks — main, other first-party, third-party, standard library — are produced
automatically by clang-format's `IncludeBlocks: Regroup` using the `IncludeCategories`
defined in `.clang-format`.

**Architectural principles:**

- Keep most behavior in pure or near-pure libraries rather than in UI or app targets.
- Treat `rock-hero-common/audio` implementation files as adapters around Tracktion/JUCE, not as a
  home for general business logic.
- Keep product `ui` submodules focused on presentation and intent emission rather than policy.
- Put headless editor workflow in `rock-hero-editor/core` once it becomes broader than a local UI
  helper.
- Keep time, threading, hardware, and IO concerns at the boundary where they can be simulated or
  replaced in tests.
- Prefer project-owned abstractions and replayable simulations over framework-heavy test strategies.

See \ref design_architectural_principles for the full guidance.

---

# Arrangement Display Design

Rock Hero is not a general-purpose DAW. The editor displays one playable Arrangement at a time:
Lead, Rhythm, Bass, or another future authored route. An Arrangement contains the lanes needed to
audition and author that route:

**Backing audio lane**: The full audio asset used for playback and waveform display. Most projects
will share the same backing audio across arrangements, but the model permits arrangement-specific
audio so a route can override the shared asset when needed.

**Guitar input/effects lane**: Receives live guitar through ASIO and routes it through VST
plugins. Automation envelopes drive plugin parameters over the arrangement duration: bypass,
wet/dry mix, wah position, and any other exposed parameter. Mid-song tone changes are achieved by
loading multiple plugins and automating their bypass and mix parameters, with short ramps
(5-10ms) to avoid clicks. Tracktion Engine's `RackType` system is available for more complex
routing needs.

The lanes in the displayed arrangement share a single transport, tempo map, and time signature
sequence managed by Tracktion Engine's `Edit` data model.

---

# Song Data Model

The `rock-hero-common/core` library owns shared persistent song data. It remains framework-free and
keeps format parsing/package dependencies private to the module.

\code{.txt}
Song
  metadata          (title, artist, album, year)
  arrangements[*]
    id            (stable project-local arrangement identifier)
    part          (Lead | Rhythm | Bass)
    difficulty    (0 Unknown, 1-10 authored rating)
    audio_asset    (required path/identifier for backing audio)
    audio_duration (full natural duration of the audio asset)
    tone_timeline_ref (opaque blob interpreted exclusively by common/audio)
    note_events[*]
      position.seconds
      duration.seconds
      string_number (1–6)
      fret
\endcode

Arrangement difficulty is stored as the numeric rating. The display tier is derived from that
rating: Easy for 1-2, Medium for 3-4, Hard for 5-6, Expert for 7-8, and Master for 9-10. A value
of 0 represents Unknown so draft/default arrangements do not imply a fake difficulty; validation
for playable songs should reject Unknown.

`Song` is the persistence root. The editor session projects the song's arrangements into a
framework-free `Session` and displays one arrangement at a time. Native song package loading
validates archive structure, safe asset paths, and required arrangement audio references. Before a
parsed song is committed to the editor session, the editor workflow validates every arrangement's
audio through the `rock-hero-common/audio` boundary and rejects the project if any asset is
unreadable or reports a non-positive duration. When an arrangement is selected, the application
passes its `audio_asset`
to `rock-hero-common/audio` for playback and waveform generation, and passes `tone_timeline_ref`
to `rock-hero-common/audio` as an opaque blob. The game or editor reads the arrangement notes to
drive gameplay or authoring. `rock-hero-common/core` never interprets tone automation data - that
belongs entirely to `rock-hero-common/audio`.

The editor-facing audio boundary is `common::audio::IAudio`: it prepares loaded songs by validating
arrangement audio and filling accepted durations, makes the selected arrangement active in the
playback backend, and clears the active arrangement when the project closes. `common::audio::IEdit`
is reserved for future undoable/redoable model-edit commands and should not carry project loading,
audio preparation, transport, or playback setup responsibilities.

---

# Application Responsibilities

## Rock Hero Editor

Hosts tone design and chart authoring in one process. Loads and mutates `Song`/`Arrangement`
while auditioning playback. Disables structural model edits while transport is running. Tone and
chart decisions are kept in one executable by design — they are too tightly coupled to author in
separate tools.

## Rock Hero Game

Loads a `Song` and starts a playback session. Displays the note highway and scoring UX. Owns scoring logic — evaluates note hit/miss events against audio-derived timing. Treats the `Song` model as read-only during gameplay.

---

# Architecture Diagram

\code{.txt}
┌───────────────────────────────┐   ┌───────────────────────────────┐
│     rock-hero-editor          │   │     rock-hero                 │
│                               │   │                               │
│  ┌─────────────────────────┐  │   │  ┌─────────────────────────┐  │
│  │    Editor Window        │  │   │  │    Game Window          │  │
│  │    (JUCE Components)    │  │   │  │    (SDL3 + bgfx)        │  │
│  │                         │  │   │  │                         │  │
│  │  • Waveform display     │  │   │  │  • 3D note highway      │  │
│  │  • Plugin chain view    │  │   │  │  • Score display        │  │
│  │  • Automation envelopes │  │   │  │  • Hit feedback/effects │  │
│  │  • Transport controls   │  │   │  │  • Fretboard view       │  │
│  └───────────┬─────────────┘  │   │  └──────────┬──────────────┘  │
│              │                │   │             │                 │
│  ┌───────────┴─────────────┐  │   │  ┌──────────┴──────────────┐  │
│  │  common/audio           │  │   │  │  common/audio           │  │
│  │  (Tracktion Engine)     │  │   │  │  (Tracktion Engine)     │  │
│  │                         │  │   │  │                         │  │
│  │  Backing Audio Lane     │  │   │  │  Backing Audio Lane     │  │
│  │  Guitar FX Lane         │  │   │  │  Guitar FX Lane         │  │
│  │  Transport + Automation │  │   │  │  Transport + Automation │  │
│  └─────────────────────────┘  │   │  └─────────────────────────┘  │
│                               │   │                               │
│  ┌─────────────────────────┐  │   │  ┌─────────────────────────┐  │
│  │  common/core            │  │   │  │  common/core            │  │
│  │    Song/Arrangement     │  │   │  │    Song/Arrangement     │  │
│  └─────────────────────────┘  │   │  │  + Scoring logic        │  │
│                               │   │  └─────────────────────────┘  │
└───────────────────────────────┘   │                               │
                                    │  ┌─────────────────────────┐  │
                                    │  │  Gameplay Systems       │  │
                                    │  │  • Pitch detection      │  │
                                    │  │  • Note matching        │  │
                                    │  │  • Latency calibration  │  │
                                    │  └─────────────────────────┘  │
                                    └───────────────────────────────┘
\endcode

Both executables link scope-level umbrella targets as static libraries. The final shape is
`rock_hero::common + rock_hero::editor` for the editor executable and
`rock_hero::common + rock_hero::game` for the game executable. Static linking avoids singleton
aliasing issues and simplifies deployment.

---

# Threading Model

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

## Rules

- No locks, heap allocation, or file IO on the audio thread.
- Cross-thread communication uses atomics or lock-free queues.
- Mutations that can rebuild processing graphs are message-thread only.

## Thread Communication

\code{.txt}
Audio Thread                Analysis Thread            UI / Game Thread
     │                            │                           │
     │  raw input samples         │                           │
     ├──► [lock-free ring buf] ──►│                           │
     │                            │  pitch/onset results      │
     │                            ├──► [lock-free struct] ───►│
     │  transport position        │                           │
     ├──► [std::atomic] ──────────┼──────────────────────────►│
     │                            │                           │
\endcode

All communication from the audio thread is lock-free. The audio thread's only outputs are the ring buffer (samples), the atomic transport position, and its normal audio output to the speakers. No mutexes, no blocking, no exceptions.

---

# Timing and Latency

The audio thread is the single source of truth for timing. The game view and scoring system derive all state from the transport position — they never maintain independent clocks.

The full latency chain that must be accounted for:

- **ASIO buffer**: 64–256 samples (1.5–5.8ms at 44.1kHz)
- **Plugin chain processing**: usually negligible for guitar effects
- **Render frame delay**: up to 16ms at 60fps
- **Display latency**: monitor-dependent, typically 5–15ms

A **calibration system** measures round-trip latency (player strums, system measures delay). This offset is applied to scoring so note hit detection aligns with what the player perceives, not the raw audio timeline.

All scoring comparisons happen in audio-sample time with calibration offsets applied consistently. Mixing audio-thread time with render-thread time in scoring logic is explicitly prohibited — it produces inconsistent hit windows.

---

# Gameplay Systems

**Pitch detection**: YIN or autocorrelation on the clean pre-effects signal. Combined with onset
detection (detecting *when* the player plays) which is often more reliable than pitch detection for
scoring. Polyphonic detection needed for chords.

**Note matching and scoring**: Configurable tolerance windows for timing and pitch accuracy. Must
handle string bends, hammer-ons, pull-offs, and slides as valid techniques rather than missed notes.

**Latency calibration**: Built into the timing architecture from day one, not bolted on later.

---

# Editor UI

Built with JUCE components — the same framework Waveform (the DAW built on Tracktion Engine) uses for its entire interface.

The editor (`rock-hero-editor`) consists of:

- **Waveform display**: `juce::AudioThumbnail` showing the backing track with a playhead overlay.
- **Plugin chain view**: Horizontal row of loaded effects with add/remove/reorder controls.
- **Automation area**: Shares the waveform's horizontal time axis and zoom/scroll state. Breakpoint envelopes control plugin parameters over the song's duration.
- **Transport controls**: Play, stop, seek.

The waveform and automation area share a single zoom/scroll controller. Coordinate conversion from time-to-pixels uses Tracktion Engine's tempo sequence for beat-accurate positioning.

UI theming is planned as a distinct later phase — functionality first, polish second.

---

# Game View

Built with SDL3 (window management, input) and bgfx (rendering abstraction over Vulkan, Metal, D3D11/D3D12, OpenGL). Lives in `rock-hero`.

The note highway is geometrically simple — textured quads on lanes with perspective projection. bgfx handles this easily with room for glow effects, particles on note hits, and other visual feedback.

SDL is initialized without its own event pump. SDL events are polled manually from JUCE's message loop. The game window gets its own native window handle that bgfx renders into.

---

# VST Plugin Safety

- **Performance monitoring**: Per-buffer processing time tracked for each plugin. Automatic bypass if a plugin consistently overruns.
- **Fallback chains**: Safe-mode effects during gameplay; full plugin freedom in practice/editor mode.
- **Smooth transitions**: Never hard-switch bypass. Always use 5-10ms parameter ramps. Pre-activate plugins (loaded with mix at zero) before they're needed to avoid CPU spikes.
- **Plugin validation**: Basic stability testing at load time.

---

# Licensing

All dependencies are compatible with AGPLv3 at zero cost:

| License | Dependency | Compatibility |
|---------|-----------|--------------|
| GPL3 | Tracktion Engine | Compatible (AGPLv3 is more restrictive, governs combined work) |
| AGPLv3 | JUCE | Requires project to use AGPLv3 |
| MIT (as of October 2025) | VST3 SDK | Permissive, compatible with everything |
| Dual GPL3/proprietary (as of October 2025) | ASIO SDK | GPL3 option compatible |
| MIT | open-psarc | Permissive, compatible with everything |
| zlib | SDL3 | Permissive |
| BSD 2-Clause | bgfx | Permissive |

The project is licensed under **AGPLv3** because JUCE requires it. For a desktop application run locally, AGPLv3 is functionally identical to GPLv3 — the additional network clause only applies to software accessed over a network.

Charging for the software is permitted. Anyone can fork, modify, and redistribute (including selling their own version). Source code must be made available to recipients. If the project ever moves to closed source, commercial licenses for both Tracktion Engine and JUCE would be required.

---

# Development Approach

Build the real architecture from the start but keep visuals simple. Start with rectangles scrolling down the screen, not a polished 3D fretboard. Validate the hard unknowns first.

## Build First: Timing Instrumentation

Before gameplay, add permanent debug logging that records:

- Transport position on each audio callback
- Sample positions written to the ring buffer
- Analysis thread detection results with timestamps
- Transport position read by the game view each frame
- Scoring inputs: position compared, calibration offset applied, result

When gameplay feels "off," these logs reveal whether the issue is transport drift, pitch detection latency, calibration error, or render jitter. Without this data, debugging timing is guesswork.

## Prototyping Priorities

1. Timing instrumentation framework
2. ASIO input through Tracktion Engine with VST plugin chain producing audio
3. Backing track playback synced to transport
4. Lock-free transport position shared with SDL/bgfx window (scrolling rectangles)
5. Pitch detection on guitar input
6. Note matching and scoring with latency calibration
7. Plugin parameter automation (bypass, mix) over song timeline

Visual polish, 3D fretboard, particles, and UI theming come after gameplay fundamentals are proven.

## Testing Infrastructure

Catch2 (v3) is declared in `conanfile.txt` and integrated into the build. Per-library test targets
live alongside each library:

- `rock-hero-common/core/tests/` for shared domain and native song package behavior
- `rock-hero-common/audio/tests/` for shared audio contracts and adapter behavior
- `rock-hero-editor/core/tests/` for headless editor workflow
- `rock-hero-editor/ui/tests/` for focused editor UI helpers and wiring
- `rock-hero-game/core/tests/`, `rock-hero-game/audio/tests/`, and `rock-hero-game/ui/tests/`
  for game-owned behavior as those modules gain real code

Tests are registered with CTest via `catch_discover_tests`. See
\ref design_architectural_principles for the full testing strategy.

---

# Known Risks

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

# Fallback Strategy

If Tracktion Engine proves unsuitable (timing issues, API incompatibilities, debugging friction),
the fallback is **JUCE alone** — building a custom transport, automation system, and full-file
audio playback on JUCE's primitives (`AudioProcessorGraph`, `AudioPluginFormatManager`,
`AudioPlayHead`). This is significant work (several months) but preserves all other architectural
decisions.

The architecture is designed for this: Tracktion Engine is isolated behind
`rock-hero-common/audio`. The game view, pitch detection, scoring, and editor UI depend on a
transport position and a data model, not on Tracktion directly. Either Tracktion or a custom JUCE
implementation can back those interfaces.
