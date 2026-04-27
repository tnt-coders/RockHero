# v20 Stage 12 - Main Window Rewire And Cleanup

## Goal

Replace the old editor app wiring with `ui::Editor`, then delete compatibility paths
that are no longer needed.

## Expected Files

- `apps/rock-hero-editor/main_window.h`
- `apps/rock-hero-editor/main_window.cpp`
- `libs/rock-hero-audio/include/rock_hero/audio/engine.h`
- `libs/rock-hero-audio/src/engine.cpp`
- old UI files that become unused after migration
- relevant CMake files

## Implementation Steps

1. Make `MainWindow` own:
   - `std::unique_ptr<audio::Engine>`,
   - `std::unique_ptr<core::Session>` or direct `core::Session`,
   - `std::unique_ptr<ui::Editor>` or direct `ui::Editor`, depending on header
     dependency needs.
2. Preserve lifetime order: engine/session must outlive `ui::Editor`; editor must
   outlive view-owned thumbnails.
3. Add the initial empty or default "Full Mix" track to the session before constructing
   `ui::Editor`.
4. Pass the engine as `audio::ITransport&` and `audio::IEdit&`; `ui::Editor`
   passes the same transport onward to `EditorView` as a const live-position
   source.
5. Pass a thumbnail creation lambda that calls `audio::Engine::createThumbnail(...)`.
6. Install `m_editor->component()` as the window content.
7. Preserve `clearContentComponent()` or equivalent teardown safety.
8. Remove direct `audio::Engine` usage from old UI widgets and views.
9. Remove old nested `MainWindow::ContentComponent`.
10. Remove old `Engine::Listener` UI coupling when no external UI implementers remain.
    This is the official endpoint for the entire legacy listener pattern: delete
    `audio::Engine::Listener`, its pointer-based `addListener` / `removeListener`
    overloads, `enginePlayingStateChanged(...)`, `engineTransportPositionChanged(...)`,
    and tests whose only purpose is preserving legacy position-listener callbacks.
11. Remove legacy `Engine` methods that are no longer part of the port surface and no
    longer used. Stage 10's live `ITransport::position()` replaces the legacy
    cursor-position read path, so remove `Engine::getTransportPosition()` once
    no project-owned UI code calls it.
12. Remove obsolete `WaveformDisplay`.
13. If any target, helper, or test still depends on `WaveformDisplay`, migrate that
    dependency in this stage to `ui::Editor`, `EditorView`, `TrackView`, or the shared
    cursor-overlay path as appropriate rather than leaving `WaveformDisplay` behind as a
    compatibility shim.
14. Remove `WaveformDisplay` source, header, tests, CMake entries, and any remaining
    references once those dependencies are migrated.
15. Collapse `audio::ScopedListener` onto a single reference-based form. Once `Engine::Listener`
    is gone, every remaining listener surface in the project (`ITransport::Listener`,
    `TrackView::Listener`, `TransportControls::Listener`) takes its listener by reference, so
    the helper should call `m_broadcaster.addListener(m_listener)` and
    `m_broadcaster.removeListener(m_listener)` rather than passing `&m_listener`. After the
    helper is converted, replace the manual `addListener` / `removeListener` pair in
    `EditorController`'s constructor and destructor with a `ScopedListener` member declared last
    so its destructor runs first during teardown. Remove the corresponding "manual subscription"
    note from `EditorController`'s class documentation in the same change.
16. Keep the live-position path listener-free. After cleanup, cursor drawing
    should depend on `ITransport::position()` plus pushed visible-range state,
    not on `Engine::Listener`, `ITransport::Listener`, or legacy waveform polling
    helpers.

## Tests

Run the broadest practical verification for the editor slice:

- core tests,
- UI controller tests,
- audio compile or adapter tests,
- editor app compile.

Manual smoke test when a GUI run is practical:

- app opens,
- load button can select an audio file,
- waveform appears after successful load,
- play/pause/stop buttons work,
- space bar toggles play/pause,
- waveform click seeks.

## Verification

Prefer full configure/build/test if the environment has been repaired. In the current
Codex environment, avoid repeatedly retrying known-hanging CMake paths; use focused
compile commands and record that full CMake/CTest still needs to be run elsewhere.

## Exit Criteria

- The editor app composes `audio::Engine`, `core::Session`, and `ui::Editor`.
- No app code manually wires controller/view/thumbnail internals.
- Old direct engine UI coupling is gone.
- Smooth cursor motion reads current position through `ITransport::position()`;
  duration or visible range comes from pushed view state.
- `WaveformDisplay` no longer exists in project-owned code.
- The final architecture still matches the v19 goal alignment checkpoint.

## Do Not Do

- Do not add new product features while rewiring.
- Do not introduce track roles.
- Do not implement undo/redo.
- Do not implement multi-stem playback.
- Do not add a thumbnail factory unless dynamic row creation is implemented in this
  same future scope.
