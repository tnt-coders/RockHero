# Bend-Amount Chevrons on Note Heads

Status: **SHIPPED 2026-07-28**, five passes on sight the same day: chevron *stacks* were
rejected as clutter (symbol count scaled with the amount), head amount *figures* were rejected
as unreadable at speed, and the dashed *target rails* were removed as redundant furniture once
the tail itself carried the amount (fifth pass). The shipped notation: **presence** — ONE `^`
chevron marker on the head (the source-game notation's own bend cue;
the atlas's formerly empty cell 11, present in both atlas paths, never gated on
`reference_cells`), rotated to point along the drawn bend-lift direction, sized at 0.6 of the
cell after the full-cell footprint read far too large over the head; **amount and stages** —
the tail's own geometry: physical lift height (`bend_lift_per_half_step` × semitones) plus
slope shading, the source-game notation's approach. The chart stores plain semitone doubles with
no special notation (user check 2026-07-28); the half-step display snapper built for figures
and rails was deleted with them. `HighwayAtlasLayout` is rectangular-grid generalized
(tested).

Fourth pass, same day: the procedural chevron did not hold up next to Charter's authored
cells, so the glyph was authored offline (PIL harness compositing candidates with the exact
`fs_texture_tint` math; user judged contact sheets interactively: candidate A, squished to
the tap arrow's footprint, squared butt-capped leg ends like the pop symbol, sharp mitered
tip) and baked into `notes.png`'s formerly empty cell 11 (the atlas stays 256×256 — the plan's
grown-grid step existed for a two-cell design and was retired with it; user question
2026-07-28). The reference asset uploads verbatim — no composition step, no legacy
accommodation (user rule) — and the procedural chevron survives only in the no-asset
fallback, the same degradation policy as every other head cell. The sustain tail also gained
slope shading
(`g_tail_slope_shade_*` in `highway_renderer.cpp`): per-vertex brightness follows the
centerline's pitch slope — climbs brighten toward white, releases darken — so bend strength
reads from the tail itself even at screen center, source-style. OPEN QUESTION (user, fifth pass):
single chevron always vs. a stack showing the max bend over the duration — recommendation is
single (stacks were already rejected as clutter, a max-stack misleads mid-compound-bend, and
the tail now carries the amount).

Sixth pass on sight (2026-07-28): the chevron was re-baked flatter and wider (slope ~0.35 vs
~0.6, vertically centered in cell 11; fallback painter matched, and its miter-normal bug —
lower-side n2 collapsing the apex into a flat top, latent under the always-present asset —
fixed), and it moved off the head to the bend-lift side (`g_bend_marker_offset_heads` head
half-heights above the note, below on inverted lanes, still flipping with the curve; lowered
1.3 → 0.95 on the seventh pass so the glyph touches the head art like the reference
notation's chevron, user 2026-07-28). The
"hard corners" in bent tails were adaptive-sampling starvation, not the curve math: sample
count was measured from the straight flat lane span, which ignores the bend's vertical lift
(and a slide's lateral travel), so mostly-vertical tails got a handful of samples — now
measured as projected arc length over a 16-segment probe of the modulated centerline. The
slope-shade clamp's hard saturation knee was replaced with tanh.

Seventh pass (2026-07-28, user report on the Periphery m118–119 chained bends: "rigid and
choppy... not intense enough... realistic to the actual pitch"): the GP import was audited
against the file and is faithful (value/50 → semitones, so 100 = whole step; plateau points
kept; offsets percent-of-sustain), so both fixes were display-side. (1) `highwayBendSemitonesAt`
moved from per-segment smoothstep to monotone cubic Hermite with Fritsch–Carlson tangents
(SciPy-PCHIP-style): same-direction control points now pass with continuous nonzero velocity
instead of easing to a flat shelf at every point — the terracing that read rigid — while
plateaus stay exactly flat, reversals turn at rest, and the FC limits forbid overshoot; at a
reversal both tangents are flat so the old smoothstep values (and tests) still hold there.
(2) `bend_lift_per_half_step` 0.28 → 0.35 = exactly one string-lane gap per semitone, the
pitch-true reading in the board's own vertical unit (a whole-step bend visibly crosses two
lanes; source's own convention quantizes bend targets to lane positions per the notation guides).
Vibrato depth is authored in semitones and rides the same scale deliberately. REMAINING: judge
the flatter glyph, the off-head offset, the fluid curve and pitch-true lift, and the
slope-shade gain/depth in the preview.

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
