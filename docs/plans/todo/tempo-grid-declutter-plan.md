# Tempo Grid Declutter Plan

Status: deferred (moved from docs/plans/in-progress 2026-07-03). Stale in places: subdivisions have
since landed, and the `TempoGridLineStrength` sketch is superseded by the implemented
`TempoGridLineRank`. The note-value grid rework has also landed since
(docs/plans/completed/timeline-ruler-review-fixes-plan.md Phase 7): generation is now
measure-anchored in note-value units via `MeasureGridWalker`. Re-read the current grid code and
revise before implementing.

## Goal

Fix zoomed-out timeline/ruler performance by reducing the number of visible tempo-grid lines before
adding narrower beat subdivisions. The current problem exists with full beats only, so subdivision
support should wait until the grid-density policy is in place.

## Direction

Implement declutter first, then add narrower divisions later.

Declutter gives the editor a reusable policy for deciding which musical grid lines are useful at a
given zoom level. Once that policy exists, subdivisions become another grid-line strength that can
appear only when there is enough room.

Subdivision rendering should live in editor grid geometry, not in the tempo map, unless the data is
authored timing information. The tempo map should answer musical timing questions; the grid geometry
layer should decide which visual divisions to draw.

## Proposed Steps

1. Add grid-density policy in editor-core.
   - Inputs: tempo map, visible timeline range, full timeline width, visible x span, and pixel
     spacing thresholds.
   - Prefer measure starts over beat lines.
   - Hide beat lines when adjacent beat columns are too close.
   - If measure starts are too dense, show every Nth measure.
   - Keep output stable while zooming so lines do not flicker unpredictably.

2. Replace raw beat output with decluttered grid output.
   - Keep the existing bounded scan; declutter must not reintroduce full-song scans.
   - Preserve merged-column behavior when multiple musical positions round to the same x column.
   - Consider making line strength explicit:

```cpp
enum class TempoGridLineStrength
{
    Measure,
    Beat,
};

struct TempoGridLine
{
    int x;
    int measure;
    int beat;
    TempoGridLineStrength strength;
};
```

3. Apply the same output to the track grid and timeline ruler.
   - The dotted track grid and ruler ticks should consume the same decluttered grid lines.
   - Ruler labels should attach only to retained measure lines.
   - Avoid separate density policies for ruler and track content.

4. Tune low-zoom behavior.
   - Beat lines should require enough horizontal spacing to remain visually useful.
   - Measure lines should survive longer than beat lines.
   - At very low zoom, emit sparse measure starts only.
   - Labels should require more room than measure ticks.

5. Add core tests.
   - High zoom emits all full beats.
   - Medium zoom emits measures and useful beats.
   - Low zoom omits beats and emits measure starts.
   - Very low zoom emits sparse measure starts and caps output near visible pixel count.
   - Non-4/4 maps preserve correct measure starts.
   - Visible-span culling still works.

6. Add UI tests.
   - Ruler measure ticks remain full height for retained measures.
   - Beat ticks disappear when zoomed out.
   - Labels follow retained measure lines.
   - Track grid and ruler agree on retained x positions.

7. Add subdivision support later.
   - Extend grid geometry to derive subdivisions between beats.
   - Add a subdivision line strength:

```cpp
enum class TempoGridLineStrength
{
    Measure,
    Beat,
    Subdivision,
};
```

   - Let the same density policy decide when subdivisions, beats, and measures are visible.

## Notes

This plan intentionally fixes the current performance problem before increasing grid density.
Adding subdivisions first would create more candidate lines and make the zoomed-out case worse.
