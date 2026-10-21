# Plan 60a — Fret-hand positions: where the hand is

## Status

**Sub-plan of `docs/plans/roadmap/60-the-hand.md`, split out 2026-10-04.** **DONE 2026-10-09:**
Phase 3's sighting is signed (the position row's grayed derived chips walk, authored chips draw
solid, Prune drops what states nothing). Phases 0, 1 and 2 are DONE (Phase 1 closed 2026-10-04 at 88.213% exact, identical to its reference). The master's
rename sweep is DONE (`c910b8d8`). **Phase 2 is DONE 2026-10-08, its sighting included (sighted
2026-10-07):** `16878e2f` the derivation takes authored positions; `3b8966db` positions derive at
read; `b8eb6f82` and `959fcb9c` the slide-out ride; `3d8bb3a1` the row over every position, with
Prune; `c074b721` and `3a74bb34` the coverage rule, with drop-on-load; `1ef9a5f8` holds as
demands; `355fd52f` the slide rule; `b4a66c15` and `0f4e71e6` the node fret and the capo floor;
`9edb7138` and `fff2dd1f` the two-layer derivation; `b95ce8f1` the hand demands; and the close-out
`9a4375f9`, `a98149cb`, `cc479791` and `51e10165`. Scorer 88.175% exact. **Phase 3's tests are
DONE (`6d2cdc01`); only the user's sighting of the position row remains.** Positions are finished
before the holds plan (`60b-holds.md`) starts.

Section numbers are the family's (the master's §0 has the map): §§1–6, the G60 gate record of §9
and §§10–12 are in the master; the phases, §8 and the rulings below keep the numbers they had.
Text written before the split says "the hand marker" and `Ctrl+H` for what is now the POSITION and
`Ctrl+P`; the design section below and each phase's REVISED note are the current reading.

## The position — the design (RULED 2026-10-04; from the master's §1a)

Saved as `positions[]`, today `fhps`.

- **The record is today's, unchanged:** `{at, fret, endFret?}`. `fret` stays REQUIRED, so the bare
  marker, the optional fret and "an end fret without a fret" do not exist.
- **Positions derive at read** (60-Q4 stands); the list stores only what the charter authored, and
  a fresh import stores none.
- **An authored position stands until the chart contradicts it.** Its window is `[fret, endFret]`
  where the end is stated, else the stated fret up to the top of the run it heads and of every
  stop gripped at its instant (`forEachGrippedStop`, the coverage rule's own query), never under
  four frets (Phase 2 build rulings, 2026-10-05). The run is cut at the first later instant that
  reaches below the stated fret: the position never holds that instant, so its notes widen nothing
  (sighted 2026-10-08: a fret-5 position before a 4-to-9 chord drew 5–9). It stands through every later instant that fits
  inside that window and emits nothing; the derivation resumes at the first demand outside it.
  This answers the "scope of a stated fret" question Phase 2 carried: neither "the run it heads"
  nor "until the next marker".
- **Whether a note at an authored position's own instant, outside its window, is REFUSED** was
  ruled yes on 2026-10-04 and re-ruled as ONE chart rule on 2026-10-05; Phase 2's build rulings
  carry it; it is built (`3a74bb34`). The derivation needs no refusal: a position owns its own
  instant. A note later on is never refused; the position stops standing there.
- **Chords:** `Ctrl+P` inserts or restates, `Ctrl+Shift+P` jumps to the row. Moved in the rename
  sweep.

## 7. Phases — 0 to 3

### Phase 0 — stored-stream hygiene — DONE

The validator refuses equal positions ("must be strictly ascending", `chart_rules.cpp:213`);
`chart.h` / `file-formats.md` say "strictly ascending and unique by position"; the
duplicate-position case is in `test_chart.cpp`. The reader-refusal clause was deliberately NOT
built (the stopgap's Phase 0 deviation: it was the order rule stated twice). 60-D6's full form,
the funnel refusing a marker at an occupied front, is built too (`handChordTarget` restates
instead of inserting; a move onto a held start is refused at the commit).

### Phase 1 — the derivation core (gated on 60-Q1; needs nothing else) — CLOSED 2026-10-04

**Status (2026-10-04): CLOSED.** The derivation, the one clamp, the end-of-import call and the old
walk's deletion are built, with a unit test per rule and the importer and census re-pinned (census
FHP rows re-signed against a same-day baseline of the walk). Acceptance (b)'s scorer is built
(`test_fret_hand_score.cpp`, `[.fhp-score]`, reading `ROCKHERO_FHP_SCORED_CORPUS_DIR`) and the
derivation publishes a `FretHandDerivationTally`, which also feeds Q3's rule-6 census rows
(reported, unsigned). The port and the reference (`reference.py`, harness `e45c1b4`) agree on
every definition: both demand a natural harmonic at its node's fret at its onset (`heldFretAt`
and its mirror `held_fret_at`) and let its ring pin nothing, the form the user RULED 2026-10-04
(§9 Q3). Acceptance (a) and (b) passed; (c)'s census is re-signed and (d)'s ceiling is taken, with
the offender worklist and the slide split deferred to Phase 6's entry. The records follow the
acceptance text below.

Phase 1 is EXACTLY the measured zero-input 13-rule algorithm (§9 Q9): no marker input, no format
change, no row change. Every rule's open point is settled by the 2026-10-04 rulings (§9, Q1–Q12);
the builder reads them as part of this scope.

