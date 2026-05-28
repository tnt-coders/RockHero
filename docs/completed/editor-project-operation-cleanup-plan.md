# Editor Project Operation Cleanup Plan

Status: Completed.

## Purpose

Remove the audio-normalization analyzer from the `EditorController` construction API while keeping
the existing busy-overlay behavior during backing-audio analysis.

The controller should depend on editor-level project operations. It should not expose the lower
level function used by `Project::load()` and `Project::import()` to compute audio-normalization
metadata.

## Problem

Before implementation, the analyzer alias had two legitimate uses:

- `Project::load()` uses it to repair missing or stale backing-audio normalization metadata.
- `Project::import()` uses it to analyze each unique imported backing-audio asset.

That part is sound. `Project` owns the policy that loaded or imported songs must have usable
normalization metadata before they are committed to the editor session.

The smell was that `EditorController::Services` also exposed
`audio_analyze_for_gain_function`. That made controller construction aware of a detail inside
project loading/importing. The controller should know that an open/import operation may progress
into a backing-audio analysis phase; it should not know which concrete function performs LUFS-I
analysis.

The former controller-level seam existed because `makeBusyAudioAnalyzeForGainFunction()` wrapped
the analyzer so the worker thread could switch the busy overlay to the analyzing state and wait for
that state to paint before expensive analysis starts. That user-visible behavior was preserved, but
the dependency is now expressed as project-operation progress rather than as an injected analyzer.

## Target Design

The desired ownership is:

```text
common/audio
  analyzeAudioForGainNormalization()

editor/core Project
  owns package/workspace mechanics and receives AudioNormalizationAnalyzer as a local test seam

editor/core EditorController
  owns editor workflow state and receives progress-aware project operations

editor/app
  wires production runtime services and audio ports
```

### Project-Level Analyzer

Rename the low-level function alias in `project.h` from `AudioAnalyzeForGainFunction` to:

```cpp
using AudioNormalizationAnalyzer = std::function<
    std::expected<common::core::AudioNormalization, common::audio::AudioNormalizationError>(
        const std::filesystem::path& input,
        const common::core::AudioNormalizationTarget& target)>;
```

Use parameter names that state the operation clearly:

```cpp
const AudioNormalizationAnalyzer& analyze_audio_normalization
```

This keeps the analyzer seam where it belongs: inside `Project` load/import behavior and the
corresponding `Project` tests.

Do not add an `IAudioNormalizationAnalyzer` interface yet. A single function alias is enough until
the analyzer needs state, cancellation, multi-step progress, or multiple operations.

### Controller-Level Project Progress

Add a controller-level progress callback for project operations:

```cpp
enum class ProjectOperationPhase
{
    AnalyzingBackingAudio,
};

using ProjectOperationProgress = std::function<void(ProjectOperationPhase phase)>;
```

Then change the controller's open/import operation signatures to receive progress, not an
analyzer:

```cpp
using OpenFunction = std::function<std::expected<common::core::Song, ProjectError>(
    Project& project,
    const std::filesystem::path& path,
    const ProjectOperationProgress& report_progress)>;

using ImportFunction = std::function<std::expected<common::core::Song, ProjectError>(
    Project& project,
    const std::filesystem::path& path,
    const ProjectOperationProgress& report_progress)>;
```

The default open/import functions build the lower-level analyzer internally:

```cpp
AudioNormalizationAnalyzer analyzer =
    [&report_progress](
        const std::filesystem::path& input,
        const common::core::AudioNormalizationTarget& target)
{
    report_progress(ProjectOperationPhase::AnalyzingBackingAudio);
    return common::audio::analyzeAudioForGainNormalization(input, target);
};

return project.load(path, {}, analyzer);
```

This keeps the real analyzer selection with the production project operation rather than with the
controller constructor.

### Busy-State Preservation

Replace `makeBusyAudioAnalyzeForGainFunction()` with a progress reporter such as:

```cpp
[[nodiscard]] ProjectOperationProgress makeBusyProjectOperationProgress(std::uint64_t token);
```

The new helper should preserve the existing behavior:

1. When called with `ProjectOperationPhase::AnalyzingBackingAudio`, transition the busy operation
   to `BusyOperation::AnalyzingBackingAudio`.
2. If called from a worker thread with a live JUCE message manager, post the transition to the
   message thread.
3. Wait for the busy overlay paint gate before returning to the worker.
4. If there is no message manager, use the same synchronous fallback behavior the tests rely on.
5. Ignore stale calls whose busy token no longer matches.

