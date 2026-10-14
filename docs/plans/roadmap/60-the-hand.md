# Plan 60 — The Hand (master): fret-hand positions and holds

## 0. The family — SPLIT 2026-10-04

The hand is one design built as two plans, in this order (user, 2026-10-04):

1. **The rename sweep** — below. DONE 2026-10-04 (`c910b8d8`).
2. **`docs/plans/roadmap/60a-fret-hand-positions.md`** — where the hand is. DONE: Phases 0–3,
   Phase 3's sighting signed 2026-10-09.
3. **`docs/plans/roadmap/60b-holds.md`** — what the hand holds. NOT STARTED; Phases 4 and 5.
4. **Phase 6, the let-ring hand coupling** (§7 here) — after Phase 4, because it needs the
   positions and the hold; it needs nothing from Phase 5, which may follow it (§10).

This file is the MASTER: the redesign both plans execute (§1a), the laws (§5), the current-state
inventory (§6), the G60 gate record (§9), the sequencing, the final acceptance bundle and the docs
list. It was `60-the-hand.md` until the split.

**Section numbers belong to the family, not to a file,** so every "§n", "Phase n" and "§9 Qn" in
any of the three still names one place:

| Where | Lives in |
|---|---|
| §§1–6, §9's gate record and its open-gates table, §§10–12, Phase 6 | this file |
| The position's design, Phases 0–3, §8, §9's Phase 1 build rulings, hand rulings and row decisions | `60a-fret-hand-positions.md` |
| The hold's design, the grip dictionary, the former Phase 3 items 3–5, Phases 4 and 5 | `60b-holds.md` |

### The rename sweep — DONE 2026-10-04 (`c910b8d8`)

One mechanical change set with no behaviour change, so that everything after it is written in the
final vocabulary (§1a, "Names and the format"):

1. **The timeline key `position` becomes `at`** in the save files: `notes[]` and the fret-hand
   list in the chart document, tempo `anchors[]`, `sections[]` and automation `points[]` in the
   song document — reader, writer, `docs/developer/file-formats.md` and every JSON fixture.
   `toneChanges[].start` becomes `at` too (user, 2026-10-04): it has no `end`, and `start` is
   kept for a range that states one.
2. **`fhps` becomes `positions`.**
3. **The position's chords move to `Ctrl+P` / `Ctrl+Shift+P`,** keeping their command ids, which
   frees the `H` pair for the hold.
4. **Code names that say "hand" and mean the position:** `MarkerRow::Hand` to
   `MarkerRow::FretHandPosition`, `g_hand_key`, `CaretJumpHandRow`, the command labels, and "hand
   marker" in comments and the developer guide. The C++ `position` fields and `GridPosition` STAY.
5. **The importer repo follows:** the converter and the harness's package reader write and read
   the new keys, and the scored corpus is rebuilt.

Acceptance: build and full tests green on a forced relink; the `[.fhp-score]` scorer reads every
rebuilt package and prints the number it printed at Phase 1's close (88.213% exact). That one run
is the only check that the converter's keys and the reader's agree, since the two are changed
apart. The census is not re-run: it reads Guitar Pro files and touches no renamed key.

**Result:** all six suites pass on a forced relink; the scorer, on the `relwithdebinfo` build,
read all 1,301 rebuilt packages and scored 88.213% (76 s, against 435 s on `debug`). The four
projects were converted and load through `Project::load` with every count matching; the
old-key originals are refused, so the reader has no fallback. Beyond the list above, the sweep
also renamed `FocusRowJump::Hand`, `onHandChordRequested`, `onHandEndClearRequested` and
`HandChordTarget`, since "the hand chord" will mean the hold's. The user's four projects on disk survive it through a
throwaway key-rename script, run on a backup-first copy and never part of the product.

## 1. Status

**MERGED 2026-09-15; IN G0 SCOPE by user ruling 2026-09-19; REVISED 2026-10-02 against the
code and the 2026-09-25..29 rulings; GATE G60-RULINGS RULED 2026-10-02.** This plan is the union
of the former plan 60 (FHP derivation) and plan 61 (span-marker redesign), plus the two hand rows
the keyboard-focus-rows plan had queued as its steps 4b and 4c. The law-by-law session the user
asked for ran on 2026-10-02 and ruled every item of §9; item 1's condition (rules 12 and 13
re-measured) was discharged 2026-10-03, so Phase 1 is open with nothing in front of it. FHPs and
span markers are part of minimum required editing, not a post-editor pass.

**RE-VERIFIED 2026-10-04 against `9b2e069c`; PHASE 1 IS READY, with no open question in front of
it.** A readiness audit that day re-checked every anchor of §6 and every phase scope against the
code, found four importer FHP passes the plan never named, and put twelve Phase 1 questions to the
user, who ruled all twelve the same day (§9, "Phase 1 build rulings — RULED 2026-10-04"). §6 and
the phase scopes carry the corrections; the gates still open for later phases are listed at the
end of §9.

**REDESIGNED 2026-10-04 (§1a), after Phase 1 closed:** the position marker and the span marker are
TWO authored kinds again — `positions` and `holds` — and the zone, the bare marker and the optional
fret are gone. §1a supersedes §2 and revises Phases 2 to 5; read it before anything below it.

**PHASE 1 CLOSED 2026-10-04; PHASE 2 IS NEXT.** The derivation is built and scores 88.213% exact
on the rebuilt corpus, identical to its reference (§7, Phase 1, Acceptance). Two acceptance items
were deferred at the close to Phase 6's entry, where the stream they measure is the final one: the
per-file offender worklist with the user's sightings, and the slide split of the seam-vs-shift
measure.

