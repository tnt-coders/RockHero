# Timeline Ruler Review Fixes

Status: ALL PHASES COMPLETE (2026-07-03). Archived in docs/plans/completed.

Source: full review of the timeline-ruler feature arc (`git diff tmp` — TempoMap
beat/quarter extensions, tempo grid geometry, TimelineRuler, timeline cursor helpers,
GridSpacingSelector, grid-spacing settings/controller wiring, transport-strip readouts).
Method: eight independent finder passes (correctness ×3, reuse, simplification/dead-code,
efficiency, altitude, conventions), every substantive candidate verified against the code.
One candidate was refuted and is excluded (zero-duration timeline click "regression" —
unreachable: `Session::loadSong` rejects non-positive audio durations, and the seek action
was gated on a loaded arrangement in both old and new code).

Line numbers are as of the review; re-locate by symbol if drifted.

## Phase 1 — Correctness bugs

- [x] **1.1 Int-overflow UB in grid note-value conversion.**
  DONE — implemented differently than first sketched: a parse-time value bound would wrongly
  reject entries like 256/1024 that Fraction reduces to valid values, so the conversion widens
  the product to int64 and collapses out-of-int (or sub-1) products to the documented-invalid
  0/1 for isValidTempoGridSpacing to reject. parseNoteValueText additionally caps digit runs at
  nine characters because juce::String::getIntValue itself accumulates into int and would
  overflow first. Tests: oversized-entry case in test_tempo_grid_geometry.cpp; overlong digit
  run in test_grid_spacing_selector.cpp.
  `tempo_grid_geometry.h:73` (`tempoGridSpacingFromNoteValue`) multiplies
  `note_value.numerator * signature_denominator` in `int`. The grid combo box is editable
  free text and `parseNoteValueText` (`grid_spacing_selector.cpp:51-56`) bounds parsed ints
  only from below, so e.g. `600000000/1` in 4/4 overflows signed int (UB) at
  `editor_view.cpp:2037` before `isValidTempoGridSpacing` runs at `:2039`.
  Fix: bound numerator/denominator at parse time (an upper bound consistent with
  `isValidTempoGridSpacing`'s limits) so invalid magnitudes never reach the multiply;
  alternatively widen the intermediate to `std::int64_t` inside
  `tempoGridSpacingFromNoteValue`. Prefer the parse-time bound: it rejects garbage at the
  edge, and the conversion keeps its simple contract.
  Test: headless case for the parse bound; UI test typing an over-large value leaves the
  grid unchanged.

- [x] **1.2 Import carries the previous project's grid spacing.**
  DONE — reset added on both the import-commit path and the rig-load failure teardown (which
  also leaves the editor project-less and previously kept stale spacing, mirroring
  closeProject). Test: "EditorController resets grid spacing on import".
  `editor_controller.cpp:1891-1934` (`finishImportSongSourceAfterLiveRigLoad`) resets
  project file, dirty flags, undo history, and cursor, but never touches
  `m_grid_spacing_beats`. Open A → set 1/128 grid → File > Import: the imported project
  inherits A's spacing, unlike open (`gridSpacingForOpenedProject`) and close (reset to
  `Fraction{1, 1}`).
  Fix: reset `m_grid_spacing_beats` to the default on the import-commit path, mirroring
  `closeProject`.
  Test: controller-state test — import after changing spacing yields the default.

- [x] **1.3 Grid spacing chosen before the first save is never persisted.**
  DONE — applyProjectWriteSuccess(SaveProjectAs) now persists grid spacing beside the cursor.
  Plain Save needs nothing: with a project path present, SetGridSpacing already persisted at
  selection time. Test: "EditorController persists grid spacing on save-as".
  `saveProjectGridSpacing` has exactly one call site (`editor_controller.cpp:2953`) gated
  by `!m_project_file.empty()` (`:2950`); `applyProjectWriteSuccess` for SaveProjectAs
  (`:3948-3956`) persists only the cursor after adopting the new path. Import → set 1/16 →
  Save As → reopen: cursor restores, spacing reverts to 1/1.
  Fix: persist grid spacing alongside the cursor in the SaveProjectAs success path.
  Test: controller-state test for the import → set spacing → Save As → reopen round trip.

