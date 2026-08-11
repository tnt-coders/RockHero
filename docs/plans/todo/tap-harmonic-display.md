# Tap-harmonic display: what is drawn today, and the decisions never made

**Status:** Unstarted, and deliberately so — the user has never specified how a tap harmonic
should be *presented*, so nothing here is signed and nothing is executable until it is walked
with them. This plan exists because 2026-08-10's verification pass fixed two consistency bugs in
the current treatment (the tap light and the camera now agree with the head about where a tap
harmonic sits), which made it easy to mistake the current display for a designed one. It is not:
it is an inherited composite. This file records exactly what IS, so the future conversation
starts from facts.

Re-verify every claim against the code before executing — this is a `todo/` plan and may lag.

## What the format carries (settled, not in question)

A tap harmonic is `NoteAttack::Tap` plus `harmonic_node` — fully representable, including the
open-string form (fret 0, node only) that E4 explicitly admits, and nodes up to
`g_max_harmonic_node` (48), far past the drawn board. The node is the sounding pitch; for a tap
the node belongs to the PICKING hand (`nodeIsOnNeck` includes Tap; `frettingFingerOnNode`
excludes it).

## What the 3D board draws today (verified 2026-08-10)

- **A stopped tap harmonic** (fret > 0): the head draws at its NODE via the shared
  `highwayDrawnSoundingPosition` authority (exact for node ≤ 24; held at the board's edge past
  that — the plan-57 interim), wearing the *generic* technique base plus TWO stacked generic
  markers: the harmonic cell and the tap cell. Nobody ever designed "a tap harmonic's mark";
  this is two independent rules composing.
- **An open-string tap harmonic** (fret 0): takes the open-string BAR across the hand window —
  no head at the node, no harmonic cell, no tap cell — while the tap LIGHT lights at the node
  and the camera frames the node. So three systems say "the node" and the drawn note itself says
  "the whole window": a known gap against the two-surfaces/one-fact law, noted in
  `docs/developer/the-3d-highway.md` and tracked with the note-view unification watch item.
- **The tap light**: lights at the node and, since 2026-08-10, glides along the node path when
  the tap glides (before that it walked the stop path under a head that rode the node path).
- **The camera**: frames the node, including the open-string case (before 2026-08-10 the
  open-string skip ran first and the note could sit entirely off screen).

The 2D lane prints the node as the head's number with the diamond harmonic head shape — the
same generic-harmonic treatment, which reads acceptably there.

## The decisions that were never made (for the user)

1. **What a tap harmonic's head should look like.** Today's stacked harmonic-cell-plus-tap-cell
   composite was never sighted as a unit. Options range from keeping the composite, to a
   dedicated cell (note the 55-Q1 history: the V family is the picking-hand signature, and a tap
   IS right-hand), to letting the light carry the tap-ness and the head only the harmonic-ness.
2. **What the open-string tap harmonic draws.** The bar-with-no-marks form contradicts the light,
   the camera, and the 2D lane. A node-anchored head would close the gap; a marked bar might
   read better for the hand-window semantics. This is the sharpest half-specified case.
3. **Whether the two-hand-tap interplay needs anything.** The fretting hand is elsewhere (taps
   are excluded from the FHP track by design); the light is the only picking-hand cue. Whether
   the head should hint the two-hand nature at all is open.

## What this plan does NOT own

- **Positions past fret 24** are G57-BOARD's question (`57-positions-past-the-drawn-board.md`),
  including whether the domain shrinks to the board. Nothing here decides display for node > 24;
  the interim clamp stands meanwhile.
- Any atlas-cell authoring goes through the texture pipeline and plan 54's cell conventions.
- W9's harmonic-cluster rulings may reshape the underlying model; check them first.

## Sequencing

Blocked on a walkthrough with the user (the questions above). Independent of G57-BOARD except
where an answer wants to draw past the board. Cheap to start: every question can be mocked on
the existing atlas before any cell is authored.
