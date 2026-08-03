# Plan 55 — Pick-Slide Notation

**Status**: Decision-gated (55-Q1 visual treatment; 55-Q2 editor authoring surface) — Phases 1–2
are executable ahead of the gate, Phases 3–5 consume its answer. Date 2026-08-03; baseline
`master @ 182faedb` plus the same-day pick-slide import revert. The inventory below was taken
this session against the live tree and the Van Halen import that motivated the plan.

## Goal

Pick slides (pick scrapes) become a first-class notated technique with their own chart field,
import path, and visual language — a **right-hand** gesture that never touches the fret-hand
position system. Guitar Pro's dedicated pick-slide flags import losslessly, the editor can author
the technique, and both the 3D highway and the 2D tab render a treatment designed for the gesture
instead of borrowing fretting-hand vocabulary.

## Why a new notation (the decision trail, 2026-08-02/03)

1. **The borrowed-vocabulary experiment failed on sight.** Mapping pick slides onto existing
   chart vocabulary — full mute + tremolo + unpitched trail-off, the closest combination the
   chart can express, since it has no pick-slide field — was built and REVERTED the same day:
   Guitar Pro notates the gesture on fret-0 dead strings, so the trail-off has no fret travel
   and the result read as flat muted bars, not a scrape (sighted in the Van Halen import,
   measure 20).
2. **It is a right-hand technique** (user rule 2026-08-03): the fretting hand is not involved, so
   nothing about it may move, anchor, or dip the FHP window. The codebase already has the
   precedent: tap-only onsets are transparent to the fret-hand generator and float above the
   window (`gp_chart_builder.cpp` generator rules; the right-hand tap-lighting system).
3. **Direction is the only semantic content.** The gesture is "scrape down (or up) the neck for
   this duration" — no start fret, no landing fret. Any representation that demands frets forces
   invention, which the import policy forbids (the no-second-guessing rule).

## Non-goals

- **Measure-3-style figures.** A dead (fret-hand-muted) chord carrying ordinary slide-out flags
  is a LEFT-hand gesture — a muted hand slide — and keeps its existing import (regular muted
  notes with unpitched trail-offs) untouched (user classification 2026-08-03). No heuristic may
  reclassify dead+trail-off as a pick slide.
- **Detection.** Scoring/detecting scrapes (broadband noise sweep) belongs to plan 22's technique
  matrix; this plan only guarantees the chart carries the technique so detection can find it.
- **A right-hand placement cue.** Kin to roadmap 25-Q5 (pinch harmonic right-hand cue); if a
  "where to scrape" cue is ever wanted, it extends this notation rather than adding fields now.

## Constraints

- (a) Layering: chart model + serialization in `common/core`; projection field in the headless
  scene model; renderer treatment in `common/ui`; importer mapping in `editor/core`. No product
  cross-dependencies.
- Format policy: the chart JSON field is added IN PLACE — no format version bump, no migration
  (project rule; early stage). Follow the developer guide's **package-format field checklist**
  (`docs/developer/` recipes) for the silent steps.
- FHP transparency is a hard invariant: no placement, dip, restore, or exit machinery may fire
  for a pick-slide note. Tests must pin this.
- Builds through `.agents/rockhero-build.ps1`; clang-tidy stays user-triggered.

## Verified current-state inventory (2026-08-03)

- **Guitar Pro encoding**: gpif `Property name="Slide"` flags **64 = pick slide down,
  128 = pick slide up** (verified against the Van Halen file: measure 20 carries flags 64 on two
  fret-0 dead strings, quarter note). Our parser already passes the raw flag byte through
  `GpNote::slide_flags` (`gp_score_parser.cpp`); the chain resolver masks `1|2|4|8|16|32` only,
  so 64/128 are currently INERT — the note imports as a plain dead note and the gesture is lost.
- **Chart model** (`rock-hero-common/core/.../chart/chart.h`): `ChartNote` carries mute, tremolo,
  bends, `slides`, `slide_out` — no pick-slide field. Serialization lives in
  `chart_document.cpp`; invariants in `chart_rules.cpp`.
- **FHP machinery that must stay untouched by the new notation**: the phrase-aware generator
  (tap-transparent precedent), `resolveSlideIns` (scoop dips), `resolveSlideOutExits`
  (trail-off rides/returns) — all keyed off slides/slide_out, which a pick-slide note will not
  carry.
