# Deferred plan — 2D side-scrolling tab view in the game

**Status: deferred.** Captured 2026-07-10 while plan 20 Phase 2 landed. Per
`docs/todo/` rules this document is not kept synchronized with the code; re-verify the
inventory below against the working tree before executing.

## Motivation

The 3D note highway (plan 25) is the game's primary play view, shared with the editor as a
preview (plan 44) through the headless-scene-model seam decided at the plan 20 gate. Players
coming from tab-reading backgrounds will ask for a 2D side-scrolling tablature option; the
editor already renders exactly that presentation (Charter-styled tab lane). This plan decides
how the game gets a 2D view without duplicating ~1,000 lines of proven glyph painting.

## Current-state inventory (as of 2026-07-10)

- `rock-hero-editor/ui/src/tab/tab_view.{h,cpp}` — `TabView final : juce::Component`,
  ~1,050 lines of paint code (string lines, layered note heads, technique glyphs, slides,
  bends, vibrato, chord pills, hand-shape spans). Consumes
  `rock_hero::editor::core::TabViewState` and `common/core/timeline`.
- The game window is SDL3-owned with a bgfx D3D11 device (plan 20 Phases 1–2); JUCE runs
  message-pump-only inside the game shell — there is no JUCE component peer to host a
  `juce::Component` in.
- `rock-hero-common/ui` already exists as the "shared UI only when both products need it"
  bucket (plan 45 put the shared string palette there).
- JUCE headless pixel tests already prove offscreen software rendering works
  (`juce::SoftwareImageType`).

## Options considered

### Option A — extract the paint core, render offscreen, scroll as bgfx textures (recommended)

1. Split `TabView` into a pure paint core and an interactive shell:
   - Paint core moves to `rock-hero-common/ui`: a function/class that draws a given chart
     time window into a `juce::Graphics` from a headless view-state (chart data + theme +
     layout metrics), with no mouse/keyboard/editor-controller coupling.
   - The editor `TabView` becomes the interactive shell (selection, marquee, Alt-insert
     quasimode, playhead) layered over the shared paint core.
2. The game renders the paint core into `juce::Image` (SoftwareImageType) **tiles**, uploads
   them as bgfx textures, and side-scrolls by drawing quads. Tab content is static per song,
   so tiles are painted once (or lazily just ahead of the playhead) — per-frame cost is quad
   submission, not software rasterization. Playhead/hit feedback draws as a thin native bgfx
   overlay on top.

Pros: reuses the proven Charter-styled rendering pixel-for-pixel; no duplicated glyph logic;
tile cache makes runtime cost trivial; interactivity stays editor-only by construction.
Cons: requires the TabView split refactor (real but mechanical); introduces a
JUCE-image-to-bgfx-texture upload seam (small, isolated in game/ui).

### Option B — 2D presentation of the shared highway scene model

Render a side-scrolling view natively in bgfx from the plan 25 headless scene model with an
orthographic camera. Rejected as the primary path: the highway scene model speaks
lane/highway semantics, not tab notation (staff lines, fret-number glyphs, bend curves,
slide lines). A tab view derived from it would re-invent the notation layer that TabView
already implements.

### Option C — reimplement tab drawing directly in SDL3/bgfx

Full duplication of the notation renderer (plus a text/glyph stack bgfx does not provide).
Rejected: maximal duplicated effort, two renderers to keep visually identical forever.

## Open questions for the user

- Approve Option A's prerequisite refactor scope (TabView paint-core extraction into
  `common/ui`) when this plan is picked up — it touches the editor's most visually sensitive
  component, so it wants pixel-test coverage before/after.
- Whether the game's 2D mode is a per-song toggle alongside the 3D highway or a global
  display setting (interacts with plan 26/27 settings surfaces).

## Sequencing

Earliest sensible start: after plan 25 Phase 3 (playable 3D skeleton) proves the game-side
render loop, and ideally after note editing (plan 40) stabilizes TabView's interactive
surface so the paint/interaction split lands once, not twice.
