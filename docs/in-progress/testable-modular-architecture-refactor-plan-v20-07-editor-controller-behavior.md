# v20 Stage 07 - Editor Controller Behavior

## Goal

Implement the headless editor workflow coordinator and prove the central workflow
with fakes.

## Expected Files

- `libs/rock-hero-ui/include/rock_hero/ui/controllers/editor_controller.h`
- `libs/rock-hero-ui/src/controllers/editor_controller.cpp`
- `libs/rock-hero-ui/tests/test_editor_controller.cpp`
- `libs/rock-hero-ui/CMakeLists.txt`
- possible `libs/rock-hero-ui/tests/CMakeLists.txt`

## Implementation Steps

1. Implement `EditorController` against `core::Session`, `audio::ITransport`,
   `audio::IEdit`, and `IEditorView`.
2. Register as an `audio::ITransport::Listener` in the constructor and unregister in
   the destructor.
3. Cache the initial derived `EditorViewState` before view attachment.
4. Implement `attachView(IEditorView&)` as a non-owning bind that immediately pushes
   cached state.
5. On transport events, re-derive from current session and transport state; suppress
   duplicate view-state pushes.
6. Implement play/pause, stop, and waveform-click seek decisions in the controller.
7. For load requests, validate the track id before calling
   `IEdit::setTrackAudioSource(...)`.
8. Mark an internal "edit in progress" state before calling `IEdit`. Natural
   `ITransport::Listener` callbacks may arrive reentrantly during the edit call
   before the session commit path runs.
9. If transport callbacks arrive while an edit is in progress, do not immediately
   push a state derived from pre-commit session data. Instead, record that a
   refresh is pending and let the load workflow finish first.
10. On failed edit, preserve `core::Session`, set a controller-composed error, then
    derive and push the final state once the in-flight edit window closes.
11. On successful edit, call `Session::replaceTrackAsset(...)`.
12. If that call returns `true`, clear the error, read `transport.state()`, and push
    the derived state once after the session and transport are back in sync. If a
    deferred transport refresh is pending, satisfy it through the same final
    derivation path instead of pushing an additional intermediate state.
13. If that call returns `false`, treat it as an internal consistency failure: assert
    in debug builds, log an error message in release builds, preserve the existing
    session state, set `last_load_error` to a controller-composed internal error, and
    push a view state. Do not terminate the process and do not silently pretend the
    session and audio state are synchronized.

## Tests

Add fakes for `ITransport`, `IEdit`, and `IEditorView`. Cover:

- `attachView(...)` immediately pushes derived state,
- duplicate transport states do not push redundant updates,
- play intent calls `play()` when a session track has an asset and transport is
  stopped,
- play intent calls `pause()` when transport is playing,
- play intent is ignored when no track has an asset,
- stop intent calls `stop()` only when enabled,
- waveform click clamps normalized input and seeks by transport duration,
- later transport ticks preserve a load error,
- invalid track load does not call `setTrackAudioSource(...)`,
- failed load preserves session and reports a composed error,
- successful load commits the track asset and clears the error,
- successful load tolerates reentrant transport notifications during `IEdit`
  without pushing a stale intermediate state derived from pre-commit session data,
- successful load produces exactly one final post-load push after session commit,
- post-edit session commit failure reports an internal error instead of crashing or
  silently clearing the error, if that path can be reached with the available session
  API,
- edit-caused transport state changes do not bypass duplicate-suppression rules once
  the in-flight edit window closes.

## Verification

Run the UI controller tests if the target exists. If full CMake is unreliable, compile
the controller source and test source from `compile_commands.json` and record that
the full test command still needs a repaired environment.

## Exit Criteria

- `EditorController` includes no JUCE headers.
- Tests use fakes of project-owned interfaces.
- The controller owns workflow policy; existing JUCE components still own the live UI
  until later stages migrate them.

## Do Not Do

- Do not extract `EditorView` yet.
- Do not modify `TransportControls` yet unless compilation requires a minimal include
  fix.
- Do not implement undo/redo.
- Do not add test-only state setters to the controller.
