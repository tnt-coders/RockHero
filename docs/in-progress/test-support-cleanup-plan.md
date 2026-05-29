# Test Support Cleanup Plan

## Status

The shared-helper extraction slices are implemented. This work follows the shared test-helper
strategy established in `centralized-test-fakes-and-trompeloeil-plan.md`: use one module-owned
`*_testing` target per production module that owns reusable test helpers, keep helpers under that
module's `tests/include/` tree, and keep production targets free of test-only code.

Implemented shared targets now include:

- `rock_hero::common::audio_testing`;
- `rock_hero::editor::core_testing`;
- `rock_hero::editor::ui_testing`.

Large test-file splitting has been reassessed and deferred. The helper extraction removed the
largest duplicated setup without introducing a new fixture layer. Splitting
`test_editor_controller.cpp` or `test_editor_view.cpp` cleanly should be a focused follow-up
because it will need careful subject boundaries and likely a small private test fixture header for
local controller/view scaffolding. Doing that in the same pass would add broad file churn beyond
the shared-helper cleanup.

## Goals

- Remove repeated test doubles when the same project-owned port fake is needed by multiple test
  files or test targets.
- Improve test readability by giving shared helpers names that describe their behavior, not just
  the interface they implement.
- Keep shared helpers owned by the module that owns the production interface.
- Avoid a global test-support warehouse.
- Keep small one-off listener probes local until duplication proves they should be shared.
- Preserve the current fake-first testing style and avoid adding Trompeloeil until strict
  interaction semantics genuinely require it.

## Non-Goals

- Do not centralize every fake by default.
- Do not introduce a single top-level `rock_hero_testing` target.
- Do not move project-config pytest fixtures or Conan recipe smoke tests into Rock Hero C++ test
  support.
- Do not share helpers from product-specific modules upward into `common`.
- Do not rename production interfaces as part of this cleanup.

## Current Test Shape

Project-owned C++ tests currently live under:

- `rock-hero-common/core/tests/`
- `rock-hero-common/audio/tests/`
- `rock-hero-editor/core/tests/`
- `rock-hero-editor/ui/tests/`

There are no game-module tests yet. `project-config/cmake-conan/tests/test_smoke.py` is a separate
tooling test suite and should stay outside this C++ test-support cleanup.

The main duplication is now cross-module rather than purely local:

- editor tests re-declare fakes for common-audio ports;
- UI tests re-declare JUCE component lookup and mouse-event helpers;
- editor UI tests re-declare a large fake implementation of `IEditorController`;
- several tests define similar temporary-directory fixtures.

### Concrete Duplication Inventory

The duplicates below are the basis for the shared-target proposals. Each entry lists the
production interface and every file that defines a fake or helper for it. Verify these line
numbers when starting a slice; they reflect the tree at the time of writing and may drift.

**Common-audio ports**

- `ITransport`:
  - `rock-hero-common/audio/tests/test_transport.cpp:12` (`FakeTransport`)
  - `rock-hero-editor/ui/tests/test_editor_view.cpp:354` (`FakeTransport`)
  - `rock-hero-editor/ui/tests/test_editor.cpp:38` (`FakeTransport`)
  - `rock-hero-editor/core/tests/test_editor_controller.cpp:485` (`FakeTransport` combined with
    `ILiveInput` — confirm a shared helper can satisfy both surfaces, or keep a local combined
    helper here)
- `ISongAudio`:
  - `rock-hero-common/audio/tests/test_song_audio.cpp:30`
  - `rock-hero-editor/ui/tests/test_editor.cpp:101`
  - `rock-hero-editor/core/tests/test_editor_controller.cpp:656`
- `IPluginHost`:
  - `rock-hero-common/audio/tests/test_plugin_host.cpp:16`
  - `rock-hero-editor/core/tests/test_editor_controller.cpp:737`
- `IAudioDeviceConfiguration`:
  - `rock-hero-common/audio/tests/test_audio_device_settings.cpp:24`
  - `rock-hero-editor/ui/tests/test_editor_view.cpp:425`
  - `rock-hero-editor/ui/tests/test_editor.cpp:216` (combined with `ILiveInput` as
    `FakeEditorAudioPorts`)
  - `rock-hero-editor/core/tests/test_editor_controller.cpp:1080`
