\page design_architectural_principles Architectural Principles

This document complements \ref design_architecture. `Project Architecture` describes the system
shape;
this document defines the engineering principles and structural constraints that keep the codebase
scalable and amenable to automated testing as it grows.

# Purpose

This document defines the architectural direction for Rock Hero with one explicit priority:

**the project must remain highly amenable to automated testing as it grows.**

This is not a secondary quality attribute. It is a structural requirement. The project is expected
to grow into a large codebase with multiple subsystems, complex timing behavior, audio integration,
editor workflows, and game logic. If testability is not designed into the structure early, the
cost of change will rise sharply and confidence in refactors will fall.

The goal is to make most behavior:

- deterministic
- fast to test
- runnable without real hardware
- independent of UI event loops where possible
- independent of real-time threading where possible
- isolated from third-party framework semantics where possible

# Core Position

The best way to make Rock Hero testable is **not** to search for clever ways to test framework
code. It is to make framework-dependent code a relatively small part of the total system.

That means:

- pure logic belongs in pure libraries
- framework integration belongs in thin adapters
- UI code should present state and emit intents, not own business rules
- app targets should compose the system, not contain major logic
- time, threading, IO, and hardware should sit at the boundary

This is the main architectural rule for scalability.

# Design Goal

As the codebase grows, the majority of tests should remain:

- unit tests over pure and headless logic in `rock-hero-common/core`, product `core` libraries,
  and future pure libraries
- adapter tests over project-owned interfaces
- focused integration tests over a few connected components

Only a small minority of tests should require:

- JUCE initialization
- a message loop
- Tracktion Engine runtime state
- a GPU
- an audio device
- plugin scanning
- end-to-end app automation

If too much important behavior can only be tested through the running app, the architecture is too
tightly coupled.

# Library Roles

## Product Scopes

The durable repository structure is organized by product scope:

- `rock-hero-common` contains behavior genuinely shared by the editor and game.
- `rock-hero-editor` contains editor-only workflow, audio policy, presentation, and startup.
- `rock-hero-game` contains game-only gameplay, audio analysis, presentation, and startup.

Common code must not depend on editor or game code. Editor code must not depend on game code. Game
code must not depend on editor code.

## Core Modules

Core modules are the preferred home for headless behavior.

`rock-hero-common/core` has the broadest reuse contract: it owns shared domain and package behavior
used by both products. It should stay headless and automated-testable, but it is not required to be
JUCE-free. It may use narrow `juce_core` utility facilities when that avoids duplicate
infrastructure and keeps the behavior testable without windows, audio devices, GPUs, Tracktion
runtime state, or the full application shell. Examples include `juce::File`, `juce::String`,
`juce::JSON`, `juce::ZipFile`, and `juce::Result` translated into project-owned errors.

Product core modules follow the same testability contract rather than a blanket framework ban. They
should prefer pure standard C++ for rules, math, state transitions, and package-independent policy,
but they may use narrow JUCE utility facilities when that keeps the product workflow simpler and
still leaves the behavior automated-testable. Examples include `juce::File`, `juce::String`,
`juce::PropertiesFile`, `juce::ValueTree`, and `juce::UndoManager`.

They should contain:

- shared song, chart, arrangement, validation, timing, and package rules in `common/core`
- editor workflow, command policy, session state, and undo/redo policy in `editor/core`
- note matching, scoring, combo/streak rules, calibration math, and simulation in `game/core`

Product core code should remain synchronous where practical, deterministic where the behavior
allows it, and easy to construct in tests. It should not own UI widgets, drawing, message-loop
lifecycle, audio devices, GPU resources, app startup, or Tracktion runtime integration unless a
specific design decision justifies the exception.

If a new feature can live in a core module without pulling in framework dependencies, that is still
usually preferable. If using a small JUCE facility removes custom infrastructure and keeps the
tests straightforward, core code may use it.

## Audio Modules

Audio modules own audio contracts, integration, infrastructure, and audio-adjacent policy.

`rock-hero-common/audio` owns shared audio ports and the default Tracktion/JUCE implementation.
Public headers expose project-owned contracts such as transport, edit, thumbnail, playback ports,
and the composition-facing engine type. Tracktion headers stay private to implementation files and
private implementation headers.

