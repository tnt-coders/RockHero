# Plan 40 — Chart Editing

**Status:** Executing — started 2026-07-16 (baseline `refactor @ 3c7febe0`; authored 2026-07-06).
**Pre-execution re-verification (2026-07-16):** the load-bearing inventory claims hold at today's
tree (arrangement.h "do not rewrite" comment, validate-only chart save path in
`rock_song_package_write.cpp`, arrangement-id-keyed projection cache "because charts are
immutable", `TabView` still pointer-inert via `setInterceptsMouseClicks(false, false)` — line
numbers drifted, semantics intact). Phase 2's blocker is cleared: the in-flight tone-track work
committed long ago (milestone 0 plays tone automation through it). Standing sequencing note: per
30-Q2 (answered 2026-07-11), plan 30 Phase 2 (shared notation paint core + layout manifest) runs
BEFORE this plan's Phase 3 adds interaction — the execution order is Phases 1–2 here, then plan
30 Phases 1–2, then Phases 3+ here. **Phase 1 complete 2026-07-16**: exact `Fraction`
operator+/operator- (64-bit intermediates, normalized results) and the grid-arithmetic unit in
common/core (`chart/grid_arithmetic.h`: `advanceGridPosition` with origin clamp,
`beatDistance` as its exact inverse, `sustainEndPosition`, and `snapGridPosition` reproducing
the editor timeline grid's semantics — measure-anchored whole-note-fraction note values,
downbeats always lines, ties to the earlier line; note-value validity policy stays with
callers). Tests cover signature-change carries, tuplet round-trips, the 7/8-with-1/4-grid
next-downbeat path, and tie stability. **Phase 2 complete 2026-07-16**: saves serialize the
in-memory chart through `writeChartDocument` (dangling refs still refused; stale comments in
arrangement.h / rock_song_package_write.cpp updated); `Session::currentChart()` added as the
fourth narrow mutation surface with `chartRevision()` advancing on every mutable acquisition
(over-invalidates, never under-invalidates — no forgot-to-notify path exists); the tab AND 3D
highway projection caches key on (arrangement id, chart revision). Tests: edited-chart
save/reload round-trip, unedited-save byte-stability, session revision semantics. The
"revision rebuilds the projection" controller-level test is deferred to the phase that adds
the first chart-mutation intent (Phase 3/4) — no public route can bump the revision until one
exists; the session-level behavior is covered now.
**Phase 3 complete 2026-07-16** (after plan 30 Phase 2 landed the paint core + manifest per
30-Q2): headless selection/caret in editor-core (`src/chart/chart_selection.{h,cpp}` —
`ChartNoteKey` (position, string) sorted-unique container with replace/toggle/add/box ops and
key→projection-index resolution; `chart_hit_testing.{h,cpp}` resolving pixels through the shared
layout manifest, heads-over-tails with nearest-onset tie-breaks); pointer intents
`onChartPointerDown/Drag/Up(ChartPointerEvent)` carrying the painted `TabLaneGeometry` (new
public header `chart/chart_pointer.h`; editor-core now links common::ui) plus
`onChartCaretMoveRequested(direction, fine)`; the whole gesture policy lives in the controller
(glyph press selects per the 40-Q3 grammar; empty click seeks + deselects + places the caret;
empty drag past a 4px threshold marquees, Shift extends the box; Alt reserved for Phase 4;
selection cleared at the one loadSessionSong commit seam). `fineGridPositionForBeat` moved from
editor/ui to editor-core tempo_grid_geometry (ui delegates) so caret stepping shares the one
fine-grid authority. `TabView` claims its band via hitTest/wantsPointerAt only while a chart
shows (cursor-overlay pass-through extended; empty-lane seeks preserved through the controller),
forwards lane-local events through a callback, and renders the overlays (accent selection
halos, lane-height caret with serifs, translucent marquee) above the shared notation — never in
the paint core. Arrow keys (Ctrl = fine) land in `EditorView::keyPressed` as interim scattered
keybinds recorded for plan 46. Tests: controller gesture policy (select/toggle/extend/collapse,
tail clicks, empty-click seek+caret, marquee replace+extend, caret grid/fine/string stepping
with clamps, selection cleared on reload), pure hit-testing (head-over-tail, overlap
resolution, zoom extremes 4–400 px/s, marquee boxes), selection-key resolution, TabView wiring
(pointer forwarding with geometry/modifiers, chart-gated transparency) and overlay rendering
smoke. Exit criteria met: notes selectable by click/marquee/keyboard, caret visible and
navigable, seek works on empty space, zero chart mutations.
**Phase 4 substantially complete 2026-07-16 — the first chart mutations are live.** One uniform
edit machinery in editor-core `src/chart/chart_edits.{h,cpp}`: pure planners build the intended
note stream, apply the 40-Q2-B normalization (any sustain ringing across the next same-string
onset truncates to exact adjacency, bend/slide payloads clipped with it), and diff into a
removed/inserted `ChartNotesEditPlan`; `ChartNotesEdit` replays the plan in either direction
through `Session::currentChart()` (revision bump → every projection rebuilds), so apply, undo,
and redo are the same primitive and truncations ride the same single undo entry by construction.
Shipped verbs: **Alt+click/press-drag-release inserts** at the snapped release point carrying
the last-used fret (occupied slot = replace); **Delete removes the selection** as one compound
entry (Delete precedence: automation point → chart selection → tone region); **typed digits
retype the selection's fret** (multi-digit entry window, clamped to g_max_fret; each keystroke
applies immediately — one undo entry per keystroke is a recorded rough edge for the polish
pass); **arrow keys nudge the selection** by the grid step (Ctrl fine; refused-not-clamped on
neck edges and occupied slots) and move the caret only when nothing is selected;
**Shift+Left/Right and Alt+wheel resize sustains** (floor at zero, growth clamps per Q2-B;
Ctrl steps the fine grid); **Escape cancels the in-flight marquee**. Selection follows every
edit under the new keys; dirty tracking rides the existing clean marker; save persists through
the Phase 2 write-on-save pipeline. Tests: insert/undo/redo round trips, insert-with-truncation
restored by ONE undo, compound delete, multi-digit fret combining, sustain clamp-and-floor,
nudge collisions refused, plus the Phase 3 suites re-verified against the nudge semantics.
**Keyboard grammar amended 2026-07-16** (user decision; full record in
docs/plans/in-progress/editing-interaction-model.md): plain keys never mutate — plain arrows now
navigate the caret unconditionally, **Alt+arrows** moves the selection (new
`onChartSelectionMoveRequested` intent), **Shift+Alt+Left/Right** resizes sustains (Shift+arrows
is reserved for future selection extension), and the automation lanes' plain-arrow point nudge
gained the same Alt requirement so the rule holds on every surface. Sustain **tail-drag is
parked** behind a watch-item trigger (docs/tracking/watch-items.md "Chart editing"); plain-wheel
zoom affirmed over modifier-gated zoom; timeline panning bindings (Shift+wheel / middle-drag)
recorded as an open deferred decision in the model doc.
**UX feedback round one applied 2026-07-17** (user feedback on the shipped verbs): (1) the caret
no longer follows glyph clicks or Alt-inserts — the selection highlight is the whole feedback,
and the caret appears only where placed (empty clicks, keyboard); (2) the selection highlight
recolors the note head's OWN border ring in the accent (stroke retracing fillHeadShape's ring
band; matching diamond for harmonics) instead of drawing an outer halo; (3) the numpad types
frets alongside the number row; (4) multi-digit fret entry now coalesces into ONE undo entry —
a digit inside the 1.5s window widens the just-pushed entry in place via the new
`EditorUndoHistory::replaceTop` (guarded: refuses when the cursor left the top entry or a save's
clean marker sits on it, degrading to a fresh push), the window opens whenever a second digit
could still fit under `g_max_fret` (so first digits 1–3), and typing after the window expires
starts a fresh value. Also fixed in passing: edits now update the selection as (selection −
removed keys) + inserted keys, so retyping/resizing a mixed selection no longer shrinks it to
only the notes that changed.
**UX feedback round two applied 2026-07-17**: the editing-caret concept was removed entirely —
the playhead is the ONE position concept; plain Left/Right now step the timeline cursor to the
adjacent grid line (Ctrl fine) via the renamed `onChartCursorStepRequested` intent, plain
Up/Down are unbound, and `ChartCaret`/`ChartCaretViewState` and the caret overlay are gone. The
selection highlight's ring stroke doubled in width (two ring-widths, centered on the ring band)
so a lit border reads at a glance.
**Alt ghost preview landed 2026-07-17** (user asked for it directly): hovering the lane with Alt
held shows the `CopyingCursor` and a translucent ghost head — snapped through the same
`musicalGridPositionForX` seam the committed insert uses (Ctrl bypasses to the fine grid),
labeled with the fret the insert would carry (`ChartEditViewState::insert_fret`, published from
the controller's last-used fret), on the hovered lane, tracking Alt-drags, cleared on
Alt-release/exit (JUCE's synthetic modifier-change mouse-move keeps it live without pointer
motion). `TabView` now takes the state's tempo map by reference (the ToneTrackView ghost
precedent) and repaints only the strips the ghost moves between.
**UX feedback round three applied 2026-07-17**: (1) ghost repaint shards fixed — the ghost's
invalidation had reused the one-pixel playhead helper, clipping the incoming ghost and leaving
fragments of the outgoing one; the lane now invalidates the ghost's real footprint. (2) The
full-color labeled ghost was initially kept over a plain white ring, and (3) the selection ring
moved outward with its inner edge at the head edge — both revised next round.
**UX feedback round four applied 2026-07-17**: (1) the ghost became a faded white ring the shape
of the head a click would insert — the user reasoned the full ghost's fret-0 label previews a
note nobody keeps (the real fret gets typed right after), and a colored/numbered ghost would only
earn its keep if inserts could target a specific fret, which no intuitive hotkey shape supports;
this deleted the `ChartEditViewState::insert_fret` plumbing that existed only for the label.
(2) The selection ring came in by one border-width — the outward cut visually buried the accent
glow — and now straddles the head edge, sitting between the head's own border and the glow so
accent stays readable on selected notes. Also hardened in passing: the ghost pixel test had
probed content the lane always paints, at a hover x that mapped to an exact tie between grid
lines; it now hovers unambiguously and probes the ring's stroke band off the string line.
**UX feedback round five applied 2026-07-17**: the selection ring stroke thinned from two
border-widths to one and a half (still edge-centered) — the user confirmed the accent glow now
reads on selected notes — and the ghost ring adopted the selection ring's exact dimensions via a
shared stroke-width lambda in TabView::paint, one authority for both edge-straddling rings (the
ghost had stroked a fixed 1.5px regardless of head size). The overlay test's ring probe became a
selected-vs-unselected render difference, stable through further stroke tuning.
**Remaining Phase 4 sub-scope (deferred to the next execution slice, before Phase 5):** pointer
drag-move of selected notes (horizontal with snap, vertical across strings — the same plain
move-drag verb automation points use) and Esc canceling an in-flight pointer drag preview.