**PHASE 2 DONE 2026-10-08** (60a's status lists the commits).

What has landed since the merge, and what this revision folds in:

- **Phase 0 is DONE** and the **hand-marker stopgap is BUILT and merged** (2026-09-26, with the
  authored end fret 2026-09-29): the stored stream is strict, the hand marker kind exists with its
  row, selection, chords, funnel snapshot and verbs, and the window's width is derived from the
  notes unless an end fret is authored (`docs/plans/completed/hand-marker-stopgap.md`). Phase 3
  is therefore mostly built; §7 lists only what remains.
- **Held stops are derived-only and the span-claim machinery is gone** (2026-09-29): a right-hand
  onset states no grip; `chartHeldStops` derives a tap's stop (a pull-off's plant, else the
  covering grip's stop); THE TAP'S FLOOR splits a span where a tap reaches its grip's stop
  (`chart_shapes.cpp`, built). Every "claim" in the design records predates this.
- **The algorithm is 13 rules**, not 11: rule 12 (plants are coverage, RULED 2026-09-18) and rule
  13 (the behind-the-nut bend, RULED 2026-09-28) joined after the harness measured the first
  eleven. ~~Neither is measured yet.~~ SUPERSEDED 2026-10-03: both re-measured (`rules12_13.py`),
  88.09% exact for the 13 rules against 88.03% for the first eleven (§9 item 1).

The design records this plan executes, in the order a new reader should take them:

- `docs/plans/completed/fhp-derivation-algorithm.md` — the measured algorithm (rules 1–11
  measured, 12–13 ruled and re-measured 2026-10-03), the tension resolutions, the
  tap-corrected evaluation regime, the profiles verdict, and the fingering / template / manual
  specification model. Source of truth
  for Phases 1, 2 and the fingering half of Phase 5. Its "three record kinds" residue is
  superseded by §9 item 4 of this plan.
- `docs/plans/completed/hand-marker-stopgap.md` — what is built: the record, the derived width,
  the marker kind and its verbs. Completed work; the plan builds on it.
- `docs/plans/todo/span-marker-redesign.md` — the founding principle, the template model, the
  resolution and invalidation laws, the settlement rider, the extent invariant, the signed span
  boundary gestures, and the span execution phases. Source of truth for Phases 3 (remainder), 4
  and the template half of Phase 5. Todo-tier: re-verify before building, and read every "claim"
  in it as retired.
- `docs/plans/todo/chord-dictionary.md` — the grip library the markers reference. Its stamp
  model is superseded by its own 2026-09-05 section (references ride the marker).
- `docs/plans/completed/keyboard-focus-rows.md` — the marker-row keyboard model the hand row
  reuses (built through 4d; its 4b/4c bodies moved here as §8).

## 1a. The 2026-10-04 redesign — positions and holds (SUPERSEDES §2 for authored records)

Ruled by the user in the design conversation that followed Phase 1's close. Where this section
and anything below it disagree, this section wins; the phases carry a REVISED note each, and the
text they supersede stays as the record of what was decided before.

**The reversal, and its reason.** 60-H1 (§2) made the position marker and the span marker one
object. For AUTHORED records that is reversed: a span is what states a grip, and a position does
not. The physical law §2 rested on — a position cannot change inside a span — never needed a
shared record: the derivation order states it once (positions feed spans, and Phase 6 clips a span
at every shift). Stated on one record, the two halves have different lifetimes, never constrain
each other, and force a bare "states nothing" marker into existence. The DERIVED hand events stay
one stream.

**The principle for both kinds.** A derived statement is never deleted, because nothing of it is
stored; it is SUPERSEDED by an authored one. The derivation asserts only what the chart proves, and
the charter states the rest.

The position's design is in `60a-fret-hand-positions.md`; the hold's, the grip dictionary's
and the list of what is still open are in `60b-holds.md`.

### Names and the format

- **Save-file keys:** `positions`, `holds`, `grips`. 60-H2's `hand` (§9 item 6) is dropped.
- **The timeline key `position` becomes `at`** on notes, positions, tempo anchors, sections and
  automation points, so that `positions` can mean the hand's. `start` and `end` are the hold's, a
  range's two ends. `toneChanges[].start` becomes `at` as well (user, 2026-10-04): it has no
  `end`, so `start` appears only where a range states one.
  The code keeps `position` (`GridPosition`, `note.position`): `at` is a label, not a second noun.
  It lands as its own commit AHEAD of Phase 2, with the converter and the scoring harness in the
  importer repo and a rebuild of the scored corpus. The user's projects on disk are carried across
  by a throwaway key-rename script.
- **Code names** (naming review, 2026-10-04): `FretHandPosition` and `FretHandPositionSelection`
  stay; `MarkerRow::Hand` becomes `MarkerRow::FretHandPosition`; the hold's record is `HandHold`,
  never a bare `Hold`, which the display's hold channel (`chartHolds`) and the game's sustain
  scoring already mean.

### What this reverses

60-H1 for authored records, and 60-H2's key; the extent invariant (law 6, non-goal "No direct span
resize", user-confirmed 2026-09-01); the 2026-10-02 strike of the front-move gesture, replaced by
edge moves that author a hold instead of rewriting tails; law 4's "the stop stays outside"; and
law 7's "markers are deletable" as the only rule, which now reads "authored statements are
deletable, derived ones are superseded".

## 2. The one object — RULED 2026-09-15; REVERSED 2026-10-04 for authored records (§1a)

**A fret-hand position marker and a span marker are one object: the hand marker.** This confirms
the 2026-09-05 shared-object ruling and closes the "one marker kind or two" question the 2026-09-12
chord reservation reopened.

The physical argument (user): the hand's position cannot change during a span, because a span is
that hand holding a shape. A slide is the one motion inside a span, and the span ends at its
landing, where a new position and shape are stated. The converse is already the ruled pipeline
direction: written notes yield positions, and positions cut rings (the let-ring hand coupling in
Phase 6 clips a span at a position shift), so a span can never straddle a position change by law.
"The position changes only at a span front or in a gap" is therefore structural, and two objects
would have to restate it by hand — the project's named recurring defect.

What the one object IS in each layer:

- **In the derivation**: a **hand event** — a front, holding until the next, carrying the standing
  window and, where a span fronts there, a shape. Every span front is a hand event with a shape; a
  position shift in a monophonic passage is a hand event without one. The position stream is the
  events' windows, coalesced where consecutive events share one; the span stream is the events
  that carry a shape, each closing at its musical close. Boundaries are changes and fronts are the
  only stored coordinate — the tone model's law again.
- **In the save file**: the hand marker record — a forced statement boundary at a grid position,
  carrying the optional direct window statement the stopgap built (`fret`, `endFret`; §9 item 4
  asks the user to confirm that generalization), later wearing the optional template reference
  (Phase 5) under the "no field before its writer exists" rule. The record exists today under the
  key `fhps` (`chart.h` `FretHandPosition {position, fret, end_fret}`); only its `fret` is still
  required, which Phase 2 relaxes.
- **In the derived layer**: a marker is a forced boundary in BOTH machines — the FHP run head and
  the span front coincide at it by shared authored input, never by one derivation feeding the
  other. The one-arrow order stands: notes + authored statements → positions → spans / rings.
- **On the surfaces**: the position chip and the span brackets stay what they are today (the chip
  is `FhpViewState`, width derived; the brackets derived); an authored marker additionally draws
  its tell in the editor 2D lane; a selected hand event outlines its chip (built) and its rails
  (Phase 3). Nothing new is invented for the highway.
- **On the keyboard**: ONE hand row between the time signature and the strings (§8, built), one
  author chord and one jump chord (`Ctrl+H` / `Ctrl+Shift+H`, built), one selection kind
  (`FretHandPositionSelection`, built). `Ctrl+P` / `Ctrl+Shift+P` are in the free pool and command
  id `0x1517` is unassigned. The span record's original `Shift+S` chord stays withdrawn.

## 3. Goal

Replace the FHP generator with the corpus-measured **13-rule figure-segmented floor tracker**
(88.09% exact agreement with 4,555 professionally reviewed arrangements, against 88.03% for the
first eleven rules and ~70% for the shipped walk; coverage 100% after the tap correction; churn at
the authored rate); make the position stream **derived at read** from the notes plus the
authored opinion layer — the hand marker with its optional direct window statement, and (Phase 5)
its template
reference; land the rest of the **hand marker** as the forced statement boundary both derivations
honour — the derived events as the row's objects, rail selection, the authored tell, the signed
boundary gestures; land **span-free zones**, **grip templates** in the song-scoped dictionary with
the template editor; and close with the **let-ring hand coupling** that clips derived spans at
position shifts.

## 4. Non-goals

- **No profile UI** (60-Q2 records the measured recommendation; overrulable there, not here).
- **No heuristic finger derivation, ever** — position is objective (97% expert agreement),
  finger is not (67%); fingers are authored or absent.
- **No authored anticipation** — anchors state on-onset truth; the highway/camera LEAD (~0.8–1.2
  s, fixed ~300–450 ms minimum-jerk move animation) is a projection-layer transform (feeds the
  open camera-timing review), never early data.
- **No second width authority.** Width = max(4, run hull) is the derivation's law; the ONE way to
  say otherwise is the marker's authored end fret (RULED 2026-09-29, built), which is a statement
  the derivation consumes, not a patch on its output. The former "no width overrides" non-goal is
  superseded by that ruling (§9 item 4); the stored-width field it refused stays gone.
- ~~**No direct span resize**~~ REVERSED 2026-10-04 (§1a): a span's edge moves, and the move
  authors a hold. The struck text: the extent invariant (user-confirmed 2026-09-01): a span's
  extent is strictly derived from its content; every want routes through tails, markers, zones or
  Delete.
- **No pointer AUTHORING gesture** for markers — chords only (the 2026-09-12/14 reservation);
  pointer SELECTION is in and built (60-D11).
- **Not W10's split-tail verb** — its kinship with the split marker is noted for W10's build.

## 5. Laws carried in, binding on every phase

Each is recorded in full in its design record; they are listed here so no phase can be built
without them in view.

1. **Sound founds. Markers define.** (span record, founding principle, minus the retired claim
   clause.) A statement exists only by something sounding; deliberate span authoring is an
   explicit marker record. A right-hand onset states no grip; its one span-facing fact is THE
   TAP'S FLOOR (RULED 2026-09-25, built 2026-09-29).
2. **One arrow.** notes + authored statements (markers with their direct statements, template
   references) → position derivation → span / ring derivation. Never the reverse: spans consume
   position shifts. **Consequence made explicit 2026-10-02:** the position derivation may read the
   notes' PLANTS (`chartPlantedStops`, derived from the notes alone) but never
   `ChartResolutions::held_stops`, whose DEFAULT tier is read out of the derived postures — a
   position derivation reading it would read spans, and Phase 6 would then close a cycle. The
   stopgap's width fold reads `held_stops` today (`chart_fret_hand.h`, `deriveHeldFretRanges`),
   which is legal only while spans do not yet read positions. ~~Phase 1 replaces that fold.~~
   SUPERSEDED 2026-10-04 (§9 Q10): the fold stays through Phase 1 and goes in Phase 2; still
   legal, since spans read no positions until Phase 6.
3. **The zero-authored-input invariant** (user, 2026-09-05). Positions must derive sanely from a
   fresh import with NO manually authored records, and cleanly with NO fingering information
   anywhere — imports may carry none, so fingerless is the PRIMARY mode, not a degraded one. The
   invariant constrains ABSENCE, never INFLUENCE: authored statements are optional as inputs but
   AUTHORITATIVE when present — a stated fret pins its run's floor outright, a stated end fret
   pins its top, and a statement incompatible with the standing window forces a re-derivation at
   its position like any other boundary. Every rule has a fingerless form that stands alone —
   rule 7's floor default IS that form (correct alone 88.8% of the time corpus-wide). The 88.09%
   corpus score of the 13 rules (88.03% for the first eleven) was measured with zero authored
   inputs, exactly the fresh-import scenario. No
   phase may add a rule that REQUIRES an authored statement to function.
4. **Statements are inputs, never output patches.** A marker bounds and pins the run it heads; a
   stated fret pins the floor; a stated end pins the top; every note edit re-derives coherently
   around standing statements. Nothing ever writes a derived output. An authored window that does
   not cover a stop reports that honestly — the stop stays outside (RULED 2026-09-25 with the
   stopgap) — rather than being refused.
5. **Invalidity is unrepresentable by the derivation being total.** Every verb edits inputs
   (tails, markers, zones), and the derivation derives something legal from every input state.
   Fronts are unique and strictly ascending — enforced as 60-D6 states (built).
6. ~~**The extent invariant.** Spans are never resizable; the zone is the one resizable record
   because it has no content to derive from.~~ REVERSED 2026-10-04 (§1a): a DERIVED span's extent
   is still strictly its content's, and an edge move authors a hold, whose two ends are the
   charter's statement.
7. **Authored statements are visible and deletable; derived ones are superseded, never deleted**
   (reworded 2026-10-04, §1a); derived boundaries draw nothing extra. Every authored
   marker draws a tell in the editor 2D lane; absence of the tell IS the derived tell. The highway
   draws derived results only.
8. **Format minimalism.** A marker stores its location and its direct statement, and nothing else
   until a field's writer exists. Format changes in place; no migration path, no version bump.
9. **One gesture authority.** Every hand verb runs through `commitChartGestureStep` (one undo entry
   per gesture, retire-on-no-change) and every marker verb through the `commitMarkerModel` funnel
   (build on a copy, refuse whole, publish once — `marker_model_commit.h`, built).

## 6. Current-state inventory (verified 2026-10-04 against `9b2e069c`)

First written 2026-10-02; every bullet below was re-verified 2026-10-04 by the readiness audit,
its anchors corrected, and the importer passes it had missed added.

- **The generator — verified 2026-10-04.** `generateFretHandPositions`
  (`rock-hero-editor/core/src/project/gp_chart_builder.cpp:2200`) is still the phrase-aware walk:
  greedy minimal shift + section / 0.8 s-rest re-anchoring (`g_fhp_phrase_rest_seconds`, `:2102`;
  the section places arrive as `phrase_boundary_places`, collected at `:3897-3918`) + the
  pinned-finger window union (`anchorsFretHand`, `:2106`) + the held-hull slide reshape
  (`heldHullAtSlideKeyframe`, `:2118`). It is handed the built notes AND the slide
  `ChartConnections` (`:2201`) and the capo, and is called once, at `:3675`, on the NATURAL stream
  — before the let-ring figure law, the same-string clamp, lattice rounding and `normalizeChart`.
  The first-entry retime (the first placement moved back to the chart's first note) lives INSIDE
  the generator (`:2467-2476`). Import-only; its result is stored in the chart and drifts from the
  notes on every later edit. Plant-blind (rule 12 unmet).
- **The importer's four further FHP passes — MISSING from the 2026-10-02 inventory, verified
  2026-10-04.** The stored stream is not the generator's output alone:
  - `resolveSlideIns` (`:2590`, run at `:3677`) READS the placements to choose a scoop's start fret
    and fabricates dip and restore placements (lifecycle rule 16);
  - `resolveSlideOutExits` (`:2736`, run at `:3799`) fabricates exit and restore placements that
    ride every unpitched slide-out (lifecycle rule 9, "the window rides the rail");
  - `roundOntoTickLattice` (`:2901`) re-addresses the placements onto the tick lattice and drops a
    collided one (`:2945-2957`);
  - `dropRestatedPlacements` (`:2968`, run at `:3835`) drops a placement that restates its
    predecessor.
  Both slide passes read the windows through `fretWindowsOf` (`:2532`), which calls
  `deriveFretHandWidths`. None of the 13 rules produces the exit ride or the dips.
- **The stored record — verified 2026-10-04.** `FretHandPosition {position, fret, end_fret?}`
  (`chart/chart.h:2214`), under the key `fhps` (read at `chart_document.cpp:689`, written at
  `:778`; `file-formats.md`). Width is DERIVED once per projection (`deriveFretHandWidths`,
  `chart_fret_hand.h`, read at `chart_projection.cpp:498`) unless `end_fret` states the window's
  top; `FhpViewState {seconds, fret, width, end_authored, ramp…}` is the chip
  (`chart_view_state.h:958`). The validator `validateFretHandPositions` (`chart_rules.cpp:202-250`)
  refuses equal positions (the strictly-ascending check at `:246`), an end below the fret, and an
  off-grid position, and normalizes the window onto the board (`normalizeFretHandPosition`,
  `:641`); the reader does not re-check order (the stopgap's Phase 0 deviation: the validator runs
  on every load and nothing searches the stream in between). The placements are also re-addressed
  by the timeline transform (`timeline_readdress.cpp:217`), by the same content map as the notes.
- **The hand marker kind is built — verified 2026-10-04** (`fret_hand_handlers.cpp`):
  `MarkerRow::Hand` (`editor_controller_impl.h:1512`), `FretHandPositionSelection {position}`
  (`editor_selection.h`), `InsertFretHandPosition` `0x1406` and `CaretJumpHandRow` `0x1516`
  (`rock-hero-editor/ui/src/keybinds/editor_command_id.h`; `0x1517` free), `Ctrl+H`
  insert-or-restate at the cursor (`handChordTarget`; the insert defaults the fret to the lowest
  stop its stretch holds, `defaultedPlacement`), `Delete`, `Alt+←/→` by one placement quantum
  (`steppedNudgePosition`), bare digits typing the START fret, `Alt`+digit the END fret,
  `Alt+Delete` clearing it (`ClearFretHandEnd`), every verb through `commitMarkerModel` over a
  `FretHandPositionsSnapshot`. One bound for every marker kind, `markerCanStartAt`
  (`chart_rules.cpp:150`). A chip click selects the placement and seeks nothing (the hit test is
  `TabView::fretHandChipAt`, `tab_view.cpp:356-383`); the pinned chip is an ordinary selectable
  chip whose column is the pin's (`:340-347`); the selected chip is outlined, pinned or scrolling
  (`ChartEditViewState::selected_fret_hand_position`, an `optional<size_t>` index,
  `editor_view_state.h:944`). Acting on an off-screen marker selection reveals it
  (keyboard-focus-rows step 4d, built and sighted). The controller caches only the scene
  (`editor_controller.cpp:2027`), never a `ChartResolutions`; the row's holder is
  `markerHolderIndex(starts, position)` (`editor_controller_impl.h:1527`).
- **Spans — verified 2026-10-04.** Derived at read (`chart_shapes.cpp`), store nothing, never
  overlap, leave gaps; the three-member accumulation minimum is a signed constant; THE TAP'S FLOOR
  is built (`:935`). The held stop is derived by `chartHeldStops` (plant, else the covering grip's
  stop; an open string holds nothing) and lives in `ChartResolutions::held_stops`
  (`chart_legato.h:402`), read OUT of the postures. `NoteAttack::None`, the `N` verb, the stored
  `held` field and the span-claim machinery (`sweepInertClaimedStops`, `InertHeldStop`,
  `claim_shapes`) are all gone. Spans are not selectable: `chartSpanRevealed`
  (`rock-hero-editor/core/include/rock_hero/editor/core/chart/chart_reveal.h:115`) has three
  grounds — lane reveal, a selected member note, the caret — and no selected-span ground; no rail
  geometry is exported (`tabShapeRailBounds` does not exist).
- **Whole-note places are THE axis — not in the 2026-10-02 inventory, verified 2026-10-04.** Since
  `987b62cf` / `f5769734` every per-revision pass reasons in whole-note `Fraction` places:
  `ChartConnections::places` (via `chartNotePlaces`), `ChartShape::place` and
  `ChartShape::bracket_place` (`std::optional<Fraction>`, `chart_shapes.h:284`). `FretWindow` and
  `chartPlantedStops` exist.
- **No template, dictionary, zone or fingering storage exists anywhere — verified
  2026-10-04.** The GP parser reads no fingering and no chord diagrams
  (`rock-hero-editor/core/src/project/gp_score.h`), so nothing could write a finger today.
- **Measurement — verified 2026-10-04.** The ground-truth dataset (16 shards, 4,555
  arrangements) lives in local scratch outside every repo, per the corpus firewall; no C++ reads
  it. The Python harness (`final.py`, `combined.py`, `rules12_13.py` and the dataset's consistency
  sweeps) is committed in the importer repo under `custom-song-importer/tools/fhp-harness/`
  (`950d071`, `774b8f3`) and finds the dataset through `ROCKHERO_FHP_DATASET_DIR` (`_dataset.py`;
  the variable is set at user scope, so a shell started before it was set does not see it).
  `final.py` prints 88.03%, the 11-rule figure; the 13-rule headline is 88.09%, from
  `rules12_13.py` — in a configuration that breaks at bars AND at sections and phrase-iterations
  (`bars + sects`), which the port will not have (§9 Q1). The GP corpus census
  (`test_corpus_census.cpp`, hidden `[.local-corpus]`, `ROCKHERO_GP_CORPUS_DIR`) carries the
  travel-aware pinned-finger counter, the hand-coupling gate counters (`spans_crossed_by_fhp_shift`,
  `fhp_shifts_inside_spans`) and the unfretted-arrival counters, and reads the width fold
  (`:2316`); three fret-hand-window rows have drifted below their pins since 2026-09-24
  (`watch-items.md:583`). The sighted offender worklist and watch entries J and K are the
  falsification ground truth.
- **The external converter — not in the 2026-10-02 inventory, verified 2026-10-04.** Since
  `a26cc16`, `custom-song-converter.py`'s `build_fhps` (`:1614-1653`) writes `endFret` wherever the
  source width is not 4; it no longer writes `width`.
- **Sections start only on measure downbeats — verified 2026-10-04** by validator
  (`song_section_rules.cpp:42-51`), so a section start is always a bar line; `readdressTimeline`
  also snaps every section back onto its measure downbeat.

## 7. Phases

Each phase is built, committed and sighted on its own. Gates are named where a Q blocks.

Phases 0 to 3 are in `60a-fret-hand-positions.md` and Phases 4 and 5 in `60b-holds.md`.
Phase 6, which needs both plans, is here.

### Phase 6 — the let-ring hand coupling (closes the sequence; gated on the census measure)

Derived spans clip at any emitted position shift not carried by a slide. Readiness gate: the
census seam-vs-shift agreement measure — defined 2026-10-04 (§9 Q12) from
`spans_crossed_by_fhp_shift` and `fhp_shifts_inside_spans` over the emitted shifts not carried by
a slide; its pass THRESHOLD is the user's, OPEN until this phase. **Owed at this phase's entry,
deferred from Phase 1's close (2026-10-04):** the measure's slide split — the product's one
predicate for a shift carried by a slide, read by the census over the derived events (Phase 1
took only the unsplit ceiling, 447 windows inside 402 spans); and the per-file offender worklist
re-measured and walked with the user (§11). Also OPEN and
owed before this phase (§9 Q3, 2026-10-04): rule 6 pins on the STORED sustain, which includes the
let-ring extension, while this phase clips rings — the two must be reconciled by a ruling. Rule 6
concentrates the clips where a hand demonstrably moved, which is what makes the coupling honest.
Structural
precondition (law 2): by this phase the position derivation reads plants, never `held_stops`,
or the coupling closes a cycle. The watch item on same-voice drones under co-struck melodies
(rescoped 2026-09-01) is re-read here.

## 9. Decision gate G60-RULINGS — RULED 2026-10-02

One list, ruled item by item in the law-by-law session of 2026-10-02. Items 1–3 unblock Phase 1,
4–7 Phase 2, 8 Phase 3, 9–12 Phase 5. Each keeps the evidence it was argued from, then the
ruling; the full evidence lives in the design records.

1. **60-Q1 — the 13-rule algorithm.** The two agenda questions carried from the former plan: (a)
   the unlabelled-section question is MOOT — a section is a measure downbeat by validator, so the
   bar-line trigger subsumes it; rule 3 reads "a bar line" alone in RockHero; (b) the claims
   question is MOOT (claims are deleted) and becomes: plants (rule 12, from `chartPlantedStops`)
   JOIN coverage; the DEFAULT held tier (the covering grip's stop under a tap) does NOT — it is
   read out of the spans, which law 2 forbids the derivation to read, and the data already says
   taps float. The tap-correction (a) and the width amendment (b) of the former Q1 text stand as
   recorded in the design record. **RULED: SIGNED, conditional on re-measuring rules 12 and 13 in
   the harness and the headline holding — condition DISCHARGED 2026-10-03.** Rule 12 lifts the
   headline from 88.03% to 88.09% exact and keeps 100% of the corpus's 34,405 plants in reach
   (the authored anchors 99.72%, the 11-rule champion 91.46%), in the form where the plant is a
   demand from the source's onset even when the source is a tap; rule 13 has no population in the
   corpus (no open string is bent) and stands as ruled physics. The user signs it as the starting
   point and expects a rework once common fingerings for known chords enter at Phase 5 (item 10);
   that rework is a Phase 5 question, not a reopening of this gate.
2. **60-Q2 — profiles.** The measured verdict was two surviving parameters, `rest_break_seconds`
   (off) and `section_breaks` (on); the second is now structurally a no-op (item 1a), and the
   first is off. The parameter vector is therefore EMPTY. **RULED: no profiles and no parameter
   at first release; profiles stay a FUTURE FEATURE the design must not foreclose.** What keeps
   the door open: the derivation is one pure function with its named constants in one place (law
   3's named-constant requirement), so a future profile is a parameter struct passed into that
   function — additive, no redesign. And a future profile is CHART-SCOPED, stored in the song,
   never a user preference: markers are authored against one derivation's output, so a per-user
   derivation would make a marker redundant for one user and wrong for another.
3. **Where Phase 1 lives and what accepts it.** A pure function in `rock-hero-common/core`
   beside `chart_fret_hand.h`, and a hidden-tag C++ scorer over the local dataset as the
   acceptance instrument (the port must be proven to BE the measured algorithm; the Python harness
   stays the reference). **RULED: yes to both.** The home is forced by item 5, not preferred:
   once nothing is stored, the game must derive the positions itself to draw the hand window,
   frame the camera and clip let-ring spans at shifts, and the resolutions bundle both products
   read already lives in common/core. Cost: a dataset reader in test code, medium.
4. **The marker's direct window statement — `{position, fret?, endFret?}` everywhere.** The
   stopgap built `fret` (2026-09-25) and `endFret` (2026-09-29) as authored statements the
   derivation consumes, an uncovered stop reporting honestly rather than being refused. Keeping
   them as the marker's one direct statement, valid anywhere a marker may stand, supersedes
   three earlier texts: §4's "no width overrides", 60-H4's "valid only over silence, refused
   elsewhere" (and the mid-ring refusal of the design record), and "the override language is
   fingering alone". One record kind, one row, one tell; the fingering of Phase 5 becomes a
   SECOND way to pin the floor, through a template, not the only one. **RULED: yes — the plan's
   law.**
5. **60-Q4 — the derived stream.** With item 4, `fhps` stops being a stream and becomes the
   marker list; the stored residue is the markers (and Phase 5's template references). The
   importer authors nothing. **RULED: yes.** Markers are never lost to derivation: it reads them
   and never removes them (rule 11 dedups output EVENTS, not records). **No old-format support in
   the product**: the format changes in place (law 8), and a pre-Phase-2 package with hand-placed
   markers, should one exist, is converted by a THROWAWAY SCRIPT outside the product (beside the
   external converter) that renames the key and writes each old placement as a fully stated
   marker; pruning the ones that state nothing is Phase 3's verb (item 6 there), which runs over
   the current format only. Generated placements carry no charter information, so for a package
   without hand-placed markers re-import beats conversion. Write the script only when such a
   package exists.
6. **60-H2 — the record key and the selection kind's name**, renamed WITH Phase 2's format
   change so the format changes once. `fhps` names a derived stream the record no longer is.
   **RULED: `hand`** (the record is the hand marker; the chord is `Ctrl+H`); the naming expert at
   Phase 2 confirms the spelling of the selection kind only.
7. **The free-note finger.** No writer exists (the GP parser reads no fingering), so law 8 keeps
   the field out until a writer lands. **RULED: deferred to the Phase 5 session; struck from
   Phase 2.** The user's direction for that session: the primary fingering carrier is the HELD
   GRIP — a marker's template fingering, which `Ctrl+H` is designed for — and per-note fingers
   that guide the derivation are a definitively planned future item, genuinely useful, not
   struck; they need their own writer before the field exists.
8. **`Alt+←/→` on the merged object.** A marker may stand anywhere on the grid (over silence
   included), so its step is the placement quantum (built). A derived span front has no grid
   position to stand on, and the signed content-editing front move (the front steps to the
   adjacent member onset by rewriting the boundary populations' tails) was examined against the
   extent invariant: a front that moved without the tails would state a hold the note does not
   make, and a front that moved WITH the tails is a second authority over the extent (law 6). A
   note that already rings through the span is already a member unless a boundary record stands
   between, so the only thing the gesture can honestly move is the boundary record. **RULED: only
   the marker moves.** `Alt+←/→` steps a marker one placement quantum and is inert on a derived
   front; a derived span changes through the sound (the tails' own verb) or a marker, and the
   derivation answers. The two invalid states a marker move can reach — a duplicate front, off the
   chart's start — are refused with the refusal flash (#278); everything else derives something
   legal (law 5). The content-editing FRONT gesture is STRUCK. The END gesture (bulk tail editing
   through the content) is carried to Phase 3's session as an open question, not decided here.
9. **The extend law** (`first-releasable-editor.md`, "An authored span swallows what validly
   fits", user direction 2026-09-19), rewritten without `held` (a swallowed note's held stop is
   what `chartHeldStops` already derives from the covering grip). **RULED: signed in that form.**
   It is the fit test of whichever verb admits a note to a span: the END gesture if Phase 3 builds
   it, else the span derivation's own membership rule over the edited tails.
10. **60-Q3 — the fingering model** (grip templates referenced from markers, stacked fingerings
    first-class, `anchor = floor − (finger − 1)`, per-note fingers per item 7) and the GP
    parser's scope. **RULED: deferred to its own Phase 5 session; the user expects it to be
    complex.** Nothing before Phase 5 needs the answer.
11. **60-Q5 — the derived-default suggestion** for unreferenced grips (it reverses the killed
    auto-match of 2026-08-31). **RULED: NO for now** — the killed auto-match stays killed and the
    picker's ranked suggestion is enough; reopenable at the Phase 5 session.
12. **Phase 5's purge-on-save and the invalidation rule's confirming word.** **RULED: deferred
    to the Phase 5 session with item 10.**

### Gates still OPEN, per phase (as of 2026-10-04)

REVISED the same day by §1a, which closes several entries of this table: Phase 2's
marker-semantics list and its selection-kind spelling (the names are ruled), Phase 3's END gesture,
and Phase 4's zone-kind question. The hold's design was re-ruled 2026-10-13 (`60b-holds.md`,
whose own "Still OPEN" list is the hold's: by-eye judgments and the fit test's clauses).

| Phase | Ruled | Still OPEN |
|---|---|---|
| 1 | 60-Q1 (item 1, condition discharged 2026-10-03), items 2 and 3, Q1–Q12 above | Nothing. CLOSED 2026-10-04 |
| 2 | 60-Q4 (item 5), items 4, 6 and 7 | Nothing. Ruled by §1a and 60a's Phase 2 build rulings (the exit ride KEPT as derived motion, the dips DROPPED); DONE 2026-10-08. |
| 3 | Items 8 and 9; the edge keys (RULED 2026-10-13, restating 2026-09-05: `Shift+Alt+←/→` moves a selected span's or hold's end, `Alt+←/→` its start only, walled at the first misfit; `60b-holds.md` item 4) | ~~The END gesture (OPEN by design: build it, or let the members' tail keys serve).~~ The tell and outline strength (a sighting). Q-D, which `bracket_place` probably answers. ~~How the derived events reach the controller (scene, or resolutions held) — a design call for the builder, not a user gate~~ |
| 4 | The hold's design (RULED 2026-10-13, `60b-holds.md`: `holds[] {start, end?, grip?}`, a pinned start, an optional authored end, the hold normalizer that trims and clears instead of refusing) | The settlement edge (re-verify on a regenerated reel; likely moot). The background triplet and the chip, tick and rail styles (a sighting) |
| 5 | 60-Q5 (item 11) RULED NO, reopenable there; the invalidation rule (item 12's second half — RULED 2026-10-13 as the hold normalizer's grip clearing, `60b-holds.md`) | 60-Q3, the fingering model, and the GP parser's fingering scope (item 10); purge-on-save (item 12) — DEFERRED to the Phase 5 session |
| 6 | The measure's DEFINITION (Q12) | The measure's slide split and the offender worklist's walk, both deferred from Phase 1's close. The pass threshold on seam-vs-shift agreement (the user's). The let-ring extension inside the stored sustain, which rule 6 pins on and this phase clips (§9 Q3), ruled before this phase. The drone watch item, re-read here |

## 10. Sequencing

**REVISED 2026-10-04 (the split, §0):** Phases 0 and 1 are done. Done: the rename sweep and Phase
2. Next: Phase 3 → Phase 4 → Phase 6 → Phase 5. The order below is the one this replaces; its
reasoning for the coupling ahead of the templates stands.

Revise the plan (done 2026-10-02; re-verified 2026-10-04) → G60 (ruled 2026-10-02) → the Phase 1
build rulings (ruled 2026-10-04) → Phase 1 → Phase 2 → Phase 3 remainder → Phase 4 →
Phase 6 → Phase 5. The three-beat ruling (user, 2026-09-05) holds: the derivation must stand alone
under the zero-authored-input invariant and is the coupling's prerequisite while needing nothing
from the marker work; the one object and its row land together; zones gate the settlement;
templates gate nothing, so the coupling (Phase 6) closes the sequence ahead of them once its
measure exists.

The interim stopgap is BUILT (2026-09-26) and nothing of it is ripped out: the hand row, its
selection, chords, funnel snapshot and verbs are Phase 3 deliverables that stay; Phase 2 changes
only their source, from the stored stream to the derived events. Fret entry stays as the direct
statement (item 4).

## 11. Final acceptance bundle

Build + tests green with the relink forced; the dataset scorer's coverage and exact-agreement
numbers recorded beside the harness's; census re-run with the position counters re-signed (fold
the three drifted rows and the stale-baseline re-sign into this pass); the offender worklist
walked with the user until the pins column is explained (FIXED-CLASS / OPINION / SIGHTED per
file); the seam-vs-shift agreement measure (defined §9 Q12) produced and consumed at Phase 6
(Phase 1 took its unsplit ceiling); the
sighting gates of Phases 3, 4 and 5 recorded; CI blind-spot reading pass over every touched hunk
(new variant alternatives → designated initializers, `-Wswitch-enum` on `MarkerRow`, optional
access in tests).

## 12. Docs to change as phases land

- **`docs/developer/the-project-lifecycle.md`** rules 5–9 (`:297-374`): rewritten to the charter
  law in Phase 1 (the 0.8 s rest and the minimal-shift wording go with the walk); rule 9's exit
  ride and rule 16's dips stay, flagged, until Phase 2 rules their home (§9 Q11).
- **`docs/plans/completed/fhp-derivation-algorithm.md`**: its status line and "three record
  kinds" residue after G60. MOVED to `completed/` 2026-10-08, when Phase 2 landed.
- **`docs/plans/completed/hand-marker-stopgap.md`**: completed work. MOVED to `completed/`
  2026-10-08.
- ~~**`docs/plans/todo/fhp-corpus-derived-generation.md`**: superseded (pointer added
  2026-10-02); delete or move to `completed/` when Phase 1 lands.~~ MOVED 2026-10-04 to
  `docs/plans/completed/`, with the walk it studied deleted.
- **`docs/plans/todo/span-marker-redesign.md`** and **`chord-dictionary.md`**: re-verify before
  Phase 3 and Phase 5; every "claim" reads as retired.
- **`docs/plans/in-progress/span-derivation-ground-up.md`**: rule 2's claim wording after the
  2026-09-29 retirement (the header was corrected 2026-10-02).
- **`keymap-matrix.md`**: the hand row's chords are Live; the span-free zone and template verbs
  go Live in Phases 4 and 5.
- **`docs/developer/`** command-id registry, `keyboard-input.md`, `the-editor-2d-views.md`: the
  derived events as the row's objects, the authored tell, the rail selection and the span reveal
  ground (Phase 3); `file-formats.md` and `chart.h`: the record's key (Phase 2), the template
  fields (Phase 5).
- **`marker-verb-grammar.md`**: the new-kind checklist ran once for the hand marker (done).
- **`docs/design/`**: none expected — the one-arrow order and the derived-over-authored rule are
  already recorded; confirm with the user before any rule changes.
- ~~**`docs/tracking/backlog.md`**: the stale "ruler shape-label band" comments in
  `tab_paint_core.cpp`.~~ SUPERSEDED 2026-10-04: the comments are gone from `tab_paint_core.cpp`
  and the backlog carries no such entry; nothing to change.
