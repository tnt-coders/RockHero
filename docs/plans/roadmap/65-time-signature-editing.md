# Plan 65 — Time-Signature Editing

**Status:** BUILT 2026-10-04, awaiting the G65-TS sighting: Phase 1 (`e69c2f2f`, the transform,
plus `8605b9a3`, the `TimeSignature` value type), Phase 2 (`cf65eff2`, the verbs, the whole-song
snapshot and `commitGridModel`), Phase 3 (`1974fe54`, `Ctrl+/`, the prompt, the ruler rows and the
renumber flash). Open for the user: the sighting below; the UI strings ("Add or Set Time
Signature", "Add/Set Time Signature", "Time signature (e.g. 7/8)", "Set", the three menu rows);
the undo-memory finding (≈ 10 MB per meter entry at 50k notes, plan 64 F9). Split out of plan 41
on 2026-10-03, where it was Phase 6 (written
2026-07-06, last revised there 2026-10-02; git history keeps that text). First sequenced after plan
60; on 2026-10-04 the user moved it forward ("IF time signature edit is clear enough to implement
I would like you to proceed"). A readiness audit the same day (against `master @ 0ec22b40`)
re-verified *Current state*, completed the slice inventory, and raised two questions the user
ruled (Q3, Q4 below); every other gap it found is folded into the text. Runs on provisional
65-Q1 = A (asked as 41-Q1, user 2026-09-19); gate **G65-TS** (named G41-TS until 2026-10-03) closes
at this plan's sighting.

## Goal

A charter edits the song's time signatures inside the marker grammar every other marker kind
follows: insert a meter at a measure downbeat, restate its numerator and denominator, delete it,
with every piece of content keeping its time and the measures after the edit renumbering, in one
undo step per edit. Today a meter other than the default exists only if the song came from a GP
import.

## Non-goals

- No new song.json key and no chart format change: `TimeSignatureChange` already persists, and the
  reader's rules (measure >= 1, positive numerator, power-of-two denominator) stay the rules.
- No tempo repair: Scale Tempo, the Note Lock and the tempo lock are
  `docs/plans/roadmap/66-tempo-repair-tools.md`, which reuses this plan's transform and undo.
- No range-scoped signature edit and no paste across meters (plan 52 owns 52-Q2).

## Constraints

- (a) **Layering**: common never depends on editor or game code; Tracktion headers stay in
  rock-hero-common/audio implementation files.
- (b) **Public-header minimalism**: the transform and the edits stay `src/`-private until a
  consumer outside the library exists.
- (f) **Undo**: RockHero-owned; the meter commits through `commitMarkerModel`
  (`rock-hero-editor/core/src/controller/marker_model_commit.h`), with a whole-song snapshot (see
  Phase 2).
- (h) **Builds** go through `.agents/rockhero-build.ps1`; each phase runs only the checks its
  changes warrant. clang-tidy runs only when the user asks.
- **Performance**: a re-address touches every position in the song, so its cost is O(song) by
  nature. It runs once per COMMAND, never per pointer event, and its cost and the snapshot's
  memory are measured on relwithdebinfo against the album-scale findings
  (`docs/plans/roadmap/64-album-scale-performance.md`: ~150–250 ms per whole-song re-address at
  50k notes, a proposed ≤ 250 ms whole-song verb bound, 64-Q4, and its undo-memory finding F9)
  before the phase ships.

## Current state (verified 2026-10-03 against `master @ 51afa283`)

- **Model**: `TimeSignatureChange` and `TempoMap` in
  `rock-hero-common/core/include/rock_hero/common/core/timeline/tempo_map.h`; the shared rules are
  `tempo_map_rules.h` (plan 41 Phase 1a), which the package translates.
- **Anchors are sub-beat since 2026-10-03** (`3a37fabb`, `79defdba`): a `BeatAnchor` is
  `{GridPosition position; int64 milliseconds}`, standing at any tick-lattice position, its time a
  whole number of milliseconds; only the terminal must sit on a downbeat. So an anchor is content
  like any other here: it re-addresses on the whole-note axis and its time is untouched. The
  2026-07-06 text's on-beat anchor assumption is gone.
- **Whole-note axis**: `TempoMap::wholeNotePositionAt` and `advanceGridPositionByWholeNotes`
  (`grid_arithmetic.h`); the tick rounding is `nearestTick` (`tick_lattice.h`).
- **Engine refresh already covered**: `syncTempoDerivedAudio()` (`tempo_handlers.cpp:299`) runs
  in the funnel's shared tail (`finishMarkerCommit`, `marker_row_handlers.cpp:297`) and when an
  undo or redo completes (`editor_controller.cpp:1909`); it rebuilds the seconds-domain automation
  curves and the host tempo mirror whenever the session's map differs from the last one pushed. A
  meter edit changes the map, so it inherits the refresh with no engine work.
- **Snapshot**: `TempoMapSnapshot` (`rock-hero-editor/core/src/timeline/tempo_map_snapshot.h`)
  captures the map alone, which is right for anchor edits and wrong for a meter edit. No
  whole-song memento exists, and `Session` has no write path for a NON-current arrangement
  (`writeChart` and the `current*` accessors reach only the loaded one, `session.h:69-157`).
- **Re-reading under another meter list is pure already**: the `TempoMap` constructor does not
  validate (`tempo_map.cpp:70-77`) and every address query reads only the signature segments, so
  `TempoMap{new_signatures, old.anchors()}` answers `wholeNotePositionAt` /
  `gridPositionAtWholeNote` under the new list (never ask it for seconds). (Until `987b62cf` the
  second query was `beatAtWholeNotePosition`.)
  `advanceGridPositionByWholeNotes(map, GridPosition{}, w)` is the whole-note → token inverse.
  `reseatTerminalAnchor` does NOT exist yet.
- **Durations are note values, not positions**: `ChartNote::sustain` and `Keyframe::offset` are
  whole-note fractions since `987b62cf` (plan `whole-note-durations.md`), so a meter edit leaves
  them untouched. (Before that they counted beats, a denominator change re-meant them, and Phase 1
  re-expressed them.)
- **Sections must stand on a downbeat** (`song_section_rules.cpp:49-62`); tone regions, FHPs and
  automation points have no downbeat rule.
- **Already live**: the meter chip row and its selection (`TimeSignatureSelection`,
  `MarkerRow`), the `↑/↓` walk and `Tab` over it, and the `Ctrl+Shift+/` jump (`0x1513`).
  `Delete`, `Enter` and `Alt+←/→` on a meter chip are inert, silently.
- **Checklist**: the 8-step new-kind checklist is `docs/plans/in-progress/marker-verb-grammar.md`,
  "What a NEW marker kind must do"; plan 41's tempo anchor is the latest kind built through it.

## Decisions

Carried from plan 41 (they stand):

1. **A meter edit moves nothing.** Under Q1 = A every object keeps its whole-note position and
   so its time; only its measure address changes. (Plan 41's tempo edits are the contrast: there
   the grid is truth, user 2026-09-19, and content moves in time with its beat.)
2. **`Ctrl+/` reads the cursor, never the selection** (2026-09-12, re-ruled 2026-09-14). A meter
   standing on the downbeat of the measure the cursor is IN, read from its tick, is RESTATED,
   reopening the numerator/denominator prompt; otherwise one is INSERTED there. Inert while
   playing and with no song. `Ctrl+Shift+/` is the select chord. The click and context menu on the
   signature band are the pointer form of the same two verbs.
3. **One meter edit is one undo entry**, restored from a whole-song snapshot: coherent full-state
   restore over granular replay (constraint (f)).
4. **The re-address happens on the whole-note axis** (2026-09-23), which a denominator change
   does not bend. The 2026-10-04 audit showed that no residue arises (Phase 1, "Exact").
5. **Renumbering is shown, not silent**: the renumbered range flashes once after the edit; the
   ruler's labels derive from the map, so nothing else renumbers by hand.
6. **Validity reports through plan 42's rule set**, never a plan-local validator, if any residual
   issue is possible under the chosen policy.

## Open questions

- **Q1 — Time-signature content policy** (asked as 41-Q1). PROVISIONALLY A (user, 2026-09-19):
  preserve every object's global position through a pure re-addressing transform — time
  preserved, measure numbers shift. G65-TS closes at this plan's sighting. If the sighting picks B
  (keep literal `m:b` tokens; content shifts in time, destructive clamping) or C (forbid meter
  edits once content exists), replace Phase 1's transform with that policy and re-derive its
  tests; the verbs and UI survive every outcome.
- **Q2 — the meter's `Alt+←/→`. RULED 2026-10-03 (user): yes, by one measure.** The step is the
  entry's `measure` ±1 in the signature list (measures before it are unaffected by it, so m±1 is
  well defined in the result), then re-addressed through Phase 1 like any other meter edit.
  Refused: the measure-1 meter; a step onto or past the neighbouring change; a step to or past
  the terminal. A held run coalesces through `commitMarkerMove` from the run's origin.
  **SUPERSEDED by plan 67 (67-Q2 (b), built 2026-10-09):** the measure quantum is gone from the
  meter row — `Ctrl+/` and the ruler's Add read the BEAT the cursor is in, and `Alt+←/→` moves a
  meter by one beat of the meter before it; the refusals above stand, plus `ReignTooShort`.
- **Q3 — later meters. RULED 2026-10-04 (user): they keep their TIME.** An edit at measure m
  changes the length of every measure until the next change; that next change keeps its
  whole-note position (so its time) and takes whatever measure number that is under the new list.
  When the edited reign is not a whole number of new measures (16 beats re-metered to 3/4), the
  later change would land mid-measure, which no partial measure can express: the edit is
  REFUSED, logged naming the next meter. With no later change the reign runs to the terminal,
  which re-seats (Phase 1), so the edit is never refused for this reason.
  **SUPERSEDED by plan 67 (ruling B, built 2026-10-09):** later meters still keep their time,
  but the edit never refuses for a reign that does not divide — the remainder is ONE derived
  short measure, shown grayed and never stored — and the terminal is never re-seated (the song
  may end mid-measure). The one refusal left is a reign shorter than one of its own measures.
- **Q4 — a section left mid-measure. RULED 2026-10-04 (user): snap to its bar's downbeat.** A
  section whose preserved time is no longer a downbeat moves back to the downbeat of the measure
  it now falls in (a section is navigation and moves no sound); the edit is refused only when two
  sections snap onto one downbeat. This is the one slice that does not keep its time, so the
  round-trip property exempts a snapped section.
  **SUPERSEDED by plan 67 (ruling A, built 2026-10-09):** a section is a named instant anywhere
  on the grid; a meter edit keeps it exactly where it stands, and the property exempts nothing.
- **Build-session answers taken from the audit's recommendations (2026-10-04):**
  - *Measure 1:* a meter always stands there, so `Ctrl+/` in measure 1 always restates; Delete on
    it and `Alt+←` onto or from it are refused and logged, as the first tempo anchor's delete is.
  - *Redundant meters:* the snapshot's `normalize()` drops a change equal to its predecessor (the
    `coalesceToneRegions` law), so inserting the prevailing meter is `Unchanged` and records
    nothing. **Narrowed by plan 67:** only a repeat on one of its predecessor's downbeats is
    redundant; a repeat off that grid cuts a derived short bar and is kept (`53863575`).
  - *The prompt:* one text field ("7/8") parsed in the core, as the BPM text is; the rules refuse
    a bad denominator; the chord's target is nothing while playing, with no song, or when the
    cursor's measure downbeat fails `objectCanStartAt`.
  - *The cursor:* `selectMarker` dissolves the caret by re-reading its OLD token under the NEW
    map, which is different music. The verb shares the tempo verbs' `commitGridModel`, with time
    as truth: read the cursor place, commit, select the landed meter, move the cursor to the
    re-addressed place.
  - *The persisted caret:* closing WITHOUT saving after a meter edit persists a new-grid caret
    token against the old-grid package. Accepted (the restore lands on a valid slot or demotes);
    `i_editor_settings.h:50-53`'s "cannot go stale" claim is corrected and a watch item recorded.
  - *Undo memory:* the whole-song snapshot is built as decided and sized per plan 64 Phase 7. If
    it breaks the byte budget, the smaller exact shape is an entry of `{before map, after map}`
    replaying the pure transform each way, with the snapped sections and re-seated terminal
    restored from it. Raise that to the user then, not before.

