# 23 — Detection Verification Harness

**Status**: Ready (execution begins after docs/plans/roadmap/22-note-detection.md Phase 1 lands the
DetectionEvent contract); 2026-07-06; baseline `refactor @ 3c7febe0`.

## Goal

A solo developer can change detection DSP and know, without picking up a guitar, whether accuracy
got better or worse. Every detection tweak is measured against committed fixtures in CI and against
the local corpus on demand — per-technique precision/recall and latency distributions, never vibes.
Scoring (docs/plans/roadmap/24-scoring-star-power-failure.md) is testable by deterministic replay of
serialized DetectionEvent logs, and an autoplay bot produces perfect event streams for end-to-end
soak runs and demos.

## Non-goals

- Choosing or implementing detection algorithms — that is docs/plans/roadmap/22-note-detection.md.
- Defining what the metrics *are* (tolerance windows, technique classes) — 22 owns the metric
  definitions; this plan builds the machinery that computes, reports, and regression-gates them.
- Scoring rules, hit windows, or the provisional-hit state machine —
  docs/plans/roadmap/24-scoring-star-power-failure.md.
- Committing or shipping any converted commercial content. The 39-package .rock corpus and the
  101-file GP corpus remain local-only soak assets forever.
- A general-purpose audio test framework. This harness serves detection verification and scoring
  replay; nothing more.

## Constraints

- (a) **Layering**: common never depends on editor or game code; editor and game never depend on
  each other. Anything both products need is extracted to rock-hero-common FIRST, as its own phase
  with tests. Tracktion headers stay isolated to rock-hero-common/audio implementation files.
- (b) **Public-header minimalism**: only headers that must be public are public; ports-and-adapters
  per docs/design/architectural-principles.md.
- (c) **NAMING FIREWALL**: the commercial real-guitar game that inspired this project is never
  named in any file; use "RS"/neutral phrasing. Charter (BSD 3-Clause) may be cited by name.
- (e) **FLAC** is the enforced package audio format; committed fixture audio is FLAC.
- (h) **Builds**: all build/test/lint commands go through `.agents/rockhero-build.ps1` (usage in
  `.agents/README.md`) — never raw cmake/ctest/ninja. Intermediate phases run only the checks their
  changes warrant. The final acceptance phase is the sanctioned bundle as separate invocations.
- (i) **Real guitar input** — fixtures are DI recordings of real guitar or synthetic renders that
  approximate one; no plastic-controller assumptions anywhere.
- Plan-normative rules this document establishes:
  - **Scoring consumes events, never audio.** The serialized DetectionEvent stream is the only
    seam between detection and scoring tests. Any scoring test that decodes audio is wrong.
  - **Corpus firewall.** Converted commercial content (local .rock corpus, GP corpus, DI takes
    recorded against those songs) never enters git, CI, or any committed baseline. CI fixtures are
    self-authored, freely-licensed songs plus synthetic chart generators only.
  - **Determinism.** Offline detection runs, autoplay bot output, synthetic renders, and metric
    computation are deterministic for a fixed input + engine version + seed. Flaky detection tests
    are treated as bugs in the harness.

## Current state inventory

- `rock-hero-game/` is a build-system skeleton: `rock-hero-game/app/main.cpp` is an 81-line JUCE
  DocumentWindow shell; `rock-hero-game/core/src/placeholder.cpp`,
  `rock-hero-game/audio/src/placeholder.cpp`, and `rock-hero-game/ui/src/placeholder.cpp` are
  placeholder static libs with `.gitkeep` include dirs. No game test targets exist yet;
  docs/design/architecture.md ("Testing Infrastructure") already names
  `rock-hero-game/core/tests/`, `rock-hero-game/audio/tests/`, `rock-hero-game/ui/tests/` as the
  intended layout.
- No detection or DSP code exists anywhere in the repo. No `IPitchDetector`, no analysis thread,
  no MIDI infrastructure. `conanfile.txt` deps: cmake-package-builder, catch2/3.13.0,
  libebur128/1.2.6, quill/11.1.0, ogg/1.3.5, vorbis/1.3.7.