Open questions Q1–Q4 below have recommended defaults and are mirrored into
`docs/plans/roadmap/00-roadmap.md` (Decisions needed). Phases 1–3 depend on none of them; later phases
state which answer they assume. The mid-sustain vibrato-span sub-scope (Phase 7) is gated on
`docs/plans/roadmap/10-format-versioning-and-chart-identity.md`.

## Goal

Full authorability of everything the chart format supports, inside the existing 2D tab lane over
the waveform: insert/delete/move notes and sustains; every technique (attack, palm/full mutes,
natural/pinch harmonics plus fractional `touch`, vibrato, tremolo, accent); bend curves and slide
waypoints; chord templates with per-string fingerings and a template editor; shape spans (chords,
chugs, arpeggios); fret-hand positions; section markers; tuning/capo/centOffset. Plus the editing
substrate that makes it usable: a selection model, an editing caret, copy/paste, multi-edit, the
settled "L links notes" merge command, and full undo integration. Outcome: a charter can produce
a complete, valid chart in the editor — today charts are read-only and enter only via GP import.

## Non-goals

- Tempo-map authoring (tap tempo, anchor drag, signature editing) — `docs/plans/roadmap/41-tempo-map-authoring.md`.
- The validation rule set itself — `docs/plans/roadmap/42-chart-validation.md` owns the rules; this plan
  owns the editor's consumption surface (live feedback, problems display).
