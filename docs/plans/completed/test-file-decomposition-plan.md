# Test File Decomposition Plan

Status: completed.

Completion note: implemented as a test-only decomposition. The editor-controller and editor-view
test cases were split by behavior, and the extracted shared helpers were placed under the existing
module-owned `*_testing` include roots rather than introducing a separate private `support/`
include convention. The affected targets plus full debug CTest passed. Runtime structure pressure
revealed by the split is recorded in
`docs/plans/in-progress/editor-runtime-structure-pressure-findings.md`.

## Purpose

Split the remaining oversized editor test files by behavior now that reusable test helpers have
been extracted into module-owned testing targets.

This is a test-structure cleanup first. It should make tests easier to scan and maintain without
changing runtime behavior. If the split exposes production design pressure, record that pressure and
handle it through the existing runtime architecture plans instead of mixing runtime refactors into
the test-file move.

## Current Position

The shared helper cleanup is complete:

- `rock_hero::common::audio_testing` owns reusable common-audio fakes;
- `rock_hero::editor::core_testing` owns reusable editor-core fakes;
- `rock_hero::editor::ui_testing` owns reusable JUCE test helpers.

The largest remaining issue is not duplicated shared fakes. It is that
`test_editor_controller.cpp` and `test_editor_view.cpp` cover many behavior clusters in single
translation units. Those files are hard to scan even after helper extraction.

## Scope

Primary targets:

- `rock-hero-editor/core/tests/test_editor_controller.cpp` (currently 5253 lines)
- `rock-hero-editor/ui/tests/test_editor_view.cpp` (currently 1521 lines)

Secondary targets only if direct duplication appears while splitting:

- other `rock-hero-editor/core/tests/test_*.cpp` files;
- other `rock-hero-editor/ui/tests/test_*.cpp` files.

Explicitly out of scope (do not bundle):

- `rock-hero-editor/ui/tests/test_editor.cpp` — 285-line construction/wiring smoke test. It still
  declares local `FakeTransport`, `FakeSongAudio`, `FakeThumbnail`, `FakeThumbnailFactory`, and
  `FakeEditorAudioPorts` even though shared versions now exist in `common::audio_testing`. Leaving
  it alone is deliberate: it is small, single-purpose, and the local fakes encode the exact
  minimal-construction contract under test. Migrating it to the shared helpers should be a
  separate slice if pursued at all, not a piggyback on this decomposition.
- production refactors;
- controller workflow extraction;
- public API changes;
- Trompeloeil adoption;
- new cross-target testing targets;
- moving single-use helpers into shared testing targets.

## Principles

- Split by the first meaningful subject under test, not by line count.
- Keep test names within the existing 78-character Catch2 limit.
- **TEST_CASE names and tags must not change during the split.** CI history, flakiness tracking,
  bisect logs, and grep workflows all key off the existing names. Move the body, keep the title
  and tags byte-identical.
- Keep one-off scaffolding local unless at least two test files need it.
- Put cross-target reusable helpers in the module-owned `*_testing` target.
- Put same-test-executable scaffolding in a private test support folder, not in public testing
  targets.
- Do not hide test intent behind a giant all-purpose fixture.

### Harness scope bound

To prevent the private harness from turning into a second controller, hold it to these limits:

- exposes no more than ~8 public methods or factory helpers;
- no harness method composes more than one production controller call into a multi-step recipe;
- no harness method takes a callback for the test body to run inside;
- assertions stay in the test body, never inside the harness;
- if the harness grows past these bounds, that is evidence of runtime design pressure — record
  it per Phase 5, do not paper over it by adding more knobs.

## Testing Target Helper Shape

The final implementation uses the existing module-owned `*_testing` targets instead of adding a
second private `support/` convention. This keeps extracted common test helpers discoverable through
the same include-path pattern already used by `NullEditorSettings`, `RecordingEditorController`,
and `component_test_helpers`.

Final helper layout:

