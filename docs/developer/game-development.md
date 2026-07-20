\page guide_game Game-Side Development

*Applies to: Game.*

The game deliberately does **not** have the editor's machinery. There is no action variant, no
availability gating, no undo history, no busy tokens, and no JUCE component tree. Do not import
those patterns; the game's shape is simpler because its problem is simpler — it plays content,
it does not edit it.

# The shape

- **`GameplaySession`** (`rock-hero-game/core/src/session/`) is the headless spine: a state
  machine (`GameplaySessionStage`: `Idle` → `Loading` → `PreparingRig` → `Ready` →
  `Playing`/`Paused`, plus finished and failed stages) that loads a package, prepares the live
  rig, and drives playback. It consumes the *same* common audio ports as the editor
  (`ISongAudio`, `ITransport`, `ILiveRig`, `IToneTimelinePlayer`, `IToneAutomation`,
  `IPlaybackClock`, `IMixControls`) — there is one audio engine, shared by both products. At rig
  completion it rebuilds the derived plugin-parameter playback curves from the arrangement's
  persisted musical automation through the shared
  `common::audio::rebuildToneAutomationCurves()` — the game-side analogue of the editor's
  post-load rebuild.
- **The library subsystem** (`rock-hero-game/core/src/library/`) scans package directories using
  the fast peek reader (`PackageDescription`) and projects entries for the menus — toured in
  \ref guide_game_library.
- **`rock-hero-game/ui`** is SDL3 + bgfx, not JUCE: the game window, menus, overlays, and the 3D
  note highway. The highway renderer itself is shared presentation code in
  `rock-hero-common/ui` (the editor's 3D preview uses the same renderer).
- **`rock-hero-game/audio`** is currently an empty placeholder — the game has no game-specific
  audio adapter yet; planned analysis work (pitch/onset detection) will live there.
- **`rock-hero-game/app/main.cpp`** composes it all: one `Engine`, the session, the window.

The plan 21 Phase 6 composition split is realized: `app/main.cpp` composes the audio side — the
`Engine`, `LiveInputMonitor`, `GameplaySession`, workspace paths, and the startup library scan —
and injects non-owning references into the shell, while `ui/src/surface/rock_hero_game.cpp`
composes only the render stack (window, device, resources, renderer) in `onInit`. New
dependencies follow that split: audio/session/library wiring belongs in `main.cpp`,
render-resource wiring in the shell. The shell's frame loop itself is toured in
\ref guide_game_shell.

# Supporting systems

Small headless pieces every game feature ends up touching:

- **`FrameClock`** (`core/frame_clock/`) — pure per-frame consumption of the playback clock plus
  frame-pacing statistics; the one place a frame's song time comes from (see
  \ref guide_musical_time).
- **`menu_bindings`** (`core/input/`) — maps raw input triggers to menu actions, with rebinding
  and conflict handling; **`song_select_menu`** (`core/menu/`) is the song/arrangement selection
  state machine.
- **`diagnostics`** (`core/diagnostics/`) — headless dev-diagnostics state, typed intents, and
  the chart-source change detector.
- **`detection`** (`core/detection/`) — the pure detection-event vocabulary (`OnsetEvent` with
  its `Transient`/`PitchStep` origin, `PitchFrame`, `PitchConfirmation`, `PolyphonicSalience`
  chord evidence, the `DetectionEvent` variant): plain trivially-copyable values timestamped in
  input-stream sample time, defined ahead of any DSP so scoring and the replay harness build
  against a frozen contract (plan 22 Phase 1).
- **`scoring`** (`core/scoring/`) — the versioned `ScoringRuleset` (rh-score-1, including the
  sustain trajectory tolerance and the overstrum/lapse-evidence gates), the `NoteVerdict`
  vocabulary, and pure ladder/window math; hit windows keep a constant real-time width across
  playback speeds and apply plan 13's effective-offset contract at consumption (plan 24
  Phase 1). The provisional-hit state machine lands here in a later phase.
- The session's port set also includes `IMixControls` (master/backing mix gains) and
  `IToneTimelinePlayer` (transport-driven scheduled tone switching) — both implemented by the
  shared `Engine`.

# Dev tooling