- `IThumbnail` and `IThumbnailFactory`:
  - `rock-hero-editor/ui/tests/test_editor_view.cpp:500` and `:548`
  - `rock-hero-editor/ui/tests/test_editor.cpp:143` and `:191`
  - `rock-hero-editor/ui/tests/test_arrangement_view.cpp:47` and `:116`

**Editor-core ports**

- `IEditorController`:
  - `rock-hero-editor/core/tests/test_editor_controller.cpp:171`
  - `rock-hero-editor/ui/tests/test_editor_view.cpp:39` (~315 lines — the single largest
    duplicated fake in the tree)

**UI helpers**

- `findChildRecursive` + `findRequiredChild<T>` wrapper:
  - `rock-hero-editor/ui/tests/test_editor_view.cpp:616` and `:642`
  - `rock-hero-editor/ui/tests/test_plugin_browser_window.cpp:46` and `:72`
- Direct-child `findRequiredChild<T>` wrappers:
  - `rock-hero-editor/ui/tests/test_editor.cpp:357`
  - `rock-hero-editor/ui/tests/test_audio_device_settings_view.cpp:12`
  - These helpers intentionally use JUCE's direct `findChildWithID()` lookup. Extract them as a
    separate direct-child helper instead of folding them into the recursive helper.
- Mouse-event builders:
  - `rock-hero-editor/ui/tests/test_transport_controls.cpp:62` (`makeButtonMouseEvent`)
  - `rock-hero-editor/ui/tests/test_editor_view.cpp:706` (`makeMouseDownEvent`)
  - `rock-hero-editor/ui/tests/test_arrangement_view.cpp:22` (`makeMouseDownEvent`)

**Intentionally local (do not extract)**

- `FakeProjectServices` at `rock-hero-editor/core/tests/test_editor_controller.cpp:1256` — used by
  only one test file and is a duck-typed template collaborator, not an interface implementation.
- `FakeEditorView` at `rock-hero-editor/core/tests/test_editor_controller.cpp:50` — only one
  consumer today; promote to `editor::core_testing` only if a second test target needs it.
- `DeferredEditorTaskRunner` at `rock-hero-editor/core/tests/test_editor_controller.cpp:1735` —
  same rule; promote only on second consumer.
- All `Fake*Listener` probes — small, surface-specific, and per the cleanup rules below they stay
  local until duplicated.

## Proposed Shared Targets

### `rock_hero::common::audio_testing`

Create this target under `rock-hero-common/audio/tests/` when extracting the first shared audio
fake:

```cmake
add_library(rock_hero_common_audio_testing INTERFACE)
add_library(rock_hero::common::audio_testing ALIAS rock_hero_common_audio_testing)
```

Headers should live under:

```text
rock-hero-common/audio/tests/include/rock_hero/common/audio/testing/
```

This target should own helpers for interfaces declared in `rock-hero-common/audio`, such as
`ITransport`, `ISongAudio`, `IPluginHost`, `IThumbnailFactory`, and
`IAudioDeviceConfiguration`.

Likely helpers:

- `ManualTransport` or `RecordingTransport`, replacing repeated `FakeTransport` variants where
  the test needs current state, current position, listener registration, and command counts.
- `RecordingTransportListener`, replacing `CapturingTransportListener` if it becomes useful
  outside transport contract tests.
- `ConfigurableSongAudio`, replacing repeated `FakeSongAudio` implementations that prepare songs,
  fill durations, record active-arrangement selection, and optionally fail.
- `RecordingPluginHost`, replacing near-duplicate plugin-host fakes in common-audio and
  editor-core tests.
- `RecordingThumbnail` and `RecordingThumbnailFactory`, replacing repeated thumbnail fakes across
  editor UI tests.
- `ConfigurableAudioDeviceConfiguration`, replacing repeated audio-device configuration fakes that
  expose a real `juce::AudioDeviceManager`, configurable status, optional input identity, listener
  registration, and manual notifications.

Do not force one shared helper to cover every local behavior. If two tests need incompatible
behavior, keep the local fake or split the shared helper into behavior-specific types.

