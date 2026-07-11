# Plan 24 — Scoring, Star Power, and Failure

**Status:** Decision-gated (star-power earning model Q1, star-power rescue Q2, IMidiTrigger
placement Q3, meter visual direction Q4, vendored-JUCE bug handling Q5); Phases 1–3 are
gate-independent and executable now. Date 2026-07-06; baseline `refactor @ 13e82fb0`.

## 1. Goal

A player plugging in a real guitar gets honest, responsive scoring: every chart note receives a
verdict (hit/miss, timing delta, detected pitch, confidence), a GH-style 4x multiplier ladder and
streak, star power deployed hands-free via a MIDI foot controller, and a failure meter tuned
toward GH-expert feel with **no-fail ON by default**. Every completed run emits a versioned score
record that docs/roadmap/27-in-song-flow-results-profiles.md stores and
docs/roadmap/29-online-leaderboards.md uploads unchanged.

## 2. Non-goals

- Pitch/onset detection itself — docs/roadmap/22-note-detection.md owns DSP and the detectability
  truth; this plan owns what detection results *mean* for score.
- Rendering the meter, star-power gauge, or hit feedback — docs/roadmap/25-note-highway-3d.md
  renders from events this plan publishes; here we only sketch visual directions (section 8, Q4).
- Results screen, local score persistence, profiles — docs/roadmap/27-in-song-flow-results-profiles.md.
- Leaderboard upload/integrity — docs/roadmap/29-online-leaderboards.md.
- Practice-mode accuracy feedback — docs/roadmap/28-practice-mode.md.
- Pause-pedal behavior — docs/roadmap/26-game-startup-menus-library.md (it reuses the IMidiTrigger
  infrastructure built here).
- Authored difficulty or any charter-authored gameplay metadata (constraint (d) territory; the
  only candidate here, star-power phrases, is resolved in Q1).

## 3. Constraints

Restated subset of the roadmap's non-negotiable constraints (docs/roadmap/00-roadmap.md):

- (a) **Layering**: common never depends on editor or game code; editor and game never depend on
  each other. Anything both products need is extracted to rock-hero-common FIRST, as its own
  phase with tests, before game code consumes it. Game code never includes editor headers.
  Tracktion headers stay isolated to rock-hero-common/audio implementation files.
- (b) **Public-header minimalism**: only headers that must be public are public;
  ports-and-adapters per docs/design/architectural-principles.md ("Ports and Adapters").
- (c) **NAMING FIREWALL**: the commercial real-guitar game that inspired this project is never
  named in any file; use "RS"/"RS2014" or neutral phrasing. Charter (BSD 3-Clause) may be named.
- (h) **Builds**: all build/test/lint commands go through `.agents/rockhero-build.ps1` (usage in
  `.agents/README.md`) — never raw cmake/ctest/ninja. Intermediate phases run only the checks
  their changes warrant; the final acceptance phase is the sanctioned bundle as separate
  invocations.
- (i) **Real guitar input**: no plastic-controller assumptions. Star power and in-song actions
  use a MIDI foot controller because both hands are on the guitar.

Additional binding rules from the design docs:

- All scoring comparisons happen in audio-sample time with calibration offsets applied
  consistently; mixing audio-thread time with render-thread time in scoring logic is prohibited
  (docs/design/architecture.md, "Timing and Latency").
- Scoring logic is game-owned and treats the `Song` model as read-only during gameplay
  (docs/design/architecture.md, "Application Responsibilities — Rock Hero Game").
- Core logic takes time as a dependency (injected timestamps, calibration offsets), never reads
  clocks directly (docs/design/architectural-principles.md, "Time Must Be a Dependency").
- Threading stays at the boundary; domain logic receives plain snapshots/events and must not care
  whether input came from the audio thread, a replay file, or a test harness
  (docs/design/architectural-principles.md, "Keep Threading at the Boundary").
- The replayable simulation layer is a first-class objective: hit/miss decisions, score
  evolution, combo behavior, and calibration correctness verified headlessly
  (docs/design/architectural-principles.md, "Add a Replayable Simulation Layer").

## 4. Current state inventory

Verified with `rg`/reads against the tree:

