# Plan 54 — Highway Visual Theming

**Status**: Revised 2026-10-07 against `master @ 6f87951f`. Not started. **Phase 0 is
decision-gated** (54-Q5 to 54-Q19 below, each with a recommendation or a ruling). Phases 1 and 2 need only
the role names, which this plan fixes. **Phases 1 to 5 gate the first public release.** This
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

## Non-goals

- Everything in the SHOULD NOT and COULD tiers below: geometry (`HighwayMetrics`), camera, timing,
  tail and light laws, the emphasis alphas, the atlas layout and what each cell means, shaders,
  fonts, backdrop art, inlay patterns (plan 58), the 2D lane's own chrome.
- **String palettes.** Owned by `docs/plans/roadmap/45-editor-theme-and-string-colors.md` and
  configured by the user separately from the theme; a theme may suggest a palette id and never
  override the user's choice (decision 7). A theme may only shade the chosen palette within a
  bound, never replace a hue (54-Q18).
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
no renderer change, only new art and the contracts in Phase 2.

| Asset | Today | Tier |
|---|---|---|
| `notes.png` head cells 0, 1, 2, 4, 5 (`highway_atlas.h:134-140, 262-271`) | 256x320 RGB, 4x5 grid of 64 px cells; adapted from Charter (`textures/LICENSE.txt`) | MUST — new art from a written brief (54-Q6) |
| `notes.png` mark cells 8-19 (`highway_atlas.h:153-248`): arpeggio brackets, legato triangle, bend caret, wide palm X, compact mute X, ring harmonic, ellipse pinch, slap, pop, V tap, pick slide | same file | Not themed — each redrawn once and moved to a fixed sheet (S3, 54-Q8, 54-Q17) |
| `inlays.png` | 256x256 RGBA single dot (`053814d9`); adapted from Charter | SHOULD — own dot; retires `LICENSE.txt` |
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
- **String palette hues and their derivation** (plan 45; the bounded shading is 54-Q18) and
  **display options** (`HighwayDisplayOptions`:
  mirror, orientation, lane minimum).

### COULD — out of v1

Glyph font (`highway_atlas.cpp:163-165`; a `font.ttf` in the theme directory later); backdrop art
(`makeHighwayBackgroundWorldToClip`, `highway_camera.cpp:383`, and the three `background_*`
metrics are defined but never called — a dormant seam); inlay patterns (plan 58); tail slope-shade
depth; the rolling flip as a player option.

### Schema guardrails

Four notation distinctions are carried by hue alone. The loader enforces them with one
`distinct(a, b)` check (a minimum CIELAB ΔE, value fixed in Phase 2 by sighting), so a theme
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
  not themeable (now the tiers above); the user's palette choice always wins over a theme's
  suggestion (54-Q2 A); themes are found by scanning a folder (54-Q3 A); two built-in themes
  (54-Q4 B).
- **Changed**: "built-in themes are complete" now means a complete theme directory; 54-Q1 C
  (inheritance from any theme) narrows if 54-Q11 is taken; the byte-identical extraction holds for
  Phase 1 only, since the product goal is a new look; the 2D non-goal narrows to the 2D lane's own
  chrome; the schedule risk is Phases 0 and 5 (design and art), not engineering.
