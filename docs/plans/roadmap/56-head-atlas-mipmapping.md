# Plan 56 — Head-Atlas Mipmapping and Distance Legibility

**Status**: **Landed 2026-10-12** (uncommitted at the time of writing; see "What landed" below for
the shape, the measurements and what remains to sight). Written 2026-08-06 while chasing "the pick
slide icon is not visible enough at distance" and deliberately scoped OUT of that texture round,
which touched only cell 9; promoted from `docs/plans/todo/` to the roadmap the same day on the
user's decision that mipmapping is wanted. The one **sequencing constraint** stands as a sighting
question rather than a blocker: the measured ratio result below says a far mark reads relatively
weaker against its own note once both are filtered correctly, so judge the marks' distance
legibility on the mipmapped build, not the old one.

## What landed

- **Padded cells, not the bounded chain.** `HighwayAtlasLayout` gained a `gutter`, and both GPU
  atlases lay every cell in a slot of `cell_size + 2 * g_atlas_gutter_texels` (16): the composed
  head atlas is spread into its gutters by `padAtlasCells` (the unpadded `composeHeadAtlas` result
  stays the authority `measureHeadArtProfile` and the 2D tab paint read), and the glyph atlas is
  rasterized straight into its gutters at `cellOrigin`. The texture-array option was rejected as a
  larger change for the same result.
- **One mip rule, generated in memory at upload** (`atlasMipChain`, no asset change): B and A
  average plainly, R and G weighted by B. That is exactly what the straight-alpha blend of the
  level-0 texels sums to, so it is correct for ANY R/G content at the edges — the "edge
  convention" this plan once called a prerequisite is not one, and no contract check was added
  for it. The glyph atlas (white premultiplied text, read by alpha alone) has B equal to A, so the
  same rule serves it; there is no second filter.
- **The far levels are exact by construction.** `g_atlas_exact_mip_levels` (4) names the deepest
  level trilinear reads at the far end (8.6:1 sits between levels 3 and 4). Through it the gutter
  keeps cells apart (`2g >= 1.5 * 2^L - 0.5`), and every atlas edge is a multiple of `2^L`
  (static_asserts beside each layout), so each level is the one above halved exactly — a
  non-power-of-two edge that halves with a remainder shifts a normalized coordinate by up to
  three quarters of a level-4 texel at the texture's far edge, which was the first draft's bug.
  The glyph cell moved from 51 to 56 texels for that rule (10 columns, 880-texel edge); the head
  atlas's 4x5 grid pads to 384x480.
- **The half-texel inset is retired.** `cellRect` is the art's exact rectangle; the gutter is
  what keeps a neighbour out, at level 0 as at every other. `headArtTexelWidth`/`Height` divide
  by `cell_size` rather than `cell_size - 1`, so the head art draws 64/63 of its former size
  (1.6%) inside an unchanged quad; `measureHeadArtProfile`'s quad centre is unchanged.
- **Memory:** head atlas 384x480x4 with nine levels = 983 KB (was 327 KB, no mips); glyph atlas
  880x880x4 with ten levels = 4.1 MB (was 1.0 MB).
