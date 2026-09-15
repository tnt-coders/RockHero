# Plan 60 — The Hand: derived positions, hand markers and spans

## 1. Status

**TABLED (user, 2026-09-05), MERGED 2026-09-15.** This plan is the union of the former plan 60
(FHP derivation) and plan 61 (span-marker redesign), plus the two hand rows the keyboard-focus-rows
plan had queued as its steps 4b and 4c. It stays parked behind gate G60-RULINGS until after the
task-list cleanup and the editor-functionality push (low-hanging fruit → keybinds + minimum
required editing → bend authoring). The user wants a LAW-BY-LAW discussion session before signing
60-Q1; do not treat any Q as signable in passing. The former plan 60 was authored 2026-09-05
against `master @ 0d1b7009`; the merge was made against `master @ 26d57204`. Re-verify the
inventory in §6 against the code before execution.

Why one plan (user, 2026-09-15): the fret-hand position and the span were being planned, and were
about to be built, as two objects with two keyboard rows, two selection kinds, two chords and two
sets of marks — while the 2026-09-05 sequencing ruling already said the marker "is ONE shared
object, a forced statement boundary serving both machines, built once". Planning and building the
hand as one coherent design is cheaper than building two halves and merging them afterwards. The
ruling itself is §2.

The design records this plan executes, in the order a new reader should take them:

- `docs/plans/in-progress/fhp-derivation-algorithm.md` — the measured 11-rule algorithm, the
  tension resolutions, the tap-corrected evaluation regime, the sighting-gated width amendment,
  the no-duplicate stream guarantee, the profiles verdict, and the fingering / template / manual
  specification model. Source of truth for Phases 1, 2 and the fingering half of Phase 5.
- `docs/plans/todo/span-marker-redesign.md` — the founding principle, the location-only marker
  record, the template model, the resolution and invalidation laws, the settlement rider, the
  extent invariant, the signed span boundary gestures, and the span execution phases. Source of
  truth for Phases 3, 4 and the template half of Phase 5. Todo-tier: re-verify before building.
- `docs/plans/todo/chord-dictionary.md` — the grip library the markers reference.
- `docs/plans/in-progress/keyboard-focus-rows.md` — the marker-row keyboard model the hand row
  reuses (Phases 1–4a built and sighted; its 4b/4c bodies moved here as §8).

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
  position and, where a span fronts there, a shape. Every span front is a hand event with a
  shape; a position shift in a monophonic passage is a hand event without one. The position
  stream is the events' positions, coalesced where consecutive events share one; the span stream
  is the events that carry a shape, each closing at its musical close. Boundaries are changes and
  fronts are the only stored coordinate — the tone model's law again.
- **In the save file**: the hand marker record — a forced statement boundary at a grid position
  (the 2026-08-31 location-only ruling stands), later wearing the optional template reference
  (Phase 5) under the "no field before its writer exists" rule. Whether the open-stretch window
  statement folds in as another optional field is 60-H4.
- **In the derived layer**: a marker is a forced boundary in BOTH machines — the FHP run head and
  the span front coincide at it by shared authored input, never by one derivation feeding the
  other. The one-arrow order stands: notes + authored statements → positions → spans / rings.
- **On the surfaces**: the position chip and the span brackets stay what they are today (both
  derived); an authored marker additionally draws its tell in the editor 2D lane; a selected hand
  event outlines its chip and its rails. Nothing new is invented for the highway.
- **On the keyboard**: ONE hand row between the time signature and the strings (§8), one author
  chord and one jump chord (60-H2), one selection kind. `Ctrl+P` / `Ctrl+Shift+P` return to the
  free pool and command id `0x1517` is unassigned. The span record's original `Shift+S` chord
  stays withdrawn (2026-09-12).

## 3. Goal

Replace the FHP generator with the corpus-measured **11-rule figure-segmented floor tracker**
(88.03% exact agreement with 4,555 professionally reviewed arrangements vs ~70% for the shipped
walk; coverage 100% after the tap correction; churn at the authored rate); make the position
stream **derived at read** from the notes plus a three-kind authored opinion layer (marker template
references, free-note fingers, open-stretch window statements); land the **hand marker** as the
forced statement boundary both derivations honour, with its keyboard row, pointer selection and
the signed boundary gestures; land **span-free zones**, **grip templates** in the song-scoped
dictionary with the template editor; and close with the **let-ring hand coupling** that clips
derived spans at position shifts.

## 4. Non-goals

