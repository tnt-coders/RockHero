# Plan 27 — In-Song Flow, Results, and Profiles

**Status:** Phase 1 COMPLETE (2026-07-12, overnight/game-shell): `IGameSettings` port +
`GameSettings` (PropertiesFile-backed per-user XML, save-on-set, test-isolation path ctor) +
`GameSettingsError` + `NullGameSettings` fake, all mirroring the editor pattern; v1 keys
profileId (UUID, get-or-create, stable across reopen — tested), profileDisplayName ("Player"
default, empty rejected), firstRunCompleted (absence = first run); `gameApplicationName()` added
to application_identity.h; key-addition convention + reserved mix keys documented in the port
header. **Extended 2026-07-12 by docs/plans/roadmap/26 Phase 4** (coordinated header change, this plan
owns the surface): `customScanRoots()` / `setCustomScanRoots(span<path>)` under key
`customScanRoots` (JSON array of native paths in one property value), plus the matching
`NullGameSettings` overrides. Verified `-Configure -Targets all` + `-RunTouchedTests` green; clang-tidy pending user
trigger. Original: Ready — 2026-07-06 — baseline `refactor @ 3c7febe0`.
Phases 2–4 are executable now (headless game/core). Phase 5 is gated on
docs/plans/roadmap/21-game-audio-engine-and-session.md and docs/plans/roadmap/24-scoring-star-power-failure.md;
Phase 6 is gated on docs/plans/roadmap/20-game-architecture-and-render-stack.md Phase 0 and
docs/plans/roadmap/26-game-startup-menus-library.md (menu input layer). Open questions 1–3 fix policy
constants consumed by Phase 3; the phase implements the recorded recommendations only after the
user answers them in docs/plans/roadmap/00-roadmap.md Decisions-needed.

## 1. Goal

A complete "song lifecycle" around actual gameplay: the player starts a song with a fair lead-in,
pauses and resumes without taking hands off the guitar (pedal/MIDI), restarts instantly, fails or
finishes cleanly, and lands on a results screen showing score, accuracy, best streak, stars,
per-section breakdown, and early/late tendency. Every completed run is persisted to a local score
store keyed by (chart identity hash, arrangement, profile, scoring-ruleset version) in a format
that docs/plans/roadmap/29-online-leaderboards.md can upload unchanged. A game-owned settings port
(IGameSettings) gives all game plans one per-user persistence seam, with a single implicit profile
at v1 whose id is stamped on every persisted record.

## 2. Non-goals

- Scoring rules, star power, the fail meter, the per-note verdict format, and the MIDI trigger
  port — owned by docs/plans/roadmap/24-scoring-star-power-failure.md; this plan consumes them.
