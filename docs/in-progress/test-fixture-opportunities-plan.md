# Test Fixture Opportunities Plan

Status: in-progress planning note. This is supporting test-maintenance work, not a runtime
refactor dependency.

## Scope

This note captures where the current tests would benefit from fixtures or fixture-like harnesses.
It does not propose converting the whole suite to Catch2 fixtures. The project already favors
plain `TEST_CASE` blocks, explicit setup, small builders, hand-written fakes, and narrow harnesses;
that remains the right default.

The goal is to reduce repeated setup where it obscures the behavior under test, while avoiding
large all-purpose fixtures that hide dependency breadth.

## Current State

Project-owned C++ tests currently use plain Catch2 `TEST_CASE` tests rather than Catch2 fixture
macros such as `TEST_CASE_METHOD`. The suite already has useful fixture-like structures:

- RAII file/directory owners for temporary settings, package, audio, and engine workspaces;
- shared editor-controller and editor-view test harness headers;
- fakes and recording adapters for project-owned audio, controller, view, and thumbnail ports;
- builders for songs, arrangements, package entries, view state, and plugin candidates.

Python tooling tests under `project-config/cmake-conan` use pytest fixtures, but that is separate
from the main C++ test style.

## Strong Fixture Candidates

### EditorController Tests

This is the highest-value candidate. The same construction pattern appears throughout the
`test_editor_controller_*.cpp` files:

- `FakeTransport`;
- `ConfigurableSongAudio`;
- optional `ConfigurableAudioDeviceConfiguration`;
- optional `RecordingPluginHost`;
- optional `FakeLiveRig`;
- `FakeProjectServices`;
- `EditorController`;
- `FakeEditorView`;
- sometimes `DeferredEditorTaskRunner` or persisted settings files.

Add a small fixture or fixture-like builder to
`rock-hero-editor/core/tests/include/rock_hero/editor/core/testing/editor_controller_test_harness.h`.

Preferred shape:

- owns the common fakes as named members;
- exposes explicit construction helpers for common controller variants;
- keeps optional ports visible at the call site when the test depends on them;
- does not auto-load a project or auto-attach a view unless the helper name says so.

Avoid a giant "loaded editor controller" fixture that silently creates every dependency. The setup
breadth is useful evidence for the runtime extraction plan, and hiding it completely would make
future boundary problems harder to see.

### EditorView UI Tests

The `EditorView` tests repeat this setup across layout, timeline, state, busy overlay, signal
chain, and audio-control coverage:

- `juce::ScopedJuceInitialiser_GUI`;
- `core::testing::RecordingEditorController`;
- `FakeTransport`;
- `RecordingThumbnailFactory`;
- `EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)}`.

Add a small `EditorViewFixture` to
`rock-hero-editor/ui/tests/include/rock_hero/editor/ui/testing/editor_view_test_harness.h`.

Preferred shape:

- owns JUCE initialization, controller fake, transport fake, thumbnail factory, and view;
- optionally accepts a custom meter source for the few tests that need it;
- leaves test-specific bounds and state setup in each test.

This is independent of the editor runtime extraction. It should make UI tests quieter without
changing runtime boundaries.

### Project Package Tests

`rock-hero-editor/core/tests/test_project.cpp` repeatedly constructs a temporary archive
directory, package paths, `Project`, `RockSongImporter`, and fake normalization analyzer.

Add a local fixture-like helper only if new project-package tests continue to grow. A fixture could
own:

- `TemporaryArchiveDirectory`;
- common package paths;
- `Project`;
- `FakeAnalyzeAudio`;
- helper methods for writing minimal `.rhp` and `.rock` packages.

This is useful test maintenance, but it is not part of the editor-controller refactor.

### AudioDeviceSettingsController Tests

`test_audio_device_settings_controller.cpp` repeats `FakeAudioDeviceSettings`,
`AudioDeviceSettingsController`, `FakeAudioDeviceSettingsView`, and `attachView`.

Add a small local fixture when touching this file again. Keep dispatcher and destruction tests
explicit because their setup is the behavior under test.

## Lower-Value Candidates

### Engine Tests

`test_engine.cpp` already has `EngineTestHarness` and `TemporarySongDirectory`. Converting the
engine integration tests to `TEST_CASE_METHOD(EngineTestHarness, ...)` would save lines, but the
current explicit local harness construction is acceptable.

### Audio Normalization And Rock Song Package Tests

`test_audio_normalization.cpp` and `test_rock_song_package.cpp` already use RAII temporary
directories. A Catch2 fixture would mostly remove one line per test. Prefer keeping these as-is
unless setup grows.

### Pure Core Tests

Timeline, difficulty, song, session, arrangement, and input-calibration tests are already direct
and readable. Fixtures would mostly hide the simple setup and should be avoided.

## Recommended Sequencing

1. Add an `EditorController` fixture-like builder before or during Stage 1 of the active editor
   runtime extraction, but keep it narrow and explicit.
2. Add an `EditorViewFixture` opportunistically when touching editor UI tests. This can happen
   independently of runtime extraction.
3. Add local fixtures for `Project` and `AudioDeviceSettingsController` only when those files are
   next changed for feature or refactor work.
4. Do not run a repo-wide conversion to Catch2 fixture macros.

## Impact On Editor Runtime Refactor Plans

This does not change the active editor runtime extraction plan or its stage order.

It does reinforce the evidence behind that plan. The broad repeated setup in the editor-controller
tests is a symptom of the same issue the runtime plan addresses: narrow behaviors still require a
large root-controller construction because `EditorController` owns several distinct workflow
clusters.

The practical impact is test-maintenance sequencing:

- add narrow fixture builders where they reduce setup noise before extracting behavior;
- use direct workflow tests for newly extracted `BusyOperationState`,
  `DeferredEditorActionState`, `PluginCatalogWorkflow`, and `InputCalibrationWorkflow`;
- keep broad controller tests as integration-style characterization tests for intent routing,
  side-effect execution, async liveness, and view-state publication.

In other words, fixtures should support the refactor by making characterization tests easier to
read, but they should not become a separate prerequisite or a substitute for extracting headless
workflow policy.

## Non-Goals

- Do not make a single master fixture for every editor-controller test.
- Do not hide behavior-specific setup such as deferred completions, live-rig failures, calibration
  route identity, or explicit project IO callbacks.
- Do not convert simple pure tests to fixtures.
- Do not introduce mocks of JUCE or Tracktion frameworks.
- Do not update durable `docs/design/` documents from this note.
