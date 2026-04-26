# v20 Stage 06 - Editor Controller Contracts

## Goal

Add the framework-free controller/view contracts and view-state types that make the
editor workflow testable without JUCE initialization.

## Expected Files

- `libs/rock-hero-ui/include/rock_hero/ui/editor/track_view_state.h`
- `libs/rock-hero-ui/include/rock_hero/ui/editor/editor_view_state.h`
- `libs/rock-hero-ui/include/rock_hero/ui/editor/i_editor_view.h`
- `libs/rock-hero-ui/include/rock_hero/ui/editor/i_editor_controller.h`
- `libs/rock-hero-ui/tests/test_editor_controller.cpp`
- `libs/rock-hero-ui/CMakeLists.txt`
- possible `libs/rock-hero-ui/tests/CMakeLists.txt`

## Implementation Steps

1. Add `TrackViewState` with exactly the fields v19 specifies:
   - `core::TrackId track_id`,
   - `std::string display_name`,
   - `std::optional<core::AudioAsset> audio_asset`.

   Define `TrackViewState` in its own public header,
   `rock_hero/ui/editor/track_view_state.h`, rather than bundling it into
   `editor_view_state.h`.

   Do not add `muted`, `gain`, `selected`, per-row playhead, or per-row length.
   Mute and gain have no rendering behavior in this revision; `selected` was cut
   in v17 as speculative; the editor's playhead cursor is one window-wide overlay
   sourced by the view at vsync rate (see stage 10), not a per-row field and not
   part of `EditorViewState`.
2. Add `EditorViewState` with exactly the following fields. Note that this
   intentionally omits v19's `cursor_proportion` field; see the Cursor
   Architecture note below.
   - `bool load_button_enabled`,
   - `bool play_pause_enabled`,
   - `bool stop_enabled`,
   - `bool play_pause_shows_pause_icon`,
   - `std::vector<TrackViewState> tracks`,
   - `std::optional<std::string> last_load_error`.

   Keep `EditorViewState` in `editor_view_state.h` and include
   `track_view_state.h` there instead of redefining the track-view type.
3. Add `IEditorView` with `setState(const EditorViewState&)`.
4. Add `IEditorController` with user-intent methods:
   - `onLoadAudioAssetRequested(core::TrackId, core::AudioAsset)`,
   - `onPlayPausePressed()`,
   - `onStopPressed()`,
   - `onWaveformClicked(double normalized_x)`.
5. Keep these files under the physical `editor/` folder but in namespace
   `rock_hero::ui`.
6. Do not include JUCE headers in these controller contracts.

## Cursor Architecture: Why The Snapshot Has No Cursor Field

The editor playhead cursor is intentionally not part of `EditorViewState`. It is
sourced by the view at vsync rate from `audio::ITransport` (see stage 10). The
state channel carries discrete transition-shaped data only — button enables,
track list, error events. This split lets each mechanism do what it is good at:

- Push (`IEditorView::setState`) for transition-shaped state that changes
  rarely, where every push corresponds to a real change worth a repaint.
- Pull (vsync timer reading transport position) for continuous-shaped state
  that changes constantly during playback, where the view's render rate, not
  the broadcaster's cadence, should drive cursor motion.

Including `cursor_proportion` in the snapshot would force the controller to
either push at Tracktion's broadcast rate (~30 Hz, not vsync, visibly steppy)
or push at vsync rate (full-state pushes for every animation frame, with
duplicate-suppression bookkeeping for all the position-only changes that don't
affect any other field). Both options are worse than just sourcing the cursor
where it is needed at the rate it is needed.

The controller therefore also does not subscribe to continuous transport
position updates. See stage 07 for the resulting subscription policy.

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