- Menus, library, song selection, first-run onboarding UI, video settings definitions — owned by
  docs/plans/roadmap/26-game-startup-menus-library.md (it persists through this plan's settings port).
- Practice-mode looping and speed control — docs/plans/roadmap/28-practice-mode.md (deferred); this plan
  only avoids blocking it.
- Multi-profile UI (creation, switching, avatars). v1 has one implicit profile; the data model
  carries a profile id so multi-profile later is additive, not a migration.
- Score upload, accounts, server anything — docs/plans/roadmap/29-online-leaderboards.md.
- Replay rendering from verdict logs (a future idea enabled by the record format, not built here).
- Any write into `.rock` packages. Scores and settings are per-user app data only.

## 3. Constraints

Applicable subset of the roadmap constraint block (see docs/plans/roadmap/00-roadmap.md):

- (a) **Layering**: common never depends on editor or game code; game never depends on editor
  code; game code never includes editor headers. Everything in this plan is game-scope except one
  shared identity constant added to `rock-hero-common/core` (Phase 1), which has no game
  dependency.
- (b) **Public-header minimalism**: only ports and value types are public; store file layout,
  index format, and PropertiesFile keys stay in `src/`. Ports-and-adapters per
  docs/design/architectural-principles.md ("Core Position", "Library Roles").
- (c) **NAMING FIREWALL**: the commercial real-guitar game that inspired this project is never
  named in any file; use "RS" or neutral phrasing. Charter (BSD 3-Clause) may be named.
- (h) **Builds**: all build/test/lint commands go through `.agents/rockhero-build.ps1` (usage in
  `.agents/README.md`) — never raw cmake/ctest/ninja. Intermediate phases run only the checks
  their changes warrant; the final acceptance phase runs the sanctioned bundle as separate
  invocations.
- (i) **Real guitar input**: both hands are on the guitar during play. Pause and in-song actions
  must be reachable via MIDI foot controller (shared infrastructure from
  docs/plans/roadmap/24-scoring-star-power-failure.md); keyboard is a fallback, never the only path.
- The game treats the `Song` model as read-only during gameplay and never mutates user packages
  (docs/design/architecture.md, "Application Responsibilities"). The score store and settings
  live in per-user app data.

## 4. Current state inventory

Verified with `rg`/reads against the tree; the working tree also carries unrelated in-flight
editor-core tone changes that this plan does not touch.

- `rock-hero-game/` is a build-system skeleton: `rock-hero-game/app/main.cpp` is a ~67-line JUCE
  DocumentWindow shell; `rock-hero-game/core/src/placeholder.cpp`,
  `rock-hero-game/audio/src/placeholder.cpp`, `rock-hero-game/ui/src/placeholder.cpp` are
  placeholder static libs with `.gitkeep` include dirs. No `rock-hero-game/*/tests/` targets
  exist yet (docs/design/architecture.md, "Testing Infrastructure", lists them as planned).
- The settings pattern to mirror exists and is editor-specific by design:
  `rock-hero-editor/core/include/rock_hero/editor/core/settings/i_editor_settings.h` (pure-virtual
  port, typed getter/setter pairs returning `std::expected<void, EditorSettingsError>`),
  `editor_settings.h`/`src/settings/editor_settings.cpp` (JUCE `juce::PropertiesFile`-backed
  implementation; per-user location built from
  `rock-hero-common/core/include/rock_hero/common/core/shared/application_identity.h` —
  `editorApplicationName()` + `applicationDataFolderName()`, XML storage, per-user, save-on-set),
  `editor_settings_error.h` (stable error-code enum + message struct), tests in
  `rock-hero-editor/core/tests/test_editor_settings.cpp`, and a null fake at
  `rock-hero-editor/core/tests/include/rock_hero/editor/core/testing/null_editor_settings.h`.
- `application_identity.h` defines `productName()`, `applicationDataFolderName()`, and
  `editorApplicationName()`; there is no game application name yet.
- Chart sections exist for the per-section breakdown:
  `rock-hero-common/core/include/rock_hero/common/core/chart/chart.h` defines `ChartSection`
  (`GridPosition position` + `std::string type`, "repeat numbering is derived") and
  `Chart::sections` sorted by position.
- `rock-hero-common/core/include/rock_hero/common/core/song/arrangement.h` gives arrangements a
  stable string `id` and a `Part` enum (Lead | Rhythm | Bass).
  `song/difficulty.h` holds `DifficultyRating` (in-memory only, never serialized).
- There is NO MIDI infrastructure anywhere in the repo (docs/plans/roadmap/24 creates it), NO score
  storage of any kind, NO game settings, NO chart-identity hash (docs/plans/roadmap/10 creates it), and
  NO GameplaySession (docs/plans/roadmap/21 creates it).
- `common::audio::InputCalibrationState` exists at
  `rock-hero-common/audio/include/rock_hero/common/audio/input/input_calibration_state.h` and is
  already persisted per input-device identity by the editor settings; the shared settings/offset
  architecture is docs/plans/roadmap/13-audio-device-settings-and-calibration.md.
- Build helper: `.agents/rockhero-build.ps1` (presets lowercase; `-Targets`, `-RunTouchedTests`,
  `-Configure` only after CMake graph changes; quiet by default).

Verified against code on 2026-07-06, refactor @ 3c7febe0.

Re-verified for Phase 1 on 2026-07-12, overnight/game-shell @ 80efcbe2 — corrections: the
game-skeleton bullet is obsolete (plan 20 Phases 1–4, plan 25 Phases 3–4, and plan 21 Phases 1–6
landed since: SDL3 shell, game/core carries frame clock/diagnostics/resources/session with a
tests target, `GameplaySession` EXISTS at rock-hero-game/core session/ with play/pause/seek/
restart and speed/loop pass-throughs — Phase 3's flow machine drives it through those instead of
raw transport calls); the editor settings pattern bullet re-verified intact (all five files as
listed); application_identity.h gained `gameApplicationName()` in this phase. The reserved mix
key names (Phase 1 scope note) were added 2026-07-12 by plan 21 Phase 4's record.

## 5. Dependencies

Inbound (this plan consumes):

- docs/plans/roadmap/10-format-versioning-and-chart-identity.md — chart-identity hash phase: the score
  store key. Phase 2 cannot finalize keys before the hash spec exists.
- docs/plans/roadmap/12-playback-clock.md — seek/pause snap rules consumed by Phase 5's resume pre-roll.
- docs/plans/roadmap/13-audio-device-settings-and-calibration.md — device-loss policy (auto-pause,
  non-destructive score state) consumed by Phase 3/5; calibration offsets appear in score records
  via docs/plans/roadmap/24's record format.
- docs/plans/roadmap/20-game-architecture-and-render-stack.md — Phase 0 gate (render stack), resource-pack
  convention (count-in click, menu SFX) consumed by Phases 5–6.
- docs/plans/roadmap/21-game-audio-engine-and-session.md — GameplaySession pause/resume/seek and
  instant-restart guarantees consumed by Phase 5.
- docs/plans/roadmap/24-scoring-star-power-failure.md — score-record format phase (per-note verdict log,
  ruleset/detection versions, chart hash, calibration offsets, modifiers), IMidiTrigger port,
  fail-meter events, fail-on default (no-fail opt-in). Phases 2, 4, and 5 are gated on the
  relevant 24 phases; pin
  exact phase numbers during the roadmap consistency pass in docs/plans/roadmap/00-roadmap.md.
- docs/plans/roadmap/26-game-startup-menus-library.md — menu input layer (keyboard/gamepad/pedal
  navigation) consumed by Phase 6.
- docs/plans/roadmap/23-detection-verification-harness.md — deterministic verdict-log replay provides
  fixtures for Phase 4 tests once available (Phase 4 also ships its own synthetic fixtures).

Outbound (other plans consume this plan):

- docs/plans/roadmap/21 (mix-volume persistence), docs/plans/roadmap/25 (lefty toggle persistence),
  docs/plans/roadmap/26 (first-run flag, video settings persistence),
  docs/plans/roadmap/24 (no-fail opt-in preference; fail is the default) → Phase 1's IGameSettings.
- docs/plans/roadmap/29-online-leaderboards.md → Phase 2's score store: the persisted record file is the
  upload unit, byte-for-byte.
- docs/plans/roadmap/28-practice-mode.md → Phase 3's flow machine leaves explicit seams (see Phase 3).

## 6. Decisions already made

Restated from their source documents (never from conversation):

- **Fail is ON by default; no-fail is opt-in via a persisted setting** —
  docs/plans/roadmap/24-scoring-star-power-failure.md §6 (settled with the user 2026-07-16). The
  no-fail preference is an `IGameSettings` key (Phase 1) read by this plan's pre-song flow; the
  fail flow is the default and no-fail suppresses song-ending only when the player enabled it,
  and results label the run's mode. A no-fail run and a fail-enabled run are separate scoring
  categories (plan 24 §6), so this plan's bests and history keep them apart.
- **The score-record format is owned by plan 24** (per-note verdict log with hit/miss, timing
  delta, detected pitch, confidence; scoring-ruleset version; detection-engine version; chart
  hash; calibration offsets; modifiers) — docs/plans/roadmap/24-scoring-star-power-failure.md. This plan
  owns only persistence, keying, indexing, and querying of those records.
- **The game treats `Song` as read-only and owns scoring UX** — docs/design/architecture.md,
  "Application Responsibilities". Corollary adopted here: nothing in this plan writes into
  `.rock` packages.
- **All scoring comparisons happen in audio-sample time with calibration offsets applied; the
  audio thread is the single source of truth for timing** — docs/design/architecture.md, "Timing
  and Latency". Flow-machine timestamps and verdict timing deltas use the playback clock
  (docs/plans/roadmap/12), never render-thread time.
- **Latency calibration is built in from day one** — docs/design/architecture.md, "Gameplay
  Systems". Results early/late tendency therefore doubles as a calibration health check
  (docs/plans/roadmap/13).
- **Product core modules may use narrow JUCE utilities (`juce::File`, `juce::PropertiesFile`,
  JSON) while staying headless and automated-testable** — docs/design/architectural-principles.md,
  "Core Modules". GameSettings and the score store live in `rock-hero-game/core` on this basis.
- **EditorSettings is editor-specific by design; the game gets its own port following the same
  pattern** — the editor port lives at
  `rock-hero-editor/core/include/rock_hero/editor/core/settings/i_editor_settings.h` and is not
  shared; shared *audio-device* settings are a separate concern owned by
  docs/plans/roadmap/13-audio-device-settings-and-calibration.md.

Adopted 2026-07-11 with the user (shared-navigation decision, stated in
docs/plans/roadmap/28-practice-mode.md §7; normative for this plan):

- **Performance mode's navigation capability set is {toggle-play, restart, quit} — nothing
  else.** Seek, section jumps, loop, and speed exist only in practice mode. The flow machine
  simply never emits out-of-set transport intents, and the shared navigation HUD renders from
  the capability set, so the restriction is data-driven — not a forked UI.
- **The flow machine feeds plan 24's record `integrity` fields**: pause count and total paused
  duration accumulate across the run, and any out-of-set transport command reaching the
  transport (possible only through non-UI paths) marks the run not clean. Integrity is evidence
  in the record (docs/plans/roadmap/24 §6), never trust in the client UI.

## 7. Open questions for the user

Mirror all three into docs/plans/roadmap/00-roadmap.md Decisions-needed.

1. **Pause control when only one footswitch is mapped.** Star power (plan 24) defaults to any
   footswitch press; pause also needs a hands-on-guitar trigger.
   - (A) Single switch, dual role: press = star power, long-hold = pause. Cost: an intentional
     pause first fires (or must suppress) a star-power deploy; adding release-latency to star
     power instead would hurt the primary action.
   - (B) Pause requires its own binding (second footswitch or a different MIDI note/CC); Esc on
     keyboard always pauses as a fallback. Cost: single-switch owners must reach the keyboard.
   - (C) User-selectable mode: B by default, A available as an opt-in combined mapping.
   - **Recommendation: B at v1**, with the binding model (24's mappable trigger actions) designed
     so C is a later mapping option, not a rework. Auto-pause paths (device loss, focus loss) make
     the keyboard fallback rarely needed mid-song.
2. **Song-start count-in.** Options: (A) always play an audible click count-in; (B) visual-only
   3-2-1 countdown with a guaranteed minimum runway (insert session lead-in silence when the
   first note is closer to position zero than the runway); (C) a settings toggle from day one.
   - **Recommendation: B with a 3.0 s minimum runway** — an audible click over the song intro is
     widely disliked; the audible click stays reserved for resume pre-roll where it earns its
     keep. Add the toggle later only if asked; the flow machine treats count-in length and
     audibility as injected policy either way.
3. **Resume pre-roll shape.** Options: (A) fixed rewind (e.g. 3.0 s) — simple, may land mid-beat;
   (B) rewind to the nearest measure boundary at least 3.0 s back (tempo-map-derived), with an
   audible click during the pre-roll and notes inside the pre-roll window replayed as
   non-scoring "ghosts"; (C) no rewind, 3-2-1 unpause in place (GH-style, hostile to fretting-hand
   placement).
   - **Recommendation: B.** The player must re-place their fretting hand (constraint (i));
     a musical count-in beats a silent timer. Already-issued verdicts are immutable — replayed
     ghost notes are display-only, so pausing can never farm score.

## 8. Phased implementation

### Phase 1 — IGameSettings port, GameSettings adapter, and profile identity

- **Scope**: Create the game's per-user settings seam mirroring the editor pattern, plus the
  implicit v1 profile. Deliver: `IGameSettings` (pure-virtual port), `GameSettings`
  (`juce::PropertiesFile`-backed, per-user XML like the editor's), `GameSettingsError` (stable
  code enum + message, mirroring `editor_settings_error.h`), and a
  `NullGameSettings` test fake. v1 keys: `profileId` (UUID string, generated on first access and
  then stable), `profileDisplayName` (default "Player"), `firstRunCompleted` (bool, consumed by
  docs/plans/roadmap/26). Document in the header the convention consumer plans follow to add keys
  (typed getter/setter pair per key, `std::optional` reads, `std::expected` writes) — plans 21,
  25, and 26 add their own keys in their own phases; this plan does not pre-declare them.
  Reserved key names already promised by plan 21 Phase 4's session-local mix values
  (2026-07-12): `mixMasterDb`, `mixBackingDb`, `mixMonitorDb` (21-Q3: global scope).
  Add `gameApplicationName()` ("Rock Hero Game") beside `editorApplicationName()` in
  `application_identity.h` so the settings file lands in the standard per-user folder.
- **Files/modules**: new `rock-hero-game/core/include/rock_hero/game/core/settings/
  {i_game_settings.h, game_settings.h, game_settings_error.h}`, new
  `rock-hero-game/core/src/settings/{game_settings.cpp, game_settings_error.cpp}`; replace the
  placeholder source list in `rock-hero-game/core/CMakeLists.txt`; new
  `rock-hero-game/core/tests/` target (`rock_hero_game_core_tests` linking `rock_hero_game_core`
  per the project's test-target convention); edit
  `rock-hero-common/core/include/rock_hero/common/core/shared/application_identity.h`.
- **Public-header impact**: three new public game/core headers; one constexpr accessor added to an
  existing common/core header. Doxygen per docs/design/documentation-conventions.md.
- **Testing plan**: `rock-hero-game/core/tests/test_game_settings.cpp` mirroring
  `rock-hero-editor/core/tests/test_editor_settings.cpp`: explicit-path construction, roundtrip of
  every v1 key, absence semantics before first write, profileId generated once and stable across
  reopen, typed errors on unsaveable paths.
- **Exit criteria**: game/core compiles as a real library with a passing test target; profile id
  survives process restart (proven by reopen test).
- **Verification** (CMake graph changed → configure; code + behavior → build + tests):

  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Configure -Targets all
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
  ```

### Phase 2 — Local score store (gated: 10's identity hash + 24's record format)

- **Scope**: Append-only, crash-safe persistence and querying of finished-run score records.
  Key tuple: **(chart identity hash — docs/plans/roadmap/10; arrangement id — `Arrangement::id`; profile
  id — Phase 1; scoring-ruleset version — docs/plans/roadmap/24; fail-mode — `modifiers.noFail`)**.
  Fail-mode is part of the best key per plan 24 §6's normative separation: a no-fail run and a
  fail-enabled run of the same chart occupy distinct best slots and never overwrite each other
  (`bestFor` takes the mode; history can still show both). Records themselves are stored
  undivided — the partition is in the derived index, not the on-disk record layout. Layout
  (private to `src/`, documented in the implementation, rebuildable by scan):
  - `<per-user app data>/Rock Hero/game/scores/<profileId>/records/<utcIso8601>-<uuid>.score.json`
    — the file bytes are exactly plan 24's serialized ScoreRecord, no store wrapper, so
    docs/plans/roadmap/29 uploads the file unchanged.
  - `.../scores/<profileId>/meta/<chartHash>.json` — display snapshot (title, artist, album,
    part, arrangement id) captured at record time so history renders after a package moves or
    is deleted. Snapshot data comes from the loaded `Song`, read-only.
  - `.../scores/<profileId>/index.json` — derived cache per key tuple: best score, stars, play
    count, last-played. Corrupt or missing index → rebuilt by scanning records; corrupt record →
    skipped with a logged warning, never deleted. Records are immutable once written.
  - Writes are atomic: temp file in the destination directory, then rename.
  - Retention: keep every record (a few KB each per plan 24); no pruning at v1.
  - Queries: `bestFor(key)`, `historyFor(key)`, `recentPlays(profileId, limit)`. Personal-best
    comparisons are per ruleset version AND per fail-mode (older-ruleset and cross-mode records
    stay visible in history, labeled — a no-fail best is never presented as beating a
    fail-enabled best or vice versa).
- **Files/modules**: new `rock-hero-game/core/include/rock_hero/game/core/scores/
  {score_record_store.h, score_store_error.h}` + `rock-hero-game/core/src/scores/…`; consumes the
  ScoreRecord type from plan 24's game/core scoring module.
- **Public-header impact**: two public headers (port + error). File layout and index format stay
  private per constraint (b).
- **Testing plan**: `test_score_record_store.cpp` with fixture record JSONs under
  `rock-hero-game/core/tests/`: write/read roundtrip, index rebuild after deletion/corruption,
  corrupt-record tolerance, key-tuple query correctness, temp+rename atomicity (no partial file
  visible under simulated failure), profile-id stamping.
- **Exit criteria**: a record written by the store is byte-identical to the input ScoreRecord
  serialization; all queries answer from a cold rebuild with the index deleted.
- **Verification**: same two commands as Phase 1 (`-Configure` only if the source list changed
  the graph; otherwise omit).

### Phase 3 — In-song flow state machine (headless; policy from open questions 1–3)

- **Scope**: A pure, deterministic state machine in game/core — no JUCE message loop, no audio,
  injected clock and policy. States: `Loading → CountIn → Playing → Paused(cause) →
  ResumePreRoll → Playing → Completed → ResultsReady` plus `Playing → Failed` (only when fail
  opted in, per plan 24) and `Restarting` (from Paused/Failed/ResultsReady back to CountIn with
  score state reset). Pause causes: `PlayerRequested`, `DeviceLost` (docs/plans/roadmap/13 policy:
  auto-pause, score state preserved non-destructively), `FocusLost` (auto-pause when the window
  loses foreground focus during Playing). Song-start policy per Q2 (default: visual count-in,
  >= 3.0 s runway; runway shortfall requested from the session as lead-in silence). Resume policy
  per Q3 (default: measure-snapped >= 3.0 s rewind, click during pre-roll, ghost replay,
  verdicts immutable). Song end: run completes when backing playback ends; a skip-to-results
  affordance arms after (last scored note + 3.0 s). The machine emits intents (`SeekTo`,
  `StartTransport`, `PauseTransport`, `PlayClick`, `PersistRecord`, `ShowResults`) that Phase 5
  binds to real ports — the machine itself stays simulation-testable per
  docs/design/architectural-principles.md ("Core Modules": simulation belongs in game/core).
  **Practice-mode seam** (docs/plans/roadmap/28): the machine takes playback-rate and loop-region fields
  in its session-config struct from day one (only 1.0 / no-loop ship now).
  **Integrity accounting** (§6, docs/plans/roadmap/24 §6): the machine accumulates pause count and
  total paused duration and clears the record's `integrity.cleanRun` on any out-of-capability
  transport command, emitting all three into the plan 24 record at persist time.
- **Files/modules**: `rock-hero-game/core/include/rock_hero/game/core/flow/in_song_flow.h`,
  `rock-hero-game/core/src/flow/in_song_flow.cpp`.
- **Public-header impact**: one public header (state, events, intents, policy struct).
- **Testing plan**: `test_in_song_flow.cpp` transition-table tests with a fake clock: count-in
  duration honored; pause during count-in restarts count-in; pause→pre-roll→resume produces the
  measure-snapped seek intent; ghost window never emits scoring; restart resets and re-enters
  CountIn; DeviceLost pauses without touching score state; fail only fires when opted in;
  record-persist intent is emitted before the show-results intent (crash-safety ordering);
  pause/resume cycles accumulate the integrity counts while pause alone leaves `cleanRun` true.
- **Exit criteria**: every legal transition covered by a test; illegal transitions are
  unrepresentable or rejected; policy constants injected, not hardcoded.
- **Verification**: build + touched tests via the two standard invocations (no `-Configure`
  unless the test source list changes the graph).

### Phase 4 — Results computation (gated: 24's verdict-log format)

- **Scope**: Pure function(s) from (per-note verdict log, `Chart::sections`, tempo map) to a
  `ResultsSummary` value: final score, accuracy % (hit ÷ scoreable notes; revoked provisional
  hits count as misses per plan 24), best streak, stars (restating plan 24's thresholds, never
  recomputing scoring), per-section rows (section label from `ChartSection::type` + derived
  repeat number, note count, accuracy) using section time spans derived from the tempo map,
  early/late tendency (mean and median signed timing delta of hits, early/late share), and a
  calibration hint (|mean delta| above a threshold → "consider re-running calibration", per
  docs/plans/roadmap/13). No-fail and modifier labels pass through from the record (plan 24 defines
  them).
- **Files/modules**: `rock-hero-game/core/include/rock_hero/game/core/results/results_summary.h`,
  `rock-hero-game/core/src/results/results_summary.cpp`.
- **Public-header impact**: one public header (value type + compute function).
- **Testing plan**: `test_results_summary.cpp` with synthetic verdict logs: all-hit and all-miss
  extremes, sectionless charts (single implicit section), early-vs-late skew detection,
  streak across section boundaries, revoked-hit accounting. Adopt docs/plans/roadmap/23's replayed
  verdict-log fixtures as an additional corpus when that harness lands.
- **Exit criteria**: summary values match hand-computed fixtures exactly; function is
  deterministic and allocation-light.
- **Verification**: standard build + touched-tests invocations.

### Phase 5 — Session integration (gated: 21 GameplaySession; 24 IMidiTrigger; 12 clock; 13 device-loss)

- **Scope**: Bind Phase 3 intents to real ports: transport pause/resume/seek through
  GameplaySession (docs/plans/roadmap/21) with seek/pause snap rules from docs/plans/roadmap/12; instant restart
  via 21's restart guarantee; count-in and pre-roll click from the resource-pack convention of
  docs/plans/roadmap/20; pedal pause via plan 24's IMidiTrigger mapped actions (default per Q1 answer;
  Esc fallback always); device-loss events from docs/plans/roadmap/13 feeding `Paused(DeviceLost)`;
  completed runs serialized by plan 24's recorder and persisted through Phase 2's store BEFORE
  results display. Live guitar monitoring stays active while paused (the player is holding a
  live instrument; muting on pause would be hostile — revisit only if feedback says otherwise).
- **Files/modules**: `rock-hero-game/audio` and/or `rock-hero-game/app` composition code binding
  the ports; exact files depend on 21's delivered shape — re-verify against code when this phase
  starts.
- **Public-header impact**: none intended; composition only.
- **Testing plan**: port-level fakes: a fake GameplaySession asserting the seek/pause/play call
  sequence for pause→resume and restart; a fake MIDI trigger driving pause; a fake store
  asserting persist-before-results ordering. No audio-device tests (adapter tests belong to the
  plans owning those adapters).
- **Exit criteria**: end-to-end pause/resume/restart against fakes matches Phase 3's intent
  traces; a manually played song on real hardware pauses and resumes via pedal (manual check,
  recorded in the PR description).
- **Verification**: standard build + touched-tests invocations.

### Phase 6 — Pause, fail, and results UI (gated: 20 Phase 0; 26 menu input layer)

- **Scope**: `rock-hero-game/ui` screens on the render stack chosen by docs/plans/roadmap/20: pause
  overlay (Resume / Restart / Quit to library), fail screen (Retry / Quit; only reachable when
  fail opted in), results screen (score, accuracy, streak, stars, per-section table, early/late
  gauge, calibration hint, personal-best comparison + new-best badge from Phase 2 queries,
  no-fail/modifier labels). All navigation through docs/plans/roadmap/26's menu input layer
  (keyboard/gamepad/pedal — a player mid-song cannot reach a keyboard). Art direction follows
  the statement recorded in docs/plans/roadmap/26. Results screen reads a completed `ResultsSummary` and
  store queries only — no scoring logic in UI per docs/design/architectural-principles.md.
- **Files/modules**: new `rock-hero-game/ui` sources (shape depends on 20's outcome; re-verify).
- **Public-header impact**: game/ui headers stay product-internal; none public beyond the module.
- **Testing plan**: view-model tests in game/core or game/ui tests where the render stack allows
  headless construction (20's headless/noop-renderer path); screen logic (menu focus order,
  best-comparison formatting) tested without a GPU.
- **Exit criteria**: full song lifecycle playable start → results → back to library with pedal
  and keyboard both; results figures match Phase 4 fixtures on a replayed verdict log.
- **Verification**: standard build + touched-tests invocations.

## 9. Final acceptance phase

Run the sanctioned bundle as separate invocations from the repo root, then pre-commit:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets clang-tidy
pre-commit run --all-files
```

Acceptance additionally requires: a full manual song run on real hardware exercising pedal pause,
resume pre-roll, restart, song completion, and a persisted record that plan 29's format checker
(once it exists) accepts unchanged.

## 10. Rollback/abort notes

- Phases 1–4 are additive headless libraries; rollback is reverting their commits. No package
  format, no shared engine, and no editor code are touched, so no cross-product fallout.
- Phase 1's only shared edit (`application_identity.h`) is an additive constexpr accessor;
  reverting it cannot affect the editor.
- Score-store risk is data loss/corruption, mitigated structurally: records are immutable and the
  index is a rebuildable cache. If the index logic misbehaves post-release, deleting `index.json`
  is a safe user-side recovery; records are never rewritten.
- Phase 5 touches live transport interactions (pause during count-in, seek races on resume). If
  pedal-pause integration proves flaky, abort to keyboard-only pause (the flow machine is
  identical; only the trigger binding is disabled) rather than shipping an unreliable pedal path.
- Phase 6 depends on plan 20's stack decision; if that gate re-opens, Phases 1–5 remain valid —
  only the screen implementations are stack-specific by design.
