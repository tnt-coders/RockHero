# Right-Hand Tap Lighting and Tapped Chord Boxes

Status: SHIPPED 2026-07-28 (same day as the design). `HighwayTapOnsetView` +
`makeHighwayTapOnsets` in `highway_view_state.h` (filled by `makeHighwayViewState`, tested in
`test_highway_projection.cpp`); the tapping-hand light pass and tapped chord boxes in
`highway_renderer.cpp` (envelope/tint constants `g_tap_light_*`, eyeball-tunable). Plain chord
boxes now count only non-tap members. First-sight refinements (user, 2026-07-28): each entry
carries a light *path* — onset through pitched glide waypoints to the fingers' release — so the
light holds through sustained contact and morphs with tapped slides (unpitched trail-offs
release from the last pitched station), and the envelope tightened (rise 0.35 → 0.18, decay
0.2 → 0.1 — the original read as too much light around each note). The rise then dropped its
wall-clock constant entirely (user rule 2026-07-28, consistency with the left hand): each tap
onset carries a projection-derived `ramp_seconds` from the fret-hand placements' own arrival
rule — the minimum-sustain-distance margin at the onset's meter, clamped so the rise never
reaches backward past the previous tap onset's release. Only the arrival *timing* is shared:
the light still rises from zero and vanishes after the decay, because the tapping hand is
discrete by design — there is no previous position to morph from. Lane-border ribbons and
board-face fret lines also treat tap paths as windows (visible tier, activation horizon, and
envelope brightening at now), per-line max-combined so hand overlap self-deduplicates. The
open questions below remain open.

## Problem

Two-hand tapping now imports correctly (left-hand taps are hammer-ons; right-hand taps are
`NoteAttack::Tap`, invisible to chord-span derivation), but the tapping hand has no presence on
the highway beyond the note heads and their orange floor numbers:

- Taps happen in the dark region far above the left hand's window light, with no lit basis under
  them.
- Two or more taps struck together get no chord box — span derivation deliberately ignores taps,
  and that must stay true for the *left-hand* shape concepts — yet a tapped dyad is a real chord
  the player reads as one gesture.

## Settled decisions (user rules 2026-07-28)

1. **Everything right-hand is derived presentation state.** Right-hand windows and tapped chord
   boxes are computed from the `Tap` notes at projection time. Nothing is authored, stored in
   `song.json`, or user-configurable — no format change, and authored `.rock` charts get the
   feature identically to GP imports. If the derivation reads wrong, the fix is a better rule,
   exactly like the left-hand FHP generator.
2. **Tapped chord boxes are NOT `ChartShape`s.** Shapes are left-hand posture semantics; three
   consumers must remain untouched by tap chords: the held-chord-under-tap arpeggio rule (a tap
   inside its own span would flip it), the FHP generator's held-shape coverage (a tap shape would
   drag the left anchor up), and ring-through posture joins. Tap boxes are their own derived
   view-state element that merely reuses the box *geometry* at render time.
3. **Per-tap light envelopes — deliberately NOT merged into runs** (user decision): the player
   genuinely lifts the finger between consecutive taps, so the light dips between taps to match
   what the body plays. Each tap onset gets one envelope (rise shortly before the hit, decay
   shortly after); overlapping envelopes on the same lanes combine by max. This makes density
   self-scaling with no grouping threshold: spaced taps pulse, moderate runs dip between taps,
   ultra-dense runs blur toward continuous light — and a run walking across the neck has its
   light *follow* the tapping hand instead of spanning the whole run's extent.
4. **Window extent = the fret extent of the simultaneous taps at that onset** (minimum one
   lane). No 4-wide convention — that is a left-hand reachability concept — and no cap.
5. **Tapped chord box per tap onset with two or more simultaneous taps**, planted in that
   onset's envelope. v1 renders per-onset boxes with no rule-11-style restrike merging (taps are
   percussive; a held box over a tapped chug may misread) — revisit after real charts.

## Sketch

- `rock-hero-common/core` highway projection: derive per-tap-onset light events
  `{seconds, fret_low, fret_high, chord}` from the note stream's `Tap` attacks; new field on
  `HighwayViewState`; unit tests beside the other derived tables (extent, single vs chord,
  overlap independence).
- Renderer: contribute the envelopes to the per-fragment window light as a second light source
  (max-combined with the left window's), with rise/decay constants tuned by eye in the preview
  (likely asymmetric: longer rise for read-ahead, shorter decay for release). Draw the tapped
  chord box with the existing box geometry, keyed as tapped.
- Visual differentiation of the right-hand light (tint/intensity vs the left window) is a
  tuning decision made in the preview, not up front.

## Open questions (decide later, not blockers)

- 2D tab view: should a tapped dyad get a chord bracket there too?
- Camera: `makeHighwayCameraTarget` currently frames taps by scanning notes directly; it could
  consume the derived envelopes instead — a unification, not a behavior need.
- Restrike merging for repeated identical tapped chords (see decision 5).
- Exact envelope rise/decay constants.