- **Render surfaces**: highway techniques render from `HighwayNoteView` (projection at
  `highway_projection.cpp`); the 2D tab paints from the shared paint core
  (`tab_paint_core.cpp`). Neither has a scrape treatment.
- **Theming coordination**: any new colors introduced by the chosen treatment enter plan 54's
  `HighwayTheme` struct rather than becoming new file-scope constants (the same coordination
  rule plan 25 carries).

## Decision gates

- **55-Q1 — highway visual treatment.** Produce composite mockup sheets (the box-mute sight-sheet
  workflow) of the candidates over a board background, then the user picks:
  - (A) **Full-board scrape band**: a wide serrated ribbon spanning all lanes, sweeping in the
    travel direction across the note's duration; unpitched-dimmed with a tremolo-like shimmer.
  - (B) **Right-hand-lit sweep**: no note head; a bright band in the tap-lighting language
    traveling along the floor across the duration — the picking hand's visual family.
  - (C) **Glyph + wake**: a dedicated head glyph (lane-free) with a broad dimmed wake trailing
    in the travel direction.
  The 2D tab marker is decided in the same gate (GP draws an angled line with "P.S."-style
  labeling; ours should stay Charter-toned).
- **55-Q2 — editor authoring surface.** Recommendation: fold into plan 40 Phase 5's
  technique-editing surface as a per-note-validated cycle (None → Down → Up) under the §9a
  apply-where-valid policy (never blind-cycle a mixed selection); confirm with the user before
  wiring a keybind.

## Phases

1. **Chart model + format.** `ChartNote::pick_slide` (enum `PickSlide{None, Down, Up}`), JSON
   serialization in place, invariants: mutually exclusive with `slides`/`slide_out`/bends (the
   gesture has no fret content); sustain remains the gesture's span; `hasSustainTechnique`-class
   rules treat it as a tail-protecting technique so the trim never drops the span. Unit tests in
   the core suite; package-format-field checklist walked.
   **Exit criteria.** Field round-trips through save/load; invariant tests reject the excluded
   combinations. **Verification.** Build via `.agents/rockhero-build.ps1`; common-core suite.
2. **Import.** Chain resolver maps flags 64/128 → `PickSlide::Down/Up` (consuming the flag before
   the slide-out branch), keeps the notated duration as the span, and leaves mute/tremolo alone
   (the notation carries the look; the chart stays honest). FHP transparency verified by test:
   no placements fabricated, no window motion attributable to the note. The measure-3 figure
   pinned by a regression test as unchanged. Normalization policy doc gains the rule.
   **Exit criteria.** Flags 64/128 import losslessly; FHP-transparency and measure-3 regression
   tests green. **Verification.** Build; gp-import suite
   (`.agents/rockhero-build.ps1 -Targets all -RunTouchedTests`).
3. **Projection + 2D tab.** `HighwayNoteView` field; tab marker per 55-Q1's answer; shared paint
   core + pixel tests.
   **Exit criteria.** Projection carries the field; tab renders the 55-Q1 marker.
   **Verification.** Build; common-core, common-ui, and editor-ui suites (paint/pixel tests).
4. **Highway treatment.** Implement the 55-Q1 winner; sight-iterate (expect retuning rounds);
   any new colors go through plan 54 coordination.
   **Exit criteria.** User signs off the treatment on sight. **Verification.** Build; sight pass
   over the Van Halen measure 20; no new file-scope color constants (plan 54 coordination).
5. **Editor authoring + acceptance.** 55-Q2's verb; final acceptance bundle (build, full suites,
   re-import sight pass on the Van Halen measures 3 and 20 — 3 unchanged, 20 rendering the new
   treatment).
   **Exit criteria.** The Acceptance section below holds in full. **Verification.** The
   sanctioned end-of-plan bundle (build, full suites, pre-commit), then the two-measure sight
   pass.

## Acceptance

- Measure 20 of the Van Halen import renders the chosen pick-slide treatment; measure 3 is
  byte-identical to its pre-plan import.
- A chart round-trips the field through save/load; the editor can author and remove it.
- No FHP output differs on any chart without pick slides; on charts with them, FHP output is
  identical to the same chart with the pick slides stripped (the transparency invariant, as a
  test).
