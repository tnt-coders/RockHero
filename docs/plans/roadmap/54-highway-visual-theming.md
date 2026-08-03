# Plan 54 — Highway Visual Theming

**Status**: Not started; all four open questions answered 2026-08-02 (54-Q1: C, 54-Q2: A, 54-Q3: A
with B promoted to its own phase, 54-Q4: B) — **no decision gates remain, Phases 1–6 are
executable**. Date 2026-08-02; baseline `master @ 182faedb`, clean working tree. The inventory
below was first taken against `cffd8572` plus uncommitted box-mute hunks, then **re-verified
against `182faedb`** after that work landed across four commits — the color-constant block did not
move, the renderer API is unchanged, and the shaders still carry no color literals.

## Goal

The 3D highway's visual identity becomes data instead of compile-time constants and a fixed asset
set. A `HighwayTheme` value carries every non-functional color the renderer draws with; the texture
set becomes themed content behind a validated authoring contract; both products select a theme by
stable id and persist it; and a theme can be delivered as a file the user drops in, not only as a
built-in.

Two things this buys that nothing else does:

1. **Accessibility completeness.** `docs/plans/roadmap/45-editor-theme-and-string-colors.md`
   Phase 4 ships a colorblind-safe *string* palette, but string color is not the only information
   channel on the highway. The arpeggio color, the three fret-number states, the chord-box teal,
   and the hit glow are fixed hex constants today, and a colorblind-safe string palette rendered
   against a fixed purple arpeggio marker or cyan active fret number is half a solution. Those
   colors must move into the same selectable layer or the guarantee plan 45 makes is not one.
2. **Art/engine separation.** `chords.png` already demonstrates the pattern — art is the source of
   truth and the renderer measures it (`box_mute_profile.h` authoring contract). Generalizing that
   lets the look change without touching the renderer.

## Non-goals

- **Geometry and layout.** Lane widths, camera framing, fade distances, perspective, note-head
  world sizes — the 39 `constexpr float/double` constants in `highway_renderer.cpp` — stay fixed.
  They are the functional layer: they encode readability and timing legibility, they are tuned
  against real charts, and a theme that can move them can make the highway unplayable. Revisit
  only if a concrete need appears.
- **String-color palettes.** Owned by plan 45 (data in `common/ui` `string_colors/`). This plan
  consumes that registry and never redefines lane colors.
- **Editor chrome (`EditorTheme`).** Editor-private, owned by plan 45 Phase 2. A highway theme and
  an editor theme are independent selections.
- **Shader authoring.** Themes supply colors and textures, never shader source. The `.sc` programs
  stay repo-owned and compiled through `rock_hero_stage_highway_shaders`.
- **2D tab lane theming.** `docs/plans/roadmap/30-game-2d-tab-view.md` owns the strip renderer; it
  shares the string palette already. If it later wants themed non-string colors, it extends this
  plan's struct rather than growing its own.
- **A theme marketplace, discovery, or any network delivery.** Local files only.

## Constraints

Applicable subset of the roadmap's non-negotiable block (see `docs/plans/roadmap/00-roadmap.md`):

- (a) **Layering**: `common` never depends on editor or game code. The theme type, the registry,
  and the file loader all live in `rock-hero-common/ui` so both products share one implementation;
  only persistence and the picker UI are per-product.
- (b) **Public-header minimalism**: the theme struct is public (both products construct and pass
  it); the loader's internals are not. Ports-and-adapters per
  `docs/design/architectural-principles.md`.
- (h) **Builds**: everything through `.agents/rockhero-build.ps1`. Intermediate phases run only the
  checks their changes warrant; the final acceptance phase runs the sanctioned bundle as separate
  invocations.

Plan-specific hard rules:

- **No JUCE in the theme's public header.** Same rule plan 45 applies to the palette header, same
  reason: the game render stack consumes it without dragging `juce_graphics` in. Colors are
  `common::ui::ArgbColor`. The *loader* may use `juce_core` JSON and `juce::Image` decoding, which
  `common/ui` already depends on for `box_mute_profile.cpp`.
- **Hex-literal-only color construction** at static or namespace scope (plan 45 decision 2;
  `editor_theme.h:76–83` records the MSVC cross-TU init-order hazard).
- **A missing or invalid themed asset fails loudly.** Matches the decision already recorded in
  `highway_renderer.h:78–79` — required textures fail `HighwayRenderer::create` with a typed
  `TextureAssetInvalid` rather than falling back procedurally, because a missing asset means a
  broken install, not a degradable state. A *user theme file* is different: it is untrusted input,
  so it falls back to the built-in default with a logged warning and never crashes.
