# Plan 47 — Editor Loop Selection (shared loop-range backend + editor loop UX)

## 1. Status

Ready — 2026-07-07 — baseline `refactor @ 0ffb6efe`. Open questions Q1–Q3 below carry
recommendations and are mirrored into docs/roadmap/00-roadmap.md (Decisions needed); Phase 1 is
executable now and is coordinated with docs/roadmap/21-game-audio-engine-and-session.md Phase 1 and
docs/roadmap/28-practice-mode.md Phase 2 by the whichever-executes-first rule in Decisions below.

## 2. Goal

Looping works in both products. This plan owns (1) the shared loop-range backend on the common
transport port and (2) the editor experience: in the editor, looping is available all the time —
the charter click-drags over a time span, the selection snaps to the current grid density by
default, playback loops the selection, and the selection survives pause/seek/arrangement switches
until cleared. Game-facing looping UX (practice mode) is docs/roadmap/28-practice-mode.md, not this
plan; both products drive the one backend this plan lands.

## 3. Non-goals

- No playback speed factor. `setPlaybackSpeed` is docs/roadmap/21-game-audio-engine-and-session.md
  Phase 1 (typed-NotSupported) and docs/roadmap/28-practice-mode.md Phase 1 (functional).
- No practice-mode features: section-based loop pick, pre-roll, count-in, per-loop accuracy —
  all docs/roadmap/28-practice-mode.md.
- No note/marquee selection in the tab lane — docs/roadmap/40-chart-editing.md Phase 3 owns chart
  selection; this plan's time selection is a transport concept, deliberately on a different
  surface (Q1).
- No playback-follow changes. The shifted-window follow stays as shipped; the fixed-cursor
  smooth-scroll evaluation (docs/todo/smooth-scroll-follow-evaluation.md) is an owned, pending
  user decision — referenced here exactly as docs/roadmap/44-editor-3d-preview.md does, never
  decided or duplicated.
- No keybind centralization. Any key this plan ships lands in the existing scattered handling and
  migrates under docs/roadmap/46-editor-keybinds.md (stated explicitly in Phase 3).
- No IPlaybackClock work. The editor reads transport on the message thread today and does not
  depend on docs/roadmap/12-playback-clock.md; that plan's extrapolator loop-wrap snap rules apply
  to its future consumers, not to this plan.

## 4. Constraints

Applicable subset of the roadmap constraint block (docs/roadmap/00-roadmap.md):

- (a) **Layering**: common never depends on editor or game code. The loop-range API both products
  need lands in rock-hero-common/audio FIRST, as its own phase with tests, before any editor UI
  consumes it. Tracktion headers stay isolated to rock-hero-common/audio implementation files.
