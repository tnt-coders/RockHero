# Test Support Cleanup Plan

Status: completed.

This plan's shared-helper extraction is implemented. The remaining large-test-file work has been
moved into `docs/plans/in-progress/test-file-decomposition-plan.md`, and optional mock-framework
adoption is tracked separately in `docs/plans/todo/trompeloeil-adoption-plan.md`.

## Completed Shape

The project now uses module-owned test-only helper targets instead of duplicating reusable fakes
inside every test file:

- `rock_hero::common::audio_testing`
- `rock_hero::editor::core_testing`
- `rock_hero::editor::ui_testing`

Each target keeps helper headers under the owning module's `tests/include/` tree and uses the
matching `rock_hero::<scope>::<module>::testing` namespace. Production targets do not link these
targets.

## Implemented Helpers

`rock_hero::common::audio_testing` owns reusable helpers for common-audio ports:

- `ConfigurableSongAudio`
- `RecordingPluginHost`
- `ConfigurableAudioDeviceConfiguration`
- `RecordingThumbnail`
- `RecordingThumbnailFactory`

`rock_hero::editor::core_testing` owns reusable helpers for editor-core contracts:

- `NullEditorSettings`
- `ImmediateEditorTaskRunner`
- `RecordingEditorController`

`rock_hero::editor::ui_testing` owns reusable JUCE test helpers:

- `findRequiredDescendant`
- `findRequiredDirectChild`
- `makeMouseDownEvent`

## Deferred Or Transferred Work

Large test-file splitting is no longer part of this completed cleanup. That work needs different
judgment: the shared helpers are already available, and the remaining question is how to split
large controller and view tests by behavior without hiding test intent behind a broad fixture.

The active follow-up is `docs/plans/in-progress/test-file-decomposition-plan.md`.

Trompeloeil adoption is also outside this completed cleanup. The project remains fake-first, and
Trompeloeil should be added only when a strict interaction test is genuinely needed. That future
decision is tracked by `docs/plans/todo/trompeloeil-adoption-plan.md`.

## Verification

The completed implementation was verified by building and running:

- `rock_hero_common_audio_tests`
- `rock_hero_editor_core_tests`
- `rock_hero_editor_ui_tests`
- full `ctest --preset debug`

All production behavior remains outside the testing targets.

## Durable Rules Confirmed

- Put reusable test helpers in the module that owns the production contract they implement.
- Prefer behavior-specific names such as `Recording*`, `Configurable*`, `Null*`, and `Immediate*`.
- Keep one-off listener probes and single-file scaffolding local.
- Do not create a global `rock_hero_testing` warehouse.
- Reserve `Mock*` names for future expectation-driven Trompeloeil types.
