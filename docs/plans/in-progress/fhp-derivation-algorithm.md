# The Finalized FHP Derivation Algorithm (#167)

**Status: PROPOSED 2026-09-05, awaiting the user's sign-off. Not yet implemented.**

This is the outcome of the FHP accuracy push: a ground-truth dataset of 4,555 professionally
reviewed arrangements (442,323 authored hand-position anchors; the dataset lives in local
scratch, outside the repo, per the corpus firewall), two independent statistical analyses run
against it, a ten-facet research pass over position pedagogy / biomechanics / the
automatic-fingering literature / charting practice, and a final expert synthesis in which every
rule below was measured, not argued. It supersedes the study in
`docs/plans/todo/fhp-corpus-derived-generation.md` (whose "~35% of moves are forced" and 0.8 s
phrase-rest rule are both now refuted) and, once implemented, replaces the body of
`generateFretHandPositions` in `rock-hero-editor/core/src/project/gp_chart_builder.cpp`.

The headline model shift: the authored anchor stream is not a coverage-satisfier with a shift
heuristic. It is a **figure-segmented floor tracker** — partition the fretted onset stream into
runs, place each run's anchor on its own floor (the index-finger fret), and hold the window
while written rings sound. Where floor placement and minimal-shift disagree, professional charts
pick the floor 82% to 3%: minimal shift — the current generator's core — is the wrong prior.

## The algorithm

One O(n) forward pass over onset groups, bounded lookahead (the run scan), in execution order.
Tags: **[LAW]** fixed in code; **[PARAM]** a named parameter with a default.

1. **[LAW — 99.97% zone invariant]** A coverage group is the fretted, non-tap notes of one onset
   instant; open strings and tapped notes never constrain the window.
2. **[LAW — 9,958/9,958 corpus slides]** A pitched slide contributes its destination fret as a
   coverage demand at the slide's end; the slide is the sanctioned carrier of any shift it
   causes.
3. **[LAW — figure segmentation]** Close the run at: a chord onset (≥2 fretted notes), a bar
   line, a section/phrase start where one exists, or hull overflow past max(4, current hull).
4. **[PARAM rest_break_seconds = off; range off / 0.5–3.0]** No rest break — a rest's measured
   move-hazard is 10–14%, and every tested value loses agreement (−0.17 at 0.5 s). A rest is
   where a move *lands*, not what triggers one.
5. **[LAW — hysteresis, single-peaked sweep: hull 0→86.2, 1→88.0, 2→85.2, any→79.7]** A run
   whose fretted material lies on **one fret** the standing window covers at the standing width
   emits nothing — a restatement, not a move.
6. **[LAW — pinned-finger certainty + 22× measured suppression]** No unforced re-derivation
   while a plain fretted **written** ring struck earlier still sounds; the move re-derives at the
   next run head after the ring ends. A forced escape still moves (99.20% of forced onsets
   move).
7. **[LAW — anchor ≤ floor 99.902%, == floor 88.8%; beats minimal-shift 82.1 : 3.1 where they
   differ]** Anchor = the run's minimum fretted fret (the index-finger floor). Never above it.
8. **[LAW — 98.25% default-4; wide anchors hug the hull 87.1%]** width = max(4, run hull);
   never below 4, never widened to absorb a held note.
9. **[LAW — 13/8,465 corpus exceptions]** Clamp anchor to [capo+1, 24−width+1], floor winning.
10. **[LAW — zero-lead 95.5%; "early" authored anchors are leading open strings 73.75%]** Emit
    at the first note the run serves, counting leading open strings; never anticipate further.
11. **[LAW — 14.2% of authored transitions are bookkeeping]** Emit only when (fret, width)
    changes.

### Measured score of exactly this algorithm (all 4,555 arrangements)

| | exact | within-1 | churn (authored) | coverage |
|---|---|---|---|---|
| **ALL** | **88.03%** | **94.81%** | 7.27 (7.58) | 99.938% |
| Lead | 88.67% | 95.12% | 8.39 (8.44) | 99.891% |
| Rhythm | 91.51% | 97.26% | 5.52 (5.48) | 99.996% |
| Bass | 77.15% | 87.45% | 9.53 (11.37) | 99.883% |

Churn = real moves per 100 fretted notes. Per-arrangement median 88.74%. Baselines on the same
harness: analyst A's candidate 86.32, analyst B's 86.13, the shipped generator ~70, the oracle
on authored segmentation 95.2 (segmentation is the remaining headroom). Always report **two
numbers**: coverage (a *possible* hand, ~99.9% everywhere) and exact agreement (the *charter's*
hand, 88) — the gap between them is the measured opinion space, and a single "correctness"
number for derived positions lies.

