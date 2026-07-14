# Plan 13 — Audio Device Settings and Calibration

Status: Ready | 2026-07-12 | baseline `refactor @ 75cc26dd`

## Goal

A shared per-app audio-config store **type** in `common/audio/settings/` that **each product
instantiates independently over its own file** — the editor and the game each own their active
device route (opaque blob + resolved identity), per-route input calibration, and (Phase 3) latency
offsets. Shared code, per-app data: exactly one writer per file, so **no `InterProcessLock`**. On
top of that store: the latency-offset model + scoring-offset contract (Phase 3), the headless
play-on-cue calibration capture (Phase 4), and the device-loss policy (Phase 5), all app-agnostic.
docs/design/architecture.md ("Gameplay Systems") mandates "Latency calibration: Built into the
timing architecture from day one, not bolted on later" — this plan is that day-one architecture.

## Non-goals

- The game-side calibration wizard UI and device-setup screens — those ship with
  docs/roadmap/26-game-startup-menus-library.md; this plan lands the headless architecture only.
- Note detection or onset algorithms beyond the minimal self-contained cue detector needed for
  calibration capture (docs/roadmap/22-note-detection.md owns detection).
- Scoring hit-window widths and the provisional-hit machine
  (docs/roadmap/24-scoring-star-power-failure.md consumes the offset contract defined here).
- Instrument profiles, cloud sync, cross-user settings, or per-product settings beyond audio.
- Moving editor workflow state (last-open project, cursors, grid, zoom) out of `EditorSettings`;
  it stays editor-only by design. Output gain and tone-document state stay Tracktion-managed.

## Constraints

Applicable subset of the roadmap's non-negotiable constraints (see docs/roadmap/00-roadmap.md):

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

Downstream consumers (recorded in both directions in docs/roadmap/00-roadmap.md):

- docs/roadmap/24-scoring-star-power-failure.md — consumes the effective-offset contract from
  Phase 3 (measured offsets shift hit-window comparisons; calibration offsets are recorded in the
  score record format).
- docs/roadmap/26-game-startup-menus-library.md — ships the calibration wizard UI and device-setup
  screens over Phase 4's headless capture flow; first-run onboarding starts at device setup;
  consumes Phase 5's device-loss re-setup prompt contract.
- docs/roadmap/21-game-audio-engine-and-session.md — the game session restores the shared device
  state from Phase 1's store at startup; live-monitoring latency stance here informs its per-tone
  latency policy.
- docs/roadmap/12-playback-clock.md — offsets are applied by consumers (scoring, render lead) in the
  clock's audio-sample time domain; they are never baked into the clock itself.
- docs/roadmap/27-in-song-flow-results-profiles.md — device-loss auto-pause interacts with its
  pause/resume pre-roll flow and non-destructive score state.
- docs/roadmap/22-note-detection.md — the capture flow's cue-response detector is deliberately
  self-contained at v1; it may later delegate to 22's onset detector without contract changes.
- docs/roadmap/20-game-architecture-and-render-stack.md — vsync/frame-pacing policy (recorded there)
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
- **Editor workflow state stays in `EditorSettings`** and is never shared (last-open-project,
  cursors, grid, zoom, waveform-visible remain editor-only).
- **Each app is the sole authority for its own audio config.** There is no shared configuration
  file: the per-app store *type* is instantiated independently over each product's own file. The
  editor may additionally consume the game's config **read-only** (one-directional, one-shot at
  toggle-on) via the toggle in plan 48; there is no reverse. (This supersedes the former
  shared-user-audio-settings premise that "a value has exactly one authoritative store" backed by a
  single shared file.)

## Open questions for the user

Mirrored into docs/roadmap/00-roadmap.md "Decisions needed".

1. **Legacy editor-key cleanup after migration.** ~~Options: (A) keep the old `EditorSettings` keys
   for one release after copying them into the shared store; (B) clear them immediately once the
   migration is test-covered.~~ **WITHDRAWN / moot** — per the "no legacy/back-compat/migration
   code at this stage" directive, Phase 2 no longer migrates anything: the editor simply writes its
   device route and calibration to its own `AudioConfigStore`, and the old `EditorSettings` audio
   keys are neither read nor copied. There is no migration to clean up after, so this question no
   longer applies.
2. **Video-offset storage granularity.** Options: (A) one machine-global video offset at v1, with
   the active display name recorded as advisory metadata; (B) key video offsets per monitor
   identity now. **Recommendation: A** — monitor identity APIs are brittle, the wizard remeasures
   in under a minute, and (A) leaves the schema field ready if (B) is ever needed.