- [x] **1.4 Latent TempoMap forward/inverse/doc divergence below the first anchor.**
  DONE — the clamp lives in secondsAtAnchorSpan, the shared tail behind both the binary-search
  query and ForwardBeatTimeCursor, so both paths clamp identically; bit-identical for valid
  maps (first anchor at quarter position zero is already clamped by the callers' <= 0 checks).
  Test: "TempoMap clamps beat positions below the first anchor".
  For a map whose first anchor is not measure 1 beat 1: `secondsAtGlobalBeatPosition`
  (`tempo_map.cpp:416-430`) and `ForwardBeatTimeCursor` extrapolate the first span's slope
  (negative fraction) below the first anchor, while the header contract
  (`tempo_map.h:199-200`) promises clamping and `beatPositionAtSeconds` (`:296-299`) does
  clamp. Unreachable today — package validation (`rock_song_package.cpp:559-565`) forces
  the first anchor to (1,1) — but the value ctor accepts arbitrary maps and the class
  elsewhere degrades gracefully rather than assuming validity.
  Fix: clamp quarter positions below the first anchor in the forward path (mirroring the
  inverse) so forward, inverse, and doc agree; add a unit test with a non-(1,1) first
  anchor.

## Phase 2 — Ruler label placement consolidation (one refactor, three payoffs)

- [x] **2.1 Collapse the five duplicated overlap-suppression blocks.**
  DONE — a file-local RulerRowPlacement cursor (named constants g_label_inset/g_label_width_pad/
  g_label_gap; cheap position-only accepts() before formatting; width-aware reserve()) now backs
  all three rows; the tempo glyph/digit pair reserves as one unit, and the pinned labels seed
  their rows at column zero through the same path instead of special-cased pushes. One deliberate
  visual delta: measure-label suppression now anchors on the label x like the other rows, so a
  measure tick at columns 0-3 draws its number (previously suppressed by the raw tick-x
  comparison); existing pixel-test sample points verified unaffected.
  `timeline_ruler.cpp` writes the same measure→pad→place→advance pattern five times:
  measure row (~`:241-266`), pinned tempo (~`:300-307`), anchor tempo loop (~`:311-331`),
  pinned signature (~`:341-348`), signature loop (~`:350-365`). Introduce one file-local
  placement helper (or a small `LabelRowBuilder` owning `next_x`) that takes the candidate
  x, text, font, and target row and applies the shared policy. The tempo glyph/digit pair
  goes through as one composed unit.
  While doing so:
  - [ ] Name the magic numbers: 4 (left inset), 8 (label width pad), 10 (inter-label gap)
    as `constexpr` values with a one-line comment, matching how every other layout
    dimension in the file is already named.
  - [ ] Reorder so the position half of the suppression test (`label_x >= next_x`) runs
    **before** string formatting and `textWidth` (GlyphArrangement) measurement; measure
    only survivors. On dense warp maps (anchor-per-beat storage is a deliberate model)
    this drops thousands of per-scroll-tick measurements to a few dozen.
  Behavior must be pixel-identical; existing ruler pixel tests are the guard.

## Phase 3 — Redundant rebuild work in the view

- [x] **3.1 setState double grid rescan.**
  DONE — setProjectLoaded early-outs on an unchanged flag and setTimelineRange on an unchanged
  range, so repeat state pushes (busy-progress ticks) skip both relayouts. The constructor's
  setProjectLoaded(false) push is gone: the arrangement view and cursor overlay now start via
  addChildComponent (hidden), matching the project-not-loaded member defaults the early-out
  relies on; resized() covers the initial layout once bounds exist.
  `EditorView::setState` (`editor_view.cpp:1163-1177`) reaches `refreshTimelineGrid` twice
  unconditionally: `setProjectLoaded` (no equality guard, `:637` → `:653` → `:804`) and
  `setTimelineRange` (`layoutScaledCanvas` at `:666` sits outside the changed-range guard).
  Busy-progress ticks push full states (`BusyOperationWorkflow` refresh →
  `updateView`, `editor_controller.cpp:1536`), so every progress tick pays two full
  `visibleTempoGridLines` scans plus two ruler label rebuilds.
  Fix: early-out `setProjectLoaded` on an unchanged flag; move `layoutScaledCanvas` inside
  the `m_timeline_range != timeline_range` branch (the scroll path already has exactly this
  skip in `refreshTimelineGridForViewChange`).

- [x] **3.2 TimelineRuler::setGrid wasted rebuild.**
  DONE — setGrid only stores; the rebuild/repaint defers to the guaranteed setGridLines
  follow-up, documented in the header with the same contract wording as setTimelineView. The
  direct-ruler pixel tests already follow setGrid with setGridLines, so they exercise the new
  contract unchanged.
  `timeline_ruler.cpp:118-119` rebuilds geometry and repaints against stale grid lines;
  the only caller (`TrackViewport::setGrid`, `editor_view.cpp:679-680`) immediately follows
  with `refreshTimelineGrid` → `setGridLines`, which rebuilds again in the same message-loop
  callback, so the first pass is never painted.
  Fix: drop the rebuild/repaint from `setGrid` and rely on the guaranteed `setGridLines`
  follow-up — the same contract `setTimelineView` already documents.

- [x] **3.3 TempoMap by-value parameters.**
  DONE — both setGrid overloads take const TempoMap& and copy into the member only after the
  inequality check; comments note the deviation from the sink-by-value convention (the common
  unchanged case would copy just to discard). Both call sites pass lvalues, so no move was lost.
  `TrackViewport::setGrid` (`editor_view.cpp:670`) and `TimelineRuler::setGrid`
  (`timeline_ruler.cpp:108`) take `TempoMap` by value; the common compare-equal case copies
  the map (authored vectors + three derived index tables) just to discard it.
  Fix: take `const TempoMap&`, copy into the member only after the inequality check.

- [x] **3.4 Per-vblank string churn in refreshTimeDisplay.**
  DONE — caches the last rendered transport seconds (optional<double> member) and returns before
  any formatting when unchanged. Seconds alone identify the text because the other readout inputs
  (project-loaded flag, tempo map) change only through setState, which resets the cache before
  its direct refresh; caching project_loaded separately would have been redundant with that
  invalidation and still insufficient for a tempo-map swap at an unchanged position.
  `editor_view.cpp:1868-1877` builds several `juce::String`s every vblank frame even while
  the transport holds position (`setText` skips only the repaint).
  Fix: cache last sampled `seconds` + `project_loaded` and early-out when unchanged.

## Phase 4 — Layering moves (headless logic out of ui)

- [x] **4.1 Move `formattedBeatPosition` (and `formattedTimelineTime`) to editor-core.**
  DONE — new `transport_readout_text.h/.cpp` in editor-core, following the
  `audio_device_status_text` precedent: `std::string` + `std::format` (editor-core's public
  headers do not expose juce_core), renamed to the core `*Text` idiom (`timelineTimeText`,
  `beatPositionText`). The hundredths-quantization comment moved with the code. Headless tests
  in `test_transport_readout_text.cpp` cover the sub-hour format, hour rollover,
  cross-field millisecond carry, negative clamp, mid-beat hundredths, and the 1.4.99 downbeat
  regression (the JUCE component test keeps guarding the label wiring).
  `editor_view.cpp:316-352` — pure derivation, including the load-bearing invariant
  "quantize to display hundredths BEFORE splitting off the whole beat" (the 3.9999… →
  `1.4.99` bug), currently guarded only by a JUCE component test.
  `architectural-principles.md`: "pure logic belongs in pure libraries"; precedent:
  `displayedTempoGridNoteValue`, `visibleTempoGridLines` already live in editor-core with
  headless tests. `juce::String` is permitted in core (or return `std::string`).
  Add headless unit tests for the hundredths quantization and the (h:)m:ss:mmm format.

- [x] **4.2 Move `timelineCursorPlacementTime` + `TimelineCursorPlacementMode` to
  editor-core.**
  DONE — both now live beside `nearestTempoGridTime` in tempo_grid_geometry, with full Doxygen
  (both enumerators documented, which also settles the enum half of 6.4). `timeline_cursor.h/.cpp`
  keep only `repaintCursorStrip`; the two mouseDown call sites qualify with `core::`. Headless
  tests in test_tempo_grid_geometry.cpp cover snap-vs-free semantics, the halfway-tie rule, and
  degenerate-geometry rejection. 5.2's shared `placementModeFor(mods)` helper was deliberately
  not folded in (separate consolidation, per phase ordering). `timeline_cursor.cpp:46-59` is JUCE-free snap policy composing two
  editor-core functions, unreachable by headless tests from `ui/src`. Natural home: next to
  `nearestTempoGridTime` in tempo_grid_geometry (or timeline_geometry). `repaintCursorStrip`
  — genuinely JUCE-bound — stays in ui/timeline_cursor.
  Add headless tests for snap-vs-free semantics and the halfway-tie rule.

- [x] **4.3 (Decide) Grid display policy in the controller.**
  DECIDED (2026-07-03) — superseded by Phase 7. The user wants the underlying musical
  wrongness fixed, not just the policy consolidated: the grid becomes note-value-authoritative
  (Phase 7), which deletes the measure-1-denominator conversion policy from both view sites
  entirely instead of moving it. Option (a)'s contract change (view forwards raw note-value
  intents; state carries the display note value) happens as Phase 7's first step.
  The measure-1-denominator policy for note-value↔spacing conversion is encoded twice in
  the view (`editor_view.cpp:1185`, `:2038`) while the controller validates separately.
  Verified self-consistent today; the cost is a future signature-aware grid needing
  lockstep edits in two UI sites. Options: (a) EditorViewState carries the derived display
  note value and the view forwards raw note-value intents; (b) leave as-is until a
  signature-aware grid is actually wanted. Decide before implementing — (a) touches the
  view-state contract.