```text
rock-hero-editor/core/tests/include/rock_hero/editor/core/testing/
  editor_controller_test_harness.h

rock-hero-editor/ui/tests/include/rock_hero/editor/ui/testing/
  editor_view_test_harness.h
```

The CMake pattern, applied to `rock-hero-editor/core/tests/CMakeLists.txt`:

```cmake
add_executable(
    rock_hero_editor_core_tests
    test_audio_device_settings_controller.cpp
    test_audio_device_status_text.cpp
    test_editor_controller_state.cpp
    test_editor_controller_plugins.cpp
    test_editor_controller_project_lifecycle.cpp
    test_editor_controller_transport.cpp
    test_editor_controller_busy.cpp
    test_editor_controller_input_calibration.cpp
    test_editor_controller_output_gain.cpp
    test_editor_controller_restore.cpp
    test_project.cpp
    test_editor_settings.cpp)

target_include_directories(
    rock_hero_editor_core_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
```

Split test files include the shared helpers through the module testing include root:

```cpp
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
```

The UI harness constructs private concrete editor-view types, so `rock_hero_editor_ui_testing`
intentionally exposes the UI `src/` include directory to tests that opt into that target. This is a
test-only convenience and does not make those headers production API.

The harnesses should own only repeated setup that would otherwise be copied into several split
files. They should not become second controllers. Prefer small named helpers over one large object
with dozens of knobs.

Good candidates for private controller support:

- construction of `EditorController::AudioPorts`;
- construction of required service bundles and project-operation fakes;
- repeated loaded-project setup;
- common accessors for the latest published `EditorViewState`;
- deferred task control if multiple split files need it.

Good candidates for private view support:

- construction of `EditorView` with shared audio ports;
- repeated state builders for common render states;
- common top-level component lookup only after reuse appears.

Keep behavior-specific setup next to the test that uses it.

## Phase 1: Inventory Current Behavior Groups

Every existing TEST_CASE is mapped to a proposed destination below. Line numbers reflect the tree
at the time of writing and may drift; the TEST_CASE titles are authoritative for matching. Move
each case verbatim — title and tags must remain byte-identical so CI history stays continuous.

### `test_editor_controller.cpp` → eight split files

**`test_editor_controller_state.cpp`** — view-state defaults, projection, attachment,
listener-driven refresh, and coalescing.

This file may create a loaded project when that is the simplest way to observe projected state, but
it should not own project open/save/import/restore success and failure policy. Transport appears
only as a notification source for state publication, not as play/stop/seek behavior.

- L1177 `EditorViewState represents one arrangement`
- L1296 `IEditorController fake receives editor intents`
- L1359 `EditorController publishes current audio device`
- L2149 `EditorController re-derives state on device change`
- L2185 `EditorController pushes derived state on view attachment`
- L2225 `EditorController derives visible timeline range`
- L2258 `EditorController pushes one state per coarse transition`
- L3662 `EditorController coalesces reentrant audio callbacks`
- L3703 `EditorController does not replay errors across transitions`

**`test_editor_controller_plugins.cpp`** — plugin browser catalog, add/remove flow, plugin
windows, live-rig load and capture.

- L1393 `EditorController enables plugin add after load`
- L1461 `EditorController opens plugin browser catalog`
- L1498 `EditorController rescans plugin browser catalog`
- L1546 `EditorController adds a browser plugin`
- L1591 `EditorController keeps plugin browser open after add error`
- L1633 `EditorController closes plugin browser`
- L1664 `EditorController reports plugin catalog scan errors`
- L1703 `EditorController loads live rig on open`
- L1744 `EditorController reports live rig plugin load progress`
- L1806 `EditorController close during live rig load supersedes open`
- L1864 `EditorController captures live rig before save`
- L1912 `EditorController plugin add marks tone dirty`
- L1949 `EditorController removes a plugin`
- L1990 `EditorController ignores stale plugin removal`
- L2022 `EditorController opens plugin windows`
- L2051 `EditorController ignores stale plugin window requests`
- L2079 `EditorController reports plugin window errors`
- L2112 `EditorController reports plugin remove errors`

