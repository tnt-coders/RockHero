# Right-Hand Tap Lighting and Tapped Chord Boxes

Status: deferred plan, designed 2026-07-28 (user + assistant design discussion), not started.
Re-verify the projection and renderer entry points named below against the current code before
implementing.

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
