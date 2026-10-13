# Intricate highway themes — render looks and the wider theme set

*Deferred 2026-10-12. The whole of this was built, sighted and then withdrawn the same day; the
user ruled "keep ONLY the simple color based themes for now" and asked for a plan that points back
at the work so it can be taken up again without re-deriving it. Nothing here is scheduled.*

## Where the work lives

**Commit `4f8e0d7bea047fb63e0e393a0bc56fd684c2bdd1`** ("Archived every sighted highway theme
beside the render looks") is the last commit that holds the full implementation, and it also
holds `docs/archive/highway-themes-2026-10-12/`: the 97 user themes sighted that day (every
`theme.json`, with a README describing the families). The commit after it removes the looks, the
archive and most of the themes. To bring any of this back, read that commit's tree — the code,
the shaders, the tests and the themes — rather than this plan's summary of it.

The render-look commits, in order, for `git show`:

- `8b556422` the plan; `dc01719c` the off-screen scene target, the present pass, the `look`
  block, the pixel, dither and halftone looks; `826ed60e` supersampling; `08c4a317` the block
  pass and the one-period scanline softening; `f35b5ac7` the CRT look; `afb1d59e` its softening;
  `0fe1bd50` the hard palette snap (`theme_colors_only`) with snapped guardrails; `4f8e0d7b` the
  theme archive.
- Not part of the looks and still shipped: `9de3f6db` (atlas mipmaps, which fixed distant
  flicker on every theme) and `1ef9a30d` (the theme's own `lane_line` / `lane_line_lit` pair).

## What was built

A theme could choose a **render look**: a whole-frame post-process that changed how the finished
image was presented without changing what was drawn or where. The scene rendered into an
off-screen multisampled target (`HighwaySceneTarget`), a block pass reduced it to one texel per
virtual pixel where a look needed that, and one full-screen present pass carried it to the
window. The theme file took `"look": {"style": ..., ...}`, strict, inherited whole from a base.

- **`pixel`** (`scale` 2-8, `scanlines` 0-1, `theme_colors_only`): hard-edged virtual pixels,
  each the MEAN of the full-resolution scene block it covered (supersampled, after the first
  point-sampled prototype flickered on far notes), with optional darkened alternate rows.
- **`crt`** (`line_height` 1-8, `scanlines` 0-1, `theme_colors_only`): the full-resolution scene
  under scanline bands, softened vertically over one band period (rows in a band weigh 1, the two
  a band away 1/2), which cured the band-crossing pulse of far notes while keeping columns sharp.
- **`dither`** (`scale` 1-8): an ordered 4x4 Bayer dither to the theme's own stated colors.
- **`halftone`** (`cell` 4-16): a 45-degree dot screen over the backdrop, dot area following
  cell luminance, with Sobel ink outlines from the full-resolution luminance.
- **`theme_colors_only`**: a hard snap of every pixel to the nearest color the theme states, in
  the block pass before scanlines, so a limited-palette board could show nothing the theme did
  not state. The guardrails then judged the highway's pairs SNAPPED (`nearestThemeColor`, the
  shader's rule restated on the CPU), which refused ice itself (its two picking-hand colors land
  12.1 apart once snapped), so a snapping theme had to state a pale color near each twin.
- The readability study: four themes whose strings were searched for perceptual and
  color-vision-deficiency distance (CIEDE2000) and whose spans sit in the hue gaps the strings
  leave — Signal (min string distance 32.5 normal / 10.8 protan-deutan, achromatic furniture,
  aqua chord / hot-pink arpeggio), Ladder (colorblind-first, protan/deutan min 15.1, sage chord /
  blush arpeggio), Slate (a `#202024` grey board with black ink, the riskiest: low-alpha layers
  may wash out on grey), Chalk (full-chroma strings on the quietest board, pale sky / blush
  spans). Slate and Chalk are in the archive; Signal and Ladder shipped until the user's
  second re-cut the same day (with Ember and Phosphor, last in `9106c2ae`), which left Default,
  the four Element themes, Night Drive and Retro Arcade.

## What the sightings taught

- Point-sampled low resolution is unreadable in motion: anything smaller than a virtual pixel
  pops in and out. Every reduced-resolution look must average the full-resolution scene.
- Fixed darkened rows (scanlines, CRT bands) make a moving note pulse as it crosses from a
  bright row to a dark one; only spreading each row's light over one full period cures it, and
  a taller band softens more. Columns need no softening.
- Fret numbers pixelate with everything else; text legibility under a pixel look was never
  solved (the options were a scale cap, drawing labels after the present at full resolution, or
  a theme-supplied pixel-font atlas snapped to the virtual grid).
- For an authentic arcade board the user chose the HARD palette snap over a dithered one:
  gradients become flat bands and edges go jagged, as on the hardware. The derived colors (the
  picking hand's pale twins, each box fill's dark middle) and every blend and anti-aliased edge
  then snap too, which is why the guardrails had to judge snapped colors.
- The verdict (user, 2026-10-12): the retro and realistic-retro work complicated the product for
  what it bought; the simple color themes carry the readability wins on their own. Keep the
  color-only format, ship a handful of distinct readable themes, and revisit looks later.

## When this is taken up

1. Decide first whether a look is a THEME's choice or a PLAYER's pin (plan 54's pinnable theme
   choices): a player may want a theme's colors without its look.
2. Restore from `4f8e0d7b`: `highway_render_look.h`, `highway_scene_target.{h,cpp}`, the
   `vs_/fs_present*.sc` shaders and `present_blocks.sh`, the `Present*` program enumerators, the
   loader's `look` block, `snapsToThemeColors` and the snapped guardrails, and their tests.
   `RenderDevice` loses its backbuffer MSAA again when the scene moves off-screen.
3. Solve text under the pixel look before shipping it (see above).
4. Re-sight on the shipped built-ins; the archived `theme-retro-arcade-*` sweeps (pixel scales,
   scanline strengths, CRT band heights, board-palette snaps) are the starting values.
5. Animated backdrops for the synthwave and arcade themes (plan 54 "Later themes", COULD tier)
   belong here too; they are backdrop ART, not a look, and were never started.