3. **Per-product buffer-size override.** ~~Options: (A) one shared device configuration for both
   products (configure once — the absorbed plan's default); (B) add a per-product override layer
   now.~~ **SUPERSEDED** — per-app independence was chosen by user directive: each product owns its
   own audio-config file, so buffer size (and every other device setting) is already per-product by
   construction. The shared-configuration option is withdrawn; there is no shared device
   configuration to override.
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

### Phase 1 — Per-app audio-config store in common

Scope: create ONE shared per-app audio-config store **type** that each product instantiates
independently over its own file — **no shared file, no `InterProcessLock`**. The locked per-app
model gives every file exactly one writer, so the two-writer concurrency machinery is deleted and
the only cross-process interaction (the editor reading the game's file) is one-directional and
read-only. New headers go in a `settings/` feature folder; the error type is a new common-audio
`AudioConfigError`; the calibration schema is the shipped XML-valued property with a `formatVersion`
root — reuse the editor's serialization code verbatim when it moves in Phase 2.

- **`ActiveDeviceRoute` value type** (`common/audio/settings/active_device_route.h`):
  `{ std::string serialized_state; std::optional<InputDeviceIdentity> identity; }`. The opaque blob
  is the JUCE restore payload; the identity is the semantic route it resolves to, persisted
  **together** on each successful apply so availability and mirroring can be answered offline
  without opening a device.
- **`IAudioConfigStore` port:** `activeDeviceRoute()` / `setActiveDeviceRoute()`; per-route
  `inputCalibrationFor` / `saveInputCalibration` / `removeInputCalibration`, returning
  `std::expected<…, AudioConfigError>` (calibration signatures mirror today's `IEditorSettings`
  accessors at `i_editor_settings.h:179-194`), with room for Phase 3's `latencyOffsetsFor` /
  `saveLatencyOffsets` / `removeLatencyOffsets`.
- **`AudioConfigStore` implementation:** `juce::PropertiesFile`, `folderName` =
  `applicationDataFolderName()`, **`applicationName` supplied by a ctor parameter**
  (`std::string_view`), `.settings` suffix, XML storage, **`processLock = nullptr`**. A scoped
  `Access { ReadWrite, ReadOnly }` mode: `ReadOnly` sets `Options.doNotSave = true` (`save()`
  early-returns, `juce_PropertiesFile.cpp:183`) and setters return
  `AudioConfigError{CouldNotSave, "store opened read-only"}`, so the editor's read of the game's
  file can never mutate it. Explicit-path ctor for test isolation.
- **`AudioConfigError` / `AudioConfigErrorCode { InvalidSettingValue, InvalidInputCalibrationHistory,
  CouldNotSave }`** — `<Subject>ErrorCode`/`<Subject>Error`, `code` + `message`. No `ReadOnlySource`
  code (a read-only write is a defended-against no-op reported as `CouldNotSave`, not a distinct
  enum value only one composition emits). The `InvalidInputCalibrationHistory` path preserves the
  editor's malformed-vs-missing distinction so a corrupt game file surfaces as a typed failure
  rather than "no calibration."
- **Two identity constants in `common/audio`** (`settings/audio_config_identity.h`):
  `editorAudioConfigApplicationName()` ("Rock Hero Editor Audio") and
  `gameAudioConfigApplicationName()` ("Rock Hero Game Audio"). They belong beside the store by
  feature-over-kind (they encode the audio-config *file-partition* convention); the generic product
  names stay in `common/core`. Each names a distinct file from the editor's `Rock Hero Editor.settings`
  (workflow state) and the game's `Rock Hero.settings` (profile). The game's audio config being its
  own file is what lets the editor read it read-only without touching the game's profile/library
  data.
- **Multi-input forward-compat requires no type here:** calibration is already a route-keyed map
  (keyed by `InputDeviceIdentity`), inherently N-capable **for channels on the one active device**.
  The player-slot model (`PlayerInputConfig`) is **game/core, not common** (plan 32). Do **not** add
  multiplayer types to the shared store. Record the honest boundary: N *separate interfaces* would
  need a multi-device `ActiveDeviceRoute` and a multi-channel selection surface — out of scope here,
  and not falsely promised as "no rework."
- **Concurrency policy** (retained, minus the lock): write only on successful device apply or
  calibration success; last-writer-wins; never clear stored state merely because an app starts
  without an open device. Cross-process consistency rides JUCE's atomic temp-file+rename XML save
  (`saveAsXml → XmlElement::writeTo → TemporaryFile::overwriteTargetFileWithTemporary`, 5×100 ms
  `replaceFileIn` retry): a concurrent reader sees the complete old or new file, never a torn one.

Files/modules: new `rock-hero-common/audio/include/rock_hero/common/audio/settings/
{i_audio_config_store.h, audio_config_store.h, audio_config_error.h, active_device_route.h,
audio_config_identity.h}`, new `rock-hero-common/audio/src/settings/audio_config_store.cpp` (holding
the private `KeyedRecordStore<Codec>` + `InputCalibrationCodec` once relocated in Phase 2), CMake
source-list additions.

Public-header impact: new public headers for the port, concrete store, typed error,
`ActiveDeviceRoute`, and the two identity constants. The codec and `KeyedRecordStore` stay
`src/`-private.

Testing plan: new `rock-hero-common/audio/tests/test_audio_config_store.cpp` — empty start with a
test-local file; active-route persist/reload/clear **including the paired identity**; calibration
history persist, duplicate-route collapse, per-route remove, gain clamp, malformed-history preserved
as a typed `InvalidInputCalibrationHistory` error; active-route and calibration independence; a
`ReadOnly` instance rejects every setter with `CouldNotSave` and leaves the file byte-unchanged.

Exit criteria: store builds and passes its tests; no editor code touched yet.

Verification:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Configure -Targets rock_hero_common_audio_tests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

### Phase 2 — Editor moves its device route onto its own per-app file

Scope: move the **device-route** value out of `EditorSettings` into the **editor's own**
`AudioConfigStore` instance and re-point (but do **not** yet delete) the calibration accessors so
the build stays green until plan 14 P3 relocates them. **No migration, no legacy read** — per the
"no legacy/back-compat/migration code at this stage" directive the format simply changes: the
editor writes its device route and calibration to its own store, and the old `EditorSettings` audio
keys are never read or copied.

- **Remove `audioDeviceState` from `IEditorSettings` / `EditorSettings`** (`i_editor_settings.h:66`);
  the editor's device persist/restore now targets `activeDeviceRoute()` on the store (capturing
  `serialized_state` **and** the live `currentInputDeviceIdentity()` together on persist).
- **Keep the calibration trio on `IEditorSettings`, delegating to the store (F#4 fix).** The draft's
  "remove the calibration trio in P2" contradicted its own coordination note and would break the
  live call sites `m_settings.inputCalibrationFor` (`input_calibration_handlers.cpp:127`) and
  `m_settings.saveInputCalibration` (`:151`). In P2 the calibration *data* relocates (the
  `KeyedRecordStore<Codec>` + `InputCalibrationCodec` move into `AudioConfigStore`, error type
  renamed to `AudioConfigError`), and `EditorSettings`' calibration methods become thin delegators to
  the editor's `AudioConfigStore`. The `IEditorSettings` calibration methods and those call sites are
  **removed by plan 14 P3** when it relocates the read/write into `LiveInputMonitor` — avoiding a
  double migration and a broken build between phases.
- `EditorController::Services` gains `common::audio::IAudioConfigStore* audio_config_store`;
  `restoreAudioDeviceState()` / `persistAudioDeviceState()` switch to it; last-open-project /
  interrupted-restore / cursor / grid / zoom / waveform-visible stay on `m_settings`.
- **No migration helper.** The format changes outright: the editor writes its device route and
  calibration to its own `AudioConfigStore`, and the old `EditorSettings` `audioDeviceState` /
  `inputCalibrationStates` keys are neither read nor cleared. A user with a pre-existing settings
  file simply reselects the device and recalibrates once. (13-Q1 is withdrawn — see Open questions.)
- `rock-hero-editor/app/main.cpp` constructs one `AudioConfigStore{ editorAudioConfigApplicationName(),
  Access::ReadWrite }` and injects it into Services.
- Add a shared in-memory `IAudioConfigStore` fake under
  `rock-hero-common/audio/tests/include/…/testing/` (full read **and** write, including
  `ActiveDeviceRoute`) for reuse by plan 14, plan 32, and plan 48.

**Coordination with plan 14 (preserved):** P2 retargets **device-route persist/restore** and
relocates the calibration *data* while keeping the `IEditorSettings` calibration methods as
delegators; plan 14 P3 relocates the **calibration read/write call sites** into `LiveInputMonitor`
and then deletes the `IEditorSettings` calibration methods. The editor `useGameAudioSettings` toggle
bool and the effective-source facade land with plan 48, not here.

Files/modules: `rock-hero-editor/core` settings + controller TUs, `rock-hero-editor/app/main.cpp`,
`rock-hero-common/audio` settings sources, test fakes.

Public-header impact: `IEditorSettings` drops `audioDeviceState`; the calibration trio stays until
14 P3.

Testing plan: no migration test (there is no migration). Update `test_editor_settings.cpp` (drop
device-state cases) and `test_editor_controller_restore.cpp` (controller reads/writes the editor's
own `IAudioConfigStore`).

Exit criteria: the editor persists and restores its device route and per-route calibration through
its own `AudioConfigStore`; the old `EditorSettings` audio keys are no longer read; all editor-core
and common-audio tests pass.

Verification:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

### Plan 13 Phases 3-6 — do they still hold under per-app data?

**All hold; per-app-data clarifications only, no structural rewrite.** The Phase 3-6 sections below
carry over from the shared-store design; read them with these per-app substitutions and notes (the
`IUserAudioSettings` name in those sections is now `IAudioConfigStore`, and its persistence lives on
each app's own file):

- **Phase 3 (latency-offset model + scoring-offset contract):** holds. The value types
  (`OutputDeviceIdentity`, `AudioRouteIdentity`, `LatencyOffsets`), the effective-offset contract
  plan 24 consumes, and the staleness rule are app-agnostic pure code added to `IAudioConfigStore`.
  Clarify: offsets are per-app data; the **game's** offsets are scoring-authoritative (the editor
  doesn't score). Per-route latency belongs on the offset record, **not** on game/core's
  `PlayerInputConfig` (latency is per physical route). **Fan-out obligation note (F#8):** introducing
  `latencyOffsetsFor`/`saveLatencyOffsets`/`removeLatencyOffsets` on `IAudioConfigStore` obliges
  *every* implementor to override them — the concrete store, all test fakes, **and plan 48's
  `EditorEffectiveAudioConfigStore`**. If plan 48 has landed, 13 P3 must update the facade in the
  same change or the facade stops compiling; record this in plan 48's forward-dependencies.
- **Phase 4 (headless calibration capture):** holds. The play-on-cue state machine + estimator stay
  headless in common and write `LatencyOffsets` to the **active app's** store; the game native
  wizard (plan 32 / plan 26 Phase 8) and any editor/dev harness drive the same code. Read
  `IUserAudioSettings` as `IAudioConfigStore` throughout.
- **Phase 5 (device-loss policy + backend stance):** holds. Each app handles loss on its own engine;
  the policy shape and ASIO>WASAPI ranking are shared product policy. The "ASIO single-client —
  independent per-app device picks can collide → surfaced as `ApplyFailed`" note is **more relevant**
  now — keep it.
- **Phase 6 (`deviceManager()` port narrowing):** holds — pure cleanup, app-agnostic, independently
  revertible.

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
- **Effective-offset contract** (the normative rule docs/roadmap/24-scoring-star-power-failure.md
  consumes; restated there, defined here):
  - `audio_offset_ms` = `measured_audio_round_trip_ms` when fresh, else
    `reported_input_ms + reported_output_ms`;
  - a detection event timestamped `t_in` in input-stream sample time corresponds to player
    intent `t_play = t_in − audio_offset_ms` when compared against chart time in the playback
    clock domain (docs/roadmap/12-playback-clock.md); hit windows are centered on chart time and
    the *event* is shifted — windows themselves never move per-device;
  - the highway render leads the clock by `video_offset_ms` (consumed via
    docs/roadmap/20-game-architecture-and-render-stack.md's render loop and
    docs/roadmap/25-note-highway-3d.md);
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
wizard (docs/roadmap/26-game-startup-menus-library.md) and any editor/dev harness drive the same
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
  - game contract (implemented by docs/roadmap/26-game-startup-menus-library.md and
    docs/roadmap/27-in-song-flow-results-profiles.md): freeze score/session state
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
  docs/roadmap/21-game-audio-engine-and-session.md.

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

- **Phase 2 changes where the editor reads and writes its device route** (from `EditorSettings`
  to the editor's own `AudioConfigStore`). There is no migration: the old keys are neither read
  nor cleared, so a revert simply points the editor back at the old keys. Any device route or
  calibration written to the new store after the switch is not reflected in the old keys; a user
  who reverts reselects the device and recalibrates once.
- **Phase 4's cue-scheduling seam** may prove inaccurate through the Tracktion transport (the
  juce-tracktion-expert checkpoint decides). Abort path: the state machine and estimator are
  seam-independent; only the cue adapter is replaced (e.g. direct output-callback click render).
- **Phase 5's device-lost signal** may be backend-inconsistent. Fallback: derive loss purely from
  `currentDeviceStatus().open` transitions on the existing change listener — weaker but uniform.
- **Phase 6** is pure cleanup and independently revertible; nothing downstream depends on it.