- Song metadata/art editing — `docs/plans/roadmap/43-song-information-and-art.md`.
- 3D preview — `docs/plans/roadmap/44-editor-3d-preview.md`.
- Chart importers beyond what exists (GP shipped; RS XML import is future work per
  `docs/plans/in-progress/note-format-and-tablature-plan.md`).
- New format capabilities beyond the already-decided vibrato spans; whammy and other sketches in
  the note-format plan stay future, routed through `docs/plans/roadmap/10-format-versioning-and-chart-identity.md`.
- Raising `g_max_chart_strings` above 8 — coordinated by `docs/plans/roadmap/45-editor-theme-and-string-colors.md`.
- Playback-follow changes; the smooth-scroll evaluation stays the user's pending call
  (`docs/plans/todo/smooth-scroll-follow-evaluation.md`).

## Constraints

Applicable subset of the roadmap constraint block:

- (a) **Layering**: common never depends on editor or game code. Anything both products need
  (grid-position arithmetic, validation rules) lands in `rock-hero-common/core` first, as its own
  phase with tests. Tracktion headers stay isolated to `rock-hero-common/audio` implementation
  files (chart editing never touches audio adapters).
- (b) **Public-header minimalism**: edit primitives and IEdit objects stay in editor-core `src/`
  headers like `rock-hero-editor/core/src/tone/tone_region_edits.h` does today; only view-state
  and controller-intent surfaces are public, per `docs/design/architectural-principles.md`
  ("Ports and Adapters", "Library Roots Hold Folders Only").
- (c) **Naming firewall**: the reference real-guitar game is never named; "RS" or neutral phrasing
  only. Charter (BSD 3-Clause) may be named and is a capabilities reference only — no blind ports.
- (d) **Derived over authored**: difficulty is never authored; the editor persists the inputs the
  calculator needs (fingerings, FHPs, techniques) but no rating fields.
- (f) **Undo**: every edit integrates with the RockHero-owned undo history in editor-core
  (`rock-hero-editor/core/src/controller/editor_undo_history.h`); Tracktion is never the product
  undo stack.
- (h) **Builds**: all verification through `.agents/rockhero-build.ps1` (usage in
  `.agents/README.md`); intermediate phases run only the checks their changes warrant; the final
  acceptance phase is the sanctioned bundle as separate invocations.

Design-doc anchors: `docs/design/architectural-principles.md` — "Core Position" (editor-core is
headless MVC, views send intents), "Separate State From Side Effects", "Preferred Kinds of Tests"
(pure unit tests first); `docs/design/architecture.md` — "Arrangement Display Design", "Editor UI".

## Current state inventory

- **Chart model** (`rock-hero-common/core/include/rock_hero/common/core/chart/chart.h`): `Chart`
  = `ChartTuning` (strings[], capo, cent_offset) + `ChordTemplate` table (name, per-string
  optional frets/fingers) + `ChartNote` stream (GridPosition, string, fret, sustain Fraction,
  attack enum Pick/Hammer/Pull/Tap/Pop/Slap, mute None/Palm/Full, harmonic None/Natural/Pinch,
  optional `touch` double, vibrato/tremolo/accent bools, bend points, slide waypoints) +
  `ChartShape` spans (position, sustain, template index) + `FretHandPosition` (fret, width) +
  `ChartSection` (position, type string). Positions are exact rational grid tokens, never seconds.
- **Rules** (`chart/chart_rules.h`): `validateChartRules(chart, tempo_map)` enforces the
  corpus-validated set (sorted (position,string) notes, no duplicate onsets, payload windows,
  template arity, shape references). `g_max_chart_strings = 8` (line 24), `g_max_fret = 30`
  (line 33). No rule computes sustain endpoints; same-string sustain overlap is unvalidated
  (open question recorded in `docs/plans/in-progress/note-format-and-tablature-plan.md`).
- **Document IO** (`chart/chart_document.h`): `readChartDocument` (line 33), `chartDocumentText`
  (line 40), `writeChartDocument` (line 48) all exist — the writer is already built and used by
  the GP importer (`rock-hero-editor/core/src/project/gp_song_importer.cpp`).
