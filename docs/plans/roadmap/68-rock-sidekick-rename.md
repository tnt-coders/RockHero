# Plan 68 — Rename the editor to Rock Sidekick

**Status:** Drafted 2026-10-10 against `master @ 8855db1d`. **PARKED 2026-10-11 as a decision
record** (user): the rename needs more decisions than this draft assumed, and is kept out of the
active plans until they are made. Do not execute the phases below until §7's new questions
68-Q6..Q9 are decided and the plan is rewritten around them. 68-Q2 is decided (`.rsk`); 68-Q1 and
68-Q3..Q5 are open, each with a recommendation. The inventory in §3 was taken by two read-only
reviews (code and build; strings, data, docs and tooling) and spot-checked against the tree.

**What changed the scope (user, 2026-10-11):** "Rock Hero" names the GAME, not the family, so the
game's tree loses its `-game` suffix and the shared library likely needs a family name that fits
both products. That reopens this draft's core assumption (§4: `rock-hero-` as the repository's
tree prefix and `rock_hero` as the root namespace), and a repository split is on the table.
Per-user file names are no longer this plan's: plan 70 names them by one product token per app
(`game`, `editor` today), stated once, so a rename changes two values and two files move by hand.

## 1. Goal

The editor becomes **Rock Sidekick**: a standalone toolset for guitarists (tablature tools and tone
automation) that stands on its own without the game, while sitting beside **Rock Hero** as its
companion. The rename is FULL — the source tree, namespace, include paths, CMake targets, the
executable, the product-identity types, every user-visible string, the persisted file names, the
log channels, and the maintained documentation — and it is executed as a small number of
mechanical, verifiable steps rather than a hand-edited sweep.

### The one rule that keeps it simple

The word "editor" in this codebase means two different things, and the rename separates them:

- **The product** — the application, its tree and namespace, its identity. This becomes
  **Sidekick**: `rock-hero-sidekick/`, `rock_hero::sidekick`, `RockSidekick`, `SidekickSettings`,
  "Rock Sidekick".
- **The role** — the editing machinery inside the product: `EditorController`, `EditorView`,
  `EditorAction`, `EditorCommandId`, `EditorUndoHistory`, `EditorTheme`, the plugin editor
  window. These STAY. Sidekick still edits projects, so `sidekick::core::EditorController` reads
  as "Sidekick's editor controller" and stays true; `SidekickAction` or `SidekickUndoHistory`
  would misdescribe what they are. Renaming them would touch ~60 types, ~5,000 occurrences and
  57 more files to make the names less accurate.

The test for any identifier: **does the game have a `Game`/`RockHeroGame` counterpart for it?**
If yes, it names the product and becomes `Sidekick`. If no, it names the editing role and stays.
The same test applies to prose (Phase 3).

## 2. Non-goals and constraints

### Non-goals

- **Positioning copy.** Text that frames the app as a game companion ("a companion chart editor"
  in `README.md:14`, "the chart-authoring editor" in `CONTRIBUTING.md`, the root
  `CMakeLists.txt:15-16` "the editor is a developer tool", the calibration guide's
  "Rock Hero gain") is a product-positioning rewrite, not a rename. Phase 3 changes the product
  NAME in those sentences; rewriting what they claim is a separate pass, recorded in
  `docs/tracking/backlog.md`.
- **A standalone installer.** `package_create` is keyed to the game as primary target with the
  editor registered as a secondary target. Shipping Sidekick as its own package is packaging
  work, recorded as a backlog item; this plan only renames the registered target's display name.
- **Shared-brand strings in `rock-hero-common`.** Plugin vendor names ("Rock Hero"), the rack name
  "Rock Hero Tones", saved plugin type names `rockHero*`, the `.rock` song format, and the
  `ROCKHERO_*` environment variables name the shared family, not the editor, and stay.
- **The repository name** (`RockHero`) stays: it is the umbrella for both products and
  `rock-hero-common`.
- **Doc file names.** No document is renamed (`adding-an-editor-action.md` names the role;
  `first-releasable-editor.md` and gate names are history and cross-referenced widely).
- **No migration code.** Per the no-legacy rule, renamed persisted files are renamed by hand once
  on the user's machine (Phase 4); nothing reads the old names.

### Constraints

