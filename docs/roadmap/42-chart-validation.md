# Plan 42 — Chart Validation

**Status:** Ready — 2026-07-06 — baseline `refactor @ 13e82fb0`. Rule severities are frozen at
the Phase 5 corpus-calibration checkpoint; open questions in section 8 shape defaults, not
structure.

## Goal

A reusable chart-content validation (lint) rule set in `rock-hero-common/core` that reports every
playability and authoring-quality problem in a chart with actionable, position-anchored messages.
Three consumers: the editor's pre-export validation report, the game's library-scan warnings
(docs/roadmap/26-game-startup-menus-library.md), and a local corpus-calibration harness. Structural
validation (`validateChartRules`) already exists and stays the hard read gate; this plan adds the
advisory content layer above it.

## Non-goals

- No game-side UI. Plan 26 owns how scan warnings render; this plan freezes the headless contract.
- No metadata validation (title/artist/album/art presence) — that is
  docs/roadmap/43-song-information-and-art.md.
- No difficulty/intensity computation — docs/roadmap/11-derived-difficulty-calculator.md.
- No auto-fix or repair transforms. Load normalization and migration ladders belong to
  docs/roadmap/10-format-versioning-and-chart-identity.md.
- No chart-format changes. Every rule here reads the existing model; promoting any lint rule into
  a structural read-rejection is a format-behavior change that routes through plan 10 with a
  corpus impact analysis.
- No live-while-editing lint UI beyond exposing a cheap re-runnable entry point; interaction
  design for in-editor feedback belongs to docs/roadmap/40-chart-editing.md.

## Constraints

- (a) **Layering**: rules live in `rock-hero-common/core`; common never depends on editor or game
  code; editor and game never depend on each other. Both products consume the same headers.
- (b) **Public-header minimalism**: only the lint entry points and finding types go public;
  per-rule helpers stay in implementation files
  (docs/design/architectural-principles.md, "Ports and Adapters", "Core Modules").
- (c) **Naming firewall**: the commercial real-guitar game that inspired this project is never
  named; use "RS" or neutral phrasing. Charter (BSD 3-Clause) may be named.
- (h) **Builds**: all build/test/lint commands go through `.agents/rockhero-build.ps1` (usage in
  `.agents/README.md`) — never raw cmake/ctest/ninja. Intermediate phases run only the checks
  their changes warrant; the final acceptance phase is the sanctioned bundle as separate
  invocations.
- **Corpus is local-only**: the 39-package .rock corpus is converted commercial content — used
  for local calibration only, never committed, never wired into CI
  (docs/roadmap/23-detection-verification-harness.md corpus strategy; docs/roadmap/00-roadmap.md).
- **Headless and automated-testable**: lint code is pure domain logic; narrow `juce_core`
  utilities only where the existing package code already uses them
  (docs/design/architectural-principles.md, "Core Modules").

## Current state inventory

Structural validation exists and is a hard gate:

- `rock-hero-common/core/include/rock_hero/common/core/chart/chart_rules.h` — `ChartErrorCode`
  (9 codes), `ChartError`, `validateChartRules(const Chart&, const TempoMap&)`; returns the
  FIRST violation only. Constants: `g_max_chart_strings = 8`, `g_max_fret = 30` (public);
  `g_max_capo = 12`, cent-offset cap ±1200.0 (private in
  `rock-hero-common/core/src/chart/chart_rules.cpp:14-17`).
- Enforced rules (`chart_rules.cpp:33-227`): usable tuning (1..8 strings, each non-empty; capo
  0..12; |centOffset| ≤ 1200); template `frets`/`fingers` arrays match string count, template
  frets in 0..30; notes sorted by (position, string) with unique onsets, valid grid positions,
  string/fret in range, non-negative sustain; `touch` only on harmonics, in (0, 30]; bend offsets
  ascending within sustain; slide offsets strictly positive, ascending, within sustain, frets in
  range; shape spans positive, sorted, `chord` index in range; FHPs fret 1..30, width ≥ 1,
  sorted; sections non-empty type, sorted.
- Package read HARD-REJECTS structural violations: chart load + `validateChartRules` failures
  become `SongPackageError{InvalidArrangement}` in
  `rock-hero-common/core/src/package/rock_song_package_read.cpp:730-745`. The GP importer also
  validates post-build (`rock-hero-editor/core/src/project/gp_chart_builder.cpp:740`).
