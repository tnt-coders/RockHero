# Plan 45 — Editor Theme and String Colors

**Status**: Ready (Phases 1–3 executable now; Phase 4 ships only after palette sign-off; Phase 5
is decision-gated); 2026-07-06; baseline `refactor @ 13e82fb0`.

## Goal

The editor gains real theme support on the existing EditorTheme seam: the user picks an editor
theme preset and a string-color preset from the View menu, both persist across sessions, and at
least one colorblind-safe string-color preset ships. The string-color palette definition (data
only) moves into `rock-hero-common` so the editor tab lane, the future editor 3D preview
(`docs/plans/44-editor-3d-preview.md`), and the game note highway
(`docs/plans/25-note-highway-3d.md`) draw from one source of truth. The plan also owns the
coordinated, decision-gated raise of `g_max_chart_strings` from 8 to the format's 10-string
target, because that raise is blocked on lane-color decisions that live here.

## Non-goals

- Game-side settings UI for preset selection — the registry this plan builds is product-neutral;
  the game persists its choice through `docs/plans/27-in-song-flow-results-profiles.md`'s
  IGameSettings and surfaces the picker via `docs/plans/26-game-startup-menus-library.md`.
- Any 3D rendering — consumers are `docs/plans/25-note-highway-3d.md` and
  `docs/plans/44-editor-3d-preview.md`.
- Note-detection support for 9–10 string instruments — raising the display/authoring cap does not
  promise detectability; see `docs/plans/22-note-detection.md` (its latency budget flags sub-B0
  fundamentals; coordinate before the Phase 5 gate closes).
- A light theme, unless the user asks for one; the phased mechanism makes adding presets pure
  data work later.
