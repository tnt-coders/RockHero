# Plan 60 — The Hand: derived positions, hand markers and spans

## 1. Status

**MERGED 2026-09-15; IN G0 SCOPE by user ruling 2026-09-19; REVISED 2026-10-02 against the
code and the 2026-09-25..29 rulings; GATE G60-RULINGS RULED 2026-10-02.** This plan is the union
of the former plan 60 (FHP derivation) and plan 61 (span-marker redesign), plus the two hand rows
the keyboard-focus-rows plan had queued as its steps 4b and 4c. The law-by-law session the user
asked for ran on 2026-10-02 and ruled every item of §9; Phase 1 is open, and its first task is the
re-measurement item 1 conditions the sign on. FHPs and span markers are part of minimum required
editing, not a post-editor pass.

What has landed since the merge, and what this revision folds in:

- **Phase 0 is DONE** and the **hand-marker stopgap is BUILT and merged** (2026-09-26, with the
  authored end fret 2026-09-29): the stored stream is strict, the hand marker kind exists with its
  row, selection, chords, funnel snapshot and verbs, and the window's width is derived from the
  notes unless an end fret is authored (`docs/plans/in-progress/hand-marker-stopgap.md`). Phase 3
  is therefore mostly built; §7 lists only what remains.
- **Held stops are derived-only and the span-claim machinery is gone** (2026-09-29): a right-hand
  onset states no grip; `chartHeldStops` derives a tap's stop (a pull-off's plant, else the
  covering grip's stop); THE TAP'S FLOOR splits a span where a tap reaches its grip's stop
  (`chart_shapes.cpp`, built). Every "claim" in the design records predates this.
- **The algorithm is 13 rules**, not 11: rule 12 (plants are coverage, RULED 2026-09-18) and rule
  13 (the behind-the-nut bend, RULED 2026-09-28) joined after the harness measured the first
  eleven; neither is measured yet.

The design records this plan executes, in the order a new reader should take them:

- `docs/plans/in-progress/fhp-derivation-algorithm.md` — the measured algorithm (rules 1–11
  measured, 12–13 ruled), the tension resolutions, the tap-corrected evaluation regime, the
  profiles verdict, and the fingering / template / manual specification model. Source of truth
  for Phases 1, 2 and the fingering half of Phase 5. Its "three record kinds" residue is
  superseded by §9 item 4 of this plan.
- `docs/plans/in-progress/hand-marker-stopgap.md` — what is built: the record, the derived width,
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

## 2. The one object — RULED 2026-09-15

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
(88.03% exact agreement with 4,555 professionally reviewed arrangements vs ~70% for the shipped
walk over the first eleven rules; coverage 100% after the tap correction; churn at the authored
rate); make the position stream **derived at read** from the notes plus the authored opinion
layer — the hand marker with its optional direct window statement, and (Phase 5) its template
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
- **No direct span resize** — the extent invariant (user-confirmed 2026-09-01): a span's extent is
  strictly derived from its content; every want routes through tails, markers, zones or Delete.
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
   which is legal only while spans do not yet read positions; Phase 1 replaces that fold.
3. **The zero-authored-input invariant** (user, 2026-09-05). Positions must derive sanely from a
   fresh import with NO manually authored records, and cleanly with NO fingering information
   anywhere — imports may carry none, so fingerless is the PRIMARY mode, not a degraded one. The
   invariant constrains ABSENCE, never INFLUENCE: authored statements are optional as inputs but
   AUTHORITATIVE when present — a stated fret pins its run's floor outright, a stated end fret
   pins its top, and a statement incompatible with the standing window forces a re-derivation at
   its position like any other boundary. Every rule has a fingerless form that stands alone —
   rule 7's floor default IS that form (correct alone 88.8% of the time corpus-wide). The 88.03%
   corpus score was measured with zero authored inputs, exactly the fresh-import scenario. No
   phase may add a rule that REQUIRES an authored statement to function.
4. **Statements are inputs, never output patches.** A marker bounds and pins the run it heads; a
   stated fret pins the floor; a stated end pins the top; every note edit re-derives coherently
   around standing statements. Nothing ever writes a derived output. An authored window that does
   not cover a stop reports that honestly — the stop stays outside (RULED 2026-09-25 with the
   stopgap) — rather than being refused.
