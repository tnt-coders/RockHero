# Centralized Test Fakes and Trompeloeil Plan

## Status

This plan is **blocked pending a firm test-support structure decision**.

Do not implement the fake extraction or Trompeloeil wiring until the folder layout, include style,
namespace shape, and CMake target/linking strategy are explicitly chosen. The fake extraction itself
is straightforward, but changing the structure after implementation would create unnecessary churn
across every test file that adopts the shared fakes.

## Framework Decision

Keep Catch2 as the project test runner/assertion framework, centralize reusable hand-written
fakes under a test-support target, and introduce Trompeloeil only for tests where interaction
semantics are the behavior under test.

The goal is not to replace fakes with mocks. The goal is to make stateful project-owned
collaborators reusable while keeping strict mocks available for call-count, ordering,
forbidden-call, and failure-escalation cases.

## Goals

- Remove repeated fake implementations from audio and UI test files.
- Keep tests behavior-oriented and simulation-friendly.
- Preserve the current split between fast public-contract tests and Tracktion/JUCE integration
  tests.
- Keep test support out of production targets.
- Avoid mocking JUCE or Tracktion directly.
- Add Trompeloeil without making it the default way to test every port.

## Non-Goals

- Do not migrate away from Catch2.
- Do not replace every hand-written fake with a mock.
- Do not introduce broad third-party wrapper interfaces only to satisfy a mocking framework.
- Do not pull `rock_hero::audio` into fast audio port tests just to access fakes; those tests should
  continue to compile against public audio headers without constructing Tracktion/JUCE adapters.
- Do not centralize one-off listener probes unless they become repeated across test files.

## Structure Decision Required

The project needs one firm answer for each of these before implementation starts:

- where shared fakes live
- what namespace they use
- what include path tests use
- whether test support is module-owned or centralized
- whether test support is one target or several dependency-scoped targets
- where Trompeloeil is found and linked

The most important constraint is dependency direction. A core-only test should not inherit UI,
JUCE, or Tracktion-facing dependencies because it wants a helper. Fast audio port tests should not
link the concrete `rock_hero::audio` adapter library just to reuse a fake for an audio interface.

## Folder and Namespace Options

### Option A: Root `tests/support` tree

Layout:

Create a root-level test-support include tree:

```text
tests/
  support/
    CMakeLists.txt
    include/
      rock_hero/
        test_support/
          audio/
            capturing_transport_listener.h
            fake_edit.h
            fake_thumbnail.h
            fake_thumbnail_factory.h
            fake_transport.h
          ui/
            fake_editor_controller.h
            fake_editor_view.h
```

Namespace:

```cpp
namespace rock_hero::test_support
```

Example include:

```cpp
#include <rock_hero/test_support/audio/fake_transport.h>
```

Pros:

- all test helpers are easy to find from the repository root
- one obvious place for future cross-module fixture builders and replay helpers
- production module directories stay focused on production code plus tests

Cons:

- ownership is less clear because `FakeTransport` lives away from `ITransport`
- one support tree can become a catch-all
- one broad support target can accidentally aggregate audio/UI dependencies
- namespace does not mirror the module that owns the interface being faked

This option is workable, but it is not currently preferred unless most support code becomes
cross-module rather than module-owned.

### Option B: Module-local `tests/support` with flat includes

Layout:

```text
libs/
  rock-hero-audio/
    tests/
      support/
        fake_transport.h
        fake_edit.h
        fake_thumbnail.h
        fake_thumbnail_factory.h

  rock-hero-ui/
    tests/
      support/
        fake_editor_view.h
        fake_editor_controller.h
```

Namespace:

```cpp
namespace rock_hero::audio::test_support
namespace rock_hero::ui::test_support
```

Example include:

```cpp
#include <fake_transport.h>
```

Pros:

- fake ownership is local to the module that owns the interface
- folder structure is very short
- CMake target visibility controls which support directory is visible

Cons:

- bare includes are less self-describing when read outside the owning CMake context
- flat support include directories can collide if two linked support targets expose the same name
- `test_support` is a little verbose when already scoped under `audio` or `ui`

This option is simple, but the include-path ambiguity is a real downside once UI tests link both
audio and UI support targets.

### Option C: Module-local `tests/include/rock_hero/<module>/test`

Layout:

```text
libs/
  rock-hero-audio/
    tests/
      include/
        rock_hero/
          audio/
            test/
              fake_transport.h
              fake_edit.h
              fake_thumbnail.h
              fake_thumbnail_factory.h
              capturing_transport_listener.h

  rock-hero-ui/
    tests/
      include/
        rock_hero/
          ui/
            test/
              fake_editor_view.h
              fake_editor_controller.h
```