**What it is.** One pure function in `rock-hero-common/core`, beside the width fold
(`chart_fret_hand.h`), which it does NOT replace in this phase (Q10).
~~`deriveFretHandPositions(notes, plants, markers, tempo_map)` → the derived hand events, each a
`GridPosition` front with a `FretWindow`~~ — SUPERSEDED 2026-10-04 (§9 Q7, Q8, Q9 in their
amended, user-signed final forms): the ONLY input is the `ChartConnections` the resolutions already
build (the saved notes, their whole-note places and predecessors), with the `TempoMap` beside it
(CORRECTED 2026-10-04: the connections do not carry the map, and a reference to it inside that
value type would dangle, so it is an explicit parameter, read only to place an instant in its
measure); of the tuning only the capo is read (rule 9's clamp needs it: frets are stored absolute, source
fret + capo, lifecycle rule 21). It returns the derived hand events, each a whole-note `Fraction`
front (the axis `ChartShape::place` already uses, which the events sit beside in Phase 2) with a
`FretWindow` (the type exists: `chart.h`). There is no `plants` parameter: plants derive inside
from the same connections (`chartPlantedStops` — rule 12). It never reads seconds: rule 4 (the
rest break) is off, and the bar test is "the measure number changed between consecutive groups"
(`position.measure`; `advanceGridPositionByWholeNotes(...).measure` for a keyframe demand). Rule
9's clamp is the ONE `fittedWindowFret(fret, width, capo)` beside `firstPlayableFret`, shared with
`normalizeFretHandPosition` (Q7). Places are precomputed in the connections, so the derivation
re-addresses nothing. **No markers input in this
phase** (Q9): no production caller has markers to pass until Phase 2 (the import authors none),
and their semantics are ruled at Phase 2. **No sections input**: a section start is a downbeat by
validator, so the bar-line trigger subsumes it; this also answers the former Q1 agenda question
about unlabelled sections and retires Q2's `section_breaks` parameter (§9 items 1 and 2).

**Why `common/core` and not the importer.** The game projects charts through the same
`makeChartViewState`; once the stream derives at read (Phase 2) both products must derive it in
one place, and the importer is just the first caller. In this phase the importer still calls it to
AUTHOR the stored stream — every derived event stored as a placement with its fret — so Phase 1
lands and is measured with no format change. **The call site moves to the END of the import**
(Q11), on the normalized chart — after the let-ring figure law, the same-string clamp, lattice
rounding and `normalizeChart` — so the stream Phase 1 stores equals what Phase 2 derives at read
(rule 6 reads rings, which those passes change). ~~The importer's call site
(`gp_chart_builder.cpp:3821`)~~ (today `:3675`, on the natural stream) goes. The importer's
other FHP passes, per Q11: `resolveSlideIns` asks a NATURAL-stream derivation for its approach
delta (it chooses the scoop's start fret before the rings are final, so it cannot wait for the
end-of-import call); the exit-ride and dip fabrication (`resolveSlideOutExits`, the dips of
`resolveSlideIns`) STAY in Phase 1, flagged in the code as pending Phase 2's ruling; the lattice
rounding and `dropRestatedPlacements` keep acting on whatever is stored.

