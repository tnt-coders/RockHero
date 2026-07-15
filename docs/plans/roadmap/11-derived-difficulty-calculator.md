# Plan 11 — Derived Difficulty Calculator

**Status:** Ready (calibration checkpoint inside Phase 3 requires user sign-off) — 2026-07-06 —
baseline `refactor @ 3c7febe0`.

## Goal

Every arrangement with a chart carries an honest 1-10 difficulty rating computed from the chart
itself — never typed in by a charter. The editor persists the rating into `song.json` as a
versioned cached value, the game reads it for its library sort columns (and recomputes stale
values into its own cache, never into the package), and improving the calculator automatically
re-rates every song on next load. Until a rating exists for an arrangement, every consumer shows
"Unknown" and sorts it last.

## Non-goals

- Hand-authored difficulty or any authoring UI for the value. There is no authoring path by
  design (`docs/design/architecture.md` § Song Data Model).
- Per-difficulty chart variants. There is exactly one chart per arrangement; difficulty is a
  derived rating, never authored variants
  (`rock-hero-common/core/include/rock_hero/common/core/chart/chart.h:330`).
- Dynamic in-game difficulty adjustment or note-dropping easy modes.
- Editor UI for displaying the rating beyond what already exists — display work rides with the
  Song Information UI (`docs/plans/roadmap/43-song-information-and-art.md`) and the game library
  (`docs/plans/roadmap/26-game-startup-menus-library.md`).
- Per-feature sub-scores (density/stamina/technique breakdown) as user-visible output. The v1
  calculator may compute them internally, but exposing them is future work.
- The full strain-graph model at v1. Ship the crude feature-weighted version first; a peak-aware
  aggregation is included because it is cheap, but tuning toward osu!/Etterna-grade strain
  modeling only happens if v1 demonstrably mis-ranks (Phase 3 evidence decides).

## Constraints

Restated subset of the roadmap constraint block (`docs/plans/roadmap/00-roadmap.md`):

- (a) **Layering**: the calculator lives in `rock-hero-common/core` so both products can call it;
  common never depends on editor or game code; editor and game never depend on each other.
- (b) **Public-header minimalism**: one new public header for the calculator API; feature
  extraction internals stay in the `.cpp`.
- (c) **NAMING FIREWALL**: the commercial real-guitar game that inspired this project is never
  named in any file; use "RS" or neutral phrasing. Charter (BSD 3-Clause) may be named.
- (d) **Derived over authored**: chart-derived descriptors are computed by versioned calculators
  and cached with the calculator version — never hand-authored. Only relational choices (part,
  representative) are authored.
- (h) **Builds**: all build/test/lint commands go through `.agents/rockhero-build.ps1` (usage in
  `.agents/README.md`), never raw cmake/ctest/ninja. Intermediate phases run only the checks
  their changes warrant; the final acceptance phase runs the sanctioned bundle.
- **The game never rewrites user packages** (roadmap tension 7, `docs/plans/roadmap/00-roadmap.md`):
  recompute happens editor-side (the editor may rewrite packages) or into the game's library
  index cache (`docs/plans/roadmap/26-game-startup-menus-library.md`), never into `.rock` files by the
  game.
- Corpus rule: the 39-package `.rock` corpus is converted commercial content — local-only,
  never committed, never in CI. Calibration constants derived from it are plain numbers and may
  be baked into code; the corpus itself never ships.

## Current state inventory

- `rock-hero-common/core/include/rock_hero/common/core/song/difficulty.h` — `DifficultyRating`
  (uint8, 0 = Unknown, authored range 1-10, `isValid`) and `DifficultyTier`
  (Unknown/Easy/Medium/Hard/Expert/Master, mapped 1-2/3-4/5-6/7-8/9-10 by `difficultyTier`).
  Covered by `rock-hero-common/core/tests/test_difficulty.cpp`.
