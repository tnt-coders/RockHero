# v20 Stage 10 - Editor View Extraction

## Goal

Move the editor component tree out of `MainWindow::ContentComponent` and into
`rock_hero::ui::EditorView`. Add the editor-wide playhead cursor overlay that
sources current transport position via a vsync-rate pull from
`audio::ITransport::position()`, independent of `IEditorView::setState` for
continuous cursor motion.

Duration and visible timeline range are not continuous cursor inputs. They remain
discrete view-state inputs because they change only on loads, coarse
transport-state transitions, zoom, scroll, or layout changes.

## Expected Files

- `libs/rock-hero-audio/include/rock_hero/audio/i_transport.h`
- `libs/rock-hero-audio/include/rock_hero/audio/engine.h`
- `libs/rock-hero-audio/src/engine.cpp`
- `libs/rock-hero-audio/tests/test_transport.cpp`
- `libs/rock-hero-audio/tests/test_engine.cpp`
- `libs/rock-hero-ui/include/rock_hero/ui/editor_view.h`
- `libs/rock-hero-ui/src/editor_view.cpp`
- `libs/rock-hero-ui/include/rock_hero/ui/editor_view_state.h`
- `libs/rock-hero-ui/include/rock_hero/ui/thumbnail_creator.h` or equivalent
  view/composition header
- `libs/rock-hero-ui/src/editor_controller.cpp`
- `libs/rock-hero-ui/tests/test_editor_controller.cpp`
- possible UI tests for view wiring
- `apps/rock-hero-editor/main_window.cpp` only as a temporary adapter if needed

## Implementation Steps

1. Add `audio::ITransport::position() const noexcept` as the live position read
   used by render-cadence cursor drawing.
2. Do not use `ITransport::state().position` for smooth cursor motion. `state()`
   remains the coarse/cached snapshot used for transition-shaped controller state.
3. Document that `position()` is currently called from the message-thread
   view/vblank path and is for smooth rendering, not transport control.
4. Make `audio::Engine::position()` read Tracktion's current transport position
   at call time and clamp it to the loaded range. Do not implement it by returning
   `m_transport_state.position` or the existing listener-updated
   `m_transport_position` cache; those inherit callback cadence and are not the
   smooth cursor source this stage is adding.
5. Extend `EditorViewState` with discrete timeline mapping inputs for the cursor.
   Prefer `core::TimePosition visible_timeline_start` and
   `core::TimeDuration visible_timeline_duration` so later zoom/scroll can reuse
   the same shape. For the current editor, derive start as zero and duration from
   the coarse transport duration. Do not add cursor position or cursor proportion.
6. Add top-level `rock_hero::ui::ThumbnailCreator` in a view/composition header,
   not in the controller contracts.
7. Define `ThumbnailCreator` as a callback that takes `juce::Component& owner`
   and returns `std::unique_ptr<audio::Thumbnail>`.
8. Implement `EditorView` as a JUCE component that implements `IEditorView`.
9. Require `IEditorController&`, `const audio::ITransport&`, and
   `ThumbnailCreator` in the `EditorView` constructor. The const transport
   reference gives the cursor path read-only access to `position()` without
   exposing playback-control calls to the view.
10. Construct the initial `TrackView` in the view constructor.
11. Immediately invoke `ThumbnailCreator` with the row component as owner and
    transfer the thumbnail into the row.
12. Own load button, transport controls, waveform rows, file chooser, and one
    editor-wide cursor overlay.
13. Convert `juce::File` to `core::AudioAsset` only in the file chooser callback.
14. Source the load target from `EditorViewState.tracks.front().track_id`.
15. Disable load when `EditorViewState.tracks` is empty.
16. Add one editor-wide playhead cursor overlay component across the waveform row
    area. Individual rows draw only their own waveform content; they do not draw
    synchronized per-row cursors.
17. Route timeline seek clicks through the same editor-wide overlay, not through
    a particular `TrackView`. This keeps click-to-seek aligned with the shared
    visible timeline and prepares the interaction to span multiple track rows.
