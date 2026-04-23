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
4. Pass the engine as both `audio::ITransport&` and `audio::IPlaybackContent&`.
5. Pass a thumbnail creation lambda that calls `audio::Engine::createThumbnail(...)`.
6. Install `m_editor->component()` as the window content.
7. Preserve `clearContentComponent()` or equivalent teardown safety.
8. Remove direct `audio::Engine` usage from old UI widgets and views.
9. Remove old nested `MainWindow::ContentComponent`.
10. Remove old `Engine::Listener` UI coupling when no external UI implementers remain.
11. Remove legacy `Engine` methods that are no longer part of the port surface and no
    longer used.
12. Remove obsolete `WaveformDisplay` only if fully replaced and no other target uses
    it. If uncertain, leave it for a separate cleanup commit.

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
- Old direct engine UI coupling is gone or explicitly isolated as a deferred cleanup.
- The final architecture still matches the v19 goal alignment checkpoint.

## Do Not Do

- Do not add new product features while rewiring.
- Do not introduce track roles.
- Do not implement multi-stem playback.
- Do not add a thumbnail factory unless dynamic row creation is implemented in this
  same future scope.