`rock-hero-game/ui/src/dev/dev_session.cpp` backs the `--dev-package` fixture path (parsed in
`app/main.cpp`, consumed during `Game` construction): load a package straight from disk with hot
reload and a stand-in clock — the fastest loop for iterating on highway rendering.
`ui/src/overlay/diagnostics_overlay.cpp` draws the screen-space frame-time graph over
the scene (fed by `FrameClock`'s pacing stats). Both are dev-only surfaces; neither participates
in the shipped menu flow.

# Adding a menu screen

Be aware of what does *not* exist: there is no menu stack, no screen framework. The game has
exactly two modes gated by a single `m_in_menu` bool (`ui/src/game/game.h`), and one concrete
menu — `SongSelectMenu` (`core/src/menu/song_select_menu.cpp`) with its two-value
`SongSelectScreen` enum. A new screen (pause, settings, results) is hand-wired, and every
touchpoint is silent:

1. The screen's headless state machine in `core/menu/` (follow `SongSelectMenu`: data in,
   `MenuAction` handled, effects reported as values).
2. The menu member + mode gating in `game.h`/`game.cpp` — a third mode means replacing the
   `m_in_menu` bool with a state enum; do that rather than stacking bools.
3. The input branch in `Game::handleWindowEvents` (menu path returns early) and the binding
   install in the `Game` constructor (`MenuBindings::bind` — only bound actions fire).
4. The render dispatch (`renderMenu` free function today) — menus draw through the screen-space
   overlay path, not the highway (see \ref guide_3d_highway).
5. The transition into/out of gameplay (`launchSong` is the only existing screen→session
   transition; a new screen needs its own analogue).
6. Headless tests for the screen machine in `core/tests/`.

# Adding an input

Two channels, one silent trap: `GameWindowEvents` carries both the mapped `GameKey` list
(gameplay) and the raw keycode list (the menu resolver), populated together in
`GameWindow::pollEvents`. The gameplay chain is compiler-guarded end to end (`GameKey`
enumerator → `toGameKey` switch → the exhaustive switch in `Game::handleWindowEvents`); the
menu chain is not (the `MenuAction` enumerator, its default binding in the `Game` constructor,
and its arm in the menu's `handle` are each silent). Wire both channels or the key works in
one mode only. Full context in \ref guide_keyboard.

# Persisting a game setting

`IGameSettings` (`core/include/.../settings/i_game_settings.h`) is the one seam: every plan that
needs a persisted value adds a **typed getter/setter pair** there (reads return
`std::optional`, writes return `std::expected`; the header documents the reserved keys). The
silent step is the three-way sync: a new pure virtual also lands in `GameSettings` (the
production store) and the test double `NullGameSettings`
(`core/tests/include/.../testing/null_game_settings.h`).

Audio device configuration is deliberately *not* here: the game keeps its **own**
`AudioConfigStore` file (sole writer), while the editor's store is opened read-only and only by
the throwaway `--import-editor-audio` dev path — the two-store split and its fail-loudly rule
are covered in \ref guide_audio_device.

# Packaged resources and the deploy contract

`GameResources` (`core/src/resources/`) is the one loading seam for packaged assets — today
`shaderPath`/`shaderBytes`/`textureBytes`; fonts and SFX directories exist in the deploy tree
but have no resolver yet. Adding an asset kind touches: the enum + resolve method in
`game_resources.{h,cpp}`, the root-resolution seam `makeGameResources` in the shell, and — the
silent one — the **CMake deploy**: `rock-hero-game/app/CMakeLists.txt` copies the resource tree
with a *stamp-based* custom command (deliberately not `POST_BUILD`, so the copy reruns when
assets change without relinking; the file comments explain), plus the parallel `install()`
rules. An asset that loads in your build tree and ships nowhere is this checklist's failure
mode.

# Extending the gameplay session

The stage machine is stable; when a stage is added anyway, the fan-out is: the legality guards
in `start`/`play`/`pause`/`seek`/`restart` (`gameplay_session.cpp`), the transport-listener
finish detection, every UI consumer that switches on `stage()` (e.g. the render path's
`Playing` check), and `test_gameplay_session.cpp`.

# Which recipes apply here

- \ref guide_add_port — fully. The game consumes the ports, and its session fakes (in
  `rock-hero-game/core/tests/test_gameplay_session.cpp`) break on any port change.
- \ref guide_add_file — fully.
- \ref guide_package_format — the game is the *reason* the peek reader exists; format changes
  almost always have a game-side consumer step.
- \ref guide_invariants — the repo-wide entries apply (liveness, threading, naming, Doxygen);
  the editor-specific ones (busy tokens, undo) do not.
- \ref guide_add_action and \ref guide_add_view — do **not** apply; they are editor machinery.

# Game-core conventions

Game logic follows the same testability contract as every core library: state machines take data
in and return data out, time is injected (transport position, frame delta), and behavior is
verified headlessly with fakes — `test_gameplay_session.cpp` is the exemplar. The long-term
architectural objective of a replayable simulation layer for gameplay and scoring is described in
\ref design_architectural_principles ("Add a Replayable Simulation Layer").
