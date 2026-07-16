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
  (`ISongAudio`, `ITransport`, `ILiveRig`, `IToneTimelinePlayer`, `IPlaybackClock`,
  `IMixControls`) — there is one audio engine, shared by both products.
- **The library subsystem** (`rock-hero-game/core/src/library/`) scans package directories using
  the fast peek reader (`PackageDescription`) and projects entries for the menus.
- **`rock-hero-game/ui`** is SDL3 + bgfx, not JUCE: the game window, menus, overlays, and the 3D
  note highway. The highway renderer itself is shared presentation code in
  `rock-hero-common/ui` (the editor's 3D preview uses the same renderer).
- **`rock-hero-game/audio`** is currently an empty placeholder — the game has no game-specific
  audio adapter yet; planned analysis work (pitch/onset detection) will live there.
- **`rock-hero-game/app/main.cpp`** composes it all: one `Engine`, the session, the window.

*Design in flux: today the game shell constructs its own adapters and resources; it is decided
(`docs/tracking/watch-items.md`) that composition moves to `app/main.cpp` with plan 21 Phase 6,
leaving the shell only the frame loop and input wiring. Don't build against the current
composition shape.*

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