- **The theme file schema changes in place.** No version ladder, no migration path, no
  compatibility shims — consistent with the project's standing rule that formats change in place
  at this stage. A schema change invalidates existing theme files by design.
- Identifiers use US "color"; the JUCE API keeps "colour" at its boundary.

## Current state inventory

Verified against code 2026-08-02, `master @ cffd8572` plus the uncommitted box-mute work.

- **Renderer entry point** —
  `rock-hero-common/ui/include/rock_hero/common/ui/highway/highway_renderer.h`:
  `HighwayRenderer::create(const HighwayShaderSet&, const HighwayTextureSet&)` returning
  `std::expected<HighwayRenderer, HighwayRendererError>`; `setViewState(...)`;
  `draw(now, dt, width, height)`; bgfx behind `struct Impl; std::unique_ptr<Impl> m_impl`.
- **Texture seam already exists and is already the right shape.** `HighwayTextureSet` holds four
  `std::vector<std::byte>` PNG payloads (`note_atlas_png`, `inlay_atlas_png`, `fingering_png`,
  `chord_marks_png`). The renderer never touches the filesystem; each product resolves bytes its
  own way (`highway_shader_loader.cpp` / the editor preview resources). **Swapping texture content
  needs zero renderer change** — the work is in the loaders and in validating what arrives.
- **Shaders carry no baked colors.** All 15 `.sc` files under `rock-hero-common/ui/shaders/`
  contain no numeric `vec3`/`vec4` color literals; colors arrive as vertex colors and uniforms.
  Shader work is not on this plan's critical path.
- **The 15 hard-coded highway colors, which are not 15 of a kind.** All sit at file scope in the
  anonymous namespace of `rock-hero-common/ui/src/highway/highway_renderer.cpp` (4,745 lines):
  - **Fourteen `constexpr ArgbColor`**: `g_beat_bar_color` (47), `g_lit_lane_color` (60),
    `g_lit_lane_dotted_color` (61), `g_lane_border_color` (122), `g_fret_inactive_color` (123),
    `g_fret_active_color` (124), `g_fret_number_active_color` (129), `g_fret_number_dim_color`
    (130), `g_fret_number_fhp_color` (131), `g_hit_glow_color` (146), `g_chord_box_color` (254),
    `g_chord_box_dark_color` (255), `g_chord_name_color` (285), `g_arpeggio_color` (289).
  - **One `constexpr std::uint32_t`**: `g_backdrop_color` (44) `= 0x000000ff`. It is *not*
    `ArgbColor` and must not be treated as one — it is handed straight to `bgfx::setViewClear` as
    a packed RGBA clear value, a different channel order from the project's 0xAARRGGBB. Phase 1
    carries it in the theme as an `ArgbColor` like every other role and converts at the
    `setViewClear` call, so theme authors never meet two color formats.
  - Do not count by grepping `constexpr ArgbColor`: line 261 is
    `[[nodiscard]] constexpr ArgbColor frameFadeModulation()`, a *function*, and it inflated the
    count in this plan's first draft.
- **Alpha is encoded in several of these constants** and varied at use sites
  (`g_lane_border_color = 0x0007928F` with "alpha varies", `g_lit_lane_color = 0x402590E8`,
  `g_fret_number_dim_color = 0x8007928F`). The extraction must preserve each constant's alpha
  byte exactly, not normalize to opaque — several use sites re-pack alpha via `packAbgr(color,
  alpha)` and some OR in `0xFF000000U` explicitly. Treat every one as load-bearing; a normalized
  alpha is the failure mode that survives a casual visual check.