- **Measured through the real chain** (headless, a bilinear sampler over the chain, 16 sub-texel
  phases, mass = shown `(R + G) * B` summed over the cell's pixels, white tint):

  | cell | minification | shipped bilinear CoV | trilinear CoV | mass change |
  |---|---|---|---|---|
  | standard head (0) | 8.6 | 22.7% | 2.3% | +2% |
  | pick slide (19) | 8.6 | 18.6% | 2.9% | +14% |
  | natural harmonic (14) | 8.6 | 34.5% | 5.4% | +12% |
  | standard head (0) | 4.6 | 6.9% | 1.3% | +4% |
  | pick slide (19) | 4.6 | 5.1% | 1.8% | +14% |
  | standard head (0) | 2.6 | 1.1% | 1.0% | +4% |

  The absolute figures differ from the 2026-08-06 table (a different raster and metric), the
  direction and order are the same: the far-scale flicker drops six to ten times, and the marks
  gain more mass than the head does here, so the ratio concern reads less severe on this metric
  than the original measurement suggested — a sighting settles it.
- **To sight:** distant heads and fret numbers should hold steady as they travel; at the hit line
  heads are 1.6% larger inside their quads and glyphs re-rasterized at 56 texels, both expected to
  be invisible; marks' legibility against their notes at the far end.

## The problem

Every GPU-sampled highway atlas is uploaded without mipmaps and without any mip/min filter flags:

```cpp
bgfx::createTexture2D(width, height, false /* _hasMips */, 1, bgfx::TextureFormat::BGRA8,
                      BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, memory);
```

`rock-hero-common/ui/src/highway/highway_atlas.cpp:43-50`. With no mip chain, a minified cell is
sampled bilinearly — at most **four texels** — no matter how many texels the pixel actually covers.
Three consequences, all worst at distance and all affecting **all sixteen cells**, not one:

1. **Thin features drop out.** The family's marks are line-dominated: a 2.25-texel rim, a fracture
   seam, tooth-scale detail. Once a cell minifies past roughly 2:1 those features fall below a
   pixel and are either missed entirely by the four samples or reproduced at random strength.
2. **It shimmers.** Because the four sampled texels shift with sub-pixel camera motion, the missing
   detail is not merely absent, it flickers as the note travels — a moving artifact the eye is far
   more sensitive to than a static loss of contrast.
3. **Marks can read DARKER than the note they annotate.** The picking-hand family signature is a
   dark interior. At distance the thin bright rim that redeems it is gone, so what survives is the
   dark core — a mark that subtracts light from the head instead of adding it.

Measured instance: the pick-scrape cell's own bright-pixel count barely exceeded an ordinary note's
before its rim gained white lift, and its widest bright run was 22 px against the note's 52 px.
That was at hit-line scale, where the cell is *magnified*. The distance case is strictly worse.

## Why this is the binding constraint, not a texture-authoring problem

Authoring can raise a mark's contrast, enlarge it, or lower its spatial frequency, and all of that
helps. What authoring cannot do is make a 2-texel feature survive being sampled by four texels out
of twenty-five. Any cell whose identity depends on line work has a distance floor set by the
sampler, not by the art. Mipmapping raises that floor for every cell at once, and it is the only
change here that does.

## What the fix is

Generate the mip chain on the CPU and upload all levels. Two facts verified against the bgfx
headers in the Conan package (`bgfx/bgfx.h:2804-2816`, `bgfx/defines.h:358-370`):

- **Trilinear comes free.** "Default texture sampling mode is linear" — with a mip chain present,
  the existing flags already give linear min and linear mip filtering. No flag change is needed;
  `BGFX_SAMPLER_MIN_POINT` / `MIP_POINT` are opt-*in* to point sampling.
- **Immutable upload carries the whole chain.** A non-NULL `_mem` makes the texture immutable and
  "expected memory layout is texture and all mips together", so `uploadAtlas` allocates one block
  and writes every level consecutively.

### The subtlety that makes a naive implementation wrong

The atlas is channel-packed with **straight, non-premultiplied** weights: R is tint weight, G is
white lift, B is coverage (`shaders/fs_texture_tint.sc:11-13`). Averaging R and G independently of
B is incorrect — it bleeds weight out of uncovered texels into covered ones, the exact error that
premultiplied alpha exists to prevent for ordinary images. Each mip level must average
`(R·B, G·B, B)` and then divide the result by the averaged B.

**This is NOT the cell edge convention.** The shipped cells carry weights multiplied by coverage
at their antialiased edges — the pristine atlas has only 80 texels where `max(R, G) > B`, while a
straight-weight bake of one cell produced 111 — but that convention is about how an edge LOOKS,
not about what averages correctly: the coverage-weighted rule reproduces the straight-alpha
blend's area sum for any R/G content, so it is right whichever convention the art follows and no
contract has to state one for its sake.

### The atlas problem that "just turn on mips" does not solve

Mipmapping a **cell grid** bleeds neighbouring cells into each other at coarse levels.
`HighwayAtlasLayout::cellRect` insets by half a texel (`highway_atlas.cpp:85-90`), which protects
level 0 bilinear sampling and nothing below it. The measured margins are tight — pop leaves only
**2 texels** — so bleeding begins within about two levels for the widest marks. Three ways out,
in ascending order of correctness and cost:

1. **Bounded chain.** Generate only the levels where every cell still clears its own margin
   (roughly two for this atlas), and accept bilinear behavior beyond that. Smallest change; keeps
   most of the benefit, since the first two levels cover the scales where features first vanish.
2. **Padded cells.** Re-lay the atlas with a gutter wide enough for the intended depth, replicating
   edge texels into it. Costs an atlas re-lay and invalidates every cell's coordinates.
3. **Texture array, one layer per cell.** bgfx supports 2D arrays (`BGFX_CAPS_TEXTURE_2D_ARRAY`),
   and each layer mips independently with no cross-cell bleed at any depth. This also retires the
   half-texel inset and the grid arithmetic entirely. The cleanest end state, and the largest
   change: `HighwayAtlasLayout`, `cellRect`, every shader's sampling, and the authoring pipeline
   all move from a grid to indexed layers.

Option 2 landed (see "What landed"): the gutter is one constant, the re-lay is one function, and
with the half-texel inset retired the grid arithmetic is simpler than before rather than more
complex, which removes option 3's main argument.

## Measured, 2026-08-06: what mips actually buy, including one result that cuts against them

The shipped cell-9 bytes were run through a correct coverage-weighted area filter — what a proper
mip chain plus trilinear converges to — and compared against the shipped four-tap bilinear:

| depth | texels per pixel | metric | bilinear (shipped) | area filter | change |
|---|---|---|---|---|---|
| far (z=32) | 8.6 per axis | value mass | 5.88 | 7.16 | **+21.8%** |
| far | | shimmer, mass CoV | 4.34% | **0.08%** | **54x better** |
| far | | shimmer, RMS dL* | 4.08 | 3.10 | −24% |
| mid (z=16) | 4.6 | shimmer CoV | 2.20% | 0.27% | 8x better |
| near (z=8) | 2.6 | shimmer CoV | 0.73% | 0.12% | 6x better |

So the case is **decisive on stability** — the flicker essentially disappears — and mips recover
about a fifth of a mark's brightness for free, because the four-tap sample systematically
under-counts thin bright features.

**The result that cuts the other way, and the reason this must not land in isolation:** at the far
edge an *ordinary note* gains **+72.7%** from a correct filter, because its thin bright rim is
currently missed on most sub-texel phases, while the pick-scrape mark gains only +21.8%. The
mark-to-note value ratio at the far edge therefore **falls from 1.83x to 1.29x** — correct
filtering makes an annotated note *relatively less* conspicuous than it is today. At mid distances
the ratio holds or improves (1.15x to 1.27x at z=16), so this is specific to the far edge, but it
means mipmapping and any distance-legibility work on the marks should be judged **together**. Ship
mips alone and every mark looks weaker against its own note at the horizon than it did before.

Mips also **do not recover form**: far-scale value spread rises only 14%, the mark is still under
4 screen pixels wide, and the ranking among authoring variants is unchanged by the filter. No
filter restores a taper that is a third of a pixel wide.

## Verification this needs

- Far-scale before/after for a representative set of cells: bright-pixel count, mean luminance and
  widest bright run against an ordinary note, at the far end of the visible window.
- The shimmer metric: sample the same feature at several sub-texel offsets at far scale and compare
  the variance before and after. A drop here is the clearest evidence the change worked.
- Memory: a full chain costs about a third more texture memory; a bounded chain far less. Report
  the actual figure.
- Confirm no visible change at hit-line scale, where cells are magnified and mips are not consulted.

## Relationship to other plans

`docs/plans/roadmap/54-highway-visual-theming.md` Phase 3 turns the implicit atlas contracts into
validated ones. A themed atlas supplied by a user gets the same mip treatment automatically, since
the chain is generated from whatever `composeHeadAtlas` yields; the contract owes nothing to this
plan (the edge convention is not a mip prerequisite, see above).
