# Timeline Snap Time-Domain Plan

Status: in progress. This plan moves snap-to-grid cursor placement from pixel-column space into
musical time. It should land before the fractional-snapping slice of
`docs/in-progress/tempo-grid-spacing-plan.md` so that fractional search is written once against
the time-domain API.

## Motivation

Snapped cursor placement currently produces a pixel-quantized time instead of the exact grid-line
time. The click path is:

1. `nearestTempoGridLineX(...)` (`rock-hero-editor/core/src/tempo_grid_geometry.cpp`) converts the
   clicked column to seconds, binary-searches the two neighboring beats, maps each beat back to a
   rounded column, and returns the nearer *column*. The exact beat seconds are computed inside the
   search and then discarded.
2. `normalizedTimelineCursorPlacementX(...)` (`rock-hero-editor/ui/src/timeline_cursor.cpp`)
   divides that column by the column count and returns a normalized ratio.
3. `EditorController::onWaveformClicked(double)` multiplies the ratio back across the loaded
   timeline range and seeks the transport.

This has three concrete problems:

- **The seek time is quantized to pixel resolution.** At low zoom one column can span tens of
  milliseconds, so playback started from a snapped cursor does not start exactly on the beat, and
  zooming in after snapping shows the cursor visibly off the grid line.
- **Presentation state participates in a domain value.** The snapped time depends on the current
  width and zoom, so the same click on the same beat produces different stored times at different
  zoom levels.
- **The snap function carries parameters it only needs to undo the pixel mapping.**
  `visible_timeline`, `width`, and `target_x` exist solely to convert into seconds and back out
  again; the musical answer was already available in seconds.

## Target Design

1. **Pure time-domain snap lookup.** Replace `nearestTempoGridLineX(...)` with a tempo-map query
   that never sees pixels:

   ```cpp
   [[nodiscard]] common::core::TimePosition nearestTempoGridTime(
       const common::core::TempoMap& tempo_map, common::core::TimePosition target);
   ```

   It binary-searches the first beat at or after the target (the existing
   `firstBeatAtOrAfterSeconds` logic), compares the two neighbors by distance in seconds, and
   resolves exact-midpoint ties to the earlier line. The return is total, not optional: `TempoMap`
   clamps degenerate inputs itself (an anchorless map resolves everything to `0.0` seconds and
   `terminalGlobalBeatIndex()` is never negative), so a nearest beat always exists. The
   tempo-grid-spacing plan later adds a spacing parameter and searches fractional positions with
   the same shape.

2. **Time-typed placement path.** Rework the shared placement helper in `timeline_cursor.cpp` so
   both click sources produce a time, not a ratio: convert the clicked x to seconds through the
   existing inverse mapping, snap via `nearestTempoGridTime` (or keep the sub-pixel click time for
   Ctrl/free placement), and hand the result to the controller.

3. **Time-typed seek intent.** Change the controller intent from `onWaveformClicked(double)` to a
   `TimePosition`-typed request. Since the signature changes anyway, rename it to reflect that
   both the waveform overlay and the timeline ruler use it (for example
   `onTimelineSeekRequested(common::core::TimePosition)`). The controller keeps its existing
   clamp-into-loaded-range-and-seek behavior, minus the ratio conversion. The `IEditorController`
   seam, fakes, and controller transport tests move from ratio cases to direct time cases.

4. **Rendering stays column-based.** `visibleTempoGridLines(...)` is genuinely presentation math —
   its pixel-merging and downbeat-promotion behavior is about drawing — and does not change.
   Column-only helpers that served snapping (`columnForBeatIndex`) are deleted once unused.

## Behavior Changes

- Snapped placement lands on the exact beat time. Playback starts exactly on the beat, and the
  cursor stays on the grid line at any later zoom. At the zoom where the click happened the
  difference is sub-pixel, so the interaction feels identical.
- Nearest-line choice is unchanged in practice: the pixel mapping is linear in seconds, so the
  nearest neighbor in time is the nearest on screen. The earlier-line tie rule is kept for exact
  midpoints.
- The current code refuses to snap to a grid line whose rounded column falls outside the visible
  span; the time-domain lookup naturally allows a nearest beat just past the visible edge. Keep
  the natural behavior: the restriction was an artifact of the pixel path, clicks near an edge
  snapping slightly off-screen is standard timeline behavior, and the controller clamp still
  bounds the seek to the loaded range.

## Implementation Steps

1. Add `nearestTempoGridTime(...)` to editor-core with tests (neighbor selection, earlier-line
   ties, targets before the first and after the terminal beat, degenerate tempo maps).
2. Rework the placement helper in `timeline_cursor.cpp` to return a `TimePosition`, updating
   `EditorView` and `TimelineRuler` call sites; repaint-strip logic is untouched.
3. Change the controller seek intent to the time-typed request across `IEditorController`, the
   controller, view fakes, and transport tests.
4. Delete `nearestTempoGridLineX(...)` and now-unused column helpers; migrate their tests to the
   time-domain equivalents.

## Non-Goals

- No grid spacing changes; that is `docs/in-progress/tempo-grid-spacing-plan.md`.
- No `visibleTempoGridLines(...)` or ruler drawing changes.
- No behavior change to Ctrl/free placement beyond carrying a time instead of a ratio.