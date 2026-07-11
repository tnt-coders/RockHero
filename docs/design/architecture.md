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
| Rock Hero package formats | JUCE JSON + ZIP utilities | `.rhp` editor project packages; `.rock` native song packages |
| Game rendering | SDL3 + bgfx | 3D note highway, visual feedback |
| Editor UI | JUCE Components | Waveform display, automation curves, plugin management |
| License | AGPLv3 | Compatible with all dependencies at zero cost |

`.rhp` packages store `project.json` at the root and song content under `song/`. `.rock`
packages store native song content directly at the archive root and can be imported by the
editor as unsaved projects.

Backing audio inside both package formats is canonically **FLAC**, and the package reader enforces
it: a package whose audio asset is WAV, Ogg, or any other format is rejected and must be
re-imported. FLAC is lossless, smaller than uncompressed WAV, and — unlike lossy formats — decoded
to identical samples by both the playback engine and the waveform thumbnail, so the drawn waveform
cannot drift out of alignment with what is heard. Anything that enters the editor in another format
(a Guitar Pro import's embedded MP3/Ogg/WAV, an external audio file) is transcoded to FLAC on the
way in by `rock-hero-common/audio`'s `transcodeToFlac`, which matches the source's bit depth
(16-bit sources stay 16-bit; higher-resolution sources clamp to FLAC's 24-bit ceiling) so lossy
audio is not padded out; a source that is already FLAC is copied verbatim.

---

# Repository Layout

\code{.txt}
RockHero/
  rock-hero-common/
    core/               - shared headless domain and package behavior
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
- `rock-hero-common/core` may use narrow `juce_core` utility facilities for package, file,
  string, JSON, and ZIP behavior when that keeps project infrastructure simple and headless.
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

### JUCE utility dependency in core modules

`rock-hero-common/core` is a headless shared module, not a framework-free island. JUCE is the
project's foundational application framework, so common core may use narrow `juce_core` facilities
where they reduce duplicate infrastructure and still preserve automated testability. Current
examples include JSON parsing/serialization, ZIP archive reading/writing, files, strings, and
typed framework results that are immediately translated into project-owned errors.

This permission is intentionally narrow:

- Common core links project-owned wrapper aliases such as `rock_hero::juce_core`, not raw
  `juce::juce_*` module targets.
- Common core keeps public domain boundaries project-owned unless a JUCE type is the pragmatic
  contract for the feature.
- Common core must not own UI widgets, drawing, message-loop lifecycle, audio devices, GPU
  resources, app startup, plugin scanning, or Tracktion runtime integration.
- Tracktion remains isolated to `rock-hero-common/audio` implementation files and private
  implementation headers.

Product core modules follow the same testability rule. `rock-hero-editor/core` and
`rock-hero-game/core` should remain headless and automated-testable, but they may use narrow JUCE
utility facilities such as files, strings, settings, value trees, or undo support when that keeps
workflow code simpler and the behavior remains testable without windows, audio devices, GPUs, or
the full application shell. UI widgets, drawing, message-loop ownership, device ownership, and
Tracktion runtime integration still belong outside product core unless a specific design decision
says otherwise.

## Include-path convention

Each library exposes its public headers through a **PUBLIC** include directory at
`rock-hero-<scope>/<module>/include/`. External consumers - other libraries, apps, and tests -
always reference first-party headers through the full nested path:

\code{.cpp}
#include <rock_hero/common/audio/engine/engine.h>
#include <rock_hero/common/core/song/song.h>
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
| `"engine_impl.h"`, `"tracktion/plugin_window.h"` | A **private `src/` header** of the same module, reached through the module's PRIVATE `src` include directory. Always feature-qualified when it lives in a feature folder. No angle form exists for these because they are on no consumer include path. |
| `<rock_hero/<scope>/<module>/foo.h>` | Any other first-party **public** header, including sibling headers in the *same* module. Keeps cross-file references uniform regardless of where they are read from. |
| `<tracktion_engine/...>`, `<juce_*/...>`, `<catch2/...>`, `<BinaryData.h>`, `<JuceHeader.h>` | Third-party. |
| `<atomic>`, `<algorithm>`, `<filesystem>` | C++ standard library (angle-bracket, no path separator, no extension). |

Example from a product-scope library source file:

\code{.cpp}
#include "engine/engine.h"

#include <rock_hero/common/audio/song/i_thumbnail.h>

#include <tracktion_engine/tracktion_engine.h>

#include <atomic>
\endcode

The four blocks — main, other first-party, third-party, standard library — are produced
automatically by clang-format's `IncludeBlocks: Regroup` using the `IncludeCategories`
defined in `.clang-format`.

## Feature-folder navigation

Inside each library, public headers and sources are grouped by feature folder
(`include/rock_hero/editor/core/timeline/...`, `src/timeline/...`). Feature folders are
navigation only: they add a path segment to includes but never a namespace segment, and CMake
targets are unchanged. Namespaces keep mirroring the product/library grid. The placement rules —
when a feature earns a folder, what stays at a library root, and how shared helpers are admitted —
live in \ref design_architectural_principles.

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

**Guitar input/effects lane**: Receives real-time guitar through ASIO and routes it through VST
plugins. Automation envelopes drive plugin parameters over the arrangement duration: bypass,
wet/dry mix, wah position, and any other exposed parameter. Mid-song tone changes are achieved by
loading multiple plugins and automating their bypass and mix parameters, with short ramps
(5-10ms) to avoid clicks. Tracktion Engine's `RackType` system is available for more complex
routing needs.

The lanes in the displayed arrangement share a single transport, tempo map, and time signature
sequence managed by Tracktion Engine's `Edit` data model.

---

# Song Data Model

The `rock-hero-common/core` library owns shared persistent song data. It remains headless and keeps
format parsing/package details private to the module.

\code{.txt}
Song
  metadata          (title, artist, album, year)
  tempo_map         (song-level beat grid: time-signature changes + sparse beat anchors; the only
                     place absolute seconds are stored — see the timing note below)
  arrangements[*]
    id                (stable project-local arrangement identifier)
    part              (Lead | Rhythm | Bass)
    difficulty        (0 Unknown, 1-10; not persisted yet)
    audio_asset        (required path/identifier for backing audio)
    audio_duration     (full natural duration of the audio asset)
    tones[*]           (named tone catalog; each tone's document path derives from its UUID as
                        tones/<uuid>/tone.json and is interpreted exclusively by common/audio)
    tone_track         (gap-free tone regions referencing catalog tones by document ref)
\endcode

Arrangement difficulty is intended to be a value *derived* from playable chart data, not authored
data a user sets by hand. The display tier is derived from the numeric rating: Easy for 1-2, Medium
for 3-4, Hard for 5-6, Expert for 7-8, and Master for 9-10. A value of 0 represents Unknown so
draft/default arrangements do not imply a fake difficulty; validation for playable songs should
reject Unknown once playable chart data exists. Difficulty is currently not persisted in song
packages and defaults to Unknown on load. See
`docs/roadmap/11-derived-difficulty-calculator.md`.

The tempo map is a **warp-anchor grid**: time signatures are stored as changes (carried forward),
and time is pinned only on a sparse set of addressed **anchors** (a measure/beat with an absolute
second). A start anchor (measure 1, beat 1) and a terminal anchor at the one-past-content downbeat
are always required; every other beat and measure interpolates, so absolute seconds appear only in
anchors. Interpolation is linear in **quarter-note (metronome) time**, not in beats: the
quarter-note tempo is constant inside each anchor span and changes only at anchors, so a
meter-denominator change between anchors (say 4/4 to 7/8) re-slices beat durations — the
eighth-note beats run twice as fast — instead of stretching each beat to the old length. Anchor
positions are persisted as on-beat tokens such as `"1:1"` and `"17:1"`.

The persisted tempo-map shape is:

\code{.json}
{
  "tempoMap": {
    "timeSignatures": [
      { "measure": 1,  "numerator": 4, "denominator": 4 },
      { "measure": 17, "numerator": 7, "denominator": 8 },
      { "measure": 25, "numerator": 4, "denominator": 4 }
    ],
    "anchors": [
      { "position": "1:1",  "seconds": 1.840 },
      { "position": "17:1", "seconds": 28.057 },
      { "position": "33:1", "seconds": 48.500 }
    ]
  }
}
\endcode

Anchor seconds - the only absolute time stored by the tempo map - keep a fixed three-decimal
(millisecond) grid, with at most +/-0.5 ms quantization error. That is below the onset-detection /
latency / hit-window floor for the charting and scoring work planned here; higher precision is
intentionally avoided, since fuzzy note onsets do not carry it.

Playable note, chord, tuning, technique, and synthesized-instrument storage is intentionally
deferred. Those decisions should be added when note display, gameplay, or import/export work needs a
real chart model. The current tone-system slice needs only the settled tempo map plus arrangement
audio and tone-document references.

`Song` is the persistence root. The editor session projects the song's arrangements into a
headless `Session` and displays one arrangement at a time. Native song package loading validates
archive structure, safe asset paths, and required arrangement audio references. Before a parsed song
is committed to the editor session, the editor workflow validates every arrangement's audio through
the `rock-hero-common/audio` boundary and rejects the project if any asset is unreadable or reports a
non-positive duration. When an arrangement is selected, the application passes its `audio_asset` to
`rock-hero-common/audio` for playback and waveform generation, and loads the tones its tone track
references into `rock-hero-common/audio` by their package-relative tone document references.
`rock-hero-common/core` validates and persists those references but never interprets the referenced
tone data - that belongs entirely to `rock-hero-common/audio`.

The editor-facing song-audio boundary is `common::audio::ISongAudio`: it prepares loaded songs by
validating arrangement audio and filling accepted durations, makes the selected arrangement active
in the playback backend, reports typed preparation and activation failures, and clears the active
arrangement when the project closes.
There is no shared-audio edit-command port today. If future *audio-model* edit commands become
real, add a dedicated boundary for them then; do not overload song loading, audio preparation,
transport, or playback setup ports with edit-command responsibilities. The editor's current
undo/redo edit-command interface lives in `rock-hero-editor/core`, because undo entries also restore
editor-only visual state (block placement, display-type overrides) that `rock-hero-common/audio`
must not depend on (see `docs/completed/editor-undo/editor-undo-plan.md`).

Audio-device and settings boundaries report recoverable side-effect failures with typed errors.
`common::audio::IAudioDeviceConfiguration` owns serialized audio-device restore diagnostics,
`common::audio::IAudioDeviceSettings` owns staged audio-settings operation failures, and
`rock-hero-editor/core` owns app-local editor settings persistence errors. These boundaries keep
stable error codes available until final UI or logging code renders a message.

The plugin-host boundary is `common::audio::IPluginHost`. It scans plugin catalog locations into
project-owned candidate descriptions, then inserts a selected candidate into the instrument chain
without exposing Tracktion or JUCE plugin descriptions through public application code. The first
implementation mutates the linear Tracktion plugin list on the instrument track and caps authored
chains at `common::audio::g_max_signal_chain_plugins` user plugins so project loads stay bounded.
Longer term tone graphs can add richer addressing for racks, containers, and parallel blended
chains while keeping plugin discovery and mutation behind the same audio adapter boundary.

The editor presents signal-chain operations in a bottom control panel separate from the
scrollable arrangement viewport. The first panel is intentionally minimal: it shows the current
linear runtime plugin chain and opens a scanned VST3 browser for inserting plugins into that chain
until the shared product cap is reached. Future rack, container, and parallel blend editing should
evolve this panel's state model rather than placing plugin controls directly into arrangement track
rows.

---

# Application Responsibilities

## Rock Hero Editor

Hosts tone design and chart authoring in one process. Loads and mutates `Song`/`Arrangement`
while auditioning playback. User plugin-chain edits are allowed during audition playback; broader
session and arrangement lifecycle operations may still fence transport while replacing the active
playback graph. Tone and chart decisions are kept in one executable by design — they are too tightly
coupled to author in separate tools.

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
│  │  • Signal chain panel   │  │   │  │  • Score display        │  │
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
`rock_hero::common + rock_hero::game` for the game executable.

Static linking is a reasoned decision, not a default. JUCE is built on process-global singletons
(`MessageManager`, `Desktop`, the `DeletedAtShutdown` registry) that misbehave across DLL
boundaries, Tracktion carries no export annotations, and Windows DLLs would force either
export-macro noise on every public class or `WINDOWS_EXPORT_ALL_SYMBOLS`, which covers neither
global data nor vftables and caps at 65,535 symbols — below what JUCE plus Tracktion emit.
Meanwhile the benefits are absent at this scale: executables link in seconds, and the product's
dynamic extension boundary already exists as VST3 plugin loading behind `IPluginHost`. Revisit
only if executable link time materially hurts iteration, first-party code must load at runtime
beyond VST plugins, or a second process needs to share live engine state.

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

**UI thread** (main thread): Handles window repaints, mouse interaction, transport controls, and
reads pitch detection results for scoring and visual feedback. In the editor, JUCE owns this
loop. In the game (gate outcome "L2"), SDL owns the frame loop on the main thread, which is also
JUCE's message thread; each frame polls SDL events, drains JUCE's pending message queue with a
bounded dispatch loop, and submits the bgfx frame.

**Render thread** (optional): bgfx can submit GPU work separately if needed. For the note highway's
geometric simplicity, single-threaded rendering from the UI thread is likely sufficient.

**Logging backend thread** (quill, both executables): the shared logger's asynchronous writer.
Producers on any thread — including the audio thread through the realtime handle — enqueue
without blocking; only this thread formats and performs file IO. It is infrastructure owned by
the logging facade; no product code schedules work on it.

The analysis thread does not exist yet — it arrives with note detection (plan 22). The game
frame loop reads song time exclusively through the playback-clock port's published snapshots
(plan 12); it never derives time from wall clock or frame counts.

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
- **Signal chain panel**: Bottom control area for loaded plugins, add/remove/reorder controls,
  and future routing edits.
- **Automation area**: Shares the waveform's horizontal time axis and zoom/scroll state. Breakpoint envelopes control plugin parameters over the song's duration.
- **Transport controls**: Play, stop, seek.

The waveform and automation area share a single zoom/scroll controller. Coordinate conversion from
time-to-pixels is pure editor-core math over the song's RockHero tempo map (timeline geometry plus
the tempo-grid scan), not Tracktion's tempo sequence; the visible-span grid is scanned once per
view change and the one result is shared by the ruler and the track content.

UI theming is planned as a distinct later phase — functionality first, polish second.

---

# Game View

Built with SDL3 (window management, input) and bgfx (rendering abstraction over Vulkan, Metal,
D3D11/D3D12, OpenGL; Direct3D 11 is the proven default backend on Windows). Lives in
`rock-hero-game`. This stack was spike-proven and compared against alternatives at the
G20-RENDER gate (2026-07-10); the evidence and decision record live in
`docs/roadmap/20-game-architecture-and-render-stack.md` § Gate record.

The note highway is geometrically simple — textured quads on lanes with perspective projection. bgfx handles this easily with room for glow effects, particles on note hits, and other visual feedback.

Loop ownership (gate outcome "L2"): SDL owns `main()` and the frame loop. JUCE is initialized
without `START_JUCE_APPLICATION`; the first `MessageManager::getInstance()` call binds the main
thread as the message thread, and the frame loop drains JUCE's pending message queue once per
frame with a bounded dispatch loop. Audio playback is unaffected by pump cadence — the audio
thread is self-contained and Tracktion's message-thread work is coarse state sync — so queued
JUCE callbacks simply wait at most one frame. The game window gets its own native window handle
that bgfx renders into; bgfx runs single-threaded on the main thread (renderFrame-before-init),
with its native render-thread split available as a recorded escalation. With no
JUCEApplication instance, WM_QUIT handling belongs to the game shell.

The editor's 3D preview renders through bgfx into a native child window hosted inside a JUCE
window (spike-proven), consuming the same headless highway scene model from
`rock-hero-common/core`; bgfx never enters common's dependency surface.

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
- `rock-hero-common/ui/tests/` for shared presentation data and the render stack's headless
  paths (string palette, highway atlas arithmetic, the Noop render-device suite)
- `rock-hero-editor/core/tests/` for headless editor workflow
- `rock-hero-editor/ui/tests/` for focused editor UI helpers and wiring
- `rock-hero-game/core/tests/` for game-owned headless behavior; `rock-hero-game/audio/tests/`
  and `rock-hero-game/ui/tests/` are reserved homes as those modules gain testable code

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
