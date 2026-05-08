# Multi-Track Viewport Shell Plan

Status: in progress.

## Summary

Introduce a real JUCE viewport around the waveform area so the editor has a proper container for
future multiple tracks. The first version still renders only the existing waveform track, but it
sits inside a darker viewport surface with a fixed initial track size. At the current 1280x800
default window size, the primary waveform track should be 240px tall, which is one third of the
available 720px viewport height.

## Key Changes

- Add a private `EditorView::TrackViewport` shell backed by `juce::Viewport` and an owned content
  component.
- Keep `ArrangementView` as the first waveform track for now; do not add new public state,
  controller APIs, audio APIs, or zoom behavior.
- Move the `CursorOverlay` into the viewport content and size it to the full viewport content
  height, not just the waveform track, so clicks and the playhead span the full viewport.
- Show the viewport even when no project is loaded; hide the waveform track and cursor, and show
  centered `No Project Loaded` text inside the darker viewport.
- Use a darker viewport/content background than the surrounding editor background, while leaving the
  current menu and transport bar structure intact.
- Treat the viewport content canvas as minimum-size content for now; resizing the editor window
  should reveal, clip, or extend passive canvas space rather than resizing the waveform track.

## Layout Behavior

- `EditorView` continues to lay out the menu bar, transport bar, gap, then the track viewport.
- The track viewport occupies the same area currently used by the arrangement/waveform area.
- At the default 1280x800 editor window size, the viewport bounds are expected to be 1264x720 after
  the existing chrome and content insets are removed.
- When loaded, the first waveform track is positioned at the top of the viewport content and uses a
  fixed 240px height.
- The viewport content canvas uses a minimum 1264x720 size for this first version.
- Resizing the window does not resize the waveform track. Smaller viewports clip or scroll the
  minimum canvas; larger viewports extend the passive canvas so the cursor and empty background
  reach the visible viewport edges.
- The cursor overlay covers the full viewport content bounds so lower empty space still supports
  timeline click and seek behavior.

## Test Plan

- Update `EditorView` layout tests to assert the new `track_viewport` bounds and the waveform
  track's fixed 240px default height.
- Add or adjust a resize test proving the waveform track height remains fixed when the editor
  window height changes.
- Add or adjust tests so the default no-project state keeps the viewport visible, hides the waveform
  and cursor, and exposes `No Project Loaded` text.
- Add a click test that clicks below the waveform track but inside the viewport and verifies the
  controller receives the normalized waveform/timeline click.
- Add a larger-than-default viewport regression test proving the cursor canvas extends to the
  bottom of the visible viewport while the waveform track remains fixed.
- Keep existing thumbnail propagation tests proving the existing `ArrangementView` still owns the
  thumbnail source.
- Run `clang-format`, `git diff --check`, and focused `clang-tidy` on the changed UI files. Run the
  UI test binary if the local build environment cooperates.

## Assumptions

- "Viewport" means a real `juce::Viewport`, not just a styled component.
- The first track should use a fixed 240px default height, based on one third of the current
  default viewport height.
- The viewport should reveal, clip, or extend passive canvas space on resize rather than scaling the
  waveform track with the window.
- The playhead and click target should span the full viewport, including empty space below the first
  track.
- This is infrastructure only; no zoom controls, scroll policy UI, extra tracks, or theme system
  should be added yet.