- **No MIDI device infrastructure exists anywhere in the repo.** `rg "MidiInput"` across
  `rock-hero-*` returns zero hits. The only `midi`-substring matches are GP-import musical-data
  parsing (`rock-hero-editor/core/src/project/gp_score_parser.cpp`,
  `gp_chart_builder.cpp`, `gp_score.h`, `tests/test_gp_song_importer.cpp`) and comments in
  `rock-hero-common/audio/src/tracktion/multi_tone_rack.cpp` plus two audio tests — none open,
  enumerate, or receive from MIDI devices. Phase 5 creates the first MIDI I/O code in the repo.
- **rock-hero-game is a build-system skeleton**: `rock-hero-game/app/main.cpp` is an 81-line JUCE
  DocumentWindow shell; `rock-hero-game/core/src/placeholder.cpp`,
  `rock-hero-game/audio/src/placeholder.cpp`, `rock-hero-game/ui/src/placeholder.cpp` are
  placeholder static libs. No game tests exist yet; test-target naming elsewhere follows
  `rock_hero_<scope>_<module>_tests` (e.g. `rock-hero-common/core/tests/CMakeLists.txt` registers
  `rock_hero_common_core_tests`).
- **Chart model** (`rock-hero-common/core/include/rock_hero/common/core/chart/chart.h`): the full
  technique surface this plan must score — `NoteAttack` {Pick, Hammer, Pull, Tap, Pop, Slap},
  `NoteMute` {None, Palm, Full}, `NoteHarmonic` {None, Natural, Pinch} with optional `touch`
  position, `vibrato`/`tremolo`/`accent` flags, `BendPoint` curves (beat-fraction offset +
  semitones), `SlideWaypoint`s (offset, fret, unpitched), `Fraction` sustains, `ChordTemplate` /
  `ChartShape` posture spans, `FretHandPosition`, `ChartSection`, and `ChartTuning` (string
  pitches, capo, `cent_offset`). Positions are exact rational `GridPosition` tokens, never
  seconds.
- **Difficulty/verdict adjacents**: `DifficultyRating` exists in-memory only
  (`rock-hero-common/core/include/rock_hero/common/core/song/difficulty.h`), never serialized —
  scoring does not touch it. Chart structural validation exists
  (`chart_rules.h::validateChartRules`, `g_max_chart_strings = 8`).
- **Chart identity**: `package_id.h` provides UUID helpers and canonical chart refs
  (`charts/<uuid>.chart.json`); there is **no content hash anywhere** — the score record's chart
  hash is strictly a docs/roadmap/10-format-versioning-and-chart-identity.md deliverable.
- **Audio ports available to the game** (`rock-hero-common/audio/include/rock_hero/common/audio/`):
  transport, song audio, plugin host, live rig, device config/settings, live input with
  `input_calibration.h`/`input_device_identity.h`. No playback-clock port yet
  (docs/roadmap/12-playback-clock.md) and no scoring/detection ports.
- **Vendored JUCE is 8.0.12**
  (`external/tracktion_engine/modules/juce/modules/juce_core/system/juce_StandardHeader.h:42-44`)
  with the UMP-reworked MIDI layer; the expert findings in Phase 5 were source-verified against
  this submodule.

Verified against code on 2026-07-06, refactor @ 13e82fb0.

## 5. Dependencies

Upstream (this plan consumes):

- docs/roadmap/22-note-detection.md Phase 1 (detection contract): `DetectionEvent` value types
  (onset, pitch(es)+confidence, sustained-pitch tracking, percussive-mute classification), the
  per-tuning worst-case confirmation times and latency budget, and the detectability column of
  the technique matrix (section 9, Phase 2 here is co-authored with it). Value types land in
  rock-hero-game/core so scoring stays pure; the producing port lives in rock-hero-game/audio.
- docs/roadmap/23-detection-verification-harness.md: serializable DetectionEvent streams; scoring
  is tested by deterministic replay of event logs, never audio; the autoplay bot emits perfect
  event streams for soak.
- docs/roadmap/12-playback-clock.md: IPlaybackClock supplies the song-time base for scoring
  timestamps.
- docs/roadmap/13-audio-device-settings-and-calibration.md: the measured-offset contract
  (input + output + video latency per device identity) that shifts hit windows; offsets are
  copied into every score record.
