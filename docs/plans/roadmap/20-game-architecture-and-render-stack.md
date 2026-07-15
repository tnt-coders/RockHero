# Plan 20 — Game Architecture and Render Stack

**Status: G20-RENDER CLOSED — user signed off 2026-07-10 (stack SDL3+bgfx, loop L2, seam
Option 1; alternatives analysis, stage/band and engine questions all answered in the Gate
record). Closure actions applied: architecture.md amended (Game View / Threading Model),
.claude/agents/game-render-expert.md created. Phase 1 complete 2026-07-10 (checkpoint answers +
wiring decisions in the Gate record's Phase 1 record); Phase 2 complete 2026-07-10 (resource-pack
convention; correction: the Phase 1 shaderc IMPORTED shim never worked — Conan's CMakeDeps
already declares bgfx::shaderc as an interface-library component, so the guarded shim was
skipped; the compile helper now invokes the find_program path directly); Phase 3 complete
2026-07-10 (frame clock + timing instrumentation; checkpoint answers and the timing contract in
the Gate record's Phase 3 record; exit criterion amended with user sign-off — the S1-mirror
engine soak transferred to plan 21's first engine-embedding phase, G21-TRACKTION-GO closed GO
the same day); Phase 4 complete 2026-07-11 (dev-diagnostics layer: headless DiagnosticsState +
typed intents + ChartSourceWatcher in game/core with tests; frame-time-graph overlay drawn
through the shared renderer's overlay pass; chart hot-reload on the --dev-package source;
autoplay stub flag + HUD tag; activation per 20-Q5 A — `--dev` runtime flag, F1/F2/F5/PgUp/PgDn
key toggles, the shared logger gained a runtime-level config so the Phase 3 trace channel
records under --dev, bgfx init.debug wired to the flag). Seam amendment (user decision
2026-07-11): the highway renderer itself was promoted to rock-hero-common/ui behind a bgfx-free
pimpl seam (Tracktion-style isolation) so the editor preview and the game share one renderer —
superseding Option 1's duplicated-thin-drawers cost; the headless scene model stays in
common/core, and RenderDevice moved to common/ui render/ (its header was already bgfx-free).
Downstream plans 21/25/44 unblocked; final acceptance bundle pending.** | Date: 2026-07-11 |
Baseline: `work-in-progress @ 050f884e` (spike evidence on `spike/render-stack @ 049c898c`)

This plan is the structural gate for all game-side work. Phases 0a–0c form the decision gate; no
phase after the gate — and no dependent plan (docs/plans/roadmap/25-note-highway-3d.md,
docs/plans/roadmap/44-editor-3d-preview.md, the render-loop parts of
docs/plans/roadmap/21-game-audio-engine-and-session.md) — starts before the gate closes with explicit
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

- No note highway content or Charter-parity visuals — that is docs/plans/roadmap/25-note-highway-3d.md.
- No gameplay session orchestration, tone switching, or mix controls — that is
  docs/plans/roadmap/21-game-audio-engine-and-session.md.
- No detection, scoring, menus, library, or settings UI (plans 22, 24, 26, 27).
- No editor preview implementation — docs/plans/roadmap/44-editor-3d-preview.md consumes this plan's
  phase 0b/0c outcomes; this plan only produces the facts 44 needs.
- No commitment to SDL3/bgfx before the spike passes. `docs/design/architecture.md` states them
  as the target design; this plan treats that as the leading candidate, not a settled fact.

## 3. Constraints

Applicable subset of the roadmap constraint block (see docs/plans/roadmap/00-roadmap.md):

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
  named in any file; use "RS"/neutral phrasing. Charter (BSD 3-Clause) may be named.
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
- `rock-hero-common/audio/include/rock_hero/common/audio/engine/engine.h:59-67` — `Engine`
  implements `ITransport`, `ISongAudio`, `IAudioDeviceConfiguration`, `IAudioMeterSource`,
  `IPluginHost`, `ILiveInput`, `ILiveRig`, `IToneAutomation`, `IThumbnailFactory` (correction
  2026-07-09: `IToneAutomation` added by the tone-automation work; line numbers refreshed).
  Header doc (lines 46-49): "Most public methods must be called on the message thread" — the game
  process must keep a live JUCE message thread for Tracktion regardless of who owns the frame
  loop.
- `docs/design/architecture.md` — "Technology Stack" table lists "Game rendering | SDL3 + bgfx"
  as target; "Game View" section states "SDL is initialized without its own event pump. SDL
  events are polled manually from JUCE's message loop"; "Threading Model" defines audio /
  analysis / UI / optional render threads; "Development Approach → Build First: Timing
  Instrumentation" mandates permanent timing logs before gameplay.
