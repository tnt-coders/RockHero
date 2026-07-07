# Plan 28 — Practice Mode (section looping, pitch-preserved slow-down, per-section accuracy)

## 1. Status

Deferred — planned now, executed later; 2026-07-06; baseline `refactor @ 13e82fb0`.
This plan's single NOW requirement is delegated: docs/plans/21-game-audio-engine-and-session.md
Phase 1 and docs/plans/12-playback-clock.md Phases 1–4 must carry a playback speed factor and
survive loop-region seeks from day one. Everything else in this file waits until its dependencies
land.

## 2. Goal

A player who cannot yet play a passage at full speed can practice it: pick a section (or a
contiguous span of sections) from the chart's `ChartSection` markers, loop it, slow the backing
track down with pitch preserved, and see per-section accuracy feedback that improves loop over
loop. The live guitar signal is never stretched — the player always plays in real time through
the same tone rig used in normal gameplay. Practice never fails a player out of a song.

## 3. Non-goals

- No new chart data. Sections already exist in the format; repeat numbering stays derived
  (`chart.h:290`), never authored. No practice-specific authoring in the editor.
- No scoring-rule changes. docs/plans/24-scoring-star-power-failure.md owns verdicts, multipliers,
  and the score-record format; this plan only slices and aggregates its output per section.
- No leaderboard eligibility for practice runs (docs/plans/29-online-leaderboards.md).
- No varispeed (pitch-shifted) playback mode. Tracktion's speed compensation is resampling, not
  stretching (`tracktion_EditPlaybackContext.cpp:266-274, 380-402`), and a pitch-shifted backing
  track would fight the player's real-pitch instrument.
- No automatic step-up trainer ("pass at 70%, bump to 80%") in v1 — listed as a stretch item in
  Phase 6, not committed.
- No editor-side practice features (the editor already has free seek and will get its own
  preview via docs/plans/44-editor-3d-preview.md).

## 4. Constraints

- (a) Layering: common never depends on editor or game code; editor and game never depend on each
  other. Anything both products need is extracted to rock-hero-common FIRST — as its own phase
  with tests — before game code consumes it. Tracktion headers stay isolated to
  rock-hero-common/audio implementation files. Enabling a time-stretch backend is a Tracktion
  build detail and must not leak stretch types into public headers.
- (b) Public-header minimalism; ports-and-adapters per docs/design/architectural-principles.md
  ("Ports and Adapters", "Keep Threading at the Boundary"). Speed and loop control ride the
  existing transport port surface from docs/plans/21-game-audio-engine-and-session.md Phase 1.
- (c) NAMING FIREWALL: the commercial real-guitar game that inspired this project is never named
  in any file; use "RS"/neutral phrasing. Charter (MIT) may be named.
- (g) Tone fidelity: practice uses the same rock-hero-common/audio Engine path as gameplay; the
  backing clip is the only stretched element, and the live rig is untouched.
  docs/design/architecture.md "VST Plugin Safety" grants full plugin freedom in practice mode.
- (h) Builds: all build/test/lint commands go through `.agents/rockhero-build.ps1` (usage in
  `.agents/README.md`) — never raw cmake/ctest/ninja. Intermediate phases run only the checks
  their changes determinately warrant; the final acceptance phase runs the sanctioned bundle as
  separate invocations.
- (i) Real guitar input: both hands are on the guitar mid-loop. Loop restart, speed nudge, and
  section navigation must be reachable via the MIDI foot controller
  (docs/plans/24-scoring-star-power-failure.md IMidiTrigger) — keyboard is a fallback, never the
  only path.

## 5. Current state inventory

Repo state (all paths repo-relative):

- Section markers exist and are already documented as the practice hook:
  `rock-hero-common/core/include/rock_hero/common/core/chart/chart.h:284-300` defines
  `ChartSection` ("Navigation/practice marker naming the passage that starts at a position") with
  a `GridPosition position` and a `std::string type` ("verse", "chorus"; repeat numbering
  derived). `Chart::sections` is a position-sorted vector (`chart.h:349-350`). Sections have no
  end — a section spans to the next section's start or the chart end.