- (b) **Public-header minimalism**: the port gains the minimal loop surface; editor selection
  types stay in editor-core headers, UI components stay in `src/` per
  docs/design/architectural-principles.md ("Ports and Adapters", "Placement Procedure for New
  Files").
- (c) **NAMING FIREWALL**: the commercial real-guitar game that inspired this project is never
  named in any file; use "RS" or neutral phrasing. Charter (BSD 3-Clause) may be named.
- (f) **Undo**: undo is RockHero-owned full-state mementos over document state. The loop
  selection is deliberately NOT part of the undo history — justified in Decisions below.
- (h) **Builds**: all build/test/lint commands go through `.agents/rockhero-build.ps1` (usage in
  `.agents/README.md`), never raw cmake/ctest/ninja. Intermediate phases run only the checks their
  changes determinately warrant; the final acceptance phase is the sanctioned bundle as separate
  invocations.

Binding design-doc rules: the audio thread is the single timing truth and threading stays at the
adapter boundary (docs/design/architecture.md "Timing and Latency";
docs/design/architectural-principles.md "Keep Threading at the Boundary"); views present state and
emit intents (docs/design/architectural-principles.md "UI Modules", "Core Position").

## 5. Current state inventory

Repo state (paths repo-relative):

- **No loop or speed API exists in common/audio public headers.** `ITransport` is
  play/pause/stop/seek plus message-thread `state()`/`position()` and a coarse listener
  (rock-hero-common/audio/include/rock_hero/common/audio/transport/i_transport.h:67-112).
  `Engine` multiply inherits the ports (engine/engine.h:57), with per-port TUs under
  rock-hero-common/audio/src/engine/ (`engine_transport.cpp`, `engine_song_audio.cpp`).
- `engine_song_audio.cpp:163` sets `transport.looping = false` inside `setActiveArrangement` —
  the Tracktion looping flag is already touched, always off.
- Grid machinery: editor-core owns pure grid geometry and snapping —
  `visibleTempoGridLines` / `nearestTempoGridTime` / `timelineCursorPlacementTime` driven by a
  note-value `Fraction` (rock-hero-editor/core/include/rock_hero/editor/core/timeline/
  tempo_grid_geometry.h:102-154); the editor default is the quarter-note grid and the per-project
  density persists via `IEditorSettings::projectGridNoteValueFor`
  (i_editor_settings.h:127-137; storage list key "projectGridNoteValues",
  rock-hero-editor/core/src/settings/editor_settings.cpp:323). `EditorViewState.grid_note_value`
  carries the active density (editor_view_state.h:244).
- **Snap-bypass modifier is already Ctrl, centralized**: `placementModeFor` maps Ctrl → Free,
  otherwise SnapToGrid, with the comment "One mapping for every timeline click site, so the
  snap-bypass modifier cannot drift" (rock-hero-editor/ui/src/timeline/timeline_cursor.cpp:69-75).
- Musical positions: `common::core::GridPosition` is measure:beat plus an exact sub-beat
  `Fraction`, serialized via the token grammar (`parseGridPositionToken` /
  `formatGridPositionToken`, rock-hero-common/core/include/rock_hero/common/core/chart/
  chart_tokens.h:26-33; type at chart/chart.h:25-64). `ToneGridPosition` is whole-beat only and
  documented as tone-region-scoped, "not a note/chart position model" (tone/tone_track.h:16-38).
  `TempoMap::secondsAtNote(measure, beat, offset)` converts musical → seconds
  (timeline/tempo_map.h:192).
- Editor state persistence homes (load-bearing for Q3): per-project view/resume state — the resume
  cursor, grid density, timeline zoom, AND the displayed arrangement — all live as app-local
  per-project-path records in `IEditorSettings`, keyed by normalized path under **flat settings
  keys** (`projectCursor:<path>`, `projectGridNoteValue:<path>`, `projectTimelineZoom:<path>`,
  `projectSelectedArrangement:<path>`; editor_settings.cpp). `project.json` is now only a
  `{ "formatVersion": 1 }` manifest — the `editorState` object and `ProjectEditorState` were
  removed (see docs/in-progress/app-local-project-view-state.md), so nothing per-project-view lives
  in the .rhp package.
- Interaction surfaces: `TimelineRuler` (pinned band above the canvas, height 53px) handles
  `mouseDown` only, converting clicks to snapped cursor placement
  (rock-hero-editor/ui/src/timeline/timeline_ruler.h:27,104-109, timeline_ruler.cpp:235).
  `CursorOverlay` spans the whole track canvas, converts lane clicks to
  `onTimelineSeekRequested`, hosts the shared `TimelineSnapGuide`, and has a hit-test
  pass-through so row targets beneath receive clicks (cursor_overlay.h:20-36,76-94,
  cursor_overlay.cpp:81-110; pass-through wired at
  rock-hero-editor/ui/src/main_window/editor_view.cpp:282).
- Just-landed tone UI gestures (commits f7752e8b/196e089c/0ffb6efe) live on `ToneTrackView`, a
  track-canvas row beneath the overlay pass-through: edge-drag with beat snap and snap guides,
  region select, boundary-move intents (rock-hero-editor/ui/src/tone/tone_track_view.cpp:209-329).
  **The ruler band hosts no tone gesture** — a ruler drag surface cannot collide with them.
- Keybinds are scattered: `EditorView::keyPressed` handles Ctrl+Z/Ctrl+Y/Space/Delete/Ctrl+T
  (editor_view.cpp:626-678), `MainWindow::keyPressed` forwards
  (main_window.cpp:101-114), and duplicated predicates sit in
  rock-hero-common/audio/src/tracktion/plugin_window.cpp:17-47 — centralization is
  docs/roadmap/46-editor-keybinds.md.
- Playback follow: `TrackViewport::followCursorWithWindowShifts` — "a cursor off-screen left
  snaps straight to the pin so playback is never running invisibly behind the view"
  (rock-hero-editor/ui/src/timeline/track_viewport.cpp:599-613).
- Fakes implementing `ITransport` that must update with any port change:
  rock-hero-common/audio/tests/test_transport.cpp:12,
  rock-hero-editor/core/tests/include/rock_hero/editor/core/testing/
  editor_controller_test_harness.h:313, rock-hero-editor/ui/tests/test_editor.cpp:41, and
  rock-hero-editor/ui/tests/include/rock_hero/editor/ui/testing/editor_view_test_harness.h:56.
- `EditorTheme` is the single color seam (accent already covers snap guides/selection borders;
  no loop colors exist) — rock-hero-editor/ui/src/shared/editor_theme.h:25-74.

Vendored Tracktion facts (source-verified by the juce-tracktion-expert agent; `[TE]` =
external/tracktion_engine/modules/tracktion_engine, `[TG]` =
external/tracktion_engine/modules/tracktion_graph/tracktion_graph):

- Loop range = `juce::CachedValue<TimePosition> loopPoint1, loopPoint2` on `TransportControl`
  ([TE]/playback/tracktion_TransportControl.h:384), bound with a **nullptr UndoManager**
  (.cpp:689-690) — persisted with the edit, never undoable; units are edit-timeline seconds.
  `looping` is a CachedValue bool (.h:387); direct assignment is the de facto API (engine code
  assigns it, .cpp:367,376; no setLooping exists) — our engine_song_audio.cpp:163 already does.
  `setLoopRange(TimeRange)` clamps into the edit (.cpp:1223-1229); `getLoopRange()` is
  order-insensitive (.cpp:1231-1234).
- **Wrap is sample-accurate modulo arithmetic** with block splitting at the boundary sample
  ([TG]/tracktion_PlayHead.h:312-342, 365-408; [TE]/playback/graph/
  tracktion_TracktionNodePlayer.h:87-105). No crossfade at the splice (WaveNode declick skipped
  on `isFirstBlockOfLoop`, [TE]/playback/graph/tracktion_WaveNode.cpp:1702-1703); plugin tails
  ring across the wrap (PluginNode all-notes-off only on jumps,
  tracktion_PluginNode.cpp:182-183).
- **Automation re-asserts at the wrapped position within the same device callback**: the
  post-wrap sub-block recomputes edit time and `AutomationIterator::setPosition` re-derives the
  value purely from the curve, walking backward correctly
  ([TE]/model/automation/tracktion_AutomatableParameter.cpp:1174-1214, 1745-1844). Rack plugins
  run through the same PluginNode path (tracktion_RackNode.cpp:397) — baked branch-gain tone
  automation is re-asserted at the wrap. Residual: per-plugin gain smoothing only (e.g.
  `VolumeAndPanPlugin` 0.05s ramp, [TE]/plugins/internal/tracktion_VolumeAndPan.h:76-82).
  Modifier-driven sources do NOT reposition (tracktion_AutomatableParameter.cpp:152-159) — our
  baked curves are on the safe side.
- **Live input monitoring is loop-agnostic by construction**: `WaveInputDeviceNode::process`
  reads a device-fed FIFO and never touches playhead or loop state
  ([TE]/playback/graph/tracktion_WaveInputDeviceNode.cpp:51-90); the automation re-assert above
  also re-asserts the tone the guitarist hears through the monitored chain.
- Live range edits reach the audio graph via a ~200 ms message-thread poll
  ([TE]/playback/tracktion_TransportControl.cpp:1099-1114; counter at .h:406). Engaging looping
  mid-play does not retrigger ([TG]/tracktion_PlayHead.h:260-271, clamp at 238-241). Play start
  with looping snaps the cursor into range and **refuses ranges < 0.01 s with a stock
  BubbleMessageComponent warning** (.cpp:1353-1371; [TE]/utilities/
  tracktion_UIBehaviour.cpp:59-72) — the port must make that path unreachable. Seeks while
  looping+playing are clamped into the range (.cpp:1704-1708). Toggling `looping` stops active
  recording (.cpp:267-274) — irrelevant here (the editor never records).

Verified against code on 2026-07-07, refactor @ 0ffb6efe.

## 6. Dependencies

- docs/roadmap/21-game-audio-engine-and-session.md — Phase 1 declares the same shared-port loop
  surface (possibly typed-NotSupported per its rollback note); Phase 3c(2) tests tone correctness
  across loop wraps. Coordinated by the whichever-executes-first rule (Decisions below).
- docs/roadmap/28-practice-mode.md — Deferred; its Phase 2 hardens game-side wrap behavior and its
  Phase 0 checkpoint e verifies `TransportControl` loop semantics. This plan executing first
  satisfies both implementation halves; 28 then extends tests only.
- docs/roadmap/12-playback-clock.md — no dependency either way. Its extrapolator already defines
  loop-wrap backward jumps as snap events (its Phase 4); the editor cursor path stays
  message-thread `ITransport::position()` (its open question 2, recommendation B).
- docs/roadmap/40-chart-editing.md — Phase 3 will add glyph-select/marquee pointer input in the tab
  lane; Q1 keeps this plan's drag surface off that lane. Phase 1 of plan 40 (grid-position
  arithmetic in common/core) overlaps the snap-to-musical helper here; coordination note in
  Phase 2.