5. **Invalidity is unrepresentable by the derivation being total.** Every verb edits inputs
   (tails, markers, zones), and the derivation derives something legal from every input state.
   Fronts are unique and strictly ascending — enforced as 60-D6 states (built).
6. **The extent invariant.** Spans are never resizable; the zone is the one resizable record
   because it has no content to derive from.
7. **Markers are visible and deletable; derived boundaries draw nothing extra.** Every authored
   marker draws a tell in the editor 2D lane; absence of the tell IS the derived tell. The highway
   draws derived results only.
8. **Format minimalism.** A marker stores its location and its direct statement, and nothing else
   until a field's writer exists. Format changes in place; no migration path, no version bump.
9. **One gesture authority.** Every hand verb runs through `commitChartGestureStep` (one undo entry
   per gesture, retire-on-no-change) and every marker verb through the `commitMarkerModel` funnel
   (build on a copy, refuse whole, publish once — `marker_model_commit.h`, built).

## 6. Current-state inventory (verified 2026-10-02 against `master`)

- **The generator** `generateFretHandPositions`
  (`rock-hero-editor/core/src/project/gp_chart_builder.cpp:2327`) is still the phrase-aware walk: greedy minimal shift + section / 0.8 s-rest re-anchoring
  (`g_fhp_phrase_rest_seconds`, `:2229`) + the pinned-finger window union + the held-hull slide
  reshape. Import-only, called once at `:3821`; its result is stored in the chart and drifts from
  the notes on every later edit. Plant-blind (rule 12 unmet).
