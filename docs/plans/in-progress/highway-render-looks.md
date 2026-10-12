# Highway Render Looks — a theme-chosen whole-view drawing style

*Opened 2026-10-12 at the user's request ("make the retro arcade style draw so it looks much lower
resolution? Or make a theme that draws sort of cartoony?"). It follows plan 54's release gate
(Phases 1-5) and is not part of it. Phases 1-2 were taken up the same day so the user can sight
the pixel look on Retro Arcade; their settled shape is under "Decisions" below, and the sections
before it are the original sketch.*

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
5. **Format.** Settled for `scale` and `scanlines` (see "Decisions"); `levels` arrives with
   quantization, if it does.

## Decisions (2026-10-12, Phases 1-2)

- **One path, always off-screen.** Every theme renders its scene into the renderer's own target
  (`HighwaySceneTarget`, `common/ui` `highway/highway_scene_target.h`) and one present pass
  (`vs_present`/`fs_present`) copies it to the window; there is no direct-to-backbuffer path for
  the plain look. The target is refitted lazily in `draw` only when its size changes,
  so creation, a window resize and a change of look are one code path.
- **MSAA moved out of `RenderDevice`.** `RenderMsaa` and `RenderDeviceConfig::msaa` are deleted;
  the backbuffer never multisamples. The plain look's target is RGBA8 with 4x MSAA and D24S8 depth
  — the backbuffer's own format, sample count and depth — resolved by bgfx when the frame leaves
  the target, and the present fetches texels by index (`texelFetch` at
  `floor(gl_FragCoord / scale)`), so the plain copy is exact and needs no per-backend flip.
- **The pixel look supersamples (revised at the first sighting).** The prototype rendered at
  ceil(size / scale) with no MSAA and showed one sample per virtual pixel; far notes smaller than
  a virtual pixel popped in and out and flickered heavily, which made the pixel and dither looks
  unreadable. Now every look renders the full-resolution multisampled scene (the target rounds
  up to whole blocks), and the present (`present_block.sh`) gives each virtual pixel the MEAN of
  the scale x scale texels it covers, so a small note tints its block by its coverage and moves
  through it smoothly. Cost: scale² fetches per window pixel (64 at the cap), and the scene no
  longer renders cheaper than the plain look. If the cap survives sighting and the cost shows, a
  downsample pass into a block-sized target makes it one fetch per texel.
- **Views.** 0 is `RenderDevice`'s (it clears the window and is never re-targeted); 1 background
  and 2 board render into the target; 3 the present and 4 the overlay draw to the backbuffer, so
  the diagnostics overlay stays at full resolution and aligned with the debug text under any
  look. A device resize unbinds every view's frame buffer, so the views re-bind each frame and a
  host never resizes between `draw` and `submitFrame`.
- **The render look** (C++ "render look", to keep it apart from the editor's existing `setLook`,
  which delivers a theme and palette) is `HighwayRenderLook = std::variant<HighwayPlainLook,
  HighwayPixelLook>` on `LoadedHighwayTheme`; `HighwayRenderer::create` and `setTheme` carry it.
- **Format.** `"look": { "style": "pixel", "scale": 3, "scanlines": 0.25 }`. `style` is required,
  `"plain"` or `"pixel"`; `plain` takes no other key; `pixel` requires `scale`, a whole number
  2-8 (1 would be the plain look with its anti-aliasing silently dropped; 8 is a sanity cap to
  tighten after sighting), and takes `scanlines`, 0 to 1, default 0, which darkens every other
  VIRTUAL row. Unknown keys refuse as `UnknownKey`, a missing `style` or `scale` as `MissingKey`,
  everything else as the new `InvalidLook`. Absent means the base's look, or plain without a
  base; a stated block replaces the base's whole, as `strings` does.
- **No palette quantization yet**, so the guardrail change (open problem 2) waits for it.
  Scanlines need none: they scale every color on alternate rows by one factor and leave the other
  rows at exactly the approved colors, so they cannot map two approved colors to one.
- **Two more styles, `dither` and `halftone` (same day, at the user's request).** Each is its own
  present program (`present_dither`, `present_halftone`) chosen per frame, never a uniform branch
  inside one shader. `{"style": "dither", "scale": 1-8}` (default 1) renders like the pixel look
  and ordered-dithers each virtual pixel's mean (4x4 Bayer) to the nearest color of a palette made only of
  the theme's own colors — every role and string color with alpha dropped, plus black and white
  (`makeHighwayDitherPalette`) — so it needs no guardrail change. `{"style": "halftone",
  "cell": 4-16}` (default 8) renders the full multisampled scene and prints it as a 45-degree dot
  screen over the theme's backdrop, each dot's area following its cell's luminance, with
  near-black ink outlines from a Sobel edge on the full-resolution luminance.
- **Scale range and text.** Text legibility is not solved this round: fret numbers pixelate with
  everything else, and the sighting judges scales 2, 3 and 4.

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