**`test_editor_controller_transport.cpp`** — play/stop/seek/waveform-click behavior only.

- L2306 `EditorController play intent toggles loaded transport`
- L2332 `EditorController ignores play intent without audio`
- L2347 `EditorController stop intent follows reset gate`
- L2376 `EditorController stop intent refreshes paused reset state`
- L2409 `EditorController waveform click clamps and scales`
- L2440 `EditorController waveform click refreshes stop state`

**`test_editor_controller_project_lifecycle.cpp`** — open/close/save/save-as/publish/import paths
and the unsaved-import prompt flow. Excludes restore (its own file) and busy routing.

- L1433 `EditorController forwards normalization to audio backend`
- L2476 `EditorController failed activation preserves session`
- L2519 `EditorController successful open stores audio`
- L2576 `EditorController close clears loaded project`
- L2993 `EditorController persists project file on exit`
- L3026 `EditorController save writes current session song`
- L3064 `EditorController save failure surfaces an error`
- L3099 `EditorController save as failure clears busy first`
- L3135 `EditorController publish writes package copy`
- L3174 `EditorController publish failure surfaces an error`
- L3215 `EditorController failed import preserves session`
- L3261 `EditorController successful import stores audio`
- L3318 `EditorController import requires Save As destination`
- L3382 `EditorController prompts before closing unsaved import`
- L3431 `EditorController discard import reopens dirty displaced project`
- L3502 `EditorController saves prompted import before close`
- L3553 `EditorController prompts before exit with unsaved import`
- L3598 `EditorController defaults open to first arrangement`
- L3625 `EditorController rejects invalid project arrangement audio`

**`test_editor_controller_restore.cpp`** — restore-on-startup, interrupted-restore prompt, and
the matching "clear restore path on X" cases. This group is large enough on its own that bundling
it into project_lifecycle would defeat the split.

- L2617 `EditorController clears missing restore path`
- L2642 `EditorController restores valid last project`
- L2671 `EditorController restore keeps path while open is pending`
- L2707 `EditorController exit during pending restore marks interrupted`
- L2750 `EditorController prompts after interrupted restore`
- L2783 `EditorController retries interrupted restore prompt`
- L2825 `EditorController cancels interrupted restore prompt`
- L2859 `EditorController clears missing interrupted restore`
- L2890 `EditorController clears restore path when open fails`
- L2917 `EditorController restore clears path after async failure`
- L2954 `EditorController restore prompts for unsaved changes`

**`test_editor_controller_busy.cpp`** — busy-message routing, supersede semantics, async
completion stages, deferred-task scheduling.

- L3735 `EditorController open begins busy with default message`
- L3767 `EditorController open reports audio analysis state`
- L3797 `EditorController import begins busy with default message`
- L3826 `EditorController import reports audio analysis state`
- L3856 `EditorController save begins busy with default message`
- L3890 `EditorController deferred save clears busy before open`
- L3970 `EditorController save as begins busy with default message`
- L4004 `EditorController publish begins busy with default message`
- L4038 `EditorController busy routing disables ordinary commands`
- L4075 `EditorController busy keeps close enabled for a loaded project`
- L4113 `EditorController busy routing blocks direct commands`
- L4192 `EditorController open completion clears busy and commits`
- L4225 `EditorController failed open clears busy then reports error`
- L4262 `EditorController close during busy supersedes open`
- L4302 `EditorController exit during busy supersedes open`
- L4339 `EditorController close during busy save supersedes write`
- L4384 `EditorController exit during busy save persists file`
- L4426 `EditorController stale completion preserves live busy state`
- L4476 `EditorController prepareSong runs on message-thread completion stage`
- L4509 `EditorController schedules audio device open via paint fence`

**`test_editor_controller_output_gain.cpp`** — output-gain controls, persistence, clamping,
reset.

- L4544 `Output gain controls enabled with live rig and arrangement`
- L4575 `Output gain controls enabled with required live rig`
- L4692 `Output gain change calls live rig and marks dirty`
- L4957 `Output gain changes clamp to valid range`
- L4984 `Output gain restored from load result`
- L5222 `Output gain resets on project close`

