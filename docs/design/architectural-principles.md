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

- unit tests over pure logic in `rock-hero-core` and future pure libraries
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

## Rock Hero Core

`rock-hero-core` should become the largest concentration of behavior in the project.

It should contain:

- song, chart, and arrangement rules
- validation rules
- timing math
- beat/second/sample conversions
- chart transforms
- note matching
- scoring logic
- calibration math
- transport-independent gameplay rules
- editor command logic that does not inherently require JUCE
- data structures used by simulations and fixtures

This code should be:

- framework-free
- synchronous
- deterministic
- easy to construct in tests

If a new feature can live here, it usually should.

## Rock Hero Audio

`rock-hero-audio` should act as an adapter around Tracktion and JUCE audio facilities.

It should contain:

- Tracktion-backed transport implementation
- backing track integration
- plugin-host integration
- waveform backend and proxy generation
- audio-thread plumbing
- lock-free cross-thread infrastructure
- translation between project abstractions and Tracktion/JUCE types

It should **not** become a generic home for gameplay rules, scoring policy, editor behavior, or
other business logic that merely happens to relate to audio.

Its purpose is integration and infrastructure.

## Rock Hero UI

`rock-hero-ui` should be presentation-focused.

It should contain:

- JUCE components
- rendering of already-derived state
- gesture handling
- intent emission to non-UI logic
- local presentation concerns

It should **not** own:

- scoring rules
- chart semantics
- persistence policy
- transport semantics
- business decisions that can be expressed outside JUCE

The right bias is: if a UI component is getting smart, move the intelligence out.

## App Targets

The app targets should mostly:

- choose concrete implementations
- wire dependencies together
- create windows
- start event loops
- bridge platform startup concerns

If core behavior lives only in an app target, it is likely too hard to test.

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
- `IAudioSession`
- `IWaveformSource`
- `IClock`
- `IPitchDetector`
- `IPluginHost`
- `ISongRepository`
- `IScoringInputStream`

Production implementations can use Tracktion, JUCE, real files, or real clocks. Tests can use
fakes or deterministic stubs.

This is preferable to mocking third-party frameworks directly.

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

- `rock-hero-audio` behavior against Tracktion-backed implementations
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
"move behavior into a library that barely knows JUCE exists."

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

- pure unit tests in `rock-hero-core`
- extracted non-UI services and command/state logic
- hand-written fakes and stubs for project-owned interfaces
- adapter tests around `rock-hero-audio`
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

- move scoring and gameplay systems into a dedicated non-app library
- move editor behavior into command-style services outside JUCE components
- keep waveform backend logic in `rock-hero-audio`, but keep drawing and interaction in
  `rock-hero-ui`
- remove broad Tracktion escape hatches in favor of narrow abstractions
- keep serialization policy separate from domain state where practical
- add builders and fixtures for `Song`, `Chart`, `Arrangement`, and note sequences
- keep app targets small and orchestration-focused

# CMake and Test Layout

Use per-library test targets rather than one large test binary.

Suggested shape:

- `libs/rock-hero-core/tests`
- `libs/rock-hero-audio/tests`
- `libs/rock-hero-ui/tests` for non-GUI or narrowly scoped GUI wiring checks
- additional test targets for future pure libraries such as gameplay or editor logic libraries

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

## Build Policy Exception

The source-level rule for `rock-hero-core` is unchanged: core must remain framework-free and must
not include JUCE or Tracktion in its public headers or implementation.

The build graph has one narrow exception. First-party targets link `rock_hero::build_policy`, and
that target currently delegates to JUCE's recommended warning, configuration, and Release LTO helper
targets. This is a pragmatic build-policy choice, not permission for core code to depend on JUCE.
It keeps one compiler policy across Rock Hero while using defaults that already match the
JUCE/Tracktion toolchain surface.

This exception must remain contained in `cmake/RockHeroBuildPolicy.cmake`. Core CMake files should
only mention Rock Hero policy targets such as `rock_hero::build_policy`, never raw `juce::` or
`tracktion::` targets. If a core-only build, package, or test workflow needs to remove the
build-time JUCE dependency later, change the implementation of the build-policy targets in that one
file and keep first-party target call sites intact.

# Decision Rules for New Code

When adding a new class, function, or subsystem, ask:

1. Can this be pure?
2. Can it take data in and return data out?
3. Can its dependencies be replaced with fakes?
4. Can it run without a message loop, audio device, filesystem, or GPU?
5. Does it really need to know about JUCE or Tracktion?
6. Is it mixing policy with side effects?
7. Is it mixing threading concerns with domain rules?

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