- The shared transport port has play/pause/stop/seek/state/position only, message-thread-only, no
  speed and no loop API
  (`rock-hero-common/audio/include/rock_hero/common/audio/transport/i_transport.h:67-112`;
  `TransportState` carries a single `playing` flag, `transport/transport_state.h:12-24`).
- The engine already routes the backing clip through Tracktion's real-time path in anticipation
  of practice speed: `rock-hero-common/audio/src/engine/engine_song_audio.cpp:142-154` disables
  the clip proxy with a load-bearing comment — "Practice-speed playback time-stretches this clip
  live... Proxy-off routes through WaveNodeRealTime's elastique reader... responds to speed
  changes immediately" — and `engine_song_audio.cpp:163` sets `transport.looping = false`.
- BUT no time-stretch backend is compiled in: repo-wide search (excluding `external/`) finds zero
  `TRACKTION_ENABLE_TIMESTRETCH_*` definitions and zero uses of `setTimeStretchMode`,
  `setSpeedRatio`, `setAutoTempo`, or `PitchShiftPlugin`. The comment above describes an
  elastique path that does not exist in this build today.
- `rock-hero-game/` is a build-system skeleton (81-line JUCE `DocumentWindow` shell in
  `rock-hero-game/app/main.cpp`; `core`/`audio`/`ui` are placeholder static libs). No practice
  code exists anywhere.
- Test targets that later phases extend: `rock_hero_common_audio_tests`
  (`rock-hero-common/audio/tests/CMakeLists.txt:22`), `rock_hero_common_core_tests`
  (`rock-hero-common/core/tests/CMakeLists.txt:6`); `rock_hero_game_core_tests` is introduced by
  docs/plans/21-game-audio-engine-and-session.md Phase 2 / docs/plans/27 Phase 1.

Vendored Tracktion time-stretch facts (source-verified by the juce-tracktion-expert agent; paths
under `external/tracktion_engine/modules/tracktion_engine/`):

- `TimeStretcher::Mode` enumerates disabled, SoundTouch normal/better, ARA (clip-only), Elastique
  Pro/Efficient/Mobile/Monophonic (+ Direct variants), and RubberBand melodic/percussive;
  `defaultMode` is chosen by preprocessor priority Elastique > RubberBand > SoundTouch > disabled
  (`timestretch/tracktion_TimeStretch.h:34-62`).
- All backend macros default to 0 (`tracktion_engine.h:144-191`). With no backend compiled, every
  stretcher method silently no-ops — `processData` returns 0, `setSpeedAndPitch` returns false
  (`timestretch/tracktion_TimeStretch.cpp:1012, 1091-1121`). **In this repo's current build all
  backends are compiled out** — a build-config accident, fixable with one macro.
- SoundTouch full source is vendored (`3rd_party/soundtouch/`, included at
  `tracktion_TimeStretch.cpp:477`) under LGPL v2.1
  (`3rd_party/soundtouch/include/SoundTouch.h:44-60`); Tracktion's own demos enable it
  explicitly (`examples/DemoRunner/CMakeLists.txt:99`). RubberBand
  is NOT vendored (include probe `#error`s if absent, `tracktion_TimeStretch.cpp:650-666`) and is
  GPL/commercial dual-licensed upstream. Elastique is a closed commercial SDK supplied externally
  (`tracktion_engine_timestretch.cpp:21-23`).
- Two live-playback stretch routes exist (`playback/graph/tracktion_EditNodeBuilder.cpp:404-624`):
  Route A, offline-rendered proxy played live (default; stretch runs offline,
  `model/clips/tracktion_AudioClipBase.cpp:1955-1956`); Route B, true real-time stretch via
  `WaveNodeRealTime` when the clip proxy is off (`tracktion_AudioClipBase.cpp:1896-1900`; readers
  initialise `realtime = true`, `playback/graph/tracktion_WaveNode.cpp:598-599, 729-730`). Our
  engine already chose Route B (`engine_song_audio.cpp:154`).
