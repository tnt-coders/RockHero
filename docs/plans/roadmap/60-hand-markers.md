# Plan 60 — The Hand: derived positions, hand markers and spans

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
  eleven. ~~Neither is measured yet.~~ SUPERSEDED 2026-10-03: both re-measured (`rules12_13.py`),
  88.09% exact for the 13 rules against 88.03% for the first eleven (§9 item 1).

The design records this plan executes, in the order a new reader should take them:

- `docs/plans/in-progress/fhp-derivation-algorithm.md` — the measured algorithm (rules 1–11
  measured, 12–13 ruled and re-measured 2026-10-03), the tension resolutions, the
  tap-corrected evaluation regime, the profiles verdict, and the fingering / template / manual
  specification model. Source of truth
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

### The position (`positions[]`, today `fhps`)

- **The record is today's, unchanged:** `{at, fret, endFret?}`. `fret` stays REQUIRED, so the bare
  marker, the optional fret and "an end fret without a fret" do not exist.
- **Positions derive at read** (60-Q4 stands); the list stores only what the charter authored, and
  a fresh import stores none.
- **An authored position stands until the chart contradicts it.** Its window is `[fret, endFret]`
  where the end is stated, else the stated fret with the width of the run it heads, never under
  four frets. It holds through every later run that fits inside that window and emits nothing; the
  derivation resumes at the first demand outside it. This answers the "scope of a stated fret"
  question Phase 2 carried: neither "the run it heads" nor "until the next marker".
