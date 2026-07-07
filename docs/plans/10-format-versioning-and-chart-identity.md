# Plan 10 — Format Versioning and Chart Identity

**Status**: Decision-gated | 2026-07-06 | baseline `refactor @ 3c7febe0`

## 1. Goal

Give the package formats a real versioning policy and give every chart a stable identity:

- `formatVersion` becomes an enforced, evolvable contract for both `song.json` and chart files
  (today the chart parser writes `1` and ignores it on read), with explicit bump rules and a
  friendly "made with a newer editor" failure instead of a generic parse error.
- A versioned migration ladder generalizes the existing silent in-place normalizers, so future
  format changes are ordinary migration steps rather than ad-hoc shape sniffing.
- A canonical **chart-identity hash** — computed from a semantic serialization of chart content +
  tempo map + tuning, never from file bytes — becomes the key that
  `docs/plans/29-online-leaderboards.md` scores against, `docs/plans/26-game-startup-menus-library.md`
  caches on, and `docs/plans/24-scoring-star-power-failure.md` stamps into score records.

User-visible outcome: packages saved by future editor versions stop being a compatibility gamble,
older packages upgrade transparently on open, and a chart's scores/records can be keyed to exactly
the content that was played.

## 2. Non-goals

- No new `song.json` metadata fields (album art, sort fields, preview start/length) — those are
  `docs/plans/43-song-information-and-art.md`, which routes its format changes through this plan.
- No serialized difficulty — `docs/plans/11-derived-difficulty-calculator.md` owns derivation and
  cache placement; this plan only guarantees its cache keys (hash + versions) are well-defined.
- No library index cache, no leaderboard protocol, no score record format — plans 26, 29, 24.
- No changes to chart *content* semantics; the format spec stays as recorded in
  `docs/in-progress/note-format-and-tablature-plan.md` (reference only — active work).
- No byte-level package hashing, ever: load normalization legitimately rewrites package bytes
  across editor versions (see inventory), so byte hashes are unstable by design.
- No general rewrite of the package write path; only the minimum atomic-replace hardening needed
  so a migration-triggered save cannot destroy the sole copy of a user package (Phase 5).
- No `.rhp` project-document (`project.json`) or tone-document ladder work in this plan's phases;
  the ladder is designed so those formats can adopt it later, but wiring them in is out of scope.

## 3. Constraints

Applicable subset of the roadmap constraint block (see `docs/plans/00-roadmap.md`):

- (a) **Layering**: everything here lives in `rock-hero-common/core` (both products read
  packages). Common never depends on editor or game code. Tracktion headers stay isolated to
  `rock-hero-common/audio` implementation files.
- (b) **Public-header minimalism**: version constants and migration machinery stay `src/`-private;
  only the chart-identity API and the read-result surface that callers genuinely need go public,
  per `docs/design/architectural-principles.md` ("Placement Procedure for New Files", step 4).
- (c) **Naming firewall**: the commercial real-guitar game that inspired this project is never
  named in any file; use "RS" or neutral phrasing. Charter (MIT) may be named.
- (d) **Derived over authored**: the chart hash is derived from content; no authored "chart
  version" field is introduced (see Open question Q6 in
  `docs/plans/43-song-information-and-art.md`, which recommends resolving chart "version" via this
  plan's identity hash).
- (e) **FLAC**: package reader changes must not weaken the FLAC-only audio enforcement.
- (h) **Builds**: all verification through `.agents/rockhero-build.ps1` (usage in
  `.agents/README.md`); intermediate phases run only determinately warranted checks; the final
  acceptance phase runs the sanctioned bundle as separate invocations.

## 4. Current state inventory

Verified with `rg`/reads against the tree; all paths repo-relative.

**song.json versioning**
- Reader hard-rejects any `formatVersion != 1`:
  `rock-hero-common/core/src/package/rock_song_package_read.cpp:976-983` reads
  `formatVersion` with default `0`, so a *missing* field is also rejected. Error is the generic
  `SongPackageErrorCode::InvalidSongDocument` ("Unsupported song.json formatVersion") — no
  distinct "newer than supported" code exists
  (`rock-hero-common/core/include/rock_hero/common/core/package/song_package_error.h`).
