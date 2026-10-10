# Plan 54 — Highway Visual Theming

**Status**: Revised 2026-10-07 against `master @ 6f87951f`; re-verified and re-sequenced
2026-10-09 against `602d5504`, then re-sequenced again the same day so the default look is
sighted before theme loading exists, and 54-Q18/Q19 recast as theme choices the player can pin.
Not started. **Phase 0 has a direction for the default look**
(below) and still gates the art phases (54-Q5 to 54-Q20, each with a recommendation or a ruling).
Phases 1 and 2a need only the role names, which this plan fixes. The engineering is ordered so the
new default look can be sighted in both products as early as possible ("Path to the first in-app
sighting"). **Phases 1 to 5 gate the first public release.** This
revision replaces the 2026-08-02 plan (baseline `182faedb`): its mechanism survives, but its
inventory was two months stale, and its goal changed from "make today's look one selectable
theme" to "make the look data, and ship RockHero's own looks in place of today's". Today's look
survives only as the user's private theme (54-Q10).

## Goal

The 3D highway's look becomes data: a **theme** of 23 color roles plus three validated texture
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
| **The theme** (`theme.json` plus its assets) | The 23 color roles (backdrop, board chrome, hand window, fret numbers, chord box and attack line, picking-hand furniture, both hands' strike pops, full-mute mark, arpeggio, capo, inlay tint and opacity, label text); the head **material** (54-Q19); the repeat-box art (`chords.png`); and its **choices** of string palette, head shape and inlay (54-Q18, 54-Q19), which the player may pin |
| **The player** (settings, per product) | One pinnable setting each for string palette, head shape and inlay, defaulting to "Theme's choice"; string orientation (54-Q5); display options; which theme is active |
| **RockHero, fixed** (code and one fixed sheet) | The technique marks (S3); the chord device and open-bar posts (S1, S2); every geometry, camera, timing, tail, light and emphasis law; shaders; the atlas layout and cell meanings; the palette, shape and inlay libraries' contracts |

**Theme's choice, or pinned.** String palette, head shape and inlay each resolve by one rule: the
player's pick if they made one, otherwise the active theme's choice. Each is one setting whose
default entry is "Theme's choice"; picking any concrete entry pins it, and picking "Theme's
choice" unpins it. There is no separate pin flag that could disagree with the value. The resolved
palette is computed once and passed to both the 3D highway and the 2D lane.

A theme cannot add elements: a backdrop is one flat color, and backdrop art, animation and bloom
are out of v1 (COULD tier).

## Non-goals

- Everything in the SHOULD NOT and COULD tiers below: geometry (`HighwayMetrics`), camera, timing,
  tail and light laws, the emphasis alphas, the atlas layout and what each cell means, shaders,
  fonts, backdrop art, inlay patterns (plan 58), the 2D lane's own chrome.
- **The string-palette registry.** Owned by
  `docs/plans/roadmap/45-editor-theme-and-string-colors.md`. A theme chooses a palette, or ships
  one into that registry; a palette the player pinned always wins (54-Q18).
- **String orientation.** A player setting, never a theme key (54-Q5).
- **Editor chrome (`EditorTheme`).** Plan 45 Phase 2; independent of the highway theme.
- A marketplace or any network delivery. Packaged export/import moves to `docs/plans/todo/`
  (54-Q12).

## Constraints

Applicable subset of the roadmap's non-negotiable block (`docs/plans/roadmap/00-roadmap.md`):
(a) layering — the theme type, registry, contracts and loader live in `rock-hero-common/ui`, only
persistence and the picker are per product; (b) public-header minimalism; (h) builds through
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

### Color roles (MUST and SHOULD) — the theme's 23 keys

A role's name is the theme file's key, so names describe what the color does, not where it is
drawn. "Alpha read" records whether the constant's own alpha byte reaches the GPU; a role whose
alpha is overwritten in code is stored opaque.

| Role | Today | Value | Alpha read | Tier |
|---|---|---|---|---|
| `backdrop` | `g_backdrop_color` :64 (packed RGBA, converted at the `bgfx::setViewClear` call :2764) | 0x000000ff | n/a | SHOULD |
| `beat_bar` | `g_beat_bar_color` :67 | 0xFF0F3B5E | yes | MUST |
| `attack_line` | drawn in `g_chord_box_color` today: downbeat line :5849, fret-span lines :4867, keyframe lines :4904 | (chord box) | — | MUST — split from `chord_box` |
| `lit_lane` | `g_lit_lane_color` :87 | 0x402590E8 | yes (:5785) | MUST |
| `lit_lane_dotted` | `g_lit_lane_dotted_color` :88 | 0x40185C94 | yes | MUST |
| `lane_border` | `g_lane_border_color` :195, forced opaque at :709 and :5638 | 0x0007928F | **no** — store opaque, drop the two `\| 0xFF000000U` | MUST |
| `fret_line` | `g_fret_inactive_color` :196 | 0xFF202020 | yes | MUST |
| `fret_line_lit` | `g_fret_active_color` :197 | 0xFFC0C0C0 | yes | MUST |
| `fret_number_lit` | `g_fret_number_active_color` :202 | 0xFF87DDF6 | yes | MUST |
| `fret_number_dim` | `g_fret_number_dim_color` :203 | 0x8007928F | yes | MUST |
| `fret_number_target` | `g_fret_number_fhp_color` :204; also node series :3727, targets :3811, pinned numbers :3924 | 0xFFFFA821 | yes | MUST — renamed: it marks every hand-position target |
| `strike_fretting` | `g_hit_glow_color` :227 via `HandLightStyle::mark_color` :706 | 0xFFFFB040 | yes | SHOULD |
| `strike_picking` | literal :713 | 0xFFFFFFFF | yes | SHOULD |
| `chord_box` | `g_chord_box_color` :417; palm mark :3325 | 0xFF00D2D5 | yes | MUST |
| `chord_box_dark` | `g_chord_box_dark_color` :418 | 0xFF003C3D | yes | MUST |
| `pick_furniture` | `g_picking_furniture_white` :680 | 0xFFC0C7D8 | yes | SHOULD |
| `full_mute_mark` | `g_full_mute_mark_color` :435 | 0xFF52798A | yes | SHOULD |
| `arpeggio` | `g_arpeggio_color` :445, used only at :5950 | 0xFFC040FF | yes | MUST |
| `capo_shade` | `g_capo_dead_zone_color` :210 | 0xB4000000 | yes (:6141) | SHOULD |
| `capo_bar` | `g_capo_bar_color` :211 | 0xFFB9BEC6 | yes | SHOULD |
| `capo_bar_rim` | `g_capo_bar_rim_color` :212 | 0xFF14161A | yes | SHOULD |
| `inlay_tint` | literal white vertex color :6074 (`fs_texture.sc:13` multiplies it) | 0xFFFFFFFF | yes | SHOULD |
| `label_text` | section labels, literal white at 0.85 :6240 | 0xFFFFFFFF | yes | SHOULD |

**Derived in code, never roles** — each is an intended coupling, so it is computed rather than
authored twice: the picking box dark (`hueless(chord_box_dark)` :715), the picking rail alpha
(0xBA :681), the tap-light lean (0.3 toward `fret_number_target`, :194, :5770-5775), and every
tier, frame and fill alpha (frame 128/255 :423, fill 32/255 :1591, ribbon tiers :5637-5646,
attack 0.85 :74). The comment at :670-679 records that `g_picking_furniture_white` was derived
offline from the teal and must be re-derived by hand whenever the teal moves — a rule kept in two
places. As a role checked by the guardrails below, that hand-kept coupling disappears.

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
- **String palette hues and their derivation** as color roles (plan 45; a theme chooses or ships
  a whole registry palette instead, 54-Q18) and
  **display options** (`HighwayDisplayOptions`:
  mirror, orientation, lane minimum).

### COULD — out of v1

Glyph font (`highway_atlas.cpp:163-165`; a `font.ttf` in the theme directory later); backdrop art
(`makeHighwayBackgroundWorldToClip`, `highway_camera.cpp:383`, and the three `background_*`
metrics are defined but never called — a dormant seam); inlay patterns (plan 58); tail slope-shade
depth; the rolling flip as a player option.

### Schema guardrails

Four notation distinctions are carried by hue alone. The loader enforces them with one
`distinct(a, b)` check (a minimum CIELAB ΔE, value fixed in Phase 2a by sighting), so a theme
cannot collapse a distinction:

- `chord_box` vs `pick_furniture` — fretting vs tapped chord box (:664-668);
- `arpeggio` vs `lane_border` — arpeggio vs held-shape rails (:5944-5951);
- `fret_number_lit`, `fret_number_dim`, `fret_number_target` — pairwise;
- `strike_fretting` vs `strike_picking` — the two hands' pops;
- plus one order check: luma(`fret_line_lit`) exceeds luma(`fret_line`) by a margin, since the hand
  window is read from it.

Texture contracts: the two existing measurements (`measureHeadArtProfile`,
`measureBoxMuteProfiles`) plus a grid-dimension check per asset.

## Current state (verified at `6f87951f`)

Re-verified at `602d5504`: every color constant and its value is unchanged. Renderer lines after
:5808 sit two lower than cited here, the 2D lane's palette fetch is at `tab_paint_core.cpp:2352`,
and the 2D fixed colors are still at :37-42.

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
  palette always wins over the theme's choice (54-Q2 A, recast by 54-Q18 so a theme may also
  ship a palette); themes are found by scanning a folder (54-Q3 A); two built-in themes
  (54-Q4 B).
- **Changed**: "built-in themes are complete" now means a complete theme directory; 54-Q1 C
  (inheritance from any theme) narrows if 54-Q11 is taken; the byte-identical extraction holds for
  Phase 1 only, since the product goal is a new look; the 2D non-goal narrows to the 2D lane's own
  chrome; the schedule risk is Phases 0 and 5 (design and art), not engineering.
- **Dropped**: a `constexpr` default theme as the lasting authority (Phase 1 scaffolding only —
  after Phase 5 the default theme's `theme.json` is the one authority), the 16-role list,
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
- **Harmonics (feeds 54-Q8):**
  - the lean is a natural-harmonic diamond with an inner line, or a string-colored diamond with a
    light inner border, plus a dark diamond printed on the ordinary head for the pinch;
  - the ring over today's diamond reads as a cross-hair, which suits a precisely placed node, and
    that quality is worth keeping in whatever replaces it;
  - the 2D lane keeps its current harmonic drawing for now.
- **Later themes:** a synthwave "night drive" look and an arcade-vector look with animated backdrops
  are wanted after v1 (a backdrop-art feature, COULD tier).
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
  - the open-bar L posts lose their foot and keep the upright leg;
  - the repeat X stays today's `chords.png` cells, tinted per theme.
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
  - **What it holds:** today's 23 colors, `notes.png` (heads only after the Phase 4b split),
    `inlays.png`, `chords.png`, and the Charter license notice.
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

  *Recommended: split.* Themes keep custom heads, and the head contract measures only heads. The
  cost is a second texture in the head pass, measured on `relwithdebinfo` in Phase 4b.
- **54-Q18 — string colors and the theme. RULED 2026-10-09 (user), recast the same day:** each
  theme chooses its string palette, and the player may pin one so it survives theme switches.
  This replaces the first ruling, a bounded `string_shading` over the user's palette: a theme that
  wants a variation of the usual colors ships that variation as its palette, so shading had
  nothing left to do and is deleted.
  - **The key:** an optional `string_palette` in `theme.json` naming a registry palette id, or a
    palette file inside the theme directory in the user-palette format (Phase 2b), registered into
    plan 45's one registry. Absent, the theme takes the default palette.
  - **The setting:** one per product, defaulting to "Theme's choice"; picking a concrete palette
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
  hand window at a fixed 1/8 of `lane_border`. That is a renderer numeric, not a theme value. A
  saturated accent still glows at 1/8, so the lines cannot fade with distance as today's teal
  does. The vector construction therefore mutes its ribbons to Oklab chroma about 9.
  - **Option A:** keep 1/8, and the muted ribbons.
  - **Option B:** lower the tier to about 1/16, which lets a fully saturated accent (chroma about
    24) fade like today's. Because it is not a theme value, B also dims today's look in the
    private theme.

  *Recommended:* decide at the first sighting with both in motion. If B wins, it stays one
  renderer constant, not a theme key, because the fade is a light law.

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
the default's values stay in code until Phase 5 in either order, and both products share the one
renderer, so every step shows in both. The cost, accepted by the user on 2026-10-09: today's look
cannot be selected in-app until Phase 2a lands, and comparing the two looks means two builds.

1. **Phase 1:** every color is a theme read, with zero visual change, proven by a throwaway local
   build with garish values.
2. **Phase 1b, the switch:** today's look is saved as files outside the tree; the default takes
   the ice-blue values, the rectangle head art and RockHero's circle inlay. **This is the first
   sighting**, in both products, with today's marks and repeat cells. 54-Q20, the inlay's opacity
   and whether ice blue reads too close to today's look are decided there from local variants.
3. **Phase 4a:** the box chord device and posts in code. With it the default look is complete and
   sighted again.

Phases 1b and 4a may run in either order; 1b first puts the colors in front of the user soonest.

Theme directories and the private theme loading in-app (Phases 2a and 3), pickers, inheritance and
user palette files (Phase 2b), the 2D theme reads and the mark-sheet split (Phase 4b), the shape
and inlay libraries and the new marks (Phase 5), and the remaining built-ins (Phase 6) follow.

### Phase 0 — Design brief and decisions

Phase 0 produces:
- the written head brief and mark brief (54-Q8);
- chord-device candidate stills (54-Q7);
- the accent family (54-Q9);
- several rendered candidates for every redesigned item (heads, marks, chord device), made through
  the `texture-author` agent, so each settles by comparison rather than on paper;
- the user's rulings on 54-Q5 to Q20.

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
renderer up. **Coverage, before any loader exists:** a throwaway local build gives every role a
distinct, garish value and is sighted once in both products; any element that keeps its old color
reads a constant, not the theme. It is never committed. **Verification**: configure (new
sources), build, touched tests.

### Phase 1b — Today's look saved; the default becomes the new look

- **Save today's look (outside the repository).** Write a theme directory into the per-user
  themes folder Phase 2a will scan, in Phase 2a's format:
  - a `theme.json` holding today's 23 values, read from Phase 1's pinned test values;
  - today's `notes.png`, `inlays.png` and `chords.png`;
  - a copy of `textures/LICENSE.txt`, whose notice travels with the Charter-adapted art.

  It is never committed, shipped or posted (54-Q10), and the user keeps a private backup. It
  cannot load until Phase 2a; the commit before this switch is the second copy.
- **The switch.** The compiled default takes the ice-blue values (54-Q9; the scratch look file
  already uses the 23 role names), and the pinned test values move with them. The default palette
  is unchanged.
- **The interim art**, baked by the `texture-author` agent at true size:
  - `notes.png` takes the rectangle head in the default's material, in today's atlas layout, and
    keeps today's mark cells until the Phase 4b split; the head contract measures the new cells;
  - `inlays.png` takes RockHero's own circle dot (54-Q15), so `textures/LICENSE.txt` narrows to
    the mark cells. Its generator is committed.

  No release ships before Phase 5, and the tree already carries those mark cells.
- **STOP: the first sighting**, in motion in both products. Decided there from local variants:
  54-Q20, the inlay's opacity (`inlay_tint`'s alpha), and whether ice blue reads too close to
  today's look beside the other accents.

**Verification**: build, touched tests.

### Phase 2a — Theme directories, contracts and selection by setting

- A theme is a directory: `theme.json` (`id`, `display_name`, optional `inherits`, optional
  `string_palette` naming a registry palette id per 54-Q18, `colors` keyed by role) plus any of
  the three PNGs.
- **One loader** for built-in and user themes. Built-ins deploy as directories under
  `resources/themes/<id>/` through `cmake/RockHeroProductResources.cmake` and fail loudly; user
  themes are scanned from per-user app data and dropped with a logged typed error.
- Contract validation as one entry point: grid dimensions per asset plus the two existing
  measurements, each error naming the asset and the violated expectation (including
  `AlphaBearingImage`, the likeliest user mistake). The guardrails above as `distinct()` and the
  luma order.
- **The resolved palette** (54-Q18): the player's stored palette id if there is one (plan 45
  Phase 3's setting, whose absent value now means "Theme's choice"), otherwise the active theme's
  `string_palette`, otherwise the default palette. It is resolved once and injected into both
  surfaces through Phase 1's seam.
- The registry and a theme id in `IEditorSettings`/`IGameSettings`. An unknown id falls back to the
  default. Selection at this phase is the stored id alone, with no picker.
- A theme switch recreates the renderer through the existing bring-up path, so a bad user atlas
  leaves the preview usable with the device intact.

**Tests**: `test_highway_texture_contract.cpp`, `test_highway_theme_file.cpp` over fixture
directories — full, colors-only, assets-only; malformed JSON, unknown role key, missing id,
contract-violating asset, every guardrail violation, an unknown `string_palette` id; each failure
drops only the offender and leaves the registry usable. Palette resolution: pinned, theme's
choice, and neither. Settings round-trip. **Verification**:
configure, build, touched tests.

### Phase 2b — Pickers, inheritance and user palettes

- Inheritance per 54-Q11. A user theme whose id matches a built-in shadows it, logged.
- The editor View submenu (mirroring plan 45 Phase 3's shape) and the game picker per 54-Q16.
- A color-only switch calls `setTheme` without recreating the renderer.
- User string-palette files (the schema absorbed from plan 45's withdrawn Phase 6) register into
  plan 45's restored registry; a user file can never claim `colorblind_safe`, and an extended tier
  of the wrong length is dropped with the expected count in the error. A theme directory may carry
  one such file as its own palette (54-Q18), registered through the same path; built-in themes
  ship theirs the same way.
- **The palette picker** in both products, per 54-Q18: `Theme's choice (<resolved palette>)`, a
  separator, then the registry's palettes; a concrete pick pins, the top entry unpins. The game's
  theme preview draws with the player's pins applied.

**Tests**: inheriting and unknown-base fixtures in `test_highway_theme_file.cpp`; a theme carrying
its own palette file; controller state for the theme and palette pickers, including that a theme
switch leaves a pinned palette alone and moves an unpinned one. **Verification**: build, touched
tests.

### Phase 3 — Today's look loads as the private theme; the coverage fixture

- **Verify the private theme.** Phase 1b's saved directory now loads. Select it in both products
  and A/B it in motion against a build of the commit before Phase 1b's switch. It must match
  exactly, apart from the code-drawn parts Phase 4a already replaced (54-Q10) and 54-Q20's
  ribbon tier if it changed.
- **Coverage fixture.** Phase 1's garish values become a test fixture theme, sighted once more
  through the loader in both products, and kept as a fixture for the loader tests.

**STOP: the private theme sighted in motion in both products; user sign-off.**
**Verification**: build, touched tests.

### Phase 4a — The chord device in code

- **S1 and S2 per 54-Q7**: the box device, prototyped in motion, and the user settles it. Any
  losing prototype is deleted before commit.

**STOP: sighting in motion in both products**; with Phase 1b landed, this is the complete default
look. **Verification**: build, touched tests.

### Phase 4b — One authority for the 2D lane; the mark sheet splits

- **The 2D paint core** reads `chord_box`, `arpeggio` and `full_mute_mark` from the active theme
  instead of `tab_paint_core.cpp:37-42`, so a distinction both surfaces draw has one authority.
- **S3's sheet split** per 54-Q17 moves today's mark cells unchanged, which is zero visual change,
  and measures the second texture's cost. The private theme's `notes.png` is re-cut locally to the
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
  shape, and `chords.png` if the device changed its repeat cells. Every generator is committed.
- **The default theme:** its `theme.json`, now a committed built-in.
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
  Phase 1; its Phase 3 palette setting defaults to "Theme's choice" (54-Q18); 54-Q13's shade
  retune if ruled.
- `docs/plans/roadmap/58-user-fretboard-art.md` — the theme directory is its home.
- `docs/plans/roadmap/56-head-atlas-mipmapping.md` — the atlas edge convention is stated by the
  Phase 2a contract.
- `docs/plans/roadmap/25-note-highway-3d.md` — new highway colors enter the theme, never new
  file-scope constants.
