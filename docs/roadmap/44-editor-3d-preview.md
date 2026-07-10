# Plan 44 — Editor 3D Preview

## 1. Status

**Decision-gated** — blocked on docs/roadmap/20-game-architecture-and-render-stack.md Phases 0b–0c
(the bgfx-in-a-JUCE-child-HWND spike finding S2 decides this plan's surface shape; the Phase 0c
renderer-sharing seam decides where the highway scene model lives) and on
docs/roadmap/25-note-highway-3d.md Phases 1–2 (headless scene model and camera). No phase below
starts before plan 20's gate closes with user sign-off. Date: 2026-07-06. Baseline:
`refactor @ 13e82fb0`.

## 2. Goal

A separate, fullscreen-capable 3D preview window in the editor: the charter opens it from the
View menu and sees the exact note highway the game will render for the current arrangement —
Charter-parity board, string-colored note heads, sustain rails, and technique rendering — scrolling
in sync with the editor transport during playback and updating live as the chart is edited. The
preview shares the headless highway scene model and camera with the game (per plan 20 Phase 0c),
so what the charter previews is what the player gets, and the highway is never written twice.

User-visible outcome: author in the 2D tab lane, press play, and watch the song fly by on the 3D
highway in a second window (or fullscreen on a second monitor) — the charter's play-along view
without touching the 2D timeline's follow behavior.

## 3. Non-goals

- No editing interactions inside the 3D view — the preview is a read-only viewer at v1; all
  authoring stays in the 2D tab lane and timeline.
- No gameplay-feedback content: hit/miss states, provisional-hit visuals, particle bursts, fret
  hit-flash, HUD, star-power or failure-meter layers are game-only
  (docs/roadmap/25-note-highway-3d.md Phase 5, docs/roadmap/24-scoring-star-power-failure.md). The
  editor has no gameplay events to feed them.
- No decision on the 2D timeline's playback-follow mode. The "side-scroll while playing" idea is
  the fixed-cursor smooth-scroll evaluation in docs/todo/smooth-scroll-follow-evaluation.md — an
  already-re-raised decision the user owns. This plan references it and stops; it does not decide
  it, duplicate it, or absorb it. The 3D preview delivers play-along presentation without
  changing the 2D follow mode either way.
- No render-stack or seam decisions — docs/roadmap/20-game-architecture-and-render-stack.md owns
  both; this plan consumes its gate record.
- No scene-model, projection, camera, or metrics implementation —
  docs/roadmap/25-note-highway-3d.md owns them; this plan consumes the shared library.
- No string-color definitions — docs/roadmap/45-editor-theme-and-string-colors.md owns the shared
  palette.
- No preview-specific keybind system — routing follows docs/roadmap/46-editor-keybinds.md.

## 4. Constraints

Applicable subset of the roadmap constraint block (see docs/roadmap/00-roadmap.md):

- (a) **Layering**: common never depends on editor or game code; editor and game never depend on
  each other. The editor preview may NEVER include game headers — everything it shares with the
  game highway (scene model, camera, pure geometry helpers, shader/atlas source assets) must live
  in rock-hero-common, extracted FIRST with tests before either product consumes it. If bgfx ever
  enters common's dependency surface (seam option 2), it gets the same isolation treatment
  Tracktion got — headers private to implementation files. Under the recommended seam option 1,
  bgfx never enters common; it is linked by each product's ui library separately.
- (b) **Public-header minimalism**: headers move to `include/` only when a consumer outside the
  library exists (docs/design/architectural-principles.md, "Placement Procedure for New Files");
  the preview window and its drawers stay private to rock-hero-editor/ui.
- (c) **NAMING FIREWALL**: the commercial real-guitar game that inspired this project is never
  named in any file; use "RS" or neutral phrasing. Charter (MIT) may be cited by name.
- (h) **Builds**: all build/test/lint commands go through `.agents/rockhero-build.ps1` (usage in
  `.agents/README.md`) — never raw cmake/ctest/ninja. Intermediate phases run only the checks
  their changes warrant; the final acceptance phase is the sanctioned bundle as separate
  invocations.
