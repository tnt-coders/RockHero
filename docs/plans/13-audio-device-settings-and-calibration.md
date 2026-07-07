# Plan 13 — Audio Device Settings and Calibration

Status: Ready | 2026-07-06 | baseline `refactor @ 3c7febe0`

## Goal

One audio-hardware identity for the whole product: the user configures a device, input channel,
sample rate, and buffer size once, and both the editor and the game restore that exact route on
launch. On top of that shared store: a full latency-offset model (input + output + video, per
device identity) whose measured offsets shift scoring hit windows, a headless play-on-cue
calibration capture flow the game wizard can drive, and a device-loss policy so an unplugged
interface never destroys in-progress state. docs/design/architecture.md ("Gameplay Systems")
mandates "Latency calibration: Built into the timing architecture from day one, not bolted on
later" — this plan is that day-one architecture.

## Non-goals

- The game-side calibration wizard UI and device-setup screens — those ship with
  docs/plans/26-game-startup-menus-library.md; this plan lands the headless architecture only.
- Note detection or onset algorithms beyond the minimal self-contained cue detector needed for
  calibration capture (docs/plans/22-note-detection.md owns detection).
- Scoring hit-window widths and the provisional-hit machine
  (docs/plans/24-scoring-star-power-failure.md consumes the offset contract defined here).
- Instrument profiles, cloud sync, cross-user settings, or per-product settings beyond audio.
- Moving editor workflow state (last-open project, cursors, grid, zoom) out of `EditorSettings`;
  it stays editor-only by design. Output gain and tone-document state stay Tracktion-managed.

## Constraints

Applicable subset of the roadmap's non-negotiable constraints (see docs/plans/00-roadmap.md):

- (a) **Layering**: common never depends on editor or game code; anything both products need is
  extracted to rock-hero-common FIRST, as its own phase with tests, before game code consumes it.
  Tracktion headers stay isolated to rock-hero-common/audio implementation files.
- (b) **Public-header minimalism**: only headers that must be public are public; ports-and-adapters
  per docs/design/architectural-principles.md ("Ports and Adapters", "Typed Boundary Errors").
- (c) **Naming firewall**: the commercial real-guitar game that inspired this project is never
  named; use RS/neutral phrasing.
- (h) **Builds**: all build/test/lint commands go through `.agents/rockhero-build.ps1` (usage in
  `.agents/README.md`), never raw cmake/ctest/ninja. Intermediate phases run only the checks their
  changes warrant; the final acceptance phase runs the sanctioned bundle as separate invocations.
- (i) **Real guitar input**: calibration flows assume both hands are on the guitar; capture flows
  must work with a strum as the response gesture, never a keypress mid-measurement.

Design-doc anchors: docs/design/architecture.md "Timing and Latency" (latency chain: ASIO buffer
1.5–5.8 ms, render frame up to 16 ms, display 5–15 ms; scoring comparisons in audio-sample time
with offsets applied consistently), "Threading Model" (graph-rebuilding mutations are
message-thread only), and docs/design/architectural-principles.md "Time Must Be a Dependency" and
"Keep Threading at the Boundary".

## Current state inventory

Verified paths and behavior on the baseline tree:

- **Settings today are editor-owned.** `IEditorSettings`
  (rock-hero-editor/core/include/rock_hero/editor/core/settings/i_editor_settings.h) exposes the
  opaque serialized audio-device state (`audioDeviceState()` / `setAudioDeviceState()`) and the
  per-physical-route input gain calibration (`inputCalibrationFor` / `saveInputCalibration` /
  `removeInputCalibration`). `EditorSettings`
  (rock-hero-editor/core/src/settings/editor_settings.cpp) stores them in a per-user
  `juce::PropertiesFile` (`applicationName` = "Rock Hero Editor", folder "Rock Hero",
  `processLock = nullptr`, XML storage) under keys `audioDeviceState` and the XML-valued
  `inputCalibrationStates` list with a `formatVersion` root (value 1). No shared user-audio store
  exists anywhere: rock-hero-common/audio/include has only `device/`, `engine/`, `input/`,
  `live_rig/`, `plugin/`, `shared/`, `song/`, `transport/` feature folders.
