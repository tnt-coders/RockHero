# 2D Bend Redesign — Waypoint Heads

Status: **PARKED 2026-08-05** by user decision after a full design investigation: "I don't
really like any of our options for this right now so let's park this and revisit it later."
The research, corpus evidence, and settled sub-decisions below survive the park; the visual
option set does not — whatever revives this plan should re-open the visuals from the settled
foundations, not from the rejected sheets.

## The idea under investigation (user proposal, 2026-08-05)

Bends get the slide treatment: every `BendPoint` renders as a selectable waypoint token in the
2D tab lane, exactly where slide waypoints already render linked heads, so bends become
grid-editable by selecting and tweaking waypoints — eliminating any need for a separate bend
editor window. The curve between waypoints follows the same eased shape as the 3D view.

This aligns with (and would upgrade) plan 40 Phase 7, which already commits to direct
manipulation of bend points on the sustain tail (Alt+click add, drag with snap, numeric entry,
remove) — the waypoint idea makes those handles permanent, visible, typed tokens.

## Settled sub-decisions (survive the park)

- **Zero sits at the tail box floor; upward motion is the bend** (user, 2026-08-04).
- **The 2D pitch mapping is linear; the 3D lift is the physical non-linear one** (user).
- **The 2D scale must represent up to 3 whole steps** — the Guitar Pro maximum — with no
  compression and no clamping inside that range (user, 2026-08-05). A fixed linear
  3-steps-at-the-ceiling scale satisfies this, keeps heights comparable across notes, and
  never rescales under an editing drag; faint whole-step rails at the 1- and 2-step rows keep
  the thirds readable. (The corpus says 99.9% of real bends sit at or below half this scale —
  the amplitude cost on common bends is the known tradeoff, and it is part of why the rendered
  options did not delight.)
- **Color-typing a thin bend line is rejected on measured evidence**, not taste: hue
  discrimination on 1–2 px lines needs 14–19.5 ΔE for even 50% of viewers under best-case lab
  conditions (Szafir, IEEE TVCG 2017, measured on lines at exactly this size), and our
  conditions are worse (six saturated tail tints, absolute identification, hue already
  encoding string identity). Thin overlays stay white; type distinctions ride shape and
  structure; color is only trustworthy on wide elements (tens of pixels — token/head sized).
- **The eased curve is shared with 3D**: `highwayBendSemitonesAt` (monotone cubic Hermite,
  Fritsch–Carlson tangents) is the one bend-shape authority; a 2D implementation hoists a
  shared evaluator rather than duplicating the math.

## Corpus evidence (2026-08-05 scan; details in memory `bend-corpus-statistics`)

1,293 source-chart archives, 555,633 notes: 0.77% of notes bend; peaks: 96.6% ≤ 1 whole step,
99.9% ≤ 1½ steps, one single note > 2 steps. Bend+slide on the same note: 48 (~1.1% of bent
notes) — and in that format co-occurrence means true simultaneity. Bend+vibrato is 5× more
common than bend+slide. Bent notes carry 1 point 74% / 2 points 20% of the time.

## Research findings (2026-08-05, three web sweeps; key citations)

- **No tab or notation product edits bends via on-note waypoints** — Guitar Pro, TuxGuitar,
  MuseScore 4, Soundslice, Power Tab, TablEdit all use a modal dialog or side panel; Dorico's
  on-canvas Engrave handles move ink, not pitch. The interaction exists in DAWs: Ableton
  per-note MPE pitch envelopes (curve drawn on the note; click segment adds, click point
  deletes, drag snaps to pitch quantum; unselected notes dim) and Guitar Pro's own automation
  editor (beat-snapped draggable points) — bends just never got that treatment there.
- **Model validation**: GP3–5 stored bends as a free point list; GP6+ regressed to a fixed
  4-point schema and every importer since carries a `Custom` fallback. Our free
  `(offset, semitones)` list is the general form. Quarter-tone is the universal quantum
  (GP: 25/quarter; MuseScore: `bendAmountInQuarterTones`); step-fraction labels (¼ ½ 1,
  "full") are the universal vocabulary — `charterBendText` already matches.
- **Interaction consensus** for tight-lane point editing: modifier-click on the curve adds
  (DaVinci uses exactly Alt+click), select+Delete removes (bare-click-deletes is an Ableton
  outlier), two-axis drag with vertical snapping to the value quantum, points clamped so they
  cannot cross neighbors in time, right-click numeric entry as the escape hatch, no
  quantize-on-reselect (MuseScore's tracker documents that bug class). WCAG 2.5.8: 24 px
  pointer targets or ≥ 24 px between undersized ones; invisible hit padding must be revealed
  on hover to be worth anything (NN/g). No surveyed tool labels points permanently in-lane;
  the engraving convention is to label semantic anchors only (peaks), outside the lane, with
  hold continuation lines instead of repeated numbers.
- **Every tool had to decide how far a bend mark extends over the sustain** (alphaTab ships it
  as a setting; MuseScore auto-draws dashed holds). Waypoints make that explicit data — a
  genuine simplification over all surveyed tools.

## The rejected option set (for the record)

Rendered as judgment sheets (local scratchpad harness `bend_waypoint.py`, ports of the shipped
geometry/colors, supersampled): (1) today's straight-segment chips for reference; (2) circle
amount tokens at the string line — rejected sub-point: a circled "1" reads as fret 1;
(3) rounded-square tokens; (4) small squares (0.62×) giving the curve room back; (5) the curve
yielding to the slide diagonal during glide overlap; (6) tokens only, no curve. Bend+slide
coexistence was drawn as ONE composite pitch line (bend deflection riding the slide ramp — the
3D tail already composes both into one centerline via `makeHighwayTailSampleTimes`).
**User verdict: none of these look right yet.** The waypoint *editing* concept was not
rejected; the *visual* execution was.

## Open when revived

- The visual language for the tokens/curve (start fresh; the user may bring references).
- Bend+slide presentation (composite line vs yield vs something new) — bounded by the measured
  rarity above.
- Whether the onset chevron (3D's cue, drawn below the head in 2D trials) participates.
- Editing gestures land with plan 40 Phase 7 regardless of the visual outcome; fold the
  MuseScore pitfall list above into that phase when it starts.