## Dependencies

- **Plan 60** (sequencing only, lifted 2026-10-04): it is not finished, and its fingering and
  template records may add position- or slot-keyed data. Re-run the `rg "GridPosition|Fraction"`
  sweep at build time; whatever plan 60 adds later must join the transform and its per-slice test.
- **Plan 41** Phases 1–4 (built): the rules, the funnel's tempo-derived refresh, the marker-run
  coalescing.
- **Plan 66** builds on Phase 1's transform and Phase 2's snapshot, after or with this plan.
- **Plan 42**: content validity after a meter edit (decision 6).
- **Plan 52**: 52-Q2's clipboard position basis is decided with Q1's philosophy.
- **Plan 64**: the whole-song re-address and snapshot costs it measured bound Phases 1–2.

## Phased implementation

Every new file follows the placement procedure; comments follow
docs/design/documentation-conventions.md. Each phase is a coherent commit (or small series), gets
a simplicity review before its commit, and verifies with a build and the touched tests.

### Phase 1 — The re-addressing transform

- **The model: meters keep time (Q3), so a meter edit is stated on the whole-note axis.** Each
  change in the edited list is placed at a whole-note position: the edited entry where the verb
  puts it (the old downbeat of its measure, or that ± one measure for `Alt+←/→`), every other
  entry where it already stands. The new list's measure numbers are DERIVED by walking those
  positions in order, and a position that is not a downbeat of the list before it refuses the
  edit (Q3's refusal). One rule produces the new list; no verb restates it.
- **Scope.** A pure transform in `rock-hero-editor/core/src/timeline/` (beside plan 41's
  `tempo_map_edits`). It takes the song's timeline state and the new list, maps every position to
  its whole-note position under the old map, and re-addresses it under a scratch
  `TempoMap{new_list, old.anchors()}` that is asked only address questions. The audit's inventory,
  each slice with the treatment its storage needs:
  - *Re-addressed* (a token in, a token out at the same whole-note position): tempo anchors (their
    milliseconds untouched), note onsets, FHP positions (`FretHandPosition::position`), tone region
    starts and tone automation points. All of these apply to EVERY arrangement, not just the
    current one.
  - *Untouched durations* (a note value): `ChartNote::sustain` and `Keyframe::offset` are
    whole-note fractions from the onset, and a meter edit keeps every whole-note position, so they
    stay as stored. A ring and a keyframe that cross a denominator change get a test each. (As
    built in `e69c2f2f` they were beat counts, re-expressed as the new map's beat distance; the
    whole-note flip, `987b62cf`, removed that step.)
  - *Snapped* (Q4): a section that is no longer a downbeat goes to its measure's downbeat, and two
    sections snapping onto one downbeat refuse the edit.
  - *Re-seated*: the terminal, re-addressed like any anchor, moves to the first downbeat AT OR
    AFTER its position when it is not one. Its time comes from the last real anchor at that span's
    quarter tempo, rounded to whole milliseconds once, as `terminalCovering` does. This new pure
    `reseatTerminalAnchor` sits beside plan 41's transforms.
  - *Untouched* (stored in seconds): anchor milliseconds, the audio asset offset and duration, and
    the persisted passive cursor.
  - *Editor state.* Every landed meter verb selects the meter (checklist step 6), which replaces
    the whole `EditorSelection` and dissolves the caret, and undo's focus does the same. So no
    selection slice is re-addressed: only the cursor's place is carried (decision on the cursor,
    *Open questions*). Pending gesture state (fret entry, ghosts, drags, a move run's origin) is
    settled or ended before any verb runs, as it is for every action.
  Spans and every view state are derived, not slices.
- **Exact, not approximate.** Every position keeps its whole-note value exactly (`Fraction`
  equality): admissible downbeats are whole ticks, so `nearestTick` has nothing to round. Seconds
  agree within a float epsilon (the quarter-axis decomposition differs). The tick-lattice rule
  (2026-09-23) holds automatically on the whole-note axis. A denominator the lattice cannot hold
  (512 and up) is already refused by the rules.
- **Tests.** Property tests over randomized maps and content:
  - resolved whole-note positions are exactly equal and seconds equal within epsilon, for every
    slice except a snapped section and a re-seated terminal;
  - an A→B→A round trip restores every token exactly, with the same two exemptions;
  - every result passes the tempo-map, section and chart rules;
  - Q3's refusal (a reign that does not divide) and its no-later-meter pass;
  - Q4's snap and its collision refusal;
  - terminal re-seat cases;
  - one test per slice, so a slice dropped from the transform fails by name.

### Phase 2 — Snapshot, verbs, chord

- **A whole-song snapshot** through `commitMarkerModel` captures the tempo map, the sections and,
  for every arrangement keyed by id, its chart, tone track and tone automation. It is captured
  once per edit and restored field-equal on undo. It captures nothing a meter verb cannot change
  (metadata, tones catalog, audio), so an undo cannot clobber those. Its `applyTo` needs ONE new
  song-level `Session` mutation in common/core's `session.h`, replacing those slices together and
  advancing the revision once; today no write path reaches a non-current arrangement. Size and
  capture cost are measured against plan 64's findings before this phase ships (see the
  undo-memory answer above).