Namespace:

```cpp
namespace rock_hero::audio::test
namespace rock_hero::ui::test
```

Example includes:

```cpp
#include <rock_hero/audio/test/fake_transport.h>
#include <rock_hero/ui/test/fake_editor_view.h>
```

Pros:

- ownership stays with the module that owns the faked interface
- include paths follow the existing `rock_hero/<module>/...` convention
- `::test` is concise because it is scoped under `audio`, `core`, or `ui`
- linked support targets can coexist without header-name collisions
- test-only headers stay outside production `include/` trees
- UI tests can clearly consume audio test support and UI test support at the same time

Cons:

- deeper than flat support folders
- requires one test-support include tree per module
- the project must document that `rock_hero::<module>::test` is test-only API

This is the current preferred option, but it should not be implemented until explicitly accepted.

### Option D: One `rock-hero-test` library

Layout:

```text
libs/
  rock-hero-test/
    include/
      rock_hero/
        test/
          fake_transport.h
          fake_edit.h
          fake_editor_view.h
```

Namespace:

```cpp
namespace rock_hero::test
```

Pros:

- one library is easy to discover
- all test support can be versioned and linked consistently
- useful if the project eventually grows a large shared simulation/replay harness

Cons:

- high risk of becoming a fake warehouse
- dependency boundaries become harder to preserve
- core tests may accidentally inherit audio/UI dependencies
- fake ownership is separated from the interface owner
- target naming does not reveal whether a helper is core-only, audio-facing, or UI-facing

This option is not recommended for all fakes. A root/shared test library may make sense later for
truly cross-cutting helpers, but module-owned fakes should not start there.

## Linking Options

### Option 1: One global support target

```cmake
add_library(rock_hero_test_support INTERFACE)
add_library(rock_hero::test_support ALIAS rock_hero_test_support)
```

This is simple, but it pressures all fakes into one dependency surface. It is only appropriate if
the support target remains include-only and dependency-light, or if it is limited to generic helpers
that do not depend on audio/UI contracts.

### Option 2: Module-owned support targets

```cmake
add_library(rock_hero_audio_test_support INTERFACE)
add_library(rock_hero::audio_test_support ALIAS rock_hero_audio_test_support)

add_library(rock_hero_ui_test_support INTERFACE)
add_library(rock_hero::ui_test_support ALIAS rock_hero_ui_test_support)
```

This is the current preferred linking direction. Each module exposes test fakes for the interfaces
it owns. Downstream tests opt into exactly the support they need:

```cmake
target_link_libraries(
    rock_hero_ui_tests
    PRIVATE rock_hero::ui
            rock_hero::audio_test_support
            rock_hero::ui_test_support
            rock_hero::build_policy
            Catch2::Catch2WithMain)
```

The audio fast-test target can link `rock_hero::audio_test_support` without linking the concrete
`rock_hero::audio` adapter library if the audio support target depends only on public audio
headers and `rock_hero::core`.

### Option 3: Split by dependency level

If support code grows beyond header-only fakes, use dependency-scoped targets:

```text
rock_hero::core_test_support
rock_hero::audio_contract_test_support
rock_hero::audio_integration_test_support
rock_hero::ui_test_support
```

This is more verbose, but it keeps fast tests clean. It is probably unnecessary right now, but it is
the escape hatch if one module's test helpers start needing heavier dependencies.

## Current Preferred Shape

Pending explicit approval, the best-balanced structure appears to be:

```text
libs/rock-hero-audio/tests/include/rock_hero/audio/test/...
libs/rock-hero-ui/tests/include/rock_hero/ui/test/...
libs/rock-hero-core/tests/include/rock_hero/core/test/...   # only when needed
```

with:

```cpp
namespace rock_hero::audio::test
namespace rock_hero::ui::test
namespace rock_hero::core::test
```

and module-owned targets:

```text
rock_hero::audio_test_support
rock_hero::ui_test_support
rock_hero::core_test_support
```

This remains a proposal, not an implementation decision.

## CMake Shape

If Option C is accepted, each module's test CMake should add its own support target before the test
executables that consume it.

For audio:

```cmake
add_library(rock_hero_audio_test_support INTERFACE)
add_library(rock_hero::audio_test_support ALIAS rock_hero_audio_test_support)

target_include_directories(rock_hero_audio_test_support INTERFACE
                           "${CMAKE_CURRENT_SOURCE_DIR}/tests/include")

target_link_libraries(rock_hero_audio_test_support INTERFACE rock_hero::core)
```

For UI:

```cmake
add_library(rock_hero_ui_test_support INTERFACE)
add_library(rock_hero::ui_test_support ALIAS rock_hero_ui_test_support)

target_include_directories(rock_hero_ui_test_support INTERFACE
                           "${CMAKE_CURRENT_SOURCE_DIR}/tests/include")

target_link_libraries(rock_hero_ui_test_support INTERFACE rock_hero::ui)
```

This CMake shape is intentionally not final until the structure decision is made.

This matters for `rock_hero_audio_tests`, which currently includes audio public headers directly
and links only `rock_hero::core`, `rock_hero::build_policy`, and `Catch2::Catch2WithMain`. That
fast target should not start linking the concrete `rock_hero::audio` adapter library just because
it wants `FakeTransport`.

## Fakes to Extract First

### `FakeTransport`

Extract the repeated transport fakes from:

- `libs/rock-hero-audio/tests/test_transport.cpp`
- `libs/rock-hero-ui/tests/test_editor.cpp`
- `libs/rock-hero-ui/tests/test_editor_controller.cpp`
- `libs/rock-hero-ui/tests/test_editor_view.cpp`

The shared fake should model the project-owned transport contract:

- `play()` sets `current_state.playing = true` and records `play_call_count`.
- `pause()` sets `current_state.playing = false` and records `pause_call_count`.
- `stop()` sets `current_state.playing = false`, resets `current_position`, and records
  `stop_call_count`.
- `seek()` updates `current_position`, records `last_seek_position`, and records
  `seek_call_count`.
- `state()` returns `current_state`.
- `position()` returns `current_position`.
- `addListener()` and `removeListener()` store non-owning listener pointers by identity.
- `setStateAndNotify()` lets tests drive coarse transport callbacks deterministically.
- Seeking should not notify listeners, matching the current design.

Use simple public state initially because the current tests intentionally treat the fake as a
deterministic test fixture. Add encapsulation later only when invariants become difficult to
maintain.

### `CapturingTransportListener`

Extract the listener recorder from `libs/rock-hero-audio/tests/test_transport.cpp`.

Keep it narrow:

- latest received `TransportState`
- callback count

Do not merge this with `TransportNotificationRecorder` from `test_engine.cpp` unless the engine
integration tests and port-contract tests converge on the same observation needs.

### `FakeEdit`

Extract the edit fake from:

- `libs/rock-hero-audio/tests/test_edit.cpp`
- `libs/rock-hero-ui/tests/test_editor.cpp`
- `libs/rock-hero-ui/tests/test_editor_controller.cpp`

The shared fake should support:

- configured `next_create_track_result`
- configured `next_audio_asset_duration`
- configured `next_create_audio_clip_result`
- recorded `last_created_track_id`
- recorded `last_created_track_name`
- recorded `last_track_id`
- recorded `last_audio_clip_id`
- recorded `last_audio_asset`
- recorded `last_position`
- `create_track_call_count`
- `create_audio_clip_call_count`
- optional `during_edit_action` callback for reentrant transport notification tests

This fake is stateful and useful. It should remain a fake, not a Trompeloeil mock, for tests that
need to simulate accepted/rejected backend track and clip creates.

### `FakeThumbnail` and `FakeThumbnailFactory`

Extract repeated thumbnail test doubles from:

- `libs/rock-hero-ui/tests/test_editor.cpp`
- `libs/rock-hero-ui/tests/test_editor_view.cpp`
- `libs/rock-hero-ui/tests/test_track_view.cpp`

Keep them focused on observable thumbnail behavior:

- last assigned `AudioAsset`
- source-assignment call count
- proxy-generation flag
- proxy progress
- loaded length
- factory-created thumbnail count
- pointer/reference to last created thumbnail where tests need to inspect it

Do not add JUCE drawing behavior to these fakes.

### `FakeEditorView`

Extract from `libs/rock-hero-ui/tests/test_editor_controller.cpp`.

This fake should remain a simple view-state sink:

- latest `EditorViewState`
- `set_state_call_count`

It is useful for controller behavior tests because the behavior under test is the final derived
view state, not merely that `setState()` was called.

### `FakeEditorController`

Extract from:

- `libs/rock-hero-ui/tests/test_editor_controller.cpp`
- `libs/rock-hero-ui/tests/test_editor_view.cpp`

This fake should record user intents emitted by views:

- audio-load requests
- play/pause presses
- stop presses
- waveform click positions

Keep this as a fake because view tests care about the captured intent values.

## Fakes to Leave Local Initially

Leave these local unless duplication increases:

- `FakeTransportControlsListener` in `test_transport_controls.cpp`
- `FakeTrackViewListener` in `test_track_view.cpp`
- `TransportNotificationRecorder` in `test_engine.cpp`

