# Centralized Test Doubles and Trompeloeil Plan

## Status

This plan is in progress. The first implementation slice is limited to editor-core test doubles
needed by both editor-core and editor-ui tests after the `EditorController` service-contract
cleanup.

The durable structure decision is:

- use one module-owned `*_testing` target per production module that needs shared test helpers;
- keep test helper headers under the owning module's `tests/include/` tree;
- use `testing` in the include path and namespace;
- keep Trompeloeil out of the build until the first strict interaction test needs it.

## Framework Decision

Keep Catch2 as the project test runner/assertion framework. Use hand-written fakes, stubs, null
objects, immediate runners, fixtures, and builders by default. Introduce Trompeloeil only for tests
where interaction semantics are the behavior under test.

The goal is not to replace fakes with mocks. The goal is to make stateful project-owned
collaborators reusable while keeping strict mocks available for call-count, ordering,
forbidden-call, and failure-escalation cases.

## Goals

- Remove repeated test-double implementations when the same helper is needed by multiple test
  targets.
- Keep tests behavior-oriented and simulation-friendly.
- Preserve the current split between fast public-contract tests and Tracktion/JUCE integration
  tests.
- Keep test helper code out of production targets.
- Avoid mocking JUCE or Tracktion directly.
- Add Trompeloeil later without making it the default way to test every port.

## Non-Goals

- Do not migrate away from Catch2.
- Do not replace every hand-written fake with a mock.
- Do not introduce broad third-party wrapper interfaces only to satisfy a mocking framework.
- Do not centralize one-off listener probes unless they become repeated across test files.
- Do not add a single global fake warehouse target.

## Chosen Structure

Each module that owns shared test helpers exposes one test-only target:

```cmake
add_library(rock_hero_editor_core_testing INTERFACE)
add_library(rock_hero::editor::core_testing ALIAS rock_hero_editor_core_testing)
```

The target exposes a module-owned test include root:

```text
rock-hero-editor/core/tests/include/
  rock_hero/
    editor/
      core/
        testing/
          null_editor_settings.h
          immediate_editor_task_runner.h
```

Tests include helpers by full path:

```cpp
#include <rock_hero/editor/core/testing/null_editor_settings.h>
#include <rock_hero/editor/core/testing/immediate_editor_task_runner.h>
```

The namespace mirrors the include path:

```cpp
namespace rock_hero::editor::core::testing
```

This keeps ownership local to the module that owns the interface while allowing downstream test
targets to opt in explicitly:

```cmake
target_link_libraries(
    rock_hero_editor_ui_tests
    PRIVATE rock_hero::editor::ui
            rock_hero::editor::core_testing
            rock_hero::build_policy
            Catch2::Catch2WithMain)
```

Use the same pattern for later modules:

```text
rock_hero::common::audio_testing
rock_hero::editor::ui_testing
rock_hero::game::core_testing
```

## Naming Rules

Name files after the concrete helper class when there is one primary helper:

```text
null_editor_settings.h
immediate_editor_task_runner.h
```

Use grouped file names only when several related helpers are intentionally maintained together:

```text
editor_settings_fixtures.h
editor_settings_builders.h
```

Prefer behavior-specific class names over broad `Fake*` names:

- `NullEditorSettings` for a no-op settings implementation.
- `ImmediateEditorTaskRunner` for a synchronous task runner.
- `RecordingEditorSettings` for settings that capture reads/writes.
- `DeferredEditorTaskRunner` for tests that control completion timing.

Reserve `Mock*` names for expectation-driven Trompeloeil types.

## First Implementation Slice

Add `rock_hero::editor::core_testing` and extract:

- `NullEditorSettings`
- `ImmediateEditorTaskRunner`

Consumers:

- `rock_hero_editor_core_tests`
- `rock_hero_editor_ui_tests`

Do not extract editor audio-port fakes in this slice. They are larger, carry more behavior, and
should be reviewed separately against the same structure after this first target proves useful.

## Future Extraction Candidates

Extract only when duplication is concrete across multiple test files or targets.

### Common Audio

Likely future target:

```text
rock_hero::common::audio_testing
```

Likely future helpers:

- `FakeTransport` or a more behavior-specific transport simulation.
- `CapturingTransportListener`.
- `FakeSongAudio`.
- `FakePluginHost`.
- `FakeThumbnail` and `FakeThumbnailFactory`.
- `FakeAudioDeviceConfiguration`.

These should live with the module that owns the interface, not in editor test directories.

### Editor Core

Potential future helpers:

- `RecordingEditorSettings`.
- `DeferredEditorTaskRunner`.
- `FakeEditorView`.
- focused project-operation builders or result fixtures.

### Editor UI

Potential future target:

```text
rock_hero::editor::ui_testing
```

Potential future helpers:

- view intent recorders;
- component lookup helpers that are repeated across UI test files;
- stable UI fixtures that do not own workflow policy.

## Trompeloeil Introduction

Add Trompeloeil through Conan only when the first real interaction-heavy test is ready. Do not add
it just to prove the dependency works.

Use Trompeloeil when one of these is the behavior:

- a collaborator must not be called;
- a collaborator must be called exactly once or exactly N times;
- calls must occur in a specific order;
- a failure must trigger a specific escalation path;
- a listener must be registered or unregistered with exact object identity;
- a dangerous backend mutation must be prevented.

Do not use Trompeloeil for tests that are better expressed as state/output checks:

- chart rules;
- timing math;
- transport position simulation;
- score calculation;
- editor state derivation;
- fake-backed success/failure flows where final state is the important result.

When reusable mocks become useful, keep them in the owning module's `testing/` tree and link
Trompeloeil through that module's testing target only if all consumers of the target genuinely need
it. If not, split only the mock-heavy helpers into a separate opt-in target at that time.

## Documentation Updates

After the first implementation slice is accepted, update durable design docs only if this pattern
becomes a project rule rather than a local implementation detail:

- `docs/design/architecture.md`: mention module-owned test helper targets.
- `docs/design/architectural-principles.md`: keep the fake-first policy and add the exact
  Trompeloeil-use criteria.
- `docs/design/coding-conventions.md`: document the `testing` include path and namespace
  convention if more than one module adopts it.

## Verification Plan

For the first editor-core slice:

```powershell
cmd.exe /d /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && ninja -C build/debug rock_hero_editor_core_tests rock_hero_editor_ui_tests'
& 'build/debug/rock-hero-editor/core/tests/rock_hero_editor_core_tests.exe'
& 'build/debug/rock-hero-editor/ui/tests/rock_hero_editor_ui_tests.exe'
```

Run full CTest after broader fake extraction phases.

## Definition of Done

- Reusable editor-core test doubles live under the selected test include layout.
- `rock_hero::editor::core_testing` is test-only and links only the dependencies its helpers need.
- Core and UI tests consume the helper target instead of raw relative include paths.
- Test files no longer duplicate the extracted helper types.
- No production target depends on testing code.
- Trompeloeil is not introduced until a test genuinely needs strict mock expectations.