- **The stored record** is `FretHandPosition {position, fret, end_fret?}` (`chart.h:2261`),
  under the key `fhps` (`chart_document.cpp:685`, `file-formats.md`). Width is DERIVED once per
  projection (`deriveFretHandWidths`, `chart_fret_hand.h`, read at `chart_projection.cpp:511`)
  unless `end_fret` states the window's top; `FhpViewState {seconds, fret, width, end_authored,
  ramp…}` is the chip (`chart_view_state.h:937`). The validator refuses equal positions, an end
  below the fret, and an off-grid position, and normalizes the window onto the board
  (`chart_rules.cpp:174-220`); the reader does not re-check order (the stopgap's Phase 0
  deviation: the validator runs on every load and nothing searches the stream in between).
- **The hand marker kind is built** (`fret_hand_handlers.cpp`): `MarkerRow::Hand`
  (`editor_controller_impl.h:1401`), `FretHandPositionSelection {position}`
  (`editor_selection.h`), `InsertFretHandPosition` `0x1406` and `CaretJumpHandRow` `0x1516`
  (`editor_command_id.h`), `Ctrl+H` insert-or-restate at the cursor (the insert defaults the fret
  to the lowest stop its stretch holds, `defaultedPlacement`), `Delete`, `Alt+←/→` by one
  placement quantum (`steppedNudgePosition`), bare digits typing the START fret, `Alt`+digit the
  END fret, `Alt+Delete` clearing it, every verb through `commitMarkerModel` over a
  `FretHandPositionsSnapshot`. One bound for every marker kind, `markerCanStartAt`
  (`chart_rules.cpp:121`). A chip click selects the placement and seeks nothing
  (`tab_view.cpp:406-418`); the pinned chip is an ordinary selectable chip whose column is the
  pin's (`:423`); the selected chip is outlined, pinned or scrolling
  (`ChartEditViewState::selected_fret_hand_position`, `editor_view_state.h:893`). Acting on an
  off-screen marker selection reveals it (keyboard-focus-rows step 4d, built and sighted).
- **Spans** are derived at read (`chart_shapes.cpp`), store nothing, never overlap, leave gaps;
  the three-member accumulation minimum is a signed constant; THE TAP'S FLOOR is built (`:947`).
  The held stop is derived by `chartHeldStops` (plant, else the covering grip's stop; an open
  string holds nothing) and lives in `ChartResolutions::held_stops` (`chart_legato.h:332`), read
  OUT of the postures. `NoteAttack::None`, the `N` verb, the stored `held` field and the
  span-claim machinery are all gone. Spans are not selectable: `chartSpanRevealed`
  (`chart_reveal.h:115`) has three grounds — lane reveal, a selected member note, the caret —
  and no selected-span ground; no rail geometry is exported (`tabShapeRailBounds` does not exist).
- **No template, dictionary, zone or fingering storage exists anywhere.** The GP parser reads no
  fingering and no chord diagrams (`gp_score.h`), so nothing could write a finger today.
- **Measurement.** The ground-truth dataset (16 shards, 4,555 arrangements) lives in local
  scratch outside every repo, per the corpus firewall; no C++ reads it. The Python harness
  (`final.py`, `combined.py` and the dataset's consistency sweeps) is committed in the importer
  repo under `custom-song-importer/tools/fhp-harness/` since 2026-10-02 and finds the dataset
  through `ROCKHERO_FHP_DATASET_DIR`; run from there it reproduces the 88.03% headline (verified
  the same day). The GP corpus census (`test_corpus_census.cpp`, hidden
  `[.local-corpus]`, `ROCKHERO_GP_CORPUS_DIR`) carries the travel-aware pinned-finger counter,
  the hand-coupling gate counters and the unfretted-arrival counters; three fret-hand-window rows
  have drifted below their pins since 2026-09-24 (`watch-items.md`). The sighted offender
  worklist and watch entries J and K are the falsification ground truth.
- **Sections start only on measure downbeats** by validator (`song_section_rules.cpp:54`), so a
  section start is always a bar line.

## 7. Phases

Each phase is built, committed and sighted on its own. Gates are named where a Q blocks.

### Phase 0 — stored-stream hygiene — DONE

The validator refuses equal positions ("must be strictly ascending", `chart_rules.cpp:213`);
`chart.h` / `file-formats.md` say "strictly ascending and unique by position"; the
duplicate-position case is in `test_chart.cpp`. The reader-refusal clause was deliberately NOT
built (the stopgap's Phase 0 deviation: it was the order rule stated twice). 60-D6's full form,
the funnel refusing a marker at an occupied front, is built too (`handChordTarget` restates
instead of inserting; a move onto a held start is refused at the commit).

### Phase 1 — the derivation core (gated on 60-Q1; needs nothing else)

**What it is.** One pure function in `rock-hero-common/core`, beside the width fold it replaces
(`chart_fret_hand.h`): `deriveFretHandPositions(notes, plants, markers, tempo_map)` → the
derived hand events, each a `GridPosition` front with a `FretWindow` (the type exists:
`chart.h`). Inputs are exactly the one-arrow inputs: the note stream, the plants column
(`chartPlantedStops` — rule 12), the authored markers (forced boundaries; a stated `fret` pins
the floor, a stated `end_fret` the top — §9 item 4), and the tempo map for bar lines (rule 3).
**No sections input**: a section start is a downbeat by validator, so the bar-line trigger
subsumes it; this also answers the former Q1 agenda question about unlabelled sections and
retires Q2's `section_breaks` parameter (§9 items 1 and 2).

**Why `common/core` and not the importer.** The game projects charts through the same
`makeChartViewState`; once the stream derives at read (Phase 2) both products must derive it in
one place, and the importer's call site (`gp_chart_builder.cpp:3821`) is just the first caller. In
this phase the importer still calls it to AUTHOR the stored stream — every derived event stored as
a marker with its fret — so Phase 1 lands and is measured with no format change.

**What it deletes.** The walk's body: the minimal-shift drag core, the boundary/rest re-anchor
machinery and `g_fhp_phrase_rest_seconds`, the pinned-finger union (rule 6's suppression subsumes
it), the held-hull slide reshape (slide keyframes become ordinary coverage events at their
destinations). `deriveHeldFretRanges` / `deriveFretHandWidths` fold into the derivation's rule 8
(the window IS the run hull from the pinned or derived floor), and `defaultedPlacement`'s stop
fold goes with them in Phase 2 (an inserted bare marker gets the derived floor for free). Net
code deletion. The-project-lifecycle rules 5–9 (`docs/developer/the-project-lifecycle.md:293-334`)
are rewritten to the three-sentence charter law of the design record.

**Acceptance.** (a) Rules 12 and 13 re-measured in the Python harness BEFORE the port is
written — Q1 signed conditional on this (§9 item 1); both populations are small and the
expectation is no headline change, but a headline drop reopens the item. (b) The C++ port scored
against the same dataset through a hidden-tag test (`[.local-fhp-dataset]`, path from
`ROCKHERO_FHP_DATASET_DIR`, the variable the Python harness already reads, exactly the census
rig's pattern)
reporting coverage and exact agreement — §9 item 3; without it nothing proves the port is the
measured algorithm. (c) Forced-relink test run; the census re-run with the three drifted
fret-hand-window rows and the pinned-window count re-pinned (expect the pinned count to collapse
toward the corpus's ~2-per-400k residue); the offender worklist re-measured; user sightings of the
worst prior offenders. (d) The seam-vs-shift agreement measure PRODUCED here for Phase 6.
**Must land before Phase 6** — building the coupling against the old walk would clip wrongly
everywhere.

### Phase 2 — the derived stream (gated on 60-Q4 and §9 item 4; needs Phase 1)

The marker's `fret` becomes OPTIONAL; the record is then `{position, fret?, endFret?}` — a forced
statement boundary carrying an optional direct window statement — under the key 60-H2 settles
(§9 item 6; one format change, not two). The derived events live INSIDE `ChartResolutions`
(`chart_legato.h:332`) as one more vector beside `shapes` and `held_stops`, derived by the one
resolutions pass, which gains the markers as an input beside the notes (`chartResolutions(notes,
markers, tempo_map)` — about fifteen callers, most of them tests). No second cache: the keyboard
plan's `ChartResolutions` name already IS this bundle, and the row, the lane and the highway all
read it through the projection. The importer stops storing a stream: it authors nothing (the
zero-authored-input invariant is the import), so `fhps` on a fresh import is empty and every
position is derived. `FhpViewState` keeps its shape; `end_authored` generalizes to "stated"
(fret, end, or both) so the chip prints a statement apart from a derived window.

What this deletes: the importer's placement authoring and its first-entry retime;
`defaultedPlacement` and the stop fold behind it; the width fold (Phase 1). What it adds: the
optional fret in the record, reader and writer; the resolutions input. Nothing user-facing is
lost — the stopgap's verbs keep working unchanged, their source changes: `Ctrl+H` inserts a bare
marker (a boundary; the derivation supplies the floor), the digit pins the floor, `Alt`+digit the
top, `Alt+Delete` clears the top, `Delete` the marker. The sighting reels stop authoring `fhps`.
**Converter note:** `custom-song-importer/tools/custom-song-converter.py` (outside this repo)
still writes the dropped `width` key on every placement (`:1202-1210`) and must stop; after this
phase it should write a marker only where the source's anchor is a deliberate statement, or
nothing.

Not here: the free-note finger (no writer exists — the GP parser reads none — so law 8 forbids
the field until Phase 5's import wiring) and the open-stretch window statement as a separate
record (subsumed: a marker with a stated fret over silence IS that statement, §9 item 4).

### Phase 3 — the hand marker and the hand row — REMAINDER (needs Phase 2)

Built by the stopgap: the record, the chords, the funnel, the row, the selection kind, the focus
column, walk / reach / Tab / jump, chip and pinned-chip selection, the outline, Delete, the
grid-quantum move, fret entry, the end fret, the reveal of an acted-on selection. What remains:

1. **The row's objects become the derived hand events** (60-H3): `markerStarts(MarkerRow::Hand)`
   and `markerSelectionAt` read `ChartResolutions`' events instead of the stored stream, so a
   fresh import with no markers still has a row. Identity stays the front position
   (`FretHandPositionSelection {position}` survives; its name is re-asked under H2). Pinned events
   are the ones a marker stands at.
2. **The authored tell** (law 7): a mark on the chip (or the event) where a marker stands;
   absence is the derived tell. Strength is the sighting's call.
3. **Rail selection and the span's selected ground**: a click on a span's rails selects its hand
   event (`tabShapeRailBounds` exported from the paint core beside `tabFhpChipBounds`);
   `chartSpanRevealed` gains a fourth ground, the selected event, so a selected span draws to its
   musical close; the game-shared paint core stays selection-free.
4. **The boundary gestures, as ruled (§9 item 8)**: `Alt+←/→` on an event WITH a marker moves
   the marker one placement quantum (built — the marker owns the boundary and the derivation
   answers), refused with the refusal flash at a duplicate front or off the chart's start; a
   DERIVED span front has NO move verb — the signed content-editing front move (tail rewriting at
   the boundary populations) is STRUCK, and a derived span changes only through the sound (the
   tails' own verb) or a marker. The END gesture (the tail keys move the co-terminating end
   through the content, admitting a note only where it FITS — the extend law of
   `first-releasable-editor.md`, signed as §9 item 9 and rewritten without `held`: a note fits
   where it sounds on a string the grip leaves free, restates the grip's stop, or sounds above the
   stop the grip holds on its string — never an open string, a fret-hand harmonic over a gripped
   stop, or through the travelled range; a swallowed note's held stop is then what
   `chartHeldStops` already derives from the covering grip, so no per-note field exists or is
   needed) is bulk tail editing, not a boundary authority, and stays an OPEN question for this
   phase's session: build it, or let the selected members' own tail keys serve. If built: hard-stop
   at a contradiction or the founding minimum with the loud refusal style, one undo entry per
   gesture through `commitChartGestureStep`. `Delete` on a DERIVED span waits for Phase 4's zone.
5. **Riding the seam**: publish the OPENING SLOT on `ChartShape` (Q-D) so the strike-less census
   row is exact — NEEDS VERIFICATION whether `bracket_position` / `landing_opened`
   (`chart_shapes.h`) already answer it.
6. **"Drop markers that state nothing"** (§9 item 5): a verb over the current format that
   removes every marker whose removal leaves the derived hand events AND the spans unchanged —
   deriving with and without it is the test, so the verb lives beside the derivation, never in a
   script. Useful after any charting session; also the pruning half of the one-off conversion of a
   pre-Phase-2 package (the throwaway script outside the product renames the key and writes each
   old placement as a fully stated marker; this verb then keeps only the ones that carry
   information). One undo entry; refuses nothing.

### Phase 4 — span-free zones and the settlement census (needs Phase 3)

Plan 61's Phase 2, unchanged in substance: the zone record (location-anchored range; one kind
with polarity vs two kinds is the naming question — nothing is built, and the record's
`SpanFounding` discriminator is a planned name, not a symbol) and its derivation wall (no
accumulation-founded opening inside, spans close at its edge, statement openings immune);
`Delete` on a derived span AUTHORS a zone over its musical close; zone resize via the note-tail
keys, BLOCKED at an authored marker (the precedence law: derived yields to the zone, authored
blocks it, only explicit deletion changes an authored statement); zone ink editor-only (red
leaning, EditorTheme roles); a marker inside a zone refused at authoring; the background sighting
triplet judged here. The settlement edge (the 2-note statement-founded bracket of the sighting
reel's measure 3) was CLAIM-founded, and claims are gone, so it is most likely moot — re-verify
on the regenerated reel (`span-marker-redesign.md`, "THE SIGHTING FIXTURE NEEDS REBUILDING": the
reel's `"attack": "none"` record no longer reads and claims cannot replace it). The ≥3 minimum is
already signed; what lands is the census re-sign of the accumulation checklist's section G and
the FHP counter rows left unsigned for this bundle.

### Phase 5 — templates, dictionary, editor and fingering coupling (gated on 60-Q3, 60-Q5)

**Prerequisite, unlisted until now:** the GP parser carries no fingering and no chord diagrams
(`gp_score.h`), so the import wiring this phase assumes ("GP chord-diagram fingering assembles
into template entries") needs the parser extended first — its own step, sized before the phase.

Plan 61's Phase 3 and the fingering half of the former plan 60 Phase 3, one arc: the template
save format and the template editor (the home of span-wide fret editing, and the only place a
stop NOTHING sounds can be stated since `NoteAttack::None` and the stored `held` left — the two
lapsed figures of `span-marker-redesign.md` "THE GAP WIDENED" return here); markers reference
entries in the song-scoped dictionary (chord and arpeggio spans share ONE list; the dictionary's
stamp model is superseded by its own 2026-09-05 section); save WARNS on template-less spans with
EXPORT blocked until resolved and a standing unresolved count in the chrome; the save-walk
resolver with the grip-matching suggestion sort (never an assignment) — its apply authors the
reference on the marker at the span's front, which is also what gives a span a durable identity;
the invalidation rule (contradicted-tell vs reference removal — the record flags the user's
confirming word as owed); the purge-on-save question; the free-note finger, landing WITH its
writer; the template coupling per the scoping law (the fingering pins the run its marker heads,
unsounded stated stops join that run's coverage, contradiction is a per-onset test that drops the
reference to notes-only fallback, references over silence feed nothing, no span extent
consulted); a template that reintroduces a stop nothing sounds brings its own justification rule
(LAW II's machinery was deleted as dead, not parked); the derived-default suggestion ships only
if 60-Q5 re-confirms. **The sub-4 width truth amendment** (60-Q1(b)) is partly DISCHARGED by the
authored end fret: a stated window is exactly `[fret, end_fret]`, however narrow, built and
sighted 2026-09-29 on a 3-fret window, so the "does a sub-4 window read oddly" gate has an
answer for the authored case; what this phase adds is the fingering-DERIVED form (both ends
pinned by a template's fingers), which inherits that sighting. The width-consumers audit (highway
window drawer, camera framing, census counters) is largely done with the end fret; re-verify the
camera framing and the census counters only.

### Phase 6 — the let-ring hand coupling (closes the sequence; gated on the census measure)

Derived spans clip at any emitted position shift not carried by a slide. Readiness gate: the
census seam-vs-shift agreement measure produced by Phase 1's acceptance; rule 6 concentrates the
clips where a hand demonstrably moved, which is what makes the coupling honest. Structural
precondition (law 2): by this phase the position derivation reads plants, never `held_stops`,
or the coupling closes a cycle. The watch item on same-voice drones under co-struck melodies
(rescoped 2026-09-01) is re-read here.

## 8. The hand row — keyboard and pointer

Absorbed from the keyboard-focus-rows plan's steps 4b and 4c on 2026-09-15, rewritten for the one
object, and BUILT by the stopgap over the stored stream (everything below is live unless marked
Phase 3). It reuses the marker-row model wholesale: every marker row walks as
`MarkerFocusRow{row}`, one holder function, one landing, one selection mapping, one jump action.

**The row and its objects (60-H3).** One row between the time signature and the strings. Its
objects are today the stored placements and, from Phase 3, the derived **hand events** of §2 read
from `ChartResolutions`, some of them pinned by authored markers. Identity is by front position.
Because every event carries a window and windows tile the song (the first owns the lead-in), the
row always has a holder once any event exists — the "gap" of the former span row applies only to
the SHAPE half, never to the row: `markerHolderAt(MarkerRow, GridPosition)` returns nothing only
for an empty stream, as for tempo and meter. The shape half keeps the half-open rule: an event's
SHAPE holds a position only while position < its musical close, so an abutting successor owns its
seam; the position half holds until the next event.

**The selection kind.** `FretHandPositionSelection {position}` beside the tempo and
time-signature kinds, keyed by front (name re-asked under 60-H2). Cursor-coupled; `Delete`,
`Alt+←/→`, the digit planes and `Alt+Delete` act on it; `Esc` releases; released when the front
names nothing after undo, arrangement switch and seek (60-D7).

**The focus column.** `focusColumn()` = the armed caret's position, else the paused cursor's; the
holder is computed at that column BEFORE the caret is demoted.

**Walk, reach, Tab, jump.** `↑` from the top string and `↓` from the time signature land on the
row's holder; `Ctrl+↑/↓` reach it as its own group; `Tab`/`Shift+Tab` step to the adjacent front;
`Ctrl+Shift+H` jumps and is silent with no events. Landing from the lead-in selects the first
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

**Verbs on the row.** `Ctrl+H` at the cursor authors or restates the marker; `Delete` removes
an authored marker, authors a zone over a derived span (Phase 4), and is inert on a derived event
with no shape (a derived position is not directly deletable, by law 7); `Alt+←/→` moves the
marker and is inert on a derived front (§9 item 8); the tail keys move the end only if Phase 3's
open END question builds them; digits
pin the floor, `Alt`+digits the top, `Alt+Delete` clears the top; `Enter` opens the template
picker on an event with a shape (Phase 5); `Ctrl+R` is inert until an event has a name to rename
(the entry's name is the dictionary's).

**Tests still owed** (the stopgap's cover the built verbs): the row walking derived events on a
fixture chart with no markers; a marker pinning an event the derivation also fronts (no
duplicate); the authored-tell pixels; pointer selection from rails; `chartSpanRevealed`'s selected
ground; `test_tab_paint_core.cpp` exported rail bounds matching the drawn rails; `Alt+←/→`
inert on a derived front; the end move's fit test at each refusal, if built; "drop markers that
state nothing" keeping exactly the markers whose removal changes an event or a span; a
strictly-ascending-fronts invariant over the derived events in `test_chart_shapes.cpp` and a
local census row for it.

**Sighting brief** (Phase 3): a GP import with dense position changes and short-gap chord charts,
with NO markers; Tab across derived fronts; the authored tell beside a derived chip; a span
ending near the cursor; a landing successor abutting its predecessor; clicking rails; the marker
move's refusal flash at a duplicate front; the outline and rail-highlight strength.

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
   the harness and the headline holding — Phase 1's first task.** The user signs it as the
   starting point and expects a rework once common fingerings for known chords enter at Phase 5
   (item 10); that rework is a Phase 5 question, not a reopening of this gate.
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

### The hand rulings (RULED 2026-09-15; kept as the record of what was decided)

- **60-H1 — one object.** §2.
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
- **60-D9 — one lazy hand-stream cache.** Resolved by Phase 2: the events live inside
  `ChartResolutions`, no second cache.
- **60-D10 — `Cmd+H` is Hide on macOS.** Accepted as H's cost in 60-H2.
- **60-D11 — pointer selection: in.** Built for chips; rails are Phase 3.
- **60-D12 — the selected pin yields exactly as the ruler's does.** Built.

## 10. Sequencing

Revise the plan (done 2026-10-02) → G60 → Phase 1 → Phase 2 → Phase 3 remainder → Phase 4 →
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
file); the seam-vs-shift agreement measure produced (Phase 1) and consumed (Phase 6); the
sighting gates of Phases 3, 4 and 5 recorded; CI blind-spot reading pass over every touched hunk
(new variant alternatives → designated initializers, `-Wswitch-enum` on `MarkerRow`, optional
access in tests, the `-Wmissing-designated-field-initializers` sweep when `FretHandPosition`
gains an optional `fret`).

## 12. Docs to change as phases land

- **`docs/developer/the-project-lifecycle.md`** rules 5–9: rewritten to the charter law in
  Phase 1 (the 0.8 s rest and the minimal-shift wording go with the walk).
- **`docs/plans/in-progress/fhp-derivation-algorithm.md`**: its status line and "three record
  kinds" residue after G60; moves to `completed/` when Phase 2 lands.
- **`docs/plans/in-progress/hand-marker-stopgap.md`**: completed work — move to `completed/`.
- **`docs/plans/todo/fhp-corpus-derived-generation.md`**: superseded (pointer added
  2026-10-02); delete or move to `completed/` when Phase 1 lands.
- **`docs/plans/todo/span-marker-redesign.md`** and **`chord-dictionary.md`**: re-verify before
  Phase 3 and Phase 5; every "claim" reads as retired.
- **`docs/plans/in-progress/span-derivation-ground-up.md`**: rule 2's claim wording after the
  2026-09-29 retirement (the header was corrected 2026-10-02).
- **`keymap-matrix.md`**: the hand row's chords are Live; the span-free zone and template verbs
  go Live in Phases 4 and 5.
- **`docs/developer/`** command-id registry, `keyboard-input.md`, `the-editor-2d-views.md`: the
  derived events as the row's objects, the authored tell, the rail selection and the span reveal
  ground (Phase 3); `file-formats.md` and `chart.h`: the optional `fret` and the record's key
  (Phase 2), the template fields (Phase 5).
- **`marker-verb-grammar.md`**: the new-kind checklist ran once for the hand marker (done).
- **`docs/design/`**: none expected — the one-arrow order and the derived-over-authored rule are
  already recorded; confirm with the user before any rule changes.
- **`docs/tracking/backlog.md`**: the stale "ruler shape-label band" comments in
  `tab_paint_core.cpp`.