- **App-identity constants already live in common.**
  rock-hero-common/core/include/rock_hero/common/core/shared/application_identity.h provides
  `applicationDataFolderName()` ("Rock Hero") and `editorApplicationName()` ("Rock Hero Editor").
- **The device-configuration port is largely in place.** `IAudioDeviceConfiguration`
  (rock-hero-common/audio/include/rock_hero/common/audio/device/i_audio_device_configuration.h)
  exposes `restoreSerializedDeviceState()`, `serializedDeviceState()`, `currentDeviceStatus()`,
  `currentInputDeviceIdentity()`, and change listeners — plus a public
  `juce::AudioDeviceManager& deviceManager()` accessor whose only production consumer is
  common/audio's own `audio_device_settings.cpp` (line 313); everything else touching it is test
  fakes. `Engine` implements this port (engine/engine.h, multiple-port inheritance).
- **The shared settings workflow already shipped.** `IAudioDeviceSettings` /
  `AudioDeviceSettings` (device/audio_device_settings.h) provide the staged
  audio-system/device/channel/sample-rate/buffer-size edit with apply/cancel/control-panel and
  typed `AudioDeviceSettingsError`. The backend preference policy is implemented and tested:
  rock-hero-common/audio/src/device/audio_device_settings.cpp lines 59–118 rank ASIO first, then
  WASAPI Exclusive > WASAPI Low Latency > WASAPI Shared, then DirectSound and WaveOut last, with
  written rationale.
- **Device-reported latency is already surfaced.** `AudioDeviceStatus`
  (device/audio_device_status.h) carries `input_latency_ms` and `output_latency_ms`.
- **Route identity and gain calibration exist; latency calibration does not.**
  `InputDeviceIdentity` (input/input_device_identity.h) keys a physical route by backend + input
  device + channel index (channel name is metadata). `InputCalibrationState`
  (input/input_calibration_state.h) is a *gain* record (`Gain` + identity, clamped ±24 dB via
  shared/gain.h). Searches for video latency, latency offsets, or hit windows find nothing —
  the offset model in this plan is entirely new code.
- **The editor controller is the only settings consumer.**
  `EditorController::Impl::restoreAudioDeviceState()` / `persistAudioDeviceState()`
  (rock-hero-editor/core/src/controller/editor_controller.cpp:1966–1991) restore and persist the
  serialized state through the port and `m_settings`; a failed restore clears the stored value.
- **Tracktion property storage is already shared.** `Engine::Engine()`
  (rock-hero-common/audio/src/engine/engine.cpp:76–86) constructs `tracktion::Engine` with
  `core::applicationDataFolderName()`, so the Tracktion `Settings.xml` (known-plugins list, scan
  results) is one file for both products.
- **The game is a skeleton.** rock-hero-game/app/main.cpp is an 81-line JUCE DocumentWindow shell;
  game core/audio/ui are placeholder static libraries. Nothing game-side consumes audio settings.
- Test targets: `rock_hero_common_audio_tests` and `rock_hero_editor_core_tests`, run via
  `.agents/rockhero-build.ps1 -RunTouchedTests`.

Verified against code on 2026-07-06, refactor @ 3c7febe0.

## Dependencies

Upstream: none — this is a foundation plan and can start immediately.

Downstream consumers (recorded in both directions in docs/plans/00-roadmap.md):

- docs/plans/24-scoring-star-power-failure.md — consumes the effective-offset contract from
  Phase 3 (measured offsets shift hit-window comparisons; calibration offsets are recorded in the
  score record format).
- docs/plans/26-game-startup-menus-library.md — ships the calibration wizard UI and device-setup
  screens over Phase 4's headless capture flow; first-run onboarding starts at device setup;
  consumes Phase 5's device-loss re-setup prompt contract.
- docs/plans/21-game-audio-engine-and-session.md — the game session restores the shared device
  state from Phase 1's store at startup; live-monitoring latency stance here informs its per-tone
  latency policy.