- docs/roadmap/46-editor-keybinds.md — any key this plan ships (Escape clear, optional Ctrl+L)
  lands scattered in `EditorView::keyPressed` now, is recorded for plan 46's registry, and
  migrates when that plan executes. Ctrl+L does not collide with plan 46's tier A/B map (plain
  `L` stays reserved for link/slide).
- docs/roadmap/44-editor-3d-preview.md — shares only the follow-decision stance: the smooth-scroll
  evaluation (docs/todo/smooth-scroll-follow-evaluation.md) is the user's pending call;
  referenced, never decided (its non-goal 3 phrasing is the template for this plan's non-goal).
- docs/in-progress/tone-track-tempo-map-plan.md — active tone work; this plan touches none of its
  files, and the ruler surface avoids its gestures (inventory).

## 7. Decisions already made

Restated inline so a fresh session needs no other context:

1. **Whichever-executes-first coordination rule (stated here, binding on plans 21/28/47).** The
   shared loop surface is one implementation, landed once: whichever of
   docs/roadmap/21-game-audio-engine-and-session.md Phase 1 or THIS plan's Phase 1 executes first
   lands the port methods on
   rock-hero-common/audio/include/rock_hero/common/audio/transport/i_transport.h, the
   `transport/transport_error.h` error enum, the Tracktion-backed implementation in
   rock-hero-common/audio/src/engine/engine_transport.cpp, and the adapter tests in
   rock-hero-common/audio/tests/test_engine.cpp. The later plan re-verifies the landed surface
   and only extends — never re-declares, never double-implements. Because plan 21 is
   decision-gated, plan 28 is Deferred, and the editor wants looping now, **this plan is expected
   to execute first and lands the real Tracktion-backed loop (not typed-NotSupported)**; plan
   21 Phase 1 then inherits a functional loop and only adds the speed surface; plan 28 Phase 2's
   "implement the Tracktion-backed loop here if 21 shipped NotSupported" clause is satisfied and
   reduces to extending the wrap test matrix.
2. **The port speaks edit-timeline seconds; musical↔seconds conversion stays outside the
   adapter** — same split the tone work uses (regions musical, engine seconds); expert guidance
   and docs/design/architectural-principles.md "Ports and Adapters".
3. **The editor loop selection is stored as musical positions** (a `common::core::GridPosition`
   pair) and converted to seconds at the engine edge via `TempoMap::secondsAtNote`
   (tempo_map.h:192). Property this buys: musical positions survive tempo edits — when
   docs/roadmap/41-tempo-map-authoring.md lands tempo editing, a loop pinned to measures 17–21
   stays pinned to those measures; the controller re-converts and re-pushes on tempo-map change.
4. **The loop selection is NOT undo history** (constraint (f) justification): undo mementos
   capture document state; the selection never mutates Song/Chart/tone data, never dirties the
   project, and persists outside the package exactly like the resume cursor and zoom
   (i_editor_controller.h:114-121 doc comment establishes that category). Putting it in
   `EditorUndoHistory` would make Ctrl+Z appear to do nothing between document edits.
5. **Snap-bypass modifier is Ctrl** — already centralized for every timeline click site
   (timeline_cursor.cpp:69-75); this plan reuses `placementModeFor`, never a second mapping.
6. **Playback follow on wrap is already settled**: the shifted-window follow snaps when the
   cursor is off-screen left (track_viewport.cpp:599-613, adopted 2026-07-03 per
   docs/todo/smooth-scroll-follow-evaluation.md's record). A loop wrap is exactly that case when
   the loop start is out of view; when the whole loop is visible, the follow correctly leaves the
   window alone. No follow code changes; Phase 4 verifies, and the smooth-scroll evaluation
   remains the user's separate pending decision — reference and stop.
7. **The port enforces a minimum engaged-loop length of 0.1 s** with a typed error, so
   Tracktion's scattered minima (0.01 s play refusal with a stock bubble warning, 1 ms timer
   clamp, 50-sample floor) are never reachable and RockHero owns all user feedback (expert
   guidance; cites in inventory).
8. **Gapless engage sequence**: `setLoopRange` → `looping = true` → position inside range →
   play; `performPlay` pushes the range synchronously at play start
   (tracktion_TransportControl.cpp:1353-1362), avoiding the ~200 ms poll. Live brace drags during
   playback accept the ~200 ms propagation; the port never reaches past `TransportControl`.

## 8. Open questions for the user

Mirror all of these into docs/roadmap/00-roadmap.md (Decisions needed).

1. **Q1 — Drag surface for the time selection.** Options: (A) the timeline ruler band —
   click-drag across the ruler creates the selection; the track area keeps today's
   click-to-seek and stays free for docs/roadmap/40-chart-editing.md Phase 3's future
   glyph-select/marquee gestures; (B) the track area — closest to DAW muscle memory but collides
   head-on with plan 40's marquee (empty-lane drag cannot mean both) and with the tone row's
   shipped edge-drags; (C) A now, plus a track-area modifier-drag later — but note Alt-drag is
   no longer available: `docs/in-progress/editing-interaction-model.md` (settled 2026-07-09)
   assigns Alt to the insert quasimode and empty-lane drag to marquee, so any later track-area
   loop gesture must be negotiated within that model. **Recommendation: A, with C recorded as a
   possible follow-up** — the ruler is gesture-free today (mouseDown seek only), reads naturally
   as "time", and never conflicts with note editing.
2. **Q2 — Loop engagement semantics.** Options: (A) explicit toggle — a selection is passive
   until a loop toggle (Ctrl+L) arms it; (B) auto-on — a selection existing means playback loops
   it; clearing the selection (Escape, click-away) restores normal playback.
   **Recommendation: B** — on the ruler the selection has no purpose other than looping, so a
   separate armed state is a mode without a payoff. Sub-policies under B (accepted defaults
   unless overruled): Play with the cursor outside the selection snaps to loop start
   (Tracktion's verified performPlay behavior); pause inside the loop resumes in place (verified
   no-retrigger); a seek INSIDE the selection keeps the loop; a seek OUTSIDE the selection clears
   it (the click is the stronger intent, and it sidesteps Tracktion's confusing seek-clamp during
   looped play); arrangement switch keeps the selection (the tempo map is song-level) and
   re-engages after activation; tempo-map edits (future, plan 41) keep musical positions and
   re-convert. If (A) is chosen instead, Ctrl+L lands scattered now and migrates under plan 46.
3. **Q3 — Persistence home. SETTLED: app-local per-project-path records in `IEditorSettings`**,
   exactly like the resume cursor / grid / zoom / selected-arrangement. Those families are now
   stored as **flat settings keys** (`"<family>:" + normalizedPath`, one string value each —
   editor_settings.cpp), not the old list-of-records codecs.
   **Multi-scalar rule (binding on this family):** a family with several scalars encodes ONE
   composite token value under ONE key (following the grid's `"num/den"` precedent), never two keys
   whose writes could tear — so loop selection stores its two grid-position tokens as a single
   composite value via `formatGridPositionToken`. Storing the selection in project.json (former
   option B) is rejected: `editorState`/`ProjectEditorState` no longer exist, and per-project view
   state does not belong in the shared .rhp package.

## 9. Phased implementation

Command forms (from `.agents/README.md`, run from the repo root) referenced below:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets clang-tidy
```

### Phase 1 — Shared loop-region port and Tracktion adapter (common-first, constraint (a))

- **Scope**: extend `ITransport` with the loop surface both products share, implemented for real:
  - `[[nodiscard]] std::expected<void, TransportError> setLoopRegion(common::core::TimeRange)`,
    `void clearLoopRegion()`, `[[nodiscard]] std::optional<common::core::TimeRange> loopRegion()
    const` — edit-timeline seconds (decision 2), message-thread-only like the rest of the port.
  - New `transport/transport_error.h` with `TransportError{LoopRegionTooShort}` — the enum plan
    21 Phase 1 later extends with `SpeedNotSupported` (coordination rule, decision 1). Minimum
    length 0.1 s enforced in the adapter (decision 7).
  - Adapter in `engine_transport.cpp`: `TransportControl::setLoopRange` plus direct
    `looping` CachedValue assignment (the de facto API; keep the existing style from
    engine_song_audio.cpp:163). Engage sequence per decision 8. `setActiveArrangement` clears
    the engine loop through the same internal helper so `looping` and the stored range can never
    diverge (replaces the bare flag write at engine_song_audio.cpp:163); callers re-apply after
    load (editor does so in Phase 4).
  - Update all four `FakeTransport` implementations (inventory) in the same commit.
- **Files/modules**: transport/i_transport.h, new transport/transport_error.h, engine/engine.h,
  src/engine/engine_transport.cpp, src/engine/engine_song_audio.cpp, src/engine/engine_impl.h,
  tests listed below, the four fake files.
- **Public-header impact**: three methods and one small error header in common/audio — reviewed
  against constraint (b); no Tracktion types leak.
- **Testing plan** (rock_hero_common_audio_tests, headless Engine + `drum_loop.wav` fixture):
  set/clear/get round-trip; reversed-endpoint range normalized (Tracktion order-insensitivity);
  sub-0.1 s range returns `LoopRegionTooShort` and leaves state unchanged; loading an
  arrangement clears an engaged loop; contract tests in test_transport.cpp extended for the
  fake. Playback-confinement and audible-wrap checks need a live device — deferred to Phase 4's
  manual soak (same split plan 12 uses for live validation).
- **Exit criteria**: tests green; editor and game builds unaffected (no consumer yet).
- **Verification** (public port change → lint matters):
  `-Targets all`, then `-RunTouchedTests`, then `-Targets clang-tidy`.

### Phase 2 — Editor-core loop-selection state, intents, and persistence

- **Scope**: headless selection model in editor-core:
  - State: `LoopSelectionViewState { GridPosition start; GridPosition end; TimeRange seconds; }`
    exposed as `std::optional<LoopSelectionViewState> loop_selection` on `EditorViewState`
    (public header, additive). Stored musically per decision 3; seconds resolved by the
    controller via `TempoMap::secondsAtNote` for rendering and for the port.
  - Intents on `IEditorController` (mirroring the tone intents' shape,
    i_editor_controller.h:149-217): `onLoopSelectionChangeRequested(GridPosition start,
    GridPosition end)`, `onLoopSelectionClearRequested()`. Handlers in a per-domain TU
    (`src/controller/` handler pattern, like tone_handlers.cpp). Degenerate input (start == end)
    clears; ordering normalized in the handler.
  - Snap helper: `nearestTempoGridPosition(...)` in editor-core
    timeline/tempo_grid_geometry.{h,cpp} — the musical-position twin of `nearestTempoGridTime`
    (same measure-anchored note-value grid, returns `GridPosition`), so the UI emits musical
    intents directly. Ctrl-bypassed (free) drags quantize to a fine fixed sub-beat fraction
    (1/960 of a beat, ~1 ms at 60 BPM) so state stays exact-rational without pretending
    pixel-perfect freedom. Coordination: if docs/roadmap/40-chart-editing.md Phase 1 has landed
    its common/core snap arithmetic by execution time, build this helper on it; otherwise land
    here and let plan 40 extend — never two snap implementations.
  - Persistence per Q3's answer (recommended: `IEditorSettings` per-project-path record storing
    two grid tokens, following the `projectCursorPositions` codec pattern; new pure virtuals →
    all settings fakes update in the same commit). Restore on project load; selection survives
    arrangement switches (Q2 sub-policy); not in undo history (decision 4).
- **Files/modules**: editor_view_state.h, i_editor_controller.h, new
  src/controller/loop_handlers.cpp (or fold into an existing timeline handler TU),
  timeline/tempo_grid_geometry.{h,cpp}, i_editor_settings.h + settings/editor_settings.cpp +
  settings fakes, recording_editor_controller.h test double.
- **Public-header impact**: additive view-state struct + two intents + two settings virtuals —
  all editor-core public headers.
- **Testing plan** (rock_hero_editor_core_tests): snap helper against a two-signature tempo map
  (measure-anchored restart, tuplet-safe fractions, halfway ties resolve earlier — mirroring
  nearestTempoGridTime's documented rules); handler normalization (reversed, degenerate);
  seconds resolution against known tempo maps; settings round-trip including token parse
  failures falling back to no selection; selection retained across a simulated arrangement
  switch.
- **Exit criteria**: controller state and persistence work headlessly; no UI yet; no engine
  engagement yet.
- **Verification**: `-Targets all`, then `-RunTouchedTests`, then `-Targets clang-tidy`.

### Phase 3 — Ruler drag surface, edge handles, clearing, and theme (assumes Q1 = A)

- **Scope**: interaction on `TimelineRuler` plus rendering:
  - Drag gesture: `mouseDown` records the anchor; `mouseDrag` past the click threshold creates or
    updates the selection preview, snapped through Phase 2's helper honoring
    `EditorViewState.grid_note_value`, Ctrl bypassing snap via the existing `placementModeFor`
    (decision 5); `mouseUp` with `event.mouseWasClicked()` performs today's cursor-placement
    seek, otherwise commits `onLoopSelectionChangeRequested`. Behavior change to a shipped
    gesture, stated plainly: ruler click-to-seek moves from mouse-down to mouse-up (same
    pattern plan 40 Q3-A adopts for the lane; rollback note below).
  - Edge handles: hovering within a few pixels of a committed selection edge shows
    `LeftRightResizeCursor` and drags that edge (mirroring the ToneTrackView edge pattern,
    tone_track_view.cpp:209-250). During any drag, emit the shared `TimelineSnapGuide` with the
    musical readout through the existing `CursorOverlay::setSnapGuide` seam so ruler drags and
    tone drags present identically.
  - Clearing: Escape in `EditorView::keyPressed` clears the selection; click-away semantics per
    Q2 (a lane seek outside the selection clears). **Keybind placement stated explicitly**:
    Escape (and Ctrl+L if Q2 = A) lands in the scattered `EditorView::keyPressed` handling
    today, is appended to the interim keybind record, and migrates to the central registry when
    docs/roadmap/46-editor-keybinds.md Phase 1 executes — this plan must not build any registry.
  - Rendering: ruler-band highlight over the selected span drawn by `TimelineRuler`; full-height
    translucent shading over the track canvas drawn by `CursorOverlay::paint` (paint-only; hit
    handling unchanged, so tone-row pass-through gestures are untouched). New `EditorTheme`
    fields — `loop_selection_fill`, `loop_selection_border` — hex literals in the theme, no
    component-local colors (theme rules in editor_theme.h header comment).
- **Files/modules**: timeline/timeline_ruler.{h,cpp}, timeline/cursor_overlay.{h,cpp},
  main_window/editor_view.cpp (wiring, Escape, intent plumbing), shared/editor_theme.h.
- **Public-header impact**: none (all editor/ui `src/`).
- **Testing plan** (rock_hero_editor_ui_tests, synchronous wiring tests per
  docs/design/architectural-principles.md "Selective UI Wiring Tests"; no pixel assertions):
  simulated ruler click (no drag) still emits a seek; simulated drag emits a change intent with
  snapped endpoints; Ctrl-drag endpoints differ from snapped ones; edge-drag emits an updated
  intent; Escape emits clear; state push with a selection renders without asserting (smoke).
- **Exit criteria**: a charter can create, adjust, and clear a grid-snapped selection with the
  mouse; the selection is visible across ruler and canvas; tone-row gestures unaffected.
- **Verification**: `-Targets all`, then `-RunTouchedTests`.

### Phase 4 — Loop engagement, wrap behavior, and cross-cutting verification (assumes Q2 = B)

- **Scope**: connect selection to the Phase 1 port and verify the wrap-adjacent behaviors:
  - Controller engages `setLoopRegion(seconds)` whenever a selection exists and clears it when
    the selection clears; re-applies after arrangement activation (Phase 1 clears engine-side)
    and after any future tempo-map change (decision 3). A `LoopRegionTooShort` result keeps the
    selection visible but marks it inactive in view state (distinct fill via the theme's muted
    treatment) — RockHero-owned feedback, never Tracktion's bubble (decision 7).
  - Play/pause/seek policies per Q2's sub-policies, implemented in the controller handlers.
  - **Verify-before-implementing checkpoint (tone across wraps)**: the expert findings prove
    curve re-assertion at the wrap is sample-accurate through the shared PluginNode path
    (inventory), but our `ToneBranchGainPlugin` ramping and the in-flight slice-5 baked
    schedule are project code — before wiring engagement, run a headless adapter test (extend
    rock_hero_common_audio_tests) proving branch-gain parameter streams re-position after a
    simulated backward seek, and a manual listening check: two-tone arrangement, loop spanning a
    region boundary, tone correct immediately after each wrap. Coordinate with
    docs/roadmap/21-game-audio-engine-and-session.md Phase 3c(2) — extend its wrap matrix if it
    has landed; otherwise this checkpoint is the first entry that matrix later absorbs. If the
    check fails, STOP and escalate per that plan's Phase 3 rollback (joint decision with the
    tone work), not a local fix.
  - **Live input monitoring**: no code — verified loop-agnostic by construction (inventory,
    WaveInputDeviceNode); the manual soak confirms the guitarist keeps hearing the live rig
    through wraps with the correct tone.
  - **Playback follow on wrap**: no code — decision 6; manual soak confirms off-screen-left snap
    on wrap and no window motion when the loop is fully visible. The smooth-scroll evaluation
    (docs/todo/smooth-scroll-follow-evaluation.md) stays the user's pending decision; this plan
    changes nothing about follow either way.
- **Files/modules**: editor-core controller handlers; rock-hero-common/audio/tests (checkpoint
  test); no new public headers.
- **Testing plan**: editor-core fake-transport tests — selection change engages the port with the
  resolved seconds; clear disengages; arrangement switch re-engages; too-short result surfaces
  the inactive flag; seek-outside clears per Q2. Adapter checkpoint test as above. Manual soak
  (live device): audible loop wrap ≤ one device buffer of artifact; correct tone after wrap;
  live guitar uninterrupted; follow snap correct; brace drag during playback settles within
  ~200 ms (accepted, decision 8).
- **Exit criteria**: end-to-end looping in the editor over real corpus material; checkpoint and
  soak notes recorded in this file.
- **Verification**: `-Targets all`, then `-RunTouchedTests`.

## 10. Final acceptance phase

Per constraint (h), as separate invocations from the repo root, all green:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets clang-tidy
pre-commit run --all-files
```

Plus: the Phase 4 manual soak checklist signed off on a real device; a local-only corpus
spot-check (never CI, never committed) looping a tone-boundary-spanning span in three packages of
different tunings; new public headers documented per docs/design/documentation-conventions.md;
docs/roadmap/00-roadmap.md status line updated, and plans 21/28 annotated that the shared loop
surface has landed (decision 1's rule consumed).

## 11. Rollback/abort notes

- **Phase 1** is additive to a public port. If the Tracktion adapter misbehaves in ways the
  headless tests cannot catch (device-only), the fallback is the same one plan 21's rollback
  reserved: keep the port surface, return a typed NotSupported from `setLoopRegion`, and file the
  adapter fix — never remove published methods. The engine_song_audio.cpp:163 refactor reverts to
  the bare flag write independently.
- **Phase 3 changes a shipped gesture** (ruler seek moves to mouse-up). Keep the click/drag
  disambiguation in one place so reverting to seek-on-mouse-down is a one-line rollback if the
  feel regresses; the selection feature itself survives via edge handles.
- **Phase 4 checkpoint failure** (tone not re-asserted at wrap): STOP; do not ship engagement
  with wrong tones. Interim degraded mode if the user wants looping anyway: on each wrap the
  editor performs an explicit stop→seek(start)→play resync (audibly rougher, tone guaranteed by
  the verified seek-resync path) behind the same selection UX, while the root cause is pursued
  jointly with the tone-track work.
- Phases 2–4 are additive editor code behind new intents; each reverts as a unit. The Q3
  persistence record is app-local; rolling back orphans stored records harmlessly (same accepted
  pattern as the retired grid-settings key).
- If plan 21 Phase 1 executes first after all (gate closes early), re-verify its landed surface
  before Phase 1 here and reduce Phase 1 to extension-only per decision 1 — no other phase
  changes.
