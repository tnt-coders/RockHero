# Plan 20 — Game Architecture and Render Stack

**Status: Decision-gated** | Date: 2026-07-06 | Baseline: `refactor @ 3c7febe0`

This plan is the structural gate for all game-side work. Phases 0a–0c form the decision gate; no
phase after the gate — and no dependent plan (docs/plans/25-note-highway-3d.md,
docs/plans/44-editor-3d-preview.md, the render-loop parts of
docs/plans/21-game-audio-engine-and-session.md) — starts before the gate closes with explicit
user sign-off. Do not create a render-stack expert agent (SDL3/bgfx or otherwise) until the gate
closes; seed it with the spike findings when it does.

## 1. Goal

A running game executable skeleton with a decided, spike-proven render stack: a real game window
and frame loop coexisting with the JUCE message thread and the shared Tracktion-backed audio
engine, a decided renderer-sharing seam so the 3D highway is written once for both the game and
the editor preview, one resource-pack convention every later game plan loads assets through, a
documented threading model, and a dev-diagnostics layer (debug overlay, chart hot-reload,
autoplay toggle, seek-to-section) designed in from day one.

User-visible outcome: launching `rock-hero` opens a game window rendering at a stable frame rate
with a diagnostics overlay, while audio playback through the shared engine keeps working — the
platform every gameplay plan builds on.

## 2. Non-goals

- No note highway content or Charter-parity visuals — that is docs/plans/25-note-highway-3d.md.
- No gameplay session orchestration, tone switching, or mix controls — that is
  docs/plans/21-game-audio-engine-and-session.md.
- No detection, scoring, menus, library, or settings UI (plans 22, 24, 26, 27).
- No editor preview implementation — docs/plans/44-editor-3d-preview.md consumes this plan's
  phase 0b/0c outcomes; this plan only produces the facts 44 needs.
- No commitment to SDL3/bgfx before the spike passes. `docs/design/architecture.md` states them
  as the target design; this plan treats that as the leading candidate, not a settled fact.

## 3. Constraints

Applicable subset of the roadmap constraint block (see docs/plans/00-roadmap.md):

- (a) **Layering**: common never depends on editor or game code; editor and game never depend on
  each other. Anything both products need (the renderer-sharing seam of phase 0c) is extracted to
  rock-hero-common FIRST — as its own phase with tests — before game code consumes it. Game code
  never includes editor headers. Tracktion headers stay isolated to rock-hero-common/audio
  implementation files. If a rendering framework enters common's dependency surface, it gets the
  same isolation treatment Tracktion got (headers private to implementation files).
- (b) **Public-header minimalism**: only headers that must be public are public;
  ports-and-adapters per `docs/design/architectural-principles.md` ("Ports and Adapters",
  "Library Roles").
- (c) **NAMING FIREWALL**: the commercial real-guitar game that inspired this project is never
  named in any file; use "RS"/neutral phrasing. Charter (MIT) may be named.
- (h) **Builds**: all build/test/lint commands go through `.agents/rockhero-build.ps1` (usage in
  `.agents/README.md`) — never raw cmake/ctest/ninja. Intermediate phases run only the checks
  their changes warrant; the final acceptance phase is the one sanctioned bundle.

Design-doc bindings: module placement follows `docs/design/architectural-principles.md`
("Library Roles", "Placement Procedure for New Files", "Keep Threading at the Boundary"); the
threading model extends `docs/design/architecture.md` ("Threading Model", "Timing and Latency",
"Game View"). Where phase 0 outcomes contradict `docs/design/architecture.md` (for example the
"SDL is polled manually from JUCE's message loop" statement, or the technology-stack table), the
gate-closure step includes an explicit "requires design-doc update + user confirmation" action
rather than a silent divergence.

## 4. Current state inventory

Verified by direct inspection of the tree:

- `rock-hero-game/app/main.cpp` — an 81-line JUCE `DocumentWindow` shell
  (`RockHeroApplication`, `START_JUCE_APPLICATION`); no SDL, no bgfx, no game loop. The file's
  own comment calls it "Temporary game shell window used until the SDL/bgfx gameplay content owns
  the view."
