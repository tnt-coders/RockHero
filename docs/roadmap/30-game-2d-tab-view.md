# Plan 30 — Game 2D Tab View (shared notation paint core)

## 1. Status

Ready — **30-Q1/Q2/Q3 all answered 2026-07-11** (recommendations approved; Q3 amended by the
user to add a simultaneous 2D+3D display mode — see §7/§8). The 30-Q1 design-doc amendment is
LANDED in docs/design/architectural-principles.md ("UI Modules"), so Phases 1–5 are executable
in order; Phase 6 is gated on docs/roadmap/24-scoring-star-power-failure.md's event feed and
docs/roadmap/25-note-highway-3d.md Phase 5's feedback-state reduction. Date: 2026-07-11.
Baseline: `master @ 7ba93b90`. Promoted from docs/todo/game-2d-tab-view.md after a multi-angle
design review (adopted/rejected record in §7); the architecture decision is settled — do not
re-litigate the rejected options without new evidence.

## 2. Goal

The game offers a 2D side-scrolling tablature display as an alternative to — or alongside — the
3D highway (three display modes: 3D, 2D, both composed in one frame): notes scroll toward a
fixed hit line inside the game's SDL3+bgfx window, with per-note hit/miss feedback and transient
effects. Its tab notation looks near-identical to the editor's tab lane — achieved structurally:
**one** JUCE paint core defines the notation (pixel-identical at equal scale by construction,
text included), so restyles and future optional style variants are always a single edit serving
the editor lane, the game view, and the editor preview alike. The editor's 3D preview window
gains a display-mode selector mirroring the game's three modes.

## 3. Non-goals

- No editing capabilities in the game view — it is strictly a viewer, consumed by the game
  exactly like the 3D highway.
- No replacement of the editor timeline's tab lane: `TabView` stays a live JUCE component inside
  the composited timeline (its paint delegates to the shared core; its hosting, compositing, and
  API do not move).
- No navigation, session, or run-integrity changes — the shared-navigation decision
  (docs/roadmap/28-practice-mode.md §7) already makes those display-agnostic; this plan swaps
  presentation only.
- No new notation features (e.g. section-marker rendering) — docs/roadmap/40-chart-editing.md
  owns notation additions; once the core is shared they benefit both products automatically.
- No general GPU vector-graphics platform (NanoVG or similar — rejected, §7). Reopen only as a
  deliberate user-approved platform bet for broader game UI, never for this feature.
- No feedback/scoring semantics: docs/roadmap/24 and docs/roadmap/25 Phase 5 own the event feed
  and the pure feedback-state reduction; Phase 6 here only presents them.

## 4. Constraints

- (a) **Layering**: common never depends on editor or game code; products never depend on each
  other. Anything both products need is extracted to rock-hero-common FIRST, as its own phase
  with tests. Tracktion headers stay isolated to rock-hero-common/audio implementation files.
- (b) **Public-header minimalism**: the game-facing strip renderer keeps a JUCE- and bgfx-free
  public header (the `HighwayRenderer` pimpl pattern). The one deliberate exception is the paint
  core's `juce::Graphics&` signature — allowed only under the 30-Q1 amendment (or replaced by
  the pixel-buffer facade fallback if 30-Q1 is refused).
- (c) **NAMING FIREWALL**: the commercial real-guitar game that inspired this project is never
  named in any file; use "RS"/neutral phrasing. Charter (BSD 3-Clause) may be named.
- (h) **Builds**: all build/test/lint commands go through `.agents/rockhero-build.ps1` (usage in
  `.agents/README.md`) — never raw cmake/ctest/ninja. Intermediate phases run only the checks
  their changes determinately warrant; the final acceptance phase runs the sanctioned bundle as
  separate invocations.
- **Render-stack facts** (plan 20 gate, plan 44 record): the game window is SDL3-owned; JUCE
  runs message-pump-only in the game process (no component peer); bgfx initializes once per
  process; the editor preview owns a bgfx child-HWND surface. A native child window cannot
  alpha-composite under JUCE-painted siblings (Win32 airspace) — the editor timeline lane must
  therefore remain JUCE-painted.
- **Portability**: new files carry explicit standard-library includes (MSVC-passes/GCC-fails
  trap); CI lints with clang-tidy warnings-as-errors on three platforms.