- **The verbs**: insert, restate (numerator, denominator), delete and move (Q2) a meter. Each
  refuses what the rules refuse, re-addresses through Phase 1 and commits ONCE. Checklist steps
  1–8:
  - a published `Ctrl+/` chord target (nothing / restate-with-prompt / insert);
  - a restate arm;
  - meter arms in `Delete` and `MoveSelection`;
  - a new author command registered with `markerAuthorChord(g_time_signature_key)`;
  - the keymap and developer-guide rows.
  
  The audit maps each step to its files (session scratchpad `plan65-readiness.md` §5; re-derive
  it if lost).
- **The cursor keeps its time**: read its place before the commit, select the landed meter, then
  move the cursor to the re-addressed place (the cursor answer above).
- **Tests** (controller suites): each verb and refusal; `Ctrl+/` twice prompts; undo and redo
  restore the whole song field-equal AND re-derive the automation curves and host tempo; a meter
  edit on a populated multi-arrangement song moves no resolved time.

### Phase 3 — UI, then the sighting

- The numerator/denominator prompt opened by the published verb from `Ctrl+/`, `Enter` and a
  double-click; the signature band's click and right-click rows ("Add Time Signature", "Set Time
  Signature…", "Delete Time Signature"), disabled while playing as the section menu's are.
