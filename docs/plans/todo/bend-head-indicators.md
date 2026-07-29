# Bend-Amount Chevrons on Note Heads

Status: **SHIPPED 2026-07-28** with the procedural glyph pass (design point 2's execution
order): `highwayBendChevronCounts` in `highway_tail.{h,cpp}` (tested), `HighwayAtlasLayout`
generalized to rectangular grids with the head atlas composed into a 4×5 canvas whose appended
row carries two runtime-rasterized chevron cells (both the reference and fallback paths, cells
16/17, never gated on `reference_cells`), and the renderer stacks the chevrons outward along
the drawn bend-lift direction (v-mirrored on inverted lanes), riding the head anchor with its
fade and tint. Tolerance settled as snap-to-nearest-half-semitone (the GP importer's
quarter-tone quantum). **Indicator semantics revised on sight (user, 2026-07-28), superseding
design point 1's maximum**: the head announces the curve's FIRST target — what the player must
bend to as the note arrives — and every later bend point whose snapped amount changes the
target announces its own smaller stack on the tail at its point, so compound bends read stage
by stage (a release to zero draws nothing but resets the tracker, so a re-bend re-announces).
REMAINING: the procedural-vs-authored looks decision — judge the procedural chevrons in the
preview against half, whole, step-and-a-half, quarter, prebend, and compound bends, and
re-author into the PNG only if they do not hold up.

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
