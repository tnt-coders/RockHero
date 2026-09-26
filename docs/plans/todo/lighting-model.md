# The lighting model: where the board uses real light, where it fakes it, where it should not

*Opened 2026-09-25 at the user's request, after the hand-shape rails went from painted stripes to
a per-fragment light in one sighting session. A discussion plan: its first deliverable is the
inventory below completed and verified, then a ruling per row. Nothing here is decided.*

## The question

bgfx has no lighting system of its own — it draws triangles with the shaders it is given, and any
light is what a shader computes. The 3D board has **no scene lighting** in the usual sense: no
light sources illuminating surfaces with normals. What it has is a set of effects that EMULATE
light, at three levels of honesty:

1. **Per-fragment light** — a shader computes an emitter's field per pixel (distance to the
   emitter, a shaped falloff, radiance that can exceed one and clip toward white) and ADDS it to
   what is behind. This is the closest the board comes to light, and it looks like light.
2. **Per-fragment masks** — a shader computes a soft edge per pixel, but the result is BLENDED
   over the board like paint: smooth, but it reads as a tinted surface, not as something emitting.
3. **Vertex-colour paint** — geometry with alphas stepped across its cross-section, blended over
   the board. Cheap, and it reads as paint, with hard slope breaks wherever a ramp meets a flat.

The user wants to know which marks sit at which level, which ones are light in intent but paint in
implementation, and where the project should move to a real, shared lighting model.

## Starting inventory (unverified — the first task is to complete and check it)

Taken from the renderer's program usage on 2026-09-25 (`highway_renderer.cpp`, the shaders in
`rock-hero-common/ui/shaders/`):

| Mark | Program | Blend | Level |
|---|---|---|---|
| Accent glow behind heads, open bars, chord-box frames | `fs_accent_glow` | additive, premultiplied | 1 — real per-fragment light |
| Hand-shape rails (both hands) | `fs_accent_glow` | additive, premultiplied | 1 — since `d635a6f3` |
| Strike pops (both hands) | `fs_window_light` | additive (`g_additive_state`) | 2→1 hybrid: a soft mask, added |
| The floor light (both hands' windows) | `fs_window_light` | alpha-blended | 2 — a soft-edged tint, not added light |
| Lane-border ribbons (the runway strips) | `fs_color_fade` | alpha-blended | 3 — paint |
| Fret-line active tier | `fs_color` | alpha-blended | 3 — paint |
| Sustain tails' slope shading | CPU brightness per station | alpha-blended | a FAKE directional light: brightness from the tail's slope "like a surface tilting under a fixed light" |
| Beat bars, shape brackets, chord-box panels, heads | `fs_color`, `fs_texture*` | blended / premultiplied | not light by intent |

To verify for each row: the program, the blend state, whether the mark is meant to READ as light,
and whether its brightness composes with the other lights (additive) or occludes them (blended).

## Questions to answer

- **Which marks are light in intent?** The floor light and the ribbons are called "light" in the
  code and the docs but are drawn as blended tints. Should they add, like the accent glow and the
  rails now do — and does an additive floor light still let the lanes' own tints show through?
- **One light model or several?** The accent glow, the rails and the strike pops each carry their
  own reach / exponent / gain; is there one falloff the board's lights share, with per-emitter
  dials, the way the accent glow already shares one falloff across heads and boxes?
- **Do lights illuminate anything?** Today a light is an emitter drawn on the floor; nothing is
  LIT by it (a string, a head, the fret wires). A real model would have emitters that brighten
  the surfaces around them. Worth it on this board, or a cost the look does not need?
- **The tail slope shading** is a hand-rolled directional light. Should it become a real one, and
  would one directional light over the board then shade every raised mark consistently?
- **Budget.** The per-frame render path has a hard budget (`docs/design/architecture.md`). A real
  lighting pass — especially one where emitters light surfaces — must be measured through
  `.agents/rockhero-build.ps1 -Preset relwithdebinfo`, never argued.
- **The game and the editor preview share one renderer**, so any model lands in both.

## Deliverable

The completed inventory, a recommendation per row (keep / move to the shared light / move to a real
lit surface), a sketch of the one light model if the recommendation is to share one, and a measured
cost. Then the user rules, and the chosen rows become roadmap work.
