# Plan 54 — Highway Visual Theming

**Status**: Revised 2026-10-07 against `master @ 6f87951f`; re-verified and re-sequenced
2026-10-09 against `602d5504`, then re-sequenced again the same day so the default look is
sighted before theme loading exists, and 54-Q18/Q19 recast as theme choices the player can pin.
**Executing:** Phase 1 landed 2026-10-10 (`HighwayTheme` and `defaultHighwayTheme()`; the string
palette is a separate `create` argument); its coverage check moved to Phase 3's fixture. Phase
1b's switch landed 2026-10-11 (ice-blue values, interim heads and inlay, `fs_texture`
premultiplying its tint so `inlay_tint`'s alpha is the inlays' opacity); its sighting is pending.
Phase 2a landed 2026-10-11: the default is `resources/themes/ice/`, loaded with any user themes
through one strict loader. Phase 2b landed 2026-10-11: inheritance from built-ins, and the
editor's theme and string-color pickers over plan 70's shared settings. Phase 4a landed
2026-10-11: the box chord device, open-bar posts as upright legs only, the glow posts unchanged
(ruled), and the cue renamed Note Targets. **Phase 0 has a direction for the default look**
(below) and still gates the art phases (54-Q5 to 54-Q22, each with a recommendation or a ruling).
Phases 1 and 2a need only the role names, which this plan fixes. The engineering is ordered so the
new default look can be sighted in both products as early as possible ("Path to the first in-app
sighting"). **Phases 1 to 5 gate the first public release.** **Sequencing 2026-10-12 (user):**
the mark brief (54-Q8, with the harmonic pair and the IP review's per-mark findings) is
definitively planned here, but it waits until span authoring and the chord-template work (plan
60b, Phases 4 and 5) are done. This revision replaces the 2026-08-02 plan (baseline
`182faedb`): its mechanism survives, but its inventory was two months stale, and its goal changed from "make today's look one selectable
theme" to "make the look data, and ship RockHero's own looks in place of today's". Today's look
survives only as the user's private theme (54-Q10).

## Goal

The 3D highway's look becomes data: a **theme** of 21 color roles plus three validated texture
assets, resolved from theme directories through one loader. The built-in themes are original
RockHero designs. Today's look leaves the shipped tree with them: its chrome colors are Charter's
values (the renderer's comments say so at `highway_renderer.cpp:199`, `:415`, `:1595`), its
`notes.png` and `inlays.png` are adapted from Charter, and its chord box reproduces Charter's
holder model. A user can select a theme in either product, tweak one, or drop in their own.

The technique marks are notation, not styling. Players already read their shape-to-meaning
pairings, so the pairings stay where they read well at distance. Every mark is redrawn from a
RockHero brief and drawn the same in every theme (S3).

What a theme may change is decided once, below, by four tiers. The design rule behind them:
**a theme changes what the notation is painted with, never what the notation says.**

## What a theme captures, at a glance

Every visual knob of the highway sits in exactly one of three owners. The tiers below are the
detailed inventory; this is the summary they resolve to.

| Owner | What it holds |
|---|---|
| **The theme** (`theme.json` plus its assets) | The 21 color roles (backdrop, board chrome, hand window, fret numbers, attack line, the chord and arpeggio colors every box and rail of either hand derives from, both hands' strike pops, full-mute mark, capo, inlay tint and opacity, label text); its eight **string colors** (54-Q18); the head **material** (54-Q19); the repeat-box art (`chords.png`); and its **choices** of head shape and inlay (54-Q19). The player may pin the string colors, head shape and inlay |
| **The player** (settings shared by both products, plan 70) | Which theme is active; one pin each for string palette, head shape and inlay, defaulting to "Theme's choice"; mirroring, string orientation (54-Q5) and plan 69's display toggles |
| **RockHero, fixed** (code and one fixed sheet) | The technique marks (S3); the chord device and open-bar posts (S1, S2); every geometry, camera, timing, tail, light and emphasis law; shaders; the atlas layout and cell meanings; the palette, shape and inlay libraries' contracts |

**Theme's choice, or pinned.** String palette, head shape and inlay each resolve by one rule: the
player's pick if they made one, otherwise the active theme's choice. Each is one setting whose
default entry is "Theme's choice"; picking any concrete entry pins it, and picking "Theme's
choice" unpins it. There is no separate pin flag that could disagree with the value. The resolved
palette is computed once and passed to both the 3D highway and the 2D lane, beside the theme's
roles rather than inside `HighwayTheme`.

**The theme file** (`<themes folder>/<id>/theme.json`, with `notes.png`, `inlays.png` and
`chords.png` beside it by those fixed names):

```json
{
  "name": "Classic",
  "head_shape": "rectangle",
  "inlay": "dot",
  "colors": {
    "backdrop": "#000000",
    "lit_lane": "#2590E840",
    "...": "every other role",
    "strings": ["#D22CF8", "#85E747", "#FF870A", "#25B2FF",
                "#F2D706", "#ED0000", "#00B5A0", "#B6B6B6"]
  }
}
```

- **The folder name is the id**, `[a-z0-9-]+`, and there is no `id` key that could disagree with
  it. A theme whose folder breaks the pattern is rejected.
- **`name`** is the picker's display name, required and never inherited.
- **`inherits`** is optional and names only a built-in (54-Q11): any built-in for a user theme,
  the default theme for a built-in (since 2026-10-12, when the shipped color themes landed
  sharing the default's art). Without it a theme is complete: every role, `strings`,
  `head_shape`, `inlay` and every asset. With it, each one left out comes from the base; roles
  inherit one by one, `strings` and each asset inherit whole.
- **`colors`** holds every color and nothing else, keyed by the `HighwayTheme` member names; one
  role table serves the loader and the later theme editor. Colors are CSS hex: `#RRGGBB` opaque,
  `#RRGGBBAA` with alpha, case-insensitive. An alpha-first form would be indistinguishable from a
  pasted CSS color and load with its channels rotated.
- **`colors.strings`** is exactly `g_max_chart_strings` opaque `#RRGGBB` colors in the player's
  string numbering, from the highest string down: entry 1 is the high e, 6 the low E, 7 the low B,
  8 the low F#, so each slot is the same string at every string count. The standard and extended
  tiers derive from it.
- **`head_shape` and `inlay`** are library ids at the top level, since they are not colors; they
  arrive with Phase 5.
- **No `look` key.** A theme chooses colors and art only; the render looks (a `look` block
  choosing a whole-frame post-process) were built and withdrawn on 2026-10-12, and live in the
  commit `docs/plans/todo/intricate-highway-themes.md` names.
- **Strict:** an unknown key or role, bad hex, or a `strings` array of the wrong length rejects
  the file. The guardrails run on the resolved theme. No version field; formats change in place.

A theme cannot add elements: a backdrop is one flat color, and backdrop art, animation and bloom
are out of v1 (COULD tier).

## Non-goals

- Everything in the SHOULD NOT and COULD tiers below: geometry (`HighwayMetrics`), camera, timing,
  tail and light laws, the emphasis alphas, the atlas layout and what each cell means, shaders,
  fonts, backdrop art, inlay patterns (plan 58), the 2D lane's own chrome.
- **The string-palette registry.** Owned by
  `docs/plans/roadmap/45-editor-theme-and-string-colors.md`. Every theme that writes `strings`
  is a palette in it, and code keeps only the accessibility palettes; a palette the player pinned
  always wins (54-Q18).
- **String orientation.** A player setting, never a theme key (54-Q5).
- **Editor chrome (`EditorTheme`).** Plan 45 Phase 2; independent of the highway theme.
- A marketplace or any network delivery. Packaged export/import moves to `docs/plans/todo/`
  (54-Q12).

## Constraints

Applicable subset of the roadmap's non-negotiable block (`docs/plans/roadmap/00-roadmap.md`):
(a) layering — the theme type, registry, contracts and loader live in `rock-hero-common/ui`; the
player's choices persist in plan 70's shared `highway.*` settings keys, and only the picker is per
product; (b) public-header minimalism; (h) builds through
`.agents/rockhero-build.ps1`.

Plan-specific rules:

- **No JUCE in the theme's public header.** Colors are `common::ui::ArgbColor`; the loader may use
  `juce_core` JSON and `juce::Image` decoding, as `box_mute_profile.cpp` already does.
- **Hex-literal-only color construction** at static or namespace scope (plan 45 decision 2).
- **Built-in themes fail loudly; user themes fall back.** A built-in that does not load is a broken
  install (`highway_renderer.h:46-51, 113-121`); a user file is untrusted input and is dropped
  with a logged, typed error.
- **Formats change in place** — no version field, no migration ladder.
- **The tree's git history is never rewritten.** Today's art and values leave the tree going
  forward only.
- Identifiers use US "color"; JUCE keeps "colour" at its boundary.

## The theming surface, by tier

Every knob the highway's look depends on was enumerated from the code at `6f87951f` and placed in
exactly one tier. Line numbers are `rock-hero-common/ui/src/highway/highway_renderer.cpp` unless
another file is named.

| Tier | Meaning | Contents |
|---|---|---|
| **MUST** | Must change for RockHero to own its look: carries a Charter-derived value, shape or cell | the board chrome colors, the chord-box colors, the arpeggio color, the head cells of `notes.png`, the drawing of every technique mark (S3), the chord-box shape (S1) |
| **SHOULD** | Not required for that, but a theme that could not change it would be incomplete or leave a visible seam | backdrop, strike pops, mute mark, capo, picking-hand furniture, label and inlay tints, `inlays.png`, `chords.png`, the 2D consumers of shared roles |
| **SHOULD NOT** | Structure, timing or notation: changing it could misread notation, misalign notes and lanes, or make a chart unplayable | `HighwayMetrics`, camera, ~51 renderer numerics, core tail/light/vibrato laws, emphasis alphas, atlas layout and cell meaning, the technique marks (S3), shaders, string palette, display options |
| **COULD** | Harmless, but nothing needs it at v1 | glyph font, backdrop art, inlay patterns, tail shade depth, rolling flip as a player option |

### Color roles (MUST and SHOULD) — the theme's 20 keys

A role's name is the theme file's key, so names describe what the color does, not where it is
drawn. "Alpha read" records whether the constant's own alpha byte reaches the GPU; a role whose
alpha is overwritten in code is stored opaque. The "Today" and "Value" columns are the pre-Phase-1
baseline; the names and the set are the model ruled 2026-10-11 (see "Schema guardrails").

| Role | Today | Value | Alpha read | Tier |
|---|---|---|---|---|
| `backdrop` | `g_backdrop_color` :64 (packed RGBA, converted at the `bgfx::setViewClear` call :2764) | 0x000000ff | n/a | SHOULD |
| `beat_bar` | `g_beat_bar_color` :67 | 0xFF0F3B5E | yes | MUST |
| `attack_line` | drawn in `g_chord_box_color` today: downbeat line :5849, fret-span lines :4867, keyframe lines :4904 | (chord box) | — | MUST — split from `chord_box` |
| `lit_lane` | `g_lit_lane_color` :87 | 0x402590E8 | yes (:5785) | MUST |
| `lit_lane_dotted` | `g_lit_lane_dotted_color` :88 | 0x40185C94 | yes | MUST |
| `lane_line` | `g_lane_border_color` :195, forced opaque at :709 and :5638 | 0x0007928F | **no** — store opaque, drop the two `\| 0xFF000000U` | MUST — renamed from `lane_border`; paints the lane ribbons only; since 54-Q20's re-ruling, the ribbons away from every light, with `lane_line_lit` at full light |
| `fret_line` | `g_fret_inactive_color` :196 | 0xFF202020 | yes | MUST |
| `fret_line_lit` | `g_fret_active_color` :197 | 0xFFC0C0C0 | yes | MUST |
| `fret_number_lit` | `g_fret_number_active_color` :202 | 0xFF87DDF6 | yes | MUST |
| `fret_number_target` | `g_fret_number_fhp_color` :204; also node series :3727, targets :3811, pinned numbers :3924 | 0xFFFFA821 | yes | MUST — renamed: it marks every hand-position target |
| `strike_fretting` | `g_hit_glow_color` :227 via `HandLightStyle::mark_color` :706 | 0xFFFFB040 | yes | SHOULD |
| `strike_picking` | literal :713 | 0xFFFFFFFF | yes | SHOULD |
| `chord` | `g_chord_box_color` :417; palm mark :3325 | 0xFF00D2D5 | yes | MUST — renamed from `chord_box`; the fretting hand's chord: box frame, fill ends, accent light, palm mark and rails |
| `arpeggio` | `g_arpeggio_color` :445, used only at :5950 | 0xFFC040FF | yes | MUST — the fretting hand's arpeggio, everywhere `chord` paints a chord, its box included |
| `full_mute_mark` | `g_full_mute_mark_color` :435 | 0xFF52798A | yes | SHOULD |
| `capo_shade` | `g_capo_dead_zone_color` :210 | 0xB4000000 | yes (:6141) | SHOULD |
| `capo_bar` | `g_capo_bar_color` :211 | 0xFFB9BEC6 | yes | SHOULD |
| `capo_bar_rim` | `g_capo_bar_rim_color` :212 | 0xFF14161A | yes | SHOULD |
| `inlay_tint` | literal white vertex color :6074 (`fs_texture.sc:13` multiplies it) | 0xFFFFFFFF | yes | SHOULD |
| `label_text` | section labels, literal white at 0.85 :6240 | 0xFFFFFFFF | yes | SHOULD |

**Derived in code, never roles** — each is an intended coupling, so it is computed rather than
authored twice. The color derivations live in one unit,
`highway_theme/highway_theme_derived_colors.h`, which the renderer and the guardrails both call:

- the picking hand's chord and arpeggio, `pickingHandColor` of each: in CIELAB LCh the hue is
  kept, the chroma drops to a quarter, and the lightness becomes the source's perceived lightness
  (Fairchild-Pirrotta L**, clamped to 100; an out-of-gamut channel clamps). The teal 0xFF00D2D5
  yields 0xFFB1CFCF and the purple 0xFFC040FF yields 0xFFD4B4E0. This replaces the baseline's
  `g_picking_furniture_white` (0xFFC0C7D8 :680), which was derived offline from the teal and had to
  be re-derived by hand whenever the teal moved, and its rail alpha (0xBA :681);
- a box fill's middle, `boxFillMiddle`: the box color times 0.286 per channel, rounded, for both
  hands, which reproduces the baseline's `g_chord_box_dark_color` (0xFF003C3D :418) from its teal
  exactly and replaces the picking box's `hueless` dark (:715);
- the dim fret number, `fretNumberDim`: `lane_line` at half alpha, exactly the baseline's
  `g_fret_number_dim_color` (0x8007928F :203);
- the tap-light lean (0.3 toward `fret_number_target`, :194, :5770-5775), and every tier, frame
  and fill alpha (frame 128/255 :423, fill 32/255 :1591, ribbon tiers :5637-5646, attack 0.85
  :74).

Every rail wears its kind's color as-is, for both hands.

### Texture assets (MUST and SHOULD)

The texture set is already data (`HighwayTextureSet`, `highway_renderer.h:53-54`, an array
indexed by `HighwayTexture` in `common/core/highway/highway_resources.h:80-100`), so these need
no renderer change, only new art and the contracts in Phase 2a.

| Asset | Today | Tier |
|---|---|---|
| `notes.png` head cells 0, 1, 2, 4, 5 (`highway_atlas.h:134-140, 262-271`) | 256x320 RGB, 4x5 grid of 64 px cells; adapted from Charter (`textures/LICENSE.txt`) | MUST — new art (54-Q6); per 54-Q19 the theme supplies a head material that a load-time bake composes with the player's library shape |
| `notes.png` mark cells 8-19 (`highway_atlas.h:153-248`): arpeggio brackets, legato triangle, bend caret, wide palm X, compact mute X, ring harmonic, ellipse pinch, slap, pop, V tap, pick slide | same file | Not themed — each redrawn once and moved to a fixed sheet (S3, 54-Q8, 54-Q17) |
| `inlays.png` | 256x256 RGBA single dot (`053814d9`); adapted from Charter | SHOULD — RockHero's own circle dot retires `LICENSE.txt` (54-Q15); the art becomes a player-selected library tinted by `inlay_tint` (54-Q19) |
| `chords.png` | 128x128 RGB, two stacked X cells, original; measured by `box_mute_profile.h` | SHOULD — restyle only if the chord device (S1) changes the repeat box |

The head art is measured at load (`head_art_profile.h:22-39, 80-96`: technique coverage identical
to standard, node cell a diamond, rect fitted to a rounded box), and both measured assets report
through the shared `StructuralArtError` (`structural_art.h:23-38`). A new head stays inside that
rect family, or the contract is revisited in the same change.

### Redesigned once in code, not themed

A theme cannot vary procedurally drawn geometry without code variants. One shape, RockHero's own,
is the simple model, so these change once for every theme:

- **S1 — the chord device.** The corner-holder fans (:1599-1606, "Charter's ChordBoxHolderModel"
  :1595), the end-to-middle gradient bars (`pushMiddleFadedQuads` :1034-1054) and the panel
  (`chordBoxFrame` :1559-1570, `pushChordBoxPanel` :1572-1706) are replaced by a RockHero device
  (54-Q7). Recoloring alone keeps the holder shape. Its sides from the hand window
  (`highwayBoxSidesAt` :3442), top at the grid top (:2988) and frame thickness `string_grid_base_y`
  (:3456) stay.
- **S2 — the open-bar L posts** (:4917-5004) reproduce the holders standalone. They change with S1
  in the same commit.
- **Provenance comments.** The comments naming Charter's values or models (:199, :361, :373, :400,
  :415, :1595; `highway_metrics.h:49-53, 92-97, 121, 194-206, 215-227`) are rewritten to state
  RockHero's own reasons when their code changes. Whether `note_half_width` moves is the head
  brief's call (54-Q14).
- **S3 — the technique marks.** A mark is notation, so it is RockHero's and the same in every
  theme. A theme still colors marks through their structural tint, but it never supplies their
  cells. Each mark is redrawn once from the 54-Q8 brief. The cells move off the theme's head sheet
  per 54-Q17, so no theme directory can carry them. `highwayHeadMarks` stays the one stacking
  authority.
- **S4 — the glow posts** (`highway_renderer.cpp`, the "Glow post" block and its constants
  `g_shadow_post_*`): the upright stem of the tail's ribbon under each single note, from the board
  up toward the head, the note's shadow. Added 2026-10-11 (user): redesigned once in code like S1
  and S2, per 54-Q22, never a theme key; it wears its string's color as today.

### SHOULD NOT — not representable in the schema

No key for any of these exists; they are unrepresentable rather than validated.

- **Geometry and camera**: every `HighwayMetrics` field (`highway_metrics.h:45-245` — spacing,
  note size, `z_per_second`, visibility window, far fade, camera, frustum), `g_highway_fret_count`,
  camera zones and glide (`highway_projection.cpp:28`, `highway_window.cpp:80-82`).
- **Renderer numerics** (~51 `double`s and 3 `int`s): attack geometry :72-74, light timing and
  falloff :80-117, tail shading and sampling :124-134 and :378-413, head and marker timing
  :143-359, open-note prism :364-371, posts :400-408, accent light :268-329 and :735, rails
  :447-453, chord-box alphas, capo fractions :215-219, inlay spacing :751, and the inline
  numerics for ribbon tiers, bar heights, line widths and glyph heights.
- **Core laws**: `highway_light.h`, `highway_hit_glow.h`, `highway_tail.h` (vibrato, tremolo,
  taper — ruled display laws), `g_open_tail_margin`, `g_unpitched_slide_end_alpha`.
- **Emphasis**: ghost 0.5, accent 1.5 (`highway_emphasis_styles.h:43, 82, 91`); the 2D lane uses
  the same ghost alpha.
- **Atlas layout and cell meaning**: grid size and cell indices (`highway_atlas.h:131-282`), glyph
  atlas (`highway_atlas.cpp:17-18`). A theme supplies head art that satisfies the layout, never
  mark cells (S3); a cell is a meaning, and `highwayHeadMarks` is the one stacking authority.
- **Shaders** (17 `.sc` plus `highway_fade.sh`, eight programs; no color literals). The G channel
  adds unmodulated white (`fs_texture_tint.sc:13`), so no theme can tint the highlight — inherent
  to the structural channel scheme; recorded in the authoring notes.
- **String-color derivation** (plan 45: the lane, ring, fill and tail surfaces derived from each
  string color; a theme supplies only the eight colors, 54-Q18) and
  **display options** (`HighwayDisplayOptions`:
  mirror, orientation, lane minimum).

### COULD — out of v1

Glyph font (`highway_atlas.cpp:163-165`; a `font.ttf` in the theme directory later); backdrop art
(`makeHighwayBackgroundWorldToClip`, `highway_camera.cpp:383`, and the three `background_*`
metrics are defined but never called — a dormant seam); inlay patterns (plan 58); tail slope-shade
depth; the rolling flip as a player option.

### Schema guardrails

The notation's color distinctions are each enforced by the loader with one `distinct(a, b)` check
(a minimum CIELAB ΔE, provisionally 15 until the built-ins are sighted side by side), comparing
colors as drawn, a derived color included, so a theme cannot collapse a distinction:

- the kind of span and the hand: `chord` vs `arpeggio`; `chord` vs `pickingHandColor(chord)`;
  `arpeggio` vs `pickingHandColor(arpeggio)`; `pickingHandColor(chord)` vs
  `pickingHandColor(arpeggio)`;
- the three states of a fret number, pairwise: `fret_number_lit`, the dim number
  (`fretNumberDim`, `lane_line` at half alpha), `fret_number_target`;
- `strike_fretting` vs `strike_picking` — the two hands' pops;
- every two string colors;
- plus one order check: L*(`fret_line_lit`) exceeds L*(`fret_line`) by a margin (provisionally 30),
  since the hand window is read from it;
- from Phase 4b, `distinct()` between the two 2D shape inks derived from `chord` and `arpeggio`,
  which share a luminance by construction.

The ice theme passes with its closest pair, the two picking-hand colors, 21.1 apart.

**Kind is authored, hand is derived (ruled by the user, 2026-10-11).** Every box and rail tells
two things apart by color alone: the kind of span (chord or arpeggio) and the hand (fretting or
picking). The theme authors one color per kind, `chord` (renamed from `chord_box`) and `arpeggio`,
which are the fretting hand's; each drives everything of its kind — box frame, fill ends, accent
light, palm mark and rails — so the arpeggio box wears `arpeggio` rather than the chord color. The
picking hand's colors are derived by `pickingHandColor` (see "Derived in code, never roles"), so
hue says the kind and colorfulness says the hand. `lane_border` becomes `lane_line` and paints only
the lane ribbons, since the rails now wear their kind's color; a theme sets its lane lines anywhere
from clearly defined to nearly invisible by `lane_line` alone, and the ribbons' falloff away from
the hand window (the 1/8 tier, 54-Q20) stays a light law in code (superseded 2026-10-12: see
54-Q20). `pick_furniture`,
`chord_box_dark` and `fret_number_dim` are deleted as roles, now derived. This supersedes the
earlier same-day split of `lane_border` into `lane_line` and a `shape_rail` role (24 roles):
`shape_rail` is never added, and the theme holds 20 roles (21 since 54-Q20's re-ruling added
`lane_line_lit`). The ice theme's `chord` is `#5CD3F5`
(ice cyan) and its `arpeggio` `#A361FF` (a vivid purple at nearly ice cyan's perceived weight),
RULED 2026-10-12 by the user from a sighting of 33 pairs. Chords and arpeggios keep separate colors
because they are edited as different things; a one-color theme stays refused by the guardrails.

Texture contracts: the two existing measurements (`measureHeadArtProfile`,
`measureBoxMuteProfiles`) plus a grid-dimension check per asset.

## Current state (verified at `6f87951f`)

Re-verified at `602d5504`: every color constant and its value is unchanged. Renderer lines after
:5808 sit two lower than cited here, the 2D lane's palette fetch is at `tab_paint_core.cpp:2352`,
and the 2D fixed colors are still at :37-42. **This section is the pre-Phase-1 baseline.** Phase 1
(`3b98a871`) replaced the color constants with `HighwayTheme` reads, gave `create` the theme and
the palette, and injected the palette into both surfaces; renderer line numbers below predate it.

- **Renderer**: `HighwayRenderer::create(const HighwayShaderSet&, const HighwayTextureSet&)`,
  `setViewState`, `draw`, plus `resetCamera`, `cameraPose`, `setDisplayOptions`
  (`highway_renderer.h:122-216`). Two construction sites: `RockHeroGame::onInit`
  (`rock_hero_game.cpp:154`) and `PreviewSurface::bringUpRenderer` (`preview_surface.cpp:193`),
  which keeps the bgfx device alive on renderer failure, so a live theme switch recreates the
  renderer, never the device.
- **Metrics**: `HighwayMetrics` is a default-constructed renderer member (:2098); nothing sets it
  from outside.
- **Textures**: three (`fingering.png` was deleted in `bdc8ebf4`). Deployed by
  `cmake/RockHeroProductResources.cmake`. `textures/LICENSE.txt` covers exactly `notes.png` and
  `inlays.png`.
- **String palette**: still the single `charterClassicPalette()`; no registry. Fetched internally at
  :2709, :2778 and `rock-hero-common/ui/src/tab/tab_paint_core.cpp:2385`, so a palette picker
  changes nothing on either surface until those take the palette as a parameter. The default
  palette's six hexes are Charter's values (`string_color_palette.cpp:40-41`).
- **Orientation**: both products hard-code `invert_string_order = true`, lowest string on top
  (`rock-hero-game/ui/src/game/game.cpp:193-199`, "until a per-player setting exists";
  `rock-hero-editor/ui/src/preview/preview_surface.h:157`). The type's default, `false`, stacks it
  at the bottom as printed tablature does.
- **2D lane**: its own fixed colors for the same distinctions (`tab_paint_core.cpp:37-42`, commented
  "Charter modern-theme fixed colors").
- **Settings ports**: `IEditorSettings` (`i_editor_settings.h:158`, the `tabMinimumDisplayedStrings`
  pattern) and `IGameSettings` (`i_game_settings.h:34-111`).
- **Tests that exist**: `test_render_device.cpp`, `test_highway_renderer_smoke.cpp`,
  `test_box_mute_profile.cpp`, `test_head_art_profile.cpp`, `test_highway_atlas.cpp`,
  `test_editor_settings.cpp`, `test_editor_controller_state.cpp`.
- **New since the 2026-08-02 plan and inside this one's scope**: the two-hand light model
  (`HandLightStyle` :686-717), the floor light and rails as light, capo (`ac75d633`), the
  accent/ghost axis with the load-measured head silhouette (`7e4896de`), section labels, harmonic
  node marks, the pick-slide cell, the 4x5 marks-final atlas (`1333163f`), the single-dot inlay
  and plan 58's user fretboard art (whose natural home is the theme directory), display options
  in the renderer (`90447b24`), one shared highway state for both surfaces (`dcb28d22`).

## Decisions carried from the 2026-08-02 plan

- **Kept**: built-ins fail loudly, user files fall back; preset ids are stable strings persisted
  per product through one registry (plan 45 decision 7); art owns structure and the theme owns
  color, now covering `notes.png` through `head_art_profile`; formats change in place; geometry is
  not themeable (now the tiers above); the palette stays its own selection, and the user's pinned
  palette always wins over the theme's choice (54-Q2 A, recast by 54-Q18 so a theme states its own
  string colors); themes are found by scanning a folder (54-Q3 A); two built-in themes
  (54-Q4 B).
- **Changed**: "built-in themes are complete" now means a complete theme directory; 54-Q1 C
  (inheritance from any theme) narrows if 54-Q11 is taken; the byte-identical extraction holds for
  Phase 1 only, since the product goal is a new look; the 2D non-goal narrows to the 2D lane's own
  chrome; the schedule risk is Phases 0 and 5 (design and art), not engineering.
- **Dropped**: a `constexpr` default theme as the lasting authority (Phases 1 and 1b only — from
  Phase 2a the default theme's `theme.json` is the one authority for its roles and its string
  colors, and `defaultHighwayTheme()` and `g_charter_classic_palette` are deleted), the 16-role list,
  `chord_name`, every `fingering.png` and 8x4-inlay sentence, and export/import as a roadmap phase.

## Phase 0 direction (2026-10-09)

Phase 0 mock-ups were rendered in a scratch mock that reproduces the renderer's geometry and drawing
order, verified pixel-exact against today's look, with only theme values and art varying. They are
kept locally, outside the tree. The user's picks from them:

- **The default look:**
  - chrome: the "vector" construction in its ice-blue accent;
  - head: the rectangle plate;
  - chord device: the "box" device;
  - inlay: RockHero's own circle dot, possibly opaque.

  Whether ice blue reads too close to today's look is settled at the first sighting, beside the
  other accents.
- **The vector construction:**
  - the ground is black and the board off the hand window is dark and nearly neutral;
  - one accent hue lives in the hand window, chord box and beat bars, drives every chrome role by a
    fixed derivation rule, and keeps clear of the six string hues;
  - lanes carrying inlays read darker than plain lanes (about 2.8x, against today's 2x).
- **Built-ins:** each accent (ice blue, violet, rose, amber-gold, red-orange, neutral) ships as its
  own preloadable built-in, inheriting one base (54-Q11).
- **Head library candidates:** rectangle, capsule, stretched hexagon, emerald cut, chamfered plate.
- **Inlay library candidates:** a plain dot like today's, redrawn by RockHero; a hollow ring; and
  the whole-look concepts' style-drawn inlay.
- **Pickers (54-Q18, 54-Q19):** string palette, head shape and inlay each default to "Theme's
  choice" and are pinned by picking a concrete entry. The game's theme preview shows the player's
  pins applied; the later theme editor's preview ignores them so an author sees their own choices.
- **Harmonics (feeds 54-Q8). Direction 2026-10-12 (user)**, confirming the 2026-10-09 lean. The
  two harmonics become one designed pair, and the ring and the ellipse marks retire:
  - **Natural:** the diamond node head alone (the edge-height diamond), with a hollow inner
    diamond, a line or glow inset from its edge, and no mark over it. In the 2026-10-09 library
    round this is option N4 (inner line) or N2 (hollow).
  - **Pinch:** the ordinary head of the active library shape, with a dark diamond centered on it.
    This is the round's "dark diamond" (picking-hand dark fill, light rim, 1.07 x string pitch
    vertex to vertex, before 54-Q8 re-derives the size law).
  - **One style:** the pinch's diamond and the natural's inner diamond are drawn by the same rule
    (stroke, rim and proportion), so the two read as a pair. The polarity flips: a light inner
    diamond on a diamond head for the natural, and a dark diamond on an ordinary head for the
    pinch. This matches the hand-fill split, if 54-Q8 keeps it.
  - **What the round measured, which the candidates must beat:**
    - On a whole head, the white lift of a light fill clips to white on most strings. N1 and N2
      lose string identity entirely (0.0 dE00) and N4 loses most of it (6.5, against today's
      17.2). The inner diamond's lift is the knob, and it must keep the string readable.
    - N4's 2-texel line is 0.4 px at the far end and merges into a lighter centre there, so the
      inner diamond's stroke is sized for the far end, not the near.
    - The dark-diamond pinch fits every library shape (99% on the head). Against a plain head it
      reads only 25% / 22% distinct at mid / far distance, against today's pinch at 51% / 25%. The
      candidates must close the mid-distance gap through size and rim contrast.
  - The ring's cross-hair quality, which suits a precisely placed node, is worth keeping in the
    inner diamond's construction where it costs nothing.
  - **2D consequence (decided with the brief, not after):** the tab lane draws both harmonics as a
    diamond head and marks the pinch with a bar. Since surfaces must not diverge, the brief rules
    whether the tab lane adopts the same polarity pair. Until then, the 2D lane keeps its current
    drawing.
- **Later themes:** animated backdrops for the synthwave and arcade themes, and the render looks,
  are wanted after v1 (`docs/plans/todo/intricate-highway-themes.md`; backdrop art is COULD tier).
  Night Drive and Retro Arcade ship as color themes since 2026-10-12.
- **A theme editor** (build, save, import and export themes) comes later, with 54-Q12's todo plan.

## Open questions (Phase 0 gate)

Each carries a recommendation; none is decided until the user rules.

- **54-Q5 — orientation default.** Ship `invert_string_order = false` (lowest string at the
  bottom, as printed tablature) as the default, behind a sighting in both products, and make it
  the per-player setting `game.cpp:193` says is owed. *Recommended.* Never a theme key.
- **54-Q6 — head brief.** A solid rounded plate with one highlight band and no inset ring; a
  capsule; or a number-in-head. *Recommended: the plate* — it stays inside the rect-family
  contract, and a number-in-head would duplicate the floor numbers and need a 2D answer.
  *Direction 2026-10-09:* the plate is the default library shape, and the capsule and three
  others join it as player choices (54-Q19).
- **54-Q7 — chord device.** Settled by trying candidates, not by picking one on paper.
  *Direction 2026-10-09:* the "box" device, prototyped in Phase 4a and settled in motion:
  - it keeps today's panel geometry, its place along the highway, the frame alpha and the fill;
  - it drops the corner holders and the gradient on the bars, which become flat;
  - the palm X takes the flat frame color, and the side columns start above the bottom bar;
  - the open-bar L posts lose their foot and keep the upright leg on both sides. **RULED
    2026-10-11 (user)** from the Phase 4a candidates (corner, upright, foot only, none): "looks
    real clean";
  - the repeat X stays today's `chords.png` cells, tinted per theme, until the 54-Q8 brief
    redraws the X family; the repeat Xs then follow it (Phase 5's `chords.png` item);
  - the faint fill keeps its end-to-middle shading (**RULED 2026-10-11, user, over a flat fill**:
    it reads clearer and more transparent); its middle is derived from the box color
    (`boxFillMiddle`, "Derived in code, never roles"), so no role holds it.
  - Every candidate drops the corner holders and the end-to-middle gradient.
  - The repeat boxes (S1) and the open-bar posts (S2) follow whichever device wins.
  - Phase 0 renders stills of each candidate; Phase 4a prototypes the shortlist in motion and the
    user settles one.
  - Starting candidate: a floor bracket or understroke with short ticks, with repeats as the same
    device ghosted at half height carrying the X.
- **54-Q8 — the technique marks.** Two tests decide each mark, in this order:
  1. Its silhouette must read at the far end of the visibility window, where a mark is a few
     pixels tall.
  2. It should carry over to printed tablature.

  Letters such as T and P.M. fail the first test. *Recommended:* keep each shape-to-meaning
  pairing players already read on a 3D highway:
  - the wide palm X and the compact mute X;
  - the flipped triangle pair for legato;
  - the bend caret and the V tap;
  - the ring harmonic over the diamond base, and the ellipse pinch;
  - the arpeggio brackets.

  Slap and pop are the two marks whose shapes are drawings rather than simple geometry, so they
  get new shapes, or S and P if those pass the distance test. Each mark is chosen from rendered
  candidates, sighted at distance.

  Every mark is redrawn from a written brief in RockHero's own rendering: stroke, fill, highlight
  and proportions. Nothing is measured from reference images. The brief also sets the mark
  family's size law (`highway_atlas.h:210, 238`, 1.07 x the string pitch) from far-end
  legibility, and decides whether two splits stay:
  - the light-fill fretting-hand / dark-fill picking-hand split (`highway_atlas.h:115-120`);
  - the wide / compact X split.

  *Recommended: keep both.* Each encodes notation that has to read at distance.

  *Direction 2026-10-09:* the harmonic lean in "Phase 0 direction" replaces the ring and the
  ellipse. One mark set must fit every library shape (54-Q19). The Phase 0 measurements show that
  scaling today's wide marks (palm X, pinch, slap, pop, tap) down to fit the hexagon and emerald
  costs far-end contrast, so the redraw sizes those five for the narrowest library shape.

  *IP review 2026-10-12 (risk analysis, not legal advice; records kept outside the tree).* The
  shipped `resources/highway/marks.png` was never redesigned. The August re-bake re-rendered
  Charter's shapes at a new size and symmetry, and Charter's shapes follow the inspiring game's
  closely. Eight of twelve cells keep Charter's silhouette and internal construction. The pairings
  are notation, and every one of them may stay. The risk lies in particular renderings:
  - **Slap and pop: HIGH.** They are free-form drawings with no notation precedent, and they match
    the inspiring game's in silhouette, proportion and fill treatment. They must be new RockHero
    shapes. *User 2026-10-12:* S and P letters probably would not read as obviously, so the brief
    designs new shapes. Starting points from that discussion (not decided):
    - motion shapes, such as a flat impact bar for the thumb's strike down and an upward hook for
      the finger's snap;
    - one shared form varied between the two (never the triangle's flip, which legato owns);
    - a bold S and P drawn as glyphs in the mark style.
  - **Natural-harmonic bullseye and ellipse pinch: MED.** The bullseye's band radii equal the
    inspiring game's within 0.03 of the radius. The harmonic direction in "Phase 0 direction"
    retires both.
  - **Mute X pair, V tap and `chords.png`'s repeat Xs: LOW each, MED as a family.** The X and the
    V are familiar symbols. But the wide-dark / compact-light split in the same fill-and-outline
    treatment, with square-cut tips, reproduces the inspiring game's encoding, and the repeat-box
    Xs repeat it. The brief therefore keeps the pairings but sets RockHero's own tip and stroke
    rule for the whole family, and either splits one axis differently or states why both splits
    stay.
  - **The size law** (1.07 x string pitch) was measured from the inspiring game's captures, as
    commit `1333163f` records. The brief re-derives it from far-end legibility, as above. No
    figure measured from reference images survives the redraw.
  - **Legato triangle, arpeggio brackets, bend caret and pick slide: LOW or none.** Each is kept
    as a pairing and redrawn in the family's style like every other mark.

  The brief is written, and the candidates drawn, without reference images open. Until Phase 5
  lands, nothing should describe the shipped marks as independently designed.
- **54-Q9 — accent pairs.** The chrome hue family for each built-in, chosen clear of the six
  string hues; exact values by sighting. *Direction 2026-10-09:* one accent hue per built-in,
  deriving every chrome role by the vector construction's rule (lightness and chroma per role,
  capped where a role nears a string hue, and at least 64° of hue from today's teal). The six
  accents in "Phase 0 direction" are the built-ins, and ice blue is the default candidate.
- **54-Q10 — today's look after it leaves the tree. RULED 2026-10-08 (user):** it is kept as the
  user's private theme, loadable in both products, and never committed or distributed in any form,
  because the repository is public. It is saved as files outside the tree before any of its values
  leave the default, and loads in-app once Phase 2a lands (Phase 3); until then the user accepts
  that it cannot be selected (2026-10-09).
  - **What it holds:** today's colors in the 21 roles, `notes.png` (heads only after the Phase 4b
    split), `inlays.png`, `chords.png`, and the Charter license notice.
  - **What it cannot hold:** the code-drawn parts (S1 chord-box holders and gradient bars, S2
    open-bar posts) and, from Phase 5, today's mark drawings on the fixed sheet (S3). It draws
    those with the universal RockHero designs. The user accepted this loss.
- **54-Q11 — inheritance scope.** `inherits` may name only a built-in theme id at v1. Deletes the
  cycle, two-pass-load, cascading-drop and depth-cap failure modes. *Recommended.* The accent
  built-ins use the same mechanism: each one inherits the vector base and sets its own colors.
- **54-Q12 — export/import.** Moves to `docs/plans/todo/highway-theme-packaging.md`; with 54-Q11 a
  copied theme folder is already self-contained apart from a built-in base every install has.
  *Recommended.*
- **54-Q13 — the default palette's shades.** An owned retune of each of the six hues, keeping the
  order. Belongs to plan 45 Phase 2. *Recommended: add it there.*
- **54-Q14 — proportions.** Whether `note_half_width` and related metrics move with the new head.
  *Recommended: the head brief decides*; the provenance comments are rewritten either way.
- **54-Q15 — the inlay dot.** Redraw it in the same bake and delete `textures/LICENSE.txt`.
  *Recommended.* A plain circle is generic fretboard furniture, so RockHero's own circle keeps
  today's form. Its opacity is `inlay_tint`'s alpha, a theme value.
- **54-Q16 — game-side picker.** Plan 26's settings surface, or a launch flag at v1. *Recommended:
  plan 26.* The first sighting needs neither: the new look is the compiled default, and Phase 2a
  later selects a theme through the stored settings id alone.
- **54-Q17 — where the marks live (S3).** Two options:
  - **Split the sheet.** The mark cells become a fixed `marks.png`, a fourth `HighwayTexture` that
    no theme directory may carry, beside a heads-only `notes.png` that themes may replace.
  - **One sheet, never themed.** `notes.png` keeps both heads and marks, and themes lose custom
    heads.

  *Recommended: split.* Themes keep custom heads, and the head contract measures only heads.
  **Landed 2026-10-12 (Phase 4b) as split FILES, one TEXTURE:** the heads-only `notes.png`
  (256×128) and the fixed `resources/highway/marks.png` (256×192) are composed at load into the
  one 256×320 atlas the draw path always read (`composeHeadAtlas`), so cell indices and batching
  are unchanged and there is no second texture to measure. The mark sheet is product art checked
  by `checkHighwayMarkSheet` in `HighwayRenderer::create`, never by the theme loader.
- **54-Q18 — string colors and the theme. RULED 2026-10-09 (user), recast the same day:** each
  theme states its string colors, and the player may pin a palette so it survives theme switches.
  This replaces the first ruling, a bounded `string_shading` over the user's palette: a theme that
  wants a variation of the usual colors states that variation, so shading had nothing left to do
  and is deleted.
  - **The key, recast 2026-10-11:** `colors.strings` in `theme.json` (see "The theme file"),
    inside `colors` because string colors are colors. It is the one mechanism: there is no palette
    id key, no palette file in a theme folder and no separate user-palette format. Every theme
    that writes `strings` registers as a palette in plan 45's registry under its own `name`; a
    theme that inherits its strings adds no entry, so its label names the theme that wrote them. A
    player who wants only their own string colors writes a theme that inherits a built-in and sets
    `strings`, then pins it.
  - **Code-owned palettes** are only the accessibility ones (plan 45 Phase 4). `colorblind_safe`
    rests on a test and a sign-off no file can vouch for, so only these carry it.
  - **The pin names its source**, a theme id or a code palette id, so a user folder cannot collide
    with a palette id.
  - **The setting:** one, shared by both products (plan 70), defaulting to "Theme's choice"; picking a concrete palette
    pins it (see "Theme's choice, or pinned"). Most themes will carry a variation of the same
    colors, and one that does not has a reason; a player who learned the strings, or needs a
    colorblind-safe palette, pins theirs with one pick.
  - **The labels:** the default entry reads `Theme's choice (<resolved palette>)`, naming what it
    resolves to, never the theme, and carrying the colorblind-safe flag when the palette has it.
    "Theme default" (collides with "the default theme") and "Match theme" (reads as an action)
    are not used. No separate Pin button: a concrete pick already pins in one click.
  - **The guardrails:** every palette, a theme's included, passes the string-order and
    distinctness checks. A pinned palette may clash with a theme's accent; that is the player's
    choice.
- **54-Q19 — head shape and the theme. Direction 2026-10-09 (user), recast with 54-Q18:** the
  theme names a head shape from a shared library and the player may pin one, under the same
  "Theme's choice" rule; the theme always supplies the head's style (material), so any shape pairs
  with any theme.
  - **Library candidates:** the rectangle (default), capsule, chamfered plate, stretched hexagon
    and flat emerald cut, from Phase 0 sighting. The emerald's material reads as a different style
    from the others. The stadium is a reserve. Asymmetric shapes were rejected. The stretched
    hexagon needs the head contract to gain one parameter, its tip run; the others fit it as is.
  - **Inlays follow the same rule:** the theme names an inlay from a small library, the player may
    pin one, and `inlay_tint` tints it.
  - **Library ids only at v1:** a theme names library entries and never ships its own shape or
    inlay outline; new entries join the library. Custom shapes wait for the theme editor.
  - **Recommended mechanism:** a load-time bake into the existing atlas. Each library shape is a
    structural outline (coverage, edge distance, inner-line mask) and the theme supplies a
    material profile (bands, rim, lift, core, line). One bake composes them, so any shape pairs
    with any theme and no theme ships per-shape cells. The `notes.png` head-cell asset above then
    becomes a material description, and the head contract is measured per library shape.
  - **Marks:** they are notation, so one mark set must fit every library shape (S3, 54-Q8).
- **54-Q20 — the faint ribbon tier.** `drawLaneBorderRibbons` draws every lane line away from the
  hand window at a fixed 1/8 of `lane_line`. That is a renderer numeric, not a theme value. A
  saturated accent still glows at 1/8, so the lines cannot fade with distance as today's teal
  does. The vector construction therefore mutes its ribbons to Oklab chroma about 9.
  - **Option A:** keep 1/8, and the muted ribbons.
  - **Option B:** lower the tier to about 1/16, which lets a fully saturated accent (chroma about
    24) fade like today's. Because it is not a theme value, B also dims today's look in the
    private theme.

  **RULED 2026-10-11 (user): A, keep 1/8.** B existed only because a saturated accent still glows
  at 1/8; a theme controls its lines' visibility through its own color, from clearly defined to
  nearly invisible. The ratio stays a light law in code.

  **RE-RULED 2026-10-12 (user): the theme sets the faint tier.** Sighting Retro Arcade, the user
  wanted the lines outside the hand window to fade much less, and wants themes to control that;
  under the ruling above a theme could only dim them. The ribbons now mirror the fret lines:
  `lane_line` is the color away from every light, alpha included, and the new `lane_line_lit`
  the color at full light, the tiers crossfading between them (the mid tier lifts 2/7 of the
  way). Ice states `lane_line` as its lit color at alpha `0x1F`, today's 1/8 tier, so its look
  holds; the dim fret number derives from `lane_line_lit`. The tiers' timing stays a light law
  in code.
- **54-Q21 — the note targets' default. Recast 2026-10-11:** the cue, renamed "Note Targets"
  (user, after a naming review: "ring" was wrong about the shape and "anticipation" already means
  a timing in music), becomes a player toggle (plan 69), so the sighting below decides whether it
  starts on or off, not whether it exists. It is a hollow copy of the head, parked
  at the hit line on the note's landing slot, that grows to full head size as the note arrives
  (`highway_renderer.cpp:5208-5261`; `g_anticipation_seconds` :233; the hollow cells 2 and 5,
  `highway_atlas.h:140, 271`). It came from the inspiring game's notation by way of Charter (its
  growth law is Charter's `drawNoteAnticipation`, line for line), and the user doubts it adds real
  readability.
  - **The legal side does not decide it** (IP assessment, 2026-10-09, kept in the local dossier):
    the cue is an idea with genre precedent, and its protectable part is the hollow art, which
    the head redesign replaces anyway. Dropping it gains only marginally against a copyright,
    takedown or trade-dress claim. If it stays, its timing is re-derived from RockHero's own
    sighting instead of the ported law, and the reason is recorded in the code.
  - **Decided by sighting, side by side** (Phase 1b), on the new default look:
    - two local builds run at once, the normal one and a throwaway variant with the ring's draw
      block disabled (never committed), on the same chart and section, in the game and the
      editor preview;
    - the material covers what the ring might help with: dense single-note runs, fast position
      shifts, chords, pre-bends, harmonics, and far-off-window landings, at full and reduced
      speed.
  - **Either way** the ring is retimed from RockHero's own sighting, and a reshape (a ring that
    shrinks onto the slot, or a slot highlight) remains an option if the user wants more
    distance. The hollow cells stay regardless, since the pre-bend target outline draws with them.

  *Recommended:* none until the sighting; the user leans toward off.

- **54-Q22 — the glow posts' redesign (S4). Added 2026-10-11 (user).** The user wants the posts
  under the notes redesigned as well as the open-bar posts (S2), and has seen no alternative for
  either. Phase 4a renders candidates for both beside today's, in the switched colors, and the
  user rules from them in motion. **The glow post's geometry stays (user, 2026-10-11):** the
  vertical space it takes and its vertical fade were carefully tuned, so its candidates vary only
  its STYLE within today's footprint (a solid band, two edge rails, a soft light-like falloff, a
  brightened core), beside today's ribbon; the earlier geometry candidates (a thinner line, a
  shorter stem, a floor dash, none) remain in the sighting build only as references. What the post
  must keep doing: stand as a vertical post beneath the note, tying a high head to its slot at
  distance, and stay quieter than the head.
  **RULED 2026-10-11 (user): keep today's ribbon.** The posts are modeled on RockHero's own
  tails, not on the inspiring game, and the user likes how they look; no restyle.

## Phased implementation

Dependencies: 1 ← 0 (role names); 1b ← 1; 4a ← 1 and 54-Q7; 2a ← 1; 3 ← 1b and 2a; 2b ← 2a;
4b ← 1; 5 ← 1b, 4a, 4b and the brief; 6 ← 5.

**Release gate.** Phases 1 to 5 gate the first public release
(`docs/plans/in-progress/first-releasable-editor.md`): no tagged release ships today's chrome
values, the Charter-adapted art or the holder chord box. Phase 6 does not gate it.

### Path to the first in-app sighting

The goal is to see the Phase 0 default look in both products, in motion, as early as possible,
with proof that every themable color is routed. The elements the scratch mock could not draw are
seen there for the first time: fret numbers, strike pops, picking-hand furniture, capo, arpeggio
and labels.

The new look becomes the compiled default before theme loading exists. Nothing is redone later:
Phase 2a moves the default's values into its `theme.json` by copying them, and both products
share the one renderer, so every step shows in both. The cost, accepted by the user on 2026-10-09: today's look
cannot be selected in-app until Phase 2a lands, and comparing the two looks means two builds.

1. **Phase 1:** every color is a theme read, with zero visual change (landed).
2. **Phase 1b, the switch and the first sighting** (below).
3. **Phase 4a:** the box chord device and posts in code. With it the default look is complete and
   sighted again. It follows 1b because its sighting judges the device in the switched colors.

Theme directories, the default theme as a file and the private theme loading in-app (Phases 2a
and 3), pickers and inheritance (Phase 2b), the 2D theme reads and the mark-sheet split (Phase 4b), the shape
and inlay libraries and the new marks (Phase 5), and the remaining built-ins (Phase 6) follow.

### Phase 0 — Design brief and decisions

Phase 0 produces:
- the written head brief and mark brief (54-Q8);
- chord-device candidate stills (54-Q7);
- the accent family (54-Q9);
- several rendered candidates for every redesigned item (heads, marks, chord device), made through
  the `texture-author` agent, so each settles by comparison rather than on paper;
- the user's rulings on 54-Q5 to Q22.

The default look's direction is in "Phase 0 direction". The scratch mock bakes its theme values
and head cells, and the sighting theme is built from them. The mark brief stays open and
runs in parallel with the engineering phases. **STOP: the user signs off on the mark brief before
Phase 5 begins.**

### Phase 1 — Color roles and palette injection

- New feature folder `highway_theme/` in `rock-hero-common/ui` (public header plus source):
  `HighwayTheme` with the 23 roles, each member's Doxygen naming what draws with it.
- `HighwayRenderer::create(shaders, textures, theme)` and `setTheme(HighwayTheme)`; every `g_*`
  color read becomes a theme read in one mechanical pass. `attack_line` splits from `chord_box`;
  `lane_border` is stored opaque; `pick_furniture` replaces the offline-derived white; the
  derived colors listed above are computed from their roles.
- **Palette injection**: the two renderer fetches and `tab_paint_core.cpp:2352` take the palette as
  a parameter (plan 45 Phase 3's seam, landed here so a picker is never inert).
- The backdrop converts ARGB to bgfx's packed RGBA at the one `setViewClear` call.
- Both construction sites pass today's values. **Zero visual change.**

**Tests**: `test_highway_theme.cpp` pins all 23 roles' current values including alpha; the
backdrop conversion round-trips `0x000000ff`; the smoke and render-device tests still bring the
renderer up. **Coverage** is proven without a sighting here: every former constant is deleted, so
a missed read cannot compile; every role has a theme read; no color literal remains in the
renderer beyond channel masks and the tail-shading law's white and black; the shaders carry
none; and the glyph and head atlases are tinted per vertex. The ice sighting shows any element
whose role changes hue, and Phase 3's garish fixture covers the rest through the loader.
**Verification**: configure (new sources), build, touched tests.

### Phase 1b — Today's look saved; the default becomes the new look

- **Save today's look (outside the repository).** Write a theme directory into the per-user
  themes folder Phase 2a will scan, in "The theme file" format:
  - a `theme.json` holding today's 23 values, read from Phase 1's pinned test values, and today's
    eight string colors;
  - today's `notes.png`, `inlays.png` and `chords.png`;
  - a copy of `textures/LICENSE.txt`, whose notice travels with the Charter-adapted art.

  It is never committed, shipped or posted (54-Q10), and the user keeps a private backup. It
  cannot load until Phase 2a; the commit before this switch is the second copy.
- **The switch.** The compiled default takes the ice-blue values (54-Q9). The scratch look file
  uses the 23 role names and packed `#AARRGGBB` hex, which maps one-to-one onto the C++ literal
  (the theme file's `#RRGGBBAA` order applies from Phase 2a). The default palette is unchanged.
  Phase 1's 23-value pin test is deleted here, its job (proving the extraction equal to the
  deleted constants) done; the backdrop conversion test stays.
- **The interim art**, composed by the `texture-author` agent at true size from the Phase 0 cells
  the user picked:
  - `notes.png` takes the rectangle head in the default's material in cells 0, 1 and 2 (base,
    tech, the rectangle's hollow) and the diamond family in 4 and 5 (the node base and its
    hollow), so no head wears the old material beside the new; every other cell, today's marks
    included until the Phase 4b split, stays byte-identical. The head contract measures the new
    cells;
  - `inlays.png` takes RockHero's own untinted circle dot (54-Q15), one file: its opacity is
    `inlay_tint`'s alpha, a theme value;
  - `textures/LICENSE.txt` narrows to `notes.png`'s mark cells. The generators are committed with
    Phase 5's art.

  No release ships before Phase 5, and the tree already carries those mark cells.
- **STOP: the first sighting**, in motion in both products. Theme-valued variants (the other
  accents, the inlay's opacity) are compared live in one throwaway local build, a key cycling
  them through `setTheme` in the same frame and light state; only the renderer constants run as
  separate local builds beside it: 54-Q20's tier at 1/16 and 54-Q21's ring removed, the latter
  side by side. Nothing of either is committed. Decided there: 54-Q20, the inlay's opacity,
  whether ice blue reads too close to today's look, and 54-Q21. If 54-Q21 drops the ring, the
  deletion is its own commit. The 2D lane still draws Charter's shape blue and purple until
  Phase 4b, an expected seam.

**Verification**: build, touched tests.

### Phase 2a — Theme directories, contracts and selection by setting

- A theme is a directory in "The theme file" format: `theme.json` plus its three PNGs. The
  loader's own model (id, name, base, roles, strings, choices, assets) is a separate type from
  the renderer's `HighwayTheme`. `inherits` is parsed and rejected until Phase 2b, so every theme
  here is complete.
- **One loader** for built-in and user themes. Built-ins deploy as directories under
  `resources/themes/<id>/` through `cmake/RockHeroProductResources.cmake` and fail loudly; user
  themes are scanned from per-user app data and dropped with a logged typed error.
- **The default theme becomes a file.** Phase 1b's compiled values move into
  `resources/themes/ice/theme.json`, whose `strings` are today's eight lightened as the Phase 0
  look drew them (CIELAB L* +4, chroma x1.04, hue +1°; the scratch look file holds the six
  standard shades, and the 7th and 8th follow the same rule; user, 2026-10-11). Until then the
  switched default draws today's exact string colors. In the same change
  `defaultHighwayTheme()` and `g_charter_classic_palette` are deleted: the file is the one
  authority for both, and both construction sites take the theme from the loader. A test that
  loads every shipped built-in and passes its contracts stands in for Phase 1's retired value pin,
  and code-built fixture themes serve the renderer tests. The move rotates the scratch file's
  `#AARRGGBB` into the theme file's `#RRGGBBAA`.
- Contract validation as one entry point: grid dimensions per asset plus the two existing
  measurements, each error naming the asset and the violated expectation (including
  `AlphaBearingImage`, the likeliest user mistake). The guardrails above as `distinct()` and the
  luma order.
- **The resolved palette** (54-Q18): the player's stored pin if there is one (plan 45 Phase 3's
  setting, naming its source, a theme id or a code palette id; absent means "Theme's choice"),
  otherwise the active theme's `strings`. It is resolved once and injected into both surfaces
  through Phase 1's seam. Every loaded theme registers its `strings` as a palette. *As built:* the
  pin setting does not exist yet and no code palette does either, so resolution is the default
  theme's `strings` (both hosts draw the default theme) and the registry of themes is the palette
  registry; the pin, its resolution
  and the pinned-case tests arrive with plan 45 Phase 3's setting.
- **No stored theme id (user, 2026-10-11).** Both hosts draw the default theme. The theme id is
  a shared player setting, so it arrives with plan 70 Phase 2's `highway.*` keys rather than in
  the per-app settings ports; an unknown stored id then falls back to the default.
- *As built:* `StringColorPalette` is one array of `g_max_chart_strings` colors in the theme
  file's order (highest string first), and `charterClassicPalette()` and `defaultHighwayTheme()`
  are deleted; today's string colors survive only in the private theme. The guardrail thresholds
  are provisional (CIE76 ΔE 15; the lit fret line at least 30 L* above the unlit one) until the
  built-ins are sighted side by side.
- A theme switch recreates the renderer through the existing bring-up path, so a bad user atlas
  leaves the preview usable with the device intact.

**Tests**: `test_highway_texture_contract.cpp`, `test_highway_theme_file.cpp` over fixture
directories — a complete theme; malformed JSON, unknown key, unknown role, bad hex, a `strings`
array of the wrong length, an alpha-bearing string color, an invalid folder id, a missing role,
contract-violating asset, every guardrail violation; each failure drops only the offender and
leaves the registry usable. CSS hex parsing, opaque and alpha forms. Palette resolution: pinned
to a theme, pinned to a code palette, and theme's choice. Every shipped built-in loads. Settings
round-trip. **Verification**:
configure, build, touched tests.

### Phase 2b — Pickers and inheritance

- Inheritance per 54-Q11 and "The theme file": roles one by one, `strings` and each asset whole.
  A user theme whose id matches a built-in shadows it, logged. A theme that inherits its strings
  registers no palette of its own.
- The editor View submenu (mirroring plan 45 Phase 3's shape) and the game picker per 54-Q16.
- A color-only switch calls `setTheme` without recreating the renderer.
- **The palette picker** in both products, per 54-Q18: `Theme's choice (<resolved palette>)`, a
  separator, then every theme that states `strings` and the code-owned accessibility palettes; a
  concrete pick pins, the top entry unpins. The game's theme preview draws with the player's pins
  applied.

*As built (2026-10-11):* the editor's View menu carries "Highway Theme" and "String Colors";
each pick writes the shared `highway.*` keys through the store and re-resolves the look against the
theme registry, the one resolution authority both products use (`themeFor`, `paletteFor`). The pin
is `highway.palette`, naming the theme whose file states the strings. A pick whose art bytes match
recolors through `setTheme(theme, palette)`; any other recreates the renderer. No game-side picker
(54-Q16 waits on plan 26): the game draws the shared picks at launch. A built-in may inherit the
default theme (added 2026-10-12 with the distilled built-in set: the registry loads the default
first and the other built-ins against it, so the color themes share its art). No code-owned
palettes exist yet, so the palette list is the themes that state their own strings.

**Tests**: inheriting and unknown-base fixtures in `test_highway_theme_file.cpp`, including a
strings-only theme; controller state for the theme and palette pickers, including that a theme
switch leaves a pinned palette alone and moves an unpinned one. **Verification**: build, touched
tests.

### Phase 3 — Today's look loads as the private theme; the coverage fixture

- **Verify the private theme** (after plan 70 Phase 2, which brings the stored theme id). Phase
  1b's saved directory already loads. Select it in both products
  and A/B it in motion against a build of the commit before Phase 1b's switch. It must match
  exactly, apart from the code-drawn parts Phase 4a already replaced (54-Q10) and 54-Q20's
  ribbon tier if it changed.
- **Coverage fixture.** A test fixture theme gives every role a distinct, garish value. It is
  selected once in both products, where any element that keeps a normal color reads something
  other than its role, and it stays as a fixture for the loader tests.

**STOP: the private theme sighted in motion in both products; user sign-off.**
**Verification**: build, touched tests.

### Phase 4a — The chord device in code

- **S1 and S2 per 54-Q7**: the box device, prototyped in motion, and the user settles it. Any
  losing prototype is deleted before commit.
- **S2 and S4 candidates**: the open-bar posts and the glow posts are shown as several
  candidates each (54-Q7, 54-Q22), switchable live in one throwaway local build, and the user
  rules on each; only the winners are committed.

**STOP: sighting in motion in both products**; with Phase 1b landed, this is the complete default
look. **Verification**: build, touched tests.

### Phase 4b — One authority for the 2D lane; the mark sheet splits

- **The 2D shape inks follow the theme's hues.** The 2D shape rails echo the 3D shape rails, so
  their sources are the roles those rails wear: `chord` for a held shape and `arpeggio` for an
  arpeggio span (`drawHandShapeRails`). The 2D lane derives each ink
  by one fixed law, the way a string's surfaces derive from its palette color:
  - The ink also grounds the span chip's opaque white label (`tab_paint_core.cpp:2728-2734`), so
    the law sets the relative luminance that gives white the lane's designed label contrast
    (about 4.45:1, a luminance near 0.186).
  - A base brighter than that is scaled in linear light, which keeps its hue and saturation
    exactly; a darker one that cannot scale up without a channel passing full is blended toward
    white, keeping hue and giving up a little saturation.
  - **Landed 2026-10-12** as `laneShapeInk` (`highway_theme_derived_colors`), carried to the 2D
    host as `TabShapeInks`; `tabShapeMarkColor`, `g_hand_shape_color`,
    `g_hand_shape_arpeggio_color` and the two hand-tuned scales are deleted. The old hand-tuned
    inks are exact fixed points of the law.
  - The result reads on any editor background, so no backdrop or light-theme rule is needed.
  - **The one added guardrail:** the two derived inks share a luminance by construction, so
    `distinct()` also holds between them, not only between their bases.
  - The mute inks stay the lane's own: `full_mute_mark` tints only the 3D full-mute X, while the
    2D mute rim is shared by every mute mark and carries no palm/dead distinction.
  - Under today's values the 2D rails turn from Charter's blue and purple to a darkened teal and
    purple; the Phase 4b sighting judges both themes in both surfaces.
  - **Superseded 2026-10-13 (user, on the UI review's recommendation):** the fixed-luminance law
    changed some span colors' NAMES — a light cyan darkened reads teal, an amber reads brown — so
    the 2D lane named a kind differently from the highway. The rails and the span chips now wear
    the role colors verbatim (`tabShapeInks`), one color per kind on both surfaces, and the chip's
    label takes black or white, whichever APCA rates higher over the fill (`chipLabelColorOn`,
    polarity turning at screen luminance 0.342). `laneShapeInk`, its guardrail pair and its tests
    are deleted. Default's 2D chord is therefore its light sky-blue, not the old hand-tuned royal
    blue (a different hue, 263 against 220 in OKLCH, which no rule shared by every theme reaches).
  - **OPEN 2026-10-13, to settle next session.** The user wants, under ONE rule for every theme:
    the original hand-tuned 2D blue `#4173DE` and purple `#9061C7` for Classic (3D teal `#00D2D5`
    / purple `#C040FF`) and Default; chip labels always white and readable; other themes
    coordinated. Verbatim colors with black-or-white labels (today) fails the first two; the
    retired fixed-luminance law fails the first and renames cyan/amber. The candidate on the table
    is a **snapped 2D ink palette**: fixed inks, each the most saturated color of its hue at the
    white-label luminance (Y about 0.186), the original blue and purple among them; a theme's span
    color draws as the ink whose OKLCH hue range contains it, blue owning cyan through blue (about
    175-270 degrees, so Classic's teal at 197 and Default's cyan at 220 land on the original blue);
    a theme whose chord and arpeggio land on one ink is refused at load. Seven inks (red, orange,
    gold, green, blue, purple, pink) or ten (plus teal, yellow, grey) — the count is free. Strings
    stay verbatim: they carry no white text, the ink recipe would turn the yellow string olive, and
    snapping would collapse the blue and teal strings and override palette pins. Not decided:
    whether to adopt the snap at all, and the ink count.
- **S3's sheet split** per 54-Q17 moves today's mark cells unchanged, which is zero visual change
  (verified byte-exact: the two files stacked decode to the old sheet's pixels). The private theme's `notes.png` is re-cut locally to the
  heads-only layout so it still passes the contract.

**STOP: sighting in motion in both products.** **Verification**: build, touched tests.

### Phase 5 — The default theme's art lands

- **The head library (54-Q19):**
  - the library shapes as structural outlines;
  - each theme's head material;
  - the load-time bake that composes the resolved shape with the active theme's material into the
    atlas, with the head contract measured per shape;
  - the theme's `head_shape` key and the player's shape setting in both products, resolved and
    picked as the palette is (54-Q18, 54-Q19).
- **The inlay library:** Phase 1b's circle dot as the default, plus the library alternatives,
  tinted by `inlay_tint`; the theme's `inlay` key and the player's inlay setting, resolved and
  picked the same way.
- **The art:** the redrawn fixed mark sheet (S3) from the mark brief, sized to fit every library
  shape; the harmonic pair as head-bake options (the natural's inner diamond on the node cell, the
  pinch's diamond on the ordinary head; "Phase 0 direction"); and `chords.png`'s repeat Xs in the
  brief's X-family rule. Every generator is committed.
- **The default theme:** its `theme.json` (a built-in since Phase 2a) gains `head_shape` and
  `inlay`, and its final art replaces the interim cells.
- **STOP: the theme rendered in motion in the game and the editor preview; user sign-off before
  commit.**
- Then, in the same change set:
  - delete the Charter-adapted art from the tree; the private theme already holds it;
  - delete `textures/LICENSE.txt`, which since Phase 1b covers only the old mark cells (54-Q15);
  - rewrite the provenance comments;
  - update `docs/plans/in-progress/highway-note-art-state.md`'s provenance lines and the developer
    guide.

**Verification**: build, touched tests, and the local corpus smoke (local only) so no chart shape
breaks under the new asset set.

### Phase 6 — The accent built-ins

The other accents in "Phase 0 direction" ship as built-ins. Each inherits the default's base
(54-Q11) and sets only its accent-derived colors, deriving every role by the same rule. Several
built-ins in use also keep a role that Phase 1 missed from hiding in a one-theme world, beside
Phase 3's coverage fixture. Each passes the same contracts. **STOP: sighting and sign-off.**

*Partly landed 2026-10-12, ahead of the accent study:* the built-in set was distilled from the
day's sighted candidates to six color themes beside ice — Signal and Ladder (the readability
set: strings placed for perceptual and color-vision-deficiency distance, Ladder colorblind-first)
and Ember, Phosphor, Night Drive and Retro Arcade (distinct whole looks) — each inheriting ice's
art and stating every role (`rock-hero-common/ui/resources/themes/`). The accent built-ins this
phase describes, the user's sighting and the sign-off remain; every other candidate is archived
in the commit `docs/plans/todo/intricate-highway-themes.md` names.

*Re-cut 2026-10-12 (user):* the default is now the `default` directory, named "Default" (the ice
colors unchanged), beside four Element themes — Earth, Wind, Fire and Water (Water since removed), each toning the
standard string families slightly toward its element, with a subtle hand-window light and dull,
slightly tinted inlays — and Night Drive and Retro Arcade. Signal, Ladder, Ember and Phosphor
left the built-ins (last in `9106c2ae`). The hand window's light is held to one strength: the
lane the light reveals (`lit_lane` over the backdrop at its alpha) composites to about L* 13-16
in every built-in, where Phosphor's green had reached 29; Retro Arcade's spans are a cobalt
chord and a hot-pink arpeggio, since the grape that paired with cobalt elsewhere sat 12 from the
purple string here.

*Elements re-cut 2026-10-13 (user):* Water left the set. Fire is "from hell": an obsidian board
(`#0A0507`) with blood-red ember lane lines, a deeper blood-red chord (`#B8001C`) and a hellfire
orange arpeggio (`#FF7A00`, which clears the orange and red strings where an amber did not), its
window an obsidian violet-grey. Earth is deep forest: a near-black green board, forest beat bars
and window, a pine-green chord (`#2E9A5A`) and a bark-brown arpeggio (`#B07A48`). Wind is light
and airy: a slate-blue board (`#16202C`, L* 12, the one built-in not on black) with pale washes,
an aqua chord and a lavender arpeggio. Every window still lifts its board by about L* 14-16.
That cut survives unsighted as the four "Element - … - Original" built-ins (Earth, Wind, Fire,
and Water as of `87a0f35b`), kept verbatim as comparison baselines.

*Showcase set and the no-haze rule, 2026-10-13 (user):* six immersive built-ins replace the
Elements — "Element - Fire" (living flame: gold and amber lane lines, a white-hot attack line,
a scarlet chord `#D81C1C` and a gold arpeggio `#FFB800`), "Element - Wind" (storm sky: stormcloud
beat bars, a lightning-white attack line, silver lane lines, a pale-cyan chord `#90F0FF` and a
rose-cloud arpeggio `#F8A8B8`), "Element - Earth" (deep underground: walnut beat bars, ochre
lane lines, moss in the dotted lanes, a moss chord `#5A9A2E` and a terracotta arpeggio
`#D06A2A`, amber mineral inlays), "Element - Water" (deep sea: oceanic beat bars, a caustic
attack line, 470 nm bioluminescent cyan lane lines, a cobalt chord `#3D6CF5` and a
phosphorescent mint arpeggio `#78F0C4`), "Hell" (fire and brimstone on obsidian: the board,
window, lane lines and inlays a glassy grey-violet, the hellishness held to blood-red beat bars,
a sulfur attack line, lit frets and labels, a blood-red chord `#C8001E` and a lava-orange
arpeggio `#FF7A00` — a first cut with red lane lines and a lava window was "way too red", user)
and "Heaven" (celestial light: soft-gold beat bars and lane lines on a faint
midnight board, a lilac chord `#C8B0FF` and a halo-gold arpeggio `#FFC84A`). Fire and Hell, and
Wind and Heaven, are distinct looks, not variants. Each tones the standard string families
toward its element. A seventh, "Blacklight", revives the early "Colour Vector" violet direction
under a better name: UV-violet beat bars and lane lines on pure black, a white-hot attack line,
Default's strings, a UV-violet chord `#7C68FF` and one fluorescent hot-pink accent on the
arpeggio `#FF30A8` (a paler lilac/pink pair collapsed to 12.7 once the picking-hand derivation
paled both). Two rulings bind every built-in except the Originals: **the board is near
black** (L* 0-3; a lit or tinted board — Wind Original's slate at L* 12 — veils the notes'
contrast), and **the hand window is a quiet cue, not a wash**: `lit_lane` over the backdrop
lifts it by about L* 4 (was 14-16; Default `#5978FF1D`, Night Drive `#C84CB41B`, Retro Arcade
`#6E6EFF1D`, the Colorblind set through Default), unlit lane lines composite to about L* 5 over
the board (color allowed only as that faint tint), lit lane lines sit near Default's L* 53
(49-59), and inlays stay dull (L* 27-52 over the board). The user's words: the window glow and
lane lines were "too in your face, which gets in the way of reading the charts"; when in doubt,
quieter. The haze the user named — a theme that "feels like
it has a haze that makes things less clear" — is any broad lift of the board; immersion is
carried by the beat bars, attack line, lit fret lines, labels and string toning instead.
Span-to-string distance is a mild consideration judged on a rendered sheet (a box frame around
a head reads as a frame, never as a head), not a number: Fire's gold arpeggio sits 10.5 CIEDE2000
from its orange string and reads fine.

*Colorblind set 2026-10-13 (user):* color-vision deficiency is not a criterion for the themes
above; instead one built-in per type, named "Colorblind - Protanopia", "- Deuteranopia",
"- Tritanopia" and "- Achromatopsia", each Default's furniture with its own strings and span
pair. Each palette keeps every string within 25 degrees of its standard hue (yellow light enough
to stay yellow, orange to stay orange, string 8 grey) and maximizes CIEDE2000 between strings 1-6
as that viewer sees them (Machado 2009 full-severity simulation; achromatopsia as luminance
alone, its strings an even lightness ladder from L* 45 to 97, neighbours at least two steps
apart). Strings 1-6 as seen, worst pair against Default's: protanopia 15.4 (1.9), deuteranopia
15.9 (6.1), tritanopia 16.0 (14.0), achromatopsia 4.6 (0.4, the ceiling for eight colors told by
lightness alone). Both span colors stay at L* 62 or above, since a frame draws at half alpha.

Later work, outside this plan:
- the theme editor, with export and import (54-Q12's todo plan);
- backdrop art and animation for the "night drive" and arcade-vector themes (COULD tier).

## Final acceptance

From the repository root, as separate invocations:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
pre-commit run --all-files
```

clang-tidy only when the user asks for it.

## Rollback and abort notes

- **Phase 1** is a pinned extraction; one revert. The risk is a normalized alpha byte, which is why
  the test pins every role including alpha.
- **Phase 2a** creates the one external compatibility surface — role names and the manifest shape.
  Formats change in place, so a later rename invalidates user files by design; the themes folder
  is user data, and dropping the feature leaves built-ins working.
- **Phase 1b** changes the default's values and two textures; one revert restores today's look.
  The private theme lives outside the repository and is untouched by any revert.
- **Phase 3** adds only a fixture and a sighting.
- **Phases 2b and 4** are each revertible per commit; Phase 4a's device sits behind a sighting.
- **Phase 5** deletes the old art from the tree. It remains in git history and in the private
  theme; a revert restores it.
- **Phase 6** is data; the sign-off exists because a built-in is a public statement of the look.

## Documentation impact

- `docs/plans/roadmap/00-roadmap.md` — item 26a and the 54 decisions block (this revision).
- `docs/developer/the-3d-highway.md` — the sharing table (:174-192) gains a theme row; the
  textured-element fan-out (:699-709) gains the theme-directory contract step. Updated in the phase
  that changes them.
- `docs/plans/roadmap/45-editor-theme-and-string-colors.md` — palette injection lands in 54
  Phase 1; its Phase 3 palette setting defaults to "Theme's choice" and names its source
  (54-Q18); its registry holds theme palettes plus the code-owned accessibility palettes, and
  `charter-classic` moves into the default theme's file at Phase 2a; 54-Q13's shade retune if
  ruled; its Phase 5a cap raise now breaks themes' `strings` arrays rather than user palette files.
- `docs/plans/roadmap/58-user-fretboard-art.md` — the theme directory is its home.
- `docs/plans/roadmap/56-head-atlas-mipmapping.md` — landed; a theme's heads get the mip chain
  from whatever the contract admits, and the contract owes it no edge-convention check.
- `docs/plans/roadmap/25-note-highway-3d.md` — new highway colors enter the theme, never new
  file-scope constants.