### `rock_hero::editor::core_testing`

This target already exists. Expand it only for helpers that implement editor-core interfaces and
are reused across test targets.

Likely additions:

- `RecordingEditorController`, replacing the repeated large `FakeEditorController` in editor-core
  and editor-ui tests.
- `RecordingEditorView`, if another test target needs an `IEditorView` sink beyond
  `test_editor_controller.cpp`.
- `RecordingEditorSettings`, if tests need to assert persisted reads/writes instead of using
  `NullEditorSettings`.
- `DeferredEditorTaskRunner`, if deferred task completion becomes useful outside the controller
  tests where it currently lives.

The current `NullEditorSettings` and `ImmediateEditorTaskRunner` remain appropriate names because
they describe concrete behavior rather than merely repeating the interface names.

### `rock_hero::editor::ui_testing`

Create this target under `rock-hero-editor/ui/tests/` when extracting the first shared JUCE UI test
helper:

```cmake
add_library(rock_hero_editor_ui_testing INTERFACE)
add_library(rock_hero::editor::ui_testing ALIAS rock_hero_editor_ui_testing)
```

Headers should live under:

```text
rock-hero-editor/ui/tests/include/rock_hero/editor/ui/testing/
```

Likely helpers:

- `findRequiredDescendant<T>()` to replace the duplicated recursive `findRequiredChild<T>` in
  `test_editor_view.cpp` and `test_plugin_browser_window.cpp`. All existing call sites recurse, so
  start with one descendant-search helper.
- `findRequiredDirectChild<T>()` to replace the duplicated direct `findChildWithID()` wrappers in
  `test_editor.cpp` and `test_audio_device_settings_view.cpp`.
- `findRequiredTopLevelComponent<T>()` for popup/window tests
  (already present locally in `test_editor_view.cpp`).
- `makeMouseDownEvent()` (or `makeLeftButtonMouseEvent()`) to replace the duplicated
  `makeMouseDownEvent` in `test_editor_view.cpp` and `test_arrangement_view.cpp`, and to subsume
  `makeButtonMouseEvent` in `test_transport_controls.cpp` if its targeting logic generalizes.
- `clickButton()` for tests that need to exercise JUCE's normal button mouse path.

Keep the direct and descendant helpers separate. The names should make each test's structural
expectation visible at the call site instead of hiding it behind a generic `findRequiredChild<T>()`
name.

### `rock_hero::common::core_testing`

Create this target only after a concrete shared common-core helper is needed by more than one test
target.

Likely future helper:

- `ScopedTemporaryDirectory`, parameterized by a readable prefix.

Potential but lower-priority helpers:

- small `Song` and `Arrangement` builders for canonical test fixtures;
- native song package fixture writers.

Be cautious with builders. Many existing song/package fixtures encode the exact behavior under
test, so moving them too early can obscure intent.

## Recommended Implementation Order

Slices 1 and 2 are independent and can run in either order or in parallel. Slice 1 is the single
largest line-count reduction (~315 duplicated lines in `test_editor_view.cpp` alone), so prefer it
first if only one slice is on the table.

1. Expand `rock_hero::editor::core_testing` with `RecordingEditorController`.

   The `IEditorController` fake in `test_editor_view.cpp:39` is the largest single duplicated
   helper in the tree. Extracting it shrinks the UI test file dramatically without depending on
   any other slice. The existing `editor::core_testing` target already owns the right namespace
   and include path, so no new CMake target is required.

2. Add `rock_hero::common::audio_testing`.

   Start with the common-audio fakes that are duplicated across module boundaries:
   `RecordingPluginHost`, `ConfigurableSongAudio`, and `ConfigurableAudioDeviceConfiguration`.
   For `FakeTransport`, decide up front whether the shared helper covers `ITransport` only
   (leaving `test_editor_controller.cpp:485` to keep its combined `ITransport`+`ILiveInput`
   helper local) or whether a second helper composes both. Pick whichever keeps each fake's
   purpose readable.

3. Extract thumbnail helpers.

   Move the three duplicated `IThumbnail` / `IThumbnailFactory` fakes into
   `common::audio::testing` if a shared helper can support both source-propagation checks and
   arrangement paint checks without becoming vague. If the paint behavior needs richer observation
   than construction tests do, split the helper names rather than adding many unrelated flags to
   one fake.