- Writer emits `"formatVersion": 1` at
  `rock-hero-common/core/src/package/rock_song_package_write.cpp:345`.

**Chart-file versioning**
- Writer emits `"formatVersion": 1` at `rock-hero-common/core/src/chart/chart_document.cpp:505`
  (`chartDocumentText`). The parser `parseChartDocument`
  (`chart_document.cpp:352-491`) never reads the field — any value, or none, parses.
- Chart files are produced only by this writer (editor save and the GP importer both route through
  `writeChartDocument`), so every existing chart carries `1`.

**Serialization is hand-built string emission (no generic JSON writer)**
- `song.json`: `songDocumentContents` at `rock_song_package_write.cpp:339-385`; timing values at a
  fixed 3-decimal grid via `formatTimingValue` (`{:.3f}`, `rock_song_package_write.cpp:167-170`,
  `g_timing_decimals = 3` in `rock-hero-common/core/src/package/rock_song_package_format.h:29`);
  other doubles via `formatJsonDouble` (`{:.15g}`, lines 173-176).
- Chart: `chartDocumentText` / `noteLine` (`chart_document.cpp:239-348, 503-571`) with
  omit-when-default rules (sustain omitted at 0, FHP `width` omitted at 4, technique fields only
  when present; `startOffset` omitted at 0 in `rock_song_package_write.cpp:275-281`).
- Consequence: unknown JSON fields are **ignored on read** (readers pull known keys from
  `juce::var`) and **dropped on save** (writers emit only known fields from the domain model).
  There is no unknown-field preservation.

**Existing silent in-place normalizers (the migration-ladder precedents)**
- Legacy per-region tone names → per-arrangement named-tone catalog: `readToneCatalog`
  (`rock_song_package_read.cpp:557-604`) rebuilds the `tones` array from the whole-song default
  tone plus legacy region names when `tones` is absent. Both shapes carry `formatVersion 1`, so
  version alone cannot distinguish them — this stays a documented v1 tolerance forever.
- Missing audio-normalization metadata → analyzed on open:
  `rock-hero-editor/core/src/controller/editor_controller.cpp:523-546` wires
  `analyzeAudioForGainNormalization` into project load/import;
  `rock-hero-editor/core/src/project/project_handlers.cpp:268` marks the project as having unsaved
  changes when `audioNormalizationUpdatedOnLoad()` reports a rewrite. This is the established
  "load changed the model → surface it as unsaved changes" precedent the ladder reuses.

**Canonical-form building blocks (already exact)**
- `Fraction` is always reduced to lowest terms with positive denominator, so equal rationals share
  one representation (`rock-hero-common/core/include/rock_hero/common/core/timeline/fraction.h:19-60`).
- Token formatters emit one canonical spelling: `formatGridPositionToken` omits the fraction for
  whole beats, `formatBeatFractionToken` omits the denominator for wholes
  (`rock-hero-common/core/include/rock_hero/common/core/chart/chart_tokens.h:29-47`).
- Chart model: `rock-hero-common/core/include/rock_hero/common/core/chart/chart.h` — tuning
  (strings/capo/centOffset), chord templates, notes (position, string, fret, sustain, attack,
  mute, harmonic, touch, vibrato, tremolo, accent, bend pairs, slide waypoints), shapes, FHPs,
  sections. All value types have `==`.
- Chart structural validation runs at package read (`rock_song_package_read.cpp:738-745` calls
  `validateChartRules`); `g_max_chart_strings = 8`, `g_max_fret = 30`
  (`rock-hero-common/core/include/rock_hero/common/core/chart/chart_rules.h:24,33`).

