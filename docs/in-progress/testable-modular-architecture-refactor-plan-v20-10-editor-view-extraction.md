# v20 Stage 10 - Editor View Extraction

## Goal

Move the editor component tree out of `MainWindow::ContentComponent` and into
`rock_hero::ui::EditorView`.

Do this without reopening the completed controller/state stages. `EditorViewState`
and its `cursor_proportion` field remain in place for v20, but live cursor animation
must not depend on pushing a full `EditorViewState` every frame.

## Expected Files

- `libs/rock-hero-ui/include/rock_hero/ui/editor_view.h`
- `libs/rock-hero-ui/src/editor_view.cpp`
- `libs/rock-hero-ui/include/rock_hero/ui/thumbnail_creator.h` or equivalent
  view/composition header
- possible UI tests for view wiring
- `apps/rock-hero-editor/main_window.cpp` only as a temporary adapter if needed

## Implementation Steps

1. Add top-level `rock_hero::ui::ThumbnailCreator` in a view/composition header, not
   in the controller contracts.
2. Define it as a callback that takes `juce::Component& owner` and returns
   `std::unique_ptr<audio::Thumbnail>`.
3. Implement `EditorView` as a JUCE component that implements `IEditorView`.
4. Require `IEditorController&` and `ThumbnailCreator` in the constructor.
5. Construct the initial `TrackWaveformRow` in the view constructor.
6. Immediately invoke `ThumbnailCreator` with the row component as owner and transfer
   the thumbnail into the row.
7. Own load button, transport controls, waveform rows, and file chooser.
8. Convert `juce::File` to `core::AudioAsset` only in the file chooser callback.
9. Source the load target from `EditorViewState.tracks.front().track_id`.
10. Disable load when `EditorViewState.tracks` is empty.
11. Add one editor-wide playhead cursor overlay component across the waveform row
    area. Individual rows draw only their own waveform content; they do not draw
    synchronized per-row cursors.
12. Use `EditorViewState.cursor_proportion` only as coarse snapshot state for the
    cursor overlay, such as initial placement and transport jumps after load, seek,
    stop, or other discrete state changes.
13. Do not design `EditorView` so smooth cursor motion requires a full
    `IEditorView::setState(...)` call on every animation frame.
14. If a minimal smooth-motion path is practical in this stage, keep it local to the
    cursor overlay through a pull-based UI cadence and a narrow transport read path.
    If that is not practical yet, leave explicit TODO comments that the overlay is a
    structural split preparing for a later smooth-motion implementation.
15. Present `last_load_error` on an edge using
    `std::optional<std::string> m_last_presented_error`.

## Tests

Add narrow view tests if the JUCE test harness can construct the component. Use a
fake `IEditorController` and fake `audio::Thumbnail`.

- `setState(...)` projects state to child controls,
- empty track list disables load,
- load intent uses `EditorViewState.tracks.front().track_id`,
- same load error is not presented twice,
- `ThumbnailCreator` is invoked exactly once during construction,
- created thumbnail is installed on the initial row before the constructor returns,
- a single cursor overlay is owned by `EditorView`, not by individual rows,
- applying `setState(...)` updates the overlay's coarse cursor state without
  requiring cursor ownership in the waveform rows.

If file chooser behavior cannot be automated cleanly, isolate and test the path from
an already selected file/asset to controller intent. Do not rely only on manual GUI
testing for state projection and thumbnail construction.

## Verification

Compile the UI target and the editor app target. Run any new UI tests if the local
test setup supports them.

## Exit Criteria

- `EditorView` emits user intents through `IEditorController`.
- `EditorView` renders only `EditorViewState`.
- `EditorView` owns the single playhead cursor overlay across all waveform rows.
- Smooth cursor motion is structurally separated from row rendering, even if the
  first extracted overlay still uses coarse snapshot updates.
- `EditorView` does not include or own concrete `audio::Engine`.
- `ThumbnailCreator` is consumed during construction and not retained.

## Do Not Do

- Do not wire `ui::Editor` yet unless needed as a temporary construction helper.
- Do not add `IThumbnailFactory`.
- Do not delete legacy `ContentComponent` until the main window is rewired.