4. Add `rock_hero::editor::ui_testing`.

   Extract the duplicated recursive helpers as `findRequiredDescendant<T>()`, the duplicated direct
   helpers as `findRequiredDirectChild<T>()`, and the duplicated `makeMouseDownEvent` builder.
   Leave `findRequiredTopLevelComponent<T>` local until a second test target needs it.

5. Consider `rock_hero::common::core_testing`.

   Extract `ScopedTemporaryDirectory` only after reviewing all temp-directory users together. Then
   consider builders if several tests are repeating the same neutral fixture data rather than
   behavior-specific setup.

6. Split large test files by subject.

   After shared helpers reduce setup noise, split the largest files by behavior:

   - controller project lifecycle;
   - controller plugin and live-rig behavior;
   - controller transport behavior;
   - controller busy-state routing;
   - controller input/output gain and calibration;
   - editor-view layout;
   - editor-view plugin UI;
   - editor-view calibration UI.

   Do this after helper extraction so each new file starts with minimal local scaffolding.

## Naming Rules

- Prefer behavior-specific names:
  - `RecordingPluginHost`
  - `ConfigurableSongAudio`
  - `ManualTransport`
  - `RecordingThumbnailFactory`
  - `ConfigurableAudioDeviceConfiguration`
- Use `Null*` for no-op implementations that deliberately ignore writes and return empty values.
- Use `Immediate*` for synchronous implementations of normally deferred behavior.
- Use `Deferred*` for helpers that store work for explicit test-controlled completion.
- Reserve `Mock*` for future Trompeloeil expectation types.
- Keep local one-off listener probes named after the exact test surface they record.

Avoid broad `Fake*` names in shared headers unless no clearer behavior name exists. Local
translation-unit fakes may keep simple names while they remain private to one test file.

## Cleanup Rules

- Extract a helper only when at least two test files or targets need the same behavior.
- Put the helper in the module that owns the production interface it implements.
- Keep shared helper headers focused. Prefer one primary helper per file unless a small group is
  intentionally inseparable.
- Do not make tests depend on private production headers through a shared testing target.
- Do not expose testing targets from production umbrella targets.
- Do not add helper behavior speculatively. Add only the state and operations needed by current
  tests.

## Verification Plan

After each extraction slice, build and run only the targets affected by that slice:

- Slice 1: `rock_hero_editor_core_tests` and `rock_hero_editor_ui_tests`.
- Slices 2 and 3: `rock_hero_common_audio_tests`, `rock_hero_editor_core_tests`, and
  `rock_hero_editor_ui_tests`.
- Slice 4: `rock_hero_editor_ui_tests`.
- Slice 5: `rock_hero_common_core_tests` plus any downstream target that consumes the new helper.
- Slice 6: whichever test targets receive file splits.

Use the same Visual Studio developer environment pattern for each target set. For example, slices 2
and 3 use:

```powershell
cmd.exe /d /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && ninja -C build/debug rock_hero_common_audio_tests rock_hero_editor_core_tests rock_hero_editor_ui_tests'
& 'build/debug/rock-hero-common/audio/tests/rock_hero_common_audio_tests.exe'
& 'build/debug/rock-hero-editor/core/tests/rock_hero_editor_core_tests.exe'
& 'build/debug/rock-hero-editor/ui/tests/rock_hero_editor_ui_tests.exe'
```

Run full CTest after the final split of large files or after any CMake target graph changes that
could affect unrelated tests.

## Definition of Done

- Repeated common-audio fakes are owned by `rock_hero::common::audio_testing`.
- Repeated editor-core interface fakes are owned by `rock_hero::editor::core_testing`.
- Repeated JUCE UI test helpers are owned by `rock_hero::editor::ui_testing`.
- Any future common-core fixture helpers are owned by `rock_hero::common::core_testing`.
- Large test files are split only after shared setup no longer dominates each file.
- Local one-off probes remain local.
- No production target links any testing target.
- The test suite still favors hand-written fakes and state/output assertions over broad mocks.
