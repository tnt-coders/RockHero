# v20 Stage 01 - Core Session Model

## Goal

Add the pure core data needed by later UI and audio stages without touching JUCE,
Tracktion, or editor UI code.

## Expected Files

- `libs/rock-hero-core/include/rock_hero/core/audio_asset.h`
- `libs/rock-hero-core/include/rock_hero/core/timeline.h`
- `libs/rock-hero-core/include/rock_hero/core/track.h`
- `libs/rock-hero-core/include/rock_hero/core/arrangement.h`
- `libs/rock-hero-core/include/rock_hero/core/session.h`
- `libs/rock-hero-core/src/session.cpp`
- `libs/rock-hero-core/tests/test_session.cpp`
- `libs/rock-hero-core/tests/test_song.cpp`
- `libs/rock-hero-core/CMakeLists.txt`
- `libs/rock-hero-core/tests/CMakeLists.txt`

## Implementation Steps

1. Add `core::AudioAsset` as a framework-free value type. Start with a filesystem
   path or path-like value consistent with the current codebase.
2. Add `core::TimePosition` and `core::TimeDuration` in `timeline.h`. Each type
   should be a small semantic wrapper around a `double seconds` member.
3. Update `core::NoteEvent` to use `TimePosition position` and
   `TimeDuration duration` instead of raw `double` timing fields.
4. Add `core::TrackId`, `core::Track`, and `core::Session`.
5. Keep `TrackId` and `Track` in `track.h`; `Session` should include that header
   rather than becoming a catch-all model header.
6. Keep `Track` role-free. Include only `id`, `name`, and `audio_asset`.
7. Add `Session::tracks()`, `Session::findTrack(...)`, `Session::addTrack(...)`,
   and `Session::replaceTrackAsset(...)`.
8. Make `Session::addTrack(...)` return the newly assigned `TrackId`.
9. Make `replaceTrackAsset(...)` return `false` for a missing track.

## Tests

Add core tests for:

- adding a track creates a stable nonzero `TrackId`,
- the id returned by `addTrack(...)` can be used to find the inserted track,
- time value wrappers preserve second values,
- note events use `position` and `duration` time value types,
- adding an empty track is valid,
- tracks preserve insertion order,
- replacing a track asset updates only that track,
- replacing a missing track asset fails cleanly,
- tracks do not require roles.

## Verification

Run the core test target if available. If the local CMake environment is still
unreliable, compile the touched core source and test files from the existing compile
commands and record the limitation.

## Exit Criteria

- `rock-hero-core` remains standard C++ only.
- No UI, JUCE, Tracktion, or audio engine headers are included by the new core
  headers.
- Later stages can construct a `Session` with one empty track or one track with an
  audio asset.

## Do Not Do

- Do not add undo/redo.
- Do not add track roles.
- Do not add mute, gain, selection, or other track controls until there is
  implemented behavior that needs them.
- Do not move editor workflow into core.
- Do not add persistence behavior.