- `docs/plans/roadmap/25-note-highway-3d.md` (which absorbed the retired docs/plans/todo 3D-highway analysis) —
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

Verified against code on 2026-07-06, refactor @ 3c7febe0. Re-verified for Phase 0a on
2026-07-09, work-in-progress @ 39aa1168 (corrections: engine.h interface list and line
references above; all other claims confirmed unchanged — `main.cpp` shell, game placeholder
libs, `conanfile.txt` contents, app CMake resource registration). Re-verified for Phase 3 on
2026-07-10, master @ 27188b94: the `main.cpp`/placeholder-lib/no-SDL claims above describe the
pre-plan tree by design (Phases 1–2 replaced them; see their records); the Phase 3 dependencies
still hold — engine.h message-thread contract (now lines 46-50), shared quill logger, and plan
12's delivered clock surface (`IPlaybackClock`, `PlaybackClockSnapshot`,
`PlaybackClockExtrapolator` under rock-hero-common/audio `clock/`).

## 5. Dependencies

- docs/plans/roadmap/00-roadmap.md — execution ordering and the Decisions-needed mirror of this plan's
  open questions and gate.
- docs/plans/roadmap/12-playback-clock.md — Phase 3 consumes its `IPlaybackClock` mirror and the
  game-side extrapolation policy; Phase 3 cannot finish before plan 12's port exists.