- **String palette** — `rock-hero-common/ui/include/rock_hero/common/ui/string_colors/
  string_color_palette.h`: `ArgbColor`, `StringColorPalette` (stable `id`, `standard[6]`,
  `extended[g_max_chart_strings - 6]`), `stringLaneColor(...)`, the Charter-exact
  `darkerColor`/`brighterColor`/`multiplyColor` chain, `StringLaneStyle` (seven derived surfaces),
  and `charterClassicPalette()`. **The multi-preset registry was deliberately collapsed** after
  plan 45 Phase 1 (see that plan's "Registry collapse" note) — `stringColorPalettes()`,
  `display_name`, and `colorblind_safe` were removed pending a real second preset and a picker.
  This plan restores them as part of Phase 2 rather than inventing a parallel registry.
- **Texture assets and their licensing** — `rock-hero-common/ui/resources/textures/`:
  `notes.png`, `fingering.png`, `inlays.png` adapted from Charter and redistributed under BSD
  3-Clause (`LICENSE.txt` covers exactly those three and must accompany source and binary
  distributions); `chords.png` is original Rock Hero art under the repository's own license. A
  themed asset pipeline must keep that notice correct — a shipped theme that replaces the Charter
  three still ships the notice while those files remain in the tree, and any new built-in theme's
  art needs its own provenance line.
- **Implicit atlas contracts, currently unvalidated as a format**: `notes.png` 4x4 grid with the
  channel scheme (texture R multiplies the tint color, G adds white highlight, B is the alpha
  mask — one atlas serves every string color); `inlays.png` 8x4 grid, one 256x512 cell per fret;
  `fingering.png` 4x4 grid; `chords.png` two stacked cells. `HighwayAtlas`
  (`src/highway/highway_atlas.h`) holds the grid math.
- **`chords.png` is measured, not merely sampled.** `box_mute_profile.h` defines the authoring
  contract (two equal stacked cells, palm mute above full mute, final display colors, straight
  alpha, arms corner to corner, stroke edge at half the glyph's peak alpha) and
  `measureBoxMuteProfiles` returns `std::expected<BoxMuteProfiles, BoxMuteProfileError>` with
  `UndecodableImage` / `UnanalyzableGlyph`. The renderer converts any failure into
  `TextureAssetInvalid`. **This is the exemplar to follow for every other themed asset**: a
  declared authoring contract, a measurement or validation step, and a typed failure.
- **Why box marks are measured rather than drawn** (relevant because it bounds what a theme may
  change): `pushChordBoxPanel` takes `x0..x1` from
  `highwayFretLineX(fret_low - 1)..highwayFretLineX(fret_high)` and `y1` from
  `string_grid_base_y + string_count * string_distance`, halved for a repeat box. Box aspect
  therefore varies with both the chord's fret span and the instrument's string count, and the X's
  arm angle varies with aspect — recorded in the comment at `highway_renderer.cpp:281` along with
  the measured finding that bitmap stretching distorts line weight.
- **Renderer recreation is an already-supported path.** `docs/developer/the-3d-highway.md`
  documents that bgfx cannot be re-initialized in-process, so the *device* is created once and
  survives window hides, while `bringUpRenderer()` deliberately keeps the device alive on renderer
  failure and retries only the renderer later. Live theme switching therefore recreates the
  renderer, never the device — no process restart, and the failure path already exists.
- **Per-product settings ports** — `IEditorSettings`
  (`rock-hero-editor/core/include/rock_hero/editor/core/settings/i_editor_settings.h`, the
  `tabMinimumDisplayedStrings` pattern) and the game's future `IGameSettings`
  (`docs/plans/roadmap/27-in-song-flow-results-profiles.md` Phase 1). Plan 45 decision 7: both
  products persist a preset id string from the same common registry; the ids match, the stores do
  not. This plan follows it exactly.

## Dependencies

- **Upstream (blocking)**:
  - `docs/plans/roadmap/45-editor-theme-and-string-colors.md` Phase 1 — **complete**; supplies
    `ArgbColor` and the palette types this plan's struct sits beside.
  - Plan 45 Phase 3 (string-color preset selection) for the editor selection chain this plan's
    picker mirrors. Phase 1 and Phase 2 here can execute before it; Phase 2's editor picker should
    land after 45 Phase 3 so both submenus share one shape rather than inventing two.
  - `docs/plans/roadmap/27-in-song-flow-results-profiles.md` Phase 1 (`IGameSettings`) for
    game-side persistence only. The editor half is unblocked.
- **Supersedes plan 45 Phase 6 entirely** (withdrawn there 2026-08-02, user direction; 45-Q3
  resolved A by consequence). That phase proposed two file schemas — one for `EditorTheme` values,
  one for string palettes — with palette loading "beside the registry in common/ui so the game can
  share it". Keeping it would mean two schemas and two loaders for one job. Disposition:
  - **String-palette files move here**, into Phase 4's scan as a sibling schema registering into
    plan 45's `stringColorPalettes()`. Plan 45 keeps ownership of the registry and of what a
    palette *means*; this plan owns only how one arrives from disk.
  - **`EditorTheme` chrome files are dropped**, not relocated — editor chrome is this plan's
    non-goal and plan 45 Phase 2's built-in presets cover the need. If they are ever wanted they
    follow this plan's loader pattern rather than reviving a second schema.
- **Downstream consumers**:
  - `docs/plans/roadmap/25-note-highway-3d.md` — Phases 4–5 add technique and feedback visuals;
    every new color they introduce should enter the theme struct rather than becoming a sixteenth
    file-scope constant. Coordinate if 25 is executing concurrently.
  - `docs/plans/roadmap/44-editor-3d-preview.md` — picks up theming with no work; it constructs
    the same renderer.
  - `docs/plans/roadmap/26-game-startup-menus-library.md` — game-side picker surface.

## Decisions already made

Restated with sources; do not re-litigate.

1. **Required assets fail loudly; user files fall back.** The distinction is deliberate: shipped
   content missing means a broken install (`highway_renderer.h:78–79`, user decision 2026-08-01,
   replacing procedural fallbacks that silently masked failures); a user theme file is untrusted
   input and gets typed-error handling with a fallback.
2. **Preset ids are stable strings, persisted per product, resolved through one common registry**
   (plan 45 decision 7).
3. **Art is the source of truth where the renderer measures it** (`box_mute_profile.h`). Themes do
   not get numeric overrides for anything derived from pixels — repaint the asset instead.
4. **Formats change in place.** No theme-file version field, no migration ladder.
5. **Geometry is not themeable** (this plan's non-goals). The 39 layout constants encode
   playability.
6. **Built-in themes are always complete; only user themes may be partial** (54-Q1 resolution
   detail). Every theme this repository ships declares all 15 colors and supplies all four
   atlases. This bounds the one real hazard partials introduce — a partial theme silently changes
   appearance when the theme it inherits from changes — to third-party files, and guarantees the
   registry always holds at least two fully-specified reference points. A shipped partial theme
   would make a routine default retune an invisible visual regression in content we own.
7. **The user's explicit string-palette selection always wins** over a theme file's preferred
   palette (54-Q2). A theme names a palette as a starting suggestion applied when the user has no
   stored choice; it can never override one. Silently retargeting an accessibility setting from a
   cosmetic file is exactly the class of behavior "no code that lies about intent" forbids.

## Open questions — ALL ANSWERED 2026-08-02

Mirror into `docs/plans/roadmap/00-roadmap.md` Decisions-needed as answered.

- **54-Q1 — theme scope at v1. ANSWERED: C** — colors + textures, with partial themes allowed.
  The user's stated intent is stronger than "omitted fields fall back to the built-in default":
  *"users should be able to start with a theme and then tweak just what they want without building
  a whole new theme."* That is inheritance from **any** registered theme, not only the default, so
  the manifest carries a named base (`inherits`). Phase 4 specifies the resolution rules and the
  failure modes this adds.
- **54-Q2 — one theme object or two. ANSWERED: A** — highway theme and string palette stay
  independent selections. A theme file may *name* a preferred palette id; the user's explicit
  palette choice always wins. String color is the accessibility control and must never be
  reachable only through a cosmetic one.
- **54-Q3 — import surface. ANSWERED: A now, B explicitly planned** — Phase 4 ships the
  themes-folder scan; the file-picker import is **Phase 6**, recorded as intended work rather than
  a maybe (user: "that seems cleaner"). Phase 6 is sequenced after the second built-in theme
  because the scan path must be proven by real content first.
- **54-Q4 — how many built-in themes ship. ANSWERED: B** — two. The registry collapse recorded in
  plan 45 is direct evidence that a single-entry registry with no production consumer gets deleted
  as dead scaffolding; a second theme is also the only way to discover anything Phase 1 failed to
  extract, since a missed constant still looks correct in a one-theme world.

## Phased implementation

Phase 1 is independent. Phase 2 needs 1. Phase 3 needs 1. Phase 4 needs 2 and 3. Phase 5 needs 4.
Phase 6 needs 4 only. No phase is decision-gated.

**Sequencing note (5 before 6, or 6 before 5).** Phase 5 is authoring work and will be the long
pole; Phase 6 is small, code-only, and carries the guarantee that makes shared themes
self-contained. Running 6 before 5 lands the complete mechanism sooner and shortens the window in
which the Phase 4 id-stability constraint applies; running 5 first proves the seam against real
content before the packaging path is built on top of it. Either order works — pick based on
whether the art is ready when Phase 4 lands.

### Phase 1 — `HighwayTheme` value struct

**Scope.** Turn the 15 file-scope color constants into a public value struct with semantic role
names, threaded through the renderer. Zero visual change.

- New feature folder `highway_theme/`:
  `rock-hero-common/ui/include/rock_hero/common/ui/highway_theme/highway_theme.h` and
  `src/highway_theme/highway_theme.cpp`. `HighwayTheme` carries one `ArgbColor` per role, with
  each member's Doxygen naming what draws with it. Roles keep the current constants' alpha bytes
  verbatim — several are non-opaque and load-bearing.
- Naming: semantic roles, not sites. `backdrop`, `beat_bar`, `lit_lane`, `lit_lane_dotted`,
  `lane_border`, `fret_inactive`, `fret_active`, `fret_number_active`, `fret_number_dim`,
  `fret_number_hand_position`, `hit_glow`, `chord_box`, `chord_box_dark`, `chord_name`,
  `arpeggio`. A role's *name* is the theme file's key, so it is a compatibility surface — pick
  names that survive a renderer refactor.
- `backdrop` is the one role whose storage format differs from its use: the theme holds
  `ArgbColor` like every other role, and the `bgfx::setViewClear` call site converts to bgfx's
  packed RGBA. Keep the conversion at that one call, not in the theme — a theme author who has to
  know two channel orders is a bug in the seam, not in their file.
- `defaultHighwayTheme()` returns today's values as `constexpr` data; the struct is default-
  constructible to it so nothing downstream must pass one to keep current behavior.
- Renderer: add a `HighwayTheme` parameter to `create(...)` and a `setTheme(HighwayTheme)` for
  live switching (colors are per-draw vertex data, so a color-only switch needs no GPU
  reallocation — this is exactly why colors and textures split across Phases 1 and 3). Replace
  every `g_*` read with a theme member read. This is the wide mechanical edit; do it in one pass
  so the diff is reviewable as a rename.
- Two call sites construct the renderer today (`RockHeroGame::onInit` and the editor's
  `bringUpRenderer()`); both pass `defaultHighwayTheme()` at this phase.

**Public-header impact.** New public header in `common/ui`. `highway_renderer.h` gains a
constructor parameter and one setter — both products recompile.

**Testing plan.** `rock-hero-common/ui/tests/test_highway_theme.cpp`: `defaultHighwayTheme()`
values match the constants they replace exactly, **including alpha bytes** (pin all 15 roles as
literal expectations — this is the byte-identity proof); role names are unique; the backdrop
ARGB→bgfx-RGBA conversion round-trips the current `0x000000ff` clear value exactly, since that one
is a channel reorder rather than a copy and a silent swap would render as a wrong clear color
nothing else would catch. Existing Noop-backend renderer tests (`test_render_device.cpp`) prove
the renderer still comes up.

**Exit criteria.** Highway renders identically in game and editor preview; the 15 constants no
longer exist in `highway_renderer.cpp`; no JUCE include in the new public header.

**Verification** (configure needed — new target sources):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Configure -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

### Phase 2 — Theme registry and per-product selection

**Scope.** Named built-in themes, selectable and persisted, plus restoration of plan 45's
collapsed palette registry so both live behind one shape.

- `highwayThemes()` returning built-in `HighwayThemePreset` entries (stable `id`, `display_name`,
  `HighwayTheme` value). Restore `stringColorPalettes()`, `display_name`, and `colorblind_safe` on
  `StringColorPalette` at the same time — this plan is the production consumer whose absence
  caused the collapse.
- Editor: `IEditorSettings::highwayThemePreset()/setHighwayThemePreset(std::string)` +
  `EditorSettings` + `NullEditorSettings`; controller handler + intent + `EditorViewState` field;
  View menu submenu with radio checkmarks, mirroring plan 45 Phase 3's shape. Unknown or missing
  ids fall back to the default, never an error.
- Preview applies a change by calling `setTheme(...)` and requesting a frame — no device work.
- Game: the same id persisted through `IGameSettings` when plan 27 Phase 1 lands; until then the
  game reads the default. Do not block this phase on it.

**Public-header impact.** Editor-core public headers gain members (product-internal). Common gains
the registry functions.

**Testing plan.** Settings round-trip and absent-default (`test_editor_settings.cpp`); controller
state test that selection updates view state and that an unknown id falls back
(`test_editor_controller_state.cpp`); registry test that every id is unique and non-empty. UI
wiring stays untested beyond compile per "Selective UI Wiring Tests".

**Exit criteria.** Selecting a theme in the View menu restyles the editor's 3D preview live and
survives restart.

**Verification.** Build, touched tests.

### Phase 3 — Textures as themed content with validated contracts

**Scope.** Make the texture set nameable and swappable, and turn the four implicit atlas contracts
into validated ones with typed failures.

- `HighwayTextureSet` stays the renderer's input — it is already correct. What changes is upstream:
  a `HighwayTextureContract` validation step in `common/ui` that checks each PNG's decoded
  dimensions against its declared grid before the renderer sees it, returning a typed error naming
  *which* asset and *which* expectation failed. Today an atlas of the wrong shape either renders
  garbage or fails deep inside `HighwayRenderer::create` with an undifferentiated
  `TextureAssetInvalid`.
- Document each contract where it is enforced, in the style `box_mute_profile.h` already sets:
  grid dimensions, cell size where fixed, channel semantics for the tinted atlas, and — for
  `chords.png` — a pointer to the existing measurement contract rather than a restatement.
- Both loaders gain a themed-asset path: resolve `<theme-dir>/<asset>.png` when the active theme
  provides one, else the built-in bytes. Keep the game's enum-driven and the editor's string-keyed
  loaders as they are; the asymmetry is recorded in `docs/developer/the-3d-highway.md` and is not
  this plan's to fix.
- Texture changes require renderer recreation (GPU upload at `create`). Route it through the
  existing bring-up path so the editor preview's device-survives-failure invariant is preserved —
  a theme with a bad atlas must leave the preview usable, exactly as a stale shader deploy does.

**Public-header impact.** New validation entry point in `common/ui`; `HighwayRendererErrorCode`
may gain differentiated codes.

**Testing plan.** `test_highway_texture_contract.cpp`: each contract accepts its shipped asset and
rejects wrong dimensions, wrong aspect, and undecodable bytes, with the error naming the asset.
Extend the existing box-mute profile tests only if the contract wording changes.

**Exit criteria.** A deliberately malformed atlas produces a typed error naming the asset and the
violated expectation; the editor preview survives it with the device intact; shipped assets pass
unchanged.

**Verification.** Build, touched tests.

### Phase 4 — Theme files

**Scope.** A theme is a directory: one `theme.json` manifest plus any subset of the asset PNGs.
A user string palette is a small separate JSON file in a sibling folder. Both load through one
implementation in `common/ui` so both products share it.

- Manifest: `id`, `display_name`, optional `inherits` (the id of the theme this one starts from),
  optional `string_palette` (a registry id this theme prefers), and an optional `colors` object
  keyed by the Phase 1 role names. Every field except `id` is optional.
- **Inheritance (54-Q1: C).** `inherits` names any registered theme — built-in or user — and
  absent means the default built-in. Resolution walks the chain and applies each level's overrides
  over its base, for **both colors and assets**: a theme inheriting `warm-dusk` gets warm-dusk's
  atlases unless it supplies its own. This is what makes "start from a theme and tweak one thing"
  work rather than only "tweak the default".
- Resolution order and its failure modes — all new with inheritance, all typed:
  - **Unknown base**: the named theme is not registered → drop this theme, log the missing id.
  - **Cycle**: A inherits B inherits A → drop every theme in the cycle, log the chain. Detect
    explicitly; do not rely on a depth cap to catch it.
  - **Depth cap**: bound the chain (suggest 8) so a long legitimate chain fails loudly rather than
    resolving slowly.
  - **Cascading drop**: a theme whose base was itself dropped is dropped too, logged with the
    root cause rather than a second "unknown base" that hides why.
  - Themes must therefore resolve in dependency order, not directory order. Registration is a
    two-pass load: parse all manifests, then resolve.
- Assets: any of `notes.png`, `inlays.png`, `fingering.png`, `chords.png` present in the directory
  overrides the resolved base. Each override runs Phase 3's contract validation.
- Discovery: scan a `themes/` folder under per-user app data at startup (54-Q3 A; packaged
  export/import is Phase 6). A user theme whose id collides with a built-in shadows it, logged.
- **User string-palette files (absorbed from withdrawn plan 45 Phase 6).** A sibling `palettes/`
  folder scanned by the same loader, carrying a separate and much smaller schema: `id`,
  `display_name`, `standard[6]`, `extended[]`. They register into plan 45's
  `stringColorPalettes()` and appear in the same picker as built-in palettes. Deliberately **not**
  folded into `theme.json`: 54-Q2 keeps palette selection independent of theme selection, and a
  palette defined inside a theme file would only be reachable by adopting that theme — collapsing
  an accessibility control into a cosmetic one through the back door. A theme's `string_palette`
  key may *name* a user palette; it still cannot define one, and still cannot override a stored
  user choice (decision 7).
  - `colorblind_safe` is never asserted for a user-authored palette. The flag is a claim this
    repository makes about presets it has run the Phase 4 CVD test against; a user file cannot set
    it true.
  - Extended-tier length must match `g_max_chart_strings - 6` exactly, or the palette is dropped
    with a typed error naming the expected count — the compiler-enforced coupling plan 45 Phase 1
    built for built-ins has no compile step for file input, so the check moves to load time.
- Failure handling: typed errors per "Typed Boundary Errors"; a malformed manifest, an unknown
  role key, a contract-violating asset, or any inheritance failure above drops that theme from the
  registry with a logged warning naming the file and reason. Never a crash, never a partially
  applied theme, and never a partially resolved inheritance chain.
- Provenance: the loader writes nothing and reads only the themes folder. Built-in theme art keeps
  its license line in `resources/textures/LICENSE.txt`; a user theme's licensing is the user's.

**Public-header impact.** Loader entry point public in `common/ui`; parsing internals private.

**Testing plan.** `test_highway_theme_file.cpp` over fixture directories. Well-formed cases: full
theme; colors-only; assets-only; a theme inheriting a built-in; a theme inheriting another user
theme; a three-level chain; a theme overriding only assets of its base; a theme naming a preferred
palette (and the assertion that a stored user palette choice still wins, per decision 7). Failure
cases: unknown role key, malformed JSON, missing `id`, contract-violating asset, id collision with
a built-in, unknown base, two-theme cycle, three-theme cycle, depth-cap overflow, and a theme
whose base was dropped for its own error. Every failure asserts the typed error, that the offender
is the only theme dropped (except cycles and cascades, where the assertion is the exact drop set),
and that the registry stays usable.

Palette files, in the same fixture harness: a well-formed palette registers and is selectable; a
wrong-length `extended` tier is dropped with the expected count in the error; a file claiming
`colorblind_safe` loads with the flag forced false rather than being rejected (the claim is
ignored, not fatal); a palette id colliding with a built-in shadows it, logged; a malformed
palette drops without affecting themes, and a malformed theme drops without affecting palettes —
the two schemas fail independently.

**Exit criteria.** A well-formed theme directory appears in the picker and applies live; a theme
inheriting another resolves both colors and assets correctly; a user palette appears alongside
built-in palettes and is selectable independently of the active theme; each malformed case logs,
drops exactly the affected entries, and leaves the rest working.

**Verification.** Build, touched tests.

### Phase 5 — A second built-in theme

**Scope.** Author one complete alternate theme — colors and the four atlases — proving the seam
end to end with a real consumer rather than fixtures.

- This is an art task, not an engineering one, and it is the schedule risk in this plan. The code
  is done at Phase 4; what remains is authoring note heads, a fretboard skin, a fingering panel,
  and the two mute marks that satisfy the contracts and read well in motion.
- The mute marks must satisfy `box_mute_profile.h`'s authoring contract, which is stricter than
  "an X" — measure the result rather than eyeballing it.
- New art needs a provenance line in `resources/textures/LICENSE.txt` (or its own notice) stating
  authorship, keeping the existing BSD 3-Clause notice for the Charter-derived three correct.
- **STOP — present both themes rendered in motion (game and editor preview) and get sign-off
  before commit.** Colors that read well in a screenshot can fail against scrolling notes.

**Public-header impact.** None — data and assets only.

**Testing plan.** Registry test extends to two themes with unique ids; both pass Phase 3's
contract validation.

**Exit criteria.** Both themes selectable, both render correctly in both products, user signed off
on the rendered result.

**Verification.** Build, touched tests, plus the local corpus smoke (local-only, never CI) to
confirm no chart shape breaks under the second asset set.

### Phase 6 — Theme export and import (54-Q3 B)

**Scope.** Packaged themes in both directions, so receiving one does not require finding a
per-user app-data folder by hand and sending one does not require the recipient to already own
whatever it was built from. Planned work, not a maybe — Phase 4's drop-in scan is the mechanism,
this is the ergonomics on top of it.

**The governing rule: partial themes are an authoring convenience, never a distribution format.**
Locally a theme may inherit — that is the whole point of 54-Q1's answer. The moment a theme
crosses a machine boundary it is complete. Export is what enforces that, and it is why export
exists at all rather than leaving users to zip a folder by hand.

- **Export flattens.** Picking a theme and exporting resolves its entire inheritance chain and
  writes one complete theme into the archive: all 15 colors, all four atlases, no `inherits` key.
  What the recipient sees is exactly what the author saw, frozen at export — the same reason
  decision 6 requires built-in themes to be complete.
- **Flattening removes the failure mode instead of handling it.** An exported theme cannot fail on
  a missing base, because it has no base. Unknown-base refusal survives in the import path only as
  the backstop for hand-authored archives, where it becomes rare rather than routine.
- **It also narrows the compatibility surface.** Because `inherits` never crosses a machine
  boundary through the supported path, a theme id is a contract between files on *one* machine.
  Renaming a shipped theme's id breaks local authoring files, not other people's themes. Keep
  shipped ids stable anyway — it is nearly free — but this is no longer a one-way door.
- **The size cost is accepted at v1.** A color-only tweak still exports all four atlases, and
  `inlays.png` alone is an 8x4 grid of 256x512 cells. If archive size ever draws a complaint, the
  escape hatch is to let an exported theme reference *built-in* theme ids (guaranteed present on
  every install, and already append-only) while still flattening everything user-authored. Do not
  build that until someone complains — it trades a guarantee for bytes.
- **Import validates before installing, not after.** Parse the manifest, resolve inheritance
  against the live registry, and run every Phase 3 asset contract on the archive's contents
  *before* anything is written to the themes folder. A theme that would be dropped at scan time
  must be refused at import with the same typed error — installing a file guaranteed to fail on
  next startup is the "no code that lies about intent" failure mode.
- Id collision with an installed theme prompts before overwriting; collision with a built-in is
  allowed (it shadows, per Phase 4) but says so.
- **Archive format**: `common/core` already carries JUCE-backed ZIP handling for package behavior,
  so both directions reuse proven machinery rather than inventing a transfer format.
- Per product: the editor gets File or View menu actions; the game surfaces them through
  `docs/plans/roadmap/26-game-startup-menus-library.md`'s settings. A "reveal themes folder"
  action is a cheap companion — add it if the platform seam is trivial, skip it if not.

**Public-header impact.** Export and import entry points are public in `common/ui` (both products
call them); archive handling stays private.

**Testing plan.** `test_highway_theme_package.cpp`. Export: a theme inheriting a built-in exports
complete and carries no `inherits` key; a three-level chain flattens to one theme; an exported
archive re-imports into a registry that has *none* of its ancestors and renders identically —
this round-trip through an ancestor-free registry is the assertion the whole design rests on.
Import: every Phase 4 failure class refuses with the same typed error **and leaves the themes
folder byte-unchanged** (a refused import must not partially write); a hand-authored archive with
an unknown base refuses naming the base; id collision with an installed theme reports rather than
silently overwriting.

**Exit criteria.** A theme built by inheriting another exports as a self-contained archive and
imports correctly on a machine that has never seen its ancestors; every archive that would be
dropped at scan time is refused at import with a message naming the reason; a refused import
leaves no trace on disk.

**Verification.** Build, touched tests.

## Final acceptance phase

After the last executed phase, run the sanctioned bundle as separate invocations from the repo
root, then commit per CLAUDE.md conventions:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets clang-tidy
pre-commit run --all-files
```

## Rollback/abort notes

- **Phase 1** is a mechanical extraction pinned byte-identical by literal-value tests; rollback is
  one revert. The risk is a dropped or normalized alpha byte, which is why the tests pin all 15
  including alpha rather than sampling.
- **Phase 2** is additive; a revert loses the setting, and `EditorSettings` tolerates unknown keys.
  Fallback-on-unknown-id means a removed theme never bricks startup.
- **Phase 3** hardens an existing failure path; the risk is over-strict validation rejecting a
  shipped asset, caught immediately by the acceptance test that the shipped set passes.
- **Phase 4** is the only phase creating an external compatibility surface. Once users author
  themes, renaming a role key or changing the manifest shape invalidates their files — and per
  this plan's constraints that is accepted rather than mitigated. The lever if it goes wrong is
  that the theme folder is user data: dropping the feature leaves built-ins working and orphans
  only files the user can delete. Inheritance widens that surface while Phase 4 stands alone:
  `inherits` makes a theme id a contract between *files*, and a hand-zipped partial theme carries
  that contract onto someone else's machine. **Phase 6's flatten-on-export closes this** — through
  the supported path a theme id never crosses a machine boundary, so the contract is local and
  renaming a shipped id costs only local authoring files. Between Phase 4 and Phase 6, treat
  shipped theme ids as append-only; after Phase 6 it is good hygiene rather than a one-way door.
- **Phase 5 is revertible as data**, but the sign-off STOP exists because a second theme is also
  a public statement about what the product looks like.
- **Phase 6 is not merely ergonomics** — export is what makes inheritance safe to share, so
  dropping it leaves 54-Q1's authoring convenience as a distribution hazard rather than a
  feature. It remains individually revertible (the drop-in folder keeps working), but reverting it
  reinstates the Phase 4 id-stability constraint above. Its one hazard is writing a broken theme
  to disk on a failed import, which the byte-unchanged assertion in its tests exists to prevent.

## Documentation impact

`docs/developer/the-3d-highway.md` names the texture and shader fan-out lists this plan changes —
its "silent steps" for adding a textured element gains the theme-override and contract-validation
steps, and the sharing-mechanics table gains a theme row. Update it in the same change set as the
phase that invalidates it, per the developer-guide maintenance rule.
