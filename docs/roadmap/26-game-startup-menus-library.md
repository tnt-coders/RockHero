# Plan 26 — Game Startup, Menus, and Song Library

## 1. Status

Ready — Phases 1–4 are executable now (headless core work); Phases 5–9 are blocked on
docs/roadmap/20-game-architecture-and-render-stack.md Phase 0 sign-off. Date: 2026-07-06.
Baseline: `refactor @ 3c7febe0`.

## 2. Goal

The game starts near-instantly into a navigable menu, shows the player's song library with
sortable, filterable metadata (artist/title/album, year, per-part intensity, tuning) without
re-reading packages at every launch, and walks a first-time player through device setup,
calibration, and the tuner before their first song. A player can drive every menu without a
keyboard once a song starts mattering (gamepad and MIDI pedal), and malformed packages produce a
visible, non-fatal warning instead of a broken library.

The library index cache IS the startup feature, not an optimization: each `.rock` is a ZIP whose
only current read path extracts the whole archive to a workspace
(`rock-hero-common/core/include/rock_hero/common/core/package/archive_io.h` +
`rock_song_package.h`), so an uncached recursive rescan every launch makes the loading bar
permanent.

## 3. Non-goals

- In-song flow, pause/resume, results screens, or the local score store — those are
  docs/roadmap/27-in-song-flow-results-profiles.md.
- The GameplaySession spine and tone switching — docs/roadmap/21-game-audio-engine-and-session.md.
- The render-stack decision, resource-pack conventions, and the vsync/frame-pacing policy —
  docs/roadmap/20-game-architecture-and-render-stack.md records those; this plan consumes them.
- Computing difficulty/intensity values — docs/roadmap/11-derived-difficulty-calculator.md; this
  plan only stores calculator-versioned results and defines the degraded "Unknown" behavior.
- Adding album-art, sort, or preview fields to song.json —
  docs/roadmap/43-song-information-and-art.md owns those format changes (routed through
  docs/roadmap/10-format-versioning-and-chart-identity.md).
- Creating the MIDI infrastructure — docs/roadmap/24-scoring-star-power-failure.md creates the
  `IMidiTrigger` port; this plan is a second consumer.
- Editor keybind centralization — docs/roadmap/46-editor-keybinds.md.

## 4. Constraints

Applicable subset of the roadmap constraint block:

- (a) **Layering**: common never depends on editor or game code; editor and game never depend on
  each other. Anything both products need is extracted to rock-hero-common FIRST — as its own
  phase with tests — before game code consumes it (Phase 1 here). Game code never includes editor
  headers. Tracktion headers stay isolated to rock-hero-common/audio implementation files.
- (b) **Public-header minimalism**: only headers that must be public are public; ports-and-adapters
  per docs/design/architectural-principles.md ("Ports and Adapters", "Library Roles").
- (c) **NAMING FIREWALL**: the commercial real-guitar game that inspired this project must never be
  named in any file; use "RS"/"RS2014" or neutral phrasing. Charter (BSD 3-Clause) may be named.
- (d) **Derived over authored**: intensity values in the index are computed by versioned
  calculators and cached with the calculator version — never hand-authored.
- (e) **FLAC** is the enforced package audio format (relevant to Phase 9 preview playback).
- (h) **Builds**: all build/test/lint commands go through `.agents/rockhero-build.ps1` (usage in
  `.agents/README.md`) — never raw cmake/ctest/ninja. Intermediate phases run only the checks
  their changes determinately warrant. The final acceptance phase is the sanctioned bundle as
  separate invocations: `-Targets all`, then `-RunTouchedTests`, then `-Targets clang-tidy`, plus
  `pre-commit run --all-files`.
- (i) **Real guitar input**: no plastic-controller assumptions; menus must be drivable by keyboard,
  gamepad, and MIDI foot controller, because both of the player's hands are on the guitar.

Additional hard rules for this plan:

- The game NEVER mutates user packages (docs/design/architecture.md "Application Responsibilities
  → Rock Hero Game" — the Song model is read-only to the game). The index lives in per-user app
  data only.
- The 39-package `.rock` corpus and 101-file GP corpus are converted commercial content:
  local-only soak assets, never committed, never in CI. CI tests use synthetic packages generated
  by the tests themselves.

## 5. Current state inventory

Verified by direct inspection:

- `rock-hero-game/app/main.cpp` is an 81-line temporary JUCE `DocumentWindow` shell
  (`RockHeroApplication`); no menus, no library, no input handling beyond window close.
- `rock-hero-game/core`, `rock-hero-game/audio`, `rock-hero-game/ui` are placeholder static
  libraries (`src/placeholder.cpp` each, `.gitkeep` include dirs).
  `rock-hero-game/core/CMakeLists.txt` already links `rock_hero::common::core`. No game test
  targets exist yet.
- SDL3 and bgfx appear nowhere in the build. `conanfile.txt` declares cmake-package-builder,
  catch2, libebur128, quill, ogg, vorbis.
- Package reading: `rock-hero-common/core/include/rock_hero/common/core/package/rock_song_package.h`
  exposes `readRockSongPackage(package_path, workspace_directory)` which extracts the full ZIP via
  `archive_io.h`'s `extractArchiveToWorkspace` before parsing. There is NO metadata-only read
  path. `readRockSongPackageDirectory` hard-rejects `formatVersion != 1`
  (`rock-hero-common/core/src/package/rock_song_package_read.cpp` lines 976–983,
  "Unsupported song.json formatVersion"); no migration machinery exists.
- song.json carries `metadata.title/artist/album/year` (`song/song.h` `SongMetadata`), tempoMap,
  FLAC-only audioAssets (optional normalization gainDb + validationSha256, optional startOffset),
  arrangements (UUID id, part Lead|Rhythm|Bass, audio ref, tone catalog/track, chart ref). There
  is NO album-art field, NO sort fields, NO preview fields, NO serialized difficulty.
- Chart tuning (`tuning{strings[],capo,centOffset}`) lives in chart files, not song.json;
  `rock-hero-common/core/include/rock_hero/common/core/chart/chart_document.h` exposes
  `parseChartDocument(const std::string&)` (line 26), usable over text streamed from a ZIP entry.
- `DifficultyRating` exists in the in-memory domain model only — never computed, never serialized
  (docs/design/architecture.md "Song Data Model": 0 = Unknown).
- Settings precedent: `rock-hero-editor/core/include/rock_hero/editor/core/settings/i_editor_settings.h`
  (port) + `editor_settings.h` (juce::PropertiesFile-backed implementation). EditorSettings is
  editor-specific by design; the game gets its own `IGameSettings` in plan 27.
- Shared mechanisms already available in `rock-hero-common/core/include/rock_hero/common/core/shared/`:
  `application_identity.h` (`applicationDataFolderName()` — canonical per-user data folder),
  `logger.h` (quill-backed), `cancellation_token.h`, `json.h`.
- No MIDI infrastructure, no gamepad handling, no image/thumbnail caching anywhere in the repo.
- Typed boundary errors are the established failure pattern
  (docs/design/architectural-principles.md "Typed Boundary Errors"); package failures are
  `SongPackageError`, chart failures `ChartError`.

Verified against code on 2026-07-06, refactor @ 3c7febe0.

## 6. Dependencies

- docs/roadmap/00-roadmap.md — execution order and gate registry.
- docs/roadmap/20-game-architecture-and-render-stack.md Phase 0a–0c — HARD GATE for Phases 5–9:
  window/main-loop ownership, resource-pack conventions (fonts, menu SFX), dev-diagnostics layer,
  and the vsync/frame-pacing policy this plan's video settings expose. Do not start Phase 5+
  before its STOP gate closes.
- docs/roadmap/10-format-versioning-and-chart-identity.md — unknown-field tolerance policy for the
  game-side reader (Phase 1 isolates today's hard formatVersion check so 10's ladder replaces it);
  package/chart identity hash strengthens index keys and enables move detection (Phase 2 note).
- docs/roadmap/11-derived-difficulty-calculator.md — calculator-versioned intensities stored in the
  index (Phase 2 fields); until it lands, every intensity is the "Unknown" bucket defined here.
- docs/roadmap/13-audio-device-settings-and-calibration.md — shared device settings store and
  calibration capture architecture; the game-side setup/calibration wizard UI ships in this plan's
  Phase 8 per that plan's split.
- docs/roadmap/22-note-detection.md — tuner screen consumed by Phase 8 step 3.
- docs/roadmap/24-scoring-star-power-failure.md — `IMidiTrigger` port and MIDI device
  enumeration/persistence; Phase 5's pedal input source shares it.
- docs/roadmap/27-in-song-flow-results-profiles.md — `IGameSettings` port phase; this plan's Phase 4
  consumes it. The roadmap should order that single phase ahead of Phase 4 here.
- docs/roadmap/42-chart-validation.md — richer content-validation reasons surfaced in the library
  warning view (soft dependency; Phase 7 shows package-level errors regardless).
- docs/roadmap/43-song-information-and-art.md — sort fields, album art (Phase 3 thumbnails, Phase 7
  columns), preview start/length (Phase 9).
- docs/roadmap/21-game-audio-engine-and-session.md — preview snippet playback path (Phase 9).

## 7. Decisions already made

- The game treats packages as read-only; all derived/cached data lives outside packages
  (docs/design/architecture.md "Application Responsibilities → Rock Hero Game"). Consequence:
  the library index is game-owned per-user app data, never written into `.rock` files.
- Difficulty/intensity is derived by versioned calculators, never authored
  (docs/design/architecture.md "Song Data Model";
  docs/roadmap/11-derived-difficulty-calculator.md, which absorbed the former todo derivation
  plan).
- FLAC is the enforced package audio format (docs/design/architecture.md "Technology Stack").
- Latency calibration is built in from day one, not bolted on (docs/design/architecture.md
  "Timing and Latency", "Gameplay Systems") — hence onboarding leads with device setup and
  calibration before the first song.
- The game UI follows a fixed-reference-resolution + uniform-scale model, not JUCE's
  logical-pixel + global-scale-factor model (docs/todo/editor-ui-scale.md "Out of scope").
- Headless-first structure: menu/library policy and state machines live in `rock-hero-game/core`;
  presentation stays thin; threading stays at the boundary
  (docs/design/architectural-principles.md "Core Modules", "Keep Threading at the Boundary",
  "Separate State From Side Effects").
- Visuals stay simple until gameplay fundamentals are proven (docs/design/architecture.md
  "Development Approach") — menu phases ship functional placeholder styling first.
- Art direction (recorded here as the durable statement; no earlier doc exists): classic GH-era
  menu energy, modernized in the spirit of GH: Warriors of Rock. Concrete visual design is
  deferred until docs/roadmap/20-game-architecture-and-render-stack.md fixes the stack.

## 8. Open questions for the user

Mirror each into docs/roadmap/00-roadmap.md "Decisions needed".

1. **Library index home**: (a) `rock-hero-game/core` (only the game needs an index today; the
   metadata peek reader it consumes lives in common), or (b) `rock-hero-common/core` (if the
   editor is ever expected to browse a library). **Recommendation: (a)** — layering rule (a) says
   extract to common when BOTH products need it; today only the game does, and Phase 1 already
   puts the reusable package-reading part in common.
2. **Index storage format**: (a) one versioned JSON document (via the already-permitted
   `juce_core` JSON facilities) plus thumbnail image files in a sibling folder, written
   atomically; (b) `juce::PropertiesFile`; (c) SQLite (new dependency). **Recommendation: (a)** —
   human-inspectable, testable, no new dependency; PropertiesFile is wrong-shaped for thousands
   of structured entries; SQLite is unjustified at friends-scale library sizes.
3. **Gamepad backend**: (a) SDL3's gamepad subsystem (assumes plan 20 lands SDL3), (b) a minimal
   Win32 XInput adapter behind the same port, (c) defer gamepad; ship keyboard + MIDI pedal
   first. **Recommendation: (a), with (c) as the fallback** if plan 20's gate rejects SDL3 —
   JUCE has no gamepad support, so no third option exists inside current frameworks.
4. **Bindable-action sharing with the editor** (mirrored in docs/roadmap/46-editor-keybinds.md,
   which states the final choice): (a) parallel systems by design — game menu actions are a
   polled/event-driven game-input concept spanning keyboard+gamepad+pedal, editor keybinds are
   JUCE KeyPress command dispatch; (b) one shared bindable-action concept in rock-hero-common.
   **Recommendation: (a)** — the input models differ in kind; sharing only a naming convention
   avoids a premature common abstraction.
5. **Bundled starter song**: onboarding ends at a "suggested first song", but a fresh install has
   an empty library. (a) Ship one self-authored, freely-licensed starter package as a game
   resource (dual-use as a docs/roadmap/23-detection-verification-harness.md CI fixture),
   (b) suggest the first library entry and show an empty-library help screen when none exists.
   **Recommendation: (a)** — plan 23 needs self-authored freely-licensed songs anyway; one asset
   serves both, and onboarding always has a playable target.

## 9. Phased implementation

### Phase 1 — Package description peek reader (rock-hero-common/core)

Constraint (a) extraction-first phase: the reusable capability both products could use — reading
package metadata without extracting the archive — lands in common with tests before any game code
consumes it.

- **Scope**: new API `readRockSongPackageDescription(package_path)` returning a
  `PackageDescription`: formatVersion, `SongMetadata`, per-arrangement {id, part, chart ref,
  tuning summary (strings/capo/centOffset)}, audio-asset presence, and non-fatal warnings. It
  streams `song.json` and each referenced `charts/<uuid>.chart.json` entry directly from the ZIP
  (juce::ZipFile entry streams; `parseChartDocument` over the streamed text) and never extracts
  audio or writes a workspace. Failures are typed `SongPackageError`s. Today's
  `formatVersion == 1` check is applied in exactly one named helper so
  docs/roadmap/10-format-versioning-and-chart-identity.md can replace it with the
  tolerance/migration policy without hunting call sites.
- **Files**: `rock-hero-common/core/include/rock_hero/common/core/package/package_description.h`
  (new public header — justified under (b): an external consumer, the game, exists from Phase 2),
  `rock-hero-common/core/src/package/package_description.cpp`; tests in
  `rock-hero-common/core/tests/`.
- **Public-header impact**: one new common/core public header; no changes to existing headers.
- **Testing**: tests build synthetic mini-packages in a temp dir via the existing
  `writeRockSongPackageDirectory` + `writeWorkspaceToArchive`, then assert: description fields
  match the written song; a package with a corrupt chart entry yields a description with a typed
  warning (not a hard failure); a non-package ZIP and a wrong-formatVersion package yield typed
  errors; no extraction side effects occur (no workspace argument exists to observe).
- **Exit criteria**: description read of a synthetic package is at least an order of magnitude
  cheaper than full extraction (no audio IO), all new tests pass, no editor/game code touched.
- **Verification** (CMake source-list additions require configure):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Configure -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

### Phase 2 — Library index model and scan planner (rock-hero-game/core, headless)

Assumes open question 1 = (a) and 2 = (a); relabel per the user's answers.

- **Scope**: the first real game code. `LibraryIndex` value model: entries keyed by absolute
  package path, each storing {path, file size, mtime, optional package hash (populated once
  docs/roadmap/10-format-versioning-and-chart-identity.md lands — enables move detection and stable
  thumbnail identity), `SongMetadata`, per-arrangement {part, tuning summary, intensity
  {value, calculatorVersion} — absent means the "Unknown" bucket}, thumbnail file name (empty
  until docs/roadmap/43-song-information-and-art.md adds art), scan status (Ok | Warning with typed
  reason text)}. Versioned index document (`indexFormatVersion`), serialized to one JSON file in
  per-user app data under `applicationDataFolderName()`
  (`rock-hero-common/core/.../shared/application_identity.h`), written atomically
  (temp file + rename), loaded tolerantly: any parse/version mismatch discards the file and
  schedules a rebuild — a corrupt cache must never block startup. Alongside it, a pure **scan
  planner**: given the previous index and a snapshot of current file facts (paths/sizes/mtimes as
  plain data — no IO in the planner), produce a plan of Add / Rescan / Reuse / Remove actions.
- **Files**: `rock-hero-game/core/include/rock_hero/game/core/library/` (`library_index.h`,
  `library_scan_plan.h`, `library_index_store.h`) + matching `src/library/` units; delete
  `rock-hero-game/core/src/placeholder.cpp` (root-rule scaffolding exemption ends when real files
  arrive — docs/design/architectural-principles.md "Library Roots Hold Folders Only"); create the
  `rock-hero-game/core/tests/` target (per docs/design/architecture.md "Testing Infrastructure"
  and the test-target-links-the-production-library convention).
- **Public-header impact**: new game/core public headers only (game/ui and game/app will consume
  them); nothing added to common.
- **Testing** (`rock-hero-game/core/tests/`): index JSON round-trip including absent-intensity and
  warning entries; version-mismatch and corrupt-file loads return "rebuild required" instead of
  failing; planner cases — new file → Add, changed size/mtime → Rescan, unchanged → Reuse,
  missing → Remove, moved file → Remove+Add today with a test documenting that hash-based move
  detection activates with plan 10.
- **Exit criteria**: game/core tests exist and pass; planner is pure and deterministic; index
  survives corruption by rebuild.
- **Verification**:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Configure -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

### Phase 3 — Scan orchestration, thumbnails port, warning capture

- **Scope**: a background/incremental scan engine in game/core: consumes a scan plan, executes it
  entry by entry via injected ports — `ILibraryDirectoryLister` (produces file facts for the
  configured scan roots), a package describer (Phase 1 API behind a small port so tests fake it),
  and `IArtThumbnailGenerator` (decodes art bytes → small cached image file; returns typed
  failure). Progress reporting (scanned/total, current package), cooperative cancellation via
  `common::core::CancellationToken`, and incremental index commits so an interrupted scan loses
  at most the in-flight entry. Malformed packages become Warning entries with the typed reason —
  scanning always completes. The worker thread itself is an adapter: game/core owns a synchronous
  `step()`-style state machine; a thin runner (game/app) pumps it on a dedicated thread
  (docs/design/architectural-principles.md "Keep Threading at the Boundary").
- **Files**: `rock-hero-game/core/{include,src}/.../library/` additions (`library_scan_engine.h`,
  ports); JUCE-backed thumbnail adapter deferred to when art exists (43) — until then the port
  ships with a no-art null implementation. Carry an explicit checkpoint: **verify JUCE image
  decode formats and headless (software image) rendering constraints with juce-tracktion-expert
  before implementing the thumbnail adapter** (shared with plan 43's art work).
- **Public-header impact**: game/core headers only.
- **Testing**: fake lister/describer drive the engine deterministically — mixed plans (adds,
  rescans, removals, one malformed package) produce correct final index, progress sequence, and
  warning entries; cancellation mid-plan leaves a loadable, partially-updated index.
- **Exit criteria**: full synthetic-library scan is reproducible in tests without touching the
  real filesystem beyond temp fixtures; warnings are data, never dialogs.
- **Verification**:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

### Phase 4 — Game settings consumption (first-run flag, scan roots, video settings, bindings)

Depends on the `IGameSettings` port phase of docs/roadmap/27-in-song-flow-results-profiles.md
(roadmap orders that phase first; if this plan executes earlier, pull exactly that port phase
forward and record it in both plans).

- **Scope**: settings fields this plan owns — `first_run_completed`; library scan roots (list of
  directories; default `<user music or app data>/Rock Hero/Songs`, created on demand); video
  settings model {display mode fullscreen/windowed/borderless, monitor identifier, resolution,
  vsync mode} whose semantics follow the frame-pacing policy recorded in
  docs/roadmap/20-game-architecture-and-render-stack.md (this plan persists and displays; 20's
  window layer applies); menu binding map storage (consumed by Phase 5). All persisted through
  `IGameSettings` (per-user app data, editor-settings pattern:
  `rock-hero-editor/core/.../settings/` port + PropertiesFile precedent).
- **Files**: game/core settings-consuming code plus field additions in the `IGameSettings`
  surface owned by plan 27 — coordinate the header change with that plan's owner section.
- **Public-header impact**: additive methods on `IGameSettings`.
- **Testing**: in-memory `IGameSettings` fake; round-trip of every new field; defaulting when
  absent (first run detected exactly once).
- **Exit criteria**: headless tests prove defaults, persistence, and first-run detection.
- **Verification**: same two invocations as Phase 3.

### Phase 5 — Menu input layer (GATED on plan 20 Phase 0; gamepad slice assumes SDL3 outcome)

- **Scope**: bindable menu actions (`MenuAction`: up/down/left/right, accept, back, pause-menu,
  rescan, etc.) with a headless binding resolver in game/core: raw input events (plain structs)
  → actions, rebind flow, conflict handling (overwrite-and-clear, mirroring
  docs/roadmap/46-editor-keybinds.md semantics without sharing code — open question 4). Input
  sources as adapters: keyboard from 20's window/event layer; gamepad via SDL3's gamepad
  subsystem (labeled "assumes outcome SDL3"; fallback per open question 3); MIDI foot controller
  via the `IMidiTrigger` port created by docs/roadmap/24-scoring-star-power-failure.md — the pedal
  must be able to drive pause/confirm because a mid-song player cannot reach a keyboard
  (constraint (i)).
- **Files**: `rock-hero-game/core/.../input/` (actions, bindings, resolver);
  `rock-hero-game/ui/` or `app/` adapters per 20's module layout; persistence via Phase 4.
- **Public-header impact**: game-scope headers only; `IMidiTrigger` is consumed, not modified.
- **Testing**: resolver unit tests (binding hit/miss, rebind, conflicts, multiple sources mapping
  to one action); adapters stay thin and untested beyond wiring.
- **Exit criteria**: all menu actions reachable from keyboard alone, from gamepad alone, and
  pause/accept from pedal alone (manual check on hardware; resolver behavior proven in tests).
- **Verification**: same two invocations as Phase 3.

### Phase 6 — Startup sequence and splash (GATED on plan 20 Phase 0)

- **Scope**: boot order — create window, show splash immediately, load the index file (fast
  path: no scanning), enter the interactive main menu, then start the background incremental
  rescan (Phase 3 engine) with unobtrusive progress. The determinate loading bar appears only
  when unavoidable work blocks interactivity (first run or index rebuild after corruption/version
  bump), driven by scan progress counts. Startup timing is logged through the shared logger
  (`rock-hero-common/core/.../shared/logger.h`, quill) per docs/design/architecture.md
  "Development Approach → Build First: Timing Instrumentation".
- **Budget (exit criteria)**: warm start (valid index) to interactive main menu ≤ 2.0 s measured
  locally against the 39-package corpus (local-only asset — never CI); index load parses 1000
  synthetic entries well under the menu frame budget in a game/core test (functional assertion +
  logged timing, no flaky wall-clock CI assertion); cold first run reaches an interactive (if
  empty-feeling) menu immediately with the scan running behind the bar.
- **Files**: `rock-hero-game/app/` startup composition (replacing the DocumentWindow shell per
  20's decided stack), game/core startup state machine, splash presentation in game/ui.
- **Public-header impact**: game-scope only.
- **Testing**: startup state machine unit tests (index-ok, index-rebuild, first-run branches);
  1000-entry synthetic index load test.
- **Verification**: same two invocations as Phase 3.

### Phase 7 — Main menu and Quick Play song list (GATED on plan 20 Phase 0)

- **Scope**: menu tree — Main (Quick Play, Options, Rescan Library, Quit); Options hosts video
  settings (Phase 4), audio device setup and calibration entry points
  (docs/roadmap/13-audio-device-settings-and-calibration.md), bindings (Phase 5), and re-run
  onboarding (Phase 8). Quick Play renders exclusively from the index — browsing never performs
  package IO. Sort/filter: title/artist/album using
  docs/roadmap/43-song-information-and-art.md sort fields when present (raw metadata fallback
  until then), year, tuning, and per-part intensity where absent values form the "Unknown"
  bucket and always sort last (restating plan 11's degraded-behavior rule — never authored
  values, constraint (d)). Malformed packages show a non-fatal warning badge with the stored
  typed reason (richer content reasons arrive with docs/roadmap/42-chart-validation.md). Manual
  rescan action reuses Phase 3 with visible progress. Song detail pane: arrangements, parts,
  tunings, art thumbnail when available.
- **Files**: song-list view model (filtering/sorting/selection as pure logic) in
  `rock-hero-game/core/.../library/`; presentation in `rock-hero-game/ui/` per 20's stack;
  fonts/menu SFX through 20's resource-pack conventions — no new loading paths invented here.
- **Public-header impact**: game-scope only.
- **Testing**: view-model tests — sort stability, Unknown-bucket placement, tuning filter,
  warning surfacing, empty-library state.
- **Exit criteria**: full browse of the local corpus with zero package IO after index warm-up
  (verified by logging); every list feature reachable via Phase 5 actions.
- **Verification**: same two invocations as Phase 3.

### Phase 8 — First-run onboarding chain (GATED on plan 20; depends on plans 13 and 22)

- **Scope**: a game/core state machine: Device Setup → Calibration → Tuner → Suggested First
  Song. Each step individually skippable; the chain is re-enterable from Options; completion (or
  final skip) sets `first_run_completed` (Phase 4). Device setup and calibration wizard UI ship
  here consuming docs/roadmap/13-audio-device-settings-and-calibration.md's architecture (that plan
  states the game-side wizard UI lands with this plan); the tuner screen comes from
  docs/roadmap/22-note-detection.md's first shippable consumer. Suggested song per open question 5.
- **Files**: `rock-hero-game/core/.../onboarding/` state machine; wizard/tuner hosting in
  game/ui.
- **Public-header impact**: game-scope only.
- **Testing**: state-machine tests — full run, skip-at-every-step, abort/resume, flag semantics
  (set once, re-run does not reset scores/settings).
- **Exit criteria**: fresh-install path reaches a playable song with no keyboard-only dead ends;
  every step skippable.
- **Verification**: same two invocations as Phase 3.

### Phase 9 — Song preview snippets (LATER; depends on plans 43 and 21)

- **Scope**: when 43's optional preview start/length fields exist, highlighting a song plays that
  FLAC snippet (honoring normalization gain and startOffset) through the engine path provided by
  docs/roadmap/21-game-audio-engine-and-session.md — no second decode stack (constraints (e), (g)
  rationale). Debounced start, stop on navigation, volume from the mix settings owned by 21/27.
  Songs without preview fields stay silent.
- **Testing**: preview scheduling logic (debounce/cancel) as pure tests; playback path is 21's
  responsibility.
- **Exit criteria**: browsing with previews never stutters the menu; absent fields degrade to
  silence.
- **Verification**: same two invocations as Phase 3.

## 10. Final acceptance phase

Run the sanctioned bundle as separate invocations from the repo root, then pre-commit:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets clang-tidy
pre-commit run --all-files
```

Acceptance additionally requires: warm-start budget met locally (Phase 6); a local (never-CI)
soak — full rescan of the 39-package corpus completes with zero crashes and warnings only where
packages are genuinely malformed; all game/core test suites green.

## 11. Rollback/abort notes

- **The index is disposable by design**: every phase must keep "delete the index file → clean
  rebuild" as a working recovery path; ship a debug action (20's dev-diagnostics layer) that does
  exactly that. No user data ever lives only in the index.
- **Phase 1** is additive to common/core; rollback is deleting the new header/TU pair. It must
  not modify the existing full-read path.
- **Phase 2/3**: if the scan planner or engine design fights reality, the fallback is a naive
  full rescan behind the same store interface — slower, but the index file format and UI are
  unaffected. Interrupted scans must never corrupt the index (atomic write is the invariant to
  protect).
- **Phase 5 gamepad slice** can be dropped without blocking release of keyboard + pedal input
  (open question 3 fallback); the resolver is source-agnostic so re-adding it later is additive.
- **Phases 5–9 presentation** is deliberately thin over game/core models: if plan 20's stack
  decision changes after sign-off, only game/ui adapters are rewritten; core models, tests, the
  index, and settings survive.
- **Phase 8**: if plan 22's tuner slips, the onboarding chain ships with the tuner step marked
  "coming soon" and auto-skipped — the state machine already supports skipping.
- **Phase 9** is fully optional; abort costs nothing outside its own files.
