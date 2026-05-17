# Editor Busy Overlay Current Status

Last updated: 2026-05-16

## Quick Recall

The editor busy-overlay work is still uncommitted and currently reflects the narrowed direction
chosen after the plugin-load animation investigation.

Completed:

- Slice 1: project open/import busy overlay, editor task runner, stale completion handling.
- Slice 1A: action routing hardening, Close/Exit supersede behavior, startup restore cleanup,
  and consistent error ordering after busy clears.
- Slice 2: Save, Save As, and Publish now run through busy state, the task runner,
  `ProjectCommand`, stale completion checks, and focused controller tests.
- Current plugin-add decision: one-off Add Plugin is back to the direct synchronous scan/add path
  with no busy overlay and no busy-overlay paint fence.
- Current project-load progress decision: `BusyViewState` supports optional determinate progress,
  and `BusyOverlay` renders a percentage bar when that progress is present.

Current head when this note was written:

- `3288bd50 Routed project writes through busy commands`

Current working tree includes uncommitted busy/progress files:

- `rock-hero-editor/core/include/rock_hero/editor/core/busy_view_state.h`
- `rock-hero-editor/core/include/rock_hero/editor/core/i_editor_view.h`
- `rock-hero-editor/core/src/busy_view_state.cpp`
- `rock-hero-editor/core/src/editor_controller.cpp`
- `rock-hero-editor/core/tests/test_editor_controller.cpp`
- `rock-hero-editor/ui/include/rock_hero/editor/ui/busy_overlay.h`
- `rock-hero-editor/ui/include/rock_hero/editor/ui/editor_view.h`
- `rock-hero-editor/ui/src/busy_overlay.cpp`
- `rock-hero-editor/ui/src/editor_view.cpp`
- `rock-hero-editor/ui/tests/test_editor_view.cpp`

Primary plan file:

- `docs/in-progress/editor-busy-overlay-plan.md`

## Current Design Shape

`EditorController` has a private action router. Controller entry points build `EditorAction`
values, and the router applies the busy policy before executing the action body.

`ProjectCommand` is the private project-level command vocabulary for Close, Open, Import, Save,
Save As, Publish, and Exit. Prompt state exposes only `ProjectCommandId` to the view.

Project writes share one path:

- `runProjectCommand(ProjectCommand::save())`
- `runProjectCommand(ProjectCommand::saveAs(std::move(file)))`
- `runProjectCommand(ProjectCommand::publish(std::move(file)))`

Package writes move `Project` ownership into `ProjectWriteTaskState`, run filesystem work through
the editor task runner, then restore and finalize controller state on the message thread.

Open and Import currently begin with animated indeterminate busy state. Open-project plugin restore
is not implemented yet. When it is added, the intended shape is to keep initial project package IO
indeterminate, then switch the same busy state to determinate progress once the controller knows the
plugin count. Each plugin step can update the message, for example `Loading plugin 2 of 5: Amp Sim`,
and set `BusyViewState::progress` to the loaded-plugin fraction before entering the blocking
Tracktion/JUCE plugin instantiation step.

The user confirmed the animated overlay freezes for real plugin instantiation. Inspection shows the
visible stall is in Tracktion/JUCE plugin instantiation and edit insertion, not just scanner
discovery. Moving one-off Add Plugin into busy state was therefore backed out: the overlay would be
visible but frozen during the only part that matters.

Close and Exit remain available while busy. They supersede in-flight operations by advancing the
busy generation and clearing busy state. This discards stale completions; it does not interrupt a
worker thread that is already running.

Audio-device changes are explicitly not the next busy-overlay implementation. Slice 4 is now a
performance investigation and optimization slice. Only add device-change busy UI if measurement
shows optimized device switching still visibly stalls.

## Next Implementation Slice

Next target when persisted project plugins exist: restore project plugins during OpenProject using
the existing busy state, update the busy message and `progress` between plugin loads, and use the
paint fence only where a message-thread-bound plugin load needs the updated determinate state to
paint before blocking.

Moving the normal JUCE `BusyOverlay` to another thread is still not a good fit for this codebase:
JUCE components and painting are message-thread-owned. A separately animated native window could be
built, but that would add a second UI loop and platform-specific behavior while the main editor
remains blocked.

## Last Verified Commands

These passed for the current narrowed progress implementation:

```powershell
pre-commit run clang-format --files rock-hero-editor/core/include/rock_hero/editor/core/busy_view_state.h rock-hero-editor/core/include/rock_hero/editor/core/i_editor_view.h rock-hero-editor/core/src/busy_view_state.cpp rock-hero-editor/core/src/editor_controller.cpp rock-hero-editor/core/tests/test_editor_controller.cpp rock-hero-editor/ui/include/rock_hero/editor/ui/busy_overlay.h rock-hero-editor/ui/include/rock_hero/editor/ui/editor_view.h rock-hero-editor/ui/src/busy_overlay.cpp rock-hero-editor/ui/src/editor_view.cpp rock-hero-editor/ui/tests/test_editor_view.cpp
```

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.codex\skills\rockhero-build\scripts\rockhero-build.ps1 -Targets rock_hero_editor_core_tests,rock_hero_editor_ui_tests -RunTouchedTests
```

Focused clang-tidy passed for:

- `rock-hero-editor/core/src/busy_view_state.cpp`
- `rock-hero-editor/core/src/editor_controller.cpp`
- `rock-hero-editor/core/tests/test_editor_controller.cpp`
- `rock-hero-editor/ui/src/busy_overlay.cpp`
- `rock-hero-editor/ui/src/editor_view.cpp`
- `rock-hero-editor/ui/tests/test_editor_view.cpp`

Direct clang-tidy was run with `--extra-arg=-Wno-unused-command-line-argument` to suppress the
unused MSVC `/MP` flag diagnostic from the compile database.