- External decisions: the user's sign-off on phases 0a, 0b, 0c (the gate).
- Downstream (for the roadmap graph, not blocking this plan):
  docs/plans/roadmap/25-note-highway-3d.md (gated on 0c seam + Phase 1),
  docs/plans/roadmap/44-editor-3d-preview.md (gated on 0b child-HWND finding + 0c),
  docs/plans/roadmap/21-game-audio-engine-and-session.md (consumes 0b coexistence outcome),
  docs/plans/roadmap/26-game-startup-menus-library.md (consumes this plan's frame-pacing/vsync policy
  and the resource-pack convention), docs/plans/roadmap/23-detection-verification-harness.md (the
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
  defect fixes — `docs/plans/roadmap/25-note-highway-3d.md` § Decisions already made. Its headless
  `HighwayViewState` concept (seconds-resolved, camera-agnostic frame content; pure-math camera;
  renderer consumes view state only) is the input to phase 0c.
- Headless behavior lives in core modules; frameworks stay in thin adapters; time/threading at
  the boundary — `docs/design/architectural-principles.md` ("Core Position", "Library Roles",
  "Time Must Be a Dependency").
- Repo license is AGPLv3; SDL3 (zlib) and bgfx (BSD-2-Clause) are license-compatible —
  `docs/design/architecture.md` ("Licensing").

## 7. Open questions for the user

Mirror all of these into docs/plans/roadmap/00-roadmap.md Decisions-needed.

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
  docs/plans/roadmap/00-roadmap.md Decisions-needed.
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
    finding decides docs/plans/roadmap/44-editor-3d-preview.md's shape and open question 2's fallback.
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

Decides where highway rendering code lives before docs/plans/roadmap/25-note-highway-3d.md bakes
geometry into one backend — otherwise the highway gets written twice (once for the game, once
for docs/plans/roadmap/44-editor-3d-preview.md).

- **Scope**: choose between:
  - **Option 1 — headless scene model in common (recommended)**: a `highway/` feature in
    `rock-hero-common/core` owning the camera math, lane layout, note transforms, and the
    seconds-resolved `HighwayViewState` (promoting the concept from docs/plans/roadmap/25-note-highway-3d.md
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

- **Checkpoint (before implementing)**: consult `.claude/agents/cmake-conan-expert.md` on the
  build-wiring choices — CMakeConfigDeps vs an IMPORTED-executable shim for the packaged bgfx
  tools (a provider-fork question, not a conanfile tweak), the Conan version pins, and the CI
  Conan-cache follow-up flagged under S6 — and `.claude/agents/game-render-expert.md` on the
  runtime choices — bgfx init/reset flag set for the shipped window and SDL subsystem
  initialization order. Record the answers in this phase's commit or the gate record.
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

### Phase 3 — Threading model and frame clock (assumes outcome: gate loop option; depends on docs/plans/roadmap/12-playback-clock.md)

- **Checkpoint (before implementing)**: consult game-render-expert on bgfx frame-timing
  semantics under the chosen loop — what `bgfx::frame()` blocks on with vsync ON, how present
  timing relates to the submitted frame, and which bgfx stats feed the instrumentation — so the
  frame-clock helpers measure what they claim to measure.
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
- **Exit criteria** (amended 2026-07-10 with user sign-off): instrumentation log lines present;
  frame-clock tests green; window renders with the loop sampling song time exclusively through
  the clock port. The original "backing engine playing (manual soak mirroring S1)" criterion
  transfers to plan 21's first engine-embedding phase — engine-in-game sits behind
  G21-TRACKTION-GO (closed the same day: GO), and the first moment an engine exists in the game
  process is where that soak is meaningful.
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
    docs/plans/roadmap/23-detection-verification-harness.md (its autoplay bot) — this phase only
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
docs/plans/roadmap/00-roadmap.md's gate row for plans 21/25/44 flipped to unblocked.

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

### 0a — Platform scope (20-Q1): **A — Windows-first, cross-platform-preserving**

Answered by the user on 2026-07-09: no multi-platform CI now; dependencies come through Conan
recipes that are cross-platform from the start, so portability is preserved by dependency and
abstraction *choices*, not by CI legs.

- **Target platforms.** Windows 10/11 x64 is the only shipped and tested platform now. Linux and
  macOS remain future targets preserved by choice-discipline: candidate stacks must be
  cross-platform-capable (SDL3, bgfx, and JUCE all are), portable layers take no Win32-direct
  dependencies (Win32 specifics stay in adapter files, the way ASIO already sits behind
  common/audio), and render dependencies arrive through multi-platform Conan recipes. The Linux
  `.desktop`/icon-theme registration already in `rock-hero-game/app/CMakeLists.txt` stays.
- **Per-OS renderer backends that must work now.** bgfx Direct3D 11 on Windows (bgfx's most
  mature backend) is the required, proven backend. Backend selection stays an init-time choice
  (native bgfx capability), so Vulkan can be evaluated later without a build-graph change. The
  shaderc pipeline (criterion S4) must be structured so adding compiled shader profiles (spirv,
  metal, glsl) later is a profile-list edit, not a redesign; only the D3D11 (`s_5_0`) profile
  must compile and ship now.
- **What CI must prove.** The existing Windows CI leg remains the only leg. It must configure and
  build with the new render dependencies (S3 records recipe viability, S6 the wall-clock cost)
  and run all tests GPU-less and window-less via bgfx's Noop renderer (S5). No Linux/macOS CI
  until there are users there; cross-platform preservation is enforced by review against this
  memo.
- **Feed into 0b criteria.** Cross-platform *capability* is a hard criterion for any candidate
  stack (rules out D3D-only render designs and Win32-only loop designs in portable layers);
  cross-platform *testing* is explicitly not a criterion.

Working decision taken to run the spike (final sign-off at the gate STOP with 0b/0c): 20-Q2 = B —
spike SDL3+bgfx as primary plus the JUCE-window+bgfx fallback branch.

### 0b — Render-stack spike findings (2026-07-10, spike/render-stack @ 049c898c)

Stack under test: Conan `sdl/3.4.8` + `bgfx/1.129.8930-495` (tools=True), Direct3D 11, vsync ON,
single-threaded bgfx (renderFrame-before-init), against the real `common::audio::Engine` playing
generated audio through the machine's ASIO interface (NeuralDSP USB, 48kHz/128). Spike code
stays on the throwaway branch; only this write-up merges.

**Loop-option enumeration (source-verified via juce-tracktion-expert):**

- **L1 — JUCE owns the loop.** Frame ticks are queue-delivered: Timer floor 1 ms, batch-fired,
  one in-flight timer message (juce_Timer.cpp:83-180); VBlankAttachment exists but requires an
  on-screen JUCE-component peer (juce_VBlankAttachment.h:44-98, juce_VBlank_windows.cpp:116-152)
  so it cannot tick an SDL-window scene. A continuous self-post chain starves WM_PAINT (JUCE's
  pump is an uncompensated GetMessage loop, juce_Messaging_windows.cpp:114-156 — matches the
  project's prior callAsync finding). MEASURED (11-min soak, Timer@1ms tick): 94,853 frames,
  avg 6.96 ms (144 Hz vsync), max 502 ms (one-off, during live plugin instantiation), 4 frames
  >20 ms; heartbeat max gap 309 ms (same event); 0 transport regressions; drift 37.6 ms/11 min;
  VST editor opened mid-soak and took focus (foreground="Gateway").
- **L2 — SDL owns main().** `initialiseJuce_GUI()` is just `MessageManager::getInstance()` and
  binds the calling thread (juce_MessageManager.cpp:38-45, 457-463). The canonical non-blocking
  pump primitive `juce::detail::dispatchNextMessageOnSystemQueue(true)` has no public header but
  is forward-declared by JUCE's own cross-module consumers (juce_FileChooser_windows.cpp:41,94);
  a bounded per-frame drain is mechanically JUCE's own modal-pump pattern.
  `runDispatchLoopUntil` is compiled out (`JUCE_MODAL_LOOPS_PERMITTED=0` default,
  juce_PlatformDefs.h:321-329). JUCE message delivery does not require JUCE's pump (hidden-window
  wndproc drains the internal queue, juce_Messaging_windows.cpp:160-171). Degradation at 60 Hz
  pumping is latency-only: queued messages wait ≤ one frame; `callBlocking` rendezvous stalls ≤
  one frame (tracktion_AsyncFunctionUtils.h:159-232). MEASURED (11-min soak): 94,852 frames,
  avg 6.96 ms, max 538 ms (plugin instantiation), 8 frames >20 ms; heartbeat max gap 346 ms
  (same event); 0 transport regressions; drift 38.8 ms/11 min; max per-frame queue drain 3
  messages; VST editor opened mid-soak (focus evidence from the L1 run and shakedowns:
  foreground="Gateway").
- **L3 — dedicated render thread.** Not prototyped: message-thread submission already sustains
  144 Hz with audio playing and a live plugin window, so L3's added thread-affinity risk buys
  nothing for milestone 0. Reopen only if profiling shows submission starving the message thread.

**Audio-safety fundamental (why loop choice cannot glitch audio):** playback is audio-thread
self-contained — the device callback fills the graph under a shared_lock and advances the
playhead with atomic stores (tracktion_DeviceManager.cpp:1491-1598,
tracktion_EditPlaybackContext.cpp:935-937, tracktion_PlayHead.h:166-373); Tracktion's
message-thread work is ≤50 Hz state reconciliation (tracktion_TransportControl.cpp:704,
1049-1109). Confirmed empirically: both soaks played through live plugin insertion with zero
transport regressions.

**Criteria results:**

| # | Criterion | Result | Evidence |
|---|---|---|---|
| S1 | Coexistence soak | **PASS** (both loop options) | Two 11-minute soaks (numbers above): audio + 400-cube scene at 144 Hz + heartbeat + live VST editor window with focus transfer; zero transport regressions; drift ≈0.006% (steady-vs-audio clock skew, absorbed by plan 12's extrapolator). ASIO device does not report xrun counts (getXRunCount() = -1) — dropout evidence is transport monotonicity + drift + heartbeat continuity. |
| S2 | bgfx in a JUCE child HWND | **PASS** (DPI not exercised) | 3-minute run, 25,852 frames into a raw Win32 child of a JUCE DocumentWindow, VBlankAttachment-driven; survived two programmatic resizes (2 bgfx resets), minimize/restore, and 1,021 JUCE panel paints alongside (paints not starved); no device loss, no crash. Single monitor at scale 1.0 — DPI-change behavior not exercised; re-check when plan 44 lands its preview window. |
| S3 | Conan delivery under repo presets | **PASS** | Both recipes resolve from Conan Center into the per-preset caches. No prebuilt binaries for msvc/195/Debug → one-time source build ≈ 6.5 min local (13-package graph incl. miniz/tinyexr/libsquish); `tools=True` builds and packages shaderc.exe. Caveats: (1) classic CMakeDeps declares no executable targets for packaged tools (recipe documents a CMakeConfigDeps requirement), so bgfxToolUtils.cmake's helpers self-disable — production wiring adopts CMakeConfigDeps or an IMPORTED-executable shim; (2) `conan download --only-recipe` into a live per-preset cache corrupts later source fetches (s.dirty) — avoid, or `conan remove` first. |
| S4 | shaderc in the CMake graph | **PASS** | Spike .sc pair + varying.def compiled at build time via add_custom_command (s_5_0 / D3D11 profile) using the packaged shaderc located via find_program off the exported BGFX_SHADER_INCLUDE_PATH; outputs deployed exe-relative; the soak scene renders from them through the build helper. Adding profiles later is a list edit (0a memo requirement met). |
| S5 | Headless CI path | **PASS** | `spike_render_tests` (Catch2): bgfx init(Noop) with no GPU/window/platform data + 4 frames + clean shutdown; discovered and run by `-RunTouchedTests`. Re-implemented properly as the first real game/ui test in Phase 1. |
| S6 | Measured CI cost | **PARTIAL** (local numbers; CI follow-up flagged) | Local: baseline configure 15 s (warm); one-time dependency source build ≈ 390 s wall; post-cache configures return to baseline. CI runs external reusable workflows (tnt-coders/ci-workflows) — their Conan-cache behavior must be confirmed at Phase 1: without a `.conan2` cache step every CI run pays the full source build (est. 20–40 min hosted-runner class). |

**Integration findings (recorded so later phases never re-learn them):**

1. Transport verbs issued before the message loop has run after engine setup do not take; a
   play() issued from the running loop works immediately. Game shells start playback after the
   loop is live — non-issue in production, footgun in tests/tools.
2. The plugin chain lives inside the multi-tone live rig: `insertPlugin` without a loaded rig
   fails (no audible branch). Minimal path: `mintEmptyTone` → `loadLiveRig` (async, completes
   via the pump) → insert works.
3. Tear the rig down through `clearLiveRig()` while the pump still runs; letting an open plugin
   editor window die inside `~Engine()` after SDL/bgfx teardown crashed (0xC0000005).
4. With no `JUCEApplication` instance, JUCE's pump consumes WM_QUIT silently — quit handling is
   the shell's job under L2. JUCE modality swallows input aimed at non-JUCE HWNDs while a JUCE
   modal is up (juce_Windowing_windows.cpp:2182-2241).
5. bgfx stays single-threaded via renderFrame-before-init even though the Conan package compiles
   BGFX_CONFIG_MULTITHREADED=1.

**Recommended stack + loop option: SDL3 + bgfx with loop option L2 (SDL owns main; bounded JUCE
queue drain per frame).** Both loop options measured equivalently here, so the choice rests on
the structural facts: L2 owns frame pacing directly, does not funnel ticks through the timer
queue, and makes quit/input ownership explicit; L1 remains a proven fallback, and the S2
child-HWND path is proven for plan 44's editor preview.

### 0c — Renderer-sharing seam (2026-07-10): **Option 1** (working decision, pending sign-off)

Headless highway scene model in `rock-hero-common/core` (`highway/` feature), thin per-product
render backends; bgfx never enters common. S2's pass keeps Option 2 technically alive, but
Option 1 stands on the layering argument (constraint (a), architectural-principles "Core
Position") independent of spike evidence, and it is what makes plan 44's preview cheap.
Coordination note: docs/plans/roadmap/00-roadmap.md lists plan 25 Phases 1–2 as pre-gate work while
plan 25's own status line waits for full gate sign-off; this seam record fixes the scene-model
home those phases need, and plan 25's rollback note prices a later home flip as mechanical.

### Stack alternatives analysis (2026-07-10, user-requested before sign-off)

Question: is SDL3+bgfx truly the best choice, considering all alternatives? Method: compare
against the discriminating requirements — (R1) two render surfaces from one renderer instance
(standalone game window AND a child HWND inside a JUCE editor preview window, per plan 44);
(R2) Windows-first with cross-platform-preserving choices (0a memo); (R3) AGPLv3-compatible
license; (R4) deliverable via Conan under the CLion-CMake/VsDevCmd environment; (R5) build-time
shader compilation inside the preset build; (R6) modest rendering needs (textured quads,
perspective, glow, particles — integration cost and stability discriminate, not raw power);
(R7) gamepad input for plan 26's menus; (R8) message-loop coexistence with JUCE/Tracktion.

**Windowing/input axis:**

- **SDL3** — keep. S1-proven coexistence; the class-leading gamepad subsystem is the reason
  26-Q3 recommends it; zlib license; Conan-proven.
- **GLFW** (Conan: glfw/3.4) — lighter, but mapping-table-only gamepad support and no benefit
  over SDL3 anywhere else. Rejected.
- **JUCE-window-only** — S2-proven; remains the recorded fallback; lacks gamepad entirely
  (26-Q3's fallback (c) would apply). Fallback, not primary.

**GPU axis:**

- **bgfx** — keep. S1–S5 proven in this repository; widest backend set (D3D11/D3D12/Vulkan/
  Metal/GL — broadest Windows hardware reach plus the 0a portability story); Conan recipe with
  packaged shaderc proven inside the preset build (S4); BSD-2; a decade of shipped-game miles.
  Known costs, all planned around: its `.sc` shader dialect, bring-your-own text (plan 25's
  glyph atlas is designed in), one process-wide instance (fine — multi-window uses per-window
  frame buffers over native handles, the documented bgfx pattern; S2 proved the child-HWND
  surface half of R1).
- **SDL_GPU** (ships inside SDL3) — the one credible challenger: would collapse the stack to a
  single dependency, modern command-buffer API, shipped in production via FNA console titles.
  Decisive negatives for this project: backends are D3D12/Vulkan/Metal only (no D3D11 — narrower
  Windows hardware reach than bgfx); the shader toolchain is the separate SDL_shadercross
  satellite (HLSL/SPIR-V → DXIL/MSL/SPIR-V), which is NOT on Conan Center (verified 2026-07-10)
  — re-opening exactly the build-graph risk S4 just retired; and rendering into a foreign JUCE
  child HWND requires wrapping it as an SDL window via creation properties — plausible but
  unproven here, so reaching bgfx-equivalent confidence costs a second spike (S2/S4 redo) for a
  dependency-count win and no capability our modest needs use. Rejected for v1; recorded as the
  natural re-evaluation candidate if bgfx ever stalls.
- **Diligent Engine** (Conan: diligent-core/2.5.x — verified) — well-documented, native
  multi-swapchain, Apache-2.0. A heavier C++ framework surface with fewer shipped-game miles
  than bgfx and zero in-repo evidence; nothing our requirements reward over bgfx. Rejected.
- **sokol_gfx** (Conan: sokol/cci.20251113) — elegant single-header, but single-context by
  design: the two-surface requirement (R1) is exactly its weak spot; windowing still needed;
  sokol-shdc is another external tool binary. Rejected.
- **wgpu-native / Dawn (WebGPU)** — not on Conan Center (verified 2026-07-10); Dawn is a giant
  vendored build; the native-layer C API and WGSL toolchain are still churning. Rejected for
  now; the most likely "in five years" candidate.
- **The Forge / full engines (Godot et al.) / raw D3D11 / JUCE OpenGL** — rejected respectively
  for framework weight, loop-and-audio ownership conflicts with Tracktion, the Windows-only
  commitment 0a option A already rejected, and the GL deprecation trajectory (JUCE GL stays the
  recorded milestone-0 emergency floor only).

**Conclusion: SDL3 + bgfx stands — now by comparative analysis, not just spike momentum.** The
decision rule mirrors 22-Q2: adopt an alternative only on a decisive, cited win. None was found;
the one close call (SDL_GPU) trades proven in-repo evidence, D3D11 reach, and an in-build shader
pipeline for one fewer dependency, and would need its own spike to tie the confidence bgfx
already has.

**Future-proofing note (2026-07-10, user question at sign-off):** a classic-GH-style 3D stage
with an animated band was assessed against this stack and requires no structural change: bgfx's
altitude (GPU abstraction, not scene system) supports skinned characters/venues/effects; the
addition would be an animation/asset layer in game-land (e.g. ozz-animation + glTF loading +
resource-pack `models/`/`animations/`), venue passes behind plan 25's highway views (its
"background" slot), and — if ever needed — bgfx's native render-thread mode (the L3 escalation
already recorded). The deferred cost is content authoring, not architecture.

The full-engine question (Unreal et al.) was asked and answered at the same time: engines are
disqualified by this project's constitution, not by rendering ability. (1) The live audio core
(Tracktion/JUCE hosting VST3 on ASIO input at sub-10 ms) cannot ride an engine's audio system
and would have to be embedded as a hostile guest; (2) Unreal's EULA is incompatible with the
AGPLv3 that JUCE/Tracktion's zero-cost tier requires, so an engine converts a free stack into
commercial JUCE + Tracktion licenses; (3) the shared highway-scene seam with the JUCE editor
preview (the 2026-07-10 twin-track directive) is impossible across an engine boundary. An
engine becomes right only if the product pivots to presentation-first (story modes, consoles)
with live audio secondary — a different product, noted here so the shape is on record.

**Gate status: 0a answered (20-Q1: A, by the user). 0b and 0c above are complete with working
decisions 20-Q2: B (executed), 20-Q3: 1, 20-Q4: A (evidence: S3 clean), 20-Q5: A and 20-Q6: A
(written recommendations adopted for Phases 1+). STOP — the gate closes only on explicit user
sign-off of this record. Gate-closure actions still pending: amend docs/design/architecture.md
(Technology Stack / Game View / Threading Model) to the proven outcome (user confirmation
required), and create .claude/agents/game-render-expert.md seeded with these findings.**

### Phase 1 record (2026-07-10) — checkpoint answers and wiring decisions

Both Phase 1 checkpoint consultations ran before implementation; decisions adopted:

**Build wiring (cmake-conan-expert):**

- **Packaged bgfx tools: IMPORTED-executable shim, not CMakeConfigDeps.** The provider fork
  injects `-g;CMakeDeps` unconditionally for conanfile.txt projects
  (`conan_provider.cmake:1131–1143`), so declaring `CMakeConfigDeps` would run both generators
  into one folder; a real migration touches the provider fork + its pytest suite + the
  classic-CMakeDeps `<prefix>_PACKAGE_FOLDER_<CONFIG>` recovery in
  `cmake/RockHeroExternalModules.cmake`, all to adopt a generator Conan still marks
  *experimental* (no Find-module support planned) while CI floats its Conan version. The shim —
  `find_program` off the recipe-exported `BGFX_SHADER_INCLUDE_PATH`, an `if(NOT TARGET)`-guarded
  `bgfx::shaderc` IMPORTED executable, and the project-owned `rock_hero_add_compiled_shader`
  helper — lives in `cmake/RockHeroRenderStack.cmake` and becomes a no-op if a future
  CMakeConfigDeps migration generates the real target. Revisit when the generator leaves
  experimental status and upstream cmake-conan adapts its own `-g` handling.
- **Pins confirmed current (2026-07-10):** `sdl/3.4.8` (newest on Conan Center) and
  `bgfx/1.129.8930-495` (only version on the modern cmake recipe), with
  `[options] bgfx/*:tools=True` — the option only adds tool components (library components are
  unchanged) but changes the package_id, so no prebuilt binaries exist and the one-time source
  build recurs per cache. SDL stays on recipe defaults (static).
- **CI Conan cache (S6 follow-up):** CI *already* caches `build/release/.conan2/p` in the shared
  `cpp-cmake-conan-setup` action, keyed `conan-<os>-hashFiles('**/conanfile.*')`. Gaps found:
  the key omits the runner image/toolset version and the floating Conan version (a toolset bump
  would pin a stale exact-match cache forever), sweeps the provider fork's test conanfiles into
  the hash, and never prunes build trees before saving (bgfx tools=True will bloat the archive).
  Recommended re-key (OS + image version + pinned Conan + root conanfile + `conan-recipes/**`),
  `conan cache clean` pre-save, a stated no-unconditional-save invariant (protects against the
  `s.dirty` corruption trap), and an integrity-check-or-wipe on restore. **These changes belong
  to the external tnt-coders/ci-workflows repo — flagged to the user, not applied here.** Also
  expect the Linux CI leg to need extra system packages once bgfx mainlines (`xorg/system`,
  `opengl/system`, `wayland` transitive requires).

**Runtime wiring (game-render-expert):**

- **bgfx init:** renderer pinned `Direct3D11` through a one-entry platform table (never
  `Count`/auto-select — only D3D11 has soak evidence); `BGFX_RESET_VSYNC` held in a single
  stored flag set because reset flags are absolute, not deltas; resolution in *pixels*;
  `format`/`numBackBuffers`/`maxFrameLatency` left at defaults (latency tuning deferred to the
  Phase 3 checkpoint); `init.debug/profile` explicit-false, to be wired to the Phase 4 dev flag
  (bgfx degrades gracefully without D3D11 SDK layers); `bgfx::renderFrame()` once before
  `bgfx::init` pins single-threaded mode; teardown mirror is plain same-thread
  `bgfx::shutdown()`.
- **Resize:** listen to `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED` and re-query
  `SDL_GetWindowSizeInPixels` — the spike's `WINDOW_RESIZED` + window coordinates only worked
  because scale was 1.0 (S2's DPI blind spot); `SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED` is
  logged as evidence when it first fires.
- **Startup/teardown:** `SDL_MAIN_HANDLED` + plain `main` + `SDL_SetMainReady()`;
  `SDL_INIT_VIDEO` only (gamepad is plan 26's additive subsystem); window flags
  `RESIZABLE | HIGH_PIXEL_DENSITY`; JUCE initialised via `ScopedJuceInitialiser_GUI` on the main
  thread; per frame SDL poll → bounded JUCE drain (soak-measured max 3; bound 256 as safety
  valve) → submit → `bgfx::frame()`; quit signal is `SDL_EVENT_QUIT` only (the JUCE pump
  swallows WM_QUIT under L2 by design); teardown made structural: future engine stops/clears its
  rig and is destroyed **before** bgfx/SDL teardown, JUCE shutdown last (the spike's
  `~Engine`-after-`SDL_Quit` ordering survived only by accident).

Implementation landed: `conanfile.txt` pins; `cmake/RockHeroRenderStack.cmake` (wrapper aliases
`rock_hero::sdl3`/`rock_hero::bgfx`, shaderc shim, shader-compile helper — first `.sc` consumer
arrives with Phase 2's resource tree); `rock-hero-game/ui` `surface/` feature (`GameShell` public
composition point, private `GameWindow`/`RenderDevice`/JUCE pump units); `rock-hero-game/app`
swapped from the JUCE `DocumentWindow` shell to a plain `main()` composing `GameShell`
(PackageBuilder now embeds the icon; the JUCE version resource is gone with `juce_add_gui_app`);
first `rock-hero-game/ui/tests` target with the headless Noop suite. A `--smoke-frames <n>`
diagnostic argument gives automated verification a bounded clean-exit run.

### Phase 3 record (2026-07-10) — checkpoint answers and the frame-timing contract

Game-render-expert checkpoint on bgfx frame-timing semantics (single-threaded, D3D11, vsync ON),
source-verified against the Conan bgfx package:

- **`bgfx::frame()` order is flip-BEFORE-render**: each call first Presents the *previous*
  frame's content (`Present(1, 0)`, the only vsync-blocking point), then executes the frame just
  submitted. So a frame's own Present happens at the top of the *next* `frame()` call — bgfx
  adds one frame of flip deferral before DXGI even sees the content.
- **Queue depth**: bgfx sets `SetMaximumFrameLatency` from `init.resolution.maxFrameLatency`
  (default 0 → DXGI default 3); no waitable-object swapchain (compiled out); FLIP_DISCARD
  windowed adds ~1 DWM composition frame. Steady-state sample-to-photons ≈ **4–5 refresh
  periods** when vsync-limited (~28–35 ms at 144 Hz, ~67–83 ms at 60 Hz), collapsing to ~1–2
  vblanks below refresh rate — regime-dependent, quasi-constant within a regime. **Plan 13's
  video-offset calibration owns this constant**; Phase 3 deliberately does not compensate.
  Latency levers if ever needed (both init-time, both change the soaked regime — plan 13's
  call): `init.resolution.maxFrameLatency = 1` and `BGFX_RESET_FLIP_AFTER_RENDER`.
- **Stats trust table** (`bgfx::getStats()` right after `frame()`, API thread): `cpuTimeFrame`
  fresh (stamped inside the call that just returned — the loop period); `cpuTimeBegin/End` +
  `numDraw` describe the previous frame; `gpuTimeBegin/End` describe the newest *completed* GPU
  frame, keyed by `gpuFrameNum` (typically 2–4 back), and the frame-level GPU timer runs
  unconditionally on FL≥10_0 (no `init.debug` needed, no cost to add); `maxGpuLatency` is an
  all-time high-water mark with debug off (never per-frame); `waitRender/waitSubmit` are always
  zero single-threaded.

**Instrumentation contract implemented** (`game.frame` log category): `frame_sample_time` —
one steady-clock stamp at the start of frame building, the `monotonic_now` fed to the
extrapolation path, so everything drawn in a frame shares one coherent song time;
`frame_boundary_time` — steady-clock stamp immediately after `submitFrame()` returns, the
pacing anchor (Present-return of frame N−1 + frame N's CPU submit; vblank-aligned only when
vsync-limited; never photon time); `clock_mirror_age` = `frame_sample_time` − snapshot capture
stamp, −1 while unpublished (zero-stamp sentinel). Per-frame record at Trace (dormant at the
logger's default Info runtime level until Phase 4's dev flag raises verbosity) plus a ~1 Hz
pacing summary at Info (count/min/max/avg over an accumulated 1 s window); bgfx `cpuTimeFrame`
logged per frame as the cross-check channel. Helpers are pure and tested in
`rock-hero-game/core` `frame_clock/` (`FrameClock`, `FramePacingStats`), consuming plan 12's
snapshot + `PlaybackClockExtrapolator`; the shell composes them and owns all clock reads (time
at the boundary). Logging backend composed in `rock-hero-game/app/main.cpp` (game log file
beside the editor's). First live run: 144 frames/s summaries, avg 6.95 ms — the S1 soak's
vsync-limited regime reproduced through the shipped loop.

**Thread map documented** in architecture.md § Threading Model (message thread == frame loop
under L2, audio thread, quill logging backend thread, plan-22 analysis slot; song time only
through the clock port). **Watch items** (`docs/tracking/watch-items.md`, the minimized-window
loop-spin and silent-Noop-fallback entries): minimized/occluded windows may stop Present throttling (bgfx has zero occlusion handling → loop
would spin; detection is free in the pacing log, throttle in the shell only if observed), and
bgfx silently swaps in the Noop renderer on device loss (pacing silently disappears — the
collapsing frame delta is the tell). Exit-criterion amendment recorded under the phase text: the
S1-mirror engine soak transferred to plan 21's first engine-embedding phase (G21-TRACKTION-GO
closed the same day: GO).