- **No profile UI** (60-Q2 records the measured recommendation; overrulable there, not here).
- **No heuristic finger derivation, ever** — position is objective (97% expert agreement),
  finger is not (67%); fingers are authored or absent.
- **No authored anticipation** — anchors state on-onset truth; the highway/camera LEAD (~0.8–1.2
  s, fixed ~300–450 ms minimum-jerk move animation) is a projection-layer transform (feeds the
  open camera-timing review), never early data.
- **No width overrides** — width = max(4, run hull) is law; the stored width field of the
  ground-truth corpus is ~98% default and carries no reach signal. (The sub-4 width TRUTH
  amendment of 60-Q1(b) is a derivation rule, not an override, and is sighting-gated in Phase 5.)
- **No direct span resize** — the extent invariant (user-confirmed 2026-09-01): a span's extent is
  strictly derived from its content; every want routes through tails, markers, zones or Delete.
- **No pointer AUTHORING gesture** for markers — chords only (the 2026-09-12/14 reservation);
  pointer SELECTION is in (60-D11).
- **Not W10's split-tail verb** — its kinship with the split marker is noted for W10's build.

## 5. Laws carried in, binding on every phase

Each is recorded in full in its design record; they are listed here so no phase can be built
without them in view.

1. **Sound founds. Claims attach. Markers define.** (span record, founding principle.) A statement
   exists only by something sounding; a claim joins but never constitutes; deliberate span
   authoring is an explicit marker record.
2. **One arrow.** notes + authored statements (free-note fingers, markers, template references) →
   position derivation → span / ring derivation. Never the reverse: spans consume position shifts.
3. **The zero-authored-input invariant** (user, 2026-09-05). Positions must derive sanely from a
   fresh import with NO manually authored records, and cleanly with NO fingering information
   anywhere — imports may carry none, and free-note fingering likely stays optional forever, so
   fingerless is the PRIMARY mode, not a degraded one. The invariant constrains ABSENCE, never
   INFLUENCE: authored statements and fingering are optional as inputs but AUTHORITATIVE when
   present — a stated finger pins its run's anchor outright (it is the override language, not a
   hint), and a mid-run finger statement incompatible with the standing window forces a
   re-derivation at its note like any other authored statement. What the invariant demands is
   that every rule has a fingerless form that stands alone — rule 7's floor default IS that form
   (correct alone 88.8% of the time corpus-wide), and a stated finger substitutes into it. This is
   the algorithm's own design premise: the 88.03% corpus score was measured with zero authored
   inputs and zero fingering, exactly the fresh-import scenario. No phase may add a rule that
   REQUIRES fingering to function.
4. **Statements are inputs, never output patches.** A marker bounds and pins the run it heads; a
   finger pins the anchor; a window statement stands over silence; every note edit re-derives
   coherently around standing statements. Nothing ever writes a derived output.
5. **Invalidity is unrepresentable by the derivation being total.** Every verb edits inputs
   (tails, holds, markers, zones), and the derivation derives something legal from every input
   state. Fronts are unique and strictly ascending — enforced as 60-D6 states.
6. **The extent invariant.** Spans are never resizable; the zone is the one resizable record
   because it has no content to derive from.
7. **Markers are visible and deletable; derived boundaries draw nothing extra.** Every authored
   marker draws a tell in the editor 2D lane; absence of the tell IS the derived tell. The highway
   draws derived results only.
8. **Format minimalism.** A marker stores its location and nothing else until a field's writer
   exists. Format changes in place; no migration path, no version bump.
9. **One gesture authority.** Every hand verb runs through `commitChartGestureStep` (one undo entry
   per gesture, retire-on-no-change) and every marker verb through the `commitMarkerModel` funnel
   (build on a copy, refuse whole, publish once).

## 6. Current-state inventory (verified 2026-09-05; re-verify before execution)

- `generateFretHandPositions` in `rock-hero-editor/core/src/project/gp_chart_builder.cpp`:
  greedy minimal-shift walk + section/0.8 s-rest re-anchoring (`g_fhp_phrase_rest_seconds`) +
  the pinned-finger window union + the held-hull slide reshape. Import-only; the result is stored
  in the chart (`fhps`) and drifts from the notes on every later edit.
- **The stored `fhps` stream accepts duplicates** (found 2026-09-15): the validator's ordering
  rule compares with `<`, not `<=` (`chart_rules.cpp`, the `InvalidFretHandPosition` arm), and the
  document reader pushes entries with no order or duplicate check, unlike the notes stream beside
  it. Every sibling marker kind (sections, tempo anchors, time signatures, tone regions, notes)
  already requires strictly ascending unique positions by validator. Phase 0 closes this.