- `rock-hero-common/core/include/rock_hero/common/core/song/arrangement.h:48` —
  `Arrangement::difficulty` exists in the in-memory model only. The package reader always writes
  `DifficultyRating{}` (`rock-hero-common/core/src/package/rock_song_package_read.cpp:766`) and
  the package writer never emits a difficulty key (no match for `difficulty` in
  `rock-hero-common/core/src/package/rock_song_package_write.cpp`). The JSON key was deliberately
  omitted to avoid implying authored data.
- Chart feature surface (`rock-hero-common/core/include/rock_hero/common/core/chart/chart.h`):
  `ChartNote` carries position (exact rational `GridPosition`), string, fret, `sustain`,
  `attack` (Pick/Hammer/Pull/Tap/Pop/Slap), `mute` (None/Palm/Full), `harmonic`
  (None/Natural/Pinch), optional `touch`, `vibrato`, `tremolo`, `accent`, a `bend` curve
  (`BendPoint` offsets + semitones), and `slides` (`SlideWaypoint`). `ChordTemplate` has
  per-string frets and fingers (fret-span/stretch features), `ChartShape` spans reference
  templates, `FretHandPosition` entries (`fret`, `width`) are the fret-hand movement signal, and
  `ChartTuning` supplies string count, capo, and cent offset.
- `rock-hero-common/core/include/rock_hero/common/core/timeline/tempo_map.h` — `secondsAtNote`
  (line 192) resolves a grid position to absolute seconds; `ForwardBeatTimeCursor` (lines
  211-233) makes a monotonic whole-chart scan cost one pass. This is how musical positions
  become the seconds needed for density and time-pressure features.
- `rock-hero-common/core/include/rock_hero/common/core/chart/chart_rules.h` —
  `validateChartRules` guarantees sorted, deduplicated, in-range notes before any calculator
  runs; `g_max_chart_strings{8}` (line 24) bounds per-string state.
- Cache-pattern precedent: `AudioNormalization`
  (`rock-hero-common/core/include/rock_hero/common/core/song/audio_normalization.h:42`) stores
  `gain_db` plus a `validation_sha256` proving the cached value still belongs to the current
  audio bytes; load recomputes when the marker is stale
  (`docs/plans/completed/backing-audio-minimal-normalization-metadata-plan.md`). Difficulty copies this
  shape: cached value + validity marker, recompute when stale.
- Note sources exist: charts enter via GP import and the corpus loads 39 packages with 135
  linked charts (local-only). The old plan's precondition "land a real note source first" is met.
- `docs/user/difficulty-ratings.md` documents the 1-10 scale and tier table and still points at
  `docs/plans/todo/arrangement-difficulty-derivation-plan.md` (now deleted — absorbed into this plan);
  `docs/design/architecture.md` § Song Data Model states difficulty is derived, currently
  unpersisted, and points at the same deleted todo doc. Both pointers must be retargeted to this
  plan when it lands.
- Tests for `rock-hero-common/core` live in `rock-hero-common/core/tests/` and register in
  `rock_hero_common_core_tests` (`rock-hero-common/core/tests/CMakeLists.txt`), linking
  `rock_hero::common::core` directly.
- No calculator code exists anywhere in the repository.

Verified against code on 2026-07-06, refactor @ 3c7febe0.

## Dependencies

- `docs/plans/roadmap/10-format-versioning-and-chart-identity.md` — Phase 4 (persistence) uses its
  additive-field policy for `song.json` changes and its chart-identity hash as the staleness
  marker for the cached rating. Phases 1-3 (the pure calculator) have no dependency on it.
- `docs/plans/roadmap/26-game-startup-menus-library.md` — consumer: its library index cache stores the
  rating tagged with calculator version and implements the game-side recompute; its sort columns
  consume the degraded-behavior contract defined in Phase 5 here. Plan 26 must not land intensity
  sorting without the "Unknown sorts last" rule from this plan.