## 5. Current state inventory

Verified against code on 2026-07-11, `master @ 7ba93b90` (session audit plus two independent
review passes; line numbers from that audit):

- **The notation authority is already a paint core wearing a thin JUCE jacket.**
  `rock-hero-editor/ui/src/tab/tab_view.cpp` (1,051 lines): every drawer is a free function in
  an anonymous namespace taking `(juce::Graphics&, metrics, style, …view)` — `drawNoteTail`
  (:274), `fillHeadShape` (:325), `drawSlideLines` (:359), `drawBendLines` (:436),
  `drawMuteIcon` (:523), `drawAttackIcon` (:600), `drawShapeSpan` (:723), `drawChordBoxPill`
  (:762). `paint()` (:896) derives everything per call from `g.getClipBounds()` plus fixed
  slack (:917-925) — i.e. it already renders **any clip window of an infinite strip**, which is
  exactly the contract tile rasterization needs.
- **`TabView` is purely presentational**: `setInterceptsMouseClicks(false, false)` (:855-858);
  no mouse handling exists in the tab lane (confirmed by docs/roadmap/40-chart-editing.md's own
  inventory). The playhead lives in `timeline/timeline_cursor.h`; seeking in
  `cursor_overlay.cpp`.
- **Full primitive inventory** (drives the §7 rejections): rects, rounded rects, ellipses,
  straight-segment polygons/polylines — including a concave 12-vertex mute "X" (:538-558),
  tremolo gem zigzags under a `reduceClipRegion` clip (:257-269), and a per-pixel vibrato sine
  stroked via `PathStrokeType` (:307-319) — one multi-stop radial `juce::ColourGradient`
  (accent glow, :506-518), and proportional text in three sizes including `ノ` (U+30CE) and
  `¼ ½ ¾` on bend chips (:471, :193) rendered through OS font fallback, with chip boxes sized
  from measured text width (:698-704, :1000-1006).
- **Scale coupling**: metrics cap note height at Charter's 25 px (`g_charter_note_height`,
  :37, :155) — right for the editor lane, too small for a fullscreen game view; every size
  (fonts included) derives from note height, so a scale knob is contained.
- **The scene model is already pure and promotable.**
  `rock-hero-editor/core/include/rock_hero/editor/core/tab/tab_view_state.h` is plain structs
  depending only on `common/core/chart`; `makeTabViewState` (tab_projection) is a pure
  `(Arrangement, TempoMap) → state` function — deliberately mirroring the
  `common/core/highway/` projection discipline.
- **Style color authority is already shared**: Charter's palette and derivation multipliers live
  in rock-hero-common/ui `string_colors/string_color_palette.h`, consumed by both `TabView`
  (tab_view.cpp:47-82, :816-820) and the highway renderer. Geometry/metrics are what §9 Phase 2
  shares; colors already are.
- **The JUCE-image→bgfx-texture seam ships in production**: `uploadAtlas(juce::Image)` at
  `rock-hero-common/ui/src/highway/highway_atlas.cpp:30-55`; the highway glyph atlas is
  runtime-rasterized with `juce::Graphics` on `juce::SoftwareImageType` (:189-220);
  `rock-hero-common/ui` links `juce_graphics` and `bgfx` PRIVATE (CMakeLists.txt:25-28).
- **The game process already runs the JUCE GUI runtime**:
  `const juce::ScopedJuceInitialiser_GUI juce_runtime;` at
  `rock-hero-game/ui/src/surface/game_shell.cpp:159` — in-game JUCE software rasterization is
  current production behavior, not new machinery.
- **The existing game text stack is inadequate for notation** (and stays highway-only): the 3D
  view's `pushGlyphText` is fixed-advance ASCII quads (`advance = glyph_height * 0.62`,
  highway_renderer.cpp:431-456; cell index range `'!'..'~'`, highway_atlas.h:65). Proportional,
  fallback-resolved notation text cannot ride it — a decisive input to §7's rejections.
- **Regression harness exists**: `rock-hero-editor/ui/tests/test_tab_view.cpp` paints into
  `SoftwareImageType` images and asserts exact pixel values (:138-169) — the byte-identity gate
  for the Phase 2 extraction.