**Hashing precedent**
- `juce::SHA256` already hashes audio streams in
  `rock-hero-common/audio/src/song/audio_normalization.cpp:328` (hex lowercased at :329); the
  project-owned wrapper target `rock_hero::juce_cryptography` exists
  (`rock-hero-common/audio/CMakeLists.txt:69`). `rock-hero-common/core` currently links
  `rock_hero::juce_core` only; `docs/design/architecture.md` ("JUCE utility dependency in core
  modules") names only narrow `juce_core` facilities as permitted in common core.

**Package write safety (input to Phase 5)**
- The `.rock` archive is rewritten **in place**: open, truncate, stream
  (`rock_song_package_write.cpp:823-858`). A crash mid-write corrupts the archive. Chart files use
  `juce::File::replaceWithText` (`chart_document.cpp:582`), which is a temp-and-swap.
- Public entry points: `readRockSongPackage[Directory]`, `writeRockSongPackage[Directory]` in
  `rock-hero-common/core/include/rock_hero/common/core/package/rock_song_package.h:23-51`.

**Sibling formats with their own hard-rejects (out of scope, ladder-adoptable later)**
- Tone documents: `rock-hero-common/audio/src/live_rig/tone_document.cpp:231-235`.
- `.rhp` `project.json`: `rock-hero-editor/core/src/project/project_io.cpp:103-107`.

**Tests today**: `rock-hero-common/core/tests/test_rock_song_package.cpp` (round-trip and error
cases, all `formatVersion 1`), `rock-hero-common/core/tests/test_chart.cpp`.

**Corpus**: 39 `.rock` packages of converted commercial content — local-only, never committed,
never in CI. All carry `formatVersion 1` in `song.json` and in every chart file.

Verified against code on 2026-07-06, refactor @ 3c7febe0.

## 5. Dependencies

Upstream: none — this is a foundation plan. Downstream plans gated on phases here:

- `docs/plans/43-song-information-and-art.md` — its metadata fields are the first real bump; it
  must not start format changes before Phase 2 lands the ladder.
- `docs/plans/29-online-leaderboards.md` — keys submissions on the Phase 3 hash + algorithm id.
- `docs/plans/26-game-startup-menus-library.md` — library cache keys include the Phase 3 hash;
  its malformed-package listing consumes Phase 2's typed newer-format error.
- `docs/plans/24-scoring-star-power-failure.md` — score record format embeds the Phase 3 hash and
  algorithm id.
- `docs/plans/11-derived-difficulty-calculator.md` — cache entries key on (chart hash, calculator
  version); needs Phase 3's identity definition.

External decisions: the Phase 0 sign-offs (Q1–Q5 below), aggregated in
`docs/plans/00-roadmap.md` Decisions-needed.

## 6. Decisions already made

Restated with sources; a fresh session must not re-litigate these.

- **Difficulty and other descriptors are derived, never authored** —
  `docs/design/architecture.md` "Song Data Model" ("a value *derived* from playable chart data,
  not authored data") and `docs/plans/11-derived-difficulty-calculator.md`. Nothing in this
  plan introduces an authored chart version or descriptor.
- **Anchor seconds keep a fixed three-decimal grid** — `docs/design/architecture.md` "Song Data
  Model"; enforced by `g_timing_decimals = 3`. The hash serialization reuses this exact stored
  precision for anchors.
- **Chart positions are exact rational grid tokens, never seconds/floats** —
  `docs/in-progress/note-format-and-tablature-plan.md` "Format Specification (v2)"; `Fraction`
  reduction makes token spellings unique, which the hash relies on.
- **Charts are arrangement-owned sidecars** (`charts/<uuid>.chart.json`), one chart per
  arrangement, no authored difficulty variants — same doc, plus `chart.h:326-331`.
- **FLAC is the enforced package audio format** — `docs/design/architecture.md` "Technology
  Stack".
- **Load-time model changes surface as unsaved changes in the editor** — established by the
  audio-normalization open flow (`project_handlers.cpp:268`); the ladder follows this precedent
  rather than inventing a new signal.
- **The warp-anchor tempo-map storage model is deliberately kept** —
  `docs/design/architecture.md` "Song Data Model"; the hash serializes anchors as stored, it does
  not resample tempo.

## 7. Open questions for the user

Mirrored into `docs/plans/00-roadmap.md` Decisions-needed. Phase 0 presents these and STOPS.

- **Q1 — formatVersion bump rule.**
  (A) *Bump on every persisted-schema change, additive or breaking* (recommended). Because both
  writers re-emit whole documents from the domain model, an older editor opening a newer additive
  package would silently strip the new fields on save; the version bump plus a hard
  "newer-than-supported" rejection is the only data-loss firewall the format has. Migration steps
  for additive changes are trivial (default-fill).
  (B) Additive changes stay in-version; only breaking changes bump. Cheaper, but accepts silent
  field loss whenever an older editor re-saves a newer package.
  (C) major.minor split (minor = additive, tolerated; major = breaking, rejected). More machinery
  than a single-editor ecosystem needs today, and still loses minor fields on old-editor save.
- **Q2 — hash persistence.**
  (A) *Compute-on-demand, never persisted inside the package* (recommended). The hash is a pure
  function of content; storing it in `song.json` creates staleness risk and drags publish
  validation into every save (the save==publish tension plan 43 already has to resolve).
  Consumers (score records, library cache, leaderboard submissions) store `(algorithm id, hex)`
  beside their own records.
  (B) Stored in `song.json` at export and validated on read. Saves recompute cost (milliseconds
  for ~200k-note corpus charts) at the price of a new invariant to police.
- **Q3 — SHA-256 provider for `rock-hero-common/core`.**
  (A) *Link the existing `rock_hero::juce_cryptography` wrapper from common/core* (recommended;
  zero new code, same primitive the audio validation hash already uses at
  `audio_normalization.cpp:328`). Requires a one-line extension of the common-core JUCE permission
  in `docs/design/architecture.md` — an explicit "requires design-doc update + user confirmation"
  step per CLAUDE.md's Documentation Maintenance Rules.
  (B) Vendor a small self-contained SHA-256 in common/core. No design-doc change, ~150 lines of
  crypto code to own forever.
  (C) Compute the hash in `rock-hero-common/audio`. Rejected shape: chart identity is headless
  core domain; game/core and tests would need the audio module for a pure function.
- **Q4 — chart-file version vs song.json version.**
  (A) *Independent integer ladders per document kind* (recommended): `song.json` and chart files
  evolve at different rates; a tuning-format change should not force a song-document bump.
  (B) One package-wide version stamped in `song.json` governing all member documents. Simpler
  mental model, coarser migrations.
- **Q5 — hash scope granularity.**
  (A) *Entire chart file (tuning, templates, notes, shapes, FHPs, sections) + tempo map*
  (recommended): identity means "the same authored chart", conservative and simple; excluded are
  song metadata, audio assets, tones, and arrangement wiring. If leaderboard fragmentation from
  cosmetic edits (e.g. renaming a section) becomes real, plan 29 can define a looser
  score-comparability key later without changing this identity.
  (B) Scoring-relevant subset only (notes + tuning + tempo map): section renames and fingering
  edits keep the same boards, but "identity" no longer matches file content and every future field
  needs an in/out ruling.

## 8. Phased implementation

### Phase 0 — decision gate

Present Q1–Q5 with the inventory evidence above. **STOP — present findings and get sign-off.**
No later phase starts before this gate closes. No code changes in this phase.

**Exit criteria**: the user has answered Q1–Q5 and signed off on the chosen outcomes.
**Verification**: none — no code changes in this phase.

### Phase 1 — validate chart-file formatVersion on read *(assumes Q4 outcome A)*

- **Scope**: `parseChartDocument` reads `formatVersion`; missing or non-integer → existing
  `ChartErrorCode::MalformedDocument`; an integer greater than the supported version → a new
  `ChartErrorCode::UnsupportedNewerFormat` with a message naming both versions ("chart was written
  by a newer editor"). Supported-version constant lives in the chart feature's private source
  (e.g. alongside `chart_document.cpp`), not a public header — no consumer outside the library
  needs it.
- **Files**: `rock-hero-common/core/src/chart/chart_document.cpp`,
  `rock-hero-common/core/include/rock_hero/common/core/chart/chart_rules.h` (new enum value only).
- **Public-header impact**: one new enum value in `ChartErrorCode` (existing public header).
- **Testing**: `rock-hero-common/core/tests/test_chart.cpp` — accepts `1`; rejects missing,
  string-typed, `0`, and `2` with the correct codes; round-trip through `chartDocumentText` still
  parses. Corpus safety: every existing chart was written by `writeChartDocument` and carries `1`
  (inventory), so nothing in the wild regresses; Phase 4 confirms against the real corpus.
- **Exit criteria**: tests pass; a synthetic version-2 chart file fails with
  `UnsupportedNewerFormat`, not `MalformedDocument`.
- **Verification** (code + behavior changed → build + tests):
  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
  ```

### Phase 2 — version policy codification + migration ladder *(assumes Q1 outcome A, Q4 outcome A)*

- **Scope**:
  1. New private machinery `rock-hero-common/core/src/package/format_migration.{h,cpp}`: a
     document-level migration step is `{int from_version; const char* description;
     juce::var (*apply)(juce::var document);}`. `applyMigrations(document, steps, current)` runs
     steps in order and reports whether any ran. Steps operate on the parsed `juce::var` *before*
     the strict current-version parser, so `rock_song_package_read.cpp` keeps exactly one parsing
     shape per document kind. The function takes its step table as a parameter so tests can drive
     synthetic ladders without touching the production registry.
  2. Read routing in `readRockSongPackageDirectory`: version `> current` → new
     `SongPackageErrorCode::UnsupportedNewerFormat` (distinct from `InvalidSongDocument`, so
     plan 26's malformed-package listing can phrase it correctly); version `< current` → ladder,
     then parse; version `== current` → parse as today. The song ladder starts **empty** — v1 is
     current — so this phase changes no bytes and no corpus behavior.
  3. Read-result surface: `readRockSongPackage[Directory]` return
     `SongReadResult { Song song; bool migrated; }` (public-header change in
     `rock_song_package.h`). The editor maps `migrated == true` to unsaved changes exactly like
     `audioNormalizationUpdatedOnLoad` (`project_handlers.cpp:268`); game-side consumers treat
     migration as in-memory only — the game never rewrites user packages.
  4. Chart files get the same routing inside `readChartDocument` (empty ladder, current = 1).
  5. The v1 tolerances stay and are documented as such in code comments: `readToneCatalog`'s
     legacy fallback (`rock_song_package_read.cpp:557-604`) is a v1 shape tolerance, not a ladder
     step, because both shapes share version 1.
  6. **Policy codification** (documentation step, gated): record the bump rule, the
     unknown-field policy (readers ignore unknown fields; writers drop them; therefore every
     schema change bumps per Q1-A), and the newer-format rejection contract in
     `docs/design/architecture.md` "Song Data Model". This is a design-doc change — **requires
     user confirmation per CLAUDE.md before editing** (can be bundled with the Q3 design-doc line
     if Q3-A is chosen in Phase 3).
- **Files**: `rock_song_package_read.cpp`, new `format_migration.{h,cpp}` (src-private),
  `song_package_error.h` (+1 enum value), `rock_song_package.h` (return type),
  `chart_document.cpp`, editor call sites of the read functions
  (`rock-hero-editor/core/src/project/project.cpp` and tests).
- **Public-header impact**: `SongReadResult` in `rock_song_package.h`; one enum value in
  `song_package_error.h`. Nothing else goes public.
- **Testing** (`test_rock_song_package.cpp` + a new `test_format_migration.cpp` in
  `rock-hero-common/core/tests/`):
  - newer version rejected with `UnsupportedNewerFormat`; equal version parses with
    `migrated == false`.
  - synthetic two-step ladder (fake v-1→0→1 steps injected via the parameterized step table)
    applies in order, reports `migrated == true`, and the migrated document parses.
  - a step that throws/returns void var fails the read with a typed error, never a partial Song.
  - editor-side: loading a `migrated` package marks unsaved changes (extend
    `rock-hero-editor/core/tests/test_project.cpp`).
- **Exit criteria**: empty-ladder behavior is byte-identical for v1 packages (existing round-trip
  tests unchanged); typed newer-format error reaches the editor's project-error surface.
- **Verification**:
  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
  ```

### Phase 3 — canonical chart-identity hash *(assumes Q2 A, Q3 A, Q5 A)*

- **Scope**: new public API in `rock-hero-common/core`:
  `include/rock_hero/common/core/chart/chart_identity.h` + `src/chart/chart_identity.cpp`.

  ```cpp
  struct ChartIdentity { std::string algorithm; std::string sha256_hex; };
  [[nodiscard]] ChartIdentity chartIdentityHash(const Chart& chart, const TempoMap& tempo_map);
  ```

  Algorithm id starts at `"RHCI-1"`. **The id changes whenever the canonical serialization or
  digest changes**; consumers always store the pair, never the bare hex.

  **Canonical serialization spec (normative)** — a UTF-8 byte string built from the *domain
  model*, never from file bytes, so load normalization and cosmetic re-serialization cannot move
  the hash:
  - Fixed field order, every field emitted explicitly (no omit-when-default rules — those belong
    to the file format, not the hash), separators fixed, no whitespace.
  - Included, in order: tuning (strings, capo, centOffset), chord templates (name, frets,
    fingers), notes (all `ChartNote` fields, bend and slide payloads in stored order), shapes,
    fret-hand positions, sections; then tempo map (timeSignatures, anchors).
  - Excluded: song metadata, audio assets and normalization, tones/tone track, arrangement ids
    and parts, package paths. (Q5-A scope.)
  - Positions and fractions via the existing canonical token formatters
    (`formatGridPositionToken`, `formatBeatFractionToken`) — unique because `Fraction` is reduced.
  - Integers via `std::to_string`. Anchor seconds at the persisted 3-decimal grid
    (`{:.3f}`, matching `g_timing_decimals`) because that *is* the exact stored value. All other
    doubles (centOffset, bend semitones, touch) via `std::format("{}", v)` shortest round-trip
    form — exact and deterministic under IEEE-754 `to_chars`, deliberately *not* the file
    format's `{:.15g}`.
  - Digest: SHA-256 over the byte string, lowercase hex (matching the audio validation hash's
    spelling at `audio_normalization.cpp:328-329`).
  - The spec text lives in the header's Doxygen block per
    `docs/design/documentation-conventions.md`, so the contract ships with the API.
- **Build**: add `rock_hero::juce_cryptography` to `rock-hero-common/core/CMakeLists.txt`
  (Q3-A), plus the one-line permission extension in `docs/design/architecture.md` ("JUCE utility
  dependency in core modules") — **requires user confirmation before the doc edit**.
- **Public-header impact**: one new public header (`chart_identity.h`). Nothing else.
- **Testing** (new `rock-hero-common/core/tests/test_chart_identity.cpp`):
  - golden known-answer vector: a small fixture chart + tempo map → exact expected hex, committed
    as the algorithm's regression anchor (any serialization drift fails loudly).
  - invariance: hash equal across parse → save (`chartDocumentText`) → re-parse; equal for a
    package in legacy tone shape vs catalog shape (tone data is out of scope); equal when song
    metadata changes.
  - sensitivity: one fret change, one tempo-anchor change, one tuning-string change, one section
    rename (per Q5-A) each produce a different hash.
  - determinism: two process-independent computations agree (same-process double call suffices as
    the automated proxy; cross-machine agreement is guaranteed by the spec, not tested in CI).
- **Exit criteria**: golden vector locked in; all invariance/sensitivity tests pass; clang-tidy
  clean on the new TU.
- **Verification** (code + behavior + new lint surface):
  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Configure -Targets all
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets clang-tidy
  ```
  (`-Configure` is warranted: the CMake graph gains a link dependency and a new TU.)

### Phase 4 — corpus impact verification (local-only)

- **Scope**: run the full 39-package corpus through the new read path and the hash. The corpus is
  converted commercial content: local-only, never committed, never in CI — this phase is a manual
  gate on the developer machine, recorded as a checklist result in the executing session.
  1. Load every package: zero read failures; zero `migrated == true` (the ladder is empty — any
     `true` is a bug).
  2. Compute every arrangement's `ChartIdentity`; save the package to a temp workspace; reload;
     hashes must be byte-identical. This is the load-normalization-stability property that makes
     the semantic hash trustworthy.
  3. Confirm every corpus chart passes the Phase 1 version check (all were written with
     `formatVersion 1`).
  Use the `corpus-smoke` skill (`.claude/skills/corpus-smoke/SKILL.md`) if present in the tree;
  otherwise drive the same loop with a throwaway local test binary that is not committed.
- **Files**: none committed (local fixtures/scratch only).
- **Testing**: the corpus run itself; CI equivalents are the Phase 2/3 unit tests over
  self-authored fixtures.
- **Exit criteria**: 39/39 packages load, round-trip, and hash stably. Any failure blocks the
  downstream plans (26/29/24/11) from consuming the hash and reopens Phase 3.
- **Verification**: no build commands beyond the already-built test binary; record the pass/fail
  table in the session log.

### Phase 5 — atomic package replace (absorbed write-safety hardening)

Absorbs the still-valid core of `docs/todo/native-package-write-safety-followups.md`, re-verified
2026-07-06: the narrowed tone-reference write-safety work is complete
(`docs/completed/native-package-write-safety-discussion.md`), but the archive itself is still
rewritten in place — open, truncate, stream (`rock_song_package_write.cpp:823-858`) — so a crash
mid-save destroys the only copy. Once migrations can rewrite packages on open→save, this risk
stops being theoretical.

- **Scope (minimal, per the question-speculative-complexity bar)**: `writeWorkspaceToArchive`
  writes to a sibling temp file (`<name>.rock.tmp` in the destination directory, same volume) and
  atomically renames over the destination on success; failure leaves the original untouched and
  reports the existing typed `ArchiveError`. Chart JSON already uses `replaceWithText`'s
  temp-and-swap (`chart_document.cpp:582`); `song.json` emission goes through the same JUCE
  safe-replace call if it does not already.
- **Deliberately deferred** (recorded here so the todo file can be deleted by the roadmap's
  disposition pass, not re-planned): the full plan/commit save split, structured field-level
  validation diagnostics (waiting on real save-repair UI), treating audio copies as planned side
  effects, and whole-directory staging for publish (waiting on plan 43's publish semantics).
  Candidate tests from the todo doc worth carrying when that work happens: multi-issue validation
  reporting, validation failure leaves destination untouched, commit-time failure reports partial
  write risk.
- **Files**: `rock_song_package_write.cpp` only.
- **Public-header impact**: none.
- **Testing**: `test_rock_song_package.cpp` — successful save replaces the archive; a simulated
  builder failure (unwritable destination directory case) leaves a pre-existing archive
  byte-identical; temp file is cleaned up on failure.
- **Exit criteria**: failure-injection test proves the original archive survives a failed save.
- **Verification**:
  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
  ```

## 9. Final acceptance phase

Per constraint (h), the one sanctioned bundle, as separate invocations from the repo root, plus
formatting:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets clang-tidy
pre-commit run --all-files
```

Acceptance additionally requires: Phase 4's 39/39 corpus table recorded; the golden hash vector
committed; the two design-doc edits (bump policy, juce_cryptography permission) either landed with
explicit user confirmation or their absence recorded as an open follow-up.

## 10. Rollback/abort notes

- **Phase 1** is a pure tightening: reverting the commit restores ignore-on-read. No data risk —
  the writer's emission is unchanged throughout.
- **Phase 2** keeps v1 parsing byte-identical (empty ladder); the risky surface is the public
  return-type change, which is compile-time-visible at every call site — revert is a clean
  `git revert`, no persisted state involved. Do not ship any *actual* migration step in this plan;
  the first real step ships with `docs/plans/43-song-information-and-art.md` where its content
  change lives.
- **Phase 3** is additive API; revert deletes the header/TU and the CMake link. The golden vector
  makes silent drift impossible after landing. If `RHCI-1` must change post-consumption (a spec
  bug found after 26/24/29 store hashes), do **not** mutate it — ship `RHCI-2` beside it and let
  consumers migrate keyed records; that is exactly why the algorithm id is stored beside the hex.
- **Phase 4** failing aborts downstream consumption but costs nothing to unwind (no commits).
- **Phase 5** touches the save path of user data: land it behind its failure-injection test in the
  same commit, and if the rename semantics misbehave on any target filesystem, revert to
  truncate-in-place (the status quo) rather than patching forward — the original behavior is
  known-bad-but-understood.