- The census rig (`test_corpus_census.cpp`, hidden `[.local-corpus]` tag) carries the
  travel-aware pinned-finger counter, the hand-coupling gate counters (spans crossed by an FHP
  shift), and unfretted-arrival counters. Cross-check baseline partly stale.
- Ground truth: the sighted offender worklist and sightings registry (local scratch, outside the
  repo per the corpus firewall); watch entries J and K in `docs/tracking/watch-items.md`.
- Spans are derived at read (`chart_shapes.cpp`), store nothing, never overlap, leave gaps; the
  three-member accumulation minimum is a signed constant. `NoteAttack::None` and the silent-hold
  claims machinery are still in the tree.
- No fingering exists anywhere in the model; no marker, template or zone storage exists.
- The keyboard marker-row model is built through step 4a: `MarkerFocusRow{row}` walks every
  marker row, `markerHolderIndex` answers the holder, `landOnRow` lands, `FocusRowJump` jumps,
  `selectedMarker()` / `selectMarker()` are the one selection mapping, and the ruler's selected
  pin skips only the early handover (`pinYieldsToIncomingLabel`'s selected exemption).
- The tab lane's position chip: `tabFhpChipBounds` is the one geometry authority; the pin is
  chosen by the same `pinYieldsToIncomingLabel` helper as the ruler; the pinned chip is inert
  chrome; a press on a scrolling chip falls through to the top string because the chart hit model
  (`ChartHitTarget`) has no hand kind.

## 7. Phases

Each phase is built, committed and sighted on its own. Gates are named where a Q or H blocks.

### Phase 0 — stored-stream hygiene (ungated, small, any time before Phase 2)

Bring the stored `fhps` stream to parity with its five siblings, independent of everything else:
the validator refuses equal positions (`<=`, message "must be strictly ascending"), the document
reader refuses unsorted or duplicate input at parse exactly as the notes stream does, the `chart.h`
/ `chart_rules.h` / `file-formats.md` wording says "strictly ascending, unique by position", and a
duplicate-position case lands in `test_chart.cpp`. Note the importer mutates the first entry's
position in place after building the stream (moving it earlier); the validator must still run
after that step. Net: a few lines. This is 60-D6's interim form; Phase 3's funnel is its full form.

### Phase 1 — the derivation core (gated on 60-Q1; independent of everything else)

Replace the walk's body with the 11-rule pass: one O(n) sweep over onset groups with the bounded
run scan. Deletions: the minimal-shift drag core, the boundary/rest re-anchor machinery and
`g_fhp_phrase_rest_seconds`, the pinned-finger union (suppression subsumes it), the held-hull slide
reshape (sub-4 widths disappear; slide keyframes become ordinary coverage events at their
destinations). Net code deletion. Acceptance: forced-relink test run; the census re-run (expect
the pinned-window count to collapse toward the corpus's ~2-per-400k residue); the offender
worklist re-measured with the walk order re-ranked; user sightings of the worst prior offenders.
**Must land before Phase 6** — the coupling clips spans at position shifts, and building it
against the old walk would clip wrongly everywhere.

### Phase 2 — the derived stream, storage only (gated on 60-Q4; needs Phase 1)

`fhps` leaves the format; the stream derives at read (editor derived-index pattern; O(n), cheap)
through one lazy hand-stream cache behind a const accessor, keyed on the arrangement and chart
revision (60-D9; name open — `ChartResolutions`, the name the keyboard plan proposed, is already
the legato resolution result in `chart_legato.h`). It must check its own freshness: releases run
between a revision bump and the next view push, so fronts read back out of the published view
state would be stale. The same cache serves §8's hand row and, as an optional follow-on, the lane
projections and the highway, removing two whole-song derivation passes per revision. The two
template-independent storage records land — the **free-note finger** (anchor evidence via
`floor − (finger − 1)`, written by GP note-level fingering import) and the **open-stretch window
statement** (record shape only; range-clamped) — but NO authoring verbs ship here. Nothing
user-facing is lost: no FHP authoring surface exists today, the sighting reels stop needing to
author `fhps`, and the external converter migrates in its own window.

### Phase 3 — the hand marker and the hand row (needs Phase 2; H2 and H3 ruled)

The marker half of the former plan 60 Phase 3 and plan 61 Phase 1, plus the keyboard rows:

1. **The record.** Grid position only, under the key 60-H2 settles (the 2026-08-31 ruling chose
   `"span"` for a span-only record; the merged object re-asks the name). A marker placed where a
   derived event already fronts PINS it (the forced-boundary semantic covers it). Deleting a
   marker reflows the derivation.
2. **The chords.** `Ctrl`+letter authors or restates at the CURSOR (the armed caret, else the
   paused cursor; never the selection; inert while playing and with no song — Phase 3 grammar of
   the keyboard plan); `Ctrl+Shift`+letter jumps onto the hand row (`0x1516`). Both through the
   `commitMarkerModel` funnel, which REFUSES a marker at an occupied front before the record
   exists (60-D6).
3. **The hand row** — §8 in full: the row, the selection kind, holder rule, pointer selection,
   the pin rule, view state, marks, reveal.
4. **The signed boundary gestures** (span record, "The span boundary gestures", SIGNED
   2026-09-05): with a hand event selected, `Alt+←/→` moves the FRONT — the marker where the
   boundary is authored, else the content (tails rewritten at the boundary populations only);
   the note-tail keys move the END through the content; both hard-stop at a contradiction or the
   founding minimum with the loud refusal style; `Delete` removes an authored marker (the boundary
   falls back to derivation). `Delete` on a DERIVED span waits for Phase 4's zone.
5. **`NoteAttack::None` rips out entirely** (user ruled — no half-state): the N verb, the attack
   value, the silent-hold claims arm and the sweep's note half leave together, ACCEPTING the named
   gap — a span with a bracket that never sounds is unauthorable until Phase 5's templates. Loaded
   files' existing `None` records drop on load WITH A LOAD NOTICE naming the marker system.
6. **Riding the seam**: publish the OPENING SLOT on `ChartShape` (Q-D) so the strike-less census
   row is exact.

### Phase 4 — span-free zones and the settlement census (needs Phase 3)

Plan 61's Phase 2, unchanged in substance: the zone record (location-anchored range; one kind with
polarity vs two kinds is the naming question) and its derivation wall (no accumulation-founded
opening inside, spans close at its edge, statement openings immune); `Delete` on a derived span
AUTHORS a zone over its musical close; zone resize via the note-tail keys, BLOCKED at an authored
marker (the precedence law: derived yields to the zone, authored blocks it, only explicit deletion
changes an authored statement); zone ink editor-only (red leaning, EditorTheme roles); a marker
inside a zone refused at authoring; the background sighting triplet judged here. The settlement
edge (the 2-note statement-founded bracket the sighting reel's measure 3 keeps ON PURPOSE) is
decided here — one rule, not both. The ≥3 minimum is already signed; what lands is the census
re-sign of the accumulation checklist's section G.

### Phase 5 — templates, dictionary, editor and fingering coupling (gated on 60-Q3, 60-Q5)

Plan 61's Phase 3 and the fingering half of the former plan 60 Phase 3, one arc: the template save
format and the template editor (the home of span-wide fret editing); markers reference entries in
the song-scoped dictionary (chord and arpeggio spans share ONE list); save WARNS on template-less
spans with EXPORT blocked until resolved and a standing unresolved count in the chrome; the
save-walk resolver with the grip-matching suggestion sort (never an assignment) — its apply
authors a marker referencing the entry at the span's front, which is also what gives a span a
durable identity; the invalidation rule (contradicted-tell vs reference removal — the record
flags the user's confirming word as owed); the purge-on-save question; this plan's FHP verbs —
finger statements over fretted material, window statements over open/silent stretches (refused
mid-ring) — consumed as derivation inputs that recalculate around, never output patches; the
template coupling per the scoping law (the fingering pins the run its marker heads, unsounded
stated stops join that run's coverage, contradiction is a per-onset test that drops the reference
to notes-only fallback, references over silence feed nothing, no span extent consulted); GP
chord-diagram fingering assembles into template entries at import, impossible hands dropped with
a report; the derived-default suggestion ships only if 60-Q5 re-confirms. **The sub-4 width truth
amendment first activates here and is SIGHTING-GATED** at that activation (a compact GMaj7-class
grip on a chart with authored fingering; keeping the 4-fret display minimum is an explicitly
legitimate outcome, then standing as a sighted display ruling of our own), and **the
width-consumers audit** (highway window drawer, camera framing, census counters) moves to this
phase with it.

### Phase 6 — the let-ring hand coupling (closes the sequence; gated on the census measure)

Derived spans clip at any emitted position shift not carried by a slide. Readiness gate: the
census seam-vs-shift agreement measure produced by Phase 1's acceptance; rule 6 concentrates the
clips where a hand demonstrably moved, which is what makes the coupling honest. The watch item on
same-voice drones under co-struck melodies (rescoped 2026-09-01) is re-read here.

## 8. The hand row — keyboard and pointer

Absorbed from the keyboard-focus-rows plan's steps 4b and 4c on 2026-09-15, rewritten for the one
object. It reuses the built marker-row model wholesale: every marker row walks as
`MarkerFocusRow{row}`, one holder function, one landing, one selection mapping, one jump action.

**The row and its objects (60-H3).** One row between the time signature and the strings. Its
objects are the derived **hand events** of §2, read from the `ChartResolutions` cache, some of them
pinned by authored markers. Identity is by front position. Because every event carries a position
and positions tile the song (the first owns the lead-in), the row always has a holder once any
event exists — the "gap" of the former span row applies only to the SHAPE half, never to the row.
This deletes the former 4c's stack-membership trick: `markerHolderAt(MarkerRow, GridPosition)`
returns nothing only for an empty stream, as for tempo and meter. The shape half keeps the
half-open rule: an event's SHAPE holds a position only while position < its musical close, so an
abutting successor owns its seam; the position half holds until the next event. The new
`MarkerRow` alternative is answered in both switches over the enum (`markerStarts`, reading the
cache bound once and guarded for the CI optional-access check, and `markerSelectionAt`) plus a
`selectedMarker` arm; `focusRowStack` lists it between the time signature and the strings.

**The selection kind.** One alternative beside the tempo and time-signature kinds, keyed by front
(name: 60-H2). Cursor-coupled for free; every dispatch ladder falls through correctly until
Phase 3's verbs give it `Delete` and `Alt+←/→` arms; `Esc` releases; left out of
`selection_present` until a verb exists. Released when the front names nothing after undo,
arrangement switch and seek (60-D7) — pinned by a test that the settle inside the select cannot
move the front it just named.

**The focus column.** `focusColumn()` = the armed caret's position, else the paused cursor's; the
holder is computed at that column BEFORE the caret is demoted. Load-bearing because an armed caret
does not move the transport.

**Walk, reach, Tab, jump.** `↑` from the top string and `↓` from the time signature land on the
row's holder; `Ctrl+↑/↓` reach it as its own group (`sameReachGroup` already gives each marker
row its own; the `CaretJumpSurfaceAbove/Below` Doxygen names the new row); `Tab`/`Shift+Tab` step
to the adjacent front through `StepToRowObject`'s generic marker branch, reading the cursor (the
marker-row Tab ruling); `Ctrl+Shift`+letter jumps and is silent with no events. Landing from the
lead-in selects the first event, whose only outlined mark may be its own scrolling chip
off-screen to the right, because the 2D pin shows nothing before the first placement — the same
accepted trade as a late first section.

**Pointer selection — 60-D11 RULED 2026-09-15: in, from day one.** A click on a scrolling
position chip or on a span's rails SELECTS the hand event and seeks nothing, exactly as a ruler
chip click does; the chart hit model gains a hand-event kind so the press no longer falls through
to the top string. The pinned chip: 60-H5.

**The pin — 60-D12 RULED 2026-09-15: exactly as the ruler's selected pin.** The pinned position
chip stands for the left edge's holder. When the chip stands for the selected event it skips only
the EARLY handover (the overlap-avoidance yield to the approaching chip); the moment the
successor's start crosses the left edge the successor takes the pin and the selected chip scrolls
away as an ordinary chip. Two chips are never both pinned. The exemption is spelled once, in one
named helper beside `pinYieldsToIncomingLabel` in `sticky_label.h`, used by the ruler and the tab
lane.

**Acting on an off-screen selection** reveals it (60-H6), a rule built on the shipped marker rows
first and inherited here. Today only a cursor MOVE glides the view: the column rule's seek before
a step or `Tab`, and the marker move. Rename, delete, and a step from a marker whose reach already
holds the cursor act blind.

**View state and marks.** `ChartEditViewState::selected_hand_event`, an index into the projected
events under the same contract as `selected_notes`. The TabView host draws the accent outline on
the selected event's scrolling chip, on the pinned chip when it is the selected one, and on the
span's rails (rail rectangles exported from the paint core as `tabShapeRailBounds` beside
`tabFhpChipBounds`); the game-shared paint core stays selection-free. `chartSpanRevealed` gains a
`selected` ground, the same shape as `chartNoteRevealed`, so a selected span draws out to its
musical close. The pin stores an index, re-derives when the selection changes. An authored marker's
tell (law 7) is a Phase 3 mark; the highlight's strength is the sighting's call.