- docs/plans/12-playback-clock.md — offsets are applied by consumers (scoring, render lead) in the
  clock's audio-sample time domain; they are never baked into the clock itself.
- docs/plans/27-in-song-flow-results-profiles.md — device-loss auto-pause interacts with its
  pause/resume pre-roll flow and non-destructive score state.
- docs/plans/22-note-detection.md — the capture flow's cue-response detector is deliberately
  self-contained at v1; it may later delegate to 22's onset detector without contract changes.
- docs/plans/20-game-architecture-and-render-stack.md — vsync/frame-pacing policy (recorded there)
  bounds how stable the video offset is.

## Decisions already made

- **Latency calibration is built in from day one** — docs/design/architecture.md, "Gameplay
  Systems" and "Timing and Latency" (calibration system: player strums, system measures delay;
  offset applied to scoring so hits align with what the player perceives).
- **All scoring comparisons happen in audio-sample time with calibration offsets applied
  consistently**; mixing audio-thread time with render-thread time in scoring is prohibited —
  docs/design/architecture.md, "Timing and Latency".
- **Backend preference order** ASIO > WASAPI Exclusive > WASAPI Low Latency > WASAPI Shared >
  DirectSound > WaveOut is implemented shared policy with rationale —
  rock-hero-common/audio/src/device/audio_device_settings.cpp:59–118 and
  test_audio_device_settings.cpp.
- **Audio-device configuration is project-owned; Tracktion's `Settings.xml` is a non-authoritative
  internal cache.** The project-owned restore overwrites it on startup and it is never read as the
  authority; the known-plugins list stays Tracktion-owned in that shared file — absorbed from
  the former shared-user-audio-settings todo plan (now deleted; this plan absorbed it).
- **Calibration is per physical input route** (backend + device + channel index; channel display
  name is metadata); same device but a different channel is a different route —
  docs/completed/per-device-input-calibration-plan.md.
- **Calibration never enters project packages or tone documents**; it is app-local user state —
  docs/completed/per-device-input-calibration-plan.md.
- **Editor workflow state stays in `EditorSettings`** and is never shared; **a value has exactly
  one authoritative store** — anything plausibly shared (e.g. buffer size) belongs to the shared
  store, with Tracktion/JUCE copies derived — absorbed from
  the former shared-user-audio-settings todo plan.

## Open questions for the user

Mirrored into docs/plans/00-roadmap.md "Decisions needed".

1. **Legacy editor-key cleanup after migration.** Options: (A) keep the old `EditorSettings` keys
   for one release after copying them into the shared store; (B) clear them immediately once the
   migration is test-covered. **Recommendation: B** — single-developer project, migration is
   covered by tests in Phase 2, and leaving two copies invites drift-and-debug sessions.
2. **Video-offset storage granularity.** Options: (A) one machine-global video offset at v1, with
   the active display name recorded as advisory metadata; (B) key video offsets per monitor
   identity now. **Recommendation: A** — monitor identity APIs are brittle, the wizard remeasures
   in under a minute, and (A) leaves the schema field ready if (B) is ever needed.