The implementation should move the existing paint-gate logic intact instead of rewriting the
thread choreography.

## Recommended Steps

### 1. Rename The Project Analyzer Alias

- Rename `AudioAnalyzeForGainFunction` to `AudioNormalizationAnalyzer`.
- Rename parameters from `analyze_audio` to `analyze_audio_normalization`.
- Update `Project::load()`, `Project::import()`, private project helpers, and `test_project.cpp`.
- Keep the default value as `common::audio::analyzeAudioForGainNormalization`.

Verification:

- `rg "AudioAnalyzeForGainFunction"` should find no production references.
- `rock_hero_editor_core_tests` should still cover project load/import normalization behavior.

### 2. Make Controller Project Operations Progress-Aware

- Add `ProjectOperationPhase` and `ProjectOperationProgress` near the controller operation alias
  declarations.
- Change `OpenFunction` and `ImportFunction` to receive `ProjectOperationProgress` instead of
  `AudioNormalizationAnalyzer`.
- Update the default open/import helpers in `editor_controller.cpp` to construct the real
  `AudioNormalizationAnalyzer` internally and call the progress function before analysis.
- Remove `audio_analyze_for_gain_function` from `EditorController::Services`.
- Remove `m_audio_analyze_for_gain_function` from `EditorController::Impl`.

This can be done without splitting `Services` yet. A future cleanup can still rename or split the
controller construction bundles into `ControllerOperations` and `RuntimeServices`.

### 3. Move Busy Analysis Reporting To Operation Progress

- Rename `makeBusyAudioAnalyzeForGainFunction()` to `makeBusyProjectOperationProgress()`.
- Change it to return `ProjectOperationProgress`.
- Move the existing message-thread post, paint gate, and token checks into the
  `AnalyzingBackingAudio` phase handling.
- Update `openProject()` and `importSongSource()` to pass the progress reporter to the open/import
  operations.

The visible busy overlay sequence should not change.

### 4. Update Controller Tests

Controller tests should no longer inject an analyzer through `Services`.

Tests that need to simulate the analysis phase should provide an open/import operation that calls:

```cpp
report_progress(ProjectOperationPhase::AnalyzingBackingAudio);
```

before returning its fake result.

Tests that care about project normalization details should stay in `test_project.cpp` and use
`AudioNormalizationAnalyzer` directly.

### 5. Update Planning Docs

- Update the framework-isolation plan references from `makeBusyAudioAnalyzeForGainFunction()` to
  the new progress-helper name.
- Update any completed normalization plan wording only if a stale statement would mislead future
  implementation work. Do not continuously synchronize old todo docs.

## Naming Notes

Use `AudioNormalizationAnalyzer` only for the low-level project seam that returns
`AudioNormalization`.

Avoid these names at controller level:

- `AudioAnalyzeForGainFunction`: clunky and too implementation-specific.
- `AudioNormalizationFunction`: too vague; it could mean analyze, validate, apply, or render.
- `Callbacks`: misleading for operations the controller invokes.

At controller level, prefer operation/progress language:

- `ProjectOperationPhase`
- `ProjectOperationProgress`
- current `OpenFunction` / `ImportFunction`, or future `OpenProjectOperation` /
  `ImportSongOperation` if the controller operation bundle is cleaned up.

## Risks

The main behavioral risk is weakening the paint gate. If the new progress reporter returns before
the analyzing busy state paints, users may see the UI freeze without the expected "Analyzing"
status during slow LUFS-I analysis.

The refactor should therefore move the current paint-gate logic mostly intact and add or preserve
tests that characterize the analyzing busy transition.

## Non-Goals

- Do not remove the analyzer seam from `Project`; it is still valuable for focused project tests.
- Do not introduce an analyzer interface until a function alias is no longer enough.
- Do not split `EditorController::Services` in the same change unless the user explicitly chooses
  to combine that cleanup.
- Do not change audio-normalization behavior or metadata semantics.
- Do not remove the busy-overlay paint gate.

## Exit Criteria

- `EditorController::Services` no longer exposes an audio-normalization analyzer.
- `EditorController::Impl` no longer stores `m_audio_analyze_for_gain_function`.
- `Project::load()` and `Project::import()` use `AudioNormalizationAnalyzer` as their local seam.
- Open/import operations can still report `AnalyzingBackingAudio` progress to the controller.
- The busy overlay still transitions to analyzing state before expensive normalization work starts.
- Controller tests fake project operation progress rather than fake low-level analysis.
- Project tests still cover normalization analysis success, failure, deduplication, and repair.
- A full test run passes.
