# Plan 56 — Head-Atlas Mipmapping and Distance Legibility

**Status**: Ready, ungated. Not started. Written 2026-08-06 while chasing "the pick slide icon is
not visible enough at distance" and deliberately scoped OUT of that texture round, which touched
only cell 9; promoted from `docs/plans/todo/` to the roadmap the same day on the user's decision
that mipmapping is wanted. There is no decision gate, but there is one **sequencing constraint**:
land this alongside mark-authoring work rather than before it — see the measured ratio result below,
which is the one finding that cuts against shipping it in isolation.

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

**This is the same decision as the cell edge convention.** The shipped cells already carry weights
multiplied by coverage at their antialiased edges — the pristine atlas has only 80 texels where
`max(R, G) > B`, while a straight-weight bake of one cell produced 111. So the family's existing
convention is already the mip-correct one, and choosing it deliberately (rather than per-bake)
is a prerequisite here rather than a separate cleanup.

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

Recommendation if this proceeds: **option 1 first** — it is contained in `uploadAtlas`, measurable,
and reversible — with option 3 recorded as the eventual shape rather than attempted at the same
time.

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
validated ones. The edge convention decided here belongs in that contract, and a themed atlas
supplied by a user would need the same mip treatment — so if both are executing, this informs the
contract rather than the other way round.