These are currently small, subject-specific probes. Centralizing them now may make the support
library noisier without reducing meaningful duplication.

## Refactor Order

This order is only valid after the structure decision is finalized.

1. Record the chosen folder, namespace, include, and target strategy in this document.
2. Add the selected support target or targets.
3. Extract `FakeTransport` and `CapturingTransportListener`.
4. Refactor `test_transport.cpp` to consume the shared transport fake first.
5. Refactor UI tests that only need `FakeTransport`.
6. Extract `FakeEdit`.
7. Refactor audio edit tests and editor-controller tests to consume the shared edit fake.
8. Extract thumbnail fakes and refactor editor/editor-view/track-view tests.
9. Extract UI intent/view fakes and refactor editor-controller/editor-view tests.
10. Re-run the fast audio, core, and UI test targets after each extraction phase.

Keep each phase mechanically small. The expected test behavior should not change during the fake
centralization pass.

## Trompeloeil Introduction

Add Trompeloeil through Conan only when the first real interaction-heavy test is ready. Do not add
it just to prove the dependency works.

Implementation steps:

1. Add the current Conan Center Trompeloeil package to `conanfile.txt`.
2. Let `CMakeDeps` generate the package config.
3. Add `find_package(trompeloeil REQUIRED)` only in the test CMake scope that needs mocks, or in
   the selected support CMake scope if reusable mock helpers become shared.
4. Link only the targets that use Trompeloeil.
5. Include the Catch2 adapter in mock-using test files:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp>
```

Prefer local mock classes in the test file until a mock is repeated enough to justify sharing it.
Mocks usually encode interaction shape, so they are less inherently reusable than fakes.

If reusable mocks become useful later, create a separate opt-in target:

```text
rock_hero::<module>_test_mocks
```

That target may link the matching support target and Trompeloeil:

```text
rock_hero::<module>_test_support
trompeloeil::trompeloeil
```

Do not make all test targets link Trompeloeil by default.

## When to Use Trompeloeil

Use Trompeloeil for tests where one of these is the behavior:

- a collaborator must not be called
- a collaborator must be called exactly once or exactly N times
- calls must occur in a specific order
- a failure must trigger a specific escalation path
- a listener must be registered/unregistered with exact object identity
- a dangerous backend mutation must be prevented

Do not use Trompeloeil for tests that are better expressed as state/output checks:

- chart rules
- timing math
- transport position simulation
- score calculation
- editor state derivation
- fake-backed success/failure flows where final state is the important result

## Candidate First Trompeloeil Tests

Do not force these conversions, but these are plausible first candidates:

- Verify an invalid editor load request never calls `IEdit::createAudioClip()`.
- Verify a failed backend edit does not commit session state and does not issue unrelated
  transport commands.
- Verify listener registration lifetimes if a future class must add/remove an exact listener
  object during construction/destruction or attach/detach.
- Verify retry or failure-escalation behavior when future repository/plugin-host ports are added.

Current controller tests using call counters are still acceptable. Convert them only when a strict
mock makes the behavior clearer than the fake.

## Documentation Updates

After implementation, update the durable design docs, not just this todo:

- `docs/design/architecture.md`
  - mention the selected test-support folder strategy
  - mention Catch2 + Trompeloeil as the intended testing stack
- `docs/design/architectural-principles.md`
  - keep the current fake-first policy
  - add the exact Trompeloeil-use criteria from this plan
- `docs/design/coding-conventions.md`
  - add include and naming conventions for the selected test-support namespace
  - document whether shared test-support headers require Doxygen or regular comments

## Verification Plan

Preferred verification after each phase:

```sh
cmake --preset debug
cmake --build --preset debug --target rock_hero_audio_tests
cmake --build --preset debug --target rock_hero_ui_tests
cmake --build --preset debug --target rock_hero_core_tests
ctest --preset debug
```

In the current Codex environment, full CMake builds may be unreliable because of the known
`VerifyGlobs.cmake` hang. If that environment problem is still present during implementation,
verify with focused compile commands where possible and rely on CI for the full preset build/test
pass.

## Definition of Done

- The folder, namespace, include, and linking strategy is explicitly decided before extraction
  begins.
- Reusable fakes live under the selected test-support layout.
- Test files no longer duplicate the extracted fake types.
- Fast audio tests remain free of concrete Tracktion/JUCE adapter linkage.
- UI tests still link the concrete UI library and use support fakes only for collaborator seams.
- Trompeloeil is linked only by tests that actually use mock expectations.
- No production target depends on test-support code.
- Design docs record the fake-first, mock-when-needed policy.