- (a) **Mechanical first.** Every change that a fixed substring replacement can make is made by
  one, so the acceptance check is "`rg` for the old spelling returns zero", not a reading pass.
- (b) **One authority for the name.** The rename removes the copies of the product name that today
  bypass `application_identity.h` (§3.4), instead of renaming each copy.
- (c) **Never replace the bare word `editor`.** JUCE `TextEditor`, `CodeEditorComponent`, the
  Tracktion `Plugin::EditorComponent`/`createEditor`, the plugin editor window
  (`common/audio/src/tracktion/plugin_window.*`, `m_editor`/`setEditor` there), and the role
  types above all contain it.
- (d) **Builds** through `.agents/rockhero-build.ps1`; clang-tidy only if the user asks. Each
  phase is one commit, verified before it lands.
- (e) **Lint configuration** (`.clang-tidy`) changes need the user's sign-off; approving this plan
  signs off the one-word regex change in Phase 1.

## 3. Current state (verified 2026-10-10 against `master @ 8855db1d`)

### 3.1 Layout and build wiring

- `rock-hero-editor/` holds 402 tracked files (core 256, ui 139, audio 4, app 2) with five nested
  include roots, each `*/include/rock_hero/editor/`: `audio/`, `core/`, `core/tests/`, `ui/`,
  `ui/tests/`.
- Four spellings carry the tree name, disjoint because their separators differ:

  | Spelling | Lines / files |
  |---|---|
  | `rock_hero::editor` | 818 / 400 (760 are `namespace` lines) |
  | `rock_hero/editor` | 413 / 208 |
  | `rock-hero-editor` | 477 / 113 (most in docs) |
  | `rock_hero_editor` | 101 / 30 (CMake, plus 4 exe names in comments) |

- CMake targets `rock_hero_editor`, `_core`, `_audio`, `_ui`, `_ui_resources`, `_exe`,
  `_shaders`, `_core_testing`, `_ui_testing`, `_core_tests`, `_ui_tests`; aliases
  `rock_hero::editor::{core,audio,ui,core_testing,ui_testing}` and the umbrella. The binary-data
  alias `rock_hero::editor_ui_resources` (`rock-hero-editor/ui/CMakeLists.txt:131`) is built by
  regex in `cmake/RockHeroJuceHelpers.cmake:12` — the unanchored `rock_hero::editor` replacement
  covers it.
- `rock-hero-editor/app/CMakeLists.txt`: `:2` `PRODUCT_NAME "Rock Hero Editor"` (feeds
  `ProjectInfo::projectName`), `:5` `OUTPUT_NAME "rock-hero-editor"`, `:23`
  `package_register_target(... DISPLAY_NAME "Rock Hero Editor")`. The macOS bundle id derives
  from the target name and follows it.
- Outside the four spellings, three places hard-code the product list:
  `.clang-tidy:53` `HeaderFilterRegex: '.*[\\/]rock-hero-(common|editor|game)[\\/].*'`,
  `scripts/verify-project-conventions.py:29` `PRODUCTS = ('common', 'editor', 'game')` (which
  maps `rock-hero-{p}/` to `include/rock_hero/{p}`, `:36`, `:172`), and the root
  `CMakeLists.txt:78` `add_subdirectory`. The root `CMakeLists.txt:32` clang-tidy regex is a
  literal `rock-hero-editor` and follows the replacement.
- Nothing editor-specific in `.github/workflows` (reusable workflow via presets),
  `CMakePresets.json`, `conanfile.txt`, `.gitignore`, `.clang-format`, `.pre-commit-config.yaml`,
  or `.agents/rockhero-build.ps1` (it discovers `*_tests.exe` by glob).

### 3.2 Product-identity identifiers (renamed)

| Today | Becomes | Size |
|---|---|---|
| `RockHeroEditor` (app class, `app/main.cpp:66,215`; 2 game doc comments) | `RockSidekick` | 4 |
| `ui::Editor` (`main_window/editor.{h,cpp}`; game counterpart `game::ui::Game`) + `m_editor` in main_window | `ui::Sidekick`, `m_sidekick` | ~35 + 11, 7 files |
| `EditorSettings`, `IEditorSettings`, `NullEditorSettings`/`nullEditorSettings`, `EditorSettingsError(Code)`, `defaultEditorSettingsErrorMessage` (game counterpart `GameSettings`) | `Sidekick…` | ~300, 7 files |
| `editorApplicationName`, `editorLogFile`, `makeEditorAudioPorts`, `FakeEditorAudioPorts` | `sidekick…`, `…Sidekick…` | small |
| `EditorCommandId::ExitEditor` | `EditorCommandId::Exit` | 7 |
| log channels `"editor.*"` (11 channels, 51 sites; the game uses `"game.*"`) | `"sidekick.*"` | 51 |