`rock-hero-editor/audio` is for editor-specific audio behavior outside the shared engine.
`rock-hero-game/audio` is for game-specific audio analysis and gameplay plumbing such as pitch
detection, onset detection, and calibration capture.

Audio modules should not become generic homes for gameplay rules, scoring policy, editor workflow,
or other business logic that merely happens to relate to audio.

## UI Modules

UI modules are presentation-focused.

They should contain:

- concrete components and views
- rendering of already-derived state
- gesture handling
- intent emission to non-UI logic
- local presentation concerns

`rock-hero-common/ui` is only for UI used by both products. `rock-hero-editor/ui` owns
editor-specific JUCE presentation. `rock-hero-game/ui` owns game-specific presentation and
rendering.

UI modules should not own scoring rules, chart semantics, persistence policy, transport semantics,
or business decisions that can be expressed outside presentation code. The right bias is: if a UI
component is getting smart, move the intelligence out.

UI modules may contain thin feature composition wrappers when the wrapper's job is to pair a view
with its product-core controller and expose one ready-to-host component. Treat those wrappers as
watch items: if they start constructing concrete adapters, owning settings or persistence, choosing
application callbacks, restoring sessions, starting event loops, or otherwise making
application-shell decisions, move concrete composition to the matching `app/` folder or move
headless policy back to product core.

## App Folders

The `app/` folders should mostly:

- choose concrete implementations
- wire dependencies together
- create windows
- start event loops
- bridge platform startup concerns

If core behavior lives only in an app folder, it is likely too hard to test.

# The Real Strategy: Architect for Testability

There are many testing techniques, but the highest-leverage strategy is structural:

1. Put logic in pure code.
2. Keep framework integration thin.
3. Introduce project-owned seams around unstable dependencies.
4. Keep time, concurrency, and side effects at the edges.
5. Simulate behavior headlessly wherever possible.

This matters more than the specific test framework.

# Ports and Adapters

The project should define small, project-owned interfaces at important boundaries.

Examples:

- `ITransport`
- `IAudio`
- `IEdit`
- `IWaveformSource`
- `IClock`
- `IPitchDetector`
- `IPluginHost`
- `ISongRepository`
- `IScoringInputStream`

Production implementations can use Tracktion, JUCE, real files, or real clocks. Tests can use
fakes or deterministic stubs.

This is preferable to mocking third-party frameworks directly.

## Typed Boundary Errors

Recoverable failures that cross a project-owned API boundary should use a domain-owned typed error
value rather than raw string text. The boundary owns the vocabulary for its failure domain and
converts lower-level library, framework, or filesystem failures into that vocabulary.

This keeps callers testable and branchable without parsing display text, while still allowing UI
and logs to use the error message carried by the domain error value.

## Why This Matters

Mocking JUCE or Tracktion directly is the wrong abstraction level:

- it couples tests to framework call patterns
- it encourages wrapper layers with large maintenance surface area
- it verifies interactions with mocks rather than real project behavior

The project should mock or fake **its own contracts**, not attempt to mock the frameworks
themselves.

# Preferred Kinds of Tests

## 1. Pure Unit Tests

These should dominate the suite.

Examples:

- chart validation
- note-event ordering
- timing window checks
- latency compensation
- score computation
- transport coordinate mapping
- editor command application
- serialization rules that do not require framework runtime

These tests should be:

- fast
- deterministic
- easy to diagnose
- cheap to write in large quantities

## 2. Adapter Tests

These validate project-owned adapters around frameworks and infrastructure.

Examples:

- `rock-hero-common/audio` behavior against Tracktion-backed implementations
- waveform source/proxy handling
- serialization adapters
- transport adapter behavior with fakes at project-owned boundaries

The goal is to test your integration code without dragging the whole app into the test.

## 3. Focused Integration Tests

These connect a few components together where the interaction matters.

Examples:

- score engine + transport snapshots + chart fixtures
- editor command service + repository fake
- waveform coordinate logic + transport state/position + presentation model

These should remain relatively small and intentional.

## 4. Selective UI Wiring Tests

Direct JUCE component tests have value, but only in a narrow role:

- validate synchronous callback wiring
- validate simple state propagation
- validate layout calculations where useful

They should not be treated as the core of the testing strategy.

## 5. Snapshot or Approval Tests