- **Editor preview hosting**: `rock-hero-editor/ui/src/preview/preview_surface.h:22-38` owns the
  bgfx child-HWND surface and the process device; a 2D/3D toggle is "which renderer draws into
  the same surface."
- **CI has no GPU pixel substrate**: the render-stack tests run the bgfx Noop backend
  (`rock-hero-common/ui/tests/test_render_device.cpp`) — cross-renderer screenshot CI does not
  exist, which is part of why dual-rasterizer options were rejected (§7).

## 6. Dependencies

- docs/roadmap/28-practice-mode.md §7 — the shared-navigation decision this plan consumes:
  intents, capability sets, sessions, and run integrity are display-agnostic; the 2D view swaps
  only presentation.
- docs/roadmap/40-chart-editing.md — coordination, not a gate: its Phase 3 adds pointer input,
  selection, caret, and overlays to the tab lane. 30-Q2 decides ordering; either way plan 40's
  interaction lands as overlays **above** the shared core (selection/caret/ghosts are
  editor-shell concerns and never enter the shared builder's inputs), and the Phase 2 layout
  manifest is its natural hit-testing substrate.
- docs/roadmap/24-scoring-star-power-failure.md (event feed) and docs/roadmap/25-note-highway-3d.md
  Phase 5 (pure feedback-state reduction, event log → per-note visual state) — Phase 6 only.
- docs/roadmap/26-game-startup-menus-library.md Phase 4 / docs/roadmap/27-in-song-flow-results-profiles.md
  Phase 1 — the settings surface where 30-Q3's display-mode choice lives.
- docs/roadmap/44-editor-3d-preview.md — the preview window Phase 5 extends with the toggle.
- docs/design/architectural-principles.md — 30-Q1's amendment must land (user-confirmed) before
  Phase 2's public header does.

## 7. Decisions already made

Adopted 2026-07-11 with the user after a deliberate multi-angle review (an independent
architecture draft, an adversarial critique of the then-preferred design, and a direct code
audit — all three converged). Recorded in full so the decision never re-litigates without new
evidence.

**Requirements the decision serves**: (1) near-identical notation between the editor lane and
the game view — same style, different framing; (2) exactly ONE authoritative definition of the
notation look — restyles and optional style variants must never require editing two renderers
(user hard requirement); (3) full headroom for hit/miss animation in the game; (4) the editor
lane stays a live JUCE-painted component in its composited timeline (airspace makes anything
else impossible); (5) the game renders inside its SDL3+bgfx window (no JUCE peer exists there).

**ADOPTED — refined Option A, "one rasterizer, sampled":**

- Shared tab scene model in `common/core` (Phase 1) mirroring the highway projection discipline.
- Shared **JUCE notation paint core** in `common/ui` (Phase 2): the existing drawers moved
  verbatim, parameterized by (state, px/s mapping, clip window, style preset, scale). All
  layout and style decisions — including future optional style variants — live here and only
  here.
- A **layout manifest** (note → head rect, tail span, anchor points in strip space) computed by
  the same metrics that paint — one geometry authority serving game feedback placement now and
  plan 40 hit-testing later.
- Game-side **tile-strip renderer** (Phase 3): tiles of static notation rasterized by the paint
  core onto `SoftwareImageType` images, uploaded via the shipped `uploadAtlas` seam, scrolled as
  textured quads; JUCE- and bgfx-free public header (HighwayRenderer pattern).
- Because one rasterizer produces every notation pixel, parity is **pixel-identical at equal
  scale by construction** — including text with OS font fallback (`ノ`, fractions), which no
  dual-rasterizer option could match. Parity caveat recorded honestly: it is per-machine (both
  products resolve the same platform typeface); if bit-stable typography across installs ever
  matters, bundle a typeface via the style preset — one edit, both products.

**The hit/miss animation ladder** (why sampled tiles never hinder animation; every rung keeps
the single paint-core authority; user's explicit adoption condition):

1. **v1 — overlay quads**: tint/glow/flash over manifest rects (hit brighten, miss red,
   provisional shimmer), progressive sustain highlight as an additive quad over the tail span
   from note start to playhead, particles and hit-line flash native in bgfx.
2. **v2 — status sprites**: the same paint core rasterizes head variants
   (normal/hit/miss/provisional) into a sprite atlas at load; the game draws the variant over
   the baked head — full art-swap feedback, congruent with the 3D highway's status-keyed atlas.
3. **v3 — heads out of tiles**: if the design ever wants notes to scale, pop, vanish, or move,
   bake tiles without heads and draw every head as a sprite per frame (tens of visible notes;
   the 3D view already draws every visible note per frame at ~145 fps). At v3 no animation is
   out of reach, and sprites still come from the one paint core.

Tiles hold only static furniture and are invalidated only by song load, zoom/scale, style
variant, or DPI — **never by gameplay**.

**REJECTED, with the evidence** (each was seriously considered in the review):

1. *Two native renderers sharing style tokens* — tokens encode colors and ratios but not the
   tremolo gem zigzag, layered head construction, bend-height mapping, chip layout, or layer
   order; every restyle edits two renderers. Direct violation of requirement (2).
2. *Renderer-agnostic draw-command layer with JUCE + bgfx executors* — the full primitive audit
   (§5) shows ~14 command kinds including multi-stop radial gradients, stroked concave polygons,
   variable-width stroked paths with joins, and a clip stack: that surface **is** a 2D vector
   API, so the "thin" bgfx executor converges on a second ~2.5k-line vector renderer plus a
   third text stack. Text is the landmine: `ノ`/fraction glyphs arrive via OS font fallback,
   which an atlas baker must reimplement or render tofu; chip boxes need injected text metrics,
   making the "pure" golden tests pin fake layout. And the one-definition claim silently stops
   above the rasterizer — AA, joins, and gradient interpolation stay defined twice — exactly
   where CI has no pixel substrate (Noop backend). Finally it taxes plan 40's churn window:
   every visual iteration touches vocabulary, builder, two executors, goldens, and fixtures.
3. *NanoVG-for-bgfx as the game executor* — outsources tessellation but keeps the two-dialect
   drift and the text-parity problem (stb_truetype ≠ DirectWrite); ships in bgfx's examples
   tree, not the Conan package — adopting means owning example-grade vendored code.
4. *Orthographic 2D presentation of `HighwayViewState`* — the highway model speaks lane
   semantics, not staff notation; its text stack is fixed-advance ASCII (§5). Would re-invent
   the notation layer the paint core already implements.
5. *bgfx child HWND inside the editor timeline* — impossible under requirement (4): native
   child windows do not alpha-composite under JUCE-painted siblings (airspace).

**Also settled here:**

- **Do NOT unify the tab scene model with `HighwayViewState`** during Phase 1: `HighwayNoteView`
  bakes display padding into `note.string` while the tab core pads at draw time via
  `extra_lanes` — merging the semantics is a real refactor across two shipped views with no
  payoff for this feature. Recorded as a watch item (docs/tracking/watch-items.md) at Phase 1
  execution, not done speculatively.
- `uploadAtlas` promotes from `src/highway/` to `common/ui` `src/shared/` when Phase 3 lands —
  it then has two feature consumers, meeting the documented `shared/` admission rule.
- The editor's 2D/3D preview toggle rides the existing `PreviewSurface` device/HWND (one bgfx
  init per process honored) — no second surface, no re-init.
- Navigation, capability sets, sessions, and score-record integrity are consumed unchanged from
  docs/roadmap/28-practice-mode.md §7 / docs/roadmap/24 §6 / docs/roadmap/27 §6.
- **Display modes are a three-way choice: 3D highway / 2D tab / both simultaneously** (user
  amendment to 30-Q3, 2026-07-11). "Both" composes the two renderers into one frame — the 2D
  strip docked as a band with the 3D highway filling the rest — via separate bgfx view IDs and
  viewport rects on the same device, both driven by the same view-state snapshot and time
  authority (one clock, one loop, one feedback state; nothing about navigation or integrity
  changes). Exact band placement/height is a Phase 4 capture-review decision, not a format or
  architecture question. The strip renderer therefore targets a caller-supplied viewport from
  day one rather than assuming it owns the frame.

## 8. Open questions for the user

All three ANSWERED 2026-07-11 (mirrored in docs/roadmap/00-roadmap.md "Decisions needed");
retained here as the record.

1. **30-Q1 — common/ui public-header amendment.** Options were: (a) amend
   docs/design/architectural-principles.md to allow `juce_graphics` types in **designated**
   common/ui public headers — mirroring exactly how common/core earned its narrow `juce_core`
   rights; (b) keep the convention and ship a framework-free pixel-buffer facade.
   **ANSWERED: (a)** — the amendment is landed in docs/design/architectural-principles.md
   ("UI Modules"); Phase 2's paint-core header is its designated exemplar.
2. **30-Q2 — sequencing vs plan 40.** Options were: (a) extract the paint core **before**
   docs/roadmap/40-chart-editing.md Phase 3 adds tab-lane interaction — `TabView` is verified
   interaction-free today, the lowest-churn window; (b) wait until plan 40 stabilizes.
   **ANSWERED: (a).**
3. **30-Q3 — display-mode setting home.** Options were: (a) a global game display setting on
   the plan 26 Phase 4 settings surface; (b) per-song per-profile persistence (plan 27).
   **ANSWERED: (a), amended by the user** — the setting is a three-way mode: 3D highway / 2D
   tab / **both simultaneously** (composition decision recorded in §7). Per-song persistence
   (b) remains additive later if user feedback asks for it.

## 9. Phased implementation

### Phase 1 — Promote the tab scene model to common/core

- **Scope**: move `TabViewState` (+ its member view structs) and `makeTabViewState` from
  `rock-hero-editor/core` `tab/` to a `rock-hero-common/core` `tab/` feature folder, namespace
  `rock_hero::common::core`. Mechanical: the types are plain structs with `common/core/chart`
  dependencies only. Editor re-points (~7 files: controller impl, editor_view_state, projection
  callers, tests) and CMake source lists move. Existing projection tests move with the code.
  Record the do-not-unify-with-`HighwayViewState` watch item (§7) in
  docs/tracking/watch-items.md with its trigger (a third consumer or a padding-semantics bug).
- **Public-header impact**: new common/core headers; editor/core headers deleted.
- **Testing**: moved projection tests green in `rock_hero_common_core_tests`; editor suites
  unaffected.
- **Verification**:

  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
  ```

### Phase 2 — Shared notation paint core + layout manifest (30-Q1 amendment landed; per 30-Q2 runs before plan 40 Phase 3)

- **Scope**: extract tab_view.cpp's anonymous-namespace drawers and `TabLaneMetrics` into a
  shared paint core in `rock-hero-common/ui` `tab/` — **move-verbatim discipline**, types
  renamed to the promoted state. Parameters: state, px-per-second mapping, clip window/bounds,
  style preset (the single home for future optional style variants), and a scale knob that
  defaults to today's editor behavior (including the 25 px cap) so editor output is
  byte-identical. Add the layout manifest (note → head rect, tail span, glyph anchors, in strip
  px) computed from the same metrics — a pure, font-light API unit-tested headlessly.
  `TabView::paint` becomes a single delegation call; its component shell, hosting, and API stay
  put.
- **Public-header impact**: the one `juce::Graphics`-bearing paint-core header (30-Q1) plus the
  manifest header; reviewed against constraint (b).
- **Testing**: the existing exact-pixel suite (test_tab_view.cpp) must pass **byte-identical**
  — it is the extraction gate; new manifest unit tests; paint-core pixel tests move to
  `rock_hero_common_ui_tests` (SoftwareImageType, no GPU).
- **Verification**: same two invocations as Phase 1.

### Phase 3 — Game tab-strip renderer (common/ui)

- **Scope**: `tab_strip_renderer.{h,cpp}` beside the highway renderer: JUCE- and bgfx-free
  public header (pimpl); small LRU tile cache; tiles painted by pointing the paint core at a
  `SoftwareImageType` image with the graphics origin translated to the tile's strip position;
  upload via `uploadAtlas` — promoted to `common/ui` `src/shared/` (two feature consumers, §7);
  per-frame scroll as textured quads with fractional-offset sampling (clamp samplers + the
  half-texel inset discipline proven in `HighwayAtlasLayout::cellRect`); paint budget of one
  tile per frame, painted ahead of the scroll edge. The renderer targets a **caller-supplied
  bgfx view ID and viewport rect** from day one (§7 both-mode decision) — it never assumes it
  owns the frame.
- **Testing**: the permanent determinism guard — a **whole-strip-vs-tiled pixel-diff test**
  (render a fixture passage once monolithically and once tiled; assert identical pixels), which
  also proves the clip-slack constant covers every overhanging element; tile-cache eviction and
  DPI-change unit tests. All headless (software images compared byte-wise; no GPU needed for
  the raster comparison).
- **Public-header impact**: one new common/ui header (bgfx-free).
- **Verification**: same two invocations.

### Phase 4 — Game display modes (3D / 2D / both)

- **Scope**: the three-way display mode in `rock-hero-game/ui` per §7 — 3D highway, 2D tab, or
  **both composed in one frame** (2D strip docked as a band, 3D highway filling the rest;
  separate bgfx view IDs and viewport rects on the same device; band placement and height tuned
  by capture review). All modes sit behind the same time authority (IPlaybackClock/stand-in dev
  clock) and the shared-navigation intents — presentation only, per
  docs/roadmap/28-practice-mode.md §7; in "both", the single clock/loop/feedback state drives
  the two renderers so they can never disagree. Dev-fixture wiring first (a
  `--display highway|tab|both` dev switch alongside `--dev-package`), the real setting arriving
  with 30-Q3's surface (plan 26 Phase 4). Hit line + playhead as native bgfx overlay per view.
  Lefty mirror = lane-order inversion through the shared core's lane mapping, verified by
  capture.
- **Testing**: game smoke run (`--smoke-frames`) in each of the three modes; capture review of
  all three (including "both" band composition).
- **Verification**: same two invocations; captures for user review.

### Phase 5 — Editor preview display selector

- **Scope**: `PreviewSurface` hosts the highway renderer, the tab-strip renderer, or both on the
  same bgfx device and child HWND (one init per process) — a display-mode selector in the
  preview window mirroring the game's three modes, reusing Phase 4's composition; identical
  view-state snapshot and navigation-intent wiring (docs/roadmap/44-editor-3d-preview.md §7).
- **Testing**: mode round-trip live check (open → cycle all three modes → close → reopen),
  settings persistence for the chosen mode via `IEditorSettings`.
- **Verification**: same two invocations.

### Phase 6 — Gameplay feedback overlays (GATED: plan 24 event feed + plan 25 Phase 5 reduction)

- **Scope**: the §7 animation ladder, v1 first: tint/glow quads and the progressive sustain
  highlight over manifest geometry, hit-line flash and particles native in bgfx, all driven by
  the shared pure feedback-state reduction (consumed unchanged from plan 25 Phase 5). Escalate
  to v2 status sprites (same paint core → atlas) only when v1's expressiveness runs out; v3
  (heads out of tiles) stays the recorded escape hatch, adopted only on a real design need.
- **Testing**: replay-driven — the plan 23 autoplay bot's event log drives the feedback with
  zero wall-clock dependence; pause freezes everything, seek replays cleanly (plan 25 Phase 5's
  exit criterion applied to this view).
- **Verification**: same two invocations; capture review.

## 10. Final acceptance phase

The sanctioned bundle as separate invocations — `-Targets all`, then `-RunTouchedTests`, then
`-Targets clang-tidy` (user-triggered), plus `pre-commit run --all-files` — and:

- editor tab-lane pixel suite byte-identical against the pre-extraction baseline;
- whole-strip-vs-tiled diff test green;
- user capture review: all three game display modes at game scale (including the both-mode band
  composition), editor lane unchanged, preview display-selector round-trip.

## 11. Rollback/abort notes

- Phases 1–2 are pure refactors gated by byte-identical pixel tests — rollback is moving files
  back; no behavior ships until Phase 3.
- Phases 3–6 are additive (new renderer and overlays; no existing surface is modified), so
  aborting strands no coupling — the shared core remains a clean win for the editor alone.
- If 30-Q1 is refused, Phase 2 re-plans onto the pixel-buffer facade **before** any extraction
  lands; nothing else changes.
- If tile determinism ever proves brittle (the diff test flakes), the recorded fallback is
  full-strip repaint per frame (low-single-digit-millisecond software raster, measured before
  adopting) — a hosting change only; the paint core and its consumers are untouched.
