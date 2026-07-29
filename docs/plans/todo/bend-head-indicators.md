# Bend-Amount Chevrons on Note Heads

Status: **SHIPPED 2026-07-28**, three passes on sight the same day: chevron *stacks* were
rejected as clutter (symbol count scaled with the amount), then head amount *figures* were
rejected as unreadable at speed. The shipped notation: **presence** — ONE `^` chevron marker
on the head (the source-game notation's own bend cue; cell 16 of the
atlas's appended fifth row, runtime-rasterized into both atlas paths, never gated on
`reference_cells`), rotated to point along the drawn bend-lift direction; **amount and
stages** — pure geometry: a dashed white target rail at each held target's exact lift height
(`bend_lift_per_half_step` × the snapped semitones) spans the stretch of tail that holds it,
so the curve visibly climbs to meet its rail, a compound bend reads as a staircase of rails,
and screen-center foreshortening no longer matters because the rail IS the reference line.
Amounts snap to the nearest half semitone (`highwayBendDisplayHalfSteps` in
`highway_tail.{h,cpp}`, tested — a plain half-step count in the GP importer's quarter-tone
quantum; the chart stores plain semitone doubles with no special notation, user check
2026-07-28). `HighwayAtlasLayout` is rectangular-grid generalized (tested). REMAINING: judge
the procedural chevron cell and the rail dash geometry/brightness (`g_bend_rail_*`) in the
preview; author the chevron into the Charter-style PNG only if the procedural one does not
hold up.

## Goal

A bent note's head announces how far to bend before the note arrives, in the style of source (the
source game): one `^` chevron per half step (1 semitone of bend), stacked
vertically — one for a half-step bend, two for a whole step, three for a step and a half.
RockHero goes one better than source: the chart vocabulary carries 0.5-semitone quarter-tone curls
as first-class data
(`rock-hero-common/core/include/rock_hero/common/core/chart/chart.h:115`), and source has no honest
way to draw them — we render a visually distinct half-size chevron for the quarter-step
component, so a curl reads differently from a full half-step bend.

## Facts verified 2026-07-26

- **Bend data at draw time**: `HighwayNoteView.bend` is a list of `{seconds, semitones}` points
  (`highway_view_state.h`); the tail curve is evaluated by `highwayBendSemitonesAt`
  (smoothstep-eased between points as of this date). A prebend is a first point sitting at the
  onset. Quarter curls arrive as `semitones = 0.5`.
- **Head markers are atlas-cell composites**: `push_marker` in
  `rock-hero-common/ui/src/highway/highway_renderer.cpp` composites atlas cells over the head
  base (alpha "over"), exactly how hammer-on/pull-off/mute markers draw today.
- **The head atlas is full**: Charter's reference 4×4 PNG (16 cells, only cell 11 free) in the
  R-tint / G-highlight / B-alpha channel scheme, uploaded by `makeHighwayAtlases`
  (`rock-hero-common/ui/src/highway/highway_atlas.{h,cpp}`). Two new glyphs (full chevron,
  quarter chevron) do not fit in one free cell.
- **Runtime rasterization already exists**: the atlas builder rasterizes a procedural fallback
  head with JUCE when the PNG is absent, so drawing new cells at atlas-build time is an
  established pattern, not a new capability.

## Design (recommended; open questions below)

1. **Indicator semantics**: show the curve's maximum bend target — `max(semitones)` over the
   note's bend points. Full chevrons = whole semitones of that maximum; add one half-size
   chevron when the fractional part is a quarter curl (≈0.5 with tolerance). Compound bends
   (up–down–up) show the max on the head; the tail curve carries the shape. Per-point markers
   along the tail are out of scope.
2. **Glyph source — decided on looks, not convenience** (user directive 2026-07-26): the choice
   between (a) runtime-rasterized chevron cells (JUCE, same channel scheme, appended to a grown
   atlas grid — the fallback path gains the same cells for free) and (b) chevrons authored into
   an expanded atlas PNG in Charter's style is a *visual-quality* decision that cannot be made
   without seeing both. Explicitly ruled out as a rationale: keeping the shipped PNG untouched.
   The texture library is expected to grow; avoiding asset expansion to save work is never a
   valid reason (this generalizes beyond this plan). Execution order: build (a) first — it is
   cheap and immediately reviewable in the preview — then judge with the user whether the
   procedural look holds up or the cells should be re-authored as (b); the renderer and layout
   work is identical either way, so nothing is thrown away. `HighwayAtlasLayout` is a pure
   grid — growing it is arithmetic, and it is already headlessly tested.
3. **Placement**: chevrons stack outward from the head along the lane's drawn bend-lift
   direction — **decided 2026-07-26: the arrows point where the drawn curve goes**, downward on
   `highwayBendInverted` lanes, so the head annotation always predicts the tail on screen —
   sized relative to the head quad, tinted and faded with the head (same composite rules as
   technique markers). They ride the head anchor, so they pin at the hit line with the head
   while sounding.
4. **Prebends**: same indicator — the chevrons on the approaching head are exactly the "bend
   before you pick" cue a prebend needs.

## Open questions

- **Tolerance policy**: quarter detection should share a tolerance constant with whatever the
  editor's bend authoring quantizes to — verify what values real charts carry (corpus check)
  before hard-coding 0.25/0.5/1.0 buckets.
- **Procedural vs authored cells**: deferred until both are seeable — see design point 2 for
  the decision rule and execution order.

## Phases

1. **Core counts helper + tests**: `highwayBendChevronCounts(bend)` → `{full, quarter}` beside
   `highwayBendSemitonesAt` in the common-core highway feature; Catch2 cases for empty, 0.5,
   1, 2, 3, 1.5, compound up–down, and prebend curves.
2. **Atlas cells**: extend `makeHighwayAtlases` composition (grid growth + two procedural
   chevron cells in both the reference and fallback paths); extend the layout unit tests for
   the new grid arithmetic.
3. **Renderer composite**: emit the chevron marker quads in the head draw via the existing
   `push_marker` pattern, honoring head fade, tint, and the decided direction policy.
   Verification: build + touched tests via `.agents/rockhero-build.ps1`, then a visual pass in
   the editor 3D preview over a chart with half, whole, step-and-a-half, quarter, and prebend
   bends.