- Chart model: `rock-hero-common/core/include/rock_hero/common/core/chart/chart.h` — `ChartNote`
  carries position (`GridPosition`: measure/beat + exact `Fraction` offset), string, fret, sustain,
  attack (Pick/Hammer/Pull/Tap/Pop/Slap), mute (None/Palm/Full), harmonic (None/Natural/Pinch),
  optional `touch`, vibrato, tremolo, accent, `bend` (BendPoint offset+semitones), `slides`
  (SlideWaypoint offset+fret+unpitched). `ChartTuning` = strings[], capo, cent_offset.
  `chart_document.h` exposes `parseChartDocument` / `readChartDocument` / `writeChartDocument`;
  `chart_rules.h` has `g_max_chart_strings{8}`, `g_max_fret{30}` and typed `ChartError`.
- Tempo map: `rock-hero-common/core/include/rock_hero/common/core/timeline/tempo_map.h` —
  `TempoMap::secondsAtNote(int measure, int beat, Fraction offset)` resolves any chart position to
  absolute seconds; anchors keep a fixed millisecond grid (docs/design/architecture.md, "Song Data
  Model": at most ±0.5 ms quantization, below the onset-detection floor).
- Test conventions: per-library Catch2 v3 targets registered with `catch_discover_tests`
  (e.g. `rock-hero-common/core/tests/CMakeLists.txt`). Test-support helpers live in module-owned
  `*_testing` INTERFACE targets — proven by `rock_hero_editor_core_testing`
  (`rock-hero-editor/core/tests/CMakeLists.txt`) and `rock_hero::common::audio_testing`. All test
  audio today is synthesized in-memory through JUCE writers
  (`rock-hero-common/audio/tests/include/rock_hero/common/audio/testing/audio_fixtures.h`); the
  repo has zero committed binary audio fixtures.
- No test or tool references any local corpus path today (verified by grep across all `tests/`
  trees); corpus mentions appear only in docs.
- CI: `.github/workflows/build.yml` runs pre-commit, CMake/Conan build+test, static analysis, and
  docs. Catch2 hidden-tag tests (tags beginning `[.`) are excluded from default runs, and
  `.agents/rockhero-build.ps1 -RunTouchedTests` runs built `*_tests.exe` with no tag filter, so
  hidden-tag tests stay local-only by construction.

Verified against code on 2026-07-06, refactor @ 3c7febe0.

## Dependencies

- docs/plans/roadmap/22-note-detection.md Phase 1 (DetectionEvent contract, technique detectability
  matrix, metric definitions: technique classes, tolerance windows, latency-distribution spec) —
  required before Phases 1, 2, and 6 of this plan.
- docs/plans/roadmap/22-note-detection.md detection-pipeline phases — required before Phases 6 and 7 here
  can run real detection. This plan places an explicit contract demand on 22: the pipeline core
  must expose an **offline synchronous entry point** (buffers in, events out, no FIFO, no threads,
  no audio device) so the runner is deterministic.
- docs/plans/roadmap/10-format-versioning-and-chart-identity.md (chart-identity hash) — the event-log
  header carries the hash; the field is nullable until 10 lands.
- Consumers (reverse dependencies, recorded in both plans): docs/plans/roadmap/24-scoring-star-power-failure.md
  (replay seam + autoplay bot for scoring tests), docs/plans/roadmap/29-online-leaderboards.md (its
  stability gate is measured by this harness's metric reports),
  docs/plans/roadmap/20-game-architecture-and-render-stack.md (dev-diagnostics autoplay toggle consumes
  the Phase 2 bot), docs/plans/roadmap/28-practice-mode.md (per-section accuracy reuses the Phase 6
  matcher/metrics vocabulary).
- External decision: fixture licensing and recording plan (Open questions 1–3).

## Decisions already made

- A replayable headless simulation layer is a first-class architectural objective — it must run
  with a Song/Arrangement, synthetic transport positions, synthetic pitch and onset detections,
  and calibration offsets, and verify hit/miss/score behavior without real hardware:
  docs/design/architectural-principles.md, "Add a Replayable Simulation Layer".
- Pitch detection, onset detection, and calibration capture belong in `rock-hero-game/audio`;
  note matching, scoring, and simulation belong in `rock-hero-game/core`:
  docs/design/architectural-principles.md, "Audio Modules" and "Core Modules".
- Detection runs on the clean pre-effects signal, on a dedicated analysis thread fed by a
  lock-free ring buffer; all scoring comparisons happen in audio-sample time with calibration
  offsets applied: docs/design/architecture.md, "Threading Model" and "Timing and Latency".
- Fake-first testing; per-library Catch2 test targets; simulation-style tests for gameplay and
  timing are "strongly preferred": docs/design/architectural-principles.md, "Testing Techniques"
  and "CMake and Test Layout"; docs/design/architecture.md, "Testing Infrastructure".
  Trompeloeil stays deferred per docs/plans/todo/trompeloeil-adoption-plan.md.
- Product core modules may use narrow `juce_core` utilities (files, JSON, strings) while staying
  headless and automated-testable: docs/design/architecture.md, "JUCE utility dependency in core
  modules" — the event-log serializer relies on this permission.
- Derived-over-authored (constraint d context): difficulty and other descriptors are computed,
  never hand-authored — the harness's ground truth is the chart itself plus a validated take, not
  hand-labeled per-onset annotations (docs/design/architecture.md, "Song Data Model").

## Open questions for the user

1. **Fixture-song license.** Committed test songs and DI takes need an explicit license. Options:
   (a) CC0/public-domain dedication for everything under the fixture tree; (b) same AGPLv3 as the
   repo. **Recommendation: (a) CC0** — fixtures are not product code, and CC0 removes every
   downstream question (forks, CI mirrors, papers citing accuracy numbers).
2. **Committed-audio budget and mechanism.** Options: (a) plain git, soft budget ≤ 30 MB of FLAC,
   hard cap 50 MB; (b) Git LFS from day one; (c) a separate fixtures submodule.
   **Recommendation: (a) plain git with the 30 MB budget** — mono 16-bit 44.1 kHz DI FLAC is
   ~1–1.5 MB/min, so the v1 corpus fits comfortably; revisit LFS only if the corpus outgrows the
   budget (Rollback notes).
3. **Who records the DI corpus.** Options: (a) you self-author 3–5 short songs/etudes and record
   DI takes per the Phase 5 protocol; (b) source freely-licensed third-party DI recordings and
   chart them. **Recommendation: (a)** — you control tuning/technique coverage and alignment, and
   licensing is unambiguous; (b) adds vetting and charting burden for material we don't control.
4. **Baseline update policy.** When detection improves, committed metric baselines must move.
   Options: (a) a manual regeneration step (test runs with an env flag that rewrites baseline
   JSON; the diff is reviewed and committed deliberately); (b) auto-ratchet in CI.
   **Recommendation: (a)** — auto-ratchet hides accidental metric-definition changes and makes
   bisecting regressions harder.

## Phased implementation

Phase order: 1 → 2 → 3 → 4 can proceed as soon as 22 Phase 1 lands (3 and 4 need only the chart
model, available today). Phase 5 is recording work that can run in parallel from Phase 3 onward.
Phase 6 needs 22's pipeline; Phase 7 needs Phase 6.

### Phase 1 — Serialized DetectionEvent stream and replay reader

- **Scope**: the versioned wire format for detection event logs, plus writer and reader. Format:
  JSON Lines — one header object, then one object per event. Header fields: `detectionLogVersion`
  (starts at 1), `engineVersion` (detection-engine version id from 22), `sampleRate`,
  `chartIdentityHash` (nullable until docs/plans/roadmap/10-format-versioning-and-chart-identity.md
  lands), `arrangementId`, calibration offsets as applied (contract from
  docs/plans/roadmap/13-audio-device-settings-and-calibration.md). Event records mirror 22's
  DetectionEvent variants (onset, pitch(es)+confidence, sustained-pitch tracking updates,
  percussive-mute classification) with timestamps as int64 audio-sample indices — never seconds,
  never render-frame time (docs/design/architecture.md, "Timing and Latency"). Reader rule:
  unknown fields are ignored, unknown event kinds are skipped with a counted warning, so old
  logs replay under newer readers. Writer rule: fixed key order and shortest-round-trip float
  formatting so identical runs produce byte-identical logs (committed golden logs diff cleanly).
- **Files/modules**: `rock-hero-game/core` gains its first real code: feature folder
  `include/rock_hero/game/core/detection/` + `src/detection/` (`detection_event_log.h/.cpp`),
  beside 22's event types (verify 22's final placement before creating the folder; if 22 placed
  the types elsewhere in game/core, co-locate with them). New test target
  `rock-hero-game/core/tests/` (`rock_hero_game_core_tests`) following
  `rock-hero-common/core/tests/CMakeLists.txt`; delete `placeholder.cpp` only when the lib gains
  its first real TU, per the root-holds-folders-only rule
  (docs/design/architectural-principles.md, "Library Roots Hold Folders Only").
- **Public-header impact**: `detection_event_log.h` is public in game/core (scoring tests,
  game/audio tests, and the app's diagnostics all consume it). Nothing new in common.
- **Testing plan**: round-trip tests (write → read → equal event sequence); unknown-field and
  unknown-event-kind tolerance; byte-stability test (two writes of the same sequence are
  identical); a committed tiny golden log proving format stability across refactors. Lives in
  `rock-hero-game/core/tests/test_detection_event_log.cpp`.
- **Exit criteria**: 24's scoring tests can be written against replayed logs with zero audio
  involvement; golden log committed.
- **Verification** (new CMake targets → configure):

  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Configure -Targets rock_hero_game_core_tests -RunTouchedTests
  ```

### Phase 2 — Autoplay bot

- **Scope**: `AutoplayEventSource` in game/core: `Chart` + `TempoMap` + a profile → a
  DetectionEvent stream identical in shape to real detector output. Onset seconds come from
  `TempoMap::secondsAtNote(measure, beat, offset)`; technique-to-event mapping follows 22's
  detectability matrix (palm/full mutes → percussive classification, bends/slides/vibrato →
  sustained-pitch trajectory samples at a configurable rate, chords → simultaneous events).
  Profiles: `Perfect` (zero jitter — demos, soak, 20's autoplay toggle) and `Humanized` (seeded
  RNG: gaussian timing jitter, fixed latency offset, miss probability, confidence spread) for
  scoring-robustness tests in 24. Deterministic for a fixed seed.
- **Files/modules**: `rock-hero-game/core` `detection/autoplay_event_source.h/.cpp`; tests in
  `rock-hero-game/core/tests/test_autoplay_event_source.cpp`.
- **Public-header impact**: public in game/core — the game app's dev-diagnostics autoplay toggle
  (docs/plans/roadmap/20-game-architecture-and-render-stack.md) consumes it at runtime, not only tests.
- **Testing plan**: event sample-times match `secondsAtNote` conversions within one sample; chord
  onsets are simultaneous; every technique class in the detectability matrix produces its mapped
  event kind; same seed → identical stream; serialized bot output round-trips through Phase 1.
- **Exit criteria**: 24 can build its provisional-hit and scoring tests entirely on bot streams;
  a bot log for a generated chart is committed as a scoring-test fixture.
- **Verification**:

  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets rock_hero_game_core_tests -RunTouchedTests
  ```

### Phase 3 — Shared chart fixture generators (common extraction first)

- **Scope**: programmatic chart generators for controlled sweeps, usable by game tests (this
  plan), editor validation tests (docs/plans/roadmap/42-chart-validation.md), and calculator tests
  (docs/plans/roadmap/11-derived-difficulty-calculator.md). Generators: chromatic/scale runs (string
  range, fret range, note value, tempo), technique-isolation etudes (one technique repeated N
  times: bends with curve shapes, pitched/unpitched slides, palm/full mutes, hammer/pull, tap,
  harmonics natural+pinch with `touch`, vibrato, tremolo, accents), chord-progression builder
  over `ChordTemplate`s, tuning presets spanning the real corpus spread (E standard, Eb, drop D,
  drop C, B standard; capo and `cent_offset` variants), and a tempo-sweep wrapper that re-emits
  any generated chart across a BPM range. All outputs satisfy `chart_rules.h` validation.
- **Files/modules**: per constraint (a) this is a common-first extraction: new
  `rock_hero::common::core_testing` INTERFACE target in `rock-hero-common/core/tests/CMakeLists.txt`
  (mirroring `rock_hero_editor_core_testing`), headers under
  `rock-hero-common/core/tests/include/rock_hero/common/core/testing/chart_generators.h` (+
  `chart_builders.h` for the raw builder vocabulary).
- **Public-header impact**: none in production headers — test-support INTERFACE target only,
  consumed via explicit link per the existing `*_testing` convention.
- **Testing plan**: `rock-hero-common/core/tests/test_chart_generators.cpp` — every generator
  output passes chart validation; note ordering invariant (sorted by position, string) holds;
  tuning presets respect `g_max_chart_strings`.
- **Exit criteria**: game and editor test targets can link `rock_hero::common::core_testing` and
  generate valid charts without hand-written note lists.
- **Verification** (new target → configure):

  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Configure -Targets rock_hero_common_core_tests -RunTouchedTests
  ```

### Phase 4 — Deterministic plucked-string synthesizer for CI render sweeps

- **Scope**: a self-contained plucked-string synthesizer (extended Karplus-Strong: seeded
  excitation, pick-position comb, per-string damping, palm-mute damping ramp, bend/slide via
  variable delay-line length driven by the chart's bend curves and slide waypoints, vibrato LFO,
  simple harmonic touch approximation) that renders any `Chart` + `TempoMap` to an audio buffer
  in-memory. Purpose: controlled tuning/technique/tempo sweeps in CI with zero committed audio —
  generate chart (Phase 3), render, detect, measure. Explicit honesty rule, stated in the header
  docs: synthetic audio measures *relative* regressions and edge-case coverage; absolute realism
  claims come only from the Phase 5 DI corpus.
- **Files/modules**: test-support only, game-side (detection tests are the sole consumer):
  `rock-hero-game/audio/tests/include/rock_hero/game/audio/testing/plucked_string_synth.h` (+
  `.cpp` if it outgrows header-only; then the testing target becomes a static lib — follow
  whichever shape the code demands, per docs/design/architectural-principles.md placement rules).
  Creates the `rock-hero-game/audio/tests/` target (`rock_hero_game_audio_tests`).
- **Public-header impact**: none in production headers.
- **Testing plan**: determinism (same chart + seed → identical samples); rendered fundamental of
  a single sustained note matches the tuning's expected frequency within a coarse tolerance
  (sanity, not detection); rendered onset count equals chart note count for a plain run.
- **Exit criteria**: a generated chart renders to audio in CI in well under a second per etude;
  Phase 6 can consume renders directly from memory.
- **Verification** (new target → configure):

  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Configure -Targets rock_hero_game_audio_tests -RunTouchedTests
  ```

### Phase 5 — Committed DI fixture corpus (self-authored, freely licensed)

- **Scope**: the real-guitar ground truth CI is allowed to see. Content: 3–5 self-authored short
  songs/etudes (30–60 s each) covering the technique checklist and at least three tunings plus one
  capo and one cent-offset case. Each fixture folder contains: the `.rock` package (chart, tempo
  map, FLAC backing — a click track or minimal bed is fine), one or more DI takes as mono FLAC
  (direct interface capture, no processing), and a per-take manifest JSON:
  `{ takeId, sampleRate, alignmentOffsetSamples, tuningVerified, excludedNotes: [noteIndex...],
  notes, license }`. Ground-truth model: the chart is the annotation; a human validates the take
  was played correctly and lists flubbed notes in `excludedNotes` — no per-onset hand labeling.
  Alignment protocol: record against the package backing/click from a synchronized start; store
  the measured offset; a harness check asserts the first N onsets land within a coarse window of
  their chart positions, catching bad offsets immediately. A `FIXTURES.md` records provenance,
  recording chain, and the license chosen in Open question 1.
- **Files/modules**: `rock-hero-game/audio/tests/fixtures/di-corpus/<fixture>/...` + `FIXTURES.md`;
  manifest reader + alignment check in `rock-hero-game/audio/tests` (test-support code).
- **Public-header impact**: none.
- **Testing plan**: manifest parse tests; alignment sanity check per committed take; package loads
  through the standard reader (FLAC-only rule, constraint e).
- **Exit criteria**: total committed audio within the Open-question-2 budget; every take passes
  the alignment check; licensing recorded. This phase is gated on Open questions 1–3.
- **Verification**:

  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets rock_hero_game_audio_tests -RunTouchedTests
  ```

### Phase 6 — Offline runner, matching, metrics, and regression baselines

- **Scope**: the measurement core. (1) Offline runner: feeds fixture audio (Phase 4 renders,
  Phase 5 takes) through 22's synchronous pipeline entry point at a fixed hop — no threads, no
  FIFO, no device — emitting a Phase 1 event log. (2) Matcher: pairs detected events to
  ground-truth chart notes using 22's tolerance windows (nearest-within-window, one-to-one);
  distinct from 24's scoring state machine by design — this measures detector quality against
  ground truth, scoring judges a player; document the distinction where both live. (3) Metrics
  (definitions owned by 22, computed here): per-technique-class precision/recall/F1; onset-latency
  distribution (detected sample − ground-truth sample): p50/p90/p99/max; pitch error in cents for
  pitched notes; false positives per minute on silence and noise fixtures. (4) Regression gating:
  per-fixture baseline JSON committed beside the fixture (metric floors + margins); tests fail on
  any metric below floor − margin; baseline regeneration via env flag
  `ROCKHERO_UPDATE_DETECTION_BASELINES=1` per Open question 4. (5) Reports: every metrics run
  writes `detection-metrics-report.json` and a human-readable `.md` table under the build dir —
  this artifact is also the measurement instrument for the docs/plans/roadmap/29-online-leaderboards.md
  stability gate.
- **Files/modules**: matcher + metrics as pure code in `rock-hero-game/core`
  `detection/detection_metrics.h/.cpp` (headless, deterministic — game/core per
  docs/design/architectural-principles.md "Core Modules"); runner in
  `rock-hero-game/audio/tests` (it touches the pipeline adapter); baselines under
  `rock-hero-game/audio/tests/fixtures/baselines/`.
- **Public-header impact**: `detection_metrics.h` public in game/core (practice-mode per-section
  accuracy in docs/plans/roadmap/28-practice-mode.md reuses the matcher vocabulary; debug overlays in
  docs/plans/roadmap/25-note-highway-3d.md read the same numbers live).
- **Testing plan**: matcher unit tests on synthetic event/ground-truth pairs (double onsets, close
  neighbors, window edges, excluded notes); metric math tests with hand-computed expectations;
  end-to-end: Phase 3 chart → Phase 4 render → pipeline → metrics ≥ baseline, and Phase 5 takes →
  pipeline → metrics ≥ baseline. CI budget: the default-visible sweep set must add < ~2 minutes to
  the test job; larger sweeps go behind the Phase 7 hidden tag.
- **Exit criteria**: a deliberate detector regression (e.g. widened onset threshold in a scratch
  branch) turns the suite red; baselines committed for every fixture; report artifact produced on
  every run.
- **Verification**:

  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets rock_hero_game_audio_tests rock_hero_game_core_tests -RunTouchedTests
  ```

### Phase 7 — Local-only soak: commercial-derived corpus and optional VST renders

- **Scope**: scale testing on content that never enters git or CI. Catch2 hidden-tag tests
  (`[.local-corpus]`) read `ROCKHERO_CORPUS_DIR`, load every local `.rock` package, and run:
  autoplay-bot stream generation for every chart (exercises Phase 2 across 135 real charts), and
  detection metrics for any local DI takes recorded against corpus songs (same manifest format as
  Phase 5, stored under the corpus dir, never committed). Results go to a local report file only —
  no committed baselines reference corpus content (corpus firewall). Optional sub-phase, executed
  only if DSP tuning demands more realism than Phases 4–5 provide: local render tooling that plays
  chart-derived MIDI through a locally installed guitar VST for sweep realism — carries an
  explicit checkpoint: **verify with juce-tracktion-expert before implementing** (offline render
  of a MIDI clip through an instrument plugin via Tracktion's render facilities), stays behind a
  hidden tag + `ROCKHERO_RENDER_VST_DIR`, and introduces file-level MIDI only inside this local
  tool (unrelated to 24's IMidiTrigger device input — note the distinction in both plans).
- **Files/modules**: `rock-hero-game/audio/tests/test_local_corpus_soak.cpp` (hidden tag);
  optional VST render tool code stays inside the same test target behind its tag.
- **Public-header impact**: none.
- **Testing plan**: the soak *is* the test; a cheap always-on unit test asserts the hidden-tag
  tests are skipped when `ROCKHERO_CORPUS_DIR` is unset (so CI can never accidentally depend on
  local content).
- **Exit criteria**: soak runs clean over all 39 local packages; local report generated. Because
  `-RunTouchedTests` applies no tag filter and Catch2 skips hidden tags by default, the soak is
  invoked by running the built executable directly — the one sanctioned direct-exe exception,
  documented here because the helper has no tag pass-through:

  ```powershell
  $env:ROCKHERO_CORPUS_DIR = "<local corpus path>"
  .\build\debug\rock-hero-game\audio\tests\rock_hero_game_audio_tests.exe "[.local-corpus]"
  ```

- **Verification** (build only; behavior is local-manual):

  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets rock_hero_game_audio_tests
  ```

## Final acceptance phase

Run the sanctioned bundle as separate invocations from the repo root, then pre-commit:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets clang-tidy
pre-commit run --all-files
```

Acceptance additionally requires: golden event log and scoring-fixture bot log committed
(Phases 1–2); all committed fixtures within budget and licensed (Phase 5, gated on Open
questions 1–2); a demonstrated red run from a deliberate detector regression (Phase 6 exit
criterion); zero corpus paths or corpus-derived data in any committed file.

## Rollback/abort notes

- **Phase 1 format churn**: if 22's DetectionEvent contract changes after logs are committed, bump
  `detectionLogVersion` and keep the reader tolerant of version 1 (the reader's
  unknown-field/unknown-kind rules exist precisely so old golden logs keep replaying). Never edit
  committed golden logs in place to match a new writer — regenerate and commit alongside a version
  bump.
- **Phase 4 synth realism disappointment**: if detection behaves qualitatively differently on
  synthetic audio than on DI takes, do not tune the synth toward the detector (circular). Demote
  synthetic sweeps to smoke/edge-case coverage, lean on Phase 5, and consider the Phase 7 VST
  render sub-phase.
- **Phase 5 size blowout**: if the corpus outgrows the budget, first trim take counts (one take
  per technique/tuning cell), then revisit Open question 2's LFS/submodule options. Never trim by
  dropping technique classes silently — coverage gaps get a line in `FIXTURES.md`.
- **Phase 6 flaky baselines**: a baseline that oscillates without code changes indicates
  nondeterminism in the pipeline or runner — treat as a bug (fix seeds/ordering), do not widen
  margins to hide it. If a fixture is legitimately marginal, quarantine it behind a hidden tag
  with a tracking note rather than deleting it.
- **Phase 7**: purely additive and local; abort costs nothing. If the VST render sub-phase proves
  awkward in Tracktion, drop it — Phases 4–5 remain the measurement basis.