- docs/roadmap/10-format-versioning-and-chart-identity.md: the semantic chart-identity hash + hash
  algorithm id stored in every score record.
- docs/roadmap/20-game-architecture-and-render-stack.md Phase 0: gates game architecture broadly;
  Phases 1–4 here are pure rock-hero-game/core code against the existing skeleton libs and do
  not depend on the render-stack outcome, but execution order is set by docs/roadmap/00-roadmap.md.
- docs/roadmap/21-game-audio-engine-and-session.md: the GameplaySession spine wires
  detection → scoring → record emission at runtime.

Downstream (consume this plan):

- docs/roadmap/25-note-highway-3d.md: hit/miss/early/late feedback events; the chosen meter/SP
  visual direction (Q4).
- docs/roadmap/26-game-startup-menus-library.md: the pause pedal reuses IMidiTrigger (Phase 5);
  menu-bindable actions may alias trigger actions.
- docs/roadmap/27-in-song-flow-results-profiles.md: results screen and local score store consume
  the score record (Phase 3) including per-note verdict detail for early/late tendency.
- docs/roadmap/29-online-leaderboards.md: uploads the Phase 3 record unchanged; no-fail labeling
  feeds its eligibility rules.

## 6. Decisions already made

- Scoring is game-owned; the game treats `Song` as read-only and evaluates hit/miss against
  audio-derived timing (docs/design/architecture.md, "Application Responsibilities").
