# Plan 55 — Pick-Slide Notation

**Status**: Decision-gated (55-Q2 editor authoring surface only) — 55-Q1 was resolved through the
full interrogation recorded in the decision trail, final user call 2026-08-03, so Phases 1–4 are
executable now and only Phase 5's authoring verbs wait on a gate. Date 2026-08-03; baseline
`master @ 182faedb` plus the same-day pick-slide import revert. The inventory below was verified
this session against the live tree, the Van Halen import that motivated the plan, and the local
corpus dataset.

## Goal

Pick slides (pick scrapes) become a first-class notated technique with their own chart stream,
import path, and visual language — a **right-hand** gesture that never touches the fret-hand
position system. A pick slide is not a note: it is its own event kind (onset + span + a
neck-position path the pick travels), so every technique exclusivity and the fret-hand
transparency hold **by construction**, not by validation. Guitar Pro's dedicated pick-slide flags
import losslessly, the editor can author and reshape the gesture's path, and both the 3D highway
and the 2D tab render it in the tap/tremolo-styled language with a numberless head.

## Why this design (the decision trail, 2026-08-02/03)

1. **The borrowed-vocabulary experiment failed on sight.** Mapping pick slides onto existing
   chart-note vocabulary — full mute + tremolo + unpitched trail-off — was built and REVERTED the
   same day: Guitar Pro notates the gesture on fret-0 dead strings, so the trail-off had no fret
   travel and the result read as flat muted bars, not a scrape (sighted in the Van Halen import,
   measure 20).
2. **It is a right-hand technique** (user rule 2026-08-03): the fretting hand is not involved, so
   nothing about it may move, anchor, or dip the FHP window. The codebase already has the
   precedent: tap-only onsets are transparent to the fret-hand generator and float above the
   window (`gp_chart_builder.cpp` generator rules; the right-hand tap-lighting system).
3. **The gesture carries a neck-position path, not fret semantics** (user extension 2026-08-03,
   superseding the earlier "direction is the only semantic content" ruling): a pick slide must be
   editable like regular slides — down to a chosen neck position, then up again, in one chain —
   so the gesture stores a start position plus a waypoint path, modeled the same way note slide
   chains are. Direction is *derived* from the path, never stored. The path's fret numbers are
   neck coordinates the PICK travels through, not fingerings — the fret hand stays uninvolved.
   Guitar Pro encodes only direction (flags 64/128 on dead strings), so import synthesizes a
   default path the user can then reshape.
4. **The visual answer is the unplayable tap+tremolo composition, interrogated and then
   deliberately kept** (user call 2026-08-03). The proposal: render the gesture as a tapped
   unpitched slide with tremolo — tapped tremolo is physically impossible to play, so the
   combination cannot be misread as a real fretted figure. The interrogation that stress-tested
   it, all evidence local or cited in this trail:
   - **Corpus survey** (the 4,100-arrangement local reference corpus from the FHP analysis;
     survey script in the session scratchpad): the corpus's chart format has **no standard
     composite** for the gesture — of 51,017 unpitched-slide notes, the 816-note
     tremolo+unpitched suspect population scatters across 8+ encodings (bare tremolo 483,
     tap+tremolo+vibrato 163, tap+tremolo 83, tremolo+vibrato 39, mute variants ~36, …); the
     tap+tremolo+slide triple covers only ~31%, and **zero** unpitched slides sit on fret 0
     there (they ride real frets, 5/7/9/12…). Community charting guidance for that format
     confirms no native scrape notation exists and that the composite confused players.
   - **Print convention research**: the real printed-tab convention is an X notehead plus a
     **wavy slanted line inside the string lanes** (Guitar Pro 8 and MuseScore 4 render pick
     scrape first-class this way); "P.S." text is only optional reinforcement — and was rejected
     here as failing to portray the gesture's aggression. SMuFL has no scrape glyph; every
     system composes one. BandFuse: Rock Legends shows no evidence of charting scrapes at all.
   - **A dedicated "noise ribbon" mark** (serrated sweep band on both surfaces) was proposed as
     the honest alternative and REJECTED by the user in favor of the composition's clarity.
   - **Why the composition now holds up**: with a real waypoint path (trail item 3), the slide
     component of the composition is *true* — the gesture genuinely travels the neck and the tail
     draws its actual path; the tremolo shimmer reads as rapid noise; and the tap glyph marks
     "right-hand-produced onset", consistent with tap's FHP-transparency precedent. The head
     rule splits by surface (user 2026-08-03): on the 3D highway the head is **numberless** —
     the board's X axis already places the scrape's start spatially, a fret digit would read as
     a fretting instruction on a gesture nothing frets, and the missing digit is the constant
     at-speed differentiator from a real tapped slide, which always shows one. In the 2D tab the
     head **shows the start neck position** — the tab's horizontal axis is time, so the digit is
     its only channel for where the scrape starts — wearing the white X full-mute marking so the
     number reads as an unpitched neck position rather than a pitch, and later path legs carry
     their targets the way unpitched-slide chips already do. The X composite on the 3D head is a
     sight-round option, not signed.

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