**Verbs on the row** (Phase 3 and later): `Ctrl`+letter at the cursor authors or restates the
marker; `Delete` removes an authored marker, authors a zone over a derived span (Phase 4), and is
inert on a derived event with no shape (a derived position is not directly deletable, by law 7);
`Alt+←/→` moves the front; the tail keys move the end; `Enter` opens the template picker on an
event with a shape (Phase 5); `Ctrl+R` is inert until an event has a name to rename (the entry's
name is the dictionary's).

**Tests.** Marker-row walks, reach, Tab, releases and the lead-in on a fixture chart with hand
events; the focus-column case (caret inside a span, transport outside); the half-open close; a
duplicate-front refusal through the funnel; undo that removes or keeps the front; settle
invariance; the jump silent with no events; pointer selection from a chip and from rails, seeking
nothing; the pin exemption shared by ruler and lane; `test_tab_view.cpp` outline pixels on chip,
pinned chip and rails, the non-yielding-early selected pin, a selection-only refresh;
`test_tab_paint_core.cpp` exported rail bounds matching the drawn rails; a strictly-ascending-fronts
invariant over the derived events in `test_chart_shapes.cpp` and a local census row for it.
**Probe first:** whether
the shared test chart's measure 2 beat 1 dyad derives a span; if so, existing walk tests that arm
there move into a gap.

**Sighting brief.** A GP import with dense position changes and short-gap chord charts; the pinned
chip at a scroll edge with the selected event on either side of the handover; the lead-in; Tab
across fronts; a span ending near the cursor; a landing successor abutting its predecessor;
clicking chips and rails; the outline and rail-highlight strength.

## 9. Decision gate G60-RULINGS

All rulings are recorded with their evidence in the design records. Q1 blocks Phase 1, Q4 blocks
Phase 2, Q3+Q5 shape Phase 5, Q2 is product scope. The six hand rulings were all RULED 2026-09-15
(H2–H6 as recommended), so no H gates a phase; they are kept below as the record of what was
decided and why.

### The five derivation rulings (unchanged from the former plan 60)

- **60-Q1** — sign (or amend) the 11-rule algorithm. Two salvaged agenda questions ride the
  law-by-law session: does an UNLABELLED section mark segment under rule 3 (the bar-line trigger
  subsumes GP section marks, which sit at measure starts — confirm that suffices); and does a
  claimedStop join the coverage group (rule 1 counts fretted non-tap notes and rule 6 keys on
  written rings — neither mentions claims; a tap's held claim dies at the right-hand-onset filter,
  and a silent hold has no sustain for rule 6 to pin). Two deliberate considerations preceded the
  signing (user, 2026-09-05); the corrective measurement pass over all 4,555 arrangements is DONE
  (tap-audit report beside the dataset):
  (a) **tap contamination — CONFIRMED, corrected, headline stands.** The source game's charts
  cover taps by WIDENING the zone from the fretted floor (85% of above-hull taps covered by
  width, 0% by higher placement; median tap − anchor = +7), while RockHero deliberately excludes
  taps from the left-hand window. Corrected score: 88.04 / 94.82 (+0.010 — taps are 0.07% of the
  metric); coverage becomes exactly 100.000% (every uncovered note was a tap); rule supports
  essentially unchanged (default-4 98.25 → 98.35%). What WAS wrong: the width ≥ 7 tail (n=408)
  is 90.7% tapping content — right-hand bookkeeping, not roaming passages — so the dataset's
  width statistics are restated with tap-scope anchors excluded. Permanent harness rule: ONE note
  predicate (`fret > 0 and not tap`) consumed by both generation and scoring, and coverage
  reported as a first-class number.
  (b) **the sub-4 width TRUTH AMENDMENT — RULED IN (user, 2026-09-05), a deliberate deviation
  from source-game convention with the same standing as the tap exclusion.** The audit confirmed
  the premise to four nines (pinky-topped extent-≤3 grips authored width 4 at 99.93%, 100.00%
  across 18,173 open-position anchors; the authored zone is measurably "the grip plus one fret of
  headroom above the pinky") and initially recommended against the rule — a recommendation
  SUPERSEDED after two flaws were conceded: its width-agreement cost was measured against the
  corpus width column already ruled information-free (circular), and its renderer alternative
  would re-derive the same predicate in a second place against one-authority (under the derived
  stream, width is derived everywhere — the derivation is the only correct home). The amended
  rule 8: width has TWO meanings that this ruling separates — **capacity** (the segmentation
  reach test keeps max(4, …): a compact hand could still absorb a 4th fret, so runs, moves, churn
  and the let-ring coupling are untouched) and **statement** (the emitted window): fingerless
  mode emits max(4, run hull) unchanged per the invariant; where fingering pins BOTH ends of the
  hand — the window start via rule 7's index seat and finger 4 PLANTED at the hull top (a hovering
  pinky proves nothing) — the emitted width states the proven extent, below 4 when that is the
  truth. Stating width 4 against a proven 3-fret grip would be lying, and the charts state truth.
  Placement agreement is unaffected (measured exactly zero); the 3.18% fire rate is a
  GP-import-sparsity artifact — on RockHero-authored charts the rule fires wherever a charter
  states a grip. **SIGHTING-GATED before the build commitment (user, 2026-09-05)**: a compact
  voicing like a GMaj7 spans only 2 frets, and extent-2 grips are 13.4% of the pinky-topped
  population, so a width-2 window that reads oddly would read oddly often. The amendment cannot
  fire until fingering exists, so it is structurally a Phase 5 feature (Phases 1–2 are purely
  fingerless and untouched either way); the gate sits at its first activation, sighted on a chart
  or reel with authored compact grips, and the width-consumers audit moves to the same phase. An
  explicitly legitimate sighting outcome: keep the 4-fret display minimum — which would then
  stand as a SIGHTED display ruling of our own ("width 4 reads cleanly even for compact grips"),
  not an inherited convention.
- **60-Q2** — profiles: one default, two named parameters (`rest_break_seconds` off,
  `section_breaks` on), no profile UI, no artist presets (artist ICC ≤ 0.25; the knob inventory
  collapsed under measurement). Overrule here if the product wants the knobs surfaced anyway.
- **60-Q3** — the fingering model: grip templates `{name, string→finger, stops}` referenced from
  hand markers plus optional free-note fingers as the format's fingering carriers and the
  derivation's inputs (stacked fingerings first-class; `anchor = floor − (finger − 1)`).
- **60-Q4** — the derived stream: delete stored `fhps`; the authored residue is exactly the three
  record kinds.
- **60-Q5** — the derived-default suggestion for unreferenced grips (reverses the killed
  auto-match of 2026-08-31; reconciled via derived styling; needs explicit re-confirmation).

### The hand rulings (opened and RULED 2026-09-15)

- **60-H1 — one object. RULED 2026-09-15** (§2).
- **60-H2 — the letter, the id and the record's name. RULED 2026-09-15: `Ctrl+H` /
  `Ctrl+Shift+H`, id `0x1516`.** H reads as "what the hand holds" and its Shift claimant (pinch
  harmonic) is untouched. The cost is accepted: `Cmd+H` is Hide on macOS (60-D10), which bites
  only when the AUTHOR chord ships in Phase 3, where the macOS default is decided (under the pair
  law a different letter would move both chords together). The record key and the selection
  kind's name are re-asked with the naming expert at Phase 3 — the 2026-08-31 key `"span"` was
  chosen for a span-only record and no longer says what the record is.
- **60-H3 — the hand row's objects. RULED 2026-09-15: the derived hand events, with authored
  markers among them** (§8), not authored markers alone. The signed boundary gestures and
  Phase 4's `Delete`-authors-a-zone act on DERIVED spans, which need a selection; a fresh import
  has no markers and must still have a row.
- **60-H4 — the open-stretch window statement. RULED 2026-09-15: an optional field of the hand
  marker, valid only over silence, refused by the funnel elsewhere** (as a mid-ring statement is
  refused), rather than a fourth record kind. One marker kind, one row, one tell. The field
  lands in Phase 5 with its writer.
- **60-H5 — the pinned chip. RULED 2026-09-15: selectable, like the ruler's pin**, revisiting
  the inert-chrome ruling. The ruler's pin is an ordinary entry in its chip vector and a click
  selects its marker; the tab lane's pin was made inert because a press there would otherwise
  place a note under the legend, which selecting the event now answers better than inertness.
- **60-H6 — acting on an off-screen selection reveals it. RULED 2026-09-15**, and it is not this
  plan's to build: the user observed the same day that a section selected by click, scrolled out
  of view, then acted on by keyboard acts BLIND, so the rule is owed on the shipped marker rows
  first (`keyboard-focus-rows.md` step 4d, buildable now) and the hand row inherits it. What
  reveals today is only a cursor MOVE: the marker move and the column rule's seek glide, while
  rename, delete, and a walk from a marker whose reach already holds the cursor move nothing and
  so glide nothing. The rule: a keyboard verb that reads the selection ends with the selected
  marker (or the landing focus) in view; a click still never scrolls away from what was clicked,
  because a click creates the focus rather than acting under it.

### The row decisions carried from the keyboard plan (D6–D12; D13 stayed there)

- **60-D6 — duplicate fronts. RULED 2026-09-15: strictly invalid, enforced by the validator AND
  refused before the record exists.** "By construction" here means the app can never author one
  — the `commitMarkerModel` funnel refuses a marker at an occupied front (the section funnel's
  `songSectionCanStartAt` pattern) and the validator and reader refuse a file that carries one —
  not a keyed container type. The survey behind it: FHPs are the only kind accepting duplicates
  today; no kind is container-typed; a container change would touch 4 production files, 12 type
  mentions in the importer and ~93 test hits, and `std::flat_map`'s libc++ availability is
  unconfirmed. Phase 0 is the interim form on the stored stream.
- **60-D7 — release. RECOMMENDED stands:** "the front names nothing", not "any chart edit".
- **60-D8 — zero-length spans stay out of the row. RECOMMENDED stands;** the kind's name is H2.
- **60-D9 — the lazy hand-stream cache. RECOMMENDED stands;** lands in Phase 2 (its name is open:
  `ChartResolutions`, the keyboard plan's proposal, already names the legato resolution result).
- **60-D10 — `Cmd+H` is Hide on macOS. Accepted as H's cost in 60-H2;** the macOS default is
  decided when the author chord ships (Phase 3).
- **60-D11 — pointer selection. RULED 2026-09-15: in** (§8).
- **60-D12 — the selected pin. RULED 2026-09-15: yields exactly as the ruler's does** (§8).

## 10. Sequencing

The three-beat ruling (user, 2026-09-05) holds, re-numbered: Phases 0–2 first — the derivation
must stand alone under the zero-authored-input invariant and is the coupling's prerequisite while
needing nothing from the marker work. Phase 3 lands the one object and its row together — the
manual authoring of positions and spans is one arc because the marker is one shared object, built
once. Phases 4 and 5 follow in that order (zones gate the settlement; templates gate nothing).
Phase 6 closes the sequence.

**The interim stopgap** the roadmap mentions (materialize current stored FHPs and derived spans
into hand-editable markers, ripped out later) is superseded by this sequencing: the hand row and
the boundary gestures of Phase 3 are exactly that hand-editing surface, and nothing about them is
ripped out when the stream becomes derived — only their source changes. If a stopgap is still
wanted before Phase 1, build §8 over today's stored stream and derived shapes; but the row's
object identity (hand events) is cleanest once Phase 2 derives both halves from one cache, and the
recommendation is to wait.

Before all of it, the current focus: the editor-functionality push (keybinds + minimum required
editing, everything a tab has except positions and markers → bend authoring).

## 11. Final acceptance bundle

Build + tests green with the relink forced; census re-run with the position counters re-signed
(fold the stale-baseline re-sign into this pass); the offender worklist walked with the user until
the pins column is explained (FIXED-CLASS / OPINION / SIGHTED per file); the seam-vs-shift
agreement measure produced (Phase 1) and consumed (Phase 6); the sighting gates of Phases 3, 4 and
5 recorded; CI blind-spot reading pass over every touched hunk (new variant alternatives →
designated initializers, `-Wswitch-enum` on `MarkerRow`, optional access in tests).

## 12. Docs to change as phases land

- **`keyboard-focus-rows.md`**: its status header and Phase 4 record already point here.
- **`keymap-matrix.md`**: the Markers table's two reserved hand rows became one on 2026-09-15;
  the hand row's chords go Live in Phase 3 and the withdrawn `Shift+S` note is struck then.
- **`docs/developer/`** `CaretJumpSurfaceAbove/Below` Doxygen and the command-id registry: the
  new row, in Phase 3.
- **`marker-verb-grammar.md`**: the new-kind checklist runs once for the hand marker.
- **`docs/developer/keyboard-input.md`**: the focus-row paragraph and the keybind recipe.
- **`docs/developer/the-editor-2d-views.md`**: the position pin and its selection, the span reveal
  grounds, the marker tell, the `EditorSelection` list.
- **`file-formats.md`** and `chart.h`: Phase 0's wording; Phase 2's removal of `fhps`; Phase 3's
  marker record; Phase 5's template fields.
- **`docs/design/`**: none expected — the one-arrow order and the derived-over-authored rule are
  already recorded; confirm with the user before any rule changes.
- **`docs/tracking/backlog.md`**: the stale "ruler shape-label band" comments in
  `tab_paint_core.cpp`.
