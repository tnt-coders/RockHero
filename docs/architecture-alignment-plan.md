# Architecture Alignment Plan

## Purpose

This document defines a concrete plan to bring the current Rock Hero codebase into closer alignment
with:

- `docs/design/architecture.md`
- `docs/design/architectural-principles.md`
- `docs/design/documentation-style.md`

The plan is intentionally incremental. The goal is not to rewrite the project at once, but to move
the codebase toward a structure that scales well, keeps third-party dependencies contained, and
maximizes automated testability.

## Current State Summary

The repository already reflects several good architectural instincts:

- `rock-hero-core` is standard C++ only
- Tracktion-heavy implementation is mostly isolated in `rock-hero-audio-engine`
- public headers are documented with Doxygen
- `rock-hero-core` already has a Catch2 test target

However, several important gaps remain:

- `rock-hero-audio-engine` still leaks Tracktion through `AudioEngine::getEngine()`
- `rock-hero-ui` still includes Tracktion in `waveform_display.cpp`
- `AudioEngine` public API still exposes `juce::File`
- `MainWindow::ContentComponent` owns significant editor interaction logic directly in app code
- `TransportControls` still owns play/pause decision logic internally
- waveform seek and playback cursor logic still live inside JUCE component code
- only `rock-hero-core` has tests today, and those tests cover only trivial construction/state
- no replayable simulation layer exists yet for gameplay/timing logic

## Alignment Goals

The codebase should move toward these outcomes:

1. Most behavior lives in pure or near-pure libraries.
2. `rock-hero-audio-engine` remains an adapter layer around Tracktion/JUCE.
3. `rock-hero-ui` becomes a presentation layer with minimal policy.
4. App targets become thinner composition roots.
5. Time, threading, hardware, and IO concerns remain at the system boundary.
6. The majority of automated tests run without GUI, audio hardware, or real-time runtime state.
7. Public headers follow `docs/design/documentation-style.md` consistently.

## Work Streams

## 1. Tighten Public Boundaries

### Objective

Reduce framework leakage across module boundaries so that interfaces reflect project abstractions
rather than Tracktion or JUCE implementation details.

### Tasks

- Replace `juce::File` in `AudioEngine` public APIs with `std::filesystem::path`.
- Replace `juce::File` in `WaveformDisplay` public APIs with `std::filesystem::path`.
- Remove `AudioEngine::getEngine()` from the public interface.
- Replace direct Tracktion access from UI code with a narrower project-owned waveform abstraction.

### Expected Result

- fewer transitive JUCE includes
- stronger separation between UI and audio-engine internals
- easier unit testing of callers
- cleaner fallback path if Tracktion integration changes later

## 2. Move Waveform Backend Logic Out of UI

### Objective

Make `rock-hero-ui` Tracktion-free at the module level, not just at the header level.

### Tasks

- Create a dedicated waveform service or source type in `rock-hero-audio-engine`.
- Move `tracktion::SmartThumbnail`, `tracktion::AudioFile`, and related proxy logic into that
  implementation.
- Change `WaveformDisplay` so it depends only on a Tracktion-free abstraction plus JUCE.
- Remove direct Tracktion linkage from `rock-hero-ui` once the backend move is complete.

### Expected Result

- `rock-hero-audio-engine` becomes the sole owner of Tracktion integration
- `rock-hero-ui` becomes closer to a true presentation layer
- waveform rendering logic becomes easier to test in pieces

## 3. Extract Editor Behavior Out of App/UI Code

### Objective

Move nontrivial editor decisions out of `MainWindow::ContentComponent` and JUCE widgets.

### Tasks

- Extract file-load flow into a project-owned editor service or controller model.
- Extract transport intent handling out of `TransportControls::onPlayPauseClicked()`.
- Extract waveform click-to-seek math into a plain function or small service.
- Introduce explicit editor state where appropriate, rather than deriving all behavior from widget
  internals.
- Make JUCE widgets emit intents or call narrow interfaces rather than directly owning policy.

### Candidate Future Library

- `rock-hero-editor-core`

This library would be a good home for:

- editor command logic
- transport intent handling
- selection state
- timeline coordinate logic
- persistence intents

### Expected Result

- thinner app target
- thinner UI components
- more pure logic available for unit tests

## 4. Introduce Project-Owned Interfaces at Key Boundaries

### Objective

Create seams that allow tests to substitute deterministic fakes without mocking third-party
frameworks.

### Tasks

- Define a transport-facing abstraction for playback state and seek/play/pause/stop behavior.
- Define a waveform abstraction for readiness, duration, progress, and rendering/data access.
- Define repository-style abstractions for loading and saving song/session state if persistence
  begins to grow.
- Define interfaces for timing or scoring input streams once gameplay systems appear.

### Timing Rule for Interface Introduction

Do not define an interface speculatively. The right trigger is having a second implementation —
typically the fake or stub needed by a test. Defining an interface before that point tends to
produce the wrong contract, because the shape of a good abstraction only becomes clear once the
real implementation exists and a concrete testing need is known.

The interfaces listed above are candidates, not obligations. Introduce each one when a test
requires a fake, not before.

### Expected Result

- tests can fake project contracts instead of mocking JUCE or Tracktion
- application logic becomes less coupled to concrete runtime objects
- future subsystem boundaries become clearer earlier

## 5. Grow Rock Hero Core Into the Main Home for Pure Logic

### Objective

Make `rock-hero-core` the primary repository of deterministic business behavior.

### Tasks