- `rock-hero-game/app/CMakeLists.txt` — `juce_add_gui_app(rock_hero_game_exe ...)`, output name
  `rock-hero`, links `rock_hero::common`, `rock_hero::game`, `rock_hero::juce_gui_basics`;
  registers icons (`resources/icons/hicolor/...`, `resources/rock-hero.ico`) and a Linux
  `.desktop` file through `package_register_target` — the only existing game resource precedent.
- `rock-hero-game/core`, `rock-hero-game/audio`, `rock-hero-game/ui` — placeholder static libs
  (`src/placeholder.cpp`, `.gitkeep` include roots). Link topology already enforces layering:
  core → `common::core`; audio → `common::core` + `common::audio` + `game::core`; ui →
  `game::core` + `game::audio` + `common::audio`. Each README declares intended first real code
  (ui: "bgfx surface integration, note highway rendering, HUD").
- `conanfile.txt` — requires: cmake-package-builder/1.2.0, catch2/3.13.0, libebur128/1.2.6,
  quill/11.1.0, ogg/1.3.5, vorbis/1.3.7. **No SDL3, no bgfx, anywhere in the build.** JUCE +
  Tracktion come from the `external/tracktion_engine/` submodule; per-preset Conan caches live at
  `build/<preset>/.conan2`.
- `rock-hero-common/audio/include/rock_hero/common/audio/engine/engine.h:57-64` — `Engine`
  implements `ITransport`, `ISongAudio`, `IAudioDeviceConfiguration`, `IAudioMeterSource`,
  `IPluginHost`, `ILiveInput`, `ILiveRig`, `IThumbnailFactory`. Header doc (lines 44-47): "Most
  public methods must be called on the message thread" — the game process must keep a live JUCE
  message thread for Tracktion regardless of who owns the frame loop.
- `docs/design/architecture.md` — "Technology Stack" table lists "Game rendering | SDL3 + bgfx"
  as target; "Game View" section states "SDL is initialized without its own event pump. SDL
  events are polled manually from JUCE's message loop"; "Threading Model" defines audio /
  analysis / UI / optional render threads; "Development Approach → Build First: Timing
  Instrumentation" mandates permanent timing logs before gameplay.
- `docs/plans/25-note-highway-3d.md` (which absorbed the retired docs/todo 3D-highway analysis) —
  deep Charter-preview analysis; proposes `highway/highway_view_state.h` (headless,
  seconds-resolved frame content) in `rock-hero-game/core` with an open question about promotion
  to common. That open question is exactly this plan's phase 0c. The analysis lives in plan 25,
  not in this plan.
- `rock-hero-common/core/include/rock_hero/common/core/shared/logger.h` — quill-backed shared
  logger; the diagnostics layer builds on it instead of inventing logging.
- `.agents/README.md` — build helper contract: lowercase presets, `-Targets` (any Ninja target
  incl. `all`, `clang-tidy`), `-RunTouchedTests`, `-Configure` only after CMake graph changes,
  `-FullOutput` for diagnosis.
- No game test targets exist yet (`rock-hero-game/*/tests/` absent); the architecture docs
  reserve them.

Verified against code on 2026-07-06, refactor @ 3c7febe0.

## 5. Dependencies

- docs/plans/00-roadmap.md — execution ordering and the Decisions-needed mirror of this plan's
  open questions and gate.
- docs/plans/12-playback-clock.md — Phase 3 consumes its `IPlaybackClock` mirror and the
  game-side extrapolation policy; Phase 3 cannot finish before plan 12's port exists.