- **Dropped**: a `constexpr` default theme as the lasting authority (Phase 1 scaffolding only —
  after Phase 5 the default theme's `theme.json` is the one authority), the 16-role list,
  `chord_name`, every `fingering.png` and 8x4-inlay sentence, and export/import as a roadmap phase.

## Open questions (Phase 0 gate)

Each carries a recommendation; none is decided until the user rules.

- **54-Q5 — orientation default.** Ship `invert_string_order = false` (lowest string at the
  bottom, as printed tablature) as the default, behind a sighting in both products, and make it
  the per-player setting `game.cpp:193` says is owed. *Recommended.* Never a theme key.
- **54-Q6 — head brief.** A solid rounded plate with one highlight band and no inset ring; a
  capsule; or a number-in-head. *Recommended: the plate* — it stays inside the rect-family
  contract, and a number-in-head would duplicate the floor numbers and need a 2D answer.
- **54-Q7 — chord device.** Settled by trying candidates, not by picking one on paper.
  - Every candidate drops the corner holders and the end-to-middle gradient.
  - The repeat boxes (S1) and the open-bar posts (S2) follow whichever device wins.
  - Phase 0 renders stills of each candidate; Phase 4 prototypes the shortlist in motion and the
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
- **54-Q9 — accent pairs.** The chrome hue family for each built-in, chosen clear of the six
  string hues; exact values by sighting.
- **54-Q10 — today's look after it leaves the tree. RULED 2026-10-08 (user):** it is kept as the
  user's private theme, loadable in both products, and never committed or distributed in any form,
  because the repository is public. Phase 3 exports it before any of its values leave the default.
  - **What it holds:** today's 23 colors, `notes.png` (heads only after the Phase 4 split),
    `inlays.png`, `chords.png`, and the Charter license notice.
  - **What it cannot hold:** the code-drawn parts (S1 chord-box holders and gradient bars, S2
    open-bar posts) and, from Phase 5, today's mark drawings on the fixed sheet (S3). It draws
    those with the universal RockHero designs. The user accepted this loss.
- **54-Q11 — inheritance scope.** `inherits` may name only a built-in theme id at v1. Deletes the
  cycle, two-pass-load, cascading-drop and depth-cap failure modes. *Recommended.*
- **54-Q12 — export/import.** Moves to `docs/plans/todo/highway-theme-packaging.md`; with 54-Q11 a
  copied theme folder is already self-contained apart from a built-in base every install has.
  *Recommended.*
- **54-Q13 — the default palette's shades.** An owned retune of each of the six hues, keeping the
  order. Belongs to plan 45 Phase 2. *Recommended: add it there.*
- **54-Q14 — proportions.** Whether `note_half_width` and related metrics move with the new head.
  *Recommended: the head brief decides*; the provenance comments are rewritten either way.
- **54-Q15 — the inlay dot.** Redraw it in the same bake and delete `textures/LICENSE.txt`.
  *Recommended.*
- **54-Q16 — game-side picker.** Plan 26's settings surface, or a launch flag at v1. *Recommended:
  plan 26.*
- **54-Q17 — where the marks live (S3).** Two options:
  - **Split the sheet.** The mark cells become a fixed `marks.png`, a fourth `HighwayTexture` that
    no theme directory may carry, beside a heads-only `notes.png` that themes may replace.
  - **One sheet, never themed.** `notes.png` keeps both heads and marks, and themes lose custom
    heads.

  *Recommended: split.* Themes keep custom heads, and the head contract measures only heads. The
  cost is a second texture in the head pass, measured on `relwithdebinfo` in Phase 4.
- **54-Q18 — string colors and the theme. RULED 2026-10-09 (user):** the base string colors are
  the user's setting, separate from the theme. A theme may adjust their general shading so the
  strings blend with its chrome, but never change a string to a different color.
  - **The key:** one optional `string_shading` in `theme.json`, applied over whichever palette the
    user chose, never six replacement colors.
  - **The bound:** lightness and chroma move within a cap, and hue moves only within a small
    tolerance. All three are fixed in Phase 2 by sighting the Phase 0 looks, which were authored
    as full per-string shades and are re-expressed as shading in Phase 2.
  - **The guardrails:** the string order and the palette's distinctness checks hold after shading.
- **54-Q19 — head shape and the theme. Direction 2026-10-09 (user), mechanism open:** the head
  shape becomes a player setting picked from a library, chosen independently of the theme just as
  the string colors are; the theme supplies only the style (material) of the head art.
  - **Library candidates:** rectangle, chamfered plate, stretched hexagon, flat emerald cut and
    stadium, from Phase 0 sighting. Asymmetric shapes were rejected.
  - **Recommended mechanism:** a load-time bake into the existing atlas. Each library shape is a
    structural outline (coverage, edge distance, inner-line mask) and the theme supplies a
    material profile (bands, rim, lift, core, line). One bake composes them, so any shape pairs
    with any theme and no theme ships per-shape cells. The `notes.png` head-cell asset above then
    becomes a material description, and the head contract is measured per library shape.
  - **Marks:** they are notation, so one mark set must fit every library shape (S3, 54-Q8).

## Phased implementation

Dependencies: 1 ← 0 (role names); 2 ← 1; 3 ← 2 and 54-Q9; 4 ← 0, 1; 5 ← 3, 4; 6 ← 5.

**Release gate.** Phases 1 to 5 gate the first public release
(`docs/plans/in-progress/first-releasable-editor.md`): no tagged release ships today's chrome
values, the Charter-adapted art or the holder chord box. Phase 6 does not gate it.

The order puts a distinct look first and swappable themes close behind it. Theme directories
(Phase 2) still come before the first visible change, because today's look must already load as a
private user theme before its values leave the default (Phase 3).

### Phase 0 — Design brief and decisions

Phase 0 produces:
- the written head brief and mark brief (54-Q8);
- chord-device candidate stills (54-Q7);
- two accent pairs (54-Q9);
- several rendered candidates for every redesigned item (heads, marks, chord device), made through
  the `texture-author` agent, so each settles by comparison rather than on paper;
- the user's rulings on 54-Q5 to Q17.

Phase 0 runs in parallel with Phases 1 and 2. **STOP: the user rules 54-Q9 before Phase 3, and
signs off on the brief before Phase 4 or 5 begins.**

### Phase 1 — Color roles and palette injection

- New feature folder `highway_theme/` in `rock-hero-common/ui` (public header plus source):
  `HighwayTheme` with the 23 roles, each member's Doxygen naming what draws with it.
- `HighwayRenderer::create(shaders, textures, theme)` and `setTheme(HighwayTheme)`; every `g_*`
  color read becomes a theme read in one mechanical pass. `attack_line` splits from `chord_box`;
  `lane_border` is stored opaque; `pick_furniture` replaces the offline-derived white; the
  derived colors listed above are computed from their roles.
- **Palette injection**: the two renderer fetches and `tab_paint_core.cpp:2385` take the palette as
  a parameter (plan 45 Phase 3's seam, landed here so a picker is never inert).
- The backdrop converts ARGB to bgfx's packed RGBA at the one `setViewClear` call.
- Both construction sites pass today's values. **Zero visual change.**

**Tests**: `test_highway_theme.cpp` pins all 23 roles' current values including alpha; the
backdrop conversion round-trips `0x000000ff`; the smoke and render-device tests still bring the
renderer up. **Verification**: configure (new sources), build, touched tests.

### Phase 2 — Theme directories, contracts and selection

- A theme is a directory: `theme.json` (`id`, `display_name`, optional `inherits`, optional
  `string_palette` suggestion, optional bounded `string_shading` per 54-Q18, `colors` keyed by
  role) plus any of the three PNGs.
- **One loader** for built-in and user themes. Built-ins deploy as directories under
  `resources/themes/<id>/` through `cmake/RockHeroProductResources.cmake` and fail loudly; user
  themes are scanned from per-user app data and dropped with a logged typed error.
- Contract validation as one entry point: grid dimensions per asset plus the two existing
  measurements, each error naming the asset and the violated expectation (including
  `AlphaBearingImage`, the likeliest user mistake). The guardrails above as `distinct()` and the
  luma order.
- Inheritance per 54-Q11. A user theme whose id matches a built-in shadows it, logged.
- Registry, `IEditorSettings`/`IGameSettings` theme id, the editor View submenu (mirroring plan 45
  Phase 3's shape), and the game picker per 54-Q16. An unknown id falls back to the default.
- A color-only switch calls `setTheme`; a texture switch recreates the renderer through the
  existing bring-up path, so a bad user atlas leaves the preview usable with the device intact.
- User string-palette files (the schema absorbed from plan 45's withdrawn Phase 6) register into
  plan 45's restored registry; a user file can never claim `colorblind_safe`, and an extended tier
  of the wrong length is dropped with the expected count in the error.

**Tests**: `test_highway_texture_contract.cpp`, `test_highway_theme_file.cpp` over fixture
directories — full, colors-only, assets-only, inheriting a built-in; malformed JSON, unknown role
key, missing id, unknown base, contract-violating asset, every guardrail violation; each failure
drops only the offender and leaves the registry usable. Settings round-trip and controller state.
**Verification**: configure, build, touched tests.

### Phase 3 — Today's look becomes a private theme; the default colors change

- **Export (outside the repository).** Write a user theme directory into the per-user themes
  folder Phase 2 scans:
  - a `theme.json` holding today's 23 values, read from Phase 1's pinned test values;
  - today's `notes.png`, `inlays.png` and `chords.png`;
  - a copy of `textures/LICENSE.txt`, whose notice travels with the Charter-adapted art.

  It is never committed, shipped or posted (54-Q10). The user keeps a private backup.
- **Verify.** Select it in both products and A/B it in motion against the built-in default, which
  still draws today's look. It must match exactly. A missed role shows up here, while the old
  values still exist to compare against.
- **Switch.** The built-in default's colors take 54-Q9's accent pair, and the pinned test values
  move with them. The textures stay in the tree until Phase 5, because the built-in default still
  draws them.

**STOP: the new chrome sighted in motion in both products; user sign-off.** **Verification**:
build, touched tests.

### Phase 4 — Redesign once in code

- **S1 and S2 per 54-Q7.** The shortlisted chord devices are prototyped in motion and the user
  settles one. The losing prototypes are deleted before commit.
- **The 2D paint core** reads `chord_box`, `arpeggio` and `full_mute_mark` from the active theme
  instead of `tab_paint_core.cpp:37-42`, so a distinction both surfaces draw has one authority.
- **S3's sheet split** per 54-Q17 moves today's mark cells unchanged, which is zero visual change,
  and measures the second texture's cost. The private theme's `notes.png` is re-cut locally to the
  heads-only layout so it still passes the contract.

**STOP: sighting in motion in both products.** **Verification**: build, touched tests.

### Phase 5 — The default theme's art lands

- Generator-baked `notes.png`, `chords.png` and `inlays.png` from the Phase 0 brief, plus the
  redrawn fixed mark sheet (S3), with the generator committed; the default `theme.json`.
- **STOP: the theme rendered in motion in the game and the editor preview; user sign-off before
  commit.**
- Then, in the same change set:
  - delete the Charter-adapted art from the tree; the private theme already holds it;
  - delete or shrink `textures/LICENSE.txt` (54-Q15);
  - rewrite the provenance comments;
  - update `docs/plans/in-progress/highway-note-art-state.md`'s provenance lines and the developer
    guide.

**Verification**: build, touched tests, and the local corpus smoke (local only) so no chart shape
breaks under the new asset set.

### Phase 6 — The second built-in theme

A second complete directory differing from the default in its accent pair and at least one
texture, so a role Phase 1 missed shows up as a wrong color rather than hiding in a one-theme
world. Passes the same contracts. **STOP: sighting and sign-off.**

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
- **Phase 2** creates the one external compatibility surface — role names and the manifest shape.
  Formats change in place, so a later rename invalidates user files by design; the themes folder
  is user data, and dropping the feature leaves built-ins working.
- **Phase 3** changes only the default's color values; one revert. The private theme lives outside
  the repository and is untouched by any revert.
- **Phase 4** is a code redesign behind a sighting; revertible per commit.
- **Phase 5** deletes the old art from the tree. It remains in git history and in the private
  theme; a revert restores it.
- **Phase 6** is data; the sign-off exists because a built-in is a public statement of the look.

## Documentation impact

- `docs/plans/roadmap/00-roadmap.md` — item 26a and the 54 decisions block (this revision).
- `docs/developer/the-3d-highway.md` — the sharing table (:174-192) gains a theme row; the
  textured-element fan-out (:699-709) gains the theme-directory contract step. Updated in the phase
  that changes them.
- `docs/plans/roadmap/45-editor-theme-and-string-colors.md` — palette injection lands in 54
  Phase 1; 54-Q13's shade retune if ruled.
- `docs/plans/roadmap/58-user-fretboard-art.md` — the theme directory is its home.
- `docs/plans/roadmap/56-head-atlas-mipmapping.md` — the atlas edge convention is stated by the
  Phase 2 contract.
- `docs/plans/roadmap/25-note-highway-3d.md` — new highway colors enter the theme, never new
  file-scope constants.