- **A note entered at an authored position's own instant, outside its window, is REFUSED.** This
  reverses the 2026-09-25 stopgap ruling (law 4's last sentence) that such a stop "stays outside".
  A note later on is not refused; it ends the hold.
- **Chords:** `Ctrl+P` inserts or restates, `Ctrl+Shift+P` jumps to the row. The built `Ctrl+H`
  pair moves there.

### The hold (`holds[]`, new)

A stretch of the song with a start and an end, in one of two polarities. It replaces three things
this plan carried: the authored span marker, the span-free zone, and the bare boundary marker.

- **The record:** `{at, until, grip?}`, ascending and non-overlapping (`until` is PROPOSED as the
  end's word, pairing with `at`; the user asked for one in place of `start` / `end`). `grip`
  absent means ONE GRIP IS HELD, its content whatever the notes inside sound; `"none"` means
  NOTHING IS HELD; from Phase 5 it may instead be the id of a `grips` entry, which then states the
  content. One field, so "none
  plus a template" cannot be written. No fret is ever stored on a hold (the 2026-09-29 ruling
  stands: a held stop is never typed).
- **A held stretch takes every note inside it that fits, ringing or not.** That is how a muted or
  staccato arpeggio out of one shape is charted: the sound never proves that hold, so the
  derivation never asserts it, and the charter states it with its extent.
- **A conflicting note inside a held stretch is REFUSED,** by chart validation, with ONE fit test —
  the test that walls an edge (§9 item 9's, rewritten without `held`). Entering, re-fretting,
  moving and pasting are refused alike, and a file holding the state does not read.
- **A not-held stretch blocks arpeggio founding only.** Struck chords inside it stand as chord
  spans: a chord span is the minimum proven truth of a grip and can be neither shrunk nor deleted.
- **Derived yields, authored blocks.** No derived span overlaps a hold; a hold's edge takes notes
  from a derived neighbour and is refused at another hold.
- **Derived spans are unchanged:** they never bridge a gap, and their ends follow the note tails.

The verbs, and what each saves:

| Verb | Saved |
|---|---|
| `Ctrl+H` over selected notes | A held stretch, first onset to the last ring's end |
| Move either edge of a derived span | A held stretch at the new extent — the span is now authored |
| Delete a derived arpeggio span | A not-held stretch over it |
| Move an edge of a hold | That edge |
| Delete a hold | Nothing: the derivation takes the stretch back |

- **Edges move outward** to a conflicting note or another hold, and **inward** to a struck chord
  or the founding minimum, a HARD STOP (the 2026-09-05 dissolve ruling, for its own reason: removal
  is Delete, and two verbs do not produce one outcome).
- **Resizing never changes a tail.** A fretted note may therefore ring past its bracket's end, the
  picture the zone design already accepted.
- **The cost accepted:** once a span is authored, NEITHER edge follows later note edits.
- **A held stretch emptied of its notes stays,** drawn empty; only Delete removes a statement.
- **`Ctrl+Shift+H`** jumps to the hold under the caret; a hold is also selected by its brackets.

### The grip dictionary (`grips`, Phase 5)

Song-scoped; a hold's `grip` points into it. An entry states the stops, with optional fingers, and
an optional `chord` name. PROPOSED, not yet ruled: export requires `chord` on an entry of three or
more strings, while the file accepts its absence, on the plan's own save-warns / export-blocks
tiers. The entry's fields are Phase 5's design.

### Names and the format

- **Save-file keys:** `positions`, `holds`, `grips`. 60-H2's `hand` (§9 item 6) is dropped.
- **The timeline key `position` becomes `at`** on notes, positions, tempo anchors, sections and
  automation points, so that `positions` can mean the hand's. PROPOSED with the hold's `until`:
  `toneChanges[].start` becomes `at` as well, so every record says where it sits with one word and
  no `start` remains (the user first kept `start`, then asked to replace it given another end word).
  The code keeps `position` (`GridPosition`, `note.position`): `at` is a label, not a second noun.
  It lands as its own commit AHEAD of Phase 2, with the converter and the scoring harness in the
  importer repo and a rebuild of the scored corpus. OPEN: whether any project on disk must survive
  it (a throwaway key-rename script, else re-import).
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

### Still OPEN

- **How a not-held stretch draws.** The 2026-09-01 zone rulings carry (editor-only, its own span
  style, red-leaning, the authored start line). PROPOSED: its bracket breaks around each chord
  span inside it and is still one object. A sighting, with the UI design review.
- **A held stretch across a position shift.** Phase 6 clips a derived span at a shift; whether an
  authored hold is refused across one, or pins the position the way a fingering will, is ruled
  with Phase 6.
- **The fit test's exact clauses**, re-read against the span derivation before the hold is built.

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
- **Validation gains one rule:** a note at an authored position's own instant, outside its
  window, is refused.
- Unchanged: the events inside `ChartResolutions`, the pass order, the importer storing nothing,
  everything this phase deletes, the exit-ride and dip ruling, the converter note, the sighting.

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

### Phase 3 — the hand marker and the hand row — REMAINDER (needs Phase 2)

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
   and `markerSelectionAt` read `ChartResolutions`' events instead of the stored stream, so a
   fresh import with no markers still has a row. Identity stays the front position
   (`FretHandPositionSelection {position}` survives; its name is re-asked under H2). Pinned events
   are the ones a marker stands at. **How the events reach the controller (verified 2026-10-04):**
   `markerStarts` returns `GridPosition`s while the events' fronts are whole-note `Fraction`
   places, and the controller holds no `ChartResolutions` — it caches only the scene
   (`editor_controller.cpp:2027`). Either the scene carries the event fronts or the controller
   keeps the resolutions; this phase decides which, by the one-authority test.
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
   row is exact — NEEDS VERIFICATION whether ~~`bracket_position`~~ `bracket_place` (renamed;
   `std::optional<Fraction>`, `chart_shapes.h:284`, verified 2026-10-04) / `landing_opened` (still
   present) already answer it; `bracket_place` probably does.
6. **"Drop markers that state nothing"** (§9 item 5): a verb over the current format that
   removes every marker whose removal leaves the derived hand events AND the spans unchanged —
   deriving with and without it is the test, so the verb lives beside the derivation, never in a
   script. Useful after any charting session; also the pruning half of the one-off conversion of a
   pre-Phase-2 package (the throwaway script outside the product renames the key and writes each
   old placement as a fully stated marker; this verb then keeps only the ones that carry
   information). One undo entry; refuses nothing.

### Phase 4 — span-free zones and the settlement census (needs Phase 3)

**REVISED 2026-10-04 (§1a) — this phase builds THE HOLD.** The zone is the hold's not-held
polarity and is no longer a record of its own: one kind, `holds[] {at, until, grip?}`, answers the
"one kind with polarity vs two kinds" question below. What carries from the zone text: it blocks
accumulation-founded openings only, statement openings are immune, a derived span closes at its
edge, `Delete` on a derived span authors it, and its ink is editor-only. What changes: BOTH
polarities are resizable from either edge, "a marker inside a zone is refused" becomes "holds do
not overlap", and the held polarity (the charter's own span, with its refusal of a conflicting
note) is built here beside it. The census re-sign stands.

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
the FHP counter rows left unsigned for this bundle. (Verified 2026-10-04: nothing here has been
renamed, and the span record's "sweep STILL OPEN" is moot — the sweep is deleted.)

### Phase 5 — templates, dictionary, editor and fingering coupling (gated on 60-Q3, 60-Q5)

**REVISED 2026-10-04 (§1a).** The dictionary is saved as `grips`, its entries carry an optional
`chord` name, and the reference rides the HOLD (`holds[].grip`), not a hand marker: read "markers
reference entries" and "authors the reference on the marker at the span's front" below as the
hold's `grip`. A grip's stops are stated ONLY here, never on a hold. Everything else in this phase
stands, re-verified at its own session.

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
move's refusal flash at a duplicate front; the outline and rail-highlight strength. From Phase 2
on (added 2026-10-04): a time-signature edit MOVES THE DERIVED POSITIONS while the notes keep
their places — rule 3 reads bar lines, so this is correct, but it is sighted, never assumed.

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
   phrase-iterations (`rules12_13.py`: `bars + sects`); the port breaks at bar lines only (item
   1a: a section always starts on a downbeat, `song_section_rules.cpp:42-51`). **FINAL:** the
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

### Gates still OPEN, per phase (as of 2026-10-04)

REVISED the same day by §1a, which closes several entries of this table: Phase 2's
marker-semantics list and its selection-kind spelling (the names are ruled), Phase 3's END gesture,
and Phase 4's zone-kind question. §1a's own "Still OPEN" list adds the not-held bracket's drawing,
a hold across a position shift, and the fit test's clauses.

| Phase | Ruled | Still OPEN |
|---|---|---|
| 1 | 60-Q1 (item 1, condition discharged 2026-10-03), items 2 and 3, Q1–Q12 above | Nothing. CLOSED 2026-10-04 |
| 2 | 60-Q4 (item 5), items 4, 6 and 7 | The selection kind's spelling (the naming expert, item 6). The marker semantics Q9 deferred: whether a marker that changes nothing is still a row event (rule 11 against §2 and law 7); the scope of a stated fret (the run it heads, or until the next marker); where a bare marker over silence fronts; whether `endFret` without `fret` is legal. The fate of the exit ride and the scoop dips (Q11: drop, or `FhpViewState` ramps). Whether any hand-placed package exists that needs the conversion script (item 5) |
| 3 | Items 8 and 9 | The END gesture (OPEN by design: build it, or let the members' tail keys serve). The tell and outline strength (a sighting). Q-D, which `bracket_place` probably answers. How the derived events reach the controller (scene, or resolutions held) — a design call for the builder, not a user gate |
| 4 | none named | The zone record's kind: one kind with polarity, or two. The settlement edge (re-verify on a regenerated reel; likely moot). The background triplet (a sighting) |
| 5 | 60-Q5 (item 11) RULED NO, reopenable there | 60-Q3, the fingering model, and the GP parser's fingering scope (item 10); purge-on-save and the invalidation rule's confirming word (item 12) — all DEFERRED to the Phase 5 session |
| 6 | The measure's DEFINITION (Q12) | The measure's slide split and the offender worklist's walk, both deferred from Phase 1's close. The pass threshold on seam-vs-shift agreement (the user's). The let-ring extension inside the stored sustain, which rule 6 pins on and this phase clips (§9 Q3), ruled before this phase. The drone watch item, re-read here |

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
- **60-D9 — one lazy hand-stream cache.** Resolved by Phase 2: the events live inside
  `ChartResolutions`, no second cache.
- **60-D10 — `Cmd+H` is Hide on macOS.** Accepted as H's cost in 60-H2.
- **60-D11 — pointer selection: in.** Built for chips; rails are Phase 3.
- **60-D12 — the selected pin yields exactly as the ruler's does.** Built.

## 10. Sequencing

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
access in tests, the `-Wmissing-designated-field-initializers` sweep when `FretHandPosition`
gains an optional `fret`).

## 12. Docs to change as phases land

- **`docs/developer/the-project-lifecycle.md`** rules 5–9 (`:297-374`): rewritten to the charter
  law in Phase 1 (the 0.8 s rest and the minimal-shift wording go with the walk); rule 9's exit
  ride and rule 16's dips stay, flagged, until Phase 2 rules their home (§9 Q11).
- **`docs/plans/in-progress/fhp-derivation-algorithm.md`**: its status line and "three record
  kinds" residue after G60; moves to `completed/` when Phase 2 lands.
- **`docs/plans/in-progress/hand-marker-stopgap.md`**: completed work — move to `completed/`.
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
  ground (Phase 3); `file-formats.md` and `chart.h`: the optional `fret` and the record's key
  (Phase 2), the template fields (Phase 5).
- **`marker-verb-grammar.md`**: the new-kind checklist ran once for the hand marker (done).
- **`docs/design/`**: none expected — the one-arrow order and the derived-over-authored rule are
  already recorded; confirm with the user before any rule changes.
- ~~**`docs/tracking/backlog.md`**: the stale "ruler shape-label band" comments in
  `tab_paint_core.cpp`.~~ SUPERSEDED 2026-10-04: the comments are gone from `tab_paint_core.cpp`
  and the backlog carries no such entry; nothing to change.