About 13 source files are renamed with their types (`editor.{h,cpp}`, the `editor_settings*`
family, `i_editor_settings.h`, and their tests). The other ~57 files with "editor" in their names
name role types and keep their names.

### 3.3 Role identifiers (kept)

`EditorController` (1,643 uses), `IEditorController`, `EditorView` (385), `IEditorView`,
`EditorViewState` (473), `EditorAction`/`EditorActionId` (1,150), `EditorCommandId` (698),
`EditorCommandSpec`, `EditorUndo*`, `EditorEditContext`, `EditorSelection`, `EditorTheme`,
`EditorKeymapPersistence`, `KeymapEditorView`, the `*EditorTaskRunner` family,
`EditorProjectMarker/Caret/Cursor`, test fixtures (`TempoEditor`, `MeterEditor`) and test tags
(`[editor-controller]`, `[editor-view]`, `[editor-action]`).

### 3.4 Name copies that bypass the identity header

- `rock-hero-editor/app/main.cpp:40` `"Rock Hero Editor.log"`; `:108,113,163` log messages.
- `rock-hero-editor/app/main.cpp:70-73` `getApplicationName()` returns `ProjectInfo::projectName`
  (CMake's `PRODUCT_NAME`), so the window title comes from CMake, not from
  `application_identity.h` — two authorities for one name.
- `rock-hero-editor/ui/src/main_window/editor_view.cpp:230,338` exit prompts; `:685` the title
  fallback.
- `rock-hero-editor/core/src/project/project.cpp:50` `temp_root / "RockHero" / "project-workspaces"`
  — the temp folder does not use `applicationDataFolderName()`.
- The game has the same log-file copy: `rock-hero-game/app/main.cpp:45` `"Rock Hero Game.log"`.

### 3.5 Persisted and per-user data

- `%APPDATA%/Rock Hero/` (`applicationDataFolderName()` = `productName()` = "Rock Hero") holds
  `Rock Hero Editor.settings` (keymap, last project, per-project markers, zoom, grid),
  `Rock Hero Game.settings`, the shared `Rock Hero Audio.settings`
  (`common/audio/src/settings/audio_config_store.cpp:27`, written by both apps under an
  inter-process lock), both logs, the game's `game-sessions/` and default `Songs/`, and
  Tracktion's own `Settings.xml` (known-plugin list, scan state) because `engine.cpp:94` passes
  the folder name as Tracktion's application name — one plugin scan serves both apps.
- Settings keys contain no "editor"; keymap command ids are numeric. Nothing editor-named is
  written into `.rock`, `song.json` or `tone.json`.
- Project files use `.rhp` ("Rock Hero Project"): 176 occurrences in 45 files, 149 in code; file
  dialogs "Open/Save Rock Hero Project" at `editor_view.cpp:2842,2896`. "Export Rock Hero Song
  (.rock)" (`:2929`) is correct as written — Sidekick exports a Rock Hero song.

### 3.6 Documentation and tooling

- Path spellings in docs: design 23, developer 40, roadmap 251, in-progress 4, todo 29,
  completed 223, tracking 33; `CLAUDE.md` 11 (Architecture tree and Key files), `AGENTS.md`
  `:66-69`, `docs/Doxyfile.in:1036` (`INPUT`).
- Product prose ("the editor" as the app, "editor and game") in the maintained docs: ~300 strict
  product mentions, ~650 counting scope adjectives ("editor-side", "editor-only").
- Tooling with paths: `.claude/agents/` (`ui-design-expert.md`, `naming-expert.md`,
  `performance-expert.md`), `.claude/skills/corpus-smoke/SKILL.md:35`,
  `.claude/workflows/rockhero-deep-review.js`, `.codex/` (4 files; `desktop-gui-architect.toml`
  names "Rock Hero Editor"). `.claude/settings.local.json` is untracked.
- The user's auto-memory: 11 files name editor paths or namespaces.

## 4. Naming

| Thing | Today | Becomes |
|---|---|---|
| Product (full / short) | Rock Hero Editor / the editor | **Rock Sidekick** / **Sidekick** |
| Source tree | `rock-hero-editor/` | `rock-hero-sidekick/` |
| Namespace | `rock_hero::editor` | `rock_hero::sidekick` |
| Include path | `<rock_hero/editor/…>` | `<rock_hero/sidekick/…>` |
| CMake targets / aliases | `rock_hero_editor_*` / `rock_hero::editor::*` | `rock_hero_sidekick_*` / `rock_hero::sidekick::*` |
| Executable | `rock-hero-editor` | `rock-sidekick` |
| App class | `RockHeroEditor` | `RockSidekick` |
| Settings / log | `Rock Hero Editor.settings` / `.log` | `Rock Sidekick.settings` / `.log` |
| Log channels | `editor.*` | `sidekick.*` |

Why `rock-hero-sidekick/` and not `rock-sidekick/`: `rock-hero-` is the repository's tree prefix,
not a brand (`rock-hero-common` is not a product), and the tree → namespace → include mapping
stays a one-word swap. `rock-sidekick/` would special-case the conventions script and both lint
regexes. A top-level `rock_sidekick` namespace is ruled out: 9,034 unqualified `common::` uses in
252 files rely on sitting inside `rock_hero`. The brand lives where users see it — the
executable, the window, the files.

## 5. Phases

### Phase 1 — Move the tree (mechanical, one commit)

1. `git mv rock-hero-editor rock-hero-sidekick`, then `git mv` each of the five
   `include/rock_hero/editor` directories to `include/rock_hero/sidekick`.
2. Replace across every tracked file outside `external/` and `project-config/` — code, CMake,
   docs of every bucket, tooling — the four spellings, unanchored:
   `rock-hero-editor` → `rock-hero-sidekick`, `rock_hero_editor` → `rock_hero_sidekick`,
   `rock_hero::editor` → `rock_hero::sidekick`, `rock_hero/editor` → `rock_hero/sidekick`.
   Their separators make them disjoint, so order does not matter. Docs are included so that every
   commit leaves every path in the tree true, including completed plans; this is the one rule
   that makes the residue check a single `rg`.
3. Hand edits: `.clang-tidy:53` regex `(common|editor|game)` → `(common|game|sidekick)`;
   `scripts/verify-project-conventions.py:29` `PRODUCTS`; `app/CMakeLists.txt:5`
   `OUTPUT_NAME "rock-sidekick"`; `rock-hero-game/ui/src/game/game.h:40` (`editor::ui::Editor`
   in a comment, renamed fully in Phase 2).
4. Run pre-commit (cmake-format realigns renamed `target_link_libraries`; the conventions script
   reports any line pushed past 100 columns by the two extra characters — known:
   `sticky_label.h:6`, a `\ref` doc line, which needs a hand reflow).
5. Delete `build/{debug,relwithdebinfo,release}/rock-hero-editor/` so stale
   `rock_hero_editor_*_tests.exe` cannot be discovered by the build helper; configure with
   `-Configure` (the source tree moved); full build; run every test suite.
6. Acceptance: `rg -n "rock-hero-editor|rock_hero_editor|rock_hero::editor|rock_hero/editor"`
   outside `external/` and `project-config/` returns zero; build and all tests green; pre-commit
   clean.

### Phase 2 — Product identity, one authority (one commit)

1. Rename the §3.2 identifiers and their files (`git mv`, with `\file` tags following — Doxygen
   does not fail on a stale tag). Replace by exact token list (`EditorSettings` covers
   `IEditorSettings`, `NullEditorSettings`, `nullEditorSettings`, `EditorSettingsError`;
   `RockHeroEditor`; `editorApplicationName`; `editorLogFile`; `makeEditorAudioPorts`;
   `FakeEditorAudioPorts`; `ExitEditor`; `"editor.` log-channel literals; the string
   `Rock Hero Editor`), tree-wide including docs; `ui::Editor` and `m_editor` in main_window by
   hand (bare word, constraint (c)).
2. `application_identity.h`: `editorApplicationName()` becomes `sidekickApplicationName()`
   returning "Rock Sidekick"; update the doc comment on `gameApplicationName()` and the pinning
   test `common/core/tests/test_application_identity.cpp`.
3. One authority for the name (§3.4):
   - `getApplicationName()` returns `sidekickApplicationName()`, so the window title reads the
     identity header; the exit prompts and the `:685` fallback read it too.
   - The log file is `sidekickApplicationName() + ".log"`; the game's becomes
     `gameApplicationName() + ".log"` (the same copy, fixed by the same rule).
   - The temp workspace root uses `applicationDataFolderName()`.
   - CMake keeps the name once, in a variable used for both `PRODUCT_NAME` and `DISPLAY_NAME`; it
     then only feeds OS metadata (bundle name, version resource, installer shortcut).
   The remaining duplication — CMake's variable and the C++ constant — is inherent: CMake cannot
   read a C++ constant, and feeding the constant from CMake would need a PUBLIC compile definition
   on `common/core`, putting both product names into every target. It is two spellings in two
   known files, both named in this plan; a stated, accepted cost.
4. Per 68-Q2, the project extension becomes `.rsk`: replace `rhp` → `rsk`, `Rhp` → `Rsk`
   (`pathWithRhpExtension`), `RHP` → `RSK` (two test names) and "Rock Hero Project" →
   "Rock Sidekick Project" (the open/save dialogs), tree-wide including docs. Every match of
   `\w*rhp\w*` today is one of these tokens, so the replacement cannot hit an unrelated word.
5. Build; run the common core, sidekick core and sidekick ui suites.
6. Acceptance: `rg -n -i "Rock Hero Editor|RockHeroEditor|editorApplicationName|EditorSettings|\"editor\.|rhp|Rock Hero Project"`
   outside `external/` and `project-config/` returns zero; a fresh run shows "Rock Sidekick" in
   the title and writes `Rock Sidekick.log` and `Rock Sidekick.settings`.

### Phase 3 — Prose and tooling (one commit, no build)

1. Maintained docs only — `docs/design/`, `docs/developer/`, `docs/user/`, `README.md`,
   `CONTRIBUTING.md`, `CLAUDE.md`, `AGENTS.md`, `00-roadmap.md`, `docs/plans/in-progress/`,
   `docs/tracking/backlog.md`, `docs/tracking/watch-items.md`. Todo plans, completed plans and
   dated tracking snapshots keep their prose (their paths were already fixed in Phase 1).
2. The prose rule (the product/role test from the top of this plan, applied to words):
   - user-facing docs (README, CONTRIBUTING, `docs/user/`, the `docs/developer/index.md` intro)
     say "Rock Sidekick" on first mention, "Sidekick" after (no article: "Sidekick opens…");
   - a sentence about the product or its scope — the dependency rules, the module trees, every
     "editor and game" pairing — says Sidekick;
   - "the editor" stays where it means the editing surface or machinery ("editor action",
     "plugin editor"); the game stays "Rock Hero" in user-facing prose and "the game" internally.
3. Tooling: the `.claude/agents/`, `.claude/skills/`, `.claude/workflows/` and `.codex/` files of
   §3.6 (paths were fixed in Phase 1; this is their prose and the "Rock Hero Editor" name).
4. Record the two non-goals in `docs/tracking/backlog.md`: the positioning-copy rewrite and the
   standalone Sidekick package.

### Phase 4 — On the user's machine (no commit)

1. Rename `%APPDATA%/Rock Hero/Rock Hero Editor.settings` to `Rock Sidekick.settings` (keeps the
   keymap, last project and per-project markers); delete the old log. Rename the user's `.rhp`
   projects to `.rsk`, and the last-project path inside the settings file with them.
2. CLion: reload the CMake project (the tree moved; `.idea/` is untracked).
3. Update the 11 auto-memory files that name editor paths, and record the product/role rule as a
   memory so later sessions name new code correctly.
4. Sighting: launch Rock Sidekick — title, exit prompts, file dialogs, settings and log file
   names, the 3D preview's resources deployed beside `rock-sidekick.exe`.

## 6. CI exposure

A rename has almost no exposure to the CLAUDE.md blind-spot table (no floats, no initializers,
no switches). What remains:

- **Include order.** Moving `rock_hero/editor` to `rock_hero/sidekick` keeps its sort position
  between `rock_hero/common` and standard headers, but a renamed header can re-sort within its
  directory. Pre-commit's clang-format fixes it; CI's pre-commit gate fails if it is skipped.
- **Column limit.** +2 characters per spelling; the conventions script catches the overflow.
- **`-Wunused-function`.** Only if a renamed helper loses its last caller; a rename does not.
- **Shadowing.** A local `sidekick` beside namespace `sidekick` is today's `editor` beside
  `editor`; qualified lookup ignores variables. `ui::Sidekick` and namespace `sidekick` differ by
  case.

## 7. Decisions

- **68-Q1 Role identifiers.** (a) Keep `EditorController`, `EditorView`, `EditorAction` and the
  rest of §3.3 — they name the editing role, which Sidekick still has. (b) Rename them too.
  **Recommendation (a):** (b) costs ~5,000 occurrences and 57 file renames to produce names that
  describe the product instead of what the type does.
- **68-Q2 The project-file extension.** `.rhp` glosses as "Rock Hero Project", which a Sidekick
  project is not. **DECIDED 2026-10-10 (user): `.rsk`**, renamed now while the no-migration rule
  makes it free. Considered: `.sidekick` (unambiguous, long), `.rocksk` (reads as a variant of the
  `.rock` song it exports to), `.rockproj` (names the game's format, not Sidekick); `.rsp`,
  `.skp` and `.rsdk` are taken by compiler response files, SketchUp and the Retro Engine SDK.
  `.rsk`'s existing uses — RBR Ruskin oceanographic logger data, enterprise risk-analysis tools,
  internal SQLite stores — are not on a guitarist's machine; the residual cost is web-search
  noise for "what is an .rsk file".
- **68-Q3 The shared data folder.** `%APPDATA%/Rock Hero/` is shared by both apps (one audio
  store, one plugin scan), so it must stay one folder; the question is its name. (a) Keep
  "Rock Hero" as the family's folder — zero cost; a Sidekick-only user sees a folder named for a
  product they never installed. (b) A neutral family or vendor name — one value in
  `applicationDataFolderName()`, free before the first release. **Recommendation:** (b) if a
  family/vendor name exists now; otherwise (a), with a watch item whose trigger is "Sidekick gets
  its own installer", since after release this becomes a migration.
- **68-Q4 Directory spelling.** `rock-hero-sidekick/` (recommended, §4) or `rock-sidekick/`.
  Reopened by 68-Q7.
- **68-Q5 Short form in prose.** "Sidekick" (recommended) or always "Rock Sidekick".
- **68-Q6 The family name** (added 2026-10-11). One name for what both products share: the shared
  library's folder and namespace, probably the root namespace both products sit inside (which keeps
  the 9,034 unqualified `common::` uses working), the per-user data folder (68-Q3, plan 70's
  70-Q1), and possibly the repository. Candidates from the naming expert, then an IP check of the
  shortlist.
- **68-Q7 The tree and namespace scheme** (added 2026-10-11). The game's tree loses `-game`
  (`rock-hero/` or equivalent), the editor becomes `rock-sidekick/` or equivalent, and the shared
  library takes the family name; namespaces, include paths and CMake targets follow one mapping.
- **68-Q8 Which "Rock Hero" strings are the family** (added 2026-10-11): plugin vendor names, the
  "Rock Hero Tones" rack, saved plugin type names `rockHero*`, the `.rock` song format, the
  `ROCKHERO_*` environment variables. Each is either the game's or the family's.
- **68-Q9 One repository or three** (added 2026-10-11). Rock Hero, Rock Sidekick and a shared
  library repository included by both. **Recommended: stay one repository now.** The dependency
  directions that make the shape clean are already enforced in one repository (common never
  depends on a product; the products never depend on each other), and cross-cutting changes are
  routine (plan 54's phases each touched common and both products in one commit). Revisit at the
  first public release or packaging, chiefly for takedown isolation: a takedown notice disables a
  whole repository, and the game carries the IP risk. Watch item: "Repository split".

## 8. Final acceptance

- The Phase 1 and Phase 2 `rg` checks return zero.
- `.agents/rockhero-build.ps1` full build and every test suite green after deleting the old
  build subdirectories; pre-commit clean.
- The Phase 4 sighting passes.
- CI green on all three platforms once the user pushes.