3. **Per-product buffer-size override.** Options: (A) one shared device configuration for both
   products (configure once — the absorbed plan's default); (B) add a per-product override layer
   now. **Recommendation: A**; add an override only when the game demonstrates a concrete need
   (e.g. editor wants larger buffers for heavy plugin chains).
4. **WASAPI-Shared during gameplay.** Options: (A) allow, with a visible high-latency warning in
   the setup UI when the reported round trip exceeds a threshold (~15 ms, tuned later); (B) allow
   detection/scoring but force live monitoring off; (C) refuse shared mode for gameplay.
   **Recommendation: A** — calibration makes scoring correct at any stable latency; monitoring
   feel is the user's trade-off to make, and (C) would lock out users with no ASIO driver.
5. **Device-loss recovery.** Options: (A) auto-pause + non-destructive state + explicit re-setup
   prompt, never silently switching hardware; (B) attempt a silent re-open of the default device.
   **Recommendation: A** — a silent device swap changes latency and monitoring without
   recalibration and can blast a living room at the wrong volume.

## Phased implementation

### Phase 1 — Shared user-audio settings store in common

Scope: create the shared per-user store both products read, absorbing the design from the former
shared-user-audio-settings todo plan (deleted after absorption) with these corrections against
the current tree:
new headers go in a `settings/` feature folder (the absorbed doc predates feature folders); the
error type is a new common-audio `UserAudioSettingsError` (the editor's `EditorSettingsError`
stays editor-owned); the `inputCalibrationStates` schema is the shipped XML-valued property with
a `formatVersion` root (the absorbed doc's description is current — reuse the editor's
serialization code verbatim when it moves in Phase 2).

- `IUserAudioSettings` port: `audioDeviceState()` / `setAudioDeviceState()`, per-route
  `inputCalibrationFor` / `saveInputCalibration` / `removeInputCalibration` (signatures mirror
  today's `IEditorSettings` methods, returning `std::expected<..., UserAudioSettingsError>`), plus
  the Phase 3 offset accessors (landed there, port designed here with room for them).
- `UserAudioSettings` implementation: `juce::PropertiesFile` with `folderName` "Rock Hero"
  (reuse `applicationDataFolderName()`), `applicationName` "Rock Hero User Audio" (new constant
  beside `editorApplicationName()` in
  rock-hero-common/core/include/rock_hero/common/core/shared/application_identity.h),
  `.settings` suffix, XML storage, and — unlike today's editor store, which sets
  `processLock = nullptr` — a `juce::InterProcessLock`, because editor and game can run
  concurrently. Explicit-path constructor for test isolation. Writes save immediately.
- Concurrency policy (absorbed, still valid): write only on successful device apply or
  calibration success; last-writer-wins; never clear stored state merely because an app starts
  without an open device; no cross-process refresh path until a real conflict is user-visible.

Files/modules: new `rock-hero-common/audio/include/rock_hero/common/audio/settings/
{i_user_audio_settings.h, user_audio_settings.h, user_audio_settings_error.h}`, new
`rock-hero-common/audio/src/settings/user_audio_settings.cpp`, one new constant in
application_identity.h, CMake source-list additions.

Public-header impact: three new public headers (the port, the concrete store for app composition,
the typed error). Nothing else becomes public.

Testing plan: new `rock-hero-common/audio/tests/test_user_audio_settings.cpp` — empty start with a
test-local file; device-state persist/reload/clear; calibration history persist, duplicate-route
collapse, per-route remove, gain clamp, malformed-history preservation as a typed error (mirror
the cases in rock-hero-editor/core/tests/test_editor_settings.cpp, which the schema comes from);
device state and calibration independence.

Exit criteria: store builds and passes its tests; no editor code touched yet.

Verification (graph changed → configure; code + tests changed → build + tests):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Configure -Targets rock_hero_common_audio_tests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

### Phase 2 — Editor migrates to the shared store

Scope: move the two audio-owned values out of `EditorSettings` and point the controller at the
shared store, per the absorbed plan.

- Remove `audioDeviceState` and the calibration trio from `IEditorSettings`/`EditorSettings`;
  relocate the `inputCalibrationStates` XML serialization code into `UserAudioSettings` unchanged.
- `EditorController::Services` gains `common::audio::IUserAudioSettings* user_audio_settings`;
  `restoreAudioDeviceState()` / `persistAudioDeviceState()`
  (editor_controller.cpp:1966–1991) and the input-calibration load/save paths switch stores;
  last-open-project / interrupted-restore / cursor / grid / zoom stay on `m_settings`.
- One-time migration helper `migrateEditorAudioSettings(EditorSettings&, IUserAudioSettings&)` in
  rock-hero-editor/core (editor-specific policy stays out of common): copy device state and
  calibration history only when the shared store lacks them; apply open question 1's answer for
  legacy-key cleanup (recommended: clear immediately, under test).
- rock-hero-editor/app/main.cpp constructs one `UserAudioSettings` and passes it into Services.
- Update `NullEditorSettings` and controller test fakes; add a common-audio null/in-memory
  `IUserAudioSettings` fake under rock-hero-common/audio/tests/include for reuse by future game
  tests.

Files/modules: rock-hero-editor/core settings + controller TUs, rock-hero-editor/app/main.cpp,
rock-hero-common/audio settings sources, test fakes.

Public-header impact: `IEditorSettings` shrinks (methods removed); no new public headers beyond a
testing fake header.

Testing plan: new `rock-hero-editor/core/tests/test_editor_audio_settings_migration.cpp`
(copy-when-missing, never-overwrite-existing, tolerate-missing-source, idempotent re-run);
update test_editor_settings.cpp (drop moved cases), test_editor_controller_restore.cpp and
test_editor_controller_input_calibration.cpp (controller reads/writes the shared port).

Exit criteria: editor behaves identically end-to-end (device restore, calibration restore per
route); old keys migrated; all editor-core and common-audio tests pass.

Verification:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

### Phase 3 — Latency-offset model and the scoring-offset contract

Scope: the offset schema, persistence, and the contract downstream plans consume. All new pure
value types and functions; no DSP.

- `OutputDeviceIdentity` (backend name + output device name) beside `InputDeviceIdentity`;
  `AudioRouteIdentity` pairing them — the measured audio round trip is a property of the full
  route, not the input alone.
- `LatencyOffsets` per `AudioRouteIdentity`:
  - `reported_input_ms` / `reported_output_ms` — seeded from `AudioDeviceStatus` (already
    surfaced today) at measurement time;
  - `measured_audio_round_trip_ms` (optional) — wizard result: strum-on-audio-cue delta,
    bundling output latency + input latency + the player/hardware chain;
  - `measured_video_offset_ms` (optional) — strum-on-visual-cue delta minus the audio round
    trip; stored machine-global at v1 per open question 2's recommendation;
  - a measurement snapshot (sample rate, buffer size) — if the active route's sample rate or
    buffer size differs from the snapshot, the measurement is **stale**: consumers fall back to
    reported latencies and the product prompts recalibration. Buffer-size changes change real
    latency; silently reusing a stale measurement would corrupt scoring.
- **Effective-offset contract** (the normative rule docs/plans/24-scoring-star-power-failure.md
  consumes; restated there, defined here):
  - `audio_offset_ms` = `measured_audio_round_trip_ms` when fresh, else
    `reported_input_ms + reported_output_ms`;
  - a detection event timestamped `t_in` in input-stream sample time corresponds to player
    intent `t_play = t_in − audio_offset_ms` when compared against chart time in the playback
    clock domain (docs/plans/12-playback-clock.md); hit windows are centered on chart time and
    the *event* is shifted — windows themselves never move per-device;
  - the highway render leads the clock by `video_offset_ms` (consumed via
    docs/plans/20-game-architecture-and-render-stack.md's render loop and
    docs/plans/25-note-highway-3d.md);
  - offsets are applied at consumption, never baked into the playback clock.
- Persistence: `IUserAudioSettings` gains `latencyOffsetsFor(route)` / `saveLatencyOffsets` /
  `removeLatencyOffsets`, stored as a second XML-valued property with its own `formatVersion`
  root, same validation posture as the calibration history (malformed preserved + typed error).

Files/modules: new headers in rock-hero-common/audio settings/ (identities may live in input/ or
a shared route header — follow the placement procedure in
docs/design/architectural-principles.md "Placement Procedure for New Files"); pure functions for
effective-offset selection and staleness.

Public-header impact: new value-type headers plus the port additions. Value types are
framework-free so game/core scoring can consume them without pulling JUCE (game links common
regardless; keep the headers std-only like input_device_identity.h).

Testing plan: extend test_user_audio_settings.cpp (offset persist/reload, staleness on
buffer-size and sample-rate change, malformed preservation); new pure tests for effective-offset
selection (measured-fresh, measured-stale, unmeasured fallback).

Exit criteria: contract documented in the headers; tests pass; 24 can cite the rule verbatim.

Verification:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets rock_hero_common_audio_tests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

### Phase 4 — Calibration capture flow (headless architecture)

Scope: the play-on-cue measurement machinery, headless in common per constraint (a) so the game
wizard (docs/plans/26-game-startup-menus-library.md) and any editor/dev harness drive the same
code.

- **Audio round trip**: schedule N cue clicks at known output times through the engine's
  transport/song-audio path; the player strums on each cue; a self-contained energy-threshold
  onset detector on the live-input tap timestamps responses in input-stream sample time; offset
  estimate = robust median of (response − cue) with outlier rejection and a minimum-sample
  confidence gate. Pure estimation math (pairing, median, rejection) is a free function set,
  unit-tested without audio.
- **Video offset**: same capture shape with visual cues (wizard renders the cue; ships with 26);
  stored per open question 2.
- The capture workflow is a headless state machine (idle → cueing → collecting → result/failed)
  driven through existing common/audio ports (`ITransport`/song audio for cues, `ILiveInput` tap
  for responses). Follow docs/design/architectural-principles.md "Separate State From Side
  Effects": the state machine is pure; the audio wiring is an adapter. Threading per
  "Keep Threading at the Boundary": sample capture on the audio side via the existing lock-free
  patterns, decisions on the message thread.
- Checkpoint: **verify with juce-tracktion-expert before implementing** the exact cue-scheduling
  seam (whether Tracktion transport playback timestamps cue onsets accurately enough, or the cue
  must be rendered through a click/metronome path with known sample offsets) — cite file:line
  findings in the implementation commit.
- On success the workflow writes `LatencyOffsets` (with the measurement snapshot) through
  `IUserAudioSettings` for the active `AudioRouteIdentity`. Gain calibration
  (docs/completed/per-device-input-calibration-plan.md) remains a separate, already-shipped
  workflow; the wizard sequences them (gain first, then latency) but the workflows stay
  independent.

Files/modules: rock-hero-common/audio settings/ or a new `calibration/` feature folder (placement
procedure again); pure estimation TU + adapter TU.

Public-header impact: one workflow port header + result value types; the onset threshold detector
stays private to the implementation.

Testing plan: pure tests for the estimator (clean pairs, jittered pairs, dropped responses,
outliers, too-few-samples failure); state-machine tests with scripted cue/response feeds; no
audio-hardware tests (adapter smoke coverage only if a seam proves testable against the existing
engine test rig).

Exit criteria: a scripted capture session produces a stored, staleness-stamped offset; wizard UI
can be built on the port without touching common again.

Verification:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Configure -Targets rock_hero_common_audio_tests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

### Phase 5 — Device-loss policy and backend stance

Scope: the defined behavior when hardware disappears, plus the recorded ASIO/WASAPI stance.

- **Detection surface**: today `IAudioDeviceConfiguration::Listener` fires a generic
  `onAudioDeviceConfigurationChanged()`. Add a distinct device-lost signal (or a documented
  derivation: `currentDeviceStatus().open` transitioning false without a user-initiated apply).
  Checkpoint: **verify with juce-tracktion-expert** how JUCE reports device removal per backend
  (change-listener vs error callback, and whether ASIO removal is reported at all) before
  choosing the surface; cite file:line findings.
- **Policy** (assumes open question 5's recommendation A):
  - auto-pause transport immediately on loss; mutations that rebuild graphs stay on the message
    thread per docs/design/architecture.md "Threading Model";
  - never clear the persisted device state or calibration on loss (the store keeps the user's
    choice for when the device returns);
  - editor: pause + non-blocking notice; existing settings window path is the recovery UI;
  - game contract (implemented by docs/plans/26-game-startup-menus-library.md and
    docs/plans/27-in-song-flow-results-profiles.md): freeze score/session state
    non-destructively, prompt re-setup, resume through 27's pre-roll; a changed route marks
    latency measurements stale by Phase 3's snapshot rule automatically.
- **Backend stance** (recorded here as the product position; setup UI text ships with 26):
  - ASIO preferred — lowest latency (64–256-sample buffers per docs/design/architecture.md
    "Timing and Latency"); note ASIO drivers are typically single-client, so editor + game
    running simultaneously may fail to open the same device: surfaced as the existing typed
    `ApplyFailed`, not treated as device loss;
  - WASAPI Exclusive — near-ASIO latency, locks the device; first fallback;
  - WASAPI Low Latency — best shared-mode option (event-driven small buffers);
  - WASAPI Shared — highest WASAPI latency (OS mixer); live tone monitoring becomes noticeably
    delayed but detection + scoring stay correct once calibrated; per open question 4's
    recommendation, allowed with a warning above ~15 ms reported round trip;
  - DirectSound/WaveOut — legacy, ranked last, never recommended, not blocked.
  This stance matches (and cites) the shipped ranking in
  rock-hero-common/audio/src/device/audio_device_settings.cpp:59–118; live-monitoring latency
  interacts with per-tone plugin latency, whose policy belongs to
  docs/plans/21-game-audio-engine-and-session.md.

Files/modules: i_audio_device_configuration.h (listener addition), engine device-config TU,
editor controller device handling.

Public-header impact: one listener method or status-derivation doc on the existing port header.

Testing plan: engine adapter test simulating device disappearance (the existing configurable
device-configuration test fake supports this); editor-core test that loss pauses transport and
preserves stored state.

Exit criteria: pulling the interface mid-playback in a manual editor session pauses cleanly,
preserves settings, and recovers through the settings window; automated tests cover the
state-preservation rules.

Verification:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

### Phase 6 — Device-port narrowing completion (cleanup)

Scope: finish the narrowing the absorbed docs proposed, corrected against today's code: the
project-owned snapshot (`AudioDeviceStatus`), serialized restore/capture, and the staged
settings workflow (`IAudioDeviceSettings`) all already exist, so the only remaining step is
removing the public `deviceManager()` accessor from `IAudioDeviceConfiguration` — its sole
production consumer is common/audio's own `audio_device_settings.cpp:313`. Give
`AudioDeviceSettings` an internal construction path to the manager (adapter-internal seam inside
common/audio), keep manager access in adapter tests only, and drop the accessor from the port.
Also carry the still-valid view follow-up from
docs/todo/audio-device-settings-extraction-followups.md relevant at this boundary: a
ChangeListener re-derivation smoke test (external device change while a settings edit is open
re-reads staged state) — it pays off twice once the game settings view exists. The remaining
follow-ups in that doc (editor view file split, window-size caps) are editor-UI concerns left to
the roadmap's disposition table.

Files/modules: i_audio_device_configuration.h, engine.h/engine_device_config.cpp,
audio_device_settings.cpp, test fakes.

Public-header impact: net removal — the JUCE manager type disappears from the port per
constraint (b).

Testing plan: existing settings/engine tests keep passing; new re-derivation smoke test in
test_audio_device_settings.cpp.

Exit criteria: no public port exposes `juce::AudioDeviceManager`; grep confirms zero product-code
consumers.

Verification:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

## Final acceptance phase

Per constraint (h), run the sanctioned bundle as separate invocations from the repo root, plus
formatting:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets clang-tidy
pre-commit run --all-files
```

Acceptance: editor and game (when it arrives) share one user-audio settings file for device state,
gain calibration, and latency offsets; editor-only workflow state remains in `EditorSettings`;
game code has no editor dependency; the effective-offset contract is documented and test-covered;
device loss is non-destructive; no public port exposes JUCE device-manager types.

## Rollback/abort notes

- **Phase 2 is the risky one** (live user settings move). The migration is copy-not-move until
  the cleanup step; reverting the commit restores the editor store as authority with no data
  loss, because the shared store was only ever written from it. If open question 1 resolves to
  immediate clearing, keep the clearing in a separate commit from the copy.
- **Phase 4's cue-scheduling seam** may prove inaccurate through the Tracktion transport (the
  juce-tracktion-expert checkpoint decides). Abort path: the state machine and estimator are
  seam-independent; only the cue adapter is replaced (e.g. direct output-callback click render).
- **Phase 5's device-lost signal** may be backend-inconsistent. Fallback: derive loss purely from
  `currentDeviceStatus().open` transitions on the existing change listener — weaker but uniform.
- **Phase 6** is pure cleanup and independently revertible; nothing downstream depends on it.