- The one-shot flash over the renumbered range (decision 5).
- **Sighting** (G65-TS closes here). Author a signature change on a fully populated chart and
  confirm nothing moves in time or sound and undo is one step. Then look closely at content that
  crosses the change (added 2026-09-23, from the lattice follow-up's own sighting, which did not
  get a close look at this): move a note carrying a bend or slide across the barline both ways and
  confirm its ring length and keyframe timing stay put musically; with snap off, tick-step a
  keyframe that sits in the later meter; convert a short note just before the change into a pick
  slide and confirm it rings a quarter note; place notes on an odd grid (`1/28`) on both sides of
  the change and confirm consecutive placements touch. Each of these was refused or off the
  lattice before `a6455969`, and the meter edit is the first verb that can create such content
  without an import. Also: a sub-beat tempo anchor after the change keeps its time.

## Final acceptance

As separate invocations: a build, the touched tests, and `pre-commit run --all-files`; clang-tidy
when the user asks. Manual script (local corpus, never CI): open a GP-imported package with tone
automation and sections, change a meter mid-song, confirm playback is unchanged and the measures
after it renumber; undo; save, reopen, and confirm the reader accepts the package unchanged.

## Rollback/abort notes

- Format-neutral: no migration, no load normalizer. Reverting the commits removes the feature;
  packages saved meanwhile stay valid.
- This is the risky plan: a re-addressing bug corrupts charts silently. Land it only behind its
  property suite; if the time-preservation invariant cannot be made provably green, STOP and
  re-raise Q1 rather than ship an approximate transform.