- Add validation rules for `Song`, `Chart`, `Arrangement`, and `NoteEvent`.
- Add timeline and coordinate conversion helpers that do not require framework types.
- Add arrangement querying, sorting, and filtering helpers.
- Add fixture builders for test data.
- As gameplay work starts, move note matching, calibration math, and scoring rules into a pure
  library rather than directly into the game app.

### Expected Result

- stronger alignment with `docs/design/architectural-principles.md`
- higher test density in pure code
- reduced pressure to test behavior through the running apps

## 6. Establish a Dedicated Gameplay Logic Layer Before the Game Grows

### Objective

Prevent the game executable from becoming the place where timing and scoring logic accumulates.

### Tasks

- Introduce a new non-app library before serious gameplay logic lands.
- Keep it independent from SDL, bgfx, and JUCE.
- Put scoring rules, hit-window evaluation, calibration application, and note matching there.
- Design the API around explicit state, explicit timing inputs, and explicit outputs.

### Candidate Future Library

- `rock-hero-gameplay`

### Expected Result

- gameplay logic remains headless and simulation-friendly
- future automated testing stays cheap even as game features expand

## 7. Add a Replayable Simulation Layer

### Objective

Create a headless way to validate gameplay and timing behavior without real-time devices.

### Tasks

- Define simulation inputs: arrangement, transport positions, pitch/onset events, calibration
  offsets.
- Define simulation outputs: hits, misses, score changes, combo updates, timing diagnostics.
- Build deterministic test fixtures around representative phrases and edge cases.
- Use the simulator as the main test harness for timing-sensitive gameplay rules.

### Expected Result

- strong confidence in scoring/timing behavior
- low dependence on real audio input or rendering for correctness
- better debugging when gameplay "feels off"

## 8. Expand the Test Suite by Layer

### Objective

Bring the test pyramid into line with the architectural principles.

### Tasks

- Expand `libs/rock-hero-core/tests` beyond construction checks into validation and behavior tests.
- Add `libs/rock-hero-audio-engine/tests` for adapter-level behavior that can be tested without
  real hardware.
- Add `libs/rock-hero-ui/tests` only for narrow synchronous wiring or pure helper logic.
- Avoid one monolithic test binary; keep per-library test targets.
- Prefer hand-written fakes and fixtures over framework mocks.

### Test Priorities

1. `rock-hero-core` behavior and fixtures
2. extracted editor command/state logic
3. waveform coordinate and transport logic
4. `rock-hero-audio-engine` adapter tests
5. gameplay simulation tests once gameplay logic exists

### Expected Result

- the majority of test coverage sits in low-friction layers
- future changes are safer and faster to validate

## 9. Treat Time and Threading as Boundary Concerns

### Objective

Keep timing and concurrency out of core logic wherever possible.

### Tasks

- Move timing calculations into explicit inputs and helper functions.
- Avoid letting domain logic read implicit UI cadence or timer state.
- Keep lock-free queues, atomics, and callback ownership in infrastructure code.
- Convert thread-driven updates into snapshots or events before they reach business logic.

### Expected Result

- easier deterministic tests
- clearer responsibility split between infrastructure and policy
- less risk of concurrency leaking into high-level logic

## 10. Bring Documentation Into Full Alignment

### Objective

Keep headers and architecture notes synchronized with the actual code structure.

### Tasks

- Update public headers as boundaries change, using `DOCUMENTATION_STYLE.md`.
- Remove stale comments that describe temporary compromises after those compromises are removed.
- Add Doxygen to any new public headers introduced during extraction/refactoring.
- Keep `ARCHITECTURE.md` descriptive and high-level.
- Record major design-policy decisions in `ARCHITECTURAL_PRINCIPLES.md` only when they become
  recurring rules rather than one-off implementation notes.

### Expected Result

- public API docs stay trustworthy
- architecture docs remain readable and purposeful
- no drift between code and documentation intent

## Recommended Execution Order

### Phase 1: Boundary Cleanup

- change public path/string types away from JUCE where practical
- remove `AudioEngine::getEngine()` from the public API
- define the desired waveform abstraction boundary

### Phase 2: Waveform Refactor

- move waveform backend logic into `rock-hero-audio-engine`
- remove Tracktion from `rock-hero-ui`
- add focused tests for extracted waveform math/helpers

### Phase 3: Editor Logic Extraction

- move transport/file-load/seek decisions into non-UI code
- thin down `MainWindow::ContentComponent`
- add tests for extracted editor logic

### Phase 4: Core Growth

- add real behavior to `rock-hero-core`
- add validation, builders, and timing helpers
- deepen the pure test suite substantially

### Phase 5: Gameplay Foundation

- introduce a dedicated gameplay library before the game app grows
- build the replayable simulation layer
- add scoring/timing tests before rendering-heavy features accumulate

## Success Criteria

The codebase is meaningfully closer to the target architecture when:

- `rock-hero-ui` no longer depends on Tracktion
- `AudioEngine` no longer leaks Tracktion through public methods
- app targets are noticeably thinner
- editor and gameplay behavior live in non-app libraries
- most new tests do not require GUI or hardware setup
- `rock-hero-core` and future pure libraries contain the majority of new behavioral code
- public headers and comments match the actual boundaries

## Non-Goals

This plan does not require:

- rewriting the whole project before adding features
- mocking JUCE or Tracktion broadly
- forcing every class through an abstraction layer
- moving visual behavior out of UI when it is genuinely presentation-only

The goal is disciplined structure, not abstraction for its own sake.

## Summary

The main architectural move is to keep shifting behavior downward:

- out of app targets
- out of JUCE components
- out of Tracktion-facing code
- into pure or near-pure libraries with narrow boundaries

That is the path that best aligns the project with its stated architecture and principles, and it
is the path most likely to keep the project scalable and highly automated-testable as it grows.
