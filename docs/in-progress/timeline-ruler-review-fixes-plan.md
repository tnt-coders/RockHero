# Timeline Ruler Review Fixes

Status: Phase 1 complete (implemented, awaiting user build + test run); later phases not
started.

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

- [ ] **2.1 Collapse the five duplicated overlap-suppression blocks.**
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

- [ ] **3.1 setState double grid rescan.**
  `EditorView::setState` (`editor_view.cpp:1163-1177`) reaches `refreshTimelineGrid` twice
  unconditionally: `setProjectLoaded` (no equality guard, `:637` → `:653` → `:804`) and
  `setTimelineRange` (`layoutScaledCanvas` at `:666` sits outside the changed-range guard).
  Busy-progress ticks push full states (`BusyOperationWorkflow` refresh →
  `updateView`, `editor_controller.cpp:1536`), so every progress tick pays two full
  `visibleTempoGridLines` scans plus two ruler label rebuilds.
  Fix: early-out `setProjectLoaded` on an unchanged flag; move `layoutScaledCanvas` inside
  the `m_timeline_range != timeline_range` branch (the scroll path already has exactly this
  skip in `refreshTimelineGridForViewChange`).

- [ ] **3.2 TimelineRuler::setGrid wasted rebuild.**
  `timeline_ruler.cpp:118-119` rebuilds geometry and repaints against stale grid lines;
  the only caller (`TrackViewport::setGrid`, `editor_view.cpp:679-680`) immediately follows
  with `refreshTimelineGrid` → `setGridLines`, which rebuilds again in the same message-loop
  callback, so the first pass is never painted.
  Fix: drop the rebuild/repaint from `setGrid` and rely on the guaranteed `setGridLines`
  follow-up — the same contract `setTimelineView` already documents.

- [ ] **3.3 TempoMap by-value parameters.**
  `TrackViewport::setGrid` (`editor_view.cpp:670`) and `TimelineRuler::setGrid`
  (`timeline_ruler.cpp:108`) take `TempoMap` by value; the common compare-equal case copies
  the map (authored vectors + three derived index tables) just to discard it.
  Fix: take `const TempoMap&`, copy into the member only after the inequality check.

- [ ] **3.4 Per-vblank string churn in refreshTimeDisplay.**
  `editor_view.cpp:1868-1877` builds several `juce::String`s every vblank frame even while
  the transport holds position (`setText` skips only the repaint).
  Fix: cache last sampled `seconds` + `project_loaded` and early-out when unchanged.

## Phase 4 — Layering moves (headless logic out of ui)

- [ ] **4.1 Move `formattedBeatPosition` (and `formattedTimelineTime`) to editor-core.**
  `editor_view.cpp:316-352` — pure derivation, including the load-bearing invariant
  "quantize to display hundredths BEFORE splitting off the whole beat" (the 3.9999… →
  `1.4.99` bug), currently guarded only by a JUCE component test.
  `architectural-principles.md`: "pure logic belongs in pure libraries"; precedent:
  `displayedTempoGridNoteValue`, `visibleTempoGridLines` already live in editor-core with
  headless tests. `juce::String` is permitted in core (or return `std::string`).
  Add headless unit tests for the hundredths quantization and the (h:)m:ss:mmm format.

- [ ] **4.2 Move `timelineCursorPlacementTime` + `TimelineCursorPlacementMode` to
  editor-core.** `timeline_cursor.cpp:46-59` is JUCE-free snap policy composing two
  editor-core functions, unreachable by headless tests from `ui/src`. Natural home: next to
  `nearestTempoGridTime` in tempo_grid_geometry (or timeline_geometry). `repaintCursorStrip`
  — genuinely JUCE-bound — stays in ui/timeline_cursor.
  Add headless tests for snap-vs-free semantics and the halfway-tie rule.

- [ ] **4.3 (Decide) Grid display policy in the controller.**
  The measure-1-denominator policy for note-value↔spacing conversion is encoded twice in
  the view (`editor_view.cpp:1185`, `:2038`) while the controller validates separately.
  Verified self-consistent today; the cost is a future signature-aware grid needing
  lockstep edits in two UI sites. Options: (a) EditorViewState carries the derived display
  note value and the view forwards raw note-value intents; (b) leave as-is until a
  signature-aware grid is actually wanted. Decide before implementing — (a) touches the
  view-state contract.

## Phase 5 — Duplication consolidations

- [ ] **5.1 Shared cursor drawing.** `TimelineRuler::drawCursor`
  (`timeline_ruler.cpp:~396-406`) and `CursorOverlay::paint` (`editor_view.cpp:~403-406`)
  duplicate the clamp/round/white/1px column draw; `advanceCursor` mirrors
  `setCursorPosition`. timeline_cursor.h exists precisely as the shared cursor seam but
  holds only half the logic. Add a `drawTimelineCursor(Graphics&, float x, int top, int
  height, int width)` helper (ruler passes its body-top offset) and call it from both.

