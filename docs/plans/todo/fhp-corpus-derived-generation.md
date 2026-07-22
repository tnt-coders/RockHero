# Corpus-Derived Fret-Hand Position Generation

Status: deferred plan, written 2026-07-21. Re-read the current generator in
`rock-hero-editor/core/src/project/gp_chart_builder.cpp` (`generateFretHandPositions`) and the
`FretHandPosition` model before implementing; the corpus paths and algorithm sketch below reflect
2026-07-21 state. The maintained plain-English spec of the shipped algorithm (and the import
sustain policy beside it) is "GP chart normalization policy" in
`docs/developer/the-project-lifecycle.md`; this plan holds only its eventual replacement.

## Problem

Guitar Pro scores carry no fret-hand positions, so GP import generates them. The shipped
algorithm (user decision 2026-07-21: "start with a simple algorithm") is a greedy minimal-shift
window walk: the hand covers `[fret, fret+width-1]` (width four unless one onset spans wider),
open strings never constrain, and an onset outside the window shifts the anchor the shortest
distance that covers it. That guarantees every note lands inside a position region, but it is not
how a good charter thinks:

- **No lookahead** — a single low passing note drags the anchor down and the next onset drags it
  back, producing churn a human would absorb with one stretched position or one deliberate shift.
- **No phrase awareness** — humans move the hand at phrase boundaries (section starts, rests,
  string-set changes), not mid-lick; the greedy walk moves exactly at the first misfit.
- **Mid-sustain slide targets are uncovered** — a slide's waypoint fret only pulls the anchor at
  the *next onset* (shift/legato slides self-heal because the next note sits at the target;
  unpitched trail-offs never move the hand). Authored charts move the anchor along the slide.
- **Minimal shift is only one prior** — real charters weigh staying low on the neck, keeping the
  index on the phrase's floor fret, and barre-shape reuse; which prior wins varies by context.

## Direction (user, 2026-07-21)

Parse a large set of carefully hand-authored charts and derive a generation algorithm that
estimates the most readable positions from the notes alone.

## Ground truth available

The `.rock` reference corpus (39 packages, 135 charts, converted from RS official charts) carries
**authored** fret-hand positions produced by professional charters — a labeled dataset pairing
note streams with expert anchor decisions, already in our own chart model. More converted charts
can grow the set through the existing converter pipeline.

## Sketch

1. **Extraction** — for every corpus chart, emit (note-stream window, authored FHP track)
   pairs; normalize tuning/capo out.
2. **Metric** — corpus agreement: fraction of notes whose authored anchor equals the generated
   one (exact and within-1-fret variants), plus a churn metric (moves per 100 notes) so
   over-fitting to jitter is visible.
3. **Candidate algorithms** — score the shipped greedy walk as the baseline, then iterate:
   lookahead-k window fitting (dynamic programming over move costs is the classic shape —
   minimize moves + distance + off-phrase penalties), phrase segmentation from rests/sections,
   slide-aware anchors, stretch tolerance by neck region (frets are narrower high up).
   Check the Existing-Libraries rule before hand-rolling any optimizer.
4. **Acceptance** — a candidate replaces the baseline when it beats it on corpus agreement
   without exceeding its churn; disagreements worth keeping become documented deliberate
   divergences.

Until then, `generateFretHandPositions` stays the single authority and its conversion note
("simple window walk; verify") keeps the guess observable in the import log.