18. The cursor overlay receives a non-owning `const audio::ITransport&` through
    its constructor. `EditorView` wires the production transport in at composition
    time; tests construct the overlay against a fake transport.
19. The cursor overlay stores the current visible timeline range supplied by
    `EditorView::setState`. It does not poll duration every frame.
20. The cursor overlay runs a `juce::VBlankAttachment` (preferred) or a 60 Hz
    `juce::Timer` whose callback reads only the current position from
    `ITransport::position()`, computes the cursor x from the stored visible range
    and component bounds, and triggers a repaint of only the narrow strip covering
    the previous and current cursor positions. The static waveform underneath
    stays in `TrackView` and is not invalidated by cursor motion.
21. The cursor overlay is independent of `IEditorView::setState` for continuous
    motion. `setState(...)` may update discrete cursor mapping inputs such as
    visible range, but it must not carry or drive current cursor position.
22. The cursor overlay may emit normalized timeline seek intent to the controller,
    but it does not register with the controller and does not depend on transport
    listeners. Cursor motion is sourced solely from the vsync pull described
    above.
23. Discrete cursor jumps after seek, stop, or load require no special handling in
    the overlay because the next vsync tick reads the new transport position
    naturally.
24. Present `last_load_error` on an edge using
    `std::optional<std::string> m_last_presented_error`.

## Tests

Add narrow view tests if the JUCE test harness can construct the component. Use a
fake `IEditorController`, fake `audio::ITransport`, and fake `audio::Thumbnail`.

- `EditorController` derives the visible timeline range from coarse transport
  duration without adding cursor position to `EditorViewState`,
- `setState(...)` projects state to child controls,
- empty track list disables load,
- load intent uses `EditorViewState.tracks.front().track_id`,
- same load error is not presented twice,
- `ThumbnailCreator` is invoked exactly once during construction,
- created thumbnail is installed on the initial row before the constructor
  returns,
- a single cursor overlay is owned by `EditorView`, not by individual rows,
- timeline seek clicks are handled by the editor-wide overlay rather than by a
  particular `TrackView`,
- the cursor overlay reads only position from the injected const transport, not
  from `EditorViewState`,
- `EditorViewState` carries visible timeline range or duration for cursor mapping
  but does not carry current cursor position,
- a `setState(...)` push that does not change the fake transport's position does
  not advance the cursor through a hidden state-driven path,
- ticking the overlay's vsync callback against a fake transport with a known
  position and a pushed visible range computes the expected cursor x and
  invalidates only the narrow strip around it,
- `Engine::position()` uses a live Tracktion position read rather than the
  listener-updated transport snapshot or cache.

If direct repaint invalidation cannot be observed cleanly in the JUCE test
harness, extract the cursor-x computation into a tiny helper and test that in
isolation. If file chooser behavior cannot be automated cleanly, isolate and test
the path from an already selected file/asset to controller intent. Do not rely
only on manual GUI testing for state projection, thumbnail construction, or cursor
position sourcing.

## Verification

Compile the audio target, the UI target, and the editor app target. Run any new
audio/UI tests if the local test setup supports them.

## Exit Criteria

- `EditorView` emits user intents through `IEditorController`.
- `EditorView` renders `EditorViewState` for transition-shaped state and injects
  a const `audio::ITransport&` into the cursor overlay for continuous motion.
- `EditorView` owns the single playhead cursor overlay across all waveform rows.
- The cursor overlay sources motion via a vsync-rate pull from
  `ITransport::position()` and is not driven by current-position values in
  `IEditorView::setState`.
- Duration or visible-range data used for cursor mapping is pushed as discrete
  view state and is not polled every frame.
- `EditorView` does not include or own concrete `audio::Engine`.
- `ThumbnailCreator` is consumed during construction and not retained.

## Do Not Do

- Do not wire `ui::Editor` yet unless needed as a temporary construction helper.
- Do not add `IThumbnailFactory`.
- Do not delete legacy `ContentComponent` until the main window is rewired.
- Do not let cursor rendering depend on `Engine::Listener` or
  `ITransport::Listener`.
- Do not poll duration every frame for cursor motion.