- Whole-Edit varispeed (`setSpeedCompensation` ±10%, `setTempoAdjustment` ±50%) is Lagrange
  resampling — NOT pitch-preserving (`playback/tracktion_EditPlaybackContext.cpp:266-274,
  380-402`). Pitch-preserved stretch is per-clip only; there is no transport-level master stretch.
- Driving clip properties: `timeStretchMode`, `pitchChange`, `autoTempo`, `autoPitch`, `warpTime`
  CachedValues (`tracktion_AudioClipBase.cpp:233-252`) and `setSpeedRatio`/`setAutoTempo` API
  (`model/clips/tracktion_AudioClipBase.h:293-348`). `getActualTimeStretchMode()` upgrades
  `disabled` to `defaultMode` when stretch is required (`tracktion_AudioClipBase.cpp:604-612`);
  `checkModeIsAvailable` collapses unavailable persisted modes to `defaultMode`
  (`tracktion_TimeStretch.cpp:839-888`).
- Trade-off data: SoundTouch "better" uses a 64-tap AA filter, 60 ms sequence / 25 ms seek window
  (`tracktion_TimeStretch.cpp:490-497`), max 8192 frames needed "derived by experimentation"
  (`:520-524`); Elastique realtime floor is 0.25x speed (`:1020-1027`); pitch ratio clamps to
  0.25–4.0 (`:136, 407, 714`); speedRatio semantics: 2 = half speed
  (`tracktion_TimeStretch.h:131-134`). An optional background read-ahead wrapper exists but is
  off by default and marked TEMPORARY (`timestretch/tracktion_ReadAheadTimeStretcher.h:16-28`,
  `utilities/tracktion_EngineBehaviour.h:115-117`).
- Open uncertainties the expert could not close (each becomes a Phase 0 verification checkpoint):
  `WaveNodeRealTime` behavior when its mode resolves to `disabled` at non-1.0 speed; whether
  `WaveNodeRealTime` contributes to graph latency reporting (PDC); RubberBand's exact
  dual-licence terms (site knowledge, not in-tree).

Verified against code on 2026-07-06, refactor @ 13e82fb0.

## 6. Dependencies

- docs/plans/21-game-audio-engine-and-session.md — Phase 0 (Tracktion-in-game GO), Phase 1 (the
  NOW requirement: `setPlaybackSpeed`/`playbackSpeed` and loop-region seek on the shared
  transport port, with non-1.0 speed returning a typed error until this plan lands), Phase 2
  (GameplaySession spine with speed/loop pass-throughs), Phase 3 (tone switching correct across
  loop wraps and instant restarts).
- docs/plans/12-playback-clock.md — Phases 1–3 (snapshot carries `playback_rate`), Phase 4
  (extrapolator snaps on loop-wrap backward jumps). Practice at 50% must not produce a smoothed
  clock gliding through the wrap.
- docs/plans/24-scoring-star-power-failure.md — per-note verdict log and score-record format with
  a speed modifier field; IMidiTrigger port for foot-controller actions; hit-window policy
  co-owned with this plan's open question 4.
- docs/plans/27-in-song-flow-results-profiles.md — Phase 1 (IGameSettings, profile id), Phase 4
  (pure per-section results computation over verdict logs — reused per loop iteration here),
  Phase 3 (pause/resume machine practice sessions run inside).
- docs/plans/23-detection-verification-harness.md — Phase 1 (serialized DetectionEvent replay)
  and Phase 2 (autoplay bot) for deterministic practice-loop tests without audio.
- docs/plans/26-game-startup-menus-library.md — Phase 5 (menu input layer and bindable actions)
  for the practice UI entry points and controls.