- **Time is a dependency** (docs/design/architectural-principles.md, "Time Must Be a
  Dependency"; docs/design/architecture.md, "Timing and Latency"): the preview samples song time
  from the audio-derived clock (docs/roadmap/12-playback-clock.md) — never wall clock — and camera
  smoothing takes an explicit frame delta.

Design-doc bindings: module placement per docs/design/architectural-principles.md ("Library
Roles", "UI Modules", "Placement Procedure for New Files"); the renderer stays a humble object
consuming headless view state per docs/design/architectural-principles.md ("Humble Object, But
With the Right Scope"); editor UI composition per docs/design/architecture.md ("Editor UI").

## 5. Current state inventory

- No preview, 3D, bgfx, or SDL code exists anywhere in the editor or the build: `conanfile.txt`
  declares only cmake-package-builder, catch2, libebur128, quill, ogg, vorbis; plan 20 Phase 1
  wires the render stack in.
- Secondary-window precedents this plan follows exist and are tested:
  - `rock-hero-editor/ui/src/input_calibration/input_calibration_window.h:27` —
    `InputCalibrationWindow final : public juce::DocumentWindow`.
  - `rock-hero-editor/ui/src/audio_device/audio_device_settings_window.cpp:279` — static
    `show(...)` factory returning `std::unique_ptr<juce::DocumentWindow>`.
  - `rock-hero-editor/ui/src/main_window/editor_view.h:390-393` — `EditorView` owns both windows
    as `std::unique_ptr<juce::DocumentWindow>` members.
  - `rock-hero-editor/ui/tests/test_editor_view_audio_controls.cpp:46` — window wiring tests via
    `findRequiredTopLevelComponent<juce::DocumentWindow>("input_calibration_window")`.
- `rock-hero-editor/ui/src/main_window/main_window.cpp:75` calls `setFullScreen(true)` — JUCE's
  maximize, not exclusive fullscreen. True fullscreen exists as
  `juce::Desktop::setKioskModeComponent`
  (external/tracktion_engine/modules/juce/modules/juce_gui_basics/desktop/juce_Desktop.h:226).
- Frame-cadence transport sampling is an established editor pattern:
  `rock-hero-editor/ui/src/timeline/cursor_overlay.cpp:19` drives cursor refresh from a
  `juce::VBlankAttachment` reading a const `common::audio::ITransport`
  (`rock-hero-common/audio/include/rock_hero/common/audio/transport/i_transport.h`);
  `editor_view.h:403` uses the same mechanism for meters.
- State push into views: `IEditorView::setState(const EditorViewState&)`
  (`rock-hero-editor/core/include/rock_hero/editor/core/controller/i_editor_view.h:32`).
  `EditorViewState` already carries `common::core::TempoMap tempo_map`
  (`.../controller/editor_view_state.h:236`) and a rebuilt-on-edit
  `std::shared_ptr<const TabViewState> tab` (`editor_view_state.h:268`) — the snapshot pattern
  the highway view state reuses for live-edit updates.
- The 2D projection reference the shared highway projection mirrors:
  `rock-hero-editor/core/src/tab/tab_projection.{h,cpp}` (`makeTabViewState`), tested by
  `rock-hero-editor/core/tests/test_tab_projection.cpp` (docs/roadmap/25-note-highway-3d.md
  Phase 1 owns the mirroring).
- Menus: `EditorView` is the menu-bar model (`getMenuBarNames` at
  `rock-hero-editor/ui/src/main_window/editor_view.cpp:655` returns File/Edit/View; the View
  menu already hosts "Show Waveform" and the lane-count submenu) — the preview's open action
  slots there.
- Keyboard handling today: `EditorView::keyPressed`
  (`rock-hero-editor/ui/src/main_window/editor_view.cpp:621`), `MainWindow::keyPressed`
  forwarding (`main_window.cpp:101-113`), and duplicated transport-shortcut predicates in
  `rock-hero-common/audio/src/tracktion/plugin_window.cpp:26-46` — centralization is
  docs/roadmap/46-editor-keybinds.md scope; the preview window is one more consumer of whatever
  seam it lands.
- The adopted 2D playback follow is the shifted window:
  `TrackViewport::followCursorWithWindowShifts`
  (`rock-hero-editor/ui/src/timeline/track_viewport.cpp:607`). Note:
  docs/todo/smooth-scroll-follow-evaluation.md cites the pre-restructure path
  `rock-hero-editor/ui/src/editor_view.cpp` — the current location is track_viewport.cpp; that
  doc is user-owned and is not edited by this plan.
- `rock-hero-common/ui` exists as a library (shared UI only when both products need it, per
  docs/design/architectural-principles.md "UI Modules") — relevant only if seam option 2 or the
  shared-asset home (open question 3) lands there.
- Settings port for window placement persistence: `IEditorSettings` / `EditorSettings`
  (`rock-hero-editor/core/include/rock_hero/editor/core/settings/editor_settings.h:26`).

Verified against code on 2026-07-06, refactor @ 13e82fb0.

## 6. Dependencies

- docs/roadmap/20-game-architecture-and-render-stack.md — **hard gate**:
  - Phase 0b spike criterion S2 (bgfx renders into a child HWND hosted inside a JUCE window;
    survives resize, DPI change, occlusion) decides this plan's surface shape. If S2 fails, this
    plan's Phases 2+ do not start (see Rollback).
  - Phase 0c renderer-sharing seam: this plan is only satisfiable with a common-homed scene model
    (option 1 with the highway feature in rock-hero-common/core, or option 2) — layering rule (a)
    forbids the editor consuming game/core. If the gate homes the scene model in game/core only,
    a promotion-to-common phase must run first (plan 25 §11 records the move as mechanical).
  - Phase 1 (bgfx in the build behind project-owned wrapper targets, shaderc wiring) — the editor
    ui library links the same wrapper targets.
- docs/roadmap/25-note-highway-3d.md — Phases 1–2 deliver the shared `HighwayViewState`,
  `highwayViewStateFor`, `HighwayMetrics`, and `highway_camera` this plan consumes; its Phase 4
  pure geometry helpers (adaptive tail sampling, taper envelopes) are consumed by Phase 3 here
  for technique parity. This plan never re-implements any of them.
- docs/roadmap/12-playback-clock.md — `IPlaybackClock` and the extrapolation policy; that plan
  lists the editor preview as a consumer. Block-quantized transport reads are invisible for a
  thin 2D cursor but visible as velocity shimmer on a whole moving content field (the analysis
  recorded in docs/todo/smooth-scroll-follow-evaluation.md item 2) — the 3D highway is exactly
  such a field, so Phase 4 consumes plan 12 rather than raw `ITransport::position()`.
- docs/roadmap/45-editor-theme-and-string-colors.md — the shared string-color palette in
  rock-hero-common (its palette-extraction phase; re-verify the phase number at execution).
  Phase 3 must not start before it lands; the preview never defines string colors.
- docs/roadmap/46-editor-keybinds.md — keybind routing for the preview window (open question 4);
  not blocking, but the preview registers into its centralized map when that plan lands.
- docs/roadmap/00-roadmap.md — execution ordering; the Decisions-needed mirror of this plan's open
  questions.
- Referenced, never absorbed or decided: docs/todo/smooth-scroll-follow-evaluation.md (the
  user-owned fixed-cursor follow decision for the 2D timeline).

## 7. Decisions already made

Restated with sources; none originate in conversation:

- **The editor keeps the 2D tab lane; the 3D highway is a separate view** — the absorbed
  highway analysis explicitly excluded editor integration of the game view
  (docs/roadmap/25-note-highway-3d.md §3), and this plan adds the highway to the editor only as a
  distinct preview window, not as a timeline replacement.
- **Charter's 3D preview is the visual target, matched very closely**, with exactly seven
  catalogued defect fixes as the only departures — docs/roadmap/25-note-highway-3d.md §7. The
  editor preview inherits this wholesale by consuming the same scene model, camera, and drawers'
  reference analysis; it introduces no visual divergences of its own.
- **The renderer-sharing seam recommendation is option 1** — headless scene model in
  rock-hero-common/core, thin per-product render backends, bgfx never in common
  (docs/roadmap/20-game-architecture-and-render-stack.md Phase 0c; gate pending). This plan's
  phases are written against that outcome and labeled accordingly.
- **The 2D timeline's playback follow is the shifted window** (trigger 0.8, 0.3s glide, pin
  0.05), adopted 2026-07-03; the fixed-cursor smooth-scroll alternative is a deferred evaluation
  the user owns, re-raise condition met — docs/todo/smooth-scroll-follow-evaluation.md. This
  plan does not touch that decision.
- **EditorTheme stays editor/ui-private; only string-color palette data is shared** —
  docs/roadmap/25-note-highway-3d.md §5, docs/roadmap/45-editor-theme-and-string-colors.md.
- **Render loops sample audio-derived time, never wall clock** — docs/design/architecture.md
  ("Timing and Latency"); docs/design/architectural-principles.md ("Time Must Be a Dependency").
- **Views receive domain state as controller-pushed snapshots** — the editor's MVC shape:
  `IEditorView::setState` with immutable `shared_ptr` payloads (`editor_view_state.h:268`),
  keeping views paint-only per docs/design/architectural-principles.md ("Humble Object, But With
  the Right Scope").

## 8. Open questions for the user

Mirror all of these into docs/roadmap/00-roadmap.md Decisions-needed.

1. **Chart-driven cue scope in the preview.** Gameplay-event-driven content (hit particles,
   fret hit-flash, status-keyed note states, HUD) is excluded by non-goal. But two Charter cues
   are chart-driven, not event-driven: the anticipation ring (scales onto the landing spot in
   the last 500ms) and the note rolling flip. Options: (a) render both — the preview shows
   exactly what an accurately-playing player sees; (b) omit both — a calmer authoring view.
   **Recommendation: (a)** — they are free (same drawers as the game path) and previewing the
   game's actual read is the point of the window.
2. **Drawer duplication vs a shared geometry layer.** Under seam option 1 the bgfx drawers are
   written twice (game/ui and editor/ui). Options: (a) accept duplication of thin drawers —
   all hard math (projection, camera, tail sampling) is already shared and unit-tested in the
   common highway feature; (b) additionally extract a renderer-agnostic geometry-command layer
   (pure CPU vertex/index list builders, no bgfx types) into the common highway feature so each
   product's drawer is only buffer upload + submit. **Recommendation: (a) to start, revisiting
   (b) after the first duplicated drawer pair exists** — propose the extraction at that point
   with measured overlap, per docs/design/architectural-principles.md ("Placement Procedure for
   New Files": extract when the second consumer is real, not speculative). Registered with plan
   20's gate record so the seam decision anticipates it.
3. **Shared shader/atlas asset home.** Plan 20 Phase 2 puts shader source in
   `rock-hero-game/ui/shaders/` — the editor cannot consume game files (constraint (a)), yet
   both products need the same five shader programs and note/glyph atlases
   (docs/roadmap/25-note-highway-3d.md §7 "Shaders"). Options: (a) move shared shader and atlas
   *source assets* to a rock-hero-common location (e.g. `rock-hero-common/ui/shaders/` — assets
   compiled per-product at build time create no library dependency), each product's CMake
   compiling and deploying them; (b) duplicate the sources in editor/ui and hand-sync.
   **Recommendation: (a)**; coordinate with plan 20 Phase 2 and plan 25 Phase 3 before either
   lands its shader tree so nothing moves twice.
4. **Transport keys while the preview window is focused.** Options: (a) none — the charter must
   refocus the main window; (b) play/pause (and seek keys) forwarded, implemented via
   docs/roadmap/46-editor-keybinds.md's centralized map if it has landed, else a minimal
   hardcoded forward mirroring `MainWindow::keyPressed` (main_window.cpp:101-113) that plan 46
   later absorbs. **Recommendation: (b)** — a preview you cannot start/stop from is an
   irritation, and the plugin windows already set the precedent that secondary windows honor
   transport keys (`plugin_window.cpp:26-46`, kept for exactly this reason).

## 9. Phased implementation

Phase numbering starts after the external gates. All phases assume plan 20's gate closed with
**seam option 1 (highway feature in rock-hero-common/core) and S2 passed (bgfx-in-JUCE-child-HWND
proven)**; if the gate lands elsewhere, re-plan Phases 2–3 before executing (Phase 1 survives any
outcome).

### Phase 0 — gate intake (no code)

- Scope: confirm plan 20 Phases 0b/0c sign-off; record the S2 finding details (device reset
  behavior on resize/DPI/occlusion, single-init-vs-reinit observations) and the seam outcome in
  this section; confirm docs/roadmap/25-note-highway-3d.md Phases 1–2 landed in the common highway
  feature and note which later plan-25 phases (techniques, Phase 4 helpers) exist; confirm
  docs/roadmap/45-editor-theme-and-string-colors.md's palette extraction landed or schedule
  Phase 3 after it; re-verify this plan's Current state inventory and refresh the baseline
  stamp. **STOP if S2 failed** — see Rollback.
- Files: this plan file only.
- Testing: none (documentation step).
- Exit criteria: gate record appended here; baseline stamp refreshed; open questions 1–4
  answered or explicitly carried.
- Verification commands: none (documentation only).

### Phase 1 — preview window shell (JUCE-only; assumes no specific render outcome)

- Scope: `PreviewWindow` in rock-hero-editor/ui, a new `preview/` feature folder, following the
  established secondary-window pattern (`InputCalibrationWindow` shape: `juce::DocumentWindow`
  subclass; owned by `EditorView` as a `std::unique_ptr<juce::DocumentWindow>` member;
  title-bar close resets the pointer). View menu gains "3D Preview" (toggle item beside "Show
  Waveform" in `EditorView::getMenuForIndex`), enabled only when a project is loaded. Content is
  a placeholder component reserving the native-surface slot. Fullscreen toggle: F11 (or the map
  plan 46 assigns) switches the window through `juce::Desktop::setKioskModeComponent`
  (juce_Desktop.h:226) — **verify with juce-tracktion-expert before implementing**: kiosk-mode
  behavior for a secondary window on Windows (multi-monitor placement, interaction with the
  maximized main window, restore path), since the only in-repo precedent is
  `setFullScreen(true)` on the main window (maximize, not kiosk). Window bounds and
  open-at-startup state persist through `IEditorSettings`.
- Files/modules: `rock-hero-editor/ui/src/preview/preview_window.{h,cpp}`;
  `rock-hero-editor/ui/src/main_window/editor_view.{h,cpp}` (menu item, ownership, command id);
  `rock-hero-editor/ui/tests/` additions.
- Public-header impact: none — the window stays in `src/` (constraint (b)); EditorView's owned
  pointer is a private member as with the existing two windows.
- Testing plan: window wiring tests in `rock-hero-editor/ui/tests/` following
  `test_editor_view_audio_controls.cpp` (`findRequiredTopLevelComponent` by component id):
  menu action opens the window, close resets ownership, reopen works, disabled without a
  project. Fullscreen stays untested-by-unit (OS interaction).
- Exit criteria: window opens/closes/reopens cleanly from the View menu; fullscreen toggles on a
  real machine; settings persistence round-trips.
- Verification commands:

  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
  ```

### Phase 2 — bgfx surface hosting (assumes outcome: S2 passed, bgfx wrapper targets from plan 20 Phase 1)

- Scope: a native-surface child component inside `PreviewWindow` producing the child HWND; bgfx
  initialization against it using exactly the pattern S2 proved (recorded in plan 20's gate
  record), rendering a cleared frame plus a debug test quad; resize/DPI-change/occlusion
  handling per S2; frame tick via `juce::VBlankAttachment` on the message thread (the
  established cadence mechanism, cursor_overlay.cpp:19) — if plan 20's gate chose a dedicated
  render thread for the game, the editor preview still ticks on the message thread at v1
  (simplest correct option; revisit only if profiling demands). Device lifetime: initialize on
  first open; keep the bgfx device alive across window close/reopen and destroy at editor
  shutdown, unless the S2 record proved clean re-init — decide from the gate record, not
  in-phase experimentation. The editor ui CMake target links the bgfx wrapper target plan 20
  Phase 1 created; the editor executable accepts that static dependency even when the preview
  is never opened (recorded cost of seam option 1).
- Files/modules: `rock-hero-editor/ui/src/preview/preview_surface.{h,cpp}`,
  `rock-hero-editor/ui/CMakeLists.txt`.
- Public-header impact: none.
- Testing plan: bgfx's Noop-renderer headless init path (plan 20 Phase 0b criterion S5 pattern)
  as an editor/ui test proving the editor-side init/teardown compiles and runs GPU-less in CI;
  surface geometry math (HWND child bounds from window bounds under DPI scale) extracted pure
  and unit-tested if nontrivial.
- Exit criteria: preview window shows a bgfx-cleared surface; survives resize, DPI change,
  minimize/occlusion, close/reopen, and fullscreen toggle without device loss or flicker;
  headless test green.
- Verification commands (CMake graph changes → configure):

  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Configure -Targets all
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
  ```

### Phase 3 — highway rendering (assumes outcome: seam option 1; plan 25 Phases 1–2 landed; plan 45 palette landed)

- Scope: render the shared scene through editor-owned drawers:
  - Build `HighwayViewState` via the common `highwayViewStateFor(Arrangement, TempoMap,
    options)` from the loaded project's current arrangement; hold it as an immutable
    `shared_ptr` snapshot exactly like `EditorViewState::tab` (editor_view_state.h:268).
  - Drawers in `rock-hero-editor/ui/src/preview/`: static board (strings from **plan 45's
    shared palette**, frets, inlays) in retained buffers; beat/measure bars with the fading
    shader; FHP lane highlights; note heads/open bars/shadows/sustain rails; technique rendering
    (bends, vibrato/tremolo with plan 25's defect-2 fix, slides, technique icons, chord boxes,
    fret numbers from the glyph atlas) reusing plan 25 Phase 4's pure sampling helpers from the
    common highway feature. Chart-driven cues per open question 1's answer. No gameplay-feedback
    content (non-goal).
  - Shaders/atlases from the shared asset home per open question 3's answer, compiled and
    deployed by the editor's build per plan 20's shaderc wiring.
  - Camera: the common `highway_camera` (FHP focus, smoothing, off-axis frustum, NDC pin) driven
    with explicit frame deltas; static frame at the cursor position while stopped.
  - If duplication with game/ui drawers proves heavier than "thin" while implementing, STOP and
    raise open question 2(b) with the measured overlap rather than silently extracting.
- Files/modules: `rock-hero-editor/ui/src/preview/` drawers and atlas loaders;
  `rock-hero-editor/ui/CMakeLists.txt` (shader compile/deploy).
- Public-header impact: none in editor/ui. If any helper this phase needs is still private in
  the common highway feature, promoting it is a plan-25-coordinated commit (constraint (a):
  extraction to common precedes consumption), not an ad-hoc copy.
- Testing plan: drawers stay untested-by-unit (thin renderer, consistent with editor JUCE paint
  treatment); any new pure geometry helper lands beside plan 25's in the common highway feature
  with unit tests there; the Phase 2 headless test keeps running with the full drawer set linked.
- Exit criteria: a loaded corpus-shaped project renders a correct static highway frame at the
  cursor; visual spot-check against Charter's preview for the same material; string colors match
  the 2D tab lane exactly (single palette source).
- Verification commands:

  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets clang-tidy
  ```

### Phase 4 — playback follow and live-edit updates (depends on docs/roadmap/12-playback-clock.md)

- Scope:
  - Song time from plan 12's `IPlaybackClock` mirror plus its extrapolation policy each vblank
    tick — never raw `ITransport::position()` (block-quantized reads shimmer on a moving field;
    the same finding recorded in docs/todo/smooth-scroll-follow-evaluation.md item 2) and never
    wall clock. Seek/pause snap per plan 12's rules; camera smoothing state resets on seek so
    the pin does not glide across the whole neck.
  - Live-edit updates: the controller rebuilds the highway snapshot alongside the existing tab
    snapshot on chart/tempo edits and pushes it through `IEditorView::setState`; the preview
    swaps the `shared_ptr` and re-streams dynamic content next frame. Rebuild cost rides the
    same policy as `TabViewState` rebuilds; if profiling shows edit-time lag, incremental
    rebuild is a plan-25-coordinated improvement, not an editor-side fork.
  - Transport keys per open question 4's answer.
- Files/modules: `rock-hero-editor/ui/src/preview/`;
  `rock-hero-editor/core/src/controller/` (highway snapshot in `EditorViewState` — coordinate
  with in-flight editor-core work at execution time); `rock-hero-editor/core/tests/`.
- Public-header impact: `EditorViewState` gains a highway snapshot member (public editor/core
  header, additive).
- Testing plan: editor-core tests proving the highway snapshot rebuilds on chart edit and is
  identical for identical inputs (mirroring existing tab snapshot tests); pure follow logic
  (clock sample → camera time input, seek-snap handling) unit-tested with fake clocks per
  docs/design/architectural-principles.md ("Time Must Be a Dependency").
- Exit criteria: playing a corpus-shaped project scrolls the highway in sync with audible
  playback (manual A/V check); edits appear in the preview within one state push; pause
  freezes the scene exactly; seek snaps without camera glide.
- Verification commands:

  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
  ```

### Phase 5 — polish and settings

- Scope: preview scroll-speed setting (same `HighwayMetrics` scaling the game exposes, persisted
  via `IEditorSettings`, independent of the game's player setting); lefty-mirror toggle
  exercising the scene model's `mirrored` flag (docs/roadmap/25-note-highway-3d.md §7) so charters
  can check lefty readability; window placement/fullscreen-state persistence hardening
  (multi-monitor restore); chord-fingering-panel visibility following plan 25 open question 2's
  answer for consistency.
- Files/modules: `rock-hero-editor/ui/src/preview/`, settings keys in editor/core.
- Public-header impact: none.
- Testing plan: settings round-trip tests in editor/core tests; mirror toggle correctness is
  already proven by plan 25's headless mirror tests — the toggle only flips the projection
  option.
- Exit criteria: settings persist across restarts; mirrored preview reflects correctly.
- Verification commands:

  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
  ```

## 10. Final acceptance phase

Run the sanctioned bundle from the repo root, as separate invocations, after the last
implementation phase (constraint (h)); fix anything surfaced before declaring the plan done:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets clang-tidy
pre-commit run --all-files
```

Acceptance additionally requires: the editor build and all existing editor tests unaffected when
the preview is never opened; the Phase 2 headless/noop path green in CI; a local (never CI)
corpus soak — open each corpus package, open the preview, play a stretch of each arrangement —
reported informally per the corpus rules in docs/roadmap/23-detection-verification-harness.md; and
a side-by-side sanity check that the preview and the game highway render the same fixture chart
identically (same scene model, same palette — differences indicate a drawer divergence to fix).

## 11. Rollback/abort notes

- **S2 fails at plan 20's gate** (bgfx cannot live in a JUCE child HWND): Phases 2+ do not
  start. Fallback options, re-planned with the user before any code: (a) a separate top-level
  native window owned by the editor process (bgfx gets its own HWND, losing docked-feel but
  keeping everything else); (b) seam option 2's shared surface component if the gate went that
  way; (c) defer the preview entirely until the game highway exists and revisit. Phase 1's
  JUCE-only shell survives every outcome.
- **Plan 20's seam lands as game/core-only**: run the promotion-to-common move (mechanical per
  docs/roadmap/25-note-highway-3d.md §11) as its own gated commit before Phase 3; if the user
  declines the promotion, this plan is unsatisfiable as scoped and returns to the roadmap.
- **Phase 2 device instability** (resets on resize/DPI in real use despite S2): keep the window
  shell, disable the surface behind the menu toggle, and escalate to plan 20 — its spike owns
  render-stack risk; do not improvise driver workarounds in editor code.
- **Phase 3 drawer duplication balloons**: STOP mid-phase and raise open question 2(b) with
  measured overlap; extraction into the common highway feature is coordinated with plan 25, not
  done unilaterally.
- **Phase 4 rebuild cost hurts editing latency**: the preview window auto-pauses live updates
  (explicit "preview paused — reopen to refresh" state) as a safe degradation while incremental
  rebuild is planned with plan 25; editing responsiveness always wins over preview liveness.
- Phases 1–2 are additive and confined to `rock-hero-editor/ui/src/preview/` plus small
  EditorView hooks; worst-case rollback is deleting the feature folder, the menu item, and the
  CMake links. Phase 4's `EditorViewState` member is additive and reverts cleanly.
