# Testable Architecture Recommendations

## Goal

Structure Rock Hero so that most behavior can be validated with fast, deterministic automated tests
 that do not require:

- a real audio device
- a JUCE message loop
- Tracktion Engine runtime state
- a GPU or game window
- filesystem-heavy integration setup

The highest-leverage design decision is to keep the majority of the project's logic outside the app
targets and outside framework-dependent layers.

## Recommended Architectural Direction

Treat the applications as thin composition roots over small, focused libraries.

The design target should be:

- `rock-hero-core` contains pure domain logic and data
- `rock-hero-audio-engine` contains Tracktion-backed engine integration and infrastructure adapters
- `rock-hero-ui` contains presentation and input translation
- app targets mostly wire dependencies and start loops

The practical rule is simple: if code does not need JUCE, Tracktion, bgfx, threads, clocks, or IO,
it should not live in a framework-dependent layer.

## Library Responsibilities

### `rock-hero-core`

This should become the largest source of project behavior over time.

Put the following here whenever possible:

- song model and chart model rules
- chart editing rules and validation
- timing math
- beat/second/sample conversions
- note matching
- scoring rules
- latency compensation
- calibration math
- timeline mapping
- pure command logic for editor and gameplay systems

This code should be deterministic, synchronous, and cheap to construct in tests.

### `rock-hero-audio-engine`

This layer should act primarily as an adapter around Tracktion and JUCE audio facilities.

Put the following here:

- transport implementation
- backing-track playback integration
- plugin-host integration
- waveform backend/proxy generation
- audio-thread infrastructure
- lock-free handoff mechanisms
- Tracktion/JUCE implementation details

Avoid placing game rules or editor business logic here unless they are inherently engine-specific.

### `rock-hero-ui`

This layer should focus on presentation.

Put the following here:

- widgets and views
- rendering of already-derived state
- conversion of user gestures into commands or intents

Avoid placing domain rules in UI classes. UI should not own scoring logic, chart validation,
transport semantics, or persistence policy.

### App Targets

The app executables should mostly:

- compose services
- choose concrete implementations
- create windows
- start event loops

If a feature's logic only exists in an app target, it is likely harder to test than it needs to be.

## Use Ports and Adapters

Define small project-owned interfaces for unstable or hard-to-test dependencies.

Examples:

- `ITransport`
- `IAudioSession`
- `IWaveformSource`
- `IClock`
- `IPitchDetector`
- `IPluginHost`
- `ISongRepository`

Production code can implement these using Tracktion, JUCE, real files, and real clocks. Tests can
use fakes or deterministic stubs.

This matters more than PIMPL. PIMPL hides implementation details from headers. Ports and adapters
make behavior replaceable in tests.

## Prefer Deterministic Units

The easiest code to test is pure and synchronous.

Actively design for units like:

- score evaluation given note events and detected input
- hit window comparisons
- arrangement validation
- latency and calibration adjustment
- transport-to-render coordinate mapping
- editor command application
- undo/redo transitions

These should be testable with plain data and without runtime frameworks.

## Separate State From Side Effects

Whenever practical, business logic should accept explicit state and inputs, then produce:

- updated state
- derived outputs
- requested side effects or intents

For example, editor or game logic should prefer returning commands such as:

- `SeekTransport`
- `StartPlayback`
- `PersistSong`
- `RefreshWaveform`

rather than directly calling framework APIs inside the same logic unit.

This allows tests to assert on behavior without needing a live engine or UI.

## Treat Time as a Dependency

Do not let core logic read wall-clock time or frame timing directly.

Inject time-related values instead:

- current transport position
- frame delta
- calibration offset
- sample rate
- block size

When time is injected, timing-sensitive systems become reproducible under test.

## Keep Threading at the Boundary

Do not spread concurrency concerns through domain logic.

Keep these concerns in infrastructure layers:

- atomics
- lock-free queues
- thread ownership
- callback wiring
- message-thread marshalling

Convert cross-thread activity into plain events, snapshots, or data transfers before it reaches
core logic.

The core should not care whether data came from the audio thread, a timer callback, or a replay.

## Add a Replayable Simulation Layer

For Rock Hero specifically, a headless simulation layer would provide strong testing leverage.

It should be able to run:

- a `Song` or `Arrangement`
- synthetic transport positions
- synthetic pitch or onset detections
- calibration offsets

And verify:

- hit and miss decisions
- score evolution
- combo and streak behavior
- timing edge cases
- latency compensation behavior

This gives high-confidence gameplay tests without depending on live audio input.

## Recommended Test Pyramid

Most tests should be in pure libraries and should run quickly.

- unit tests for `rock-hero-core`
- adapter tests for `rock-hero-audio-engine`
- focused integration tests across a few libraries
- very few end-to-end tests with real devices, plugins, or UI

If a behavior can only be validated through a full app run, the design is likely too coupled.

## Practical Refactoring Recommendations

To align the current project with a more testable structure:

- move scoring and gameplay systems into a dedicated non-app library
- move editor business logic into command-style services outside JUCE widgets
- keep waveform backend logic in `rock-hero-audio-engine`, but keep rendering and interaction in
  `rock-hero-ui`
- replace Tracktion escape hatches with narrow abstractions
- keep serialization concerns separate from core model behavior where practical
- add fixture builders for `Song`, `Chart`, `Arrangement`, and note sequences to make tests cheap
  to write

## CMake and Test Layout

Prefer per-library test targets over one large future test target.

Suggested layout:

- `libs/rock-hero-core/tests`
- `libs/rock-hero-audio-engine/tests`
- `libs/rock-hero-ui/tests` for non-GUI presentation logic only

Register them with `ctest`, but ensure most tests do not depend on audio devices, plugins, or
windowing environments.

## Decision Rule for New Code

When adding a new class or subsystem, ask:

- Can this be pure?
- Can it take data in and return data out?
- Can its dependencies be faked?
- Can it run without a message loop, audio device, filesystem, or GPU?

If yes, it belongs in a test-friendly core library.

If no, it likely belongs at the edge of the system, and the edge should remain as thin as
possible.

## Summary

The structure that maximizes automated testability is not primarily about adding tests later. It is
about placing logic where tests are naturally cheap:

- domain behavior in pure libraries
- framework code in thin adapters
- UI as presentation
- apps as composition roots

That architecture makes tests faster to write, faster to run, and more trustworthy.