**`test_editor_controller_input_calibration.cpp`** — input calibration, monitoring, route
rollback. Includes the two "no input device" / "missing calibration" cases that anchor the gating
behavior tested in this file.

- L4600 `Signal chain reports no input device before missing calibration`
- L4634 `Missing input calibration disables live input until manually requested`
- L4722 `Input calibration success stores app-local gain and enables monitoring`
- L4775 `Input calibration retry resets committed gain before measuring`
- L4827 `Input calibration start restores route on gain failure`
- L4870 `Input calibration start restores route on monitor failure`
- L4913 `Manual input calibration stores gain and enables monitoring`
- L5011 `Stored input calibration stays disabled until live rig load completes`
- L5059 `Input route change clears calibration and disables monitoring`
- L5111 `Manual input recalibration dismissal restores previous calibration`
- L5171 `Manual input recalibration cancellation restores previous calibration`

### `test_editor_view.cpp` → six split files

**`test_editor_view_state.cpp`** — projection of view-state into controls, thumbnail/arrangement
input, plugin intents.

- L328 `EditorView applies arrangement audio to the thumbnail`
- L382 `EditorView setState projects controls without polling position`
- L494 `EditorView emits plugin remove intents`
- L536 `EditorView emits plugin open intents`
- L569 `EditorView projects audio device menu button state`

**`test_editor_view_layout.cpp`** — menu strip, toolbar, default viewport layout, resize
behavior. The single MenuBarButton unit test lives here too (see note below).

- L599 `MenuBarButton preferred width grows with label text`
- L616 `EditorView lays out menu strip actions without overlap`
- L641 `EditorView lays out toolbar below the menu bar`
- L669 `EditorView lays out the default track viewport`
- L876 `EditorView stop reset snaps track viewport to start`
- L910 `EditorView keeps waveform track fixed on resize`
- L934 `EditorView keeps zoomed cursor width on larger viewport`

**`test_editor_view_timeline.cpp`** — zoom mapping, wheel zoom, cursor geometry, timeline click
routing, space-key handling.

- L697 `EditorView default zoom maps ten seconds`
- L725 `EditorView wheel zoom scales track width`
- L754 `EditorView wheel zoom out fits full timeline`
- L783 `EditorView wheel zoom centers visible cursor`
- L830 `EditorView wheel zoom centers offscreen cursor`
- L963 `EditorView forwards timeline clicks to the controller`
- L1024 `EditorView forwards space key to the controller`
- L1038 `EditorView cursor geometry maps position through visible range`

**`test_editor_view_busy_overlay.cpp`** — busy overlay visibility and the paint-fence callback
contract.

- L1097 `EditorView shows the busy overlay while state.busy is set`
- L1165 `EditorView runs busy callback after overlay paint`
- L1205 `EditorView runs busy callback when hidden`

**`test_editor_view_signal_chain.cpp`** — signal-chain controls, meters, gate-driven enablement.

- L1236 `Signal-chain controls present and disabled by default`
- L1261 `EditorView creates audio meter components`
- L1279 `Signal chain meters sit with their controls`
- L1305 `EditorView samples audio meter source`
- L1332 `Signal-chain controls follow view-state gates`

**`test_editor_view_audio_controls.cpp`** — calibration prompt and output gain control widgets.

- L1359 `Input calibration button emits controller intent`
- L1382 `Calibration prompt starts with target and status`
- L1442 `Calibration gain control hides negative rounded zero`
- L1466 `Manual calibration stays editable after saving`
- L1499 `Output gain slider emits controller intent`

### MenuBarButton

There is no separate `test_menu_bar_button.cpp` today; the single MenuBarButton case lives inline
in `test_editor_view.cpp:599`. Move it into `test_editor_view_layout.cpp` rather than promoting
it to its own file. If MenuBarButton coverage grows beyond the current single case, the right
moment to give it a dedicated file is when that growth happens, not as part of this split.

### Anonymous-namespace utility strategy