- **Charts are read-only in-session today.**
  `rock-hero-common/core/include/rock_hero/common/core/song/arrangement.h:83-96`: `chart_ref` +
  `std::optional<Chart> chart`; the doc comment states saves validate presence but "do not
  rewrite it until chart editing exists".
  `rock-hero-common/core/src/package/rock_song_package_write.cpp:454-457` validates the chart
  document on disk without writing it. The tab projection cache in
  `rock-hero-editor/core/src/controller/editor_controller_impl.h:476-479` is keyed by arrangement
  id explicitly "because charts are immutable while a project is open".
- **Projection and rendering**: `rock-hero-editor/core/src/tab/tab_projection.cpp`
  (`makeTabViewState`) resolves the chart to seconds through the tempo map into
  `rock-hero-editor/core/include/rock_hero/editor/core/tab/tab_view_state.h`; the JUCE renderer
  `rock-hero-editor/ui/src/tab/tab_view.{h,cpp}` draws notes, sustains, techniques, bends,
  slides (with linked-appearance heads at waypoints), chord pills, and shape spans. `TabView`
  "ignores pointer events" (tab_view.h:106) — no mouse handling exists in the tab lane.
  **Sections are stored and serialized but not projected or rendered** (no `sections` in
  `tab_view_state.h` or `tab_projection.cpp`).
- **Interaction today**: `rock-hero-editor/ui/src/timeline/cursor_overlay.cpp:92-108` converts
  lane clicks into `onTimelineSeekRequested`; `EditorView::keyPressed`
  (`rock-hero-editor/ui/src/main_window/editor_view.cpp:620`) handles undo/redo/space locally.
  No chart intents exist on
  `rock-hero-editor/core/include/rock_hero/editor/core/controller/i_editor_controller.h` (tone
  intents at lines 153-194 are the closest pattern) and no chart entries exist in
  `editor_action_id.h`.
- **Undo**: `editor_undo_history.h` — polymorphic `IEdit` (line 96) with two-phase
  begin/commit/abort `EditorUndoHistory` (line 280), clean-marker dirty tracking; concrete edit
  pattern in `rock-hero-editor/core/src/tone/tone_region_edits.h` (inverse-command structs
  applied through `EditorEditContext`).
- **Grid**: `rock-hero-editor/core/include/rock_hero/editor/core/timeline/tempo_grid_geometry.h`
  provides pure grid-line geometry and snap lookup driven by a note-value Fraction (editor
  default 1/4); `timeline_geometry.h` maps pixels to timeline seconds.
- **Corpus**: 39 local `.rock` packages with 135 linked charts and a 101-file GP corpus load
  through the production reader (local-only converted commercial content, never committed).

Verified against code on 2026-07-06, refactor @ 3c7febe0.

## Dependencies

- `docs/plans/in-progress/tone-track-tempo-map-plan.md` — active tone work with uncommitted editor-core
  changes in flight. **Do not start Phase 2 until that work is committed**; both touch
  `editor_controller_impl.h` and the handler-TU layout.
- `docs/plans/roadmap/41-tempo-map-authoring.md` — gates the *from-scratch* charting promise only. Every
  phase here is executable against imported packages (GP import already builds tempo maps); the
  roadmap should sequence 41 before "author a chart from bare audio" is claimed as done.
- `docs/plans/roadmap/42-chart-validation.md` — Phase 10 consumes its rule set for live feedback; the
  same-string-overlap rule (Q2) is co-owned: this plan sets the edit-time semantics, 42 flags
  residual violations from imports.
- `docs/plans/roadmap/10-format-versioning-and-chart-identity.md` — the vibrato-span format change
  (decided 2026-07-06 in the note-format plan, "lands with the next format touch") must route
  through 10's versioning policy before Phase 7's vibrato sub-scope executes. Note: every chart
  edit changes the chart-identity hash 10 defines; that is correct behavior (a different chart is
  a different chart) and needs no coordination beyond awareness.
- `docs/plans/roadmap/45-editor-theme-and-string-colors.md` — when the shared string-color palette
  definition is extracted, the tab renderer consumes it; no ordering constraint either way.
- `docs/plans/roadmap/46-editor-keybinds.md` — chart editing adds many shortcuts. Interim policy: new
  keys follow the existing local `EditorView::keyPressed` pattern and are recorded in one table
  (Phase 3) that 46 ingests; migration to the central registry is 46's work, not this plan's.
- `docs/plans/in-progress/editing-interaction-model.md` — the settled editor-wide interaction grammar
  (modifier vocabulary, verb table, snapping, cursor/ghost feedback). Binding for every gesture
  this plan adds; the tone track and automation lanes already implement it.
- `docs/plans/roadmap/11-derived-difficulty-calculator.md` — downstream consumer only: fingerings, FHPs,
  and techniques authored here are its inputs. No gate.
- `docs/plans/roadmap/52-range-edit-operations.md` — decision-gated (G52-RANGE-EDIT) sibling: the plan
  47 time selection doubling as an edit range (range copy/cut/paste/delete carrying all four
  chart streams, paste-overwrite at the cursor, signature-aware rebase). It depends on Phases
  3–4 here and shares ONE clipboard codec with Phase 9 (coordination note there); Phase 9's
  note-selection copy/paste scope is otherwise untouched.
- No sub-plans are registered at this time (see Q4).

## Decisions already made

Restated from `docs/plans/in-progress/note-format-and-tablature-plan.md` (the source for all
format-side decisions) and the design docs — a fresh session needs no other context:

1. **One true tab per arrangement.** No difficulty levels, no phrase-max-difficulty machinery;
   difficulty is a derived rating, never authored (note-format plan, "One true tab per
   arrangement"; constraint (d)).
2. **One physical onset = one note; no link-next in the format.** Techniques that evolve during a
   sustain are positioned payloads inside the note (bend points, slide waypoints, vibrato spans
   when they land). Linked *rendering* is derived, never stored (note-format plan, "One physical
   onset = one note event").
3. **L is an editor merge command over payload storage** (note-format plan, "Linking is an editor
   command"): pressing L on the selected note merges it into its same-string predecessor — same
   fret extends the predecessor's sustain and absorbs the note's techniques as positioned
   payloads; different fret appends a pitched slide waypoint at the note's onset offset
   (unpitched slides stay explicitly authored). Hammer-on/pull-off/tap are never link targets.
   Split/unlink is the inverse command and synthesizes an attack at the seam (editor policy, not
   format). Segments between payload boundaries are synthesized view entities: each discrete
   mid-sustain change point draws a clickable linked-appearance head (the renderer already does
   this for slide waypoints); continuous payloads edit as curve handles, not heads.
4. **Chords are templates plus shape spans, never chord events.** The template table is
   per-arrangement; notes are the sounding truth; shapes add the notation layer. No stored
   arpeggio flag — chord-box vs arpeggio-bracket rendering derives from whether the span's notes
   arrive together or sequentially (note-format plan, "Format Direction" and "Deliberately
   absent").
5. **Technique fields are optional with defaults; positions are exact rationals** extending the
   tempo-map token grammar; durations are beat fractions; payload offsets are note-relative.
6. **Vibrato spans** are decided format work: bool-or-spans with canonical-uniqueness rules,
   landing with the next format touch (note-format plan, "Mid-sustain vibrato spans"); tremolo
   stays whole-note (re-picking = new onsets).
7. **Undo is the RockHero-owned editor-core history** (constraint (f);
   `docs/plans/completed/editor-undo` records the settled design; the shipped mechanism is
   `EditorUndoHistory` + `IEdit`). Chart edits are pure-data edits: inverse-command or
   before/after-snapshot `IEdit` objects both satisfy it; no Tracktion involvement.
8. **Charter is a capabilities reference only.** Its notation inventory calibrated the format;
   its editing UX informs but never dictates ours (note-format plan, "Reference: Charter (BSD 3-Clause)").
9. **Save == publish validation; normalize, don't reject** (established invariant, see
   `docs/plans/roadmap/43-song-information-and-art.md` for the export-gate tension — not this plan's to
   resolve). Edit primitives therefore keep the chart valid at every commit point rather than
   letting invalid states reach save.

## Open questions for the user

- **Q1 — Arpeggio handling: shape-span mechanism vs dedicated arpeggio editor.**
  Options: (A) no dedicated editor — the shapes editor (Phase 8) covers arpeggios because the
  format already derives arpeggio rendering from note arrival under the span; (B) a dedicated
  arpeggio authoring mode (pick a template, click a rhythm, notes generated); (C) a stored
  arpeggio flag on templates (format change through plan 10). **Recommendation: A.** B authors
  what is derivable and adds a mode; C reintroduces a flag the format deliberately dropped.
  Revisit B as a convenience generator later if authoring pain proves real.
- **Q2 — Same-string sustain overlap semantics on edit** (open in the note-format plan).
  Options: (A) reject the edit; (B) auto-truncate the earlier note's sustain to the new onset,
  captured in the same undo entry; (C) allow overlap and only warn. **Recommendation: B** — it
  matches GP's editing feel, keeps every commit point valid (decision 9), and the truncation is
  visible and undoable. 42 additionally flags residual overlaps found in imported charts.
- **Q3 — Click semantics in the tab lane.** **SETTLED 2026-07-09** by the editor-wide
  interaction model (`docs/plans/in-progress/editing-interaction-model.md`), which extends the
  recommended outcome A: glyph click selects, empty click seeks + deselects, empty drag marquees,
  Alt+click/Alt+drag is the insert quasimode (works on occupied strips too), Ctrl bypasses grid
  snap to the 1/960 fine grid, Shift extends selection / axis-locks drags, Esc cancels an
  in-flight gesture. Phases 3–8 follow that document's verb grammar; do not re-derive gestures
  locally.
- **Q4 — Sub-plan registration.** This plan fits the line cap by keeping phases terse. Options:
  (A) execute as one plan; (B) split the deep-UI phases into registered sub-plans
  `docs/plans/roadmap/40a-chord-template-and-shape-editor.md` (Phase 8) and
  `docs/plans/roadmap/40b-curve-payload-editors.md` (Phase 7) if execution shows a phase exceeding a
  session. **Recommendation: A now, B on demand** — the roadmap should note 40a/40b as reserved
  names so a later split does not renumber anything.

## Phased implementation

Common command forms used below (from `.agents/README.md`; run from the repo root):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets clang-tidy
```

### Phase 1 — Grid-position arithmetic in common/core

- **Scope**: pure functions beside the chart rules: advance a `GridPosition` by a beat Fraction
  (carrying across beats/measures via the tempo map's signature data, the same axis
  `tab_projection.cpp` already uses through `globalBeatIndex`), distance between two positions in
  beats, sustain endpoint of a note, and snap-to-grid for a note-value Fraction. These are needed
  by the L-merge command, the overlap policy, marquee hit ranges, and 42's overlap rule — both
  products' code, so common-first per constraint (a).
- **Files**: new `rock-hero-common/core/include/rock_hero/common/core/chart/` (or `timeline/`)
  header + `src/` TU; placement per the feature-folder rules in
  `docs/design/architectural-principles.md` ("Feature Folders", "Placement Procedure").
- **Public-header impact**: one new common/core header; no existing header changes.
- **Testing**: Catch2 pure unit tests in `rock-hero-common/core/tests/`: cross-measure carries
  under signature changes, identity round-trips (advance then distance), snap ties, tuplet
  fractions (corpus uses /5 /7 /9 /12 denominators).
- **Exit criteria**: endpoint arithmetic exists with tests; nothing else changed.
- **Verification**: `-Targets all`, then `-RunTouchedTests`, then `-Targets clang-tidy` (new
  public header).

### Phase 2 — Mutable-chart pipeline: write-on-save and cache invalidation

- **Scope**: end the charts-are-immutable era before any edit exists. (1) Save/publish serializes
  the in-memory `Chart` through `writeChartDocument` instead of only validating the file on disk;
  the stale comments at `arrangement.h:94` and `rock_song_package_write.cpp:455` are updated.
  (2) A chart revision counter (session- or controller-owned) joins the tab-projection cache key
  at `editor_controller_impl.h:476-479` so edits invalidate the memoized `TabViewState`.
  (3) A no-op-edit save must remain byte-stable: writing an unedited loaded chart reproduces an
  equivalent document (field order and token formatting come from `chartDocumentText`, which the
  GP importer already exercises). Sequence after the in-flight tone work commits (Dependencies).
- **Files**: `rock-hero-common/core/src/package/rock_song_package_write.cpp`,
  `rock-hero-common/core/include/rock_hero/common/core/song/arrangement.h` (comment only),
  `rock-hero-editor/core/src/controller/editor_controller_impl.h` + `editor_controller.cpp`,
  `rock-hero-editor/core/src/project/project_io.cpp` as needed.
- **Public-header impact**: none beyond the arrangement.h doc comment.
- **Testing**: common/core package test — load a corpus-shaped fixture package, mutate the chart
  in memory, save, reload, compare equal; save-without-edit round-trip stability test; editor-core
  test that bumping the revision rebuilds the projection.
- **Exit criteria**: an in-memory chart mutation survives save/reload; unedited saves are stable;
  projection cache honors revisions.
- **Verification**: `-Targets all`, then `-RunTouchedTests`.

### Phase 3 — Selection model, caret, and pointer input in the tab lane

- **Scope**: the interaction substrate. Selection state lives headless in editor-core: notes are
  keyed by (position, string) — unique by validation, stable across unrelated edits; shapes,
  FHPs, and sections by (kind, position). An editing caret = grid-snapped position + string lane,
  moved by arrows (left/right by grid step, up/down by string) and mouse. `TabView` gains
  hit-testing that maps pixels to (seconds, lane) via the existing `timeline_geometry` /
  `tempo_grid_geometry` seams and forwards intents (`onChartPointerDown(...)`-shaped, mirroring
  the tone intents at `i_editor_controller.h:153-194`); the controller resolves hits against the
  seconds-resolved `TabViewState` so hit policy is testable without JUCE. Gestures per the settled
  interaction model (Q3): glyph hit selects (Ctrl+click toggles membership, Shift+click extends),
  empty click seeks and deselects, empty drag marquees; Alt stays reserved for Phase 4's
  insertion quasimode. Selection and caret render as overlays in `TabView`. New shortcuts start
  the interim keybind table handed to `docs/plans/roadmap/46-editor-keybinds.md`.
  **Coordination with docs/plans/roadmap/30-game-2d-tab-view.md** (30-Q2, ANSWERED 2026-07-11):
  plan 30 Phase 2 moves the tab-lane *painting* into a shared common/ui paint core with a
  layout manifest and runs BEFORE this phase (decided while `TabView` is still
  interaction-free). This phase's selection/caret/ghost visuals are editor-shell overlays drawn
  ABOVE the notation and never enter the shared core; its hit-testing consumes plan 30's layout
  manifest instead of duplicating glyph geometry.
- **Files**: new editor-core `src/chart/` folder (selection state, hit resolution),
  `i_editor_controller.h`, `editor_view_state.h`, `editor_action_id.h` + availability,
  `tab_view.{h,cpp}`, `cursor_overlay.cpp` (yield to glyph hits per the settled Q3 gestures).
- **Public-header impact**: new controller intents and view-state fields (editor-core public
  includes); selection/caret types kept in `src/` headers where possible per constraint (b).
- **Testing**: editor-core unit tests for hit resolution (glyph vs empty, overlapping sustains,
  zoom extremes), selection transitions, caret snapping; one UI wiring test per the pyramid in
  `docs/design/architectural-principles.md` ("Selective UI Wiring Tests").
- **Exit criteria**: notes selectable by click/marquee/keyboard; caret visible and navigable;
  seek still works on empty space; zero chart mutations yet.
- **Verification**: `-Targets all`, then `-RunTouchedTests`, then `-Targets clang-tidy`.

### Phase 4 — Note insert, delete, move, and sustain editing with undo

- **Scope**: the first mutations. Edit primitives in editor-core (`src/chart/chart_edits.{h,cpp}`
  modeled on `tone_region_edits.h`): insert preserving the (position, string) sort, delete,
  move (position and/or string), set fret, set sustain — each returning the information its
  `IEdit` needs. Overlap policy per Q2-B: an insert/move/sustain-extend that collides
  auto-truncates the earlier note inside the same compound undo entry. Input per the interaction
  model: digit keys type fret numbers at the caret (multi-digit entry window, clamped to
  `g_max_fret`), Alt+click pencil placement on a string lane at the grid snap (Ctrl bypasses to
  the fine grid; ghost preview + `CopyingCursor` while Alt is held), drag-move with snap
  (Shift axis-locks), sustain editing via tail drag, Alt+wheel, and Shift+Left/Right by grid
  step, delete key, Esc cancels an in-flight gesture. All edits push through
  `EditorUndoHistory::push`; dirty tracking then works via the existing clean marker.
- **Files**: editor-core `src/chart/` (edits + handlers TU following the per-domain handler
  pattern), `editor_action_id.h`, availability, `tab_view.cpp` gestures.
- **Public-header impact**: new intents only.
- **Testing**: primitive tests (sort invariant, duplicate-onset refusal, Q2 truncation), undo
  round-trip tests (apply → undo → chart equals original by `operator==`), redo equivalence,
  compound-entry atomicity.
- **Exit criteria**: with a chart loaded, notes can be created, moved, resized, and deleted;
  every path is undoable; saves persist the result (Phase 2). Arrangements without a chart get a
  minimal "create empty chart" action (tuning seeded from a default) so editing has an entry
  point ahead of `docs/plans/roadmap/41-tempo-map-authoring.md`.
- **Verification**: `-Targets all`, then `-RunTouchedTests`.

### Phase 5 — Technique and note-property editing

- **Scope**: attack (pick/hammer/pull/tap/pop/slap), mute (none/palm/full), harmonic
  (none/natural/pinch) with optional `touch` numeric entry, vibrato (whole-note bool until
  Phase 7's gated sub-scope), tremolo, accent. Shortcuts cycle or toggle on the selection; a
  context menu and a small note-properties strip expose the same intents (single source of truth
  in the controller). Applying a property to an N-note selection is one compound undo entry —
  the first multi-edit lands here.
- **Files**: editor-core `src/chart/` edits/handlers; `tab_view.cpp` (context menu), possibly a
  small properties component under `rock-hero-editor/ui/src/tab/`.
- **Public-header impact**: intents + view-state only.
- **Testing**: per-technique set/unset round-trips; illegal-combination guards (touch only with
  harmonic); multi-apply undo atomicity.
- **Exit criteria**: every `ChartNote` field authorable except bend/slides/vibrato-spans.
- **Verification**: `-Targets all`, then `-RunTouchedTests`.

### Phase 6 — L-link merge and split commands

- **Scope**: decision 3 verbatim. L on a selected note merges into its same-string predecessor:
  same fret → extend sustain, rebase and absorb the note's payloads (offsets shift by the
  predecessor-onset delta using Phase 1 arithmetic; a zero-sustain technique-carrying note is a
  pure payload boundary); different fret → append a pitched slide waypoint at the onset offset.
  Refuse when the target's attack is hammer/pull/tap (never link targets) with a status message.
  Split places the caret's grid position as the seam: the tail becomes a new note with a
  synthesized plain-pick attack, payloads re-partitioned and rebased. Both are single undo
  entries and exact inverses in the common cases. Rendering already draws linked-appearance heads
  at slide waypoints; extend the same treatment to any future payload-boundary kind.
- **Files**: editor-core `src/chart/` (merge/split primitives + edits), handlers, keybind table.
- **Public-header impact**: intents only.
- **Testing**: merge same-fret (sustain math, payload rebase, vibrato/tremolo absorption rules),
  merge different-fret (waypoint appended, ascending-offset invariant kept), HOPO-target refusal,
  split inverse property (merge then split at the seam restores the original two notes), corpus
  spot-check on GP-imported slide chains.
- **Exit criteria**: L and split work as specified and validate clean afterward.
- **Verification**: `-Targets all`, then `-RunTouchedTests`.

### Phase 7 — Curve payload editors: bends, slide waypoints, vibrato spans

- **Scope**: direct manipulation on the sustain tail. Bend points: add (Alt+click on the tail,
  per the interaction model), drag (offset horizontally with snap, semitones vertically in free
  granularity — 0.25 curls are already representable), numeric entry, remove; primitives enforce
  ascending offsets within the sustain. Slide waypoints: add/move/remove, toggle unpitched; strictly-positive ascending
  offsets ≤ sustain enforced. **Gated sub-scope (assumes plan 10's chart-format versioning
  outcome)**: vibrato spans per decision 6 — span handles on the tail with the
  canonical-uniqueness rules from the note-format plan; until 10 closes, vibrato stays the
  whole-note bool and this sub-scope is skipped without blocking the phase.
- **Files**: editor-core `src/chart/` primitives/edits; `tab_view.cpp` handle geometry (reusing
  the existing bend/slide drawing paths); `tab_view_state.h` gains handle metadata if hit-testing
  needs it.
- **Public-header impact**: possible `tab_view_state.h` additions (public editor-core header).
- **Testing**: primitive-window invariants; drag mapping tests (pixel → offset/semitone) as pure
  geometry; undo round-trips; GP-imported bend corpus spot-check.
- **Exit criteria**: bends and slides fully authorable; vibrato spans too once 10 closes.
- **Verification**: `-Targets all`, then `-RunTouchedTests`.

### Phase 8 — Chord templates, shapes, FHPs, and sections

- **Scope**: (1) Template editor dialog: name, per-string frets/fingers (arity fixed by the
  tuning's string count; thumb=0, 1-4 fingers), create/edit/duplicate; an explicit
  "remove unused templates" command rather than silent GC (silent GC would churn saved documents
  and the plan-10 identity hash for no user action). (2) Shape spans: create over a selection or
  by drag, resize with snap, re-point at a template, delete; template-reference integrity kept by
  the primitives. Arpeggios per Q1-A: no dedicated editor — sequential notes under a span already
  render as a bracket. (3) FHPs: place/move/delete markers, fret + width entry. (4) Sections:
  first project and render them (they are stored but invisible today — inventory), then
  add/rename/move/delete with a small type vocabulary that grows as needed. All undoable.
- **Files**: editor-core `src/chart/` + projection (`tab_projection.cpp`, `tab_view_state.h` gain
  sections), `tab_view.cpp`, new dialog components under `rock-hero-editor/ui/src/tab/` or
  `chart/`.
- **Public-header impact**: `tab_view_state.h` section views; intents.
- **Testing**: template arity/reference integrity, shape-span windows, unused-template command,
  section projection ordering; dialog logic kept headless-testable (state in editor-core).
- **Exit criteria**: every chart collection authorable end to end; sections visible.
- **Verification**: `-Targets all`, then `-RunTouchedTests`, then `-Targets clang-tidy` (new
  components).

### Phase 9 — Copy/paste, duplicate, transpose, and bulk edit

- **Scope**: clipboard holds a position-relative JSON fragment in the document grammar (notes,
  shapes, FHPs rebased to the selection's first onset); paste rebases at the caret with Q2-B
  collision handling; duplicate = copy+paste at selection end. Transpose: string up/down and
  fret +/- on the selection, refused (not clamped) when any note would leave the tuning/fret
  range — validation-preserving edits only. Bulk property edit generalizes Phase 5's multi-apply.
  Cross-arrangement paste within a project is allowed when string counts are compatible;
  otherwise refused with a message. **Single-codec coordination with
  docs/plans/roadmap/52-range-edit-operations.md** (time-range copy/paste over the plan 47
  selection, decision-gated): whichever plan executes first lands the clipboard payload codec;
  the later plan extends it — never two clipboard formats. Plan 52's signed answers to its
  Q2 (position basis across signature changes) and Q5 (OS-clipboard transport) bind this
  phase's codec when they exist.
- **Files**: editor-core `src/chart/` (clipboard codec reusing `chart_document` serialization
  helpers), handlers; `editor_view.cpp` keys.
- **Public-header impact**: intents only.
- **Testing**: copy/paste round-trip equality (including cross-measure sustains and payload
  rebasing), transpose refusal bounds, paste-collision truncation undo atomicity.
- **Exit criteria**: a riff can be duplicated across a song in seconds; all paths undoable.
- **Verification**: `-Targets all`, then `-RunTouchedTests`.

### Phase 10 — Tuning/capo/centOffset editing and live validation surface

- **Scope**: (1) Arrangement tuning dialog: per-string pitch names, capo, centOffset (cap ±1200
  cents as shipped), string count within `g_max_chart_strings` (raising the cap belongs to
  `docs/plans/roadmap/45-editor-theme-and-string-colors.md`). Adding strings is safe (templates pad);
  removing a string that carries notes requires explicit confirmation and deletes those notes in
  one compound undo entry; templates re-fit. (2) Live validation: after each committed edit,
  debounced `validateChartRules` (plus plan 42's extended rules when they land) runs off the hot
  path; results surface as a non-blocking problems indicator with jump-to-position. Save stays
  normalize-don't-reject (decision 9); whether publish gains a content gate is plan 42/43's
  question, not decided here.
- **Files**: editor-core `src/chart/` (tuning edits, validation scheduling), a tuning dialog
  component, `editor_view_state.h` problems summary.
- **Public-header impact**: view-state additions; intents.
- **Testing**: string add/remove compound undo, template re-fit arity, validation debounce logic
  (pure, fake scheduler per "Time Must Be a Dependency" in
  `docs/design/architectural-principles.md`), problems projection.
- **Exit criteria**: tuning fully authorable; edits report problems live; no save regression.
- **Verification**: `-Targets all`, then `-RunTouchedTests`.

## Final acceptance phase

Run the sanctioned bundle as separate invocations from the repo root, plus formatting:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets clang-tidy
pre-commit run --all-files
```

Acceptance additionally requires a local corpus smoke pass (never CI): open several of the 39
corpus packages, perform an edit of each phase's kind on a real chart, save, reopen, and confirm
`validateChartRules` passes and undo history behaves — the corpus is converted commercial
content and stays local-only.

## Rollback/abort notes

- **Phase 2 is the only phase that changes save behavior.** Its risk is corrupting real chart
  documents on save. Mitigations: the round-trip stability test gates it; the corpus backup
  discipline already in place (packages are regenerable from sources) covers user data during
  development. Abort = revert the write-path commit; the validate-only path is self-contained at
  `rock_song_package_write.cpp:454-457` and easy to restore.
- **Phase 3's cursor-overlay change** alters a shipped gesture (click-to-seek). Keep the yield
  logic in one place so reverting to seek-always is a one-line rollback if Q3's answer changes.
- **Phases 4-10 are additive editor-core/ui code** behind new intents; each phase reverts as a
  unit without touching earlier phases. Undo-entry classes are append-only — never repurpose an
  existing IEdit's semantics, so mid-plan rollbacks cannot corrupt histories.
- **Vibrato-span sub-scope (Phase 7)** must not start before plan 10 closes; if 10 changes the
  spelling, only the span primitives and handle UI are affected — bends/slides are independent.
- If any phase reveals the line-cap pressure Q4 anticipates, stop, register 40a/40b in
  `docs/plans/roadmap/00-roadmap.md`, and move the remaining scope there rather than compressing phases.
