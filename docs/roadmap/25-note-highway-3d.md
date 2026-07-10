# Plan 25 — 3D Note Highway

## 1. Status

**Decision-gated** — blocked on docs/roadmap/20-game-architecture-and-render-stack.md Phase 0a–0c
sign-off (render-stack spike and renderer-sharing seam). No spike of its own; every phase after
Phase 0 below assumes plan 20's gate closed with SDL3 + bgfx confirmed and the seam decided.
Date: 2026-07-06. Baseline: `refactor @ 3c7febe0`.

## 2. Goal

A 3D note highway for the game executable: the player looks down a guitar-neck-shaped board
scrolling toward them, with string-colored note heads on fret lanes, sustain rails, technique
glyphs, hit/miss feedback, and a camera that keeps the playable fret region readable at all
times. **The visual target is [Charter](https://github.com/Lordszynencja/Charter)'s 3D preview
(MIT), matched very closely** — Charter is the settled look for this project's 2D notation and
its 3D preview is the settled starting point for the highway. Only the seven implementation
defects catalogued in section 7 diverge from Charter's behavior: match first, improve
deliberately. The chart sidecar format (`charts/<uuid>.chart.json`) is the single data source and
already carries everything rendered here — no format changes are required by this plan.

## 3. Non-goals

- No note detection, scoring, or gameplay-rule logic — hit/miss/early/late events are *inputs*
  to the renderer, produced per docs/roadmap/24-scoring-star-power-failure.md.
- No venue/stage art beyond the parallax background layer.
- No vocals/lyrics/showlights initially (Charter draws lyrics; ours can follow later).
- No editor integration — the editor keeps the 2D tab lane; docs/roadmap/44-editor-3d-preview.md
  owns the editor-side preview built on the shared scene model.
- No star-power/failure-meter visual design — that open question belongs to plan 24; this plan
  reserves the HUD layer that will host whichever direction is chosen.
- No menu, library, or settings UI — the lefty-mirror and scroll-speed *flags* land here; their
  user-facing toggles ship with docs/roadmap/26-game-startup-menus-library.md and the settings
  store of docs/roadmap/27-in-song-flow-results-profiles.md.

## 4. Constraints

- (a) **Layering**: common never depends on editor or game code; editor and game never depend on
  each other. Anything both products need (shared highway scene model per plan 20 Phase 0c, the
  string-color palette from plan 45) is extracted to rock-hero-common FIRST, as its own phase
  with tests, before game code consumes it. Game code never includes editor headers — the
  editor's `TabViewState`/`makeTabViewState` are mirrored or promoted, never included.
- (b) **Public-header minimalism**: headers move to `include/` only when a consumer outside the
  library exists (docs/design/architectural-principles.md, "Placement Procedure for New Files").
- (c) **NAMING FIREWALL**: the commercial real-guitar game that inspired this project is never
  named in any file; use "RS" or neutral phrasing. Charter (MIT) may be cited by name.
- (h) **Builds**: all build/test/lint commands go through `.agents/rockhero-build.ps1` (usage in
  `.agents/README.md`) — never raw cmake/ctest/ninja. Intermediate phases run only the checks
  their changes warrant; the final acceptance phase is the sanctioned bundle as separate
  invocations.
- (i) **Real guitar input**: no plastic-controller assumptions; the lefty mirror exists because
  real left-handed guitarists exist.
- **Time is a dependency** (docs/design/architectural-principles.md, "Time Must Be a
  Dependency"): the render loop samples the audio-derived playback clock
  (docs/roadmap/12-playback-clock.md) every frame — never wall clock. All animation time derives
  from one frame clock; randomness is seeded per event so replays and pauses behave.

## 5. Current state inventory

- `rock-hero-game/` is a build-system skeleton: `rock-hero-game/app/main.cpp` is an 81-line JUCE
  `DocumentWindow` shell; `rock-hero-game/{core,audio,ui}/src/placeholder.cpp` are placeholder
  static libraries with `.gitkeep` include directories. No highway, render, or scene code exists
  anywhere in the repository.
- SDL3 and bgfx appear nowhere in the build. `conanfile.txt` declares only cmake-package-builder,
  catch2, libebur128, quill, ogg, vorbis. `docs/design/architecture.md` ("Game View", lines
  528–533) records the target design: SDL3 window/input, bgfx rendering, SDL initialized without
  its own event pump and polled from JUCE's message loop, bgfx rendering into the native window
  handle. Plan 20 Phase 0b validates exactly this before any phase here starts.
- The editor's 2D tablature lane is the shipped precedent this plan mirrors:
  - `rock-hero-editor/core/include/rock_hero/editor/core/tab/tab_view_state.h` —
    `TabViewState`: seconds-resolved `TabNoteView` (start/end seconds, string, fret, attack,
    mute, harmonic, vibrato, tremolo, accent, bend points, slide waypoints), `TabShapeView`
    (with projection-derived `arpeggio` flag), `TabFhpView`.
  - `rock-hero-editor/core/src/tab/tab_projection.{h,cpp}` — `makeTabViewState(Arrangement,
    TempoMap)`, private to editor/core; tested by
    `rock-hero-editor/core/tests/test_tab_projection.cpp`.
  - `rock-hero-editor/ui/src/tab/tab_view.{h,cpp}` — `tabVisibleNoteRange` (tab_view.cpp:891–905)
    binary-searches sorted starts plus a prefix-max sustain-end table for the visible-note range;
    `tabStringColor` (tab_view.cpp:862) with `g_standard_string_colors` (six Charter base colors)
    and `g_tertiary_string_colors` — the palette plan 45 extracts to common.
- `rock-hero-common/core/include/rock_hero/common/core/chart/chart.h` carries every technique
  the highway renders: bends as semitone curve points, slide waypoints with `unpitched`,
  harmonics with optional fractional `touch` position (chart.h:175–182), palm/full mutes,
  vibrato, tremolo, accent, `ChartSection{position, type}` (chart.h:285–300), chord templates
  with per-string fingerings, shapes, FHPs.
- `rock-hero-common/core/include/rock_hero/common/core/chart/chart_rules.h:24` —
  `g_max_chart_strings{8}`; chart_rules.h:33 — `g_max_fret{30}`. The highway sizes lanes from
  the tuning's string count, capped by this authority.
- Chart sidecar naming is canonical: `charts/<uuid>.chart.json`
  (`rock-hero-common/core/src/package/package_id.cpp:17-18`,
  `rock_song_package_read.cpp:717`). Chart files write `formatVersion` 1; the parser ignores it
  on read — validation is docs/roadmap/10-format-versioning-and-chart-identity.md scope, not ours.
- `rock-hero-editor/ui/src/shared/editor_theme.h` is editor-private by decision; only the
  string-color palette *data* is extracted to common (plan 45), not the theme.

Verified against code on 2026-07-06, refactor @ 3c7febe0.

## 6. Dependencies

- docs/roadmap/20-game-architecture-and-render-stack.md — **hard gate**: Phase 0a (platform
  scope), Phase 0b (SDL3 + bgfx spike: message-loop coexistence, Conan-vs-vendored, shaderc in
  the CMake graph, headless/noop-renderer CI path), Phase 0c (renderer-sharing seam: where the
  headless highway scene model lives — game/core vs common). Also supplies the game module
  layout, resource-pack conventions (shader binaries, atlases), threading model, and the
  dev-diagnostics layer this plan's debug overlay plugs into.
- docs/roadmap/12-playback-clock.md — `IPlaybackClock` and the game-side extrapolation policy; the
  render loop's authoritative song time. Phases 1–2 here run against fixed/fake clocks; Phase 3
  onward needs it for live playback.
- docs/roadmap/21-game-audio-engine-and-session.md — the GameplaySession that actually plays a
  song while the highway scrolls; required for the milestone-0 vertical slice, not for this
  plan's unit-tested core phases.
- docs/roadmap/45-editor-theme-and-string-colors.md — the shared string-color palette definition
  in rock-hero-common (its palette-extraction phase; re-verify the phase number against that
  plan at execution). Phase 3 here consumes it and must not start before it lands; the highway
  never defines its own string colors.
- docs/roadmap/24-scoring-star-power-failure.md — hit/miss/early/late and provisional-hit events
  consumed by Phase 5; the technique scoring matrix informs which technique glyphs deserve
  gameplay-feedback treatment vs cosmetic rendering.
- docs/roadmap/22-note-detection.md (via 24) — detection confidence values shown in the debug
  overlay.
- Reverse dependencies: docs/roadmap/44-editor-3d-preview.md consumes the headless scene model and
  camera per plan 20 Phase 0c; docs/roadmap/26-game-startup-menus-library.md and
  docs/roadmap/27-in-song-flow-results-profiles.md surface the lefty and scroll-speed settings
  whose flags are defined here.

## 7. Decisions already made

Restated from the absorbed 3D-highway todo plan, since deleted (written 2026-07-06 from a
source-level analysis of Charter's `preview3D` package, files under
`src/main/java/log/charter/gui/components/preview3D/`; repository claims re-verified against
this tree on 2026-07-06):

- **Charter's 3D preview is the visual target, matched very closely.** Board layout, camera
  behavior, note presentation, and effects should look near-identical. Improvements beyond the
  defect list below are proposed, not silently made.
- **Rendering is SDL3 + bgfx** per docs/design/architecture.md ("Game View"), subject only to
  plan 20 Phase 0b confirmation.
- **World coordinates and constants are Charter's, exactly** (starting values; all collected
  into one `HighwayMetrics` struct):
  - X = fret axis: fret 0 at x=0, each fret 1.2 units wide (`firstFretDistance = 1.2`, length
    multiplier 1 → equal-width frets by default).
  - Y = string axis: board surface at y=0, `stringDistance = 0.35`, bottom string at y=0.35; low
    string at the bottom by default, string order invertible by config.
  - Z = time axis: `z = (t_note - t_now) * 0.02 / scrollSpeed`; hit line at z=0; visibility
    window ~1600ms × scrollSpeed.
  - Note head half-width 0.48 (`firstFretDistance / 2.5`); sustain tail half-width one third of
    that; a bend lifts the tail by `stringDistance * 0.8` per half-step.
- **Camera reproduces Charter's behavior** — the property that makes it read well:
  - Nearly fixed: tiny fixed rotations in Charter (`rotX = 0.06`, `rotY = 0.03`, `rotZ = 0`),
    zero roll, NDC-space aspect correction.
  - **The fretboard pin**: project the world point `(camX, 0, 0)` (board surface at the camera's
    fret focus, at the hit line) into NDC, then append a pure NDC translation placing it at
    NDC y = -0.9. X offset intentionally zero: the board is pinned vertically only and slides
    left/right freely as the fret focus moves. A pure NDC translation cannot rotate or skew, so
    the pin never disturbs verticality. Reproduce exactly.
  - **Fret focus**: scan fret-hand positions in `[now, now + 3000ms]`; camera X target = world
    middle of the min/max fret range, blended 10% toward a fixed whole-neck weighted position;
    fret-span target drives out-zoom (`camY = 5 + 0.2*(span-4)`, `camZ = -2.5 - 0.2*(span-4)`);
    both targets approached with exponential smoothing `mix = 1 - pow(1 - 0.7, frameTime)` —
    frame-rate independent, ~70% of remaining distance per second.
  - Background gets a separate matrix: same camera with position/parallax divided by 4 plus a
    slow sinusoidal sway. Optional camera shake on hits (off by default), strength scaling with
    chord size, cubic decay over 1s.
- **Render pass structure** (Charter's `Preview3DPanel`): depth test GEQUAL with reversed clear,
  alpha blending on. Order: clear → background → noteboard (beat bars, lane borders, FHP lane
  highlights) → hand shapes → notes (sorted far-to-near; chord boxes drawn after as a separate
  transparent list) → strings and frets → inlays → fingering → chord names → section name.
- **Board furniture**: per-string colored string lines (same per-string color derivation as the
  2D lane — one authority); thin fret quads with three states (inactive, active within current +
  upcoming FHP windows, and a 100ms sqrt-decay hit-flash thickening up to 4x — a large part of
  the "alive" feel); beat bars as lines plus soft gradient quads, wider/brighter on measure
  downbeats, clipped horizontally to the active FHP fret range, distance-faded by a dedicated
  fading shader (fade between z(250ms) and z(50ms)); FHP lane highlights lighting
  `fretFrom..fretTo` lanes ("lit runway"); fret numbers along the board; inlays, fingering,
  chord names, section labels.
- **Notes** (Charter's `Preview3DGuitarSoundsDrawer`): atlas-textured head quads tinted the
  string color, keyed by note status; a rolling flip for single notes (rotate around Z from -90°
  to 0° during the last second: `rotation = clamp(-π(t-now-100ms)/1000ms, -π/2, 0)`; chord notes
  don't rotate); open-string notes as procedurally built wide bars spanning the FHP window
  (models cached per width); a note shadow triangle-fan on the board under every unhit note
  (load-bearing for depth perception — keep it); an anticipation ring scaling down onto the
  landing position during the last 500ms; technique overlay quads (hammer-on, pull-off,
  palm/full mute, pop, slap; accents draw a stretched glow copy; harmonics use distinct head
  textures); sustain tails as three triangle strips (left edge, inner at alpha 192, right edge)
  following a centerline sampled over time — bends interpolate linearly between bend points with
  outer-string bend inversion so curves stay inside the board; vibrato adds `sin(t·π/80ms)` Y
  wobble; tremolo adds a triangle-wave X wobble (period 60ms); slides ease toward the target
  fret (`pow(sin(progress·π/2), 3)` pitched, `1 - sin((1-progress)·π/2)` unpitched, unpitched
  additionally dimming); hit explosions of ~100 point-sprites with gravity parabola and
  red-to-dark fade over 500ms; translucent chord-box panels at chord onsets drawn after opaque
  content.
- **Shaders** — five tiny programs mirrored from Charter: color, color-texture, fading
  (`fadeStart`/`fadeEnd` uniforms multiplying alpha by a clamped Z ramp), shadow-highlight
  texture (atlas where R multiplies tint, G adds white highlight, B is alpha mask — one atlas
  serves every string color; keep this channel scheme), glyph text.
- **The seven Charter defects and our fixes** — the only intended departures, each an
  implementation defect or performance wart, not a style choice:
  1. Per-millisecond tail tessellation (`getTimeValuesToDrawForEveryPoint`): a 5-second vibrato
     note builds ~15,000 vertices per frame. Ours: sample tails adaptively at fixed screen-space
     resolution (~1 vertex per 4px of projected tail length, hard cap) — visually identical, two
     orders of magnitude cheaper.
  2. Tremolo/vibrato wobble unclamped at tail ends (absolute-time phase `pointTime % period`).
     Ours: phase the modulation from the note onset and taper amplitude to zero over the
     first/last ~10% of the tail so rails start and end on the string line — the same fix the 2D
     tremolo tail shipped.
  3. Per-frame text texture generation for fret numbers. Ours: pre-rasterized glyph atlas (bgfx
     has no text; an atlas is needed anyway).
  4. Immediate-mode rebuild of static geometry every frame. Ours: retained vertex buffers for
     strings/frets/inlays; only dynamic content streams through bgfx transient buffers.
  5. `new Random()` per camera-shake frame and wall-clock effects. Ours: one frame clock for all
     animation time, randomness seeded per event, so replays and pauses behave.
  6. Magic-constant soup. Ours: one documented `HighwayMetrics` struct holding every world-space
     constant, initialized to Charter's values, so tuning is one file.
  7. Rotation-approximated verticality (small rotX/rotY leave verticals only approximately
     vertical, hidden by the NDC pin). Ours: an off-axis (lens-shift) perspective frustum with
     zero rotation — the same composition, with world-vertical → screen-vertical made
     mathematically exact and unit-testable. Pin math unchanged; aspect correction via frustum
     extents rather than post-scale.
- **Module placement** follows docs/design/architectural-principles.md ("Library Roles",
  "Placement Procedure for New Files"): headless scene model, projection, camera math, and
  metrics in a `highway/` feature folder of the core library plan 20 Phase 0c selects
  (rock-hero-game/core by default; rock-hero-common/core if the seam decision shares it with the
  editor preview); SDL3/bgfx device, passes, drawers, and atlases in rock-hero-game/ui; the
  render loop samples the transport for authoritative song time (never wall clock).
- **Arpeggio is derived, not stored**: the highway derives chord-vs-arpeggio from sequential
  onsets under a shape span exactly as the 2D lane's projection does (`TabShapeView::arpeggio`).
- **Forward extensions the format already sketches** (render support added when the data lands):
  whammy dives = signed bend curve on the rail; between-fret harmonics = harmonic head at the
  fractional `touch` position (chart.h:175–182) instead of the fret middle.

Corrections made while absorbing: the old doc's "chart format v2" phrasing is dropped — chart
sidecars write `formatVersion` 1 today and version policy belongs to plan 10; "SDL3 + bgfx (not
OpenGL)" is restated as the architecture.md target design pending plan 20 Phase 0b; the module
sketch's "decide later" on projection sharing is superseded by plan 20 Phase 0c owning that
decision.

New decisions layered onto the absorbed plan (from the roadmap consolidation, recorded here as
normative):

- **Lefty mirror from day one**: the headless view state and camera take a `mirrored` flag that
  flips the fret-axis mapping and mirrors the camera's X focus, as pure math the renderer never
  sees. Nearly free now, painful to retrofit. The user-facing toggle ships with the settings
  work (plans 26/27); a separate string-order-invert option (Charter has one) rides the same
  seam.
- **String colors come from plan 45's shared palette** — single source of truth across editor
  tab, editor preview, and game highway. This plan never defines note/string colors.
- **Hit/miss feedback is event-driven**: the renderer is a pure function of
  `HighwayViewState` + camera state + a gameplay event feed (plan 24's hit/miss/early/late and
  provisional-hit events, timestamped in song time). Charter's status-keyed note atlas maps onto
  these states; provisional-hit visual policy follows whatever plan 24's state machine emits
  (provisional → subdued spark; confirmed → full burst; revoked → miss treatment).
- **The debug overlay is designed into the HUD from the start** per plan 20's dev-diagnostics
  layer: a screen-space overlay view hosting frame timing, playback-clock drift, hit-window
  visualization at the hit line, and detection-confidence traces — present from the first
  rendered frame, populated as upstream plans land.

## 8. Open questions for the user

1. **Fret-width taper**: Charter defaults to equal-width frets (`fretLengthMultiplier = 1`); a
   multiplier under 1 gives realistic narrowing toward the body. Options: (a) keep Charter's
   equal width; (b) taper. Recommendation: (a) for Charter parity, with the multiplier already a
   `HighwayMetrics` field — revisit after seeing real charts on screen. Not blocking.
2. **Chord fingering panels shown by default?** Options: (a) on, matching Charter's preview;
   (b) off, reduce clutter, enable via setting. Recommendation: (a) on by default — this is a
   learning-oriented game and Charter parity is the stated target; make it a setting either way.
3. **Scroll speed / visibility window as a player setting, and its interaction with
   difficulty**: options: (a) free player setting, no difficulty coupling; (b) tied to derived
   difficulty; (c) fixed at v1. Recommendation: (a) — a plain setting persisted with plan 27's
   store; no coupling to docs/roadmap/11-derived-difficulty-calculator.md output at v1.
4. **Camera shake on hits**: Charter ships it disabled as a "secret". Options: (a) implement in
   Phase 5 behind a default-off setting; (b) drop entirely. Recommendation: (a) — cheap once the
   event feed exists, and the deterministic-seed fix (defect 5) makes it replay-safe.

(Mirrored into docs/roadmap/00-roadmap.md "Decisions needed".)

## 9. Phased implementation

Phase numbering starts after the external gate. Phases 1–2 are headless and renderer-free; they
may begin the moment plan 20 Phase 0c fixes the scene-model home, even if the bgfx spike is
still integrating. Phases 3+ need the render stack in the build.

### Phase 0 — gate intake (no code)

- Scope: confirm plan 20 Phase 0a–0c sign-off; record the chosen scene-model library (below,
  `<core-lib>` = rock-hero-game/core by default, rock-hero-common/core if the seam is shared);
  confirm plan 45's palette extraction landed or schedule Phase 3 after it; re-verify this
  plan's Current state inventory against the then-current tree per docs/roadmap/00-roadmap.md
  rules.
- Exit criteria: seam recorded in this file's status line; baseline stamp refreshed.
- Verification: none (documentation step).

### Phase 1 — headless highway scene model

- Scope: `HighwayViewState` — seconds-resolved, camera-agnostic frame content mirroring the
  editor's `TabViewState` discipline (notes with start/end seconds, string, fret, technique
  fields, bend/slide payloads in seconds; shape spans with derived arpeggio; FHP windows;
  beat/measure list; sections); `highwayViewStateFor(Arrangement, TempoMap, options)` mirroring
  `makeTabViewState` (rock-hero-editor/core/src/tab/tab_projection.cpp is the reference
  implementation — mirrored or promoted per the Phase 0 seam, never included across the
  boundary); `HighwayMetrics` with Charter's documented constants; the `mirrored` (lefty) and
  string-order-invert flags in the projection options; the visible-range helper (sorted starts +
  prefix-max sustain ends, as shipped in tab_view.cpp:891–905) as a headless utility beside the
  view state.
- Files: `<core-lib>` `highway/` feature folder — `highway_view_state.h`,
  `highway_projection.{h,cpp}`, `highway_metrics.h`, plus tests.
- Public-header impact: `highway_view_state.h` and `highway_metrics.h` public (consumed by
  game/ui); `highway_projection.h` starts in `src/` and moves public only when the renderer
  target consumes it directly (constraint (b)).
- Testing: new `test_highway_projection.cpp` in `<core-lib>/tests`, linked to the production
  library per project convention; cases mirror
  rock-hero-editor/core/tests/test_tab_projection.cpp (positions→seconds via tempo map,
  technique payload mapping, arpeggio derivation, empty-chart projection) plus mirror-flag
  round-trips (mirroring twice is identity; mirrored note X = reflected unmirrored X) and
  beat/measure-list correctness against tempo-map fixtures.
- Exit criteria: projection of corpus-shaped fixture charts produces the same seconds the 2D
  projection produces for identical inputs; all tests pass.
- Verification:

  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Configure -Targets all
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
  ```

  (`-Configure` because new targets/test executables enter the CMake graph.)

### Phase 2 — camera mathematics

- Scope: `highway_camera.{h,cpp}` in the same `highway/` folder — pure math, no rendering: FHP
  window scan → target camX/span; exponential smoothing step taking an explicit frame delta;
  off-axis (lens-shift) world→clip matrices with zero rotation (defect 7 fix); the NDC pin
  offset (project `(camX, 0, 0)`, translate to configured NDC y, default -0.9); lefty-mirrored
  focus; background parallax matrix (camera / 4 + sway term with injected time).
- Files: `<core-lib>/highway/highway_camera.{h,cpp}`, tests.
- Public-header impact: `highway_camera.h` public (game/ui consumes matrices).
- Testing: `test_highway_camera.cpp` — focus targeting from FHP fixtures; smoothing convergence
  and frame-rate independence (two half-steps ≈ one full step); **the verticality invariant**: a
  world-vertical segment projects to a screen-vertical segment for every legal camera state — a
  regression test instead of a property we hope holds; pin placement (projected anchor lands at
  the configured NDC y); mirrored camera reflects unmirrored output.
- Exit criteria: all camera tests pass; matrices are deterministic functions of
  (view state, metrics, time, dt, flags).
- Verification:

  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
  ```

### Phase 3 — board and notes rendering (playable skeleton; assumes plan 20 gate outcome)

- Scope, in rock-hero-game/ui on the SDL3 + bgfx stack plan 20 integrated: bgfx views for
  background / board scene / screen-space overlay; the five shader programs (color, color_fade,
  texture_tint with the R=tint/G=highlight/B=alpha channel scheme, glyph text) compiled through
  plan 20's shaderc wiring and packaged per its resource conventions; static board in retained
  vertex/index buffers — strings tinted from **plan 45's shared palette**, frets, inlays; beat
  and measure bars with the distance-fade shader; FHP lane highlights; note heads with atlas
  tinting and the rolling flip, open-note bars spanning the FHP window, note shadows, plain
  sustain rails, far-to-near sorting, passed-note fade; fret-number rail from a pre-rasterized
  glyph atlas (defect 3 fix); dynamic content streamed via transient buffers from
  `HighwayViewState` each frame using the Phase 1 visible-range helper; render loop samples
  plan 12's playback clock; debug overlay v1 (frame timing, clock drift readout) on the overlay
  view. The lefty flag flows through end to end.
- Files: rock-hero-game/ui `highway/` feature folder (drawers, atlas builders, pass setup);
  shader sources under the location plan 20 established; no chart or tempo-map types leak into
  ui — it consumes `HighwayViewState` + per-frame time only.
- Public-header impact: none public beyond the ui library's composition surface; drawers stay in
  `src/`.
- Testing: renderer stays untested-by-unit (thin), consistent with how the editor treats JUCE
  paint code; the headless/noop-renderer CI path from plan 20 Phase 0b must keep linking and
  running; atlas-layout and visible-range helpers that are pure get unit tests in `<core-lib>`
  or game/ui tests where headless.
- Exit criteria: a fixture chart scrolls correctly against a fake clock and a real playing
  session (plan 21) when available; lefty flag mirrors the picture; no per-frame heap growth for
  static content (spot-check via plan 20's diagnostics).
- Verification:

  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Configure -Targets all
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets clang-tidy
  ```

### Phase 4 — techniques (all data already in the chart format)

- Scope: bend-curved rails with per-bend-point interpolation, outer-string bend inversion, and
  prebend offset at the head; vibrato/tremolo rails with onset-phased, end-tapered modulation
  (defect 2 fix; Charter's shape language otherwise) sampled adaptively in screen space
  (defect 1 fix); slide rails with Charter's easing curves, unpitched slides dimming toward the
  end; technique icon quads (hammer/pull/tap, palm/full mute, pop/slap, harmonic head variants,
  accents); chord boxes with template names and fingering panels (per open question 2); arpeggio
  spans rendered as Charter renders arpeggio handshapes, derived not stored; anticipation rings;
  fret hit-flash.
- Files: rock-hero-game/ui highway drawers extended; tail-sampling math that is pure
  (screen-space step computation, taper envelopes) lands beside the camera in `<core-lib>` with
  unit tests.
- Public-header impact: none.
- Testing: unit tests for adaptive tail sampling (vertex count bounded by cap; endpoints on the
  string line under taper; bend interpolation hits bend points exactly) in `<core-lib>/tests`.
- Exit criteria: every technique in chart.h renders; a corpus-shaped worst case (long vibrato +
  tremolo sustains) stays within the vertex budget defect 1's fix targets.
- Verification:

  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
  ```

### Phase 5 — gameplay feedback, HUD, and feel polish (assumes plan 24 event feed)

- Scope: consume plan 24's event stream (hit / miss / early / late, provisional-hit lifecycle,
  timestamps in song time): status-keyed note atlas states, hit particle bursts (one burst per
  string; chord fallback per the reference analysis; per-event seeded randomness per defect 5),
  fret hit-flash triggering, early/late indication at the hit line; parallax background;
  section-label overlay; optional camera shake behind a default-off setting (open question 4);
  debug overlay v2 — hit-window visualization at the hit line and detection-confidence trace
  fed by plans 24/22, per plan 20's diagnostics layer; HUD layer reserves the slot for plan 24's
  meter/star-power visual direction once that open question closes.
- Files: rock-hero-game/ui highway + hud folders; the event-feed port shape is owned by plan 24
  (game/core) — this phase only subscribes.
- Public-header impact: none here; the event port's header ownership is plan 24's.
- Testing: feedback state reduction (event log → per-note visual state) is pure and lands in
  `<core-lib>` with deterministic replay tests over plan 23-style event logs; particles and
  shake stay untested-by-unit.
- Exit criteria: an autoplay-bot event log (plan 23) drives visually correct feedback end to
  end with no wall-clock dependence (pause freezes everything; seek replays cleanly).
- Verification:

  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
  ```

## 10. Final acceptance phase

Run the sanctioned bundle from the repo root, as separate invocations, after the last
implementation phase:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets clang-tidy
pre-commit run --all-files
```

Acceptance additionally requires: the verticality and mirror invariants green; the plan 20
headless/noop-renderer CI path still green; a local (never CI) corpus soak — load corpus
packages and scroll each arrangement's highway — reported informally per the corpus rules in
docs/roadmap/23-detection-verification-harness.md.

## 11. Rollback/abort notes

- Phases 1–2 are additive headless code with tests; rollback is deleting the `highway/` feature
  folder and its test files — no other code references them until Phase 3.
- If plan 20 Phase 0c later *flips* the scene-model home (game/core ↔ common/core), the move is
  mechanical (namespace + CMake target + include paths) because the code is pure and
  renderer-free by construction; do it as its own commit before any dependent work continues.
- Phase 3 is the risk concentration: if bgfx integration regresses after plan 20's spike
  (driver issues, shaderc pipeline breakage), stop feature work, keep Phases 1–2 (they are
  renderer-agnostic by design), and escalate to plan 20 — its spike owns the render-stack
  decision and its fallback options; do not improvise an alternative renderer inside this plan.
- Defect-fix divergences (adaptive tails, off-axis frustum) each have a Charter-literal fallback:
  if the off-axis frustum fights some later feature, Charter's rotation + pin approach is a
  drop-in replacement inside `highway_camera.cpp` losing only the exactness test; if adaptive
  sampling shows artifacts, densify the cap — never revert to per-millisecond sampling.
- Phase 5 degrades gracefully if plan 24 slips: the event port can be fed by the plan 23
  autoplay bot alone, keeping the phase demonstrable without real detection.