- `docs/plans/roadmap/40-chart-editing.md` — future producer of chart changes; Phase 4's
  recompute-on-save hook is designed now so chart editing invalidates ratings for free later.
- External decision: the calibration sign-off inside Phase 3 (user reviews the corpus ranking
  table before thresholds are baked).

## Decisions already made

- **Difficulty is derived, never authored.** Source: `docs/design/architecture.md` § Song Data
  Model ("intended to be a value *derived* from playable chart data, not authored data a user
  sets by hand") and the chart model itself (`chart.h:330`). Removing subjectivity by
  construction: there is no authoring path, only a calculator.
- **The numeric 1-10 rating is the source of truth; the tier name is derived display.** Source:
  `docs/user/difficulty-ratings.md` and `difficulty.h` (`difficultyTier`). 0 means Unknown and is
  reserved for unrated arrangements.
- **Cache-with-validity-marker pattern.** Difficulty is to the chart what normalization gain is
  to the audio bytes: a computed result persisted beside a marker proving it is current,
  recomputed when stale. Source:
  `docs/plans/completed/backing-audio-minimal-normalization-metadata-plan.md` and the absorbed
  arrangement-difficulty derivation todo plan (now deleted; its content lives in this plan).
- **Pure, deterministic, per-arrangement, versioned.** The calculator is a pure function over
  chart data living in core, unit-testable without frameworks; each part rates independently;
  any change that alters output bumps the calculator version so caches recompute. Source: the
  absorbed todo plan; placement per `docs/design/architectural-principles.md` § Core Modules
  ("shared song, chart, arrangement, validation, timing, and package rules in `common/core`")
  and § Pure Unit Tests.
- **Recompute split: editor writes packages, game writes only its cache.** Source:
  `docs/plans/roadmap/00-roadmap.md` (tension 7) and
  `docs/plans/roadmap/26-game-startup-menus-library.md` (library index cache).
- **Algorithm direction (v2 ceiling).** If v1 mis-ranks, evolve toward the strain model proven
  by osu! Star Rating and Etterna MSD: per-window local difficulty → decaying strain
  accumulation → peak-weighted aggregation (sorted descending, geometric weighting). Peak
  weighting is deliberate: average density under-rates a chart that is easy except for one
  brutal passage, which humans perceive as hard. Source: the absorbed
  arrangement-difficulty derivation todo plan (now deleted). Corrections made while absorbing: the
  "anchors" movement signal it hoped for exists today as `Chart::fret_hand_positions`
  (`chart.h:347`); its precondition "land a real note source" is satisfied by GP import; its
  proposed signature `computeDifficulty(const Arrangement&)` becomes
  `computeDifficulty(const Chart&, const TempoMap&)` because tempo scaling requires the map and
  the chart is now a first-class member (`Arrangement::chart`, optional).

## Open questions for the user

1. **Persist the rating into `song.json` in the same release as the calculator, or hold
   persistence until plan 26 needs browsable ratings?**
   Options: (a) persist immediately — arrangements gain an additive `difficulty` object on the
   next editor save, packages stay browsable without parsing charts; (b) editor-side
   recompute-on-load only, persistence deferred.
   **Recommendation: (a).** The absorbed plan already chose manifest persistence "so it stays
   browsable without parsing every chart"; the field is additive under plan 10's policy, and
   deferring only moves the same format change later while forcing plan 26 to parse charts.
2. **Calibration method for mapping the raw scalar to 1-10.**
   Options: (a) user rates ~12-15 reference arrangements from the local corpus (spread across
   tiers), and a monotonic piecewise mapping is fitted to them; (b) percentile calibration —
   rank the 135-chart corpus by scalar and cut deciles; (c) hand-tuned fixed thresholds
   adjusted by eye.
   **Recommendation: (a) as the authority with (b) as a sanity cross-check.** Only (a) anchors
   the scale to human judgment the user trusts; (b) alone assumes the corpus difficulty
   distribution is uniform, which it is not. The resulting thresholds are baked as constants in
   code (numbers only — no corpus content ships).
3. **Cache the raw continuous scalar beside the 1-10 integer?**
   Options: (a) store both `rating` (1-10) and `intensity` (scalar, fixed 3-decimal
   serialization); (b) store only the integer.
   **Recommendation: (a).** Plan 26's intensity sort otherwise collapses into ten coarse
   buckets with arbitrary tie order; the scalar costs one JSON number and is already computed.

## Phased implementation

### Phase 1 — Calculator API and feature extraction (pure, no persistence)

- **Scope:** New public header
  `rock-hero-common/core/include/rock_hero/common/core/chart/difficulty_calculator.h` and
  implementation `rock-hero-common/core/src/chart/difficulty_calculator.cpp`. API surface:
  - `inline constexpr std::uint32_t g_difficulty_calculator_version{1};`
  - `struct DifficultyComputation { double intensity{0.0}; DifficultyRating rating{}; };`
  - `[[nodiscard]] DifficultyComputation computeDifficulty(const Chart& chart, const TempoMap&
    tempo_map);`
  Preconditions documented: the chart has passed `validateChartRules` (sorted notes are relied
  on); an empty chart or one the caller could not validate yields Unknown. Internal feature
  extraction (translation-unit-local): per-note onset seconds via `ForwardBeatTimeCursor`;
  windowed note density (notes/sec); chord rate (simultaneous onsets at one `GridPosition` and
  `ChartShape` spans); technique weights (attack kind, mute, harmonic, vibrato, tremolo, accent,
  bend-curve presence/extent, slide waypoint count); fret-hand movement cost (`FretHandPosition`
  shift distance and rate, fret deltas between consecutive notes); chord stretch (template fret
  span); string-skip cost (string distance between consecutive onsets). This phase returns
  intensity 0 / rating Unknown — features are computed and tested, blending comes in Phase 2.
- **Files/modules touched:** the two new files;
  `rock-hero-common/core/tests/CMakeLists.txt` and new
  `rock-hero-common/core/tests/test_difficulty_calculator.cpp`; the library's source list in
  `rock-hero-common/core/CMakeLists.txt`.
- **Public-header impact:** one new public header (justified: editor core and, later, game core
  both call it). Feature internals stay private to the `.cpp`.
- **Testing plan:** pure Catch2 unit tests in `test_difficulty_calculator.cpp` using small
  synthetic charts built inline (the existing chart tests show the pattern): feature extractors
  produce expected values for hand-computed fixtures; empty chart yields Unknown; determinism
  (two calls, identical results); positions convert to seconds consistently across a tempo
  change (anchored tempo map fixture).
- **Exit criteria:** build green, new tests pass, clang-tidy clean on new files.
- **Verification:**
  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Configure -Targets all
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets clang-tidy
  ```
  (`-Configure` because the CMake source lists change.)

### Phase 2 — v1 scoring model

- **Scope:** Blend the Phase 1 features into a scalar: per-window local difficulty (fixed-width
  sliding time window over onset seconds), each window scoring density + technique weight +
  movement cost + chord stretch + string-skip cost, all implicitly tempo-scaled because windows
  are in seconds; aggregate window scores with descending-sorted geometric peak weighting (the
  cheap half of the strain model — no decay simulation yet). Output is the raw `intensity`
  scalar; `rating` stays Unknown until Phase 3 calibrates the mapping. All weights and window
  constants live in one named-constant block with a comment stating they are calibration
  outputs.
- **Files/modules touched:** `difficulty_calculator.cpp`, tests.
- **Public-header impact:** none.
- **Testing plan:** ordering properties on synthetic charts, not absolute values: doubling
  density raises intensity; adding techniques to identical note streams raises intensity; a
  chart that is easy except one brutal 10-second passage scores above the same chart with the
  passage averaged out (peak-weighting proof); slower tempo map for identical grid content
  lowers intensity; bass-like 4-string content is scored by the same code path (string count
  from tuning, no hardcoded 6).
- **Exit criteria:** property tests pass; intensity is a total order over the synthetic ladder.
- **Verification:** `-RunTouchedTests` invocation as in Phase 1 (no `-Configure` unless test
  files were added; add `-Targets clang-tidy` if new files appear).

### Phase 3 — Corpus calibration and the 1-10 mapping (user sign-off checkpoint)

- **Scope:** A local-only calibration harness: Catch2 test cases tagged `[.local-corpus]`
  (hidden tag — never runs by default, so CI is untouched) that read the corpus root from the
  `ROCKHERO_CORPUS_DIR` environment variable, skip cleanly when unset, load every package, run
  the calculator over all linked charts, and print a ranked table (package, arrangement part,
  intensity). Depending on the user's answer to open question 2: collect reference ratings for
  ~12-15 arrangements, fit a monotonic piecewise-linear scalar→1-10 mapping, cross-check against
  corpus percentiles, and bake the thresholds as constants. **STOP — present the ranked corpus
  table and proposed thresholds to the user and get sign-off before baking.** After sign-off,
  `computeDifficulty` returns calibrated ratings and `g_difficulty_calculator_version` stays 1
  (first shipped version).
- **Files/modules touched:** `difficulty_calculator.cpp` (thresholds), new
  `rock-hero-common/core/tests/test_difficulty_corpus.cpp` (hidden-tag harness), tests
  CMakeLists.
- **Public-header impact:** none.
- **Testing plan:** the hidden-tag harness itself, plus committed unit tests pinning the
  scalar→rating mapping at its exact baked thresholds (boundary values map to the agreed tiers);
  a regression test asserting `isValid` holds for every non-Unknown output.
- **Exit criteria:** user has signed off on the ranking; thresholds baked; default test run
  (which excludes `[.local-corpus]`) is green with no corpus present.
- **Verification:** as Phase 1 (`-Configure` for the new test file), plus one manual local run:
  ```powershell
  $env:ROCKHERO_CORPUS_DIR = "<local corpus path>"
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
  ```
  (Hidden-tag cases are then invoked directly on the built
  `rock_hero_common_core_tests.exe` with the `[.local-corpus]` tag argument.)

### Phase 4 — Persistence and editor-side recompute (assumes open question 1 = persist now)

- **Scope:** Additive `song.json` arrangement fields, key names finalized against
  `docs/plans/roadmap/10-format-versioning-and-chart-identity.md` conventions — proposed shape:
  `"difficulty": { "rating": 7, "intensity": 6.842, "calculatorVersion": 1, "chartHash":
  "<plan-10 identity hash at compute time>" }`. Writer emits it only when a computed rating
  exists; reader accepts absence (Unknown). Staleness rule, mirroring `AudioNormalization`:
  on package load, recompute when `calculatorVersion != g_difficulty_calculator_version` or the
  stored `chartHash` does not match the chart's current plan-10 identity hash; recomputation is
  silent in-place load normalization, consistent with the established save==publish
  normalize-don't-reject behavior (the same mechanism that rebuilds legacy tone names and
  missing normalization metadata today). When chart editing lands
  (`docs/plans/roadmap/40-chart-editing.md`), its save path recomputes for dirty charts; the hash check
  makes that fail-safe rather than load-order-dependent. Arrangements without a chart stay
  Unknown and never emit the object. Doc updates in the same change: retarget
  `docs/user/difficulty-ratings.md` and the `docs/design/architecture.md` § Song Data Model
  pointer from the absorbed todo doc to this plan, and update that section's "not persisted"
  wording — a factual alignment of docs with implemented behavior per CLAUDE.md's Documentation
  Maintenance Rules, flagged to the user in the change description.
- **Files/modules touched:** `rock_song_package_read.cpp`, `rock_song_package_write.cpp`,
  `rock-hero-common/core/tests/test_rock_song_package.cpp`, the two docs above.
- **Public-header impact:** none expected (Arrangement already carries `difficulty`; the cache
  metadata — version + hash — needs a small struct beside it, e.g. an
  `std::optional<DifficultyCache>` member in `arrangement.h`).
- **Testing plan:** package round-trip (write rating → read same rating without recompute);
  stale-calculator-version triggers recompute on load; stale chart hash triggers recompute;
  absent field yields Unknown; chartless arrangement never writes the object; unknown-field
  tolerance interplay covered by plan 10's tests.
- **Exit criteria:** round-trip green; loading a corpus package twice is idempotent (second load
  performs no recompute); no formatVersion bump needed under plan 10's additive policy.
- **Verification:** as Phase 1: `-Targets all`, `-RunTouchedTests`, `-Targets clang-tidy`.

### Phase 5 — Game-side contract (specification consumed by plan 26)

- **Scope:** Normative contract, implemented by
  `docs/plans/roadmap/26-game-startup-menus-library.md`, recorded here as the owning spec:
  1. The game never writes to `.rock` packages. Ever.
  2. The library index cache stores, per arrangement: `rating`, `intensity`,
     `calculatorVersion`, sourced from `song.json` when fresh; when the package's stored value
     is missing or its `calculatorVersion` is stale, the game recomputes via the shared
     `computeDifficulty` during its background scan and stores the result in the cache only.
  3. Degraded behavior, in force from the moment plan 26 lands sorting (even before this
     plan's calculator ships): missing/stale/uncomputable ratings display as "Unknown" and
     sort after all rated arrangements regardless of sort direction; ties within a rating sort
     by `intensity` when present.
  4. Cache entries are invalidated by plan 26's own keys (path + size + mtime + package hash)
     plus `calculatorVersion`.
  No game code is written in this plan (the game is a build-system skeleton today); the deliverable
  is this contract plus the already-shared calculator in common/core, which satisfies layering
  constraint (a) ahead of time.
- **Files/modules touched:** none (documentation contract; plan 26 cites this section).
- **Testing plan:** lands with plan 26 (headless cache logic tests there).
- **Exit criteria:** plan 26's library-cache phase references this section verbatim in its
  Decisions-already-made.
- **Verification:** none (no code).

## Final acceptance phase

Run the sanctioned bundle as separate invocations from the repo root, per constraint (h):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets clang-tidy
pre-commit run --all-files
```

Acceptance evidence: all four green; a local corpus pass (Phase 3 harness) shows every linked
chart producing a valid 1-10 rating or an explained Unknown; loading and re-saving a corpus
package is byte-stable on the second cycle.

## Rollback/abort notes

- **Phases 1-3 are purely additive code** with no persistence: rollback is deleting the new
  files and CMake entries. No package is affected.
- **Phase 4 fields are additive** under plan 10's unknown-field tolerance: aborting means the
  editor stops writing the `difficulty` object; older packages that already carry it are simply
  re-normalized (object dropped or refreshed) on next load. No migration ladder entry is needed
  for rollback.
- **Miscalibration is self-healing by design:** if shipped ratings prove wrong, fix weights or
  thresholds, bump `g_difficulty_calculator_version`, and every consumer recomputes on next
  load/scan. Never patch persisted values by hand.
- **If v1 mis-ranks unacceptably at the Phase 3 checkpoint**, abort shipping the mapping: leave
  ratings Unknown (the Phase 5 degraded contract already makes that presentable), keep the
  feature extraction and harness, and schedule the strain-model v2 as a follow-up — the
  calculator-version cache makes that upgrade safe by construction.
- **Risk note:** the corpus skews toward the user's own library; calibration may not generalize
  to arbitrary community charts. Accepted for v1 — the version bump path is the correction
  mechanism.