- (a) Layering: chart model + serialization in `common/core`; projection views in the headless
  scene model; renderer treatment in `common/ui`; importer mapping in `editor/core`. No product
  cross-dependencies.
- Format policy: the chart JSON stream is added IN PLACE — no format version bump, no migration
  (project rule; early stage). Follow the developer guide's **package-format field checklist**
  (`docs/developer/` recipes) for the silent steps.
- FHP transparency is structural (the generator consumes the note stream; the gesture never
  enters it) — and still pinned by test, per Acceptance.
- Builds through `.agents/rockhero-build.ps1`; clang-tidy stays user-triggered.

## Verified current-state inventory (2026-08-03)

- **Guitar Pro encoding**: gpif `Property name="Slide"` flags **64 = pick slide down,
  128 = pick slide up** (verified against the Van Halen file: measure 20 carries flags 64 on two
  fret-0 dead strings, quarter note). Our parser already passes the raw flag byte through
  `GpNote::slide_flags` (`gp_score_parser.cpp`); the chain resolver masks `1|2|4|8|16|32` only,
  so 64/128 are currently INERT — the note imports as a plain dead note and the gesture is lost.
- **Chart model** (`rock-hero-common/core/.../chart/chart.h`): `Chart` already models distinct
  content kinds as parallel sorted streams (`notes`, `shapes`, `fret_hand_positions`) — the
  precedent the new `pick_slides` stream follows. `ChartNote` keeps its "only event kind in the
  note stream" contract; `SlideWaypoint` is the reusable path element. Serialization lives in
  `chart_document.cpp` (per-stream arrays, fields omitted at defaults); invariants in
  `chart_rules.cpp` (typed `ChartError`, per-stream ordering/range rules).
- **Known option-A hazards this design avoids** (found in review): a flat note field would let
  `planRetypeFrets` anchor transposition on the gesture's junk fret and let
  `normalizeSustainOverlaps` silently truncate the gesture's span; the separate stream never
  enters those paths.
- **FHP machinery that stays untouched**: the phrase-aware generator, `resolveSlideIns`,
  `resolveSlideOutExits` — all consume the note stream, which a pick slide is not in.
- **Render surfaces**: highway techniques render from the projected views
  (`highway_projection.cpp`); the 2D tab paints from the shared paint core
  (`tab_paint_core.cpp`), which already draws the composition's glyph family: the tap attack
  triangle, the tremolo pointed-gem tail, and slide diagonals following waypoint chains. The
  existing note slide path cannot carry the gesture directly (fret-equal segments draw as ties;
  unpitched end chips print fret digits), so the gesture gets its own draw reusing those pieces.
- **Theming coordination**: any new colors introduced by the treatment enter plan 54's
  `HighwayTheme` struct rather than becoming new file-scope constants (the same coordination
  rule plan 25 carries).
- **Corpus dataset**: the 4,100-arrangement extraction (`dataset.jsonl`) from the FHP analysis
  survives in the 2026-07-28 session scratchpad and backed the survey above (local-only,
  never committed).

## Decision gates