`test_editor_controller.cpp` and `test_editor_view.cpp` each have a large anonymous-namespace
header above the test cases — fakes, builders, helpers. The split workflow is:

1. **First slice:** seed `editor_controller_test_harness.h` in the owning module's `*_testing`
   include root (or the view equivalent) using the Phase 2 harness-seeding rule. Helpers used by
   only one destination file move into that destination file's own anonymous namespace.
2. **Subsequent slices:** if a helper that is currently local to one split file gets reused by a
   later split file, promote it to the harness then. Do not pre-promote on speculation.
3. **Local copies are temporarily acceptable** during a slice in progress — duplication for one
   commit is better than over-extracting a harness method that turns out to fit only one caller.

Concretely, the current local fakes in `test_editor_controller.cpp` resolve as follows:

- `FakeEditorView` (L58) — used by nearly every group. Move into the harness as the default sink.
- `FakeTransport` (L179, combined `ITransport`+`ILiveInput`) — used by transport, plugins
  (live-rig load), input calibration, and several lifecycle cases. Move into the harness. Do not
  share with `common::audio_testing` — the combined-interface shape is editor-controller-specific.
- `FakeLiveRig` (L350) — used by plugins, live-rig load, input calibration, output gain. Move
  into the harness.
- `FakeProjectServices` (L657) — used across plugins, transport, project lifecycle, restore, busy,
  output gain, input calibration, and state-projection tests. Move into the private controller
  harness from the first controller split. It represents test-owned project-operation wiring for the
  controller test executable, not a cross-target helper.

Exit criteria:

- every TEST_CASE in the two primary files is listed above with an assigned destination;
- no destination file ends up with fewer than three cases — if a proposed file would be that
  small, merge it into the nearest behavior neighbor before starting Phase 3;
- no destination is named only after line count.

## Phase 2: Introduce Private Test Harnesses

Seed the harness from the Phase 1 inventory, not from speculation. Two categories of helper go in
from the first slice:

1. Helpers needed by the first slice itself.
2. Helpers the Phase 1 inventory shows are reused by at least three later behavior groups, even
   if the first two slices do not exercise them. The four currently named fakes
   (`FakeEditorView`, `FakeTransport`, `FakeLiveRig`, `FakeProjectServices`) qualify under this
   rule and belong in the harness from slice 1.

Helpers discovered mid-slice that were not inventoried follow the strict rule: keep them local to
the first split file that needs them, and promote them to the harness only when a second split
file actually needs the same helper. The pre-promotion allowance is anchored in the inventory, not
in mid-slice guesses.

Rules:

- seed the harness per the two categories above;
- keep public `*_testing` helpers as the first choice for reusable port fakes;
- avoid a harness method for every controller or view action;
- keep assertions in the test body — the harness exposes accessors and setup only;
- respect the harness scope bound under [Principles](#harness-scope-bound).

Exit criteria:

- split files can share required setup without copy-paste;
- local behavior remains readable from the test body;
- harness contents are traceable back to either the slice that needed them or the Phase 1
  inventory entry that justified pre-promotion;
- no production source file changed.

## Phase 3: Split Editor Controller Tests

Move one behavior group at a time, in the order below, updating
`rock-hero-editor/core/tests/CMakeLists.txt` after each slice. The destination files and case
assignments are fixed by the Phase 1 inventory; this phase is mechanical execution.

Recommended slice order, chosen so harness extraction happens against the smallest, most-isolated
group first:

1. `test_editor_controller_state.cpp` — smallest harness footprint; flushes out the minimum
   default-setup the harness needs.
2. `test_editor_controller_transport.cpp` — small, behavior-bounded; second consumer of the
   harness, which justifies the first round of promotions.
3. `test_editor_controller_output_gain.cpp` — small, mostly arrangement-level state.
4. `test_editor_controller_plugins.cpp` — first large slice; exercises live-rig setup.
5. `test_editor_controller_input_calibration.cpp` — depends on live-rig setup already promoted in
   slice 4.
6. `test_editor_controller_busy.cpp` — largest single slice; lifts deferred-task plumbing into
   the harness if it isn't already there.
7. `test_editor_controller_project_lifecycle.cpp` — done late because it is the broadest file-IO
   slice, and benefits from the settled project-operation harness introduced by earlier slices.
8. `test_editor_controller_restore.cpp` — done last; the restore-prompt flow rides on the same
   project-operation harness exercised in slice 7, so this slice mostly moves cases rather than
   growing the harness further.

Ship slices 7 and 8 as separate commits / PRs to keep each diff reviewable.

If during a slice a proposed file ends up with only one or two small cases, merge it into the
nearest behavior neighbor instead of shipping a near-empty file. The Phase 1 inventory makes that
unlikely, but the rule still applies.

After each moved group:

- build `rock_hero_editor_core_tests`;
- run `rock_hero_editor_core_tests`;
- confirm Catch2 discovery still reports every moved case under its unchanged title and tags
  (diff the full `--list-tests` output, not just the test count).

## Phase 4: Split Editor View Tests

Move one behavior group at a time, in the order below, updating
`rock-hero-editor/ui/tests/CMakeLists.txt` after each slice.

Recommended slice order:

1. `test_editor_view_state.cpp` — smallest, exercises only `setState` projection.
2. `test_editor_view_busy_overlay.cpp` — three cases; isolates the paint-fence contract.
3. `test_editor_view_signal_chain.cpp` — bounded, no timeline dependencies.
4. `test_editor_view_audio_controls.cpp` — calibration prompt + output gain widgets.
5. `test_editor_view_layout.cpp` — includes the inline `MenuBarButton` case from L599; no
   separate menu-bar-button file is created.
6. `test_editor_view_timeline.cpp` — last because it has the densest mouse-event / zoom helpers.

After each moved group:

- build `rock_hero_editor_ui_tests`;
- run `rock_hero_editor_ui_tests`;
- confirm Catch2 discovery still reports every moved case under its unchanged title and tags;
- confirm `findRequiredDescendant<T>` and `makeMouseDownEvent` from `editor::ui_testing` still
  cover every split file's lookup/mouse needs — if a split file has to reintroduce a local
  finder, that is a signal the shared helper is missing a variant.

## Phase 5: Reassess Runtime Pressure

After the test-only split, review whether the new test files reveal production responsibility
pressure.

Runtime refactor pressure is real only if one of these appears:

- several behavior files need unrelated setup just to construct `EditorController`;
- a single test must arrange many unrelated subsystems to assert one behavior;
- private test harnesses start mirroring production workflow clusters;
- tests cannot name a behavior without referencing several unrelated controller responsibilities.

If that happens, do not patch around it with a larger harness. Record the finding against the
runtime architecture plans, especially:

- `docs/plans/in-progress/editor-controller-root-facade-plan-v2.md`;
- `docs/plans/in-progress/editor-core-framework-isolation-plan.md`.

## Verification Plan

Run focused tests after each slice:

```powershell
cmd.exe /d /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && ninja -C build/debug rock_hero_editor_core_tests rock_hero_editor_ui_tests'
& 'build/debug/rock-hero-editor/core/tests/rock_hero_editor_core_tests.exe'
& 'build/debug/rock-hero-editor/ui/tests/rock_hero_editor_ui_tests.exe'
```

Run full `ctest --preset debug` after the final split or after any CMake target graph change that
could affect unrelated tests.

## Definition Of Done

- `test_editor_controller.cpp` no longer contains unrelated controller behavior clusters.
- `test_editor_view.cpp` no longer contains unrelated view behavior clusters.
- shared test helpers remain in module-owned `*_testing` targets.
- private test support is used only for same-executable repeated setup.
- private test harnesses respect the scope bound under Principles (≤8 public helpers, no
  multi-step recipes, no in-harness assertions, no test-body callbacks).
- no production code changes were required for the test-file split.
- any runtime design pressure is recorded as a separate follow-up, not hidden in test scaffolding.
- affected test targets and full CTest pass.