Use these sparingly for stable outputs:

- waveform rendering regressions
- stable visual components
- possibly rendered audio-output comparisons in narrowly defined cases

They are regression detectors, not proof of correctness.

## 6. End-to-End Tests

These are useful later for stable user flows, not as a primary means of validating logic.

They are slower, more fragile, and more expensive to maintain.

# Anti-Goals

The project should explicitly avoid these patterns as primary strategy:

- heavy reliance on UI tests for logic validation
- mocking JUCE widget hierarchies
- mocking `juce::Graphics`, `juce::Component`, or `juce::Timer` as a broad pattern
- placing major logic in app targets
- placing business rules in audio-thread callbacks
- encoding test seams through large wrapper layers over third-party frameworks

These patterns scale poorly.

# Humble Object, But With the Right Scope

The Humble Object pattern is useful, but it should be applied with the right ambition.

The small version is:

- extract a calculation or state machine out of a component

That is good.

The stronger version is:

- move substantial editor or gameplay behavior into non-UI services and models

That is better.

For Rock Hero, the strongest path is usually not just "make the JUCE component thinner." It is
"move behavior into a headless library where JUCE use is deliberate, narrow, and easy to test."

# Time Must Be a Dependency

Timing is central to this project. Therefore, time must be explicit.

Core logic should not directly read:

- wall clock time
- frame time
- JUCE timers
- message-loop timing
- implicit render cadence

Instead, inject:

- transport position
- frame delta
- sample rate
- block size
- calibration offset
- simulated timestamps

This makes timing-sensitive behavior reproducible in tests.

# Keep Threading at the Boundary

Audio software often becomes hard to test because concurrency spreads everywhere.

That should be avoided.

Threading concerns should stay in infrastructure code:

- lock-free queues
- atomics
- callback ownership
- message-thread marshalling
- audio-thread handoff logic

Domain logic should receive plain snapshots, events, or immutable inputs.

The core should not care whether input came from:

- the audio thread
- a timer
- a replay file
- a test harness

If it does care, the boundary is in the wrong place.

# Separate State From Side Effects

One of the strongest patterns for testability is:

- explicit input
- explicit state
- explicit output
- explicit requested side effects

For example, a logic function might produce:

- updated editor state
- a `SeekTransport` intent
- a `PersistSong` intent
- a `RefreshWaveform` intent

rather than directly mutating the audio engine, filesystem, and UI in one method.

This makes behavior observable in tests without needing the real subsystems.

# Add a Replayable Simulation Layer

For Rock Hero specifically, a replayable simulation layer should be treated as a first-class
architectural objective.

This layer should be able to run headlessly with:

- a `Song` or `Arrangement`
- synthetic transport positions
- synthetic pitch and onset detections
- calibration offsets
- expected player inputs

And verify:

- hit and miss decisions
- score evolution
- combo and streak behavior
- timing edge cases
- calibration correctness

This is likely to become one of the most valuable test assets in the project.

It gives confidence in gameplay behavior without:

- real guitar input
- real audio devices
- real plugin chains
- real rendering

# Recommended Test Pyramid for Rock Hero

The intended test distribution is:

- many pure unit tests
- some adapter tests
- fewer focused integration tests
- very few UI tests
- very few end-to-end tests

This is not accidental. It is the scaling model.

As the project grows, the number of tests should grow mostly in the lower layers, not in the
highest-friction layers.

# Testing Techniques: What to Prefer

## Strongly Preferred

- pure or headless unit tests in `common/core`, `editor/core`, and `game/core`
- extracted non-UI services and command/state logic
- hand-written fakes and stubs for project-owned interfaces
- adapter tests around `rock-hero-common/audio`
- deterministic fixtures and builders
- simulation-style tests for gameplay and timing

## Selectively Useful

- direct JUCE component tests for synchronous wiring
- snapshot tests for stable visual output
- end-to-end tests once the product has mature flows

## Generally Not Recommended

- broad JUCE wrapper/mocking layers
- interaction-heavy mock tests as the default
- verifying implementation details instead of observable behavior

# On Mocks

Mocks are not forbidden, but they should not drive the architecture.

The project should prefer:

- pure logic tests first
- fakes and stubs for project-owned interfaces second
- mocks only where interaction semantics are truly the behavior under test

Examples where a mock may be justified:

- retry behavior
- failure escalation
- ordering constraints
- backoff policy
- ensuring a dangerous external action is not triggered

Examples where a mock is usually the wrong tool:

- testing chart rules
- testing timing math
- testing score computation
- testing UI calculations
- testing domain state transitions

The project should resist the trap of creating interfaces only to satisfy mocking tools.

# Concrete Structural Recommendations

As the codebase evolves, favor these moves:

- move scoring and gameplay systems into `rock-hero-game/core`
- move editor behavior into `rock-hero-editor/core` command-style services outside JUCE
  components
- keep waveform backend logic in `rock-hero-common/audio`, but keep drawing and interaction in
  `rock-hero-editor/ui`
- remove broad Tracktion escape hatches in favor of narrow abstractions
- keep serialization policy separate from domain state where practical
- add builders and fixtures for `Song`, `Arrangement`, and note sequences
- keep app targets small and orchestration-focused

# CMake and Test Layout

Use per-library test targets rather than one large test binary.

Suggested shape:

- `rock-hero-common/core/tests`
- `rock-hero-common/audio/tests`
- `rock-hero-editor/core/tests`
- `rock-hero-editor/ui/tests` for non-GUI or narrowly scoped GUI wiring checks
- `rock-hero-game/core/tests`, `rock-hero-game/audio/tests`, and `rock-hero-game/ui/tests`

Register them with `ctest`, but keep most of them free from hardware and windowing requirements.

## Third-Party Module Linkage

Rock Hero does not link raw `juce::juce_*` or `tracktion::tracktion_*` module targets directly
from its libraries and apps. Instead, the repository defines project-owned static wrapper targets
for the JUCE and Tracktion modules it uses. Those wrappers privately link the raw third-party
module targets, then forward the required compile definitions and include paths to consumers.

This project therefore treats raw JUCE and Tracktion module linkage as an internal build concern
behind a project-owned wrapper layer:

- Rock Hero targets link wrapper aliases such as `rock_hero::juce_gui_basics` and
  `rock_hero::tracktion_engine` rather than raw third-party modules.
- project-owned interfaces and adapters are exported, not raw JUCE or Tracktion module targets.

This keeps the dependency graph explicit while avoiding repeated third-party module compilation
across the project's modular target structure.

## Core JUCE Utility Use

The source-level rule for `rock-hero-common/core` is now testability-first rather than JUCE-free.
Common core may include and link narrow `juce_core` utilities for package, file, JSON, ZIP, string,
and result-handling behavior when doing so keeps the project simpler. This is not permission to
move UI, message-loop ownership, audio-device ownership, GPU behavior, app startup, plugin
scanning, or Tracktion runtime integration into common core.

`rock-hero-common/core` and other first-party targets still link project-owned wrapper aliases,
such as `rock_hero::juce_core`, rather than raw `juce::` module targets. Tracktion module targets
remain behind the audio adapter layer. Recoverable errors crossing project-owned APIs should still
be translated into domain-owned error values rather than leaking raw framework diagnostics as the
branchable contract.

# Decision Rules for New Code

When adding a new class, function, or subsystem, ask:

1. Can this be pure?
2. Can it take data in and return data out?
3. Can its dependencies be replaced with fakes?
4. Can it run without a message loop, audio device, filesystem, or GPU?
5. Does it need JUCE utility behavior, and can that stay headless and easy to test?
6. Does it touch Tracktion or runtime framework behavior that belongs in an adapter?
7. Is it mixing policy with side effects?
8. Is it mixing threading concerns with domain rules?

If the answer points away from purity or replaceability, treat that as a design warning and justify
it explicitly.

# Strategic Summary

Rock Hero should be designed so that automated testability scales with project size rather than
collapsing under it.

That requires:

- pure libraries for rules and decisions
- thin adapters around Tracktion, JUCE, and other frameworks
- presentation-focused UI
- app targets as composition roots
- explicit treatment of time, threading, and side effects
- replayable headless simulation for gameplay and timing

This is the architecture that best supports:

- large-scale refactoring
- rapid feature development
- long-lived maintainability
- high confidence in behavior changes
- sustainable growth of the codebase

If the project follows this structure consistently, automated tests will remain an asset as the
system becomes more complex, instead of becoming a brittle burden.