- **55-Q1 — format and visual treatment. RESOLVED 2026-08-03** (full trail above). The settled
  design:
  - **Format — a separate chart stream, not a note field.** `Chart::pick_slides` holds
    `ChartPickSlide{position, string, sustain, fret, waypoints}`: onset, display-anchor string
    (which tab staff line and highway lane the head sits on), span in beats, the neck position
    where the scrape begins, and a `SlideWaypoint` chain (reused from note slides) for the path —
    down-then-up chains are just multiple waypoints. No direction field (derived per path
    segment), no `None` state (absence is absence from the stream), no attack/tremolo/mute/bend
    fields to combine — every exclusivity and FHP transparency are unrepresentable-by-
    construction. The gesture is inherently unpitched, so unpitchedness is a constant of the
    event kind, not data: there is no `SlideOut` analog to store, and **every** path segment
    renders in the unpitched trail-off language (user requirement 2026-08-03: pick slides look
    like unpitched slides) — `SlideOut` only ever adds an end offset + target fret, which each
    waypoint already carries. One validity rule the path adds over note slides: consecutive neck
    positions (start fret included) must strictly differ — the travel is what makes it a scrape,
    so it cannot sit still between path points, whereas note slides legitimately hold equal-fret
    segments as ties. JSON: a fifth top-level array, e.g.
    `"pickSlides": [{"position": "20:1", "string": 5, "sustain": "1", "fret": 12, "slides": [...]}]`.
  - **3D highway**: numberless tap-marked head at the anchor lane and start-position X; tremolo-
    shimmer tail that **follows the waypoint path** across the neck, wearing the unpitched
    trail-off appearance (dimmed, falling away) on every segment; no FHP-window coupling; no
    floor furniture (the trail-off no-furniture rule).
    Sight knobs, not gates: the head glyph's exact form, and — if sighted rounds show confusion
    with real tapped slides — swapping the tail shimmer for a serrated scrape texture is a
    tuning move within this answer, not a redesign.
  - **2D tab**: a head showing the **start neck position** (the tab's horizontal axis is time,
    so the digit is its only channel for where the scrape starts) **carrying the white X
    full-mute marking** (user 2026-08-03: the digit is a neck position, not a pitch — the X says
    unpitched despite the number; the digit+X pairing already exists in the tab's dead-chord
    figures, and tap+mute is one more unplayable combination reinforcing the read), with the tap
    attack triangle, tremolo gem tail, and slide diagonals following the waypoints in the
    unpitched style — each leg's target carried by the unpitched-slide position chip; **no text
    label** ("P.S." rejected as failing the gesture's aggression). Net effect: a superset of the
    print pick-scrape convention (x + slanted line) inside the user's original composition, made
    true by the path. The measure-3 muted hand-slides stay distinguishable: both show X heads,
    only the pick slide carries the tap triangle and tremolo gems.
- **55-Q2 — editor authoring surface.** Reshaped by the stream design: the verbs are
  gesture-object operations — place/remove, adjust span, drag path waypoints exactly like note
  slide waypoints (plan 40's curve-editing patterns), plus convert-selected-dead-note(s)-to-
  gesture as one undo entry. Recommendation: fold into plan 40 Phase 5's technique-editing
  surface; confirm with the user before wiring keybinds.

## Phases

1. **Chart stream + format.** `ChartPickSlide` + `Chart::pick_slides` (reusing `SlideWaypoint`),
   JSON serialization in place (fifth top-level array), validation: positions strictly ascending
   (one right hand — no simultaneous gestures), span strictly positive, at least one waypoint,
   waypoint offsets strictly ascending within the span, every consecutive pair of neck positions
   (start fret included) strictly differing — no stationary segments, the travel is the gesture —
   anchor string and path frets in range, no gesture span overlapping the next gesture's onset.
   Unit tests in the core suite; package-format checklist walked.
   **Exit criteria.** Stream round-trips through save/load; validation rejects each listed
   invalid shape. **Verification.** Build via `.agents/rockhero-build.ps1`; common-core suite.
2. **Import.** Chain resolver diverts flag-64/128 carriers into `ChartPickSlide` entries before
   the slide-out branch and removes the carrier dead notes from the note stream (they are GP's
   encoding vehicle, not content). Default path synthesized from the flag direction across the
   notated span: **down 17 → 3, up 3 → 17**, as named constants tuned at sight. The values are
   corpus-derived (FHP can't inform a right-hand start, so charter consensus is the authority):
   in the reference corpus's suspect population, ~70% of down-slides start at fret 13+ (17+ is
   the largest bucket) and ~80% end at fret 7 or below with 10–14 frets of travel; up-slides
   mirror it. Two simultaneous same-direction flags merge
   into one gesture (anchor = lowest notated string, span = longest); conflicting directions
   keep the first with an import diagnostic. The measure-3 figure pinned unchanged by a
   regression test; FHP transparency pinned by test. Normalization policy doc gains the rule.
   **Exit criteria.** Flags 64/128 import losslessly; merge, m3-regression, and FHP-transparency
   tests green. **Verification.** Build; gp-import suite
   (`.agents/rockhero-build.ps1 -Targets all -RunTouchedTests`).
3. **Projection + 2D tab.** `HighwayPickSlideView` / `TabPickSlideView` (start/end seconds,
   lane, path in view units); the dedicated tab draw per 55-Q1; shared paint core + pixel tests.
   **Exit criteria.** Projection carries the stream; tab renders the 55-Q1 marker including a
   down-then-up chain. **Verification.** Build; common-core, common-ui, and editor-ui suites.
4. **Highway treatment.** Implement the 55-Q1 rendering (numberless tap-marked head, path-
   following shimmer tail); sight-iterate the head form, default-path constants, and tail
   texture on the Van Halen measure 20 (expect retuning rounds); any new colors go through
   plan 54 coordination.
   **Exit criteria.** User signs off the treatment on sight. **Verification.** Build; sight pass
   over the Van Halen measure 20; no new file-scope color constants (plan 54 coordination).
5. **Editor authoring + acceptance.** 55-Q2's gesture verbs; final acceptance bundle (build,
   full suites, re-import sight pass on the Van Halen measures 3 and 20 — 3 unchanged, 20
   rendering the new treatment).
   **Exit criteria.** The Acceptance section below holds in full. **Verification.** The
   sanctioned end-of-plan bundle (build, full suites, pre-commit), then the two-measure sight
   pass.

## Acceptance

- Measure 20 of the Van Halen import renders the pick-slide treatment; measure 3 is
  byte-identical to its pre-plan import.
- A chart round-trips the stream through save/load, including a down-then-up waypoint chain;
  the editor can author, reshape, and remove a gesture.
- No FHP output differs on any chart without pick slides; on charts with them, FHP output is
  identical to the same chart with the `pick_slides` stream stripped (the transparency
  invariant, as a test — structural by design, pinned anyway).
