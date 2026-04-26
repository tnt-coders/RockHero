# v20 Stage 06 - Editor Controller Contracts

## Goal

Add the framework-free controller/view contracts and view-state types that make the
editor workflow testable without JUCE initialization.

## Expected Files

- `libs/rock-hero-ui/include/rock_hero/ui/editor/track_waveform_state.h`
- `libs/rock-hero-ui/include/rock_hero/ui/editor/editor_view_state.h`
- `libs/rock-hero-ui/include/rock_hero/ui/editor/i_editor_view.h`
- `libs/rock-hero-ui/include/rock_hero/ui/editor/i_editor_controller.h`
- `libs/rock-hero-ui/tests/test_editor_controller_contracts.cpp`
- `libs/rock-hero-ui/CMakeLists.txt`
- possible `libs/rock-hero-ui/tests/CMakeLists.txt`

## Implementation Steps

1. Add `TrackWaveformState` with exactly the fields v19 specifies:
   - `core::TrackId track_id`,
   - `std::string display_name`,
   - `std::optional<core::AudioAsset> audio_asset`.

   Define `TrackWaveformState` in its own public header,
   `rock_hero/ui/editor/track_waveform_state.h`, rather than bundling it into
   `editor_view_state.h`.

   Do not add `muted`, `gain`, `selected`, per-row playhead, or per-row length.
   Mute and gain have no rendering behavior in this revision; `selected` was cut
   in v17 as speculative; the playhead cursor is shared across rows and lives on
   `EditorViewState`, not duplicated per row.
2. Add `EditorViewState` with exactly the fields v19 specifies:
   - `bool load_button_enabled`,
   - `bool play_pause_enabled`,
   - `bool stop_enabled`,
   - `bool play_pause_shows_pause_icon`,
   - `double cursor_proportion` (single shared playhead, not per-row),
   - `std::vector<TrackWaveformState> tracks`,
   - `std::optional<std::string> last_load_error`.

   Keep `EditorViewState` in `editor_view_state.h` and include
   `track_waveform_state.h` there instead of redefining the row type.
3. Add `IEditorView` with `setState(const EditorViewState&)`.
4. Add `IEditorController` with user-intent methods:
   - `onLoadAudioAssetRequested(core::TrackId, core::AudioAsset)`,
   - `onPlayPausePressed()`,
   - `onStopPressed()`,
   - `onWaveformClicked(double normalized_x)`.
5. Keep these files under the physical `editor/` folder but in namespace
   `rock_hero::ui`.
6. Do not include JUCE headers in these controller contracts.

## Tests

Add a headless contract test. It should not initialize JUCE.

Cover:

- `EditorViewState` can represent no tracks and a single track,
- equality or comparison support works if added for duplicate suppression,
- a fake `IEditorView` can receive state,
- a fake `IEditorController` can receive load/play/stop/seek intents,
- the test includes controller contract headers without including JUCE component
  headers.

## Verification

Run the new UI contract test if possible. If local CMake remains unreliable, compile
the test source and touched headers through a focused compile path.

## Exit Criteria

- Controller tests can include these headers without JUCE component setup.
- Names read as plain English in production and tests:
  `FakeEditorView`, `IEditorView`, `IEditorController`, `EditorViewState`.
- No `Sink` terminology remains in the new contracts.

## Do Not Do

- Do not implement `EditorController` yet.
- Do not add a separate CMake target.
- Do not create a `rock_hero::ui::controllers` namespace.
- Do not define `ThumbnailCreator` here because it names JUCE.
