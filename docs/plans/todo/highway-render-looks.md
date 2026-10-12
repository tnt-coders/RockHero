# Highway Render Looks — a theme-chosen whole-view drawing style

*Opened 2026-10-12 at the user's request ("make the retro arcade style draw so it looks much lower
resolution? Or make a theme that draws sort of cartoony?"). Deferred: it follows plan 54's release
gate (Phases 1-5) and is not part of it. Re-verify every code claim below against the current
renderer before executing; line-level facts here were checked only at the time of writing.*

## The idea

Plan 54 lets a theme choose COLORS (the role table) and ART (notes.png, inlays.png, chords.png).
This plan lets a theme also choose a LOOK: a whole-view post-process that changes how the finished
frame is presented — chunky low-resolution pixels for an arcade theme, inked outlines and flat
fills for a cartoon theme — without changing anything the frame says.

## The boundary (the part that makes this practical)

A look changes the **finished image**, never **what is drawn or where**.

- Geometry, layout, notation, timing and the camera are identical under every look. A theme cannot
  reshape a head, move a lane, or draw a technique differently. That keeps the one renderer, the
  one test surface, and the rule that no surface shows notation another cannot.
- Looks are a **closed, built-in set** chosen by name, with a few validated numeric parameters.
  Themes never ship shaders: bgfx shaders are compiled per backend by `shaderc` at build time, so a
  user-supplied shader would need a runtime compiler, and a closed set keeps a downloaded theme
  inert.
- The 2D editor lane is untouched. It is an editing surface drawn by JUCE; a look would only make
  it harder to edit on. The editor's 3D preview uses the shared renderer and so shows the look,
  which is what sighting a theme needs.

## How it works

Today the highway encodes its views (`g_background_view`, `g_board_view`, `g_overlay_view` in
`highway_renderer.cpp`) straight to the window's backbuffer. A look needs one structural change:

1. **Render the scene into an off-screen frame buffer** (`bgfx::createFrameBuffer`) instead of the
   backbuffer, sized from the window and the look (full size, or 1/N for the pixel look), and
   rebuilt on resize. MSAA moves to that target (or is dropped for the pixel look, where it would
   only soften what the look wants hard).
2. **Present it with one full-screen pass** whose fragment shader is the look's. The default look
   is a plain copy, so a theme with no look draws exactly what it draws today.

Cost on the per-frame deadline path: one extra full-screen pass, which is negligible; the pixel
look also renders the scene at a fraction of the pixels, so it is cheaper than today. Measure on
`relwithdebinfo` before claiming either.

## The candidate looks

### `pixel` — the first one to build

Render the scene at 1/`scale` resolution with point sampling, then upscale with hard edges.
Optional parameters: a palette quantization (`levels` per channel) and scanlines (`scanline`
strength). This is the cheapest look and the most convincing test of the whole mechanism, so it is
the prototype, sighted on the Retro Arcade theme.

### `toon` — only if `pixel` earns the mechanism

Inked outlines from an edge-detect pass (on color/luminance, or on depth if the target carries a
depth texture), optionally posterized fills. Most of a cartoon look is ART, not post-process: the
board has no shaded 3D models for cel shading to act on — it is flat quads, atlas sprites and
emulated light (see `lighting-model.md`). So `toon` would pair with flat-art theme sheets and a
small set of STYLE knobs (glow intensity, gradient flattening), not a new draw path.

## The open problems (each is a sighting or a design call, not a blocker)

1. **Text legibility under `pixel`.** At 1/4 resolution a fret number becomes a few pixels tall.
   Options, in order of cost: (a) cap `scale` (2-3) and sight it; (b) draw labels after the
   present pass at full resolution — crisp numbers over a pixelated board — which needs the glyph
   draws separated into a post-present stage (verify which glyphs ride `g_overlay_view` today and
   which sit in the board's depth-tested pass); (c) let the theme supply a pixel-font glyph atlas
   drawn at full resolution, snapped to the look's virtual pixel grid.
2. **Guardrails must judge the presented colors.** Palette quantization can merge two role colors
   the loader approved (chord vs arpeggio, the hand pales). The plan 54 guardrails should run on
   each role color AFTER the look's color transform, so a look cannot defeat them.
3. **Stepped motion.** At low resolution, distant notes advance in visible pixel jumps (near the
   strike line they already move several pixels a frame). Part of the aesthetic, but sight it at
   speed for readability.
4. **Accessibility and preference.** A look is a theme's choice, but a player may want a theme's
   colors without its look. Decide whether the look joins plan 54's pinnable theme choices (pin
   "no look") or stays bound to the theme.
5. **Format.** A probable shape, to settle when executing:
   `"look": { "style": "pixel", "scale": 3, "levels": 0, "scanline": 0.25 }` — strict loading,
   unknown style or out-of-range parameter refused, absent block = the plain copy.

## Phases

1. **Off-screen scene + plain present.** Frame buffer lifecycle (create, resize, device reset,
   both the game window and the editor preview), the copy pass, MSAA moved onto the target. Zero
   visible change; pinned by a test that the plain look presents the scene unchanged.
2. **The `pixel` look** behind a theme field, prototyped on Retro Arcade, plus the guardrail change
   (2) for its quantization. **USER SIGHTING**, including text legibility (1) and motion (3).
3. **Decide** from that sighting: keep looks at all, how labels survive them, and whether a look is
   pinnable (4).
4. **`toon`**, only if Phase 3 keeps the mechanism and a cartoon theme is wanted: the edge pass
   plus the style knobs, sighted with a flat-art theme.