- All scoring comparisons in audio-sample time with calibration applied; calibration is built in
  from day one, not bolted on (docs/design/architecture.md, "Timing and Latency", "Gameplay
  Systems").
- Bends, hammer-ons, pull-offs, and slides are valid techniques, not missed notes
  (docs/design/architecture.md, "Gameplay Systems — Note matching and scoring").
- Pure unit tests dominate; score computation, timing-window checks, and latency compensation
  are named pure-test targets; project-owned ports (`IPitchDetector`, `IScoringInputStream` are
  listed examples) are faked, frameworks are not mocked
  (docs/design/architectural-principles.md, "Preferred Kinds of Tests", "Ports and Adapters").
- Difficulty and other chart descriptors are derived by versioned calculators, never authored
  (docs/design/architecture.md, "Song Data Model"; docs/roadmap/11-derived-difficulty-calculator.md)
  — this shapes the Q1 recommendation for star-power phrases.

Decisions **established as normative by this plan** (restate when citing this plan):

- **No-fail is ON by default**; failing out is opt-in. No-fail runs are labeled in the score
  record (`modifiers.noFail`); leaderboard eligibility is decided later by
  docs/roadmap/29-online-leaderboards.md, not here.
- **The provisional-hit state machine (section 9, Phase 2) is mandatory, not optional.**
  Rationale, stated explicitly: "GH scoring exactly" assumes deterministic, instantaneous
  plastic-button events. Our observations are probabilistic and latency-delayed — low-string
  pitch confirmation costs 50–80 ms (per docs/roadmap/22-note-detection.md Phase 1's budget),
  which consumes most of a GH-style hit window. We therefore adopt the GH 4x multiplier ladder
  and scoring constants *honestly adapted*: onsets register provisional hits immediately for
  responsiveness, late pitch evidence confirms or revokes them, and all authoritative accounting
  happens on a committed ledger (below). The displayed score may visibly self-correct on
  revocation; that is the honest cost of real-guitar physics and we do not hide it.
- **Unmatched onsets are not penalized in ruleset v1.** Real guitars produce string noise, ghost
  onsets, and mid-phrase mutes; GH-style overstrum penalties would punish normal playing. Logged
  in the record, revisited by ruleset version if abuse appears.
- **All tunables are ruleset-versioned**: hit windows, ladder thresholds, meter constants, star
  thresholds live in one `ScoringRuleset` value (initial id `rh-score-1`); any constant change
  bumps the version; every record carries it.

## 7. Open questions for the user

Mirrored into docs/roadmap/00-roadmap.md "Decisions needed".

- **Q1 — How is star power earned?** The chart format has no star-power phrase markers.
  - (a) *Derived phrases*: a versioned calculator generates SP phrases from chart structure
    (sections, note-run density), cached like difficulty (constraint (d) pattern); no format
    change. **Recommended** — consistent with derived-over-authored, ships without touching
    docs/roadmap/10-format-versioning-and-chart-identity.md; can be upgraded later.
  - (b) *Authored phrase markers*: format addition via plan 10 plus editor authoring in
    docs/roadmap/40-chart-editing.md; musically curated like GH, but hand-authored gameplay
    metadata contradicts the project's derived-over-authored stance and delays this plan.
  - (c) *Continuous accrual*: SP meter fills from confirmed-hit streaks, no phrases at all;
    simplest, but loses GH's "nail this passage" moments.
- **Q2 — Does star-power deploy rescue the failure meter?** (a) Yes, GH-authentic: deploy adds an
  immediate meter boost and doubles meter gain while active — **recommended** (it is the classic
  clutch-save loop and gives the pedal a defensive use); (b) No: SP is score-only, meter stays
  independent (simpler tuning).
- **Q3 — IMidiTrigger port placement**: (a) `rock-hero-game/audio` — **recommended**: only the
  game needs pedals today, constraint (a) extracts to common only when both products need it,
  and moving a small port later is mechanical; (b) `rock-hero-common/audio` now, if editor pedal
  use (hands-free transport while charting) is considered near-certain.
- **Q4 — Meter/star-power visual direction** (feeds docs/roadmap/25-note-highway-3d.md): see the
  sketches in section 8. Recommendation: direction C with B's charge accent.
- **Q5 — Vendored JUCE MIDI bug handling** (details in Phase 5): (a) design-around now —
  single-`MidiInput`-per-identifier discipline plus an engine-exclusion checkpoint —
  **recommended**; (b) patch the one-line upstream fix into the submodule (fork maintenance
  burden); (c) bump the submodule (largest blast radius; fold into a routine JUCE/Tracktion
  update instead).

## 8. Meter and star-power look-and-feel — discussion (Q4)

The 3D highway needs its own design language, not GH's 2D bar bolted onto a corner. Three
directions, sketched for the user to react to; the chosen one becomes a
docs/roadmap/25-note-highway-3d.md HUD requirement:

- **A — Stage atmosphere**: the failure meter *is* the venue. Healthy = warm stage wash, crowd
  audio bed; failing = lights dim toward a cold spot, crowd thins. Star power ready = spotlight
  sweep; deployed = full light show. Zero HUD chrome; risk: readability of exact meter state.
- **B — Amp-top diegetics**: a skeuomorphic VU needle (failure) and a glowing tube/charge lamp
  (star power) rendered on an amp head at the highway horizon. Readable at a glance, thematically
  ours; risk: horizon clutter competing with note readability.
- **C — Highway edge-light strips** (recommended): the highway's own edge rails carry state —
  failure meter as rail color temperature and fill length; star power as an energy surge that
  runs up the strings/rails when ready and saturates the lane glow while deployed. Keeps eyes on
  the note stream where they must be during real-guitar play; pairs naturally with 25's glow
  pipeline; can borrow B's charge lamp as a peripheral accent.

## 9. Phased implementation

### Phase 1 — Scoring domain model and ruleset (gate-independent)

**Scope**: pure value types and functions in rock-hero-game/core: `NoteVerdict` (hit, miss,
onset-only-hit, revoked-wrong-pitch, with timing delta ms, detected pitch cents, confidence,
sustain-held fraction), `ScoringRuleset` (versioned constants), timing-window math (expected
note time from tempo map + calibration offsets per
docs/roadmap/13-audio-device-settings-and-calibration.md's contract), the GH ladder (1x/2x/3x/4x
at streaks 0/10/20/30; star power doubles to 8x), base scoring (50/note; chords = sum of member
notes; sustains 25 per beat pro-rated by held fraction), star thresholds on
score/max-base-score ratio (v1 defaults 5★ ≥ 2.8, 4★ ≥ 2.0, 3★ ≥ 1.2, 2★ ≥ 0.6 — tunable by
ruleset version). Default onset window ±100 ms around the calibrated expected time (ruleset
constant), verdict timing delta always recorded signed (negative = early) for 27's tendency
display.
**Files**: new `rock-hero-game/core/include/rock_hero/game/core/scoring/` headers +
`rock-hero-game/core/src/scoring/` + `rock-hero-game/core/tests/` (new test target
`rock_hero_game_core_tests`, replacing reliance on `placeholder.cpp`). Include form per
docs/design/architecture.md "Include-path convention" (`<rock_hero/game/core/scoring/...>`).
**Public-header impact**: new game-scope public headers only; nothing added to common.
**Testing**: pure unit tests (window math incl. calibration shifts and speed factor pass-through,
ladder transitions, chord/sustain arithmetic, star thresholds) — the "should dominate" category
of docs/design/architectural-principles.md.
**Exit criteria**: tests green; no dependency on detection, MIDI, rendering, or Tracktion.
**Verification** (new CMake test target → configure justified):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Configure -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

### Phase 2 — Provisional-hit state machine, committed ledger, technique matrix (gate-independent)

**Scope**: the mandatory core. Per-note lifecycle:

- `Pending → Armed` when the calibrated window opens.
- `Armed + onset` (nearest-unclaimed matching by |timing delta|; simultaneous candidates
  disambiguated by early pitch evidence when present) `→ Provisional` — streak/multiplier/HUD
  update *optimistically*, score not banked.
- `Provisional + consistent pitch → Confirmed`; sustained notes proceed to `Holding` with
  sustained-pitch tracking; drop banks partial sustain credit.
- `Provisional + confident contradicting pitch → Revoked`: the onset is released and re-matched
  against other `Armed` notes whose expected pitch fits (an early next note claims it);
  otherwise the note commits as a miss (reason wrong-pitch).
- `Provisional` deadline lapse (onset + per-tuning confirmation budget from
  docs/roadmap/22-note-detection.md Phase 1, no decisive pitch) → **confirm by default** (favor
  the player; false positives beat false negatives for fun; ruleset-versioned policy). Full
  mutes are onset-only by design and always resolve this way.
- `Armed` window close without onset `→ Missed`.

**Committed ledger**: notes commit in chart order once terminal. Multiplier, streak, star-power
gain, and the failure meter are computed **only over the committed sequence**; the displayed
state is committed + provisional overlay and may visibly correct on revocation. Failure (Phase 4)
is evaluated only on committed state, so no one fails from a note that later confirms.

**Technique scoring matrix — normative spec** (detectability column owned by
docs/roadmap/22-note-detection.md Phase 1 and co-authored there; chart fields from
`rock-hero-common/core/include/rock_hero/common/core/chart/chart.h`):

| Chart field | v1 class | Scoring rule |
|---|---|---|
| Note onset+pitch | scored | Core verdict via state machine above |
| Sustain | scored | Held fraction from sustained-pitch tracking; pro-rated credit |
| Chords (simultaneous notes/shapes) | scored, lenient | One verdict per chord; hit when the detected pitch set is consistent (≥ 2 member pitches, no strong contradiction — thresholds from 22's contract); score = sum of member notes |
| `NoteMute::Full` | scored (onset-only) | Percussive-mute classification confirms; no pitch requirement |
| `NoteMute::Palm` | scored, lenient | Pitch verdict as normal; damped-timbre check lenient |
| `NoteAttack` Hammer/Pull/Tap | scored, lenient | Lowered onset-confidence threshold in-window; attack kind itself cosmetic |
| `NoteAttack` Pop/Slap | scored | As normal notes; attack kind cosmetic |
| Harmonics (Natural/Pinch, `touch`) | scored, lenient | Scored by resulting pitch; octave-error leniency |
| Bends (`BendPoint` curves) | lenient | Onset+initial pitch scored; trajectory-to-target checked at ±50 cents when tracking is available, else cosmetic at v1 |
| Slides (`SlideWaypoint`s) | lenient | Onset scored; waypoint pitch glide lenient; `unpitched` tails cosmetic |
| `tremolo` | lenient | Onset+pitch scored; repetition rate cosmetic at v1 |
| `vibrato`, `accent` | cosmetic | Never affect verdicts at v1 |
| `FretHandPosition`, shapes' fingering, sections | never scored | Notation/navigation only |

**Files**: rock-hero-game/core scoring feature folder + tests.
**Public-header impact**: game-scope only. Consumes `DetectionEvent` value types from
docs/roadmap/22-note-detection.md Phase 1; if this phase executes first, it defines those types in
rock-hero-game/core as the draft contract and 22 Phase 1 ratifies them (record whichever
happened in both plans' status lines).
**Testing**: pure unit tests for every transition (including revoke-and-rematch, deadline-lapse,
ledger correction cascades); deterministic replay tests over serialized DetectionEvent streams
per docs/roadmap/23-detection-verification-harness.md (hand-authored fixture streams checked in;
never the commercial-content corpora). This is the replayable simulation layer of
docs/design/architectural-principles.md made real.
**Exit criteria**: replaying a fixture stream twice yields byte-identical verdict sequences; all
matrix rows have at least one test.
**Verification**:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

### Phase 3 — Score record format and writer (gate-independent)

**Scope**: define and freeze the record schema NOW — 29 consumes it unchanged and it cannot be
bolted on later. JSON via the narrow `juce_core` permission
(docs/design/architecture.md, "JUCE utility dependency in core modules"), living in
rock-hero-game/core:

```json
{
  "formatVersion": 1,
  "recordId": "<uuidv4>",
  "createdUtc": "2026-07-06T00:00:00Z",
  "chart": { "hash": "<plan-10 semantic hash>", "hashAlgorithm": "<plan-10 id>",
             "arrangementId": "<uuid>", "part": "Lead" },
  "rulesetVersion": "rh-score-1",
  "detectionVersion": "<from docs/roadmap/22-note-detection.md>",
  "calibration": { "deviceIdentity": "<from plan 13>", "inputLatencyMs": 0.0,
                   "outputLatencyMs": 0.0, "videoLatencyMs": 0.0 },
  "modifiers": { "noFail": true, "speed": 1.0, "leftyMirror": false },
  "profileId": "<uuid, plan 27>",
  "result": { "score": 0, "maxStreak": 0, "accuracyPercent": 0.0, "stars": 0,
              "failed": false, "completed": true, "starPowerDeployments": 0 },
  "verdicts": [[0, "hit", -12.5, 6420, 0.93, 1.0], [1, "missNoOnset", null, null, null, 0.0]]
}
```

`verdicts` rows are `[noteIndex, verdictCode, timingDeltaMs, detectedPitchCents,
confidence, sustainHeldFraction]`; verdict codes: `hit`, `hitOnsetOnly`, `missNoOnset`,
`missWrongPitch` (revoked). Per-section aggregates are derived by 27 from verdicts + chart
sections, not stored. Size: compact arrays keep dense charts in the low tens of KB — fine for
local storage and upload. Chart hash/algorithm fields are written as `null` until
docs/roadmap/10-format-versioning-and-chart-identity.md lands, and the record format version does
NOT change when they start being populated (nullable-by-design).
**Files**: rock-hero-game/core `scoring/score_record.h/.cpp` + tests.
**Public-header impact**: game-scope only.
**Testing**: golden-file round-trip tests; unknown-field tolerance on read (mirrors plan 10's
reader policy); a full Phase 2 replay ends in a serialized record fixture.
**Exit criteria**: schema documented in the header per docs/design/documentation-conventions.md;
27 and 29 can cite this section as the contract.
**Verification**: same two invocations as Phase 2.

### Phase 4 — Failure meter and no-fail (gate-independent; Q2 slot reserved)

**Scope**: meter in [0, 1], start 0.5; v1 constants (ruleset-versioned): confirmed hit +0.005,
committed miss −0.02, fail at 0 — tuned toward GH-expert feel via 23's replay harness against
synthetic miss patterns, not vibes. No-fail ON by default: meter runs and displays but never
ends the song; `result.failed`/`modifiers.noFail` recorded. Failure evaluation reads only the
committed ledger (Phase 2). If Q2 = yes, star-power deploy applies an immediate +0.15 and 2x
meter gain while active (constants land in Phase 6; this phase leaves a named extension point).
**Files/testing**: rock-hero-game/core + unit/replay tests (meter trajectories under fixture
streams; no-fail never terminates).
**Exit criteria**: an expert-shaped fixture (95% accuracy) never approaches fail; a sustained
30%-accuracy fixture fails within ~40 committed notes with fail opt-in.
**Verification**: same two invocations as Phase 2.

### Phase 5 — IMidiTrigger port and JUCE adapter (assumes Q3 = rock-hero-game/audio; Q5 = design-around)

**Scope**: the repo's first MIDI I/O. Port `IMidiTrigger` in
`rock-hero-game/audio/include/rock_hero/game/audio/midi/i_midi_trigger.h`: message-thread API for
enumerate/open/close/device-change listening with typed errors
(docs/design/architectural-principles.md, "Typed Boundary Errors"), plus a wait-free consumer
side: trigger events `{timestampSeconds, action}` drained from an SPSC queue by the game loop.
Mappable trigger: mapping = `{deviceIdentity, matcher, action}`; default matcher = **any note-on
or any CC with value ≥ 64** from the selected device; actions enum starts with `StarPower` and
`Pause` (docs/roadmap/26-game-startup-menus-library.md wires Pause). A capture mode ("press the
pedal now") records the first incoming message as the matcher. Keyboard fallback binding exists
for dev/autoplay use, but the pedal is the primary input per constraint (i). Device
identity + mapping persist through the game settings store
(docs/roadmap/27-in-song-flow-results-profiles.md's IGameSettings); this phase defines the keys.

The JUCE adapter (`rock-hero-game/audio/src/midi/`) bakes in the source-verified facts from the
vendored JUCE 8.0.12 (all paths under `external/tracktion_engine/modules/juce/modules/`):

- Enumeration/open/close/device-change subscription are message-thread-only (debug-asserted):
  `juce_audio_devices/midi_io/juce_MidiDevices.cpp:198-203`,
  `juce_MidiDeviceListConnectionBroadcaster.cpp:228,246`.
- `handleIncomingMidiMessage` runs synchronously on the WinMM driver callback thread — zero
  hops, never the message thread (`juce_audio_devices/native/juce_Midi_windows.cpp:2617-2621 →
  2717-2745 → 2646-2661 → 2991-2998`; `midi_io/ump/juce_UMPInput.cpp:106-109`;
  `juce_MidiDevices.cpp:158-180`). The callback does only wait-free work: decode 2–3 bytes,
  match, push to the SPSC queue. No allocation, locks, or MessageManager calls.
- Persist `MidiDeviceInfo::identifier` (Windows/WinMM = device interface path, stable per
  physical port; `juce_Midi_windows.cpp:2445-2468`), match by identifier first, name as degraded
  fallback; identifiers are backend-specific (`midi_io/juce_MidiDevices.h:121-126`).
- Call `start()` once after open and never toggle it: pre-`start()` messages are dropped, and
  `consume` try-lock contention with `start()`/`stop()` silently drops messages
  (`juce_MidiDevices.cpp:70-75, 160-163, 192`). "Armed" is our own atomic, not `start()`.
- Hot-plug: one `MidiDeviceListConnection` for the app lifetime (`juce_MidiDevices.h:59-88`),
  callbacks on the message thread (`juce_MidiDeviceListConnectionBroadcaster.cpp:285-303`);
  WinMM detection debounces `WM_DEVICECHANGE` by 500 ms
  (`juce_Midi_windows.cpp:3148-3152`), so tolerate ≥ 0.5 s lag. An open `MidiInput` has **no
  disconnect callback** — it just goes silent (`juce_MidiDevices.cpp:142-156`;
  `juce_UMPInput.cpp:111-117`); unplug detection = diff the device list on every change event.
- Timestamps are seconds on the `juce::Time::getMillisecondCounter()` base, arrival-stamped,
  ~1 ms resolution on WinMM (`juce_MidiDevices.h:280-282`; `juce_Midi_windows.cpp:2701-2715`).
  Correlate to song time by sampling `Time::getMillisecondCounterHiRes()` at
  docs/roadmap/12-playback-clock.md sync points; pedal actions need only human-scale precision.
- **Vendored use-after-free bug (Q5)**: `Win32::InputDevice::removeConsumer` calls
  `consumers.add(c)` instead of `remove` (`juce_Midi_windows.cpp:2586-2589`; fixed upstream).
  WinMM devices are shared via a weak-ptr cache (`:2543-2553`), so destroying one of two
  `MidiInput`s on the same identifier leaves a dangling consumer → UAF on the driver thread.
  Mitigation (recommended Q5a): the adapter enforces **exactly one `juce::MidiInput` per device
  identifier process-wide**, and an explicit **pre-implementation checkpoint** verifies that the
  rock-hero-common/audio engine configuration does not open MIDI inputs through Tracktion's
  DeviceManager for the same device (expert-flagged open uncertainty — check engine setup, and
  if it does, disable that device as an engine MIDI input before opening ours).

**Public-header impact**: new game/audio public port + event/mapping value types; JUCE types stay
out of the port (adapter-private), keeping game/core consumers framework-free.
**Testing**: port-level tests with a fake trigger source (mapping/matcher logic, capture mode,
SPSC drain, hot-plug state transitions driven by a fake device list) — pure and CI-safe. The
JUCE adapter gets a manual hardware checklist (enumerate, map, trigger latency feel, unplug ≥
0.5 s + silent-input tolerance, replug reopen) because CI has no MIDI hardware; forum evidence
says some devices never emit unplug events — verify with the actual pedal.
**Exit criteria**: fake-based tests green; hardware checklist executed once with a real
footswitch and results recorded in this plan's status line.
**Verification** (new lib content + new test target → configure):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Configure -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

### Phase 6 — Star power: earning and deploy (assumes Q1 and Q2 outcomes)

**Scope**: earning per the Q1 decision (recommended: derived phrases from a versioned generator
in rock-hero-game/core, cached game-side with its generator version — never written into
packages); GH-shaped economy: completing a phrase (every phrase note committed as hit) = +25%
SP meter; deploy allowed at ≥ 50% via the Phase 5 `StarPower` trigger action; active SP doubles
the current multiplier (to 8x max); drain over 32 beats of song time per full bar
(ruleset-tunable). If Q2 = yes, wire the Phase 4 extension point (+0.15 immediate, 2x meter gain
while active). Deployments counted into the score record (`result.starPowerDeployments`); SP
state changes published as events for docs/roadmap/25-note-highway-3d.md. Vibrato-on-sustain as an
SP filler (whammy analog) is explicitly deferred to a later ruleset version.
**Files/testing**: rock-hero-game/core + unit tests (phrase completion under
provisional/revoked edges — a revoked note retroactively voids its phrase; deploy gating; drain
math in musical time; ladder×SP interaction) and replay fixtures.
**Exit criteria**: deterministic SP trajectories over fixture streams; phrase generation stable
for a fixed generator version.
**Verification**: same two invocations as Phase 2.

## 10. Final acceptance phase

Run the sanctioned bundle as separate invocations from the repo root, then pre-commit:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets clang-tidy
pre-commit run --all-files
```

Acceptance: all green; a full-song replay of an autoplay-bot event stream
(docs/roadmap/23-detection-verification-harness.md) produces a valid score record with 100%
accuracy, max streak, and deterministic SP deployments; a degraded stream exercises provisional
revocation and (fail opt-in) failure.

## 11. Rollback / abort notes

- Phases 1–4 and 6 are additive pure game/core code with no common/editor impact: rollback =
  revert the game/core scoring folder and tests; nothing else in the product can have grown a
  dependency on them before docs/roadmap/21-game-audio-engine-and-session.md wires the session.
- Phase 5 carries the real risk (vendored UAF on the driver callback thread). If instability
  appears with real hardware, fall back in order: (1) keep the port, disable hot-plug reopen
  (manual re-select only — narrows the close/reopen risk window), (2) keyboard-only star power
  temporarily (constraint (i) degraded, flagged in the roadmap), (3) escalate Q5 to patching or
  bumping the submodule with user sign-off. The port boundary means none of these touch scoring.
- If the ruleset constants prove mistuned after real play, bump `rulesetVersion` — records are
  self-describing, so old records stay honest and comparable per version
  (docs/roadmap/29-online-leaderboards.md relies on this).
- If Q1 lands on authored phrases (option b), Phase 6 blocks on
  docs/roadmap/10-format-versioning-and-chart-identity.md and docs/roadmap/40-chart-editing.md;
  everything through Phase 5 still ships, with SP earning stubbed to continuous accrual (Q1c)
  behind the same interfaces if an interim is wanted — record that choice in the roadmap first.
