# Plan 65 — Time-Signature Editing

**Status:** Not started. Split out of plan 41 on 2026-10-03, where it was Phase 6 (written
2026-07-06, last revised there 2026-10-02; git history keeps that text). **Sequenced after plan 60**
(`docs/plans/roadmap/60-hand-markers.md`), by the user's direction of 2026-10-03: time-signature
editing "is one of the most important things ... But it may come after our span marker work unless
it is quite simple". It is not quite simple — a whole-song re-addressing transform held by a
property suite — so it waits for plan 60. Runs on provisional 65-Q1 = A (asked as 41-Q1, user
2026-09-19); gate **G65-TS** (named G41-TS until 2026-10-03) closes at this plan's sighting.
Re-verify *Current state* against the code before building.

## Goal

A charter edits the song's time signatures inside the marker grammar every other marker kind
follows: insert a meter at a measure downbeat, restate its numerator and denominator, delete it,
with every piece of content keeping its time and the measures after the edit renumbering, in one
undo step per edit. Today a meter other than the default exists only if the song came from a GP
import.

## Non-goals

- No new song.json key and no chart format change: `TimeSignatureChange` already persists, and the
  reader's rules (measure >= 1, positive numerator, power-of-two denominator) stay the rules.
- No tempo repair: Scale Tempo, Keep Notes in Time and the tempo lock are
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
  captures the map alone, which is right for anchor edits and wrong for a meter edit.
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
   does not bend, with any residue rounded through `nearestTick`.
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
- **Q2 — the meter's `Alt+←/→`.** The marker grammar moves a selected marker by its kind's step;
  the 2026-07-06 text named only set and remove. Build-session question: move by one measure
  (recommended — a remove at the old downbeat plus an insert at the new, through the same
  transform), or leave the move inert.

## Dependencies

- **After plan 60** (sequencing, not a code dependency).
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

- **Scope.** A pure transform in `rock-hero-editor/core/src/timeline/` (beside plan 41's
  `tempo_map_edits`) taking the song's position-bearing state and an old and a new signature list,
  and mapping every position to its whole-note position under the old list, then re-addressing it
  under the new one. Every position-bearing slice, without exception:
  - the tempo map's anchors (sub-beat since 2026-10-03; their milliseconds untouched);
  - every position-bearing stream of every arrangement's chart: notes and their rings, keyframes,
    hand markers (FHPs) and spans, and whatever plan 60 adds by the time this is built;
  - song sections; every arrangement's tone regions and tone automation points;
  - the editor's own positions: the armed chart caret's grid address, in-session and in its
    persisted `caret:` marker token; plan 47's loop-selection `GridPosition` pair; the marker
    selections (`TempoAnchorSelection`'s position, `TimeSignatureSelection`'s measure, a
    `SongSectionSelection`'s position); and `ChartCursor::column`, which the author-at-cursor
    grammar makes a marker-verb input (2026-07-18 audit: without these the caret and loop land on
    different music after a signature edit while everything else holds its time).
  The transform is time-preserving: every item resolves to the same seconds before and after,
  to within half a tick. The old terminal downbeat may stop being a downbeat under the new
  bucketing; a pure `reseatTerminalAnchor` beside plan 41's transforms re-seats it on the next
  downbeat at or past content, deterministically.
- **The tick lattice (2026-09-23).** Every stored position must be a whole number of ticks
  (1/3840 of a whole note), and validation refuses one that is not. A global-beat position "plus
  fraction" re-read under a larger denominator is not: an odd tick of a quarter-note beat is half
  a tick of an eighth-note one. Hence the whole-note axis and `nearestTick` (decision 4), and the
  time-preservation invariant holds to within half a tick rather than exactly. A denominator the
  lattice cannot hold (512 and up) is already refused by the package validator.
- **Tests.** Property tests over randomized maps and content: a round trip A→B→A restores every
  token exactly; resolved seconds identical within half a tick (anchor milliseconds untouched);
  every result passes the tempo-map and chart rules; terminal re-seat cases; one test per
  position-bearing slice above, so a slice dropped from the transform fails by name.

### Phase 2 — Snapshot, verbs, chord

- **A whole-song snapshot** through `commitMarkerModel`: the tempo map plus every slice Phase 1
  re-addresses, captured once per edit and restored field-equal on undo. Size and capture cost
  are measured against plan 64's findings before this phase ships; if the snapshot's memory over
  a long undo history is a problem there, that is a finding to raise, not a reason to fall back to
  granular replay.
- **The verbs**: insert, restate (numerator, denominator) and delete a meter, refusing what the
  rules refuse, each re-addressing through Phase 1 and committing ONCE. Checklist steps 1–8: a
  published `Ctrl+/` chord target (nothing / restate-with-prompt / insert), a restate arm, a
  meter arm in `Delete` (and in `MoveSelection` if Q2 lands as recommended), the letter declared
  once, the keymap and developer-guide rows.
- **The cursor keeps its place** with no seek: the edit preserves time, so the paused cursor's
  seconds still name the same music, and the caret's grid address re-addresses in Phase 1.
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