**What it deletes.** The walk's body: the minimal-shift drag core, the boundary/rest re-anchor
machinery and `g_fhp_phrase_rest_seconds`, the `phrase_boundary_places` parameter and its
collection (`:3897-3918`), the pinned-finger union (rule 6's suppression subsumes it), the
held-hull slide reshape and `heldHullAtSlideKeyframe` (slide keyframes become ordinary coverage
events at their destinations), `anchorsFretHand` (or reuse it as rule 1's predicate), and the
first-entry retime (`:2467-2476`), which rule 10 retires with no special case: the window before
the first front is the reader's lookup question (§9 Q6, final form).
~~`deriveHeldFretRanges` / `deriveFretHandWidths` fold into the
derivation's rule 8 (the window IS the run hull from the pinned or derived floor), and
`defaultedPlacement`'s stop fold goes with them in Phase 2~~ — SUPERSEDED 2026-10-04 (Q10): with no
format change the fold still has readers — the projection (`chart_projection.cpp:498`),
`fretWindowsOf`, the census (`:2316`), `defaultedPlacement` and `test_gp_song_importer.cpp:309` —
so the import stores frets only and the projection's width stays the stopgap's through Phase 1.
The derivation computes rule 8's width internally, and the scorer reads THAT width, never the
fold; in Phase 1 the width is therefore derived twice and the two disagree — an acknowledged
yield (§9 Q10). The fold goes in Phase 2 with `defaultedPlacement`'s stop fold (law 2 allows
the wait). Net code
deletion. The-project-lifecycle rules 5–9 (`docs/developer/the-project-lifecycle.md:297-374`)
are rewritten to the three-sentence charter law of the design record — except rule 9's exit ride
and rule 16's dips, which describe the passes that stay flagged until Phase 2.

**Acceptance.** (a) DONE 2026-10-03: rules 12 and 13 re-measured in the Python harness
(`rules12_13.py`) before the port — rule 12 +0.06 exact and every plant covered, rule 13 without
a corpus population (§9 item 1). The port must implement rule 12 in the measured form: the plant
is a demand from the source's onset, including a tapped source (§9 Q2 states the final form: a
one-note demand at the source's onset widening every group up to the destination's onset).
(b) The C++ port scored against the same dataset through a hidden-tag test
(`[.fhp-score]` in `test_fret_hand_score.cpp`, the census rig's pattern) reading the scored corpus
— the converter's `.rock` packages — from `ROCKHERO_FHP_SCORED_CORPUS_DIR`, the variable the
Python reference reads too (`ROCKHERO_FHP_DATASET_DIR` stays the raw dataset only the Python
harness reads), reporting coverage and exact agreement — §9 item
3; without it nothing proves the port is the measured algorithm. The scorer reads the songs
through the custom-song converter and RockHero's production chart reader, with NO hand-written
C++ adapter, and depends on the converter's pitched-slide fix (§9, decisions of 2026-10-04).
**Pass bar (Q12):** exact agreement within ±0.05 pt of the Python reference RE-RUN on bar-line
breaks only and with every other input choice the port makes (§9 Q1), with the intermediate counts
printed — fretted notes scored, groups, plants (against the corpus's 34,405), emissions — so any
gap can be attributed. The scorer reads the derivation's internal rule-8 width (Q10) and skips
notes before the first anchor exactly as the harness's `score` does; before the port is scored,
the Python reference measures rule 10's cross-bar share (Q6).
(c) Forced-relink test run; the census re-run with the three drifted
fret-hand-window rows and the pinned-window count re-pinned (expect the pinned count to collapse
toward the corpus's ~2-per-400k residue), plus the named row Q3 adds (rule 6's rings under GP's
dense durations); the offender worklist re-measured; user sightings of the worst prior offenders.
(d) The seam-vs-shift agreement measure PRODUCED here for Phase 6, as Q12 defines it: from the
census counters `spans_crossed_by_fhp_shift` and `fhp_shifts_inside_spans`, over the emitted
shifts not carried by a slide. Phase 6's threshold on it stays the user's.
**Must land before Phase 6** — building the coupling against the old walk would clip wrongly
everywhere.

**Acceptance records (2026-10-04).**

- **(b) PASSED.** The reference is the harness's `reference.py` (`cd3fe0e`, amended `e45c1b4`):
  the 13 rules on bar-line breaks only, reading the same converted packages with the port's input
  choices. Port and reference both score **88.213% exact** (3,895,334 of 4,415,805 fretted notes;
  within one fret 95.04%, coverage 99.929%, 320,736 emissions; Lead 89.402%, Rhythm 91.156%, Bass
  76.983%) and agree on every intermediate count: 2,529,636 groups, 83,039 keyframe demands,
  43,292 plants of which 31,088 on a pressed fret, 1,314,890 runs. The port was re-run on a forced
  relink the same day. The corpus was rebuilt after the converter's pitched-slide fix (`788ebc5`,
  refined by `323c618`). It scores 4,100 of the dataset's 4,555 arrangements, in 1,301 packages,
  because the converter fails four dataset archives whole (455 arrangements, 448 of them in one
  compatibility pack). The plant yardstick of 34,405 above belongs to the raw dataset and does not
  compare; the scored corpus's figure is the 31,088 both sides print. Rule 10's cross-bar share
  (Q6): the bar stop cuts 22,611 reach-backs, 12.39% of the 182,543 emissions after a bar line and
  7.05% of all emissions. On the raw dataset the measured champion scores 88.06% on bars alone
  against 88.09% with section breaks.
- **(c) Census re-signed; the worklist DEFERRED to Phase 6's entry.** On a forced relink every
  fret-hand row equals its pin: 16,014 windows placed, 2,224 arriving where nothing fretted sounds,
  64 windows over 98 pinned rings, 402 spans crossed by 447 interior shifts. The pinned count did
  not collapse toward the dataset's residue as predicted: 64 of 16,014 is 0.4%, against the walk's
  88 that day. Rule 6's named row reports 478 figures held of 63,426 cut (0.75%, against 0.14% on
  the dataset's sparse sustains) over 185,696 pinning rings, and stays reported until the user
  signs a figure. The one position-level sighting on record (scratch registry #1, a window leaving
  fret 10 mid-figure at 5:4) is fixed: the probe shows one window from 5:1 standing through the
  figure. Deferred: the per-file offender worklist's re-measure and the user's sightings of its
  worst entries. The list holds per-file counts of the 2026-09-05 walk and no positions, the tool
  that produced its columns was never committed, and Phase 2's ruling on the fabricated exit
  rides and dips changes the stored stream it would measure.
- **(d) The ceiling is taken; the slide split DEFERRED to Phase 6's entry.** With slide carriage
  not subtracted, 447 stored windows arrive strictly inside 402 spans: 2.8% of the 16,014 windows
  and 1.8% of the 22,316 spans. Q12's denominator, the emitted shifts not carried by a slide, has
  no counter, and "carried by a slide" has no authority yet. Phase 6's coupling needs that
  predicate in the product, so the census reads it there instead of stating it first in a test;
  and the split belongs on the derived events, which Phase 2 makes the stream, where the gate
  reads today a stored stream that carries the importer's fabricated placements.

### Phase 2 — the derived stream (gated on 60-Q4 and §9 item 4; needs Phase 1)

**REVISED 2026-10-04 (§1a) — this phase is POSITIONS ONLY, and smaller.** Read the text below
through these changes:

- **Ahead of the phase, its own commit:** the timeline key `position` becomes `at` (§1a, "Names and
  the format").
- **The record does not change shape.** `fret` stays required: strike "The marker's `fret` becomes
  OPTIONAL" and every consequence of it — the bare `Ctrl+H` marker, `{position, fret?, endFret?}`,
  and three of the four marker-semantics questions (where a bare marker over silence fronts,
  whether `endFret` without `fret` is legal, whether a marker that changes nothing is a row event:
  an authored position is always a row object). The fourth, the scope of a stated fret, is RULED:
  it stands until the chart contradicts it.
- **The key is `positions`,** not `hand`; the chords are `Ctrl+P` / `Ctrl+Shift+P`; the
  `MarkerRow::Hand` rename's touch list below is the `positions` rename's.
- **The resolutions pass takes the authored positions** as its input beside the notes; no hold
  reaches it in this phase, and nothing about holds is built here.
- ~~**Validation gains one rule:** a note at an authored position's own instant, outside its
  window, is refused.~~ SUPERSEDED by the build rulings' coverage rule (gripped, not struck).
- Unchanged: the events inside `ChartResolutions`, the pass order, the importer storing nothing,
  everything this phase deletes, the exit-ride and dip ruling, the converter note, the sighting.

**Phase 2 build rulings — 2026-10-05** (a readiness audit against `999c0386`, then the user's
answers). These are the current reading wherever the older text below disagrees.

- **Built in steps** (the list at the end of these rulings): step 1, the derivation accepting
  authored positions, is DONE 2026-10-05 with no visible change.
- **The authored window, exactly** (refines §1a's wording): `[fret, end fret]` where the end is
  stated; else the stated fret is the floor and the top is the top of the run the position heads,
  never under four frets (the run cut where it first reaches below the stated fret, refined
  2026-10-08 — see the design section). "The fret plus the run's width" would leave a run's top uncovered
  wherever the stated fret sits below the run's floor; this is the reach the width fold gave.
- **An authored position is a forced boundary:** it is always an event, no run crosses it, the run
  it heads starts at the first demand at or after it, and a derived front never reaches back to
  it. It OWNS ITS OWN INSTANT: whatever sounds there cannot end it, so the derivation is total
  with no refusal. Built as step 1 (`deriveFretHandEvents`' `authored` parameter).
- **The refusal — RULED 2026-10-05 (user), and it is ONE rule.** A note occupies the time from its
  onset up to, but not including, its ring's end, so a ring that reaches exactly to the next note
  or to a position's start is not there: there is no "carried-in" class to tolerate and no case
  analysis. The rule: AN AUTHORED POSITION COVERS EVERY FRETTED NOTE GRIPPED AT ITS INSTANT —
  struck there, held by a ring sounding through it, or a stop of a grip whose span stands there,
  sounding or not (user, later the same day: "GRIPPED at its instant, not just sounding"; a
  natural harmonic's ring holds nothing) — where covering is read against what the position
  states — its fret, and its end fret where it has one. It is a chart rule like any other: one predicate, and an edit that would break it is
  refused, whichever side the edit is on. (An earlier draft of this entry enforced it at the edit
  only and judged each edit on what it touched; the user rejected that as machinery for a simple
  problem.) A loaded file is the one exception: loading DROPS a breaking position and reports it
  as a load notice (user, 2026-10-07), because a converter cannot apply the rule — it would need
  the whole hold derivation — and the notes alone already derive a window there. Measured before
  the switch: 3,336 of 389,463 positions in 324 of 1,301 converted packages broke it; the user's
  four projects, none. About 2,720 of those sit mid-slide, moved early because the source game
  had no window motion to carry the hand with the slide. The slide rule stands (user, 2026-10-07):
  the window here moves with the slide itself, so those positions are dropped. Built as step 5.
- **The dips are DROPPED** (user). They cannot be rebuilt from stored data — a scoop is an
  ordinary slide once imported — and rules 1 and 2 already demand the approach fret and the
  arrival.
- **The exit ride is KEPT — RULED 2026-10-05 (user):** the hand travels with an unpitched
  slide-out, so the window must. It is rebuilt as MOTION DERIVED FROM THE SLIDE, never as a
  position: at the slide-out's end the window arrives covering the exit fret, riding the slide's
  own segment with the unpitched ease; at the next demand instant it returns to whatever the
  position stream states there. A hand genuinely moving on in the slide's direction is simply the
  next position, so the drawing has no departure case; the importer keeps its own departure test,
  which only chooses the stored exit fret. A slide-out with no room before the next demand stays
  planted. The motion lives in its own list beside the chips, read by the highway's hand track
  alone, so it draws no chip, takes no click and is no row object. Step 2 deletes the stored form;
  this is step 3, and Phase 2 is not sighted without it.
- **The steps, renumbered:** (1) the derivation accepts authored positions, DONE; (2) positions
  derive at read, DONE; (3) the slide-out motion, DONE; (4) the row walks the derived positions,
  with the prune command, DONE; (5) the coverage rule, DONE.
- **Step 4 as built (2026-10-05):** the row's markers ARE the hand events' chips. A derived chip
  draws as a ghost (user), an authored one solid. A verb that STATES something on a derived
  position (a fret or an end fret) first authors the standing fret there, as `Ctrl+P` does, in one
  undo step; Delete and `Alt`+arrows on a derived position are refused with the red glow; clearing
  an end it never stated does nothing. "Prune Hand Positions" (no default chord, Edit menu) removes
  every stored position whose removal leaves the derived events unchanged.
- **HOLDS ARE DEMANDS — RULED 2026-10-05 (user), found on a fresh import of a tapped arpeggio:**
  "Frethand positions MUST cover rings too… they are provably held", and "held grips must also
  always be contained within the FHP window". So at read the order is notes → spans → positions
  (REVERSING the order the text below gives, and the 2026-09-04 planned coupling that would cut a
  span at a hand shift: the hand never leaves a held grip instead). One function,
  `fretHandHolds`, states which fingers are provably down and from when to when — each held ring,
  and every pressed stop of every span's grip for the span's whole extent — and the demand, rule
  6's pins and the coverage rule all read it. A hold demands its stop at every covered instant
  inside it, half-open. Where a stored ring and the next grip cannot share a hand, THE RING IS
  WRONG and is fixed where it is stored: the importer's let-ring figure law now ends a figure at
  a tap that plants off its grip. Measured: scorer 88.213% → 88.190% (3,894,283 of 4,415,805);
  spans holding a stop outside the window 88 → 0. The import pipeline's own arrow (written
  durations → positions → ring extension) is unchanged.
- **Converted songs KEEP the source's positions as authored** (user). They are what a charter
  placed, they behave the same under "stands until contradicted" (the next one ends each), and the
  scorer needs them as its ground truth. This reverses the converter note below: the converter
  does not change. The scorer derives with an empty `authored` list, so the truth is never an
  input to what it scores.
- **Existing projects are PRUNED, not stripped** (user): remove every stored position whose removal
  leaves the derived events unchanged, and keep the rest as authored. That is the former Phase 3
  item 6, built in this phase as an editor command.
- **The row walks the derived positions in this phase** (user): the former Phase 3 item 1. Three
  sites assume the stored list and the drawn chips line up
  (`chart_handlers.cpp` selection outline, `editor_controller.cpp` pending box,
  `fret_hand_handlers.cpp` chip click), which stops being true the moment positions derive. A
  chip maps to an authored position by its front. On a selected DERIVED position, typing a fret
  authors a position there and Delete is refused.
- **`Ctrl+P` states the fret already standing at the cursor,** the nut window where nothing
  stands. `defaultedPlacement`'s stop fold is gone; the function stays as that one statement,
  which every stating verb on a derived position also goes through (step 4).
- **As built:** the notes layer `chartResolutions(notes, capo, tempo_map)` carries
  `FretHandDemands`; authored positions enter only the hand layer,
  `fretHandResolutions(notes_layer, positions, tempo_map)`. `FretHandEvent` sits beside
  `FretWindow` in `chart.h`.
- **Every census fret-hand row is re-signed** on the derived events, which gain width-only changes
  and lose the fabricated placements.

SUPERSEDED by the REVISED note and the build rulings; kept as record.

The marker's `fret` becomes OPTIONAL; the record is then `{position, fret?, endFret?}` — a forced
statement boundary carrying an optional direct window statement — under the key 60-H2 settles
(§9 item 6; one format change, not two). The derived events live INSIDE `ChartResolutions`
(`chart_legato.h:402`) as one more vector beside `shapes` and `held_stops`, derived by the one
resolutions pass, which gains the markers as an input beside the notes (`chartResolutions(notes,
markers, tempo_map)` — ~~about fifteen callers, most of them tests~~ 28 call sites, verified
2026-10-04: 26 in tests and 2 in production, `chart_projection.cpp:180` and
`fret_hand_handlers.cpp:73`). It also needs the TUNING (the capo, Phase 1's rule 9), so the pass
takes the `Chart` or gains an overload — the Phase 2 builder's call. Inside the pass the
derivation runs AFTER `chartConnections` and BEFORE `deriveChartShapes`: law 2 and Phase 6 need
that order, and it is available, since `chartPlantedStops` reads only the connections. No
second cache: the keyboard plan's `ChartResolutions` name already IS this bundle, and the row,
the lane and the highway all read it through the projection. The importer stops storing a
stream: it authors nothing (the
zero-authored-input invariant is the import), so `fhps` on a fresh import is empty and every
position is derived. `FhpViewState` keeps its shape; `end_authored` generalizes to "stated"
(fret, end, or both) so the chip prints a statement apart from a derived window.

What this deletes: the importer's placement authoring (the end-of-import call Phase 1 placed,
`dropRestatedPlacements`, and the lattice rounding's placement half); ~~and its first-entry
retime~~ (SUPERSEDED 2026-10-04: rule 10 retires the retime in Phase 1, §9 Q6);
`defaultedPlacement` and the stop fold behind it; the width fold, `deriveFretHandWidths` /
`deriveHeldFretRanges`, which Phase 1 kept (§9 Q10) — its readers (the projection,
`fretWindowsOf`, the census, `test_gp_song_importer.cpp:309`) move to the derived events. What it
adds: the optional fret in the record, reader and writer; the resolutions input. Nothing
user-facing is lost — the stopgap's verbs keep working unchanged, their source changes: `Ctrl+H`
inserts a bare marker (a boundary; the derivation supplies the floor), the digit pins the floor,
`Alt`+digit the top, `Alt+Delete` clears the top, `Delete` the marker. The sighting reels stop
authoring `fhps`.

**The marker semantics this phase rules first** (deferred from Phase 1 by §9 Q9; the open list
is at the end of §9): whether a marker that changes nothing is still a row event (rule 11 drops
an event whose window does not change, against §2 and law 7); the scope of a stated fret — the
run it heads, as the design record says, or until the next marker, as the stopgap behaves today;
where a bare marker over silence fronts; whether `endFret` without `fret` is legal. **Visible
change to expect:** today a placement governs its whole stretch until the next one, and
still-sounding rings from before it widen the window; under the 13 rules a stated fret pins only
the run it heads (if so ruled), and rule 8 never widens for held notes.

**The exit ride and the dips** (lifecycle rules 9 and 16; Phase 1 keeps them, flagged, §9 Q11)
cannot be stored once the importer authors nothing, and no rule of the 13 produces them. This
phase RULES their home: drop them, or move them into the projection as `FhpViewState` ramps — the
same tier as the camera lead. Whichever way, `resolveSlideOutExits`' placement fabrication and
the dip/restore half of `resolveSlideIns` leave the importer here.

**The `hand` rename's touch list** (§9 item 6) includes, beyond the reader, writer and
`chart.h`: the timeline re-addressing (`timeline_readdress.cpp:217`), the
`FretHandPositionsSnapshot`, and the census.

**Sighting:** rule 3 reads bar lines, so from this phase a time-signature edit MOVES THE DERIVED
POSITIONS while the notes keep their places. That is correct; it is in the sighting brief (§8).

**Converter note** — ~~`custom-song-importer/tools/custom-song-converter.py` (outside this repo)
still writes the dropped `width` key on every placement (`:1202-1210`) and must stop~~ STALE,
SUPERSEDED 2026-10-04: since `a26cc16` the converter no longer writes `width`; its `build_fhps`
(`:1614-1653`) writes `endFret` wherever the source width is not 4. After this phase that makes
every wide source anchor an AUTHORED statement (§2), which it is not. The converter must stop
writing `fhps`, or write only deliberate statements.

**Cross-plan (plan 64).** Once the derivation lives in the resolutions bundle it is part of every
per-revision re-derivation. Plan 64's segment-per-silence idea (its F2, "segments or checkpoints")
assumes a derivation that keeps no memory across a cut, and the 13-rule walk CARRIES the standing
window across silence (rules 5 and 6), so the FHP pass is not segmentable as-is. A cross-plan
note, not a blocker: plan 64's §10 records it against its Phase 0-f.

Not here: the free-note finger (no writer exists — the GP parser reads none — so law 8 forbids
the field until Phase 5's import wiring) and the open-stretch window statement as a separate
record (subsumed: a marker with a stated fret over silence IS that statement, §9 item 4).

### Phase 3 — the position row's sighting and owed tests

**Status (2026-10-08):** items 1, 2 and 6 were built in Phase 2 (`3d8bb3a1`): the row's objects
are the hand events, an authored chip draws solid beside the derived ghost, and Prune is item 6,
with their tests in `test_editor_controller_marker_rows.cpp` and `test_tab_paint_core.cpp`. Item 3
(rail selection) lives in `60b-holds.md`. The tests §8 owed the position row are DONE
(`6d2cdc01`): the row walking the derived windows of a chart storing no position, a stored
position on a derived front as one row object, and the strictly-ascending-fronts invariant in
`test_chart_fret_hand.cpp` with its census ZERO row (0 over the GP corpus); the rail and span
tests moved to `60b-holds.md` with item 3. The user's sighting (§8's brief) was signed
2026-10-09, so the phase is DONE.

**REVISED 2026-10-04 (§1a).** The row is the POSITION row, and its items read so: item 1 (the
derived events as the row's objects) and item 2 (the authored tell) stand, for positions; item 3
(rail selection) stands and becomes how a span or a hold is selected; item 4's END-gesture
question is CLOSED — edges move, and the move authors a hold (§1a), so neither the struck
content-editing front move nor the tail-rewriting end gesture is built; item 6 ("drop markers that
state nothing") stands for positions. The hold — its record, validation rule, `Ctrl+H` pair, edge
verbs and Delete — is built in what was Phase 4.

Built by the stopgap: the record, the chords, the funnel, the row, the selection kind, the focus
column, walk / reach / Tab / jump, chip and pinned-chip selection, the outline, Delete, the
grid-quantum move, fret entry, the end fret, the reveal of an acted-on selection. What remains:

1. **The row's objects become the derived hand events** (60-H3): `markerStarts(MarkerRow::Hand)`
   and `markerSelectionAt` read `FretHandResolutions`' events instead of the stored stream, so a
   fresh import with no markers still has a row. Identity stays the front position
   (`FretHandPositionSelection {position}` survives; its name is re-asked under H2). Pinned events
   are the ones a marker stands at. **How the events reach the controller (verified 2026-10-04):**
   `markerStarts` returns `GridPosition`s while the events' fronts are whole-note `Fraction`
   places. **Decided: the controller keeps the resolutions**, as a two-layer memo beside the
   scene: the notes layer (`chartResolutions`, keyed by the arrangement and
   `Session::chartNotesRevision()`, seeded by a judged note edit with its gate's own derivation)
   and the hand layer (`fretHandResolutions`, keyed by `Session::chartRevision()` with the scene).
   The scene is built from the two
   (`makeChartViewState(chart, resolutions, hand, tempo_map)`), and the row's verbs read the
   hand events from the memo (`fretHandResolutions().events`).
2. **The authored tell** (law 7): a mark on the chip (or the event) where a marker stands;
   absence is the derived tell. Strength is the sighting's call.
6. **"Drop markers that state nothing"** (§9 item 5): a verb over the current format that
   removes every marker whose removal leaves the derived hand events AND the spans unchanged —
   deriving with and without it is the test, so the verb lives beside the derivation, never in a
   script. Useful after any charting session; also the pruning half of the one-off conversion of a
   pre-Phase-2 package (the throwaway script outside the product renames the key and writes each
   old placement as a fully stated marker; this verb then keeps only the ones that carry
   information). One undo entry; refuses nothing.

## 8. The hand row — keyboard and pointer

Absorbed from the keyboard-focus-rows plan's steps 4b and 4c on 2026-09-15, rewritten for the one
object, and BUILT by the stopgap over the stored stream (everything below is live unless marked
Phase 3). It reuses the marker-row model wholesale: every marker row walks as
`MarkerFocusRow{row}`, one holder function, one landing, one selection mapping, one jump action.

**The row and its objects (60-H3).** One row between the time signature and the strings. Its
objects are the hand events (`FretHandResolutions::events`), derived and authored alike. Identity
is by front position.
Because every event carries a window and windows tile the song (the first owns the lead-in), the
row always has a holder once any event exists — the "gap" of the former span row applies only to
the SHAPE half, never to the row: the holder function — ~~`markerHolderAt(MarkerRow,
GridPosition)`~~ really `markerHolderIndex(starts, position)` (`editor_controller_impl.h:1527`,
verified 2026-10-04) — finds nothing only for an empty stream, as for tempo and meter. The shape
half keeps the half-open rule: an event's SHAPE holds a position only while position < its
musical close, so an abutting successor owns its
seam; the position half holds until the next event.

**The selection kind.** `FretHandPositionSelection {position}` beside the tempo and
time-signature kinds, keyed by front (name re-asked under 60-H2). Cursor-coupled; `Delete`,
`Alt+←/→`, the digit planes and `Alt+Delete` act on it; `Esc` releases; released when the front
names nothing after undo, arrangement switch and seek (60-D7).

**The focus column.** `focusColumn()` = the armed caret's position, else the paused cursor's; the
holder is computed at that column BEFORE the caret is demoted.

**Walk, reach, Tab, jump.** `↑` from the top string and `↓` from the time signature land on the
row's holder; `Ctrl+↑/↓` reach it as its own group; `Tab`/`Shift+Tab` step to the adjacent front;
`Ctrl+Shift+P` jumps and is silent with no events. Landing from the lead-in selects the first
event, whose only outlined mark may be its own scrolling chip off-screen to the right — the same
accepted trade as a late first section.

**Pointer selection — 60-D11 RULED 2026-09-15: in.** A click on a position chip SELECTS the
placement and seeks nothing, exactly as a ruler chip click does (`tab_view.cpp:406`), gated by
`marker_edits_enabled` like every marker surface; a click on a span's rails is Phase 3 item 3.

**The pin — 60-D12 RULED 2026-09-15: exactly as the ruler's selected pin.** Built: the pinned
chip is an ordinary selectable chip whose column is the pin's; the selected chip is outlined
wherever it stands (`selected_fret_hand_position`).

**Acting on an off-screen selection** reveals it (60-H6) — built on the shipped marker rows
(keyboard-focus-rows 4d) and inherited here.

**Verbs on the row.** `Ctrl+P` at the cursor authors or restates the marker; `Delete` removes
an authored marker, and is refused on a derived event, the chip flashing (a derived position is
not directly deletable, by law 7); `Alt+←/→` moves the
marker and is refused on a derived front the same way (§9 item 8); the tail keys move a SPAN's end, not a
position's (`60b-holds.md` item 4); digits
pin the floor, `Alt`+digits the top, `Alt+Delete` clears the top; `Enter` opens the template
picker on an event with a shape (Phase 5); `Ctrl+R` is inert until an event has a name to rename
(the entry's name is the dictionary's).

**Tests still owed** — none for the position row; every one landed (the stopgap's cover the built
verbs): ~~the row walking derived events on a fixture chart with no markers~~ (landed: "The
position row walks the derived windows of a chart storing none"); ~~a marker pinning an event the
derivation also fronts (no duplicate)~~ (landed: "A stored position on a derived front is one row
object"); ~~the authored-tell pixels~~ (landed: "Tab FHP chip draws a derived position as a
ghost"); ~~`Alt+←/→` inert on a derived front~~ (landed: "Delete and the move refuse a derived
position"); ~~"drop markers that state nothing" keeping exactly the markers whose removal changes
an event~~ (landed: "The prune removes exactly the redundant stored positions"); ~~a
strictly-ascending-fronts invariant over the derived events in `test_chart_shapes.cpp` and a
local census row for it~~ (landed in `test_chart_fret_hand.cpp`, where the hand events are
tested: "The hand events front strictly ascending", and the census row "... fronting at or before
the last (ZERO)", 0 over the GP corpus); ~~the end move's fit test at each refusal, if built~~
(struck: §1a closed the end gesture, and it is not built). The rail and span tests — pointer
selection from rails, `chartSpanRevealed`'s selected ground, the exported rail bounds matching
the drawn rails — are owed by `60b-holds.md` with rail selection.

**Sighting brief** (Phase 3, positions): a GP import with dense position changes and short-gap
chord charts, with NO markers; Tab across derived fronts; the authored tell beside a derived chip;
the marker move's refusal flash at a duplicate front; the selected chip's outline strength. From
Phase 2 on (added 2026-10-04): a time-signature edit MOVES THE DERIVED POSITIONS while the notes
keep their places — rule 3 reads bar lines, so this is correct, but it is sighted, never assumed.
The span items — clicking rails, the rail-highlight strength, a span ending near the cursor, a
landing successor abutting its predecessor — are sighted with rail selection in `60b-holds.md`.

## 9. The rulings that are this plan's

### Phase 1 build rulings — RULED 2026-10-04

The 2026-10-04 readiness audit (against `22ce8fae`; the plan re-verified against `9b2e069c`) found
twelve points of the 13 rules a builder could not settle from the record and the code. The user
ruled all twelve the same day: Q1–Q8 and Q10 signed as the audit recommended; Q9, Q11 and Q12
ruled as stated below. **AMENDED 2026-10-04 (user-signed the same day):** a fretting-hand review
and a simplicity review refined Q1–Q8 and Q10, and the user signed the amendments. The forms below
are FINAL and SUPERSEDE the audit's recommended wording, which survives only in the audit; where
an amendment reverses a point of the audit form, the reversed point is named. They are numbered
Q1–Q12 here, cited elsewhere in this plan as "§9 Qn"; they are not the 60-Q items above.

**Two further decisions (user, 2026-10-04) shape the scorer:**

- **The scorer reads the test songs through the custom-song converter and RockHero's production
  chart reader** — no hand-written C++ adapter. It measures the port on exactly the charts the
  product would read; every input choice below is the converter's and the reader's, and the Python
  reference re-run adopts the same choices. (Supersedes the audit form's scorer-adapter clauses in
  Q2 and Q8: the converter, not the scorer, rings pull-off sources, places notes on the tick
  lattice and floors a zero sustain.)
- **The converter's pitched-slide bug is being fixed now, and the scorer depends on the fix.** A
  pitched `slideTo` used to be written as an END keyframe, which RockHero reads as an unpitched
  slide-out unless it arrives on the next head; the converter now holds the destination INSIDE the
  ring.

1. **Q1 — the reference re-run.** The 88.09% run breaks at bars AND at sections and
   phrase-iterations (`rules12_13.py`: `bars + sects`); the port breaks at bar lines only (ruled;
   item 1a — a section may stand anywhere on the grid since plan 67, and was never an input).
   **FINAL:** the
   port's reference is `rules12_13.py` RE-RUN with `bars` alone AND with every other input choice
   the port makes — the option-B source demand (Q2), slid sources planted (Q2), legato sources rung
   as the converter rings them, and the slide-out exclusion (Q3, Q5). The ±0.05 check (Q12) then
   compares the same inputs. Item 2's ±0.02 suggests the figure sits near 88.07–88.11%.
2. **Q2 — a plant.** **FINAL:** a plant (`chartPlantedStops`, the source law 2 names) is a
   ONE-NOTE DEMAND at its source's onset that WIDENS EVERY GROUP up to its destination's onset.
   "Pressed frets only" is not a plant rule: it is rule 1, through the existing `pressedFret`
   (`chart.h:2070`), applied to every demand alike. Slid sources are planted. The scorer prints
   the plant population against the corpus's 34,405. (Audit form superseded: its "[source onset,
   destination onset], plus a source-onset demand where the source has no group" stated one rule
   as two.)
3. **Q3 — a plain ring (rule 6).** **FINAL:** a plain ring is a note with `fret > 0`, not
   `rightHandOnset`, not dead. It PINS from its LAST PITCHED STOP to the end of its pitched ring,
   where a slide-out's start ends it — so a slide-then-hold pins at its destination, while bends
   and vibrato keep pinning. Natural harmonics (fret 0) fail `fret > 0` on their own; pinch
   harmonics (pressed) DO pin, as they did in the 88.09% run. The length is the STORED `sustain`,
   never `ink_end` (presentation). GP durations are dense where the dataset's are sparse, so the
   re-measure the design record owes is a NAMED census row (Phase 1 acceptance (c)). (Audit form
   superseded: its "no `harmonic_node`" and "no keyframe fret statement" exclusions are gone.)
   **A natural harmonic — RULED 2026-10-04 (user):** the window must cover its node's fret when
   it is struck, because the fretting hand is exactly on that node (rule 1, a one-note demand at
   the onset, through `heldFretAt`); its ring then holds the hand nowhere, because the finger
   lifts and the string rings on until something contradicts it or the tail ends. Both halves are
   built and pinned (`test_chart_fret_hand.cpp`: "places a natural harmonic at its node's fret",
   "pins a pinch harmonic's ring but not a natural's"), and the reference reads the demand the
   same way since harness `e45c1b4`. Measured both ways on the scored corpus (5,279 natural
   harmonics): the node demand adds 998 emissions and moves exact agreement by −25 notes of
   4,415,805 (88.213% against 88.214%), so the ruling is physics, not a score.
   **Owed before Phase 6:** the stored sustain includes the let-ring extension, which conflicts
   with Phase 6's ring clipping; that must be ruled before Phase 6 (in the open gates below).
4. **Q4 — "unforced" (rules 5 and 6).** **FINAL:** a run is unforced exactly when its hull lies
   inside the standing window — two `FretWindow::covers` calls (`chart.h:2200`), on the hull's
   lowest and highest fret. "No wider than the standing window" follows from "inside" and is not a
   second test. Everything else is forced.
5. **Q5 — keyframe demands (rule 2).** **FINAL, with no special clauses:** every pitched keyframe
   fret statement is a one-note demand at its own place. Scrapes are excluded by rule 1
   (`rightHandOnset`), slide-outs by `slideOutKeyframe` (`chart.h:1211`), exactly as
   `statedStopFrom` (`chart_shapes.cpp`) excludes them. Arrivals merge into a group by INSTANT
   (`endsOnNextHead`, `chart_legato.h:118`, is exact equality). Groups count STRUCK fretted notes;
   demands only widen the hull, so a demand never makes a chord.
6. **Q6 — rule 10 is ONE rule.** **FINAL:** a run's front is the first onset after the LATEST of:
   the previous run's last onset, the end of its last plain ring (Q3), and the boundary that broke
   the run. (Read 2026-10-04, as the Python reference reads it: the ring term is the LATEST END
   among every plain ring struck before the run; reading only the previous run's rings scores
   identically.) The window BEFORE the first front is the reader's lookup question, which retires the
   importer's first-entry retime with no special case. The audit form's "tap-only" means "an onset
   with no demand", since rule 12 makes a tapped pull-off source a demand. Rule 10 is UNMEASURED:
   the Python reference must first measure its cross-bar share (the bar stop cuts open-string
   pickups). The scorer DOES count tapped notes, so the audit's "the scorer is unaffected" was
   wrong (superseded).
7. **Q7 — ONE clamp.** **FINAL:** one function, `fittedWindowFret(fret, width, capo)`, beside
   `firstPlayableFret` (`chart_rules.h:111`) — `max(firstPlayableFret(capo), min(fret, g_max_fret
   − width + 1))`, spelled with the named constants, never literals. The derivation passes the
   run's width; `normalizeFretHandPosition` and the old walk's copy (`gp_chart_builder.cpp:2445`,
   `:2563`) pass `g_min_fret_hand_width`. (Audit form superseded: its "two clamps with two jobs"
   was the rule stated twice.)
8. **Q8 — axis and inputs.** **FINAL:** fronts are whole-note `Fraction` places, as with
   `ChartShape`; the ONLY input is `ChartConnections`, with the `TempoMap` beside it (CORRECTED
   2026-10-04: the connections do not carry the map, and a reference inside that value type would
   dangle, so the map is an explicit parameter), and only the capo is read from the tuning; there is no `plants` parameter (plants derive inside). The
   derivation never reads seconds. The bar test is "the measure number changed between
   consecutive groups", read from `position.measure`, and from
   `advanceGridPositionByWholeNotes(...).measure` (`grid_arithmetic.h:216`) for a keyframe demand.
   (Built 2026-10-04 as ONE spelling for every instant: `tempo_map.gridPositionAtWholeNote(place)
   .measure`, read once per group.)
   The scorer skips notes before the first generated or authored anchor exactly as the harness's
   `score` does.
9. **Q9 — markers in Phase 1. RULED: markers wait for Phase 2.** Phase 1 is exactly the measured
   zero-input 13-rule algorithm: no marker input. No production caller has markers before Phase 2,
   and their semantics are Phase 2's to rule: whether an event a marker stands at survives rule
   11's dedup (a marker must stay selectable as an event, §2, law 7); whether a stated fret pins
   "the run it heads" (the design record) or "until the next marker" (the stopgap today); where a
   bare marker over silence fronts.
10. **Q10 — the width fold stays until Phase 2.** **FINAL:** `deriveFretHandWidths` and
    `deriveHeldFretRanges` stay until Phase 2 (their readers are listed in Phase 1's deletes); the
    import stores frets only. The derivation computes rule 8's width INTERNALLY, and the SCORER
    reads that internal width, never the fold. In Phase 1 the width is therefore derived twice —
    internally and by the projection's fold — and the two DISAGREE: an acknowledged yield, which
    Phase 2 removes by deleting the fold.
11. **Q11 — the importer's FHP passes. RULED:** for Phase 1 the derivation is called at the END
    of the import, on the normalized chart, so Phase 1's stored stream equals what Phase 2 derives
    at read. `resolveSlideIns` asks a NATURAL-stream derivation for its approach delta. The
    fabricated exit-ride (`resolveSlideOutExits`) and dip placements stay in Phase 1, flagged;
    **Phase 2 rules** whether to drop them or move them into the projection's `FhpViewState`
    ramps (the camera lead's tier).
12. **Q12 — pass criteria. RULED:** acceptance (b) passes when the port's exact agreement is
    within ±0.05 pt of the Python reference re-run on bar-line breaks only, with the port's input
    choices (Q1), and the scorer prints the intermediate counts — fretted notes scored, groups,
    plants, emissions — so any gap can be attributed. **Seam-vs-shift agreement is defined now**:
    from the census counters `spans_crossed_by_fhp_shift` and `fhp_shifts_inside_spans`, over the
    emitted shifts not carried by a slide. Phase 6's threshold on it stays the user's.

### The hand rulings (RULED 2026-09-15; kept as the record of what was decided)

- **60-H1 — one object.** §2. REVERSED 2026-10-04 for authored records (§1a).
- **60-H2 — `Ctrl+H` / `Ctrl+Shift+H`, id `0x1516`.** Built. H reads as "what the hand holds";
  `Cmd+H` is Hide on macOS (60-D10), a cost accepted, with the macOS default decided when the
  chord ships there. The record key and the selection kind's name are item 6 above.
- **60-H3 — the hand row's objects are the derived hand events, authored markers among them.**
  Phase 3 item 1; today the row walks the stored placements.
- **60-H4 — the open-stretch window statement is a field of the hand marker, not a fourth
  record kind.** Stands, generalized by item 4: the field is the marker's `fret` / `endFret`,
  valid wherever a marker may stand, built 2026-09-25/29.
- **60-H5 — the pinned chip is selectable, like the ruler's pin.** Built.
- **60-H6 — acting on an off-screen selection reveals it.** Built on the shipped rows (4d) and
  inherited.

### The row decisions carried from the keyboard plan (D6–D12; D13 stayed there)

- **60-D6 — duplicate fronts are strictly invalid**, refused by the validator (built, Phase 0)
  and before the record exists by the funnel (built, the stopgap) — not a keyed container type.
- **60-D7 — release when "the front names nothing"**, not on any chart edit. Built.
- **60-D8 — zero-length spans stay out of the row.** Stands.
- **60-D9 — one lazy hand-stream cache.** Resolved by Phase 2: the events live in the hand layer,
  `FretHandResolutions`, memoized beside the notes layer.
- **60-D10 — `Cmd+H` is Hide on macOS.** Accepted as H's cost in 60-H2.
- **60-D11 — pointer selection: in.** Built for chips; rails are Phase 3.
- **60-D12 — the selected pin yields exactly as the ruler's does.** Built.