- Tone-track structural validation:
  `rock-hero-common/core/src/tone/tone_track_rules.cpp` checks canonical unique region ids,
  valid endpoints, start-before-end, not past the terminal anchor, sorted/non-overlapping,
  canonical tone-document refs. It does NOT require gap-free coverage — regions with gaps pass
  validation; only the editor-side edits keep coverage (`CannotRemoveOnlyRegion` in
  `tone_track_rules.h:43-44`, coverage-safe delete in `tone_track_edits`).

Gaps this plan fills (verified absent):

- No endpoint arithmetic anywhere: nothing computes "position + sustain → grid position".
  `Fraction` (`rock-hero-common/core/include/rock_hero/common/core/timeline/fraction.h`) has
  comparison and `toDouble()` but NO `+`/`-` operators.
  `docs/in-progress/note-format-and-tablature-plan.md` (Open Questions) records same-string
  sustain overlap as unvalidated for exactly this reason.
- No note-name → pitch parser anywhere in common. Tuning strings ("E2") are validated only as
  non-empty (`chart_rules.cpp:44-53`); `"X9"` passes. A private MIDI→name formatter exists in
  `rock-hero-editor/core/src/project/gp_chart_builder.cpp:92` (`midiNoteName`).
- Chart positions are not bounded by the tempo map's terminal anchor (`isValidGridPosition`,
  `chart_rules.cpp:19-24`), unlike tone regions (`tone_track_rules.cpp:73-79`). Notes past the
  terminal anchor load silently.
- Dangling chord-template references are ALREADY a structural error (`shape.chord` out of range,
  `chart_rules.cpp:167`). The remaining lint scope is unused templates and note-vs-template
  disagreement, which `docs/in-progress/note-format-and-tablature-plan.md` explicitly anticipates
  ("Validation can advise when sounding notes under a span disagree with the template posture").
- No lint/advisory layer, no all-findings collection, no editor validation report, no CLI or
  corpus harness. `rock-hero-game/` is a build-system skeleton (placeholder libs).
- Chart files write `formatVersion: 1` (`rock-hero-common/core/src/chart/chart_document.cpp:505`)
  but no reader validates it — plan 10's concern, noted here because rule-id stability follows
  the same discipline.
- Tests: `rock-hero-common/core/tests/test_chart.cpp` covers the structural rules; the test
  target is `rock_hero_common_core_tests`
  (`rock-hero-common/core/tests/CMakeLists.txt`). Grid-token formatting for messages already
  exists (`formatGridPositionToken`, reused by `chart_rules.cpp:26-29`).
- Corpus: 39 converted RS-format packages with linked charts under the local
  `Rock Hero Stuff/Chart References` folder (outside the repo), with `_conversion_report.json`
  statistics (docs/in-progress/note-format-and-tablature-plan.md, "Corpus validation").

Verified against code on 2026-07-06, refactor @ 13e82fb0.

## Dependencies

- Depends on: nothing hard. All phases build on current `rock-hero-common/core`.
- Consumed by:
  - docs/roadmap/40-chart-editing.md — live-feedback hooks call the Phase 3/4 entry points; the
    Phase 1 endpoint arithmetic is also 40's link/merge prerequisite.
  - docs/roadmap/26-game-startup-menus-library.md — library scan stores/reports findings keyed by
    the Phase 4 stable rule tokens; load failures already carry `SongPackageError` reasons.
  - docs/roadmap/43-song-information-and-art.md — the pre-export gate (Phase 6) composes with 43's
    metadata validation; 43 owns the publish-vs-save split decision this plan defers to.
  - docs/roadmap/11-derived-difficulty-calculator.md — reuses Phase 1 endpoint/linearization
    arithmetic for density windows.
  - docs/roadmap/22-note-detection.md — reuses the Phase 2 note-name → pitch utility for the tuner
    (cents vs arrangement tuning including capo and centOffset).
  - docs/roadmap/28-practice-mode.md — motivates the "no sections" sanity finding (section loops).
- External decisions: 43's export-validation model (blocking vs advisory) — see Q3.

## Decisions already made

- Shared validation rules live in `rock-hero-common/core`
  (docs/design/architectural-principles.md, "Core Modules": "shared song, chart, arrangement,
  validation, timing, and package rules in `common/core`").
- Chart validation is a named example of the pure unit tests that should dominate the suite
  (docs/design/architectural-principles.md, "Preferred Kinds of Tests", §1).
- Recoverable cross-boundary failures use domain-owned typed values with stable codes; callers
  never parse message text (docs/design/architectural-principles.md, "Typed Boundary Errors").
- The structural rule set is corpus-validated and stays a hard read gate
  (docs/in-progress/note-format-and-tablature-plan.md, "Corpus validation (2026-07-06)").
- Shapes are advisory notation; "validation can advise when sounding notes under a span disagree
  with the template posture" (docs/in-progress/note-format-and-tablature-plan.md, Format
  Specification commentary).