- Serialized colors in the chart or song format. The format carries no colors
  (`docs/in-progress/note-format-and-tablature-plan.md`, "String count, micro-bends, and forward
  extensions"): color is display configuration only.

## Constraints

Applicable subset of the roadmap's non-negotiable block (see `docs/plans/00-roadmap.md`):

- (a) **Layering**: common never depends on editor or game code; editor and game never depend on
  each other. Anything both products need is extracted to rock-hero-common FIRST — as its own
  phase with tests — before game code consumes it. That is exactly Phase 1 here.
- (b) **Public-header minimalism**: only headers that must be public are public; ports-and-adapters
  per `docs/design/architectural-principles.md` (Ports and Adapters; Placement Procedure for New
  Files, step 4).
- (c) **NAMING FIREWALL**: the commercial real-guitar game that inspired this project is never
  named in any file; use "RS"/"RS2014" or neutral phrasing. Charter (MIT) may be named.
- (h) **Builds**: all build/test/lint commands go through `.agents/rockhero-build.ps1` (usage in
  `.agents/README.md`) — never raw cmake/ctest/ninja. Intermediate phases run only the checks
  their changes determinately warrant; the final acceptance phase runs the sanctioned bundle as
  separate invocations.

Plan-specific hard rules:

- Theme and palette objects are constructed from hex literals only — never from `juce::Colours`
  named constants, which are per-TU dynamically initialized globals with no cross-TU ordering
  guarantee under MSVC incremental linking (recorded in `editor_theme.h` lines 76–83).
- The shared palette header in common must not include any JUCE header. It is plain data plus
  integer math, so the future game render stack (bgfx per
  `docs/plans/20-game-architecture-and-render-stack.md`) can consume it without dragging
  `juce_graphics` into its dependency surface.
- Identifiers use US "color"; the JUCE API keeps "colour" at its boundary (established code
  precedent throughout `editor_theme.h` and `tab_view.cpp`; no aliases).

## Current state inventory

- **EditorTheme seam** — `rock-hero-editor/ui/src/shared/editor_theme.h` defines the
  `EditorTheme` value struct (16 semantic color roles, Charter-informed cool dark palette) and
  `editorTheme()`; `editor_theme.cpp` returns one function-local static default-built instance
  and its comment names swapping that instance plus a repaint as "the future user-theme hook".
  The header is `src/`-private (correct: no consumer outside the library).
- **Theme consumers** — 13 editor/ui TUs call `editorTheme()` (arrangement_view, track_viewport,
  timeline_ruler, timeline_cursor, cursor_overlay, editor_view, menu_look_and_feel,
  signal_chain_view, insert_slot_view, plugin_browser_window, audio_device_settings_window,
  input_calibration_window, tone_track_view). Most read at paint time, but
  `plugin_browser_window.cpp:217` bakes theme colors into JUCE widget colour properties
  (`m_list_box.setColour(juce::ListBox::backgroundColourId, ...)`) outside `paint()` — a live
  theme swap must re-apply such properties, not just repaint.
- **String colors** — all in `rock-hero-editor/ui/src/tab/tab_view.cpp`:
  `g_standard_string_colors` (lines 25–32: Charter's six — red 0xffed0000, yellow 0xfff2d706,
  blue 0xff25b2ff, orange 0xffff870a, green 0xff85e747, purple 0xffd22cf8),
  `g_tertiary_string_colors` (lines 38–41: teal 0xff00b5a0 for the 7th, Charter STRING_7
  near-white gray 0xffb6b6b6 for the 8th), Charter fixed colors (lines 44–50: note background,
  chord box, hand shape, arpeggio, vibrato, mute borders), and the Charter-exact derivation
  chain: `charterDarker`/`charterBrighter`/`charterMultiply` (lines 62–109, bit-for-bit java
  `Color.darker()`/`brighter()` semantics) feeding `StringStyle` (lines 112–130: lane,
  border_inner, inner, linked_inner, tail, tail_edge, accent). `tabStringColor`
  (tab_view.cpp:862–876) assigns the six standard colors to the six highest displayed lanes and
  walks the tertiary tier downward with modulo cycling.
- **Doc/code mismatch** — `tab_view.h:45–49` documents the tertiary tier as "teal, magenta,
  chartreuse, indigo" cycling, but the implementation ships only teal plus Charter's near-white
  gray for the 8th. The recorded decision in
  `docs/in-progress/note-format-and-tablature-plan.md` ("String count..." section, dated
  2026-07-06) specifies the RYB tertiary tier — 7th teal (0,181,160), 8th magenta (255,0,144),
  9th chartreuse (170,220,0), 10th indigo (88,84,255) — with "exact RGB values are display
  configuration, finalized visually during the tab-rendering slice". The shipped slice finalized
  the 8th as gray instead of magenta and is pinned by
  `rock-hero-editor/ui/tests/test_tab_view.cpp:69–88`. Open question 2 resolves this.
- **String cap** — `rock-hero-common/core/include/rock_hero/common/core/chart/chart_rules.h:24`:
  `inline constexpr int g_max_chart_strings{8}`, with the comment "raise this once
  ninth-and-beyond lane colors are chosen". Enforced in `chart_rules.cpp:36–41`
  (`validateChartRules` rejects out-of-range tuning). Other consumers:
  `editor_controller.cpp:989,1253` (setting clamps), `editor_view.cpp:707,817` (View >
  Tablature Strings menu loop and command-range check),
  `test_editor_controller_state.cpp:766–785`. GP import builds tuning uncapped from
  `track.tuning_midi` (`gp_chart_builder.cpp:439–443`); the cap bites at validation.
- **Lane density** — `tab_view.h:19–27` pins six-string reference density: the hosting row is
  sized proportionally to string count (TrackViewport), a four-string bass shrinks the row, >6
  grows it; `tabLaneCenterY` (tab_view.cpp:882–889) divides evenly at any count.
- **rock-hero-common/ui** — placeholder library: `rock_hero_common_ui` STATIC target with
  `src/placeholder.cpp` only, PUBLIC-links `rock_hero::common::core`
  (`rock-hero-common/ui/CMakeLists.txt`). No tests target, no public headers.
- **Settings pattern** —
  `rock-hero-editor/core/include/rock_hero/editor/core/settings/i_editor_settings.h` is the
  persistence port (e.g. `tabMinimumDisplayedStrings()`/`setTabMinimumDisplayedStrings`, lines
  94–102); production `EditorSettings` in `editor/core/src/settings/`, test fakes in
  `editor/core/tests/include/.../null_editor_settings.h`, round-trip tests in
  `test_editor_settings.cpp`. View-menu state flows controller → `EditorViewState` → EditorView.
- **Design-doc anchors** — `docs/design/architecture.md` "Editor UI": "UI theming is planned as a
  distinct later phase — functionality first, polish second" (this plan is that phase).
  `docs/design/architectural-principles.md` "Library Roles / UI Modules":
  `rock-hero-common/ui` is only for UI used by both products; "Feature Folders" and "Library
  Roots Hold Folders Only" govern the new files; "CMake and Test Layout" prescribes per-library
  test targets.
- **In-flight work caution** — the working tree carries uncommitted editor-core tone-handler and
  tone-UI changes (see `docs/in-progress/tone-track-tempo-map-plan.md`). Phases 2–3 touch
  editor-core controller/view-state files; re-verify merge state before executing.

Verified against code on 2026-07-06, refactor @ 13e82fb0.

## Dependencies

- Upstream (blocking): none for Phases 1–4; this plan sits on the current baseline.
- Phase 5 coordination (not code dependencies, but gate inputs):
  - `docs/plans/22-note-detection.md` — its detection contract notes that 9–10 string low
    fundamentals stress the confirmation-latency budget; its Consumed-by list already points here.
    Raising the cap is display/authoring scope only; record that in the gate summary.
  - `docs/plans/10-format-versioning-and-chart-identity.md` — confirm against its bump rules that
    loosening a validation maximum (accept-more, no serialized-field change) needs no
    formatVersion bump before flipping the constant.
- Downstream consumers (they depend on this plan):
  - `docs/plans/25-note-highway-3d.md` — consumes Phase 1 (shared palette + derivation); it
    defers all note/string color definitions here.
  - `docs/plans/44-editor-3d-preview.md` — consumes Phase 1 and the Phase 3 preset registry.
  - `docs/plans/40-chart-editing.md` — defers the `g_max_chart_strings` raise to Phase 5 here.
  - `docs/plans/26-game-startup-menus-library.md` /
    `docs/plans/27-in-song-flow-results-profiles.md` — game-side preset picker and persistence of
    the game's string-color preset id (registry from Phases 1/3/4).

## Decisions already made

Restated with sources; do not re-litigate:

1. **EditorTheme stays editor/ui-private**; it is the single seam for app-chrome colors, read at
   paint time so a swap plus repaint retargets the whole editor (`editor_theme.h:1–9` and the
   swap-hook comment in `editor_theme.cpp:6–7`). What moves to common is the string-color
   palette *data*, not the theme (`docs/plans/25-note-highway-3d.md`, Current-state notes: "the
   string-color palette *data* is extracted to common (plan 45), not the theme").
2. **Hex-literal-only color construction** for anything initialized at static or namespace scope
   (`editor_theme.h:76–83`, MSVC cross-TU init-order hazard).
3. **Charter-exact tablature rendering**: every per-string surface derives from the base string
   color through Charter's fixed multiplier/brighten/darken chain, bit-for-bit
   (`tab_view.cpp:21–24, 61–109`). Extraction must preserve the exact integer math.
4. **Lane density pinned to six-string reference density** (`tab_view.h:19–27`); >6 strings grow
   the row, bass shrinks it. Not a theme concern; Phase 5 only re-verifies it at 10 lanes.
5. **The format is string-count-generic with a 10-string target and carries no colors**
   (`docs/in-progress/note-format-and-tablature-plan.md`, "String count, micro-bends, and forward
   extensions", 2026-07-06). The RYB tertiary-tier colors recorded there are input to open
   question 2.
6. **The cap raise is coordinated by this plan** (`docs/plans/40-chart-editing.md` Non-goals;
   `docs/plans/22-note-detection.md` Consumed-by). It is a format/domain gate, not a theme toggle.
7. **Settings stay per-product**: EditorSettings is editor-specific by design and the game gets
   its own IGameSettings port (`docs/plans/27-in-song-flow-results-profiles.md`). Both products
   persist a preset id string from the same common registry; the ids match, the stores do not.
8. **Derived-over-authored does not apply**: presets are product data, not charter-authored
   package content; nothing here writes into `.rock` packages.

## Open questions for the user

Mirror all four into `docs/plans/00-roadmap.md` Decisions-needed.

1. **Colorblind-safe preset colors (gates Phase 4).** String color is the primary information
   channel and the Charter-derived base palette has red/green (protan/deutan) hazards: red
   0xffed0000, green 0xff85e747, and orange 0xffff870a are mutually confusable. Options:
   (A) Okabe-Ito-derived preset (first cut in the Phase 4 appendix) validated by an automated
   CVD-simulation distance test, iterated visually with you; (B) hue-rotation of the classic
   palette that keeps its "feel" but fixes only the worst pairs; (C) rely on lane position alone
   and skip the preset. **Recommendation: A** — approve the methodology now, sign off on final
   hex values when Phase 4 presents them rendered; C is rejected by the roadmap requirement that
   at least one colorblind-safe preset ships.
2. **Classic preset lanes 8–10.** The recorded decision says 8th magenta 0xffff0090, 9th
   chartreuse 0xffaadc00, 10th indigo 0xff5854ff
   (`docs/in-progress/note-format-and-tablature-plan.md`), but the shipped, test-pinned 8th is
   Charter's near-white gray 0xffb6b6b6 and `tab_view.h:45–49` still documents the unshipped
   tier. Options: (A) keep gray for the 8th (it was finalized visually during the tab slice, per
   that doc's own "finalized visually" clause) and adopt chartreuse/indigo for 9/10;
   (B) restore the full RYB tier including magenta for the 8th. **Recommendation: A**, fixing the
   stale `tab_view.h` doc in Phase 1; switching later is one constant plus one test expectation.
3. **v1 theme delivery scope.** (A) Built-in presets only (Default Dark ships; more are data);
   (B) file-based user themes at v1. **Recommendation: A**; user-provided theme/palette files are
   the Phase 6 stretch, consistent with "functionality first, polish second"
   (`docs/design/architecture.md`, Editor UI).
4. **When to raise `g_max_chart_strings` to 10.** (A) Execute Phase 5 immediately after Phase 4
   (colors exist; the raise is accept-more and unblocks the format's 10-string target for GP
   import and future authoring); (B) defer until a concrete extended-range need appears.
   **Recommendation: A**, with the explicit note that display/authoring support does not promise
   detection support (`docs/plans/22-note-detection.md`).

## Phased implementation

Phases 1 and 2 are independent of each other; 3 needs both; 4 needs 1 and open question 1;
5 needs 4 (or at least agreed 9/10-lane colors for every registered preset) and open question 4.

### Phase 1 — Shared string-color palette in rock-hero-common/ui

**Scope.** Extract the string-color palette definition and the Charter-exact derivation math into
`rock-hero-common/ui` as JUCE-free data plus pure integer functions; re-point the editor tab
renderer at it with bit-identical output.

- Placement rationale: this is presentation data both products need —
  `docs/design/architectural-principles.md` "UI Modules" reserves `rock-hero-common/ui` for
  exactly that. `common/core` was considered and rejected: color policy is not domain behavior,
  and `common/core`'s feature groups (song/timeline/package/session/shared) have no honest home
  for it. The library already PUBLIC-links `rock_hero::common::core`, which the palette needs
  only for the `g_max_chart_strings` coupling below.
- New feature folder `string_colors/`:
  `rock-hero-common/ui/include/rock_hero/common/ui/string_colors/string_color_palette.h` and
  `rock-hero-common/ui/src/string_colors/string_color_palette.cpp`. Contents:
  - `ArgbColor` (std::uint32_t, 0xAARRGGBB — matches the existing hex-literal spelling and
    converts trivially to `juce::Colour` and to bgfx formats at each consumer's boundary);
  - `StringColorPalette`: stable id (persisted by settings), display name, `colorblind_safe`
    flag, `std::array<ArgbColor, 6> standard` (six-string window, lowest lane first),
    `std::array<ArgbColor, g_max_chart_strings - 6> extended` (7th string downward). Sizing
    `extended` by the cap is deliberate: raising `g_max_chart_strings` will not compile until
    every registered preset defines the new lane colors — the domain gate is compiler-enforced;
  - `stringLaneColor(displayed_string, displayed_string_count, palette)` reproducing
    `tabStringColor`'s window logic exactly (six highest lanes take `standard`, lanes below walk
    `extended` downward; keep the defensive modulo);
  - Charter-exact `darkerColor`/`brighterColor`/`multiplyColor` (ported integer math from
    tab_view.cpp:62–109) and `StringLaneStyle` (the seven derived surfaces, ported from
    `StringStyle`), so `docs/plans/25-note-highway-3d.md` derives identical surfaces;
  - the "Charter Classic" preset as `constexpr` data: the six standard colors plus teal/gray
    (or teal/magenta per open question 2), and a `stringColorPalettes()` registry returning all
    built-ins.
- Editor switch-over: `tab_view.cpp` deletes its color arrays and derivation chain;
  `tabStringColor` remains in `tab_view.h` as a thin wrapper converting `ArgbColor` to
  `juce::Colour` so the editor/ui surface and its tests are untouched. Update the stale
  `tab_view.h:45–49` tertiary-tier doc and the palette-location comment in `editor_theme.h:6–8`
  (the palette now lives in common, the theme does not — a comment fix, not a design change).
- CMake: replace `placeholder.cpp` in `rock_hero_common_ui`'s source list with the real TU (the
  placeholder exemption in `docs/design/architectural-principles.md` "Library Roots Hold Folders
  Only" applies only to not-yet-implemented libraries); add `rock-hero-common/ui/tests` with a
  `rock_hero_common_ui_tests` target linking `rock_hero::common::ui` (per-library test targets,
  same doc, "CMake and Test Layout").

**Public-header impact.** First public headers in `rock-hero-common/ui` (the palette header;
consumers outside the library exist, satisfying Placement Procedure step 4). No editor public
headers change.

**Testing plan.** `rock-hero-common/ui/tests/test_string_color_palette.cpp`: port the lane-window
expectations pinned in `test_tab_view.cpp:69–88` (bass keeps red–orange, 7-string adds teal below,
8-string adds the 8th color); known-value checks that `darkerColor`/`brighterColor` reproduce the
java semantics on edge channels (0, 1, 2, 255); a registry test that every preset's id is unique
and non-empty. The untouched `test_tab_view.cpp` color pins double as the bit-identical parity
proof for the editor.

**Exit criteria.** Editor renders byte-identically (existing ui tests green, no expectation
edits); new common/ui tests green; no JUCE include in the new public header.

**Verification** (configure needed: new target sources and a new tests target):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Configure -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets clang-tidy
```

### Phase 2 — Swappable EditorTheme presets and persistence

**Scope.** Activate the theme seam: named built-in EditorTheme presets, live switching, and
persistence.

- `editor_theme.h/.cpp`: add `EditorThemePreset` (stable id, display name, `EditorTheme` value,
  hex-literals only), `editorThemePresets()`, and `setEditorTheme(...)` swapping the active
  instance (message-thread only). v1 ships "Default Dark" (current values) as the only preset;
  more presets are data.
- Repaint propagation: survey all 13 consumer TUs for theme reads outside `paint()`. Known case:
  `plugin_browser_window.cpp:217` bakes `panel_background` into ListBox colour properties — add a
  re-apply hook (e.g. components refresh widget colours in `lookAndFeelChanged()` or an explicit
  theme-changed notification from MainWindow) rather than relying on repaint alone.
  *Verify with juce-tracktion-expert before implementing*: whether
  `sendLookAndFeelChanged()`/`lookAndFeelChanged()` is the idiomatic JUCE broadcast for restyling
  child components without a LookAndFeel subclass per color, and whether `MenuBarComponent`
  restyles without recreation.
- Persistence and flow, following the `tabMinimumDisplayedStrings` pattern end to end:
  `IEditorSettings::editorThemePreset()/setEditorThemePreset(std::string)` +
  `EditorSettings` implementation + `NullEditorSettings`; controller handler + intent +
  `EditorViewState` field (unknown/missing ids fall back to Default Dark, never an error); View
  menu gains a "Theme" submenu with radio checkmarks (`editor_view.cpp:688–717` pattern).
- Coordination: editor-core controller files carry in-flight tone work; rebase and re-verify the
  baseline stamp before this phase.

**Public-header impact.** `i_editor_settings.h`, `i_editor_controller.h`, `editor_view_state.h`
gain members (editor-core public headers, product-internal). No common headers change.

**Testing plan.** Extend `test_editor_settings.cpp` (round-trip + absent default) and
`test_editor_controller_state.cpp` (selection intent updates view state and persists; unknown id
falls back). UI wiring stays untested beyond compile per
`docs/design/architectural-principles.md` "Selective UI Wiring Tests".

**Exit criteria.** Switching presets in the View menu restyles the whole editor live (including
the plugin-browser list background) and the choice survives restart.

**Verification.**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets clang-tidy
```

### Phase 3 — String-color preset selection in the editor

**Scope.** Make the Phase 1 registry user-selectable, independent of the chrome theme (a
colorblind player needs the note palette swap regardless of chrome taste).

- `IEditorSettings::stringColorPreset()/setStringColorPreset(std::string)` plus the same
  controller/view-state/menu chain as Phase 2 ("String Colors" submenu listing
  `stringColorPalettes()`); unknown ids fall back to Charter Classic.
- `TabView` receives the active palette (extend `setState(...)` or add a setter beside
  `setVisibleTimeline`) and derives its `StringLaneStyle` set from it; repaint on change.

**Public-header impact.** Editor-core headers as in Phase 2. Common registry unchanged.

**Testing plan.** Settings round-trip and controller-state tests as in Phase 2; a `TabView` check
that lane colors follow the selected palette (existing test seam: `tabStringColor`-level
functions stay pure).

**Exit criteria.** Preset switch recolors the tab lane live and persists; default behavior
(no stored preset) is pixel-identical to Phase 1.

**Verification.** Same three invocations as Phase 2.

### Phase 4 — Colorblind-safe preset (gated on open question 1)

**Scope.** Ship one colorblind-safe preset in the common registry, validated mechanically.

- First-cut proposal (Okabe-Ito derived, dark-background variants; lanes lowest→highest of the
  standard window): vermillion 0xffd55e00, yellow 0xfff0e442, sky blue 0xff56b4e9, orange
  0xffe69f00, bluish green 0xff009e73, reddish purple 0xffcc79a7; extended tier (7th→10th): blue
  0xff0072b2, white 0xffffffff, then two placeholders pending the same review as classic 9/10.
  These are a starting point, not the deliverable — final values are signed off rendered.
- Automated acceptance: a CVD-simulation distance test in
  `rock-hero-common/ui/tests/test_string_color_palette.cpp` — simulate protanopia, deuteranopia,
  and tritanopia (Viénot 1999 or Machado 2009 linear-RGB matrices, implemented as test-local
  math), convert to Lab, and assert a minimum pairwise CIE76 distance across all defined lanes of
  every preset flagged `colorblind_safe`, both for base colors and for the derived note-fill
  surfaces (fills are what players actually read; the Charter multiplier chain shrinks
  distances). The classic preset is exempt by flag — its hazards are documented, not asserted
  away. Thresholds are calibrated so the classic palette's red/green pairs fail under the deutan
  simulation, proving the test detects the hazard it exists for.
- **STOP — present the preset rendered in the editor (screenshots) plus the CVD test report and
  get sign-off on the final hex values before commit.**

**Public-header impact.** None beyond new preset data in the existing registry.

**Testing plan.** As above; plus registry test updates (two presets, unique ids).

**Exit criteria.** Preset selectable via Phase 3 UI; CVD test green for the new preset; user
signed off on the rendered values.

**Verification.** Same three invocations as Phase 2.

### Phase 5 — DECISION-GATED: raise g_max_chart_strings to 10

Assumes open question 4 resolved to "raise now" and 9/10-lane colors exist for every registered
preset (open questions 1–2). This is a format/domain gate owned by common/core, not a theme
toggle.

- **5a — impact survey (no code change).** Enumerate and re-verify every cap consumer:
  `chart_rules.h:24` (constant + comment), `chart_rules.cpp:36–41` (message text derives from the
  constant), `editor_controller.cpp:989,1253` (clamps auto-extend), `editor_view.cpp:707,817`
  (menu loop and command range auto-extend), `test_editor_controller_state.cpp:766–785`,
  `test_tab_view.cpp:85` comment, TrackViewport proportional row sizing at 10 lanes. Confirm
  against `docs/plans/10-format-versioning-and-chart-identity.md` that an accept-more validation
  change needs no formatVersion bump. Local-only corpus checks (never CI): all 39 `.rock`
  packages still load (they are ≤8 strings; the change is accept-more), and count how many of the
  101 GP corpus files carry 9–10 string tracks that become importable (today they fail
  `validateChartRules` because `gp_chart_builder.cpp:439–443` builds tuning uncapped).
  **STOP — present the survey plus final classic-preset 9/10 colors; user sign-off flips the
  constant.**
- **5b — the raise.** Set the constant to 10; the `extended` array grows to 4 and the compiler
  forces every preset to define lanes 9/10 (the Phase 1 coupling working as designed); update the
  `chart_rules.h` comment (color precondition now satisfied) and the `test_tab_view.cpp:85`
  comment; extend palette tests to 10 lanes; verify menu, clamps, and lane geometry at 10 lanes.
  Record in the commit message that detection support is explicitly out of scope
  (`docs/plans/22-note-detection.md`).

**Public-header impact.** `chart_rules.h` constant change — a common/core public header consumed
by both products; coordinate with any in-flight plan phases that pin the value in tests
(`docs/plans/23-detection-verification-harness.md` fixture generators respect the constant
symbolically, not numerically — verify, don't assume).

**Testing plan.** Existing suites prove no regression at ≤8; new palette tests at 9/10; a
validation test that a 10-string chart passes and an 11-string chart still fails.

**Exit criteria.** 10-string charts validate, display, and import from GP; all presets define 10
lanes; corpus spot-checks pass locally.

**Verification.** Same three invocations as Phase 2 (no configure needed — no CMake graph
change), plus the local corpus smoke (local-only, never CI).

### Phase 6 — Stretch: user-provided theme and palette files (assumes open question 3 outcome B,
or later demand)

File-based user presets (JSON in per-user app data, one schema for EditorTheme values and one for
string palettes), validated on load with typed errors per
`docs/design/architectural-principles.md` "Typed Boundary Errors"; malformed files fall back to
built-ins with a logged warning, never a crash. Registered ids join the same menus. Colorblind
safety is not asserted for user files. Palette-file loading lives beside the registry in
common/ui so the game can share it; theme-file loading is editor-only. Do not start before
Phases 2–3 are stable.

**Exit criteria**: a well-formed user theme/palette file appears in the same menus as
built-ins and applies live; a malformed file falls back to built-ins with a logged warning and
no crash; tests cover both paths.

**Verification.** Same three invocations as Phase 2 (build, targeted UI tests, clang-tidy on
touched files).

## Final acceptance phase

After the last executed phase (and again after Phase 5 or 6 if they land later), run the
sanctioned bundle as separate invocations from the repo root, then commit per CLAUDE.md commit
conventions:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets clang-tidy
pre-commit run --all-files
```

## Rollback/abort notes

- **Phase 1** is a pure extraction pinned bit-identical by untouched ui tests; rollback is a
  single revert. If common placement proves premature, the data can retreat to editor/ui without
  format or persistence impact — but do this only before plan 25/44 consume it.
- **Phases 2–3** are additive; a revert loses the setting but `EditorSettings` tolerates unknown
  keys, so downgrade is safe. Fallback-to-default on unknown preset ids means a removed preset
  never bricks startup.
- **Phase 4**: the preset is data; removing it is safe. Keep the CVD test even if the preset is
  reworked — it is the acceptance harness for any future palette.
- **Phase 5 is a one-way door once content exists**: after anyone authors or imports a 9/10-string
  chart, lowering the cap orphans those charts at validation. That is why 5a ends in a STOP and
  the sign-off is explicit. Before any content exists, reverting the constant (and shrinking the
  arrays) is mechanical.
- The Phase 2 JUCE restyle mechanism is the only framework-risk item; if the
  juce-tracktion-expert check finds `lookAndFeelChanged()` unsuitable, the fallback is an
  explicit project-owned theme-changed notification from MainWindow — more wiring, no redesign.
