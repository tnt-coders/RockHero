# v20 Stage 03 - Edit Port Scaffolding

## Goal

Introduce the first project-owned audio edit boundary without taking on undo/redo
implementation or broader clip-timeline scope yet.

## Expected Files

- `libs/rock-hero-audio/include/rock_hero/audio/i_edit.h`
- `libs/rock-hero-audio/tests/test_edit.cpp`
- `libs/rock-hero-audio/CMakeLists.txt`
- possible `libs/rock-hero-audio/tests/CMakeLists.txt`

## Implementation Steps

1. Add `audio::IEdit`.
2. Add one minimal mutation method representing the existing single-file editor
   behavior:
   `setTrackAudioSource(core::TrackId, const core::AudioAsset&) -> bool`.
3. Document that this interface is the first project-owned facade over the concrete
   Tracktion edit model. It is intentionally minimal in v20 and should not yet try
   to model all future clip, track, or automation edits.
4. Document that the first concrete implementation is still single-track-applied:
   - only the most recently set track source is required to behave correctly,
   - setting a second track id is unspecified until stem playback lands,
   - this is tied to the current `TransportState.duration` semantics.
5. Document that successful mutation updates the paired transport state synchronously
   through the normal transport boundary.
6. Document that transport listeners may naturally report any transport-visible state
   changes caused by a successful edit.
7. Add explicit TODO comments in the header where broader edit commands and a
   project-owned undo/history surface are likely to land later. Do not add
   `undo()`, `redo()`, or `historyState()` yet.

## Tests

Add an automated contract test with a tiny fake `IEdit` implementation. This proves
controller tests can use the port without Tracktion.

Cover:

- a fake implementation receives `core::TrackId`,
- a fake implementation receives `core::AudioAsset`,
- return values can represent success and failure,
- the test includes only public core/audio port headers, not concrete `Engine` or
  Tracktion headers.

## Verification

Run the new audio port test if possible. If local CMake remains unreliable, compile
the test source and touched headers through a focused compile path and record the
missing full test run.

## Exit Criteria

- Audio mutation is represented by `IEdit`, not `ITransport`.
- The header contains explicit TODO comments for future edit-surface expansion and
  future project-owned undo/history integration.
- Existing editor behavior still builds through old `Engine::loadFile(...)`.

## Do Not Do

- Do not add a thumbnail factory.
- Do not implement undo/redo.
- Do not implement multi-stem playback.
- Do not remove `Engine::loadFile(...)` yet.