- docs/plans/20-game-architecture-and-render-stack.md — Phase 0 (render stack sign-off) and
  Phase 2 (resource-pack convention; count-in click asset) for Phase 6 UI; Phase 4's
  dev-diagnostics seek-to-section is the debug precursor of section navigation.
- docs/plans/25-note-highway-3d.md — highway renders correctly at non-1.0 `playback_rate` from
  the clock (no highway change should be needed if it consumes the clock; verified in Phase 5).
- docs/plans/22-note-detection.md — no direct dependency for stretching (detection taps the live
  dry input, which is never stretched), but the latency-budget interplay feeds open question 4.
- External decision: time-stretch backend choice and licensing sign-off (open question 1;
  coordinate with the roadmap's licensing audit — docs/plans/00-roadmap.md Decisions needed).

## 7. Decisions already made

- Speed factor and loop-region seek are plumbed through the shared transport port and the clock
  snapshot from day one, before practice mode exists — source:
  docs/plans/21-game-audio-engine-and-session.md Phase 1 and docs/plans/12-playback-clock.md
  (snapshot `playback_rate`, loop-wrap snap rule).
- The backing clip plays proxy-off through Tracktion's real-time route (Route B) precisely so
  speed changes apply live without proxy re-renders — source: code comment at
  `rock-hero-common/audio/src/engine/engine_song_audio.cpp:142-154`.
- Sections are the practice navigation unit and repeat numbering is derived, never authored —
  source: `chart.h:284-300` doc comments; constraint (d) derived-over-authored.
- Practice/editor contexts get full plugin freedom (no gameplay safe-mode restrictions) — source:
  docs/design/architecture.md "VST Plugin Safety".
- Per-section results computation is a pure function over (verdict log, `Chart::sections`, tempo
  map) — source: docs/plans/27-in-song-flow-results-profiles.md Phase 4. Practice reuses it per
  loop iteration instead of reinventing section math.
- The live guitar path is never stretched. Only the backing clip is time-stretched; the player
  performs in real time. This is forced by physics (the instrument is live) and by constraint
  (g)'s same-engine-path rule; recorded here so no phase ever considers stretching the monitor
  path.

## 8. Open questions for the user

1. **Time-stretch backend.** Options: (A) SoundTouch — vendored in the submodule, enabled with
   one macro, LGPL v2.1 (license-compatible with our AGPLv3 per docs/design/architecture.md
   "Licensing" table pattern), moderate quality, ~8192-frame worst-case feed; (B) RubberBand —
   better quality reputation, NOT vendored, GPL/commercial dual license that must be verified and
   the source supplied by us; (C) Elastique — commercial closed SDK, incompatible with a
   zero-cost AGPLv3 distribution. **Recommendation: A (SoundTouch)** for v1; re-evaluate B only
   if Phase 0 listening tests fail. Requires adding a SoundTouch row to the architecture.md
   licensing table (design-doc update + user confirmation per CLAUDE.md).
2. **Speed range and step.** Options: (A) 50–100% in 5% steps (safe for all backends); (B)
   25–100% (Elastique's realtime floor is 0.25x; SoundTouch has no hard floor but quality
   degrades); (C) include over-speed 100–125% for burst training. **Recommendation: A for the v1
   UI with the port accepting 0.25–1.5 so the range is a UI policy, not an interface limit.**
3. **What practice runs record.** Options: (A) accuracy-only, nothing persisted; (B) full verdict
   log persisted to a separate practice-stats store (keyed chart hash, arrangement, section span,
   speed, profile), never the main score store, never leaderboard-eligible; (C) practice runs
   write normal score records flagged with the speed modifier. **Recommendation: B** — keeps the
   main score store clean while enabling loop-over-loop trend feedback and long-term "this
   section at 70%" progress. Speed modifier field already exists in
   docs/plans/24-scoring-star-power-failure.md's record format either way.
4. **Hit-window domain at reduced speed.** At 50% speed a window defined in song time doubles in
   wall-clock terms. Options: (A) wall-clock-constant windows — timing precision stays the skill
   being trained, notes just arrive slower; (B) song-time windows — very forgiving at low speed,
   risks training sloppy timing; (C) song-time windows but capped at 1.5x their full-speed
   wall-clock width. **Recommendation: A**, co-owned with docs/plans/24's hit-window spec; note
   that detection confirmation latency (50–80 ms worst case per docs/plans/22-note-detection.md)
   becomes proportionally less punishing at low speed under A, which is a desirable side effect.
5. **Loop pre-roll.** The player must re-place the fretting hand at each wrap. Options: (A) fixed
   2 s; (B) one full measure (min 1.5 s), configurable; (C) none, loop butt-joins. Also whether
   the pre-roll plays a count-in click (asset from docs/plans/20 Phase 2's resource pack).
   **Recommendation: B with the count-in click on by default; pre-roll notes are shown dimmed and
   not scored.**

## 9. Phased implementation

### Phase 0 — Backend enablement and verification spike (STOP gate)

Scope: turn the compiled-out stretch path into a measured, working facility on a scratch branch.
Assumes open question 1 answered (default assumption: SoundTouch).

- Define `TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1` on the Tracktion module targets (mirror
  `examples/DemoRunner/CMakeLists.txt:99`); confirm `TimeStretcher::defaultMode` becomes
  SoundTouch-backed. This is a CMake change to our Tracktion consumption, not to the submodule.
- Verification checkpoints (each cites or closes an expert open uncertainty; run with
  juce-tracktion-expert before writing production code):
  a. `WaveNodeRealTime` at mode `disabled` — confirm today's 1.0-speed playback is byte-plain
     (the editor ships on this path) and characterize non-1.0 behavior pre-backend; trace
     `getNodeProperties`/reader chain for PDC contribution.
  b. Whole-clip slow-down route: clip `setSpeedRatio` + `setAutoTempo(false)` vs `autoTempo` with
     a scaled tempo sequence. Verify which keeps the Edit timeline in song seconds so
     `ITransport::position()` and the plan-12 clock keep reporting song time while wall-clock
     advance scales by the factor — this is the semantics docs/plans/12 assumes
     (`playback_rate` in the snapshot, song-time positions).
  c. Speed change while playing: no proxy re-render (already proxy-off), no dropout beyond one
     buffer, latency of the backing path measured at 1.0 vs 0.5 (loopback capture) — any added
     backing latency shifts backing-vs-live alignment and must be reported to the calibration
     model in docs/plans/13-audio-device-settings-and-calibration.md.
  d. Confirm 1.0 speed pays no stretch cost (engine comment claims "stays cheap at 1x",
     `engine_song_audio.cpp:152-153` — written for elastique; re-verify for SoundTouch).
  e. Loop range semantics on `tracktion::engine::TransportControl` (shared checkpoint with
     docs/plans/21 Phase 1 if that phase shipped loop as typed-NotSupported).
- Deliverable: a findings note (quality listening result at 50%/75%, CPU per buffer, measured
  latency delta, PDC answer) appended to this plan.
- Exit criteria: **STOP — present findings and get user sign-off on backend and route.** Phases
  1+ assume outcome: SoundTouch, clip-`setSpeedRatio`-shaped route (adjust here if b says
  otherwise).
- Verification commands (spike branch only):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Configure -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets rock_hero_common_audio_tests -RunTouchedTests
```

### Phase 1 — Functional playback speed through the shared port (assumes Phase 0 outcome)

Scope: upgrade docs/plans/21 Phase 1's `setPlaybackSpeed` from typed-error-on-non-1.0 to
functional over the verified route. Files: `rock-hero-common/audio/src/engine/engine_transport.*`
/ `engine_song_audio.cpp` (apply speed to the backing clip), no new public headers — the port
surface from plan 21 Phase 1 is unchanged (public-header impact: none; doc comments updated to
drop the not-supported caveat). The live-rig graph is untouched.

Testing (extend `rock_hero_common_audio_tests`, `drum_loop.wav` fixture): speed 0.5 accepted and
reported; transport position advances in song time at half wall-clock rate (poll-based, coarse
tolerance); speed restored to 1.0 resumes normal advance; clock snapshot (plan 12) publishes
`playback_rate` = the set factor. Exit criteria: tests green; editor behavior at default 1.0
unchanged (editor never calls the new setter).

Verification:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets rock_hero_common_audio_tests -RunTouchedTests
```

### Phase 2 — Loop-region hardening (assumes Phase 0 outcome)

Scope: make loop-region playback wrap cleanly: wrap fires a plan-12 boundary publish (backward
jump → extrapolator snap), tone regions re-apply at the wrap target (docs/plans/21 Phase 3 test
matrix already covers wrap — extend it, don't duplicate), and wrap-to-pre-roll positioning is a
plain `seek` so no new engine concept is needed. If docs/plans/21 Phase 1 shipped loop as typed
NotSupported (its rollback note allows that), implement the Tracktion-backed loop here using
Phase 0 checkpoint e. Files: engine transport/song-audio TUs; public-header impact: none.

Testing (`rock_hero_common_audio_tests`): loop set/clear round-trip; playback confined to range;
position after wrap == loop start (± one buffer); clock snapshot after wrap is inside the range.
Exit criteria: audible wrap on the dev machine has no glitch longer than one device buffer
(manual check recorded in the phase notes). Verification: same invocation as Phase 1.

### Phase 3 — Section-span math in common/core (pure)

Scope: pure helpers over the chart domain, placed in `rock-hero-common/core` (chart feature
folder) because three consumers need identical math: practice (this plan), results per-section
rows (docs/plans/27 Phase 4), and section-sanity validation (docs/plans/42-chart-validation.md).
Coordinate: if docs/plans/27 Phase 4 lands first with game-local span math, this phase EXTRACTS
it to common per constraint (a) rather than writing a second copy.

- `sectionSpans(chart)` → ordered `[start, end)` grid spans (last span ends at chart end) with
  derived labels ("Chorus 2" — repeat numbering computed, constraint (d)).
- Span-to-seconds conversion via the tempo map; pre-roll start computation (one measure back,
  clamped to song start) honoring open question 5's answer.

Public-header impact: one new header in
`rock-hero-common/core/include/rock_hero/common/core/chart/` (name settled at implementation;
follows docs/design/documentation-conventions.md). Testing (`rock_hero_common_core_tests`):
empty-sections chart → single implicit span; duplicate type names → correct repeat numbers;
last-section span end; pre-roll clamp at song start; conversion against a two-anchor tempo map.
Exit criteria: tests green; docs/plans/27's results computation compiles against the shared
helper (or a follow-up task is filed in that plan if it has not started).

Verification:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets rock_hero_common_core_tests -RunTouchedTests
```

### Phase 4 — PracticeSession state machine in game/core (headless)

Scope: `rock-hero-game/core` headless orchestration over GameplaySession (docs/plans/21 Phase 2):
selected span (one or more contiguous sections), speed factor, loop on/off; state machine
PreRoll → Playing → Wrap (→ PreRoll), driven by clock snapshots, never wall-clock sleeps (time
is a dependency per docs/design/architectural-principles.md "Time Must Be a Dependency").
Pre-roll
notes flagged unscored. Each loop iteration closes a verdict-log slice (docs/plans/24 format).
No-fail is unconditional in practice.

Public-header impact: new `rock-hero-game/core` headers only (game-internal). Testing
(`rock_hero_game_core_tests`): deterministic replay per docs/plans/23 Phase 1 — scripted
DetectionEvent/verdict streams and a fake clock; prove iteration boundaries land on wrap
publishes, pre-roll notes excluded, speed changes mid-loop take effect next iteration (policy:
speed changes apply at the next wrap, avoiding mid-phrase tempo lurch). Exit criteria: replay
tests green with zero audio involvement.

Verification:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets rock_hero_game_core_tests -RunTouchedTests
```

### Phase 5 — Per-section accuracy feedback and practice stats

Scope: per-iteration accuracy via docs/plans/27 Phase 4's pure computation applied to each
iteration's verdict slice; iteration trend (last N accuracies per section span); persistence per
open question 3's answer (recommended: separate practice-stats store keyed chart hash from
docs/plans/10-format-versioning-and-chart-identity.md, arrangement id, section span, speed,
profile id from docs/plans/27 Phase 1). Confirm the highway (docs/plans/25) needs no change at
non-1.0 rate beyond consuming `playback_rate` from the clock — file an issue against that plan if
its extrapolator use proves otherwise.

Testing (`rock_hero_game_core_tests`): accuracy aggregation across iterations; store round-trip;
key stability across reloads. Exit criteria: a replayed three-iteration loop produces three
accuracy rows and one persisted best. Verification: same invocation as Phase 4.

### Phase 6 — Practice UI and foot-controller input (gated: docs/plans/20 Phase 0; 26 Phase 5)

Scope: entry points (Quick Play song entry → "Practice" per docs/plans/26 Phase 7 list; in-song
pause menu → "Practice this section" per docs/plans/27 Phase 6); section picker listing Phase 3
labels; speed control (range per open question 2); loop toggle; count-in click from the resource
pack (docs/plans/20 Phase 2); on-highway section boundary markers and a per-iteration accuracy
readout. Bindable actions through docs/plans/26 Phase 5's input layer with MIDI pedal bindings
via docs/plans/24's IMidiTrigger: restart loop, speed down/up one step, toggle loop, exit
practice (constraint (i)). Stretch (not committed): auto step-up trainer raising speed after a
threshold accuracy.

Public-header impact: none outside `rock-hero-game`. Testing: state-driven UI logic headless
where practical (selection model, binding dispatch); manual checklist for pedal actions and
audible count-in. Exit criteria: full practice loop operated end-to-end with hands on the guitar,
keyboard untouched after entering the loop.

Verification:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

## 10. Final acceptance phase

Per constraint (h), as separate invocations from the repo root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets clang-tidy
pre-commit run --all-files
```

Plus the local-only corpus smoke (never CI): load corpus packages and spot-practice one section
at 50% in three packages of different tunings, confirming tone-region correctness across wraps.

## 11. Rollback/abort notes

- **Phase 0 quality failure** (SoundTouch artifacts unacceptable at 50–75%): do not silently
  escalate to RubberBand — its license and sourcing are an explicit user decision (open question
  1). Interim fallback: ship section looping WITHOUT slow-down (loop + pre-roll + per-section
  accuracy are independently valuable); the port keeps returning the typed error for non-1.0.
- **Phase 0 checkpoint b failure** (Edit timeline does not stay in song seconds under the chosen
  route): STOP — this breaks plan 12's clock semantics for every consumer. Re-route via the
  alternative (auto-tempo/tempo-sequence scaling) before any production code; if both fail,
  practice speed is re-scoped to a preprocessed offline-stretched audio variant (worst case,
  explicitly presented to the user first).
- **Phase 2 wrap glitch** that cannot be fixed at the engine level: fall back to stop → seek →
  pre-roll → play at each wrap; the mandatory pre-roll makes this fallback nearly invisible.
- Phases 1–2 are additive behavior behind an existing port; rollback is reverting the engine TU
  changes — no format, package, or public-header rollback exists anywhere in this plan.
- Phase 3 extraction is a pure-code move; if the shared helper churns docs/plans/27's landed
  code, revert to game-local math and file the extraction as its own small task.