## Phase 5 — Duplication consolidations

- [x] **5.1 Shared cursor drawing.**
  DONE — `drawTimelineCursor(Graphics&, const Component&, optional<float> cursor_x, int top)`
  in timeline_cursor owns the guard, round, clamp, and one-pixel white fill; the ruler passes
  `g_ruler_body_top`, the overlay passes 0. The advance/set mirror was left alone (it is the
  same two-line update pattern, but sharing it would mean sharing m_cursor_x state for no
  drawing benefit). `TimelineRuler::drawCursor`
  (`timeline_ruler.cpp:~396-406`) and `CursorOverlay::paint` (`editor_view.cpp:~403-406`)
  duplicate the clamp/round/white/1px column draw; `advanceCursor` mirrors
  `setCursorPosition`. timeline_cursor.h exists precisely as the shared cursor seam but
  holds only half the logic. Add a `drawTimelineCursor(Graphics&, float x, int top, int
  height, int width)` helper (ruler passes its body-top offset) and call it from both.

- [x] **5.2 Shared snap-modifier mapping.**
  DONE — `placementModeFor(const juce::ModifierKeys&)` lives in timeline_cursor (not beside the
  placement function as first sketched: that moved to editor-core in 4.2, and core cannot see
  juce::ModifierKeys, so the ui-side cursor seam is its natural home). Both mouseDown sites call
  it. The `event.mods.isCtrlDown() ? Free :
  SnapToGrid` ternary is duplicated in `TimelineRuler::mouseDown` and
  `CursorOverlay::mouseDown`. One `placementModeFor(const juce::ModifierKeys&)` beside the
  placement function removes the divergence point. (Folds naturally into 4.2's move.)

- [x] **5.3 Shared text-width helper.**
  DONE — new ui-internal `text_metrics.h/.cpp` holds `textWidth(font, text)`;
  timeline_ruler.cpp's file-local copy is deleted and MenuBarButton::preferredWidthForHeight
  now measures through it (behavior-identical: same GlyphArrangement layout and ceil). The GlyphArrangement `textWidth` workaround for
  JUCE's deprecated string-width API exists in `timeline_ruler.cpp:54-60` and
  `menu_bar_button.cpp:52-55`. Hoist one small ui-internal helper.

- [x] **5.4 (Decide) editor_settings keyed-record family.**
  DECIDED go + DONE (2026-07-03) — file-local `KeyedRecordStore<Codec>` template owns the shared
  lifecycle (replace-by-key with normalize/validate, read with load-time dedup and
  malformed-vs-missing distinction mapped onto the codec's error code, format-versioned
  whole-list write); each family reduced to one codec struct carrying its property/tag names,
  error code, `normalized`/`isValid`/`sameKey`, and `fromXml`/`toXml`. XML format, error codes,
  and messages are byte-identical, so existing settings files and test_editor_settings.cpp are
  unchanged. Behavior notes: the per-item tag check moved out of fromXml (the child iterator
  already filters by tag); calibration gain clamping now happens once in `normalized()` on every
  store entry instead of twice (read + replace) — same observable values. Done ahead of Phase 7
  deliberately: 7.5's new note-value grid record becomes one more codec. Grid-spacing persistence is
  the third structurally identical record family in `editor_settings.cpp` (cursor:
  `:44-54/:184-284/:652-708`; grid spacing: `:56-66/:286-395/:710-766`; input calibration:
  `:68-72/:397-524/:768-862`) — same five-function shape (replace / read-state-xml /
  read-history / write-history / lookup+save), same key normalization, same malformed-XML
  triage, ~110 duplicated lines per family. A generic keyed XML record-store parameterized
  on tag names + a per-record codec would reduce each family to its codec. This is a real
  consolidation, not speculative generality — but it is a standalone refactor; do it as its
  own change, not bundled with bug fixes.

- [x] **5.5 Test fixture dedup.**
  DONE — the four identical three-anchor 4/4 maps collapsed into a file-local
  `makeSparseAnchor44Map()` builder; the Phase 1.4 below-first-anchor map is a different
  fixture and stays inline. `test_tempo_map.cpp` inlines the same three-anchor 4/4
  map four times (`:51-60`, `:71-80`, `:93-102`, `:138-147`); extract a file-local builder
  following the `makeUniform44Map` / `makeOneMeasureTempoMap` pattern used by the other
  test files.

## Phase 6 — Leftovers and conventions sweep

- [x] **6.1 Dead struct field.**
  DONE — `TempoGridLine::beat` dropped from the struct, the merge-promotion write, and the
  aggregate construction (the `beat` local stays for rank classification); test expectation
  literals trimmed. Phase 7's per-measure rank addressing does not need the field back. `TempoGridLine::beat` (`tempo_grid_geometry.h:99`) is
  written and merge-maintained in production (`tempo_grid_geometry.cpp:190,196`) but read
  only by test equality assertions. Drop the field (keep the local used for rank
  classification); trim test expectations; reintroduce only with a real consumer.

- [x] **6.2 Stale comments from removed iterations.**
  DONE — the drawCursor "event band" wording was already replaced with "header bands" during
  5.1; the editor_view.cpp TrackViewport member comment now names the header bands instead of
  anchors; the timeline_ruler.h refreshRulerGeometry comment lost its "anchor geometry" mention
  in the 6.4 rewrite.
  - `timeline_ruler.cpp:394` — "event band" (no such band; reword to tempo/signature bands).
  - `timeline_ruler.h:70` — "tick, label, and anchor geometry" (no anchor geometry remains).
  - `editor_view.cpp:1008` — "shows measure orientation and tempo-map anchors" (anchors are
    no longer visualized).
- [x] **6.3 Spelling convention.**
  DONE — both "colour" occurrences in project prose (drawLabelRow's header and cpp comments)
  now read "color"; JUCE API identifiers untouched. "colour" in project-owned prose at `timeline_ruler.h:84`
  and `timeline_ruler.cpp:380` → "color" (US spelling for our identifiers and comments;
  British only for JUCE API names).
- [x] **6.4 Doxygen coverage for the new headers.**
  DONE — timeline_ruler.h converted per documentation-conventions.md: \file header, Doxygen for
  g_timeline_ruler_height (documenting the height ↔ cpp-private band-layout coupling), the
  class, the callback alias, and every public method with full \param coverage; private members
  and methods keep regular comments per the convention. The TimelineCursorPlacementMode enum
  half was already covered by 4.2's move (see the struck-through note below).
  `timeline_ruler.h` is the only src header (of 17) documented with plain `//` comments;
  documentation-conventions.md requires Doxygen for every project-owned header declaration
  visible outside a single .cpp — convert the class, public methods, and
  `g_timeline_ruler_height` (documenting the 53px ↔ cpp-private band-layout invariant).
  ~~`timeline_cursor.h:19` — Doxygen the `TimelineCursorPlacementMode` enum and BOTH
  enumerators (the doc forbids partial enumerator coverage).~~ Done by 4.2's move: the enum now
  lives in tempo_grid_geometry.h with both enumerators documented.
- [x] **6.5 Stale in-progress plans.**
  DONE — timeline-snap-time-domain-plan.md and tempo-grid-spacing-plan.md deleted;
  tempo-grid-declutter-plan.md moved to docs/plans/todo/ with a staleness note (superseded sketch,
  subdivisions landed, and Phase 7 reshapes grid generation first). Working-tree changes only;
  staging left to the user. Per CLAUDE.md's documentation-maintenance rules:
  delete `docs/plans/in-progress/timeline-snap-time-domain-plan.md` and
  `docs/plans/in-progress/tempo-grid-spacing-plan.md` (fully landed; they reference APIs that no
  longer exist), and move `docs/plans/in-progress/tempo-grid-declutter-plan.md` to `docs/plans/todo/`
  (deferred; its `TempoGridLineStrength` sketch is superseded by the landed
  `TempoGridLineRank` and needs revision before any future use).

## Phase 7 — Note-value-authoritative grid (fix the measure-1 signature policy for real)

Decision (2026-07-03, user): the grid's musical meaning must not silently change across
time-signature changes, and the selector label must never lie. Today the step is authoritative
in tempo-map beats, and a beat is denominator-relative (`tempo_map.h` segments:
`quarters_per_beat = 4 / denominator`), so a 1-beat grid means quarter notes in 4/4 but eighth
notes in 6/8 — while the selector label converts against measure 1's denominator everywhere.
The fix: the user's chosen note value (1/8, 1/16, …) becomes the single authoritative grid
unit, and grid lines mean that musical duration in every section (REAPER's grid semantics).

Design anchor: a note value of n/d whole notes is a constant `4n/d` quarter notes regardless
of meter, and the tempo map's metronome axis is quarter notes. So generation moves from the
beat axis to the quarter axis, **measure-anchored**: within each measure, lines sit at
`j * (4n/d)` quarters from the measure's start quarter position. Measure anchoring (rather
than song-start-uniform stepping) keeps every downbeat on a grid line even in meters whose
measure length is not a multiple of the step (7/8 measure = 3.5 quarters with a 1/4-note
grid), matching REAPER. Within standard meters and power-of-two grids the two schemes
coincide. Exact rational per-line addressing (the whole + remainder/denominator trick used
today) carries over; signature denominators are power-of-two per package validation, so the
per-measure quarter offsets stay exact.

- [x] **7.1 Contract flip (the old 4.3(a)).**
  DONE — `EditorViewState.grid_note_value{1, 4}`; the controller intent is
  `onGridNoteValueChangeRequested` (matching the interface's *Requested idiom rather than the
  sketch's onGridNoteValueChosen, which stays as the selector's Listener method); the action is
  `EditorAction::SetGridNoteValue{note_value}`. The view forwards raw note values and displays
  the state value verbatim; both `timeSignatureAt(1)` conversion sites are gone. Rejection
  feedback verified free: GridSpacingSelector::handleSelectionCommitted unconditionally reverts
  its display to the last applied value after emitting, so a controller-dropped intent needs no
  state push. The GridSpacingSelector widget name/component IDs stay (unit-neutral, test-facing). `EditorViewState.grid_spacing_beats` →
  `grid_note_value` (authoritative, default `Fraction{1, 4}` — same visual default as
  today's 1 beat in 4/4). `IEditorController::onGridSpacingChangeRequested(spacing_beats)` →
  `onGridNoteValueChosen(note_value)`. The view forwards raw note values and displays
  `m_state.grid_note_value` directly; both `timeSignatureAt(1)` conversion sites disappear.
  Decide rejection feedback while here: the controller drops invalid entries without a state
  push, whereas today's view-local drop relies on the selector snapping back to its last
  displayed value — verify GridSpacingSelector still reverts without a push, or have the view
  re-set it explicitly after a rejected intent.
- [x] **7.2 Validation in note-value terms.**
  DONE — `isValidTempoGridNoteValue` with terms in [1, 128]
  (`g_max_tempo_grid_note_value_term`); 0/1 stays the documented invalid value. The 1.1 int64
  overflow guard vanished with the conversion it guarded (no multiply exists any more);
  GridSpacingSelector's 9-digit parse cap stays as the entry-side overflow guard. Replace `isValidTempoGridSpacing` bounds
  checking of beat fractions with a note-value validity check (terms in [1, 128] still; 0/1
  stays the documented invalid value). The 1.1 int64 overflow guard logic moves with the
  conversion it guards (see 7.3); GridSpacingSelector's 9-digit parse cap stays.
- [x] **7.3 Grid generation on the quarter axis.**
  DONE — a file-local `MeasureGridWalker` in tempo_grid_geometry.cpp owns the measure-anchored
  walk: within a measure with signature num/den_sig, line j sits `j*n*den_sig/d` beats after the
  downbeat (exact integers over the note denominator), restarting at every downbeat. No new
  TempoMap API was needed after all: lines address as exact rationals on the existing global
  beat axis (`globalBeatIndex`, `secondsAtGlobalBeatPosition`, `ForwardBeatTimeCursor`,
  `timeSignatureAt`), so the seek keeps a binary search (measures, then steps) and the scan
  keeps the amortized-constant forward cursor. Rank falls out of the offset (0 → Measure;
  whole-beat multiple → Beat). `displayedTempoGridNoteValue`/`tempoGridSpacingFromNoteValue`
  and their tests are deleted. One robustness addition beyond the sketch: non-positive
  signature terms from a malformed map clamp to one inside the walker, because a zero step
  would hang the scan where the old beat-axis code merely misplaced lines. Invalid note values
  fall back to the quarter-note grid (previously whole-beat). Rework `visibleTempoGridLines` and
  `nearestTempoGridTime` to take the note value and step measure-anchored quarter offsets:
  per-measure line addressing (cumulative line counts per signature segment for the
  monotonic index used by the binary search + forward cursor), rank classification from the
  per-measure address (offset 0 → Measure; on a whole beat, i.e. offset a multiple of the
  section's `4/denominator` quarters → Beat; else Subdivision). TempoMap may need a narrow
  public query for segment/measure quarter geometry — keep it a read-only derived-data
  accessor, mirroring the existing derived index tables. `displayedTempoGridNoteValue` and
  `tempoGridSpacingFromNoteValue` (and their tests) are deleted with the measure-1 policy;
  any internal note-value→quarters conversion lives beside the generation code.
- [x] **7.4 Plumbing unit change.**
  DONE — `timelineCursorPlacementTime`, `TrackViewport::setGrid`, `TimelineRuler::setGrid`,
  `CursorOverlay::setGridNoteValue` (renamed), and `EditorController::m_grid_note_value` all
  speak note values, defaulting to `Fraction{1, 4}`; import/close/failure-teardown resets and
  the settings-restore fallback are quarter-note. Phase 1.2/1.3 test defaults updated, with
  stored/entered fixture values moved off the default (1/8, 1/16) so restores and persists
  cannot pass via the fallback. `timelineCursorPlacementTime`, `TrackViewport::setGrid`,
  `TimelineRuler::setGrid`, `CursorOverlay::setGridSpacing`, and
  `EditorController::m_grid_spacing_beats` all switch from beat fractions to the note value.
  Import/close resets become `Fraction{1, 4}` (update the Phase 1.2/1.3 tests' expected
  defaults).
- [x] **7.5 Persistence migration.**
  DONE — one new codec (`ProjectGridNoteValueCodec`, exactly as 5.4 intended): property key
  "projectGridNoteValues", root tag PROJECT_GRID_NOTE_VALUES, item NOTE_VALUE with
  noteValueNumerator/noteValueDenominator. Settings API renamed to
  projectGridNoteValueFor/saveProjectGridNoteValue; the error code is
  InvalidProjectGridNoteValueHistory. Old "projectGridSpacings" records are orphaned (never
  read), so affected projects reset to the default grid once. Save points unchanged. The editor-settings grid record stores the note value
  under a new record tag/key so existing app-local beat-unit records are ignored rather than
  reinterpreted (one-time spacing reset per project; acceptable for convenience data). Save
  points (eager on change, save-as adoption) unchanged.
- [x] **7.6 Tests.**
  DONE — geometry: new mixed-meter case (4/4 → 6/8 quarter grid stays one line per second with
  Measure-rank downbeats, plus snap-across-boundary and the halfway tie), new odd-meter case
  (7/8 proves the downbeat restart a song-uniform walk would miss), note-value validity bounds;
  existing 4/4 cases converted to note-value units (1/8, 1/16, 1/12, dotted-quarter 3/8 — the
  last gaining the measure-2 downbeat line that measure anchoring adds); fallback tests now
  target the quarter-note default. Controller: intent/validation/reset/restore/save-as round
  trips in note values with non-default fixtures. Settings: record family renamed, malformed
  history targets the new key, and a new case proves retired beat-unit records are ignored.
  UI: selector forwards raw note values (recorded intent is now {1, 16} for "1/16") and
  displays state verbatim; the shared-snapping pixel test uses 1/8 (the old half-beat); the
  direct-ruler pixel tests use 1/4 and 1/8 (identical pixels to the old 1/1 and 1/2 beat
  fixtures in 4/4). Core geometry: mixed-denominator map (4/4 → 6/8) keeps a constant
  musical step across the boundary with all downbeats on Measure-rank lines; odd meter (7/8)
  proves measure anchoring; snap across a signature boundary; halfway ties still resolve
  earlier. Controller: note-value intent round trip, invalid-entry rejection, persistence
  round trip incl. old-tag records being ignored. UI: selector displays the pushed note value
  with no tempo-map dependency; existing ruler/grid pixel tests updated only where the unit
  rename touches them (uniform 4/4 fixtures produce identical pixels).

Implementation note: this is its own change after Phases 5–6 land, with 7.1 as the first
commit. It deliberately does not touch the tempo map's beat↔seconds model, the transport
readout, or anything shared with the future game runtime — the grid becomes a pure
note-value consumer of the existing quarter-note axis.

## Suggested order

Phase 1 first (real bugs, each independently testable). Phase 2 next (single refactor,
pixel-test-guarded). Phase 3 items are small and independent. Phases 4–6 as follow-ups;
5.4 needs an explicit go/no-go decision before implementation (4.3 is decided — see Phase 7).
Phase 7 runs last as its own change once 5–6 land. Keep bug fixes, the ruler refactor, each
consolidation, and the Phase 7 feature as separate commits.

## Review notes for posterity

- All new classes were judged justified (TimelineRuler, GridSpacingSelector,
  ForwardBeatTimeCursor, the data carriers); the structural findings are about placement
  and duplication, not existence.
- No O(n²) or per-paint allocation found; label caching discipline held.
- Refuted candidate recorded above so it is not re-reported by future reviews.