- External decisions: the user's sign-off on phases 0a, 0b, 0c (the gate).
- Downstream (for the roadmap graph, not blocking this plan):
  docs/plans/25-note-highway-3d.md (gated on 0c seam + Phase 1),
  docs/plans/44-editor-3d-preview.md (gated on 0b child-HWND finding + 0c),
  docs/plans/21-game-audio-engine-and-session.md (consumes 0b coexistence outcome),
  docs/plans/26-game-startup-menus-library.md (consumes this plan's frame-pacing/vsync policy
  and the resource-pack convention), docs/plans/23-detection-verification-harness.md (the
  autoplay toggle here is the front end of 23's autoplay bot).

## 6. Decisions already made

Restated from their source documents; none originate in conversation:

- SDL3 + bgfx is the *stated target design* for game rendering, with SDL polled from JUCE's loop
  and bgfx rendering into a native window handle — `docs/design/architecture.md` ("Technology
  Stack", "Game View"). This plan treats that as the leading candidate to be proven or replaced
  by the phase 0b spike, per the gate.
- Static linking of scope umbrella targets is settled with reasoning
  (`docs/design/architecture.md`, "Architecture Diagram" section) — the game executable links
  `rock_hero::common + rock_hero::game`; render-stack dependencies must fit that model.
- The audio thread is the single source of truth for timing; scoring/render never keep
  independent clocks — `docs/design/architecture.md` ("Timing and Latency"). Plan 12 owns the
  clock port; this plan's loop only consumes it.
- Timing instrumentation is built before gameplay — `docs/design/architecture.md` ("Build First:
  Timing Instrumentation"). The dev-diagnostics layer here is that mandate's game-side home.
- The highway visual target is Charter's 3D preview, matched closely, with seven enumerated
  defect fixes — `docs/plans/25-note-highway-3d.md` § Decisions already made. Its headless
  `HighwayViewState` concept (seconds-resolved, camera-agnostic frame content; pure-math camera;
  renderer consumes view state only) is the input to phase 0c.
- Headless behavior lives in core modules; frameworks stay in thin adapters; time/threading at
  the boundary — `docs/design/architectural-principles.md` ("Core Position", "Library Roles",
  "Time Must Be a Dependency").
- Repo license is AGPLv3; SDL3 (zlib) and bgfx (BSD-2-Clause) are license-compatible —
  `docs/design/architecture.md` ("Licensing").

## 7. Open questions for the user

Mirror all of these into docs/plans/00-roadmap.md Decisions-needed.

1. **Platform scope (phase 0a).**
   Options: (A) Windows-first, cross-platform-preserving choices — keep renderer/windowing
   abstractions that run on Linux/macOS later, ship and test only Windows now; (B) Windows-only
   commitment — allows D3D11-only rendering and Win32-direct loops, halves the spike surface but
   makes any later port a rewrite; (C) multi-platform CI from day one — maximum insurance,
   significant immediate CI and testing cost.
   **Recommendation: A.** SDL3/bgfx earn their complexity precisely as cross-platform
   abstractions; ASIO is already Windows-specific but isolated behind common/audio; the Linux
   `.desktop` resource already in-tree signals cross-platform intent. B would undercut the main
   argument for the candidate stack; C buys little while there are no users.
2. **Candidate set for the 0b spike.** Options: (A) spike SDL3+bgfx only; (B) spike SDL3+bgfx as
   primary plus a JUCE-window+bgfx fallback branch (JUCE owns window/input, bgfx renders into
   its HWND, no SDL); (C) also evaluate SDL3+SDL_GPU (drop bgfx).
   **Recommendation: B.** The JUCE+bgfx variant reuses the child-HWND work that plan 44 needs
   anyway, removes the loop-coexistence risk entirely, and its main cost (weaker gamepad support
   than SDL3, needed by plan 26's menu input layer) is measurable in the spike. C's shader
   toolchain is younger than bgfx's shaderc; evaluate on paper in 0b, prototype only if A and B
   both fail their criteria.
3. **Renderer-sharing seam (phase 0c).** Options in phase 0c below.
   **Recommendation: Option 1** (headless scene model in rock-hero-common/core, thin per-product
   render backends).
4. **Dependency delivery: Conan vs vendored submodules** for the chosen stack. Options: (A)
   Conan recipes pinned in `conanfile.txt` (consistent with catch2/quill/ogg/vorbis; per-preset
   caches already isolate them); (B) vendored submodules under `external/` (consistent with
   JUCE/Tracktion; full control of patches; heavier checkout and CI).
   **Recommendation: A if the spike shows current recipes build cleanly under the
   CLion-CMake + VsDevCmd/Ninja environment; otherwise B.** Recorded as spike criterion S3; the
   gate answer includes the measured data.
5. **Dev-diagnostics activation.** Options: (A) compiled into all builds, enabled by a runtime
   flag (`--dev` command line + in-game key toggle); (B) debug-configuration-only compilation.
   **Recommendation: A.** Release-build timing bugs are exactly what the overlay exists for, and
   a single build flavor avoids divergence; the runtime flag keeps players out of it.
6. **Frame pacing default: vsync ON with measured frame-time instrumentation.** Options: (A)
   vsync ON default, toggle in 26's video settings; (B) uncapped with frame limiter.
   **Recommendation: A** — predictable pacing beats raw latency for a rhythm display whose hit
   timing is scored in audio time (plan 12/24), and the policy is recorded here because 26's
   video settings need a stated baseline.

## 8. Phased implementation

### Phase 0a — Platform-scope declaration (gate part 1)

- **Scope**: a one-page decision memo (appended to this plan under "Gate record" when answered):
  target platforms now/later, which per-OS renderer backends must work, what CI must prove.
  Output feeds 0b's evaluation criteria — cross-platform preservation is either a hard criterion
  or dropped.
- **Files**: this plan file only. No code.
- **Testing**: none (decision phase).
- **Exit criteria**: user answers open question 1; answer recorded in this plan and in
  docs/plans/00-roadmap.md Decisions-needed.
- **Verification commands**: none (documentation only).

### Phase 0b — Render-stack integration spike (gate part 2)

Prototype on a throwaway branch (`spike/render-stack`); nothing merges to `refactor` except the
findings write-up. Spike code may bend conventions; it is deleted after the decision.

- **Scope — enumerate, then prove.** First enumerate the JUCE-coexistence options with risks,
  consulting `.claude/agents/juce-tracktion-expert.md` for framework facts (message-manager
  ownership, `ScopedJuceInitialiser_GUI`, dispatch-loop APIs) and citing file:line findings from
  `external/tracktion_engine/` in the write-up:
  - **Option L1 — JUCE owns the loop** (the `docs/design/architecture.md` "Game View" shape):
    the JUCE application shell stays; SDL events are polled and bgfx frames submitted from a
    high-frequency callback on the message thread. Risk: message-loop jitter caps frame pacing;
    known Windows hazard that posted messages starve `WM_PAINT`.
  - **Option L2 — SDL owns `main()`**: JUCE initialized without `START_JUCE_APPLICATION`; the
    SDL loop services the JUCE message queue each frame. Risk: subtle MessageManager thread
    affinity, Tracktion timers, and plugin-editor windows (the live rig can open VST editors —
    `rock-hero-common/audio/src/tracktion/plugin_window.cpp` exists precisely for focus
    handling). Verify with juce-tracktion-expert before implementing.
  - **Option L3 — dedicated render thread**: JUCE keeps the message thread (windows/input
    created there), bgfx submission runs on a render thread. Risk: SDL window/event thread
    affinity on Windows; bgfx's own API-vs-render thread split must be understood first.
- **Spike criteria (all must be measured, not vibed):**
  - **S1 Coexistence soak**: chosen loop option runs the shared `common::audio::Engine` playing
    a FLAC through the transport while rendering a moving test scene at 60 fps for 10+ minutes —
    zero audio dropouts, no message starvation (a JUCE `Timer` heartbeat keeps firing), a VST
    editor window opens and takes focus correctly.
  - **S2 bgfx-in-a-JUCE-child-HWND**: bgfx renders into a child HWND hosted inside a JUCE
    window; survives resize, DPI change, and occlusion without flicker or device loss. This
    finding decides docs/plans/44-editor-3d-preview.md's shape and open question 2's fallback.
  - **S3 Dependency delivery**: resolve SDL3 and bgfx via Conan under the repo presets
    (per-preset `.conan2` caches) OR document the failure and prove the vendored-submodule
    route; record configure/build time deltas for both where feasible.
  - **S4 shaderc in the CMake graph**: a `.sc` shader pair compiles at build time to the
    Windows backend(s) chosen in 0a, as a custom command inside the preset build, through
    `.agents/rockhero-build.ps1` (the CLion-bundled CMake + VsDevCmd Ninja environment) — not a
    hand-run tool.
  - **S5 Headless CI path**: bgfx initializes with its Noop renderer in a Catch2 test with no
    GPU and no window (verify the exact init call pattern during the spike); the test runs via
    `-RunTouchedTests`.
  - **S6 Measured CI cost**: wall-clock delta for configure+build with the new dependencies on
    the CI runner class, recorded in the findings.
- **Files/modules**: spike branch only; findings appended to this plan under "Gate record".
- **Public-header impact**: none.
- **Testing plan**: S5's Noop test is the one spike artifact shaped like a real test; it is
  re-implemented properly in Phase 1, not merged from the spike.
- **Exit criteria**: findings written up with pass/fail per criterion and a recommended stack +
  loop option. **STOP — present findings and get user sign-off.** The decision is only
  "definitive" once the spike passes; a failed criterion loops back to open question 2's
  alternatives.
- **Verification commands** (on the spike branch, after its CMake graph changes):

  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Configure -Targets all
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
  ```

### Phase 0c — Renderer-sharing seam decision (gate part 3)

Decides where highway rendering code lives before docs/plans/25-note-highway-3d.md bakes
geometry into one backend — otherwise the highway gets written twice (once for the game, once
for docs/plans/44-editor-3d-preview.md).

- **Scope**: choose between:
  - **Option 1 — headless scene model in common (recommended)**: a `highway/` feature in
    `rock-hero-common/core` owning the camera math, lane layout, note transforms, and the
    seconds-resolved `HighwayViewState` (promoting the concept from docs/plans/25-note-highway-3d.md
    out of game/core, per constraint (a): both products need it, so it is extracted to common
    FIRST, with tests, before game code consumes it). Each product keeps a thin render backend
    (game/ui owns the full-screen game surface; editor/ui owns the preview window per plan 44).
    bgfx never enters common. Cost: the backend drawers are written twice, but they are thin —
    all the hard math (projection, camera smoothing, verticality invariant, tail tessellation
    policy) is shared and unit-tested headlessly, matching `docs/design/architectural-principles.md`
    ("Core Position", "Add a Replayable Simulation Layer").
  - **Option 2 — shared bgfx surface component in rock-hero-common/ui**: one rendering component
    both products embed. Cost: bgfx enters common's dependency surface and must get Tracktion-style
    framework isolation (headers private to common implementation files) per constraint (a);
    common/ui grows GPU lifecycle concerns that `docs/design/architectural-principles.md`
    ("Core Modules") pushes to the boundary; the editor executable takes a bgfx dependency even
    when the preview window is closed.
  - **Option 3 — no sharing**: rejected up front; duplicating the highway violates the stated
    reason this phase exists.
- **Files**: this plan file (decision record); no code until Phase 1+.
- **Exit criteria**: user signs off on the seam option (open question 3), informed by S2 (if
  bgfx-in-JUCE-HWND fails, Option 2 is dead anyway and 44 needs a different surface strategy).
  Gate closes only when 0a, 0b, and 0c are all signed off. Gate closure also files the
  design-doc update ("requires design-doc update + user confirmation"): amend
  `docs/design/architecture.md`'s Technology Stack / Game View / Threading Model sections to the
  proven outcome, and create `.claude/agents/game-render-expert.md` seeded with the spike
  findings.
- **Verification commands**: none (documentation only).

---

**Everything below assumes the gate closed. Phase labels state their assumed outcome; if the
gate lands elsewhere (e.g. JUCE-window+bgfx, or SDL_GPU), re-plan Phases 1–4 before executing —
the phase shapes survive, the dependency and loop details do not.**

### Phase 1 — Dependency wiring and window/loop swap (assumes outcome: SDL3+bgfx via the loop option chosen at the gate)

- **Scope**: add the chosen dependencies (Conan pins in `conanfile.txt` or submodules under
  `external/`, per gate answer to open question 4) behind project-owned wrapper targets in the
  same style as the JUCE/Tracktion wrappers (`docs/design/architecture.md`, "JUCE and Tracktion
  CMake linkage"); wire shaderc into the build graph (S4's proven pattern, productionized);
  replace the `DocumentWindow` shell in `rock-hero-game/app/main.cpp` with the chosen loop
  model rendering a cleared frame; add the Noop-renderer headless test as the first real
  `rock-hero-game/ui/tests/` target (per `docs/design/architectural-principles.md`, "CMake and
  Test Layout", and MEMORY-independent repo rule: test targets link the library they test).
- **Files/modules**: `conanfile.txt` and/or `external/`; root and `rock-hero-game/*` CMake;
  `rock-hero-game/app/main.cpp`; new `rock-hero-game/ui/src/` window/device units (feature
  folder `surface/` or per the gate's naming); `rock-hero-game/ui/tests/`.
- **Public-header impact**: minimal — the game window/loop stays private to app+ui; at most one
  public header exposing a `GameShell`-style composition point for app/. Remove the ui
  `.gitkeep` in the same change that adds the first public header (per
  `rock-hero-game/ui/README.md`).
- **Testing plan**: Noop-renderer init/teardown test (proves headless CI path); a loop-tick unit
  test over a pure frame-scheduler helper if the loop shape extracts one. Lives in
  `rock-hero-game/ui/tests/`.
- **Exit criteria**: `rock-hero` opens the game window, renders, exits cleanly; audio engine
  construction unaffected (editor build untouched); headless test green in CI.
- **Verification commands** (graph changed → configure; code changed → build; behavior → tests):

  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Configure -Targets all
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
  ```

### Phase 2 — Resource-pack convention (assumes outcome: any GPU stack)

One convention so plans 25/26/27 never invent their own loading paths.

- **Scope**:
  - On-disk `resources/` tree deployed next to the executable by CMake (extending the existing
    `package_register_target` icon precedent in `rock-hero-game/app/CMakeLists.txt`):
    `resources/fonts/`, `resources/shaders/<backend>/`, `resources/sfx/` (menu SFX, count-in
    click), `resources/textures/` (note atlas, glyph atlas outputs).
  - Compiled shader binaries are build products landing in `resources/shaders/<backend>/`;
    shader source lives in `rock-hero-game/ui/shaders/` (committed).
  - A small headless `GameResources` resolver in `rock-hero-game/core` (feature folder
    `resources/`): maps typed resource ids to paths under an injected root, returns typed
    boundary errors for missing files (`docs/design/architectural-principles.md`, "Typed
    Boundary Errors"). App composes it with the real executable-relative root; tests use temp
    dirs. Font/SFX asset *choices* (which font, which click sample) are deferred to plan 26's
    art direction; this phase fixes only locations and the loading seam.
- **Files/modules**: `rock-hero-game/core` (`resources/` feature + tests — first real
  `rock-hero-game/core/tests/` target), `rock-hero-game/app` CMake deploy step,
  `rock-hero-game/ui/shaders/`.
- **Public-header impact**: `rock_hero/game/core/resources/game_resources.h` public (ui and app
  consume it); remove core `.gitkeep` in the same change.
- **Testing plan**: resolver unit tests — id→path mapping, missing-root/missing-file typed
  errors, no filesystem dependence beyond a temp fixture dir.
- **Exit criteria**: fresh build produces a complete `resources/` tree; the Phase 1 window loads
  its clear-screen shader (or first texture) exclusively through `GameResources`.
- **Verification commands**:

  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Configure -Targets all
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
  ```

### Phase 3 — Threading model and frame clock (assumes outcome: gate loop option; depends on docs/plans/12-playback-clock.md)

- **Scope**: document (in this plan's Gate record + the design-doc update) and implement the
  game-process thread map: JUCE message thread (mandatory for Tracktion — `engine.h:46-47`),
  audio thread (Tracktion/ASIO), the frame loop per the gate, quill's logging backend thread,
  and the future analysis thread slot (plan 22). Restate and enforce
  `docs/design/architecture.md` "Threading Model" rules (no locks/allocs on audio thread;
  atomics/lock-free queues across threads; graph mutations message-thread only). The frame loop
  samples song time ONLY through plan 12's `IPlaybackClock` mirror + extrapolation policy —
  never wall clock, per `docs/design/architecture.md` ("Timing and Latency"). Implement the
  frame-pacing policy from open question 6's answer (default vsync ON) with per-frame timing
  instrumentation (frame delta, present time, clock-mirror age) recorded through the shared
  quill logger (`rock-hero-common/core/.../shared/logger.h`) — the game-side half of
  architecture.md's "Build First: Timing Instrumentation".
- **Files/modules**: `rock-hero-game/ui` loop units; `rock-hero-game/core` pure frame-clock
  helpers (extrapolation consumption, pacing stats) with tests.
- **Public-header impact**: none beyond a headless frame-clock helper header in game/core.
- **Testing plan**: pure unit tests for extrapolation-consumption and pacing-stat math with
  simulated timestamps (time as a dependency, per `docs/design/architectural-principles.md`,
  "Time Must Be a Dependency"); no test touches a real clock or GPU.
- **Exit criteria**: window renders with the backing engine playing (manual soak mirroring S1);
  instrumentation log lines present; frame-clock tests green.
- **Verification commands**:

  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
  ```

### Phase 4 — Dev-diagnostics layer (assumes outcome: any GPU stack)

Cheap now, painful to bolt on later; every later game plan hooks into it.

- **Scope**:
  - Headless `diagnostics/` feature in `rock-hero-game/core`: a `DiagnosticsState` value
    (overlay on/off, autoplay flag, selected panels) plus typed intents (seek-to-section by
    `ChartSection` index, reload-chart) — explicit state + requested side effects, per
    `docs/design/architectural-principles.md` ("Separate State From Side Effects"), so the layer
    is unit-testable without a window.
  - Overlay rendering in `rock-hero-game/ui`: frame-time graph, playback-clock drift
    (mirror-vs-extrapolated, from Phase 3 instrumentation), and reserved panels that plans 22/24/25
    fill later (detection confidence trace, hit-window visualization).
  - Chart hot-reload: a dev-only file watcher on the loaded chart source that re-issues the
    reload intent; consumers (25's projection) rebuild view state.
  - Autoplay toggle: a stub flag here; the perfect-event emitter itself belongs to
    docs/plans/23-detection-verification-harness.md (its autoplay bot) — this phase only
    reserves the switch and the HUD indication.
  - Activation per open question 5's answer (recommended: all builds, `--dev` flag + key
    toggle).
- **Files/modules**: `rock-hero-game/core` (`diagnostics/` + tests), `rock-hero-game/ui`
  overlay drawer, `rock-hero-game/app` flag parsing.
- **Public-header impact**: `DiagnosticsState`/intents header public in game/core; overlay
  stays private to game/ui.
- **Testing plan**: state-transition and intent-emission unit tests in
  `rock-hero-game/core/tests/`; watcher debounce logic tested against a fake timestamp source.
- **Exit criteria**: overlay toggles at runtime showing live frame/clock panels; editing a
  loaded chart file on disk triggers the reload intent within one second in dev mode.
- **Verification commands**:

  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
  ```

## 9. Final acceptance phase

Run the sanctioned bundle, as separate invocations, only when all executed phases are complete
(constraint (h)); fix anything surfaced before declaring the plan done:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets clang-tidy
pre-commit run --all-files
```

Acceptance also requires: the Gate record section filled (0a/0b/0c answers + spike data), the
`docs/design/architecture.md` update confirmed with the user and applied, and
docs/plans/00-roadmap.md's gate row for plans 21/25/44 flipped to unblocked.

## 10. Rollback/abort notes

- **Phase 0b spike fails on coexistence (S1)**: fall back through open question 2's ladder —
  JUCE-window+bgfx (drop SDL3; gamepad gap handled in plan 26 via a dedicated input library or
  JUCE facilities, evaluate then), then SDL3+SDL_GPU, then the milestone-0 floor: JUCE's own
  `OpenGLContext` rendering scrolling rectangles (matches `docs/design/architecture.md`
  "Development Approach": rectangles first) while the stack decision reopens. The spike branch
  is throwaway by design; aborting costs nothing on `refactor`.
- **Phase 0b fails only S3 (Conan)**: not an abort — switch to vendored submodules (open
  question 4 option B) and continue.
- **Phase 1 regression risk**: the loop swap touches `main.cpp` only after the spike proved the
  pattern; if post-merge instability appears, revert the app commit — core/audio/ui additions
  are additive libraries and can stay.
- **Phase 3**: if the frame loop starves audio or vice versa under real load, the loop option
  choice reopens at the gate level (this is why S1 requires a soak, not a smoke).
- **Phase 4**: the diagnostics layer is additive; individual panels can be reverted without
  touching the loop. The hot-reload watcher is dev-flag-gated, so a faulty watcher never affects
  normal runs.
- **General**: no phase here modifies editor code, common/core domain types, or package
  formats; the worst-case rollback is deleting game-module additions and restoring the
  81-line shell `main.cpp`.

## Gate record

(To be filled at gate closure: 0a platform-scope answer; 0b findings table with S1–S6
measurements, chosen stack + loop option, juce-tracktion-expert citations; 0c seam choice;
design-doc update confirmation; game-render-expert agent creation note.)