- Same-string sustain-overlap semantics were deliberately left open until the endpoint
  arithmetic exists (docs/in-progress/note-format-and-tablature-plan.md, "Open Questions").
- Difficulty is derived, never authored; one true tab per arrangement
  (`rock-hero-common/core/include/rock_hero/common/core/chart/chart.h:326-331`;
  docs/in-progress/note-format-and-tablature-plan.md, "Deliberately absent").
- Feature folders earn their folder at the first file; folders are navigation-only, no namespace
  impact (docs/design/architectural-principles.md, "Feature Folders") — justifies the new
  `lint/` feature folder.

## Open questions for the user

- **Q1 — Same-string sustain-overlap semantics.** When a note's sustain runs strictly past the
  next onset on the same string: (a) structural reject at read; (b) truncate on load
  (normalizer); (c) lint Warning only. Endpoint-equals-next-onset is legal adjacency (linked
  slide runs) and is never flagged. **Recommendation: (c)** — existing packages must keep
  loading; official-DLC conversions carry millisecond link gaps and repair belongs to plan 10's
  ladder if calibration shows real overlap volume. Phase 5 reports the corpus frequency before
  the severity is frozen.
- **Q2 — Corpus harness form.** (a) env-gated Catch2 corpus suite inside
  `rock_hero_common_core_tests` (`[corpus-lint]` tag, skipped when `ROCKHERO_CORPUS_DIR` is
  unset); (b) a standalone `tools/chart-lint` console executable; (c) both.
  **Recommendation: (a) first** — zero new targets, `-RunTouchedTests` picks it up locally, CI
  never sees the corpus; add (b) later only if ad-hoc workflows (batch-linting third-party
  packages) demand a CLI. Recorded because (b) would create the repo's first `tools/` bucket.
- **Q3 — May lint findings ever block?** Editor behavior when the report contains findings:
  (a) advisory-only, always saveable; (b) Warning-and-above blocks a future explicit "publish"
  action (not save). **Recommendation: (a) now** — the current model is save==publish with
  normalize-don't-reject, and blocking semantics belong to 43's publish-split decision; this
  plan ships the report advisory-only and revisits under
  docs/roadmap/43-song-information-and-art.md.

## Phased implementation

### Phase 1 — Endpoint arithmetic in `timeline/`

The prerequisite everything position-window-shaped needs (overlap, FHP windows, coverage), and
the same arithmetic plan 40's link/merge and plan 11's density windows need.

- Scope: add `Fraction` addition/subtraction (normalized, same int-overflow discipline the
  comparison uses); add a new public header
  `rock-hero-common/core/include/rock_hero/common/core/timeline/grid_arithmetic.h` with
  `src/timeline/grid_arithmetic.cpp`:
  - `BeatCoordinate` — exact linearized position: `std::int64_t` global beat index (via
    `TempoMap::globalBeatIndex`) plus in-beat `Fraction`; totally ordered.
  - `toBeatCoordinate(GridPosition, const TempoMap&)`,
    `advanceByBeats(GridPosition, Fraction, const TempoMap&) -> GridPosition` (walks measures
    with `beatsPerMeasureAt`), and `noteEndPosition(const ChartNote&, const TempoMap&)`.
- Files/modules: `fraction.h` (edit), new `timeline/grid_arithmetic.{h,cpp}`, new
  `tests/test_grid_arithmetic.cpp`; `rock-hero-common/core` CMake source lists.
- Public-header impact: one new public header; additive `Fraction` operators.
- Testing: pure unit tests — cross-measure advance under mixed signatures (7/4 alternating 4/4,
  the corpus-hostile case), fraction denominators 5/7/9/12/16 from the corpus findings,
  ordering/round-trip properties, sustain past the final measure.
