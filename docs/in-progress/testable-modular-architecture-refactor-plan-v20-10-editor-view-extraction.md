# v20 Stage 10 - Editor View Extraction

## Goal

Move the editor component tree out of `MainWindow::ContentComponent` and into
`rock_hero::ui::EditorView`. Add the editor-wide playhead cursor overlay that
sources transport position via a vsync-rate pull from `audio::ITransport`,
independent of `IEditorView::setState` (see stage 06's Cursor Architecture
note).

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
11. Add one editor-wide playhead cursor overlay component across the waveform
    row area. Individual rows draw only their own waveform content; they do not
    draw synchronized per-row cursors.
12. The cursor overlay receives a non-owning `audio::ITransport&` (or a narrower
    project-owned position-reader interface if one is introduced for testing
    convenience) through its constructor. `EditorView` wires the production
    transport in at composition time; tests construct the overlay against a
    fake.
13. The cursor overlay runs a `juce::VBlankAttachment` (preferred) or a 60 Hz
    `juce::Timer` whose callback reads `transport.state().position` and
    `transport.state().duration`, computes the current cursor x, and triggers a
    repaint of only the narrow strip covering the previous and current cursor
    positions. The static waveform underneath stays in `TrackWaveformRow` and is
    not invalidated by cursor motion.
14. The cursor overlay is fully independent of `IEditorView::setState`. It does
    not read from `EditorViewState`, does not register with the controller, and
    does not depend on transport listeners. Cursor motion is sourced solely from
    the vsync pull described above.
15. Discrete cursor jumps (after seek, stop, load) require no special handling
    in the overlay because the next vsync tick reads the new transport position
    naturally.
16. Present `last_load_error` on an edge using
    `std::optional<std::string> m_last_presented_error`.

## Tests

Add narrow view tests if the JUCE test harness can construct the component. Use a
fake `IEditorController` and fake `audio::Thumbnail`.

- `setState(...)` projects state to child controls,
- empty track list disables load,
- load intent uses `EditorViewState.tracks.front().track_id`,
- same load error is not presented twice,
- `ThumbnailCreator` is invoked exactly once during construction,
- created thumbnail is installed on the initial row before the constructor
  returns,
- a single cursor overlay is owned by `EditorView`, not by individual rows,
- the cursor overlay reads its position from the injected transport, not from
  `EditorViewState`, and a `setState(...)` push that does not change transport
  state does not move the cursor,
- ticking the overlay's vsync callback against a fake transport with a known
  position computes the expected cursor x and invalidates only the narrow strip
  around it (verifiable through a stub repaint sink if the JUCE test harness
  permits, otherwise extract the cursor-x computation into a tiny helper and
  test that in isolation).

If file chooser behavior cannot be automated cleanly, isolate and test the path from
an already selected file/asset to controller intent. Do not rely only on manual GUI
testing for state projection and thumbnail construction.

## Verification

Compile the UI target and the editor app target. Run any new UI tests if the local
test setup supports them.

## Exit Criteria

- `EditorView` emits user intents through `IEditorController`.
- `EditorView` renders `EditorViewState` for transition-shaped state and reads
  transport via the injected `audio::ITransport` for cursor motion.
- `EditorView` owns the single playhead cursor overlay across all waveform rows.
- The cursor overlay sources motion via a vsync-rate pull from `ITransport` and
  is not driven by `IEditorView::setState`.
- `EditorView` does not include or own concrete `audio::Engine`.
- `ThumbnailCreator` is consumed during construction and not retained.

## Do Not Do

- Do not wire `ui::Editor` yet unless needed as a temporary construction helper.
- Do not add `IThumbnailFactory`.
- Do not delete legacy `ContentComponent` until the main window is rewired.