- [ ] **5.2 Shared snap-modifier mapping.** The `event.mods.isCtrlDown() ? Free :
  SnapToGrid` ternary is duplicated in `TimelineRuler::mouseDown` and
  `CursorOverlay::mouseDown`. One `placementModeFor(const juce::ModifierKeys&)` beside the
  placement function removes the divergence point. (Folds naturally into 4.2's move.)

- [ ] **5.3 Shared text-width helper.** The GlyphArrangement `textWidth` workaround for
  JUCE's deprecated string-width API exists in `timeline_ruler.cpp:54-60` and
  `menu_bar_button.cpp:52-55`. Hoist one small ui-internal helper.

- [ ] **5.4 (Decide) editor_settings keyed-record family.** Grid-spacing persistence is
  the third structurally identical record family in `editor_settings.cpp` (cursor:
  `:44-54/:184-284/:652-708`; grid spacing: `:56-66/:286-395/:710-766`; input calibration:
  `:68-72/:397-524/:768-862`) — same five-function shape (replace / read-state-xml /
  read-history / write-history / lookup+save), same key normalization, same malformed-XML
  triage, ~110 duplicated lines per family. A generic keyed XML record-store parameterized
  on tag names + a per-record codec would reduce each family to its codec. This is a real
  consolidation, not speculative generality — but it is a standalone refactor; do it as its
  own change, not bundled with bug fixes.

- [ ] **5.5 Test fixture dedup.** `test_tempo_map.cpp` inlines the same three-anchor 4/4
  map four times (`:51-60`, `:71-80`, `:93-102`, `:138-147`); extract a file-local builder
  following the `makeUniform44Map` / `makeOneMeasureTempoMap` pattern used by the other
  test files.

## Phase 6 — Leftovers and conventions sweep

- [ ] **6.1 Dead struct field.** `TempoGridLine::beat` (`tempo_grid_geometry.h:99`) is
  written and merge-maintained in production (`tempo_grid_geometry.cpp:190,196`) but read
  only by test equality assertions. Drop the field (keep the local used for rank
  classification); trim test expectations; reintroduce only with a real consumer.

- [ ] **6.2 Stale comments from removed iterations.**
  - `timeline_ruler.cpp:394` — "event band" (no such band; reword to tempo/signature bands).
  - `timeline_ruler.h:70` — "tick, label, and anchor geometry" (no anchor geometry remains).
  - `editor_view.cpp:1008` — "shows measure orientation and tempo-map anchors" (anchors are
    no longer visualized).
- [ ] **6.3 Spelling convention.** "colour" in project-owned prose at `timeline_ruler.h:84`
  and `timeline_ruler.cpp:380` → "color" (US spelling for our identifiers and comments;
  British only for JUCE API names).
- [ ] **6.4 Doxygen coverage for the new headers.**
  `timeline_ruler.h` is the only src header (of 17) documented with plain `//` comments;
  documentation-conventions.md requires Doxygen for every project-owned header declaration
  visible outside a single .cpp — convert the class, public methods, and
  `g_timeline_ruler_height` (documenting the 53px ↔ cpp-private band-layout invariant).
  `timeline_cursor.h:19` — Doxygen the `TimelineCursorPlacementMode` enum and BOTH
  enumerators (the doc forbids partial enumerator coverage).
- [ ] **6.5 Stale in-progress plans.** Per CLAUDE.md's documentation-maintenance rules:
  delete `docs/in-progress/timeline-snap-time-domain-plan.md` and
  `docs/in-progress/tempo-grid-spacing-plan.md` (fully landed; they reference APIs that no
  longer exist), and move `docs/in-progress/tempo-grid-declutter-plan.md` to `docs/todo/`
  (deferred; its `TempoGridLineStrength` sketch is superseded by the landed
  `TempoGridLineRank` and needs revision before any future use).

## Suggested order

Phase 1 first (real bugs, each independently testable). Phase 2 next (single refactor,
pixel-test-guarded). Phase 3 items are small and independent. Phases 4–6 as follow-ups;
4.3 and 5.4 need an explicit go/no-go decision before implementation. Keep bug fixes,
the ruler refactor, and each consolidation as separate commits.

## Review notes for posterity

- All new classes were judged justified (TimelineRuler, GridSpacingSelector,
  ForwardBeatTimeCursor, the data carriers); the structural findings are about placement
  and duplication, not existence.
- No O(n²) or per-paint allocation found; label caching discipline held.
- Refuted candidate recorded above so it is not re-reported by future reviews.