### Honest residuals

- ~5% non-index-finger placement: a fingering datum the chart model does not carry (see the
  fingering extension below). Fingering-aware placement measured +1.48 on the arrangements that
  state fingers.
- ~4–6% box-coherence re-anchors: the mechanism is confirmed (among featureless unforced moves,
  the landing equals the anchor of the figure's previous occurrence 65.0% vs a 25.5% null — a
  2.5× lift on every part), but every deterministic form tested over-triggers and loses ~2 pts.
  Mechanism named, rule refused.
- Bass trails (77.15): the 3-fret low-frame idiom is visible in the error mass (24.4% of bass
  errors sit at floor −2) but has no derivable trigger; the open-string-pivot hypothesis is not
  bass-specific; the higher unforced tolerance is structural. Same algorithm for bass, residual
  stated, no fake parameter.

## The profiles verdict

The question: should users save FHP "profiles" — parameter sets that fully define every non-law
case, tuned to a signature look, possibly artist-fitted — or does one sensible default win?

Measured answer: **one school, one sensible default; no artist presets.** The corpus shows no
charter/artist clustering (ICC ≤ 0.25 on every style metric; the visible extremes are content —
open-position strumming vs thrash — not style). The honest knob inventory collapsed under
measurement: `hysteresis_hull` is a law (every other value loses 2–8 pts), `width_default` is an
invariant, the placement-offset split is finger-shaped rather than parameter-shaped (no constant
or conditioner beats 0), no bass-specific parameter earned its keep, and a churn/style slider
was refused because synthetic unforced moves cannot be placed better than 65% even knowing the
mechanism — a slider would inject noise, not style. The surviving parameters:
`rest_break_seconds` (default off) and `section_breaks` (default on, ±0.02). Artist-fitted
presets would imitate repertoire, not hand style: fake.

Recommendation: laws fixed in code; the named constants and the two parameters reachable per the
standing no-hidden-style-constants requirement; **no profile UI**. The charter's signature look
lives in hand-editing the generated stream in the editor — and, later, per-note fingering, the
one lever that measurably personalizes output. (User ruling pending; the profiles product idea
stays recorded and can be revisited if a second charting school ever shows up in data.)

## What changes in RockHero (implementation deltas)

- **Deleted** from `generateFretHandPositions`: the minimal-shift drag core, the boundary/rest
  re-anchor machinery and `g_fhp_phrase_rest_seconds`, the pinned-finger window union, the
  held-hull slide reshape (sub-4 widths disappear), and the drag special cases. **Replaced by**
  the 11-rule pass — net code deletion. Slide keyframes become ordinary coverage events: the run
  walk re-derives at the destination floor, so "the slide is the shift" needs no special code.
- **Data vs render**: anchors state on-onset truth (rule 10); the highway and camera LEAD by
  ~0.8–1.2 s and animate moves over a fixed ~300–450 ms minimum-jerk curve (measured human shift
  kinematics; smootherstep is exactly that profile) — a projection-layer transform, never early
  anchors. This also answers the open camera-timing question (#153): the drift speed was right,
  the timing must lead.
- **The let-ring coupling** consumes the emitted FHP stream: derived spans clip at any emitted
  move not carried by a slide. Rule 6 concentrates those clips where a hand demonstrably moved,
  which is what makes the coupling honest.
- **Acceptance harness**: the sighted-offender worklist (pins first, in the scratch reference
  registry) re-measured after the reimplementation; sighting #1 traces to the deleted
  rest-break + minimal-shift pair. Ring suppression's real effect must be re-measured on GP
  imports, whose written durations are dense where the ground-truth corpus's sustain field is
  sparse.
- **Fingering extension (future)**: an optional finger on `ChartNote`; rule 7 becomes
  `floor − (finger − 1)` where a finger is stated — one substitution, +1.5–1.9 measured, and it
  subsumes the placement-offset "style" entirely. Never derive fingers heuristically: position
  is objective (97% expert agreement), finger is not (67%).

## Open rulings for the user

1. Sign the 11-rule algorithm (or amend rules before the build).
2. The profiles question: accept the no-profile-UI recommendation, or overrule and pick which
   parameters surface anyway.
3. Whether to add the optional fingering field to the format (import Guitar Pro's fingering
   where authored) — the single largest remaining accuracy lever.