- Exit criteria: all new tests pass; no behavior change anywhere else.
- Verification:

  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Configure -Targets all
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
  ```

### Phase 2 — Note-name pitch utility in `chart/`

- Scope: new public header
  `rock-hero-common/core/include/rock_hero/common/core/chart/note_pitch.h` +
  `src/chart/note_pitch.cpp`:
  - `parseNoteName(std::string_view) -> std::optional<int>` (MIDI number; letters A–G, optional
    `#`/`b`, octave; "E2" → 40) and `formatNoteName(int) -> std::string` (sharps spelling,
    matching the GP importer's `midiNoteName` table).
  - `soundingMidiNote(const ChartTuning&, int string, int fret) -> std::optional<int>` =
    parsed open pitch + capo + fret; `centOffset` stays a separate additive detail for
    consumers that need frequency (plan 22's tuner).
- Optional cleanup, same phase: switch `gp_chart_builder.cpp:92` to the shared formatter so the
  two spellings cannot drift (editor depends on common — allowed).
- Public-header impact: one new public header.
- Testing: `tests/test_note_pitch.cpp` — parse/format round trip, flats, rejection cases,
  capo arithmetic.
- Exit criteria: parser rejects every malformed name; GP importer tests still pass if the
  cleanup is taken.
- Verification: same two commands as Phase 1 (`-Configure` because CMake source lists change).

### Phase 3 — Lint engine and rule set v1 in `lint/`

- Scope: new feature folder `rock-hero-common/core/include/rock_hero/common/core/lint/`:
  - `lint_finding.h` — `LintSeverity {Error, Warning, Info}`; `ChartLintRule` enum with STABLE
    ids (append-only, never renumbered — consumers persist them);
    `LintFinding {rule, severity, optional<GridPosition> position, optional<int> string,
    std::string message}`; `LintReport = std::vector<LintFinding>`.
  - `chart_lint.{h,cpp}` — `lintChart(const Chart&, const TempoMap&, const ChartLintOptions&)`
    collecting ALL findings (deliberate contrast with `validateChartRules`' first-error gate).
    Messages reuse `formatGridPositionToken` so every finding names its `m:b(+n/d)` position.
  - `tone_track_lint.{h,cpp}` — `lintToneTrack(const ToneTrack&, const TempoMap&)`.
- Rule set v1 (default severities; Phase 5 calibrates before freeze):

  | Token | Rule | Default |
  |---|---|---|
  | `sustain_overlap` | same-string sustain strictly past next onset (endpoint == onset is legal adjacency) | Warning (Q1) |
  | `fret_behind_capo` | fretted note with 1 ≤ fret ≤ capo (capo occupies those frets) | Warning |
  | `open_string_bend` | bend payload on fret 0 | Warning |
  | `slide_to_open` | slide waypoint targeting fret 0 | Warning |
  | `impossible_span` | fretted-span width over threshold: simultaneous notes at one onset, chord-template posture, or FHP width | Warning |
  | `unused_template` | chord template never referenced by any shape | Info |
  | `shape_note_mismatch` | notes under a shape span on strings the template marks null | Info |
  | `fhp_coverage_gap` | fretted note before the first FHP | Warning |
  | `fret_outside_fhp` | onset fret outside the active FHP window [fret, fret+width-1] | Info |
  | `section_sanity` | no sections at all; or duplicate same-position sections | Info |
  | `empty_chart` | chart carries zero notes | Info |
  | `past_terminal_anchor` | note/shape/FHP/section position or endpoint past the tempo map's terminal anchor | Warning |
  | `tuning_name_unparseable` | tuning string fails Phase 2's parser | Warning |
  | `tone_coverage_gap` | tone regions leave a gap in [1:1, terminal anchor] | Warning |

  `ChartLintOptions` carries the span threshold model only (default: flag fretted spans > 5
  frets at or below fret 11, > 6 above — hand span grows up the neck; exact numbers are Phase 5
  calibration outputs, not commitments). Slide waypoints are excluded from FHP-window checks in
  v1 (glides legitimately leave the window mid-sustain).
- Files/modules: the four `lint/` headers/TUs, `tests/test_chart_lint.cpp`,
  `tests/test_tone_track_lint.cpp`, CMake source lists.
- Public-header impact: three new public headers (`lint_finding.h`, `chart_lint.h`,
  `tone_track_lint.h`).
- Testing: per rule — one triggering case, one boundary non-triggering case (especially
  endpoint-adjacency for `sustain_overlap` and window edges for `fret_outside_fhp`), plus one
  multi-violation chart proving all findings are collected in position order.
- Exit criteria: every v1 rule implemented and tested; `validateChartRules` untouched; no read
  path change.
- Verification: same two commands as Phase 1.

### Phase 4 — Arrangement/song composition and the frozen consumer contract

- Scope: `lint/arrangement_lint.{h,cpp}` — `lintArrangement(const Arrangement&, const TempoMap&)`
  merges chart + tone-track findings (chartless arrangements lint tone coverage only);
  `lintSong(const Song&)` returns per-arrangement reports tagged with arrangement id and part.
  Add `ruleToken(ChartLintRule) -> std::string_view` — the stable snake_case tokens from the
  Phase 3 table — for machine consumers (plan 26's library cache, CI logs). Document in the
  header: tokens and enum values are append-only; removal or renumbering follows the same
  versioning discipline docs/roadmap/10-format-versioning-and-chart-identity.md defines for
  format changes.
- Public-header impact: one new public header.
- Testing: `tests/test_arrangement_lint.cpp` — merge ordering, arrangement tagging, token
  round-trip stability (static-assert style enumeration test that fails on renumbering).
- Exit criteria: a headless consumer (the game's scan, plan 26 Phase TBD) can obtain
  `lintSong` results from a `readRockSongPackage` result with no editor code; contract frozen.
- Verification: same two commands as Phase 1.

### Phase 5 — Corpus calibration harness (local-only) — STOP checkpoint

Assumes Q2 outcome (a); adjust mechanically if the user picks (b) or (c).

- Scope: `tests/test_corpus_lint.cpp` in `rock-hero-common/core/tests`, tagged `[corpus-lint]`
  and self-skipping unless `ROCKHERO_CORPUS_DIR` is set. When set: recursively find `*.rock`,
  `readRockSongPackage` each, run `lintSong`, and emit a per-rule frequency table plus the worst
  offenders per rule (package, arrangement, position) to stdout and to a report file beside the
  corpus. CI never sets the variable; the suite stays green without it.
- Calibration meaning: the corpus is converted official DLC and quality CDLC — professionally
  playable content. Any rule firing broadly across it is miscalibrated (threshold wrong or the
  technique is legal), and official-content frequency is the evidence for Q1's severity answer
  and the `impossible_span` threshold numbers.
- Files/modules: one test file; CMake test source list.
- Testing: the harness IS the test; plus one unit test that the suite skips cleanly when the
  env var is unset.
- Exit criteria: report generated over all 39 packages with zero crashes; **STOP — present the
  frequency report and proposed final severity/threshold table for sign-off before Phase 6.**
- Verification:

  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Configure -Targets all
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
  # Local calibration run (never CI):
  $env:ROCKHERO_CORPUS_DIR = 'C:\path\to\Rock Hero Stuff\Chart References'
  $exe = Get-ChildItem build\debug -Recurse -Filter rock_hero_common_core_tests.exe | Select-Object -First 1
  & $exe.FullName "[corpus-lint]"
  ```

### Phase 6 — Editor pre-export validation report (assumes Q3 outcome (a): advisory)

- Scope: an explicit "Validate charts" editor action producing a findings report; save is never
  blocked. Editor-core runs `lintSong` on the session's song and exposes a report view state
  (findings grouped by arrangement, each with rule token, severity, position token, message);
  editor-ui renders the list in a dismissable panel/dialog. Position tokens make messages
  actionable; seek-on-click is a stretch item deferred to plan 40's interaction work.
- Files/modules (re-verify at execution — editor-core tone handlers are in flight on this
  branch): new action id in `rock-hero-editor/core/src/controller/editor_action_id.h` wiring;
  new `lint` feature folder in editor/core (`lint_report_view_state.h` + handler TU following
  the existing per-domain handler pattern); new editor/ui component under
  `rock-hero-editor/ui/src/lint/`. No changes to save flow semantics in
  `rock-hero-editor/core/src/project/project_handlers.cpp` beyond an optional post-save summary
  line when findings exist.
- Public-header impact: editor-core view-state header only; nothing in common changes.
- Testing: editor-core unit test — action produces the expected report view state from a song
  fixture with known violations; UI wiring follows the existing listener-test pattern where one
  exists.
- Exit criteria: user can run validation on an open project and read every finding with its
  position; undo history is untouched (lint is read-only analysis).
- Verification:

  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Configure -Targets all
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
  ```

## Final acceptance phase

Run the sanctioned bundle as separate invocations, then pre-commit:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets clang-tidy
pre-commit run --all-files
```

Plus one local (never CI) corpus pass per the Phase 5 command block confirming zero crashes and
the signed-off severity table.

## Rollback/abort notes

- Phases 1–4 are purely additive (new headers, new TUs, additive `Fraction` operators): abort =
  remove the new files and their CMake list entries; no package, read-path, or serialization
  behavior changes anywhere, so no corpus or compatibility risk.
- The one standing risk is scope creep from lint into the read gate: promoting any rule to a
  structural rejection changes which existing packages load and MUST route through
  docs/roadmap/10-format-versioning-and-chart-identity.md with a corpus impact analysis — never do
  it inside this plan.
- Phase 5 writes nothing into corpus packages (read + report only); a bad run is discarded.
- Phase 6 sits behind a single editor action; rollback is removing the action wiring and the
  report component. If the in-flight editor-core tone work has reshaped the handler layout,
  re-verify the touchpoints listed there before starting rather than force-fitting these paths.
