# Plan 14 — Shared Live-Input Monitoring Service

Status: Ready | 2026-07-12 | baseline `refactor @ 75cc26dd`

## Goal

One shared code path routes the player's real guitar through the loaded arrangement's tone rig for
**both** products. The editor's working "calibrate-first" live-input monitoring gate — enable
monitoring only when a calibration matching the current input route exists — moves out of
`rock-hero-editor/core` into a new `rock-hero-common/audio` service, `LiveInputMonitor`, which also
owns the calibration measurement/commit orchestration. The editor becomes a thin driver of that
service (it decides *when* to gate; the service owns *how*); the game (`GameplaySession`) drives the
**same** service. Both products drive that one gate, each over its **own** `IAudioConfigStore`: the
game plays the live guitar using the **game's own** native calibration, and the editor over the
editor's own. The shared artifact is the gate code path, **not** a shared calibration. There is no
second gate, no duplicated calibration policy, and no unity-gain shortcut: an uncalibrated or
mismatched device yields silent (disabled) monitoring, never uncalibrated live audio. This delivers
plan 21's Phase 6 soak line "live guitar audible through the authored tone" as a shared,
calibrate-first behavior instead of an editor-only one.

## Non-goals

- The shared per-app audio-config store itself — `common::audio::IAudioConfigStore` (active device
  route + per-route calibration) is docs/roadmap/13-audio-device-settings-and-calibration.md Phase 1:
  one shared store **type** that each product instantiates over its **own** file (exactly one writer
  per file, so **no `InterProcessLock`**). Migrating the editor onto its own instance is that plan's
  Phase 2. This plan **consumes** the store type; it does not build it.
- Any game device/calibration UI. The game ships with zero JUCE UI (SDL3/bgfx only); its headless
  native audio-config drivers are docs/roadmap/32-game-native-audio-config.md (which drive this plan's
  shared measurement/commit over the game's own store), and its SDL device picker and calibration
  wizard are docs/roadmap/26-game-startup-menus-library.md Phase 8 over those drivers. This plan
  builds the shared gate only.
- The latency-offset model, scoring hit-window shifts, and the play-on-cue latency capture flow
  (docs/roadmap/13 Phases 3-4). A calibrated **gain** route is fully audible with no latency
  compensation — detection taps the raw pre-rack signal (i_live_input.h:22-26 dry-tap contract), so
  monitoring feel is independent of the offset model.
- The calibration **measurement math** (`InputCalibrationCapture`, `calculateInputCalibration`) —
  already shared in `common/audio/input/input_calibration.h`; reused unchanged.
- Device-loss policy (docs/roadmap/13 Phase 5) and the `deviceManager()` port narrowing
  (docs/roadmap/13 Phase 6).
- Editor workflow/view-state redesign. The editor's `InputCalibrationStatus`/`InputCalibrationPrompt`
  view types and disabled-message strings stay editor-owned; only the port-driving policy moves.

## Constraints

Applicable subset of the roadmap's non-negotiable block (see docs/roadmap/00-roadmap.md):

- (a) **Layering**: `common` never depends on `editor` or `game`; anything both products need is
  extracted to `rock-hero-common` FIRST, as its own phase with tests, before game code consumes it.
  Tracktion headers stay isolated to `rock-hero-common/audio` implementation files. The service is
  placed in `common/audio` (not `common/core`): it drives `ILiveInput` and references
  `InputCalibrationState`/`Gain`, all `common/audio` types, and `common/audio` already publicly links
  `common/core` — placing it in `common/core` would invert that dependency.
- (b) **Public-header minimalism**: only headers that must be public are public; ports-and-adapters
  per docs/design/architectural-principles.md ("Ports and Adapters", "Typed Boundary Errors").
- (c) **Naming firewall**: the commercial real-guitar game that inspired this project is never named;
  use "RS"/neutral phrasing.
- (h) **Builds**: all build/test/lint commands go through `.agents/rockhero-build.ps1` (usage in
  `.agents/README.md`) — never raw cmake/ctest/ninja. Intermediate phases run only the checks their
  changes warrant; the final acceptance phase runs the sanctioned bundle as separate invocations.
  `clang-tidy` is on-demand only (final phase / user trigger), never routine post-change.
- (i) **Real guitar input**: the live path is a real instrument through an arbitrary VST chain; both
  the player's hands are on the guitar, so the game never assumes a keypress to arm monitoring.

Binding design-doc rules also obeyed: coding-conventions.md ("Recoverable Error Returns" — return
`std::expected<T, DomainError>`; `std::optional<Error>` as an operation result is FORBIDDEN; each
public error domain owns `<Subject>ErrorCode` + `<Subject>Error`; cross-domain translation exposes
the receiving API's *coarser* code and preserves detail in `message`, "Listener Interfaces vs Observer
Structs", "View And Component Suffixes", "Projection Modules", "Parameter Passing" — `const&` for
non-trivial read-only inputs, no top-level `const` on by-value params; CamelCase types / camelCase
methods / `m_lower_case` members / `lower_case` locals; **no namespace aliases** — write `tracktion::`,
never `te::`), and architectural-principles.md ("Separate State From Side Effects", "Sum Types vs
Interfaces", "Keep Threading at the Boundary", "Placement Procedure for New Files"). Core MAY log;
best-effort helpers that log are explicitly sanctioned.

## Current state inventory

Verified against code on 2026-07-12, refactor @ 75cc26dd. `file:line` values are re-checked at
execution per the plan-phase baseline rule.

- **The editor gate is the code to move.** `EditorController::Impl::applyLiveInputGate()`
  (rock-hero-editor/core/src/input_calibration/input_calibration_handlers.cpp:189-243) is the ordered,
  level-triggered gate. Preconditions, in order: (1) always tear down calibration-audition monitoring
  first (`setCalibrationInputMonitoringBestEffort(false)`, :191); (2) NOT `audioDeviceSettingsOpen()`
  (:193); (3) `project_audio_ready` AND `arrangement_loaded` (:200-204); (4)
  `current_input_device_identity` present (:206-210); (5) a saved calibration exists (:212-218);
  (6) the calibration matches the physical route via `calibrationMatches(...)` (:220-224); then arm —
  `setInputGain(gain)` (:226), `setLiveInputMonitoringEnabled(true)` (:234); each arm failure marks the
  backend unavailable and disables (:227-240). The gate returns `void`; "why off" is only log-context
  strings. **It calls no `updateView()`** — the surrounding lifecycle handler repaints once.
- **Route snapshot / rollback and best-effort helpers.** `InputCalibrationRouteState`
  (`input_gain`/`live_input_monitoring_enabled`/`calibration_input_monitoring_enabled`) is declared on
  `EditorController::Impl` (editor_controller_impl.h:371-376) with a doc comment stating it "is a
  backend (live-input port) concern owned by the controller, not the headless workflow."
  `currentInputCalibrationRouteState()` / `restoreInputCalibrationRouteStateBestEffort()`
  (handlers.cpp:246-274) and `setLiveInputMonitoringBestEffort` / `setCalibrationInputMonitoringBestEffort`
  / `setInputGainBestEffort` (handlers.cpp:276-313) are pure `ILiveInput`-driving that log on failure
  through the editor shim `logEditorControllerBestEffortFailure`.
- **Commit / measurement orchestration** lives on `Impl` and is **shot through with interior
  `updateView()` calls** (editor-only): `onInputCalibrationMeasurementStarted` (handlers.cpp:23-74,
  interior `updateView()` at :40, :72), `commitInputCalibration` (:316-384, at :339, :356, :376, :382),
  `restoreCalibrationMeasurementState` (:387-490, at :405). Commit distinguishes
  `LiveInputErrorCode::InputRouteUnavailable` from other failures to choose rollback (:335-357,
  :350-359, :459-462). `selectInputCalibrationForCurrentRoute` (:120-141, the store read via
  `m_settings.inputCalibrationFor`, which returns
  `std::expected<std::optional<InputCalibrationState>, EditorSettingsError>` per
  i_editor_settings.h:177-179), `saveActiveInputCalibration` (:144-153, the store write),
  `executeInputCalibrationEffects` (:156-176), `inputCalibrationContext()` (:179-186, which samples
  identity **once** at :184 and threads it). The only editor coupling here is `m_settings`
  (`IEditorSettings`) for load/save, plus `updateView()`/`reportError()`/`m_transport.pause()`.
- **`InputCalibrationWorkflow`** (rock-hero-editor/core/src/input_calibration/input_calibration_workflow.{h,cpp})
  is a pure state machine that makes NO port calls — it returns Effects/plans. Every pure method
  (`requestPrompt` workflow.cpp:109, `snapshot` :131, `prepareMeasurementStart` :168, `prepareCommit`
  :213, `prepareMeasurementRestore` :295, `calibrationMatches` :160) consumes
  `Context::current_input_device_identity` (workflow.h:42). Its **only** editor couplings are the two
  view-state `#include`s (workflow.h:13-14) and the view projection: the `Snapshot` struct
  (workflow.h:59-78) plus `snapshot()`/`status()`/`disabledMessage()`/`promptGainDb()` (workflow.cpp:131-158,
  361-419), where `status()` returns the **editor** enum `InputCalibrationStatus` (workflow.h:295).
- **The gate re-runs at 10 editor sites** (level-triggered): constructor tail
  (editor_controller.cpp:~1053), device-config change (~1428), settings open (~1447) / close (~1455),
  and every `m_project_audio_ready` edge in project_handlers.cpp (including the arrangement switch via
  the shared rig-load stage). Route-change sites call `selectInputCalibrationForCurrentRoute()` then
  `applyLiveInputGate()`; project-lifecycle edges call `applyLiveInputGate()` alone (no store re-read).
  `EditorController::Impl` is itself `private common::audio::IAudioDeviceConfiguration::Listener`
  (editor_controller_impl.h:93) and holds a `ScopedListener` (:628-631).
- **The shared substrate already exists in `common/audio` `input/`** and is reused as-is: `ILiveInput`
  (i_live_input.h — `inputGain()`/`setInputGain()` :38-45, monitoring getters/setters :57-79; the three
  setters return `std::expected<void, LiveInputError>`; **"All methods are message-thread operations."**
  :19; dry-tap contract :22-26); `InputCalibrationState` with
  `inputCalibrationMatchesPhysicalRoute(state, identity)`; `InputDeviceIdentity` with
  `samePhysicalInputRoute` and `isValidInputDeviceIdentity`; `Gain` + `clampGain` (default 0 dB unity,
  ±24 dB). NOTE: there is **no** free `calibrationMatches` symbol — `calibrationMatches` is a workflow
  **method** (workflow.cpp:160) delegating to the free `inputCalibrationMatchesPhysicalRoute`.
- **The current route comes from a shared port.** `IAudioDeviceConfiguration::currentInputDeviceIdentity()`
  → `std::optional<InputDeviceIdentity>`, and its `Listener::onAudioDeviceConfigurationChanged()` is a
  JUCE `ChangeListener` callback (engine.cpp:67 `changeListenerCallback`, registered on the device
  manager at engine.cpp:109) → **delivered on the message thread**. `Engine` implements this port and
  `ILiveInput`, so the live-input backend is already in-process for both products.
- **The game session is code-complete (plan 21) and injects from `app/`.** `GameplaySession`
  (rock-hero-game/core/{include,src}/.../session/gameplay_session.{h,cpp}) takes six audio ports by
  reference (gameplay_session.h:104-107; ctor at .cpp:40-54), all satisfied by the single
  `common::audio::Engine` composed in rock-hero-game/app/main.cpp. It reaches
  `GameplaySessionStage::Ready` in `onRigLoadCompleted` (gameplay_session.cpp:433) — the game's
  readiness edge — and tears down in `close()` (:344-375); `restart()`/replay do no rig work
  (:246-262). **The rig-load completion is delivered on the message thread**: `Engine::loadLiveRig`
  refuses off-thread (engine_live_rig.cpp:884-888), and its continuations run through
  `juce::MessageManager::callAsync` (`yieldThenContinue`, :1382-1396) with the synchronous empty-tone
  path finalizing inline within the message-thread `loadLiveRig` call; `finalizeLiveRigLoad` fires
  `operation->on_result(...)` at :1236. The session does **not** inject `ILiveInput` and drives **no**
  monitoring gate today; the game reads no saved calibration.
- **`IGameSettings`** (rock-hero-game/core/.../settings/i_game_settings.h) is game-local (profile,
  first-run, `customScanRoots`); it holds no calibration/audio-device state and is the wrong home.
- **No `ILiveInput` test fake exists** (only the production `Engine` implements it);
  `ConfigurableAudioDeviceConfiguration` exists under
  rock-hero-common/audio/tests/include/.../testing/. The shared logger is
  rock-hero-common/core/include/rock_hero/common/core/shared/logger.h (quill-backed).
- Test targets: `rock_hero_common_audio_tests`, `rock_hero_editor_core_tests`,
  `rock_hero_game_core_tests`, run via `.agents/rockhero-build.ps1 -RunTouchedTests`.

## Dependencies

- **docs/roadmap/13-audio-device-settings-and-calibration.md — HARD dependency (Phase 3 only).**
  Phase 1 (`common::audio::IAudioConfigStore` port + `AudioConfigStore` impl + `AudioConfigError`,
  `Access { ReadWrite, ReadOnly }`, explicit-path ctor, in-memory fake) is a **shared store type each
  product instantiates over its own file** — exactly one writer per file, so there is **no
  `InterProcessLock`**. It must land before this plan's Phase 3. Phase 2 (editor migrates its
  **device-route** persistence off `IEditorSettings` onto its **own** `AudioConfigStore` instance, and
  keeps the calibration accessors as thin store-backed delegators) must land before this plan's Phase 3,
  where the editor becomes a thin driver over its own store. Plan 13 P1's store surface —
  `inputCalibrationFor(InputDeviceIdentity) → std::expected<std::optional<InputCalibrationState>, AudioConfigError>`,
  plus `saveInputCalibration` / `removeInputCalibration` and `activeDeviceRoute()` (used by Phase 4) —
  is **sufficient** for this plan's read, write, and device-restore paths with no new method. **The
  game path (Phase 4) does not depend on plan 13 P2**: the game composes its **own** `AudioConfigStore`
  and never reads the editor's file (see Phase 4).
- **Coordination note to avoid a double calibration-store migration (was critique F9).** Plan 13 P2
  and this plan both touch the two calibration-store call sites (`selectInputCalibrationForCurrentRoute`
  read, `saveActiveInputCalibration` write). To avoid migrating-then-moving the same code: **plan 13 P2
  retargets only the editor device-route persist/restore paths and keeps the `IEditorSettings`
  calibration methods as thin store-backed delegators; this plan's Phase 3 relocates the calibration
  read/write into `LiveInputMonitor` pointing directly at the injected `IAudioConfigStore&`, then
  deletes the now-unused `IEditorSettings` calibration methods P2 kept.** Record this split on plan 13
  P2 so the editor keeps *no* direct calibration store access after this plan.
- **docs/roadmap/21-game-audio-engine-and-session.md — code-complete; provides the game driver.**
  `GameplaySession`, the inject-from-`app/` composition, and the `ILiveInput` dry-tap contract already
  exist. This plan extends the constructor and wires the gate at its `Ready`/`close` edges. Phase 21
  established that the game pumps the JUCE message thread (else `loadLiveRig`'s message-thread assertion
  would already fail); Phase 4 re-verifies this at the wiring seam.
- **docs/roadmap/32-game-native-audio-config.md — downstream consumer (game native config).** The
  game's headless native setup drivers consume this plan's shared measurement/commit API
  (`beginMeasurement`/`commitCalibration`/`setManualCalibration`), writing the **game's own**
  `AudioConfigStore`. docs/roadmap/26-game-startup-menus-library.md Phase 8 renders the SDL device-setup
  and calibration wizard over plan 32's drivers and surfaces the typed `MonitoringDisabledReason`
  ("calibrate this device"). No shared code is duplicated for the game UI.

## Decisions already made

- **Placement is `rock-hero-common/audio`, feature folder `input/`.** By the Placement Procedure, the
  feature is live-guitar input monitoring/calibration — the existing `input/` feature (which already
  holds `i_live_input.h`, `input_calibration*.h`, `input_device_identity.h`). No new `monitoring/`
  folder: kind never decides placement, and a second folder would fracture one feature.
- **Service name: `LiveInputMonitor`** (namespace `rock_hero::common::audio`). Plain-noun facade
  matching `common/audio` precedent (`Engine`, `AudioDeviceSettings`); avoids the editor-reserved word
  "Controller" and the vague "Service". (Soft — see open question 1.)
- **The pure/impure split the editor already drew is preserved and relocated, not redrawn.**
  `LiveInputMonitor` is the impure adapter that owns `ILiveInput`; it holds a pure
  `InputCalibrationWorkflow` by value. `InputCalibrationRouteState` (backend concern, per its own doc
  comment) becomes a private nested `LiveInputMonitor::RouteState`. This honors "Separate State From
  Side Effects" — workflow produces plans; service performs them.
- **The workflow's `Context` KEEPS its identity field; the driver-facing context is two bools (was
  critiques A1/F3).** The pure methods consume `Context::current_input_device_identity` pervasively, so
  it cannot be dropped. Instead: the driver (editor/game) supplies only a `LiveInputMonitoringContext`
  of two bools (`session_audio_ready`, `arrangement_loaded`); the **service** reads
  `m_device_configuration.currentInputDeviceIdentity()` **once per driven operation** and populates the
  workflow's internal `Context` from it. This satisfies the "driver does not supply identity" intent
  *and* keeps the pure methods self-contained *and* preserves the single-sample-per-operation semantics
  the editor has today (`inputCalibrationContext()` samples once at handlers.cpp:184) — a service that
  re-read identity at each workflow call could otherwise observe a different route mid-commit.
- **A pure decision method is extracted onto the workflow.** `evaluateMonitoring(const Context&) →
  MonitoringDecision` computes every ordered gate branch as a plain value (reading the workflow's own
  `m_calibration_state`, exactly as `applyLiveInputGate` reads `activeCalibrationState()`), so all
  branches are unit-tested without ports. It emits every branch **except** `BackendUnavailable` and
  `CalibrationStoreUnavailable`, which are the service's post-I/O outcomes.
- **The service is a plain adapter — no listener list and no observer struct (was critiques A2/F6).**
  Because each product drives the gate from its own lifecycle handler (open question 3, recommend B),
  every `LiveInputMonitor` state change is caused by a driver-invoked method, so the driver can
  `updateView()` after the call returns. There is no single-consumer callback bundle to install and no
  second `IAudioDeviceConfiguration::Listener` on the same signal — the coding-conventions.md:88-99
  observer-struct rule and the listener-ordering hazard both fall away. (The editor's existing device
  listener is unchanged.)
- **The interior `updateView()` calls coalesce into the driver's trailing repaint (was critique F1).**
  The commit/measurement methods move to the service, which cannot call `updateView()`. Their interior
  repaints (handlers.cpp:40,72,339,356,376,382,405) become a single settled-state `updateView()` the
  editor handler issues after the service call returns. This is **benign**: (1) view derivation
  (`deriveViewState`) is pure and idempotent, so the final frame is identical; (2) these operations are
  synchronous on the message thread with no yield between the interior `updateView()` and the return,
  so per the `callAsync`-starves-paints behavior a WM_PAINT never actually renders mid-operation today —
  the observable frame is already the settled one. Phase 1 pins the load-bearing invariant (the exact
  `ILiveInput` call trace) and the settled view-state after each operation; it does **not** freeze paint
  *count* (see "Changes from draft" for why this diverges from the critic's exact-count suggestion).
- **The gate returns a value; only branchable operations return `std::expected`.** The gate
  (`refresh`/`applyGate`) is best-effort by contract (arm failures fold into a disable reason + log),
  so it returns a `LiveInputMonitoringStatus` value — never `std::expected`, never
  `std::optional<Error>`. Measurement/commit operations return
  `std::expected<void, LiveInputMonitorError>`.
- **Error domain: mint `LiveInputMonitorError` with three COARSE codes (was critique A3).** Per the
  coarse-boundary-error rule (coding-conventions.md:397-402), the public error exposes operation-level
  codes and preserves detail in `message`; the mirrored `InputRouteUnavailable` public code is dropped.
  The internal rollback distinction stays inside the impl, branching on the raw `LiveInputError.code`
  the `ILiveInput` setters return (as the editor does today at handlers.cpp:350). Public codes:
  `InvalidRequest` (precondition/route-changed cases the workflow surfaces), `BackendRejected` (any
  `ILiveInput` setter failure), `CalibrationStoreUnavailable` (`IAudioConfigStore` I/O). Verify at
  execution that the JUCE callers of the editor's calibration handlers consume only `.message` (they
  `reportError(message)` today); if none branch on `.code`, the editor handler return type changes from
  `LiveInputError` to `LiveInputMonitorError`.
- **The service reads the current route itself; a corrupt store is surfaced, never silently degraded.**
  An `AudioConfigError` from the store maps to `MonitoringDisabledReason::CalibrationStoreUnavailable`
  + a log line — never to a silent "no calibration → disable" (this preserves plan 13's three-state read
  contract). `IGameSettings` is not touched.
- **Notes to record on plan 13 (documentation only, no store-surface change):** (1) add "shared
  `LiveInputMonitor` (editor + game)" to plan 13 P1/P2's downstream-consumers list — each product
  instantiates its **own** `AudioConfigStore` and the monitor reads/writes that app's own store;
  (2) record the corrupt-store surfacing contract so P1's typed-error path is not later "simplified"
  to an empty optional; (3) confirm P2's in-memory `IAudioConfigStore` fake is full read **and** write
  and lives under `rock-hero-common/audio/tests/include/.../testing/` for reuse here; (4) record the
  Phase-2/Phase-3 calibration-migration split (above). There is **no shared file and no
  `InterProcessLock`**: each product is the sole writer of its own audio-config file. The editor's read
  of the **game's** store is a one-directional, read-only, **fresh one-shot at toggle-on** owned by
  **plan 48**, so no store-freshness-under-lock question arises here.

## Open questions for the user

Mirror each into docs/roadmap/00-roadmap.md "Decisions needed".

1. **Service name.** (A) `LiveInputMonitor`; (B) `InputMonitoringGate` (leans on the established "gate"
   vocabulary but the object also owns commit/measurement, which "gate" undersells);
   (C) `LiveInputMonitoringService`. **Recommendation: A** — concise plain-noun facade consistent with
   `common/audio` naming; the "gate" is one method (`applyGate`) on it.
2. **Moved workflow header visibility.** (A) publish `input/input_calibration_workflow.h` (the service
   holds `InputCalibrationWorkflow` by value, so the header must be complete at the service's public
   header — publishing matches sibling public value types like `input_calibration_state.h`); (B) hide
   the workflow behind a pimpl'd service so the header can stay `src/`-private. **Recommendation: A** —
   the by-value member makes a public header the truthful design; a pimpl adds an allocation and
   indirection for no boundary benefit, and every workflow type is already a `common::audio` value type.
3. **Device-change re-gate ownership (was critiques A5/F6).** (A) `LiveInputMonitor` self-registers as
   an `IAudioDeviceConfiguration::Listener` and re-gates on device change/unplug for **both** products
   from one place; (B) each product drives `refresh()` in its own handler — the editor keeps its single
   ordered `onAudioDeviceConfigurationChanged` (`persistAudioDeviceState()` + `refresh()` + `updateView()`),
   and the game gains autonomous unplug-disable later with plan 26's device layer. **Recommendation: B**
   — it is still fully shared (both products drive the *same* service; no gate logic is duplicated),
   it avoids stacking a second listener on the same signal (unspecified invocation order relative to the
   editor's existing listener) on top of Phase 3's already-significant move, and it keeps the service a
   plain adapter with no observer machinery. Record A as a clean fast-follow once B is proven; A would
   buy the game device-unplug-disables-monitoring for free before plan 26, at the cost of the
   listener-ordering care and an observer callback the editor would install for its repaint.

## Phased implementation

Phases 1-2 have no plan-13 dependency and land first (the safety net + the pure-workflow move).
Phase 3 is gated on plan 13 P1+P2 (the editor drives its **own** store); Phase 4 is gated on plan 13
P1 + this plan's Phase 3 **only** (the game composes and reads its **own** store). Phases 1-3 are
editor-and-common only and behavior-preserving; **Phase 4 wires the game's gate but leaves it silent
until a game-side calibration exists** (dev-seeded here; the product calibration arrives with plan 32).

### Phase 1 — Shared `FakeLiveInput` + editor characterization net (regression safety)

Scope: build the ONE `ILiveInput` fake that both the editor characterization and the later service
tests share (was critique F7), then pin today's observable editor behavior **before** any code moves.
No production code changes in this phase.

- Add `rock-hero-common/audio/tests/include/rock_hero/common/audio/testing/fake_live_input.h`:
  records the ordered `ILiveInput` setter call sequence and supports an injectable per-call
  `LiveInputError` (including `InputRouteUnavailable`). It is pure `common/audio` with no plan-13
  dependency, so it can land now and be reused verbatim by Phase 3's service tests — a single fake
  spans the characterization trace and the service tests, so the golden trace stays meaningful.
- Extend `rock-hero-editor/core/tests/test_editor_controller_input_calibration.cpp` using
  `FakeLiveInput` + `ConfigurableAudioDeviceConfiguration`. Assert, at the controller boundary: each of
  the six disable branches; arm-on-match (calibration-monitoring-off → gain → enable, in that order);
  `InputRouteUnavailable` rollback at each branch site; measurement start/cancel/dismiss; commit success
  plus both gain-failure and enable-failure rollbacks; device-change re-gates and pushes view-state;
  `m_transport.pause()` on prompt open.
- **Two durable, relocation-invariant assertions** (was critiques F1/F4):
  - a **golden `ILiveInput` trace** — capture the exact setter call sequence for the
    open → calibrate → commit → close arc and assert equality. This trace is store-agnostic and
    error-type-agnostic, so it survives P2 and Phase 3 unchanged.
  - a **settled view-state assertion** — after each operation, assert the editor's derived view-state
    (calibration status + disabled message + prompt) equals the expected settled value. This is what
    Phase 3 must preserve; paint *count* is explicitly not asserted (the interior repaints coalesce).

Files/modules: new common test fake header; rock-hero-editor/core/tests additions; game/common CMake
test source lists if the fake header changes a target.
Public-header impact: none (test-only).
Testing plan: the tests above are the deliverable; they must pass against the unmodified editor and the
new fake.
Exit criteria: the characterization suite + golden trace pass on HEAD with no production edits.
Verification:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Configure -Targets rock_hero_common_audio_tests rock_hero_editor_core_tests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

### Phase 2 — Move the pure workflow to common/audio + extract the editor view projection

Scope: relocate the pure state machine to `common/audio`, sever its view seam, and add the pure
`evaluateMonitoring` decision. The editor still owns all port-driving — no behavior change.

- Move `input_calibration_workflow.{h,cpp}` to
  `rock-hero-common/audio/include/rock_hero/common/audio/input/input_calibration_workflow.h` +
  `rock-hero-common/audio/src/input/input_calibration_workflow.cpp`, namespace
  `rock_hero::editor::core` → `rock_hero::common::audio`. Delete the two editor view-state `#include`s
  (workflow.h:13-14). **`Context` keeps `current_input_device_identity`** (was critiques A1/F3); all
  stored state, `Effect`/`Effects`, `MeasurementSession`, `CommitPlan`, `MeasurementRestore*`, and every
  pure method move unchanged.
- **Sever the editor-enum coupling.** The workflow's private `status()` currently returns the editor
  enum `InputCalibrationStatus` (workflow.h:295). Replace the workflow's projection surface
  (`Snapshot`, `snapshot()`, `status()`, `disabledMessage()`, `promptGainDb()`) with the pure
  common decision below; the editor mapping to `InputCalibrationStatus`/`InputCalibrationPrompt` and the
  English strings move into the editor projection module (next bullet).
- Add a small shared value-type header
  `rock-hero-common/audio/include/rock_hero/common/audio/input/live_input_monitoring_status.h` (was
  critique #8 — a dedicated home so plan 26's SDL menu includes one enum, not the whole workflow):
  ```cpp
  enum class LiveInputMonitoringState : std::uint8_t { Active, Disabled };

  enum class MonitoringDisabledReason : std::uint8_t
  {
      None, AudioDeviceSettingsOpen, SessionNotReady, NoInputDevice,
      MissingCalibration, CalibrationRouteMismatch, BackendUnavailable,
      CalibrationStoreUnavailable,
  };

  struct LiveInputMonitoringContext
  {
      bool session_audio_ready{false};
      bool arrangement_loaded{false};
  };

  struct LiveInputMonitoringStatus
  {
      LiveInputMonitoringState state{LiveInputMonitoringState::Disabled};
      MonitoringDisabledReason reason{MonitoringDisabledReason::SessionNotReady};
  };
  ```
- Add the pure decision method to the workflow (reads its own `m_calibration_state` + the context
  identity, mirroring `applyLiveInputGate`'s ordered branches at handlers.cpp:193-224 exactly):
  ```cpp
  struct MonitoringDecision
  {
      LiveInputMonitoringState target{LiveInputMonitoringState::Disabled};
      MonitoringDisabledReason reason{MonitoringDisabledReason::SessionNotReady};
      common::audio::Gain gain{};   // meaningful only when target == Active
  };

  [[nodiscard]] MonitoringDecision evaluateMonitoring(const Context& context) const;
  ```
  `BackendUnavailable`/`CalibrationStoreUnavailable` are never emitted here — they are the service's
  post-I/O outcomes.
- Extract the editor view seam into a **projection module** (was critique A4 — not a `*View` class;
  coding-conventions.md "Projection Modules" §243-249 and the `make…` grammar §49-63):
  `rock-hero-editor/core/src/input_calibration/input_calibration_text.h`(+`.cpp`), holding free
  functions — `inputCalibrationStatusFor(MonitoringDisabledReason, bool backend_available) →
  InputCalibrationStatus`, `inputCalibrationDisabledMessageFor(InputCalibrationStatus) → std::string`
  (the hardcoded editor English), and a `makeInputCalibrationViewState(...)` builder that assembles the
  editor slice (renamed from the workflow's `Snapshot` to an editor-owned `InputCalibrationViewState` in
  the editor view-state headers) from the service/workflow pure getters. `InputCalibrationStatus`/
  `InputCalibrationPrompt` stay in `editor_view_state.h`. The editor `Impl` still constructs and drives
  the workflow directly this phase, now via the moved type and the new projection module; its Phase 1
  settled view-state assertions pin the exact strings/flags this module must reproduce.
- Move the pure workflow unit tests to
  `rock-hero-common/audio/tests/test_input_calibration_workflow.cpp` (they include the moved header
  directly) and add an `evaluateMonitoring` branch matrix.

Files/modules: new common/audio workflow header/impl + status header + workflow test; new editor
`input_calibration_text.{h,cpp}` + `InputCalibrationViewState` type; edits to
`input_calibration_handlers.cpp` / `editor_controller_impl.h` include paths and the view build site;
CMake source-list changes in both libraries (a determinate graph change).
Public-header impact: two new public common/audio headers (`input_calibration_workflow.h`,
`live_input_monitoring_status.h`); no new editor public headers (the projection module is
`src/`-private per "Projection Modules stay private until an outside consumer exists").
Testing plan: moved pure workflow tests + `evaluateMonitoring` branch matrix; Phase 1's golden trace
and settled view-state assertions stay green **unchanged**; a small approval test pins each
`MonitoringDisabledReason` → `InputCalibrationStatus` + disabled-message in the new projection module.
Exit criteria: editor behaves identically; workflow + decision + projection tests green; Phase 1 net
green.
Verification (graph changed → configure):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Configure -Targets rock_hero_common_audio_tests rock_hero_editor_core_tests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

### Phase 3 — Introduce `LiveInputMonitor`; editor becomes a thin driver (assumes plan 13 P1+P2)

Scope: create the service, relocate the port-driving layer off `EditorController::Impl` into it, and
reduce the editor to a driver. Behavior-preserving up to the coalesced-repaint decision.

- New error header
  `rock-hero-common/audio/include/rock_hero/common/audio/input/live_input_monitor_error.h`
  (impl in `src/input/live_input_monitor_error.cpp`), mirroring `live_input_error.h`, with **coarse**
  codes (was critique A3):
  ```cpp
  enum class LiveInputMonitorErrorCode : std::uint8_t
  {
      InvalidRequest,               // operation not valid in the current route/session state
      BackendRejected,              // an ILiveInput setter failed
      CalibrationStoreUnavailable,  // an IAudioConfigStore read or write failed
  };

  struct [[nodiscard]] LiveInputMonitorError
  {
      LiveInputMonitorErrorCode code{};
      std::string message;

      explicit LiveInputMonitorError(LiveInputMonitorErrorCode error_code);
      LiveInputMonitorError(LiveInputMonitorErrorCode error_code, std::string message_text);
  };
  ```
- New service `rock-hero-common/audio/include/rock_hero/common/audio/input/live_input_monitor.h`
  (impl `src/input/live_input_monitor.cpp`) — a plain adapter, no listener/observer. The store is held
  as a single **swappable** `IAudioConfigStore&` dependency (was plan-14 D3): each composition root
  injects the app's **own** store, and plan 48 can substitute the editor's effective-source facade with
  **no monitor edit** — do not hard-wire the editor's raw store as a non-swappable member:
  ```cpp
  class LiveInputMonitor final
  {
  public:
      LiveInputMonitor(
          ILiveInput& live_input, IAudioDeviceConfiguration& device_configuration,
          IAudioConfigStore& audio_config_store);

      // Gate + teardown (both products drive these).
      [[nodiscard]] LiveInputMonitoringStatus refresh(LiveInputMonitoringContext context);
      [[nodiscard]] LiveInputMonitoringStatus applyGate(LiveInputMonitoringContext context);
      void disableMonitoring();
      [[nodiscard]] LiveInputMonitoringStatus status() const noexcept;

      // Measurement / commit (editor drives now; plan 26 SDL wizard later).
      [[nodiscard]] std::expected<void, LiveInputMonitorError> beginMeasurement(
          LiveInputMonitoringContext context);
      [[nodiscard]] std::expected<void, LiveInputMonitorError> cancelMeasurement();
      [[nodiscard]] std::expected<void, LiveInputMonitorError> commitCalibration(
          double gain_db, const std::optional<InputDeviceIdentity>& expected_identity);
      [[nodiscard]] std::expected<void, LiveInputMonitorError> setManualCalibration(double gain_db);

      // Prompt + editor audio-device-settings modal (editor; game leaves untouched).
      [[nodiscard]] bool requestPrompt(LiveInputMonitoringContext context);
      void closePrompt() noexcept;
      void openAudioDeviceSettings();
      [[nodiscard]] LiveInputMonitoringStatus closeAudioDeviceSettings(
          LiveInputMonitoringContext context);

      // Pure read surface for the editor's view projection.
      [[nodiscard]] std::optional<InputCalibrationState> activeCalibrationState() const;
      [[nodiscard]] std::optional<InputDeviceIdentity> currentInputDeviceIdentity() const;
      [[nodiscard]] bool calibrationMatchesCurrentRoute() const;
      [[nodiscard]] bool promptVisible() const noexcept;
      [[nodiscard]] bool audioDeviceSettingsOpen() const noexcept;
      [[nodiscard]] bool backendAvailable() const noexcept;

  private:
      struct RouteState
      {
          Gain input_gain;
          bool live_input_monitoring_enabled{false};
          bool calibration_input_monitoring_enabled{false};
      };

      // Samples currentInputDeviceIdentity() ONCE and builds the workflow context for one operation.
      [[nodiscard]] InputCalibrationWorkflow::Context workflowContext(
          LiveInputMonitoringContext context) const;
      [[nodiscard]] LiveInputMonitoringStatus applyGateInternal(
          const InputCalibrationWorkflow::Context& context);
      [[nodiscard]] std::expected<void, LiveInputMonitorError> reselectCalibration(
          const InputCalibrationWorkflow::Context& context);
      [[nodiscard]] RouteState currentRouteState() const;
      void restoreRouteStateBestEffort(const RouteState& route_state);
      bool setLiveInputMonitoringBestEffort(bool enabled, std::string_view reason);
      bool setCalibrationInputMonitoringBestEffort(bool enabled, std::string_view reason);
      bool setInputGainBestEffort(Gain gain, std::string_view reason);

      InputCalibrationWorkflow m_workflow;
      ILiveInput& m_live_input;
      IAudioDeviceConfiguration& m_device_configuration;
      IAudioConfigStore& m_audio_config_store;   // swappable: app's own store, or plan 48's facade
      LiveInputMonitoringStatus m_status{};
  };
  ```
  `refresh` builds the workflow context via `workflowContext(context)` (single identity sample),
  `reselectCalibration` (reads `m_audio_config_store.inputCalibrationFor(identity)` from the injected
  `IAudioConfigStore&`; on `AudioConfigError` → set `m_status = {Disabled, CalibrationStoreUnavailable}`,
  log, do NOT arm, return; on success feed the optional to `syncCommittedInputDeviceIdentity`), then
  `applyGateInternal`.
  `applyGate` skips the reselect. `applyGateInternal` folds in the ordered gate: tear down calibration
  monitoring; `evaluateMonitoring`; on `Active`, `setInputGain` then `setLiveInputMonitoringEnabled`,
  folding any `LiveInputError` into `{Disabled, BackendUnavailable}` + `markBackendUnavailable`; cache
  `m_status`. Move `beginMeasurement`/`commitCalibration`/`cancelMeasurement`/route-snapshot bodies from
  the editor into the private layer with **two mechanical substitutions the golden trace pins**:
  (1) each interior `updateView()` is dropped (the editor driver repaints once after the call returns);
  (2) `m_settings` (`IEditorSettings`) → `m_audio_config_store` (`IAudioConfigStore`,
  `EditorSettingsError` → `AudioConfigError` → translated to `LiveInputMonitorError` at the return
  site), and the `logEditorControllerBestEffortFailure` shim → the common logger (`RH_LOG_WARNING`,
  category `"audio.live_input_monitor"`). The internal `InputRouteUnavailable`-vs-other rollback branch
  stays, branching on the raw `LiveInputError.code` in-impl; the public error is coarse.
- Editor `Impl`: replace `m_input_calibration` (workflow) and the `InputCalibrationRouteState` struct
  with `LiveInputMonitor& m_live_input_monitor` (injected via `EditorController::Services`, composed in
  rock-hero-editor/app/main.cpp over the Engine's `ILiveInput`/`IAudioDeviceConfiguration` + the
  editor's **own** `AudioConfigStore`, passed as the swappable `IAudioConfigStore&`). Delete the moved
  methods from `input_calibration_handlers.cpp`. This phase relocates the calibration read/write call
  sites (`input_calibration_handlers.cpp:127,151`) into `LiveInputMonitor` and **deletes the
  `IEditorSettings` calibration methods that plan 13 P2 kept as delegators**, so the editor retains no
  direct calibration-store access. Rewrite
  `inputCalibrationContext()` → `monitoringContext()` (two bools). The 10 gate sites become
  `m_live_input_monitor.refresh(monitoringContext())` (route-change edges) /
  `applyGate(monitoringContext())` (lifecycle edges) / `disableMonitoring()` (close/fail edges), keeping
  the same trigger structure. Per open question 3 (recommend B): the editor's
  `onAudioDeviceConfigurationChanged` keeps its single ordered body —
  `persistAudioDeviceState(); refresh(monitoringContext()); updateView();`. Calibration handlers delegate
  to the service, then issue **one** `updateView()` after the call returns (coalescing the old interior
  repaints), and keep `m_transport.pause()`/`reportError(error.message)`. `deriveViewState()` builds the
  calibration slice via `makeInputCalibrationViewState(m_live_input_monitor, monitoringContext())`.
- New `rock-hero-common/audio/tests/test_live_input_monitor.cpp` over Phase 1's `FakeLiveInput`,
  `ConfigurableAudioDeviceConfiguration`, and plan 13 P2's in-memory `IAudioConfigStore`.
- **Budgeted test retargets at this boundary (was critique F4):** update Phase 1's editor tests where
  they (a) seeded calibration through an `IEditorSettings` fake — retarget to the `IAudioConfigStore`
  fake now that **this phase** removes those `IEditorSettings` methods — and (b) asserted the editor handler error type
  `LiveInputError` — retarget to `LiveInputMonitorError` if the handler return type changed. The golden
  `ILiveInput` trace and the settled view-state assertions are unchanged.

Files/modules: three new common/audio input files + two impls + service test; editor
`editor_controller_impl.h`, `input_calibration_handlers.cpp`, `editor_controller.cpp`,
`project_handlers.cpp`, `editor_controller` services wiring, rock-hero-editor/app/main.cpp; CMake source
lists. This phase removes the `IEditorSettings` calibration methods that plan 13 P2 kept as delegators.
Public-header impact: two new public common/audio headers (service + error).
Testing plan: service tests — the `applyGate`/`refresh` decision matrix (each `MonitoringDisabledReason`,
plus `Active` arming order); `InputRouteUnavailable` rollback via injected `LiveInputError`; gain-vs-enable
failure rollbacks; `refresh` surfaces `CalibrationStoreUnavailable` on a corrupt store and does not arm;
`disableMonitoring` tears down both paths; identity sampled once per operation (route change injected
mid-operation does not change the commit's expected-identity check). Editor: Phase 1's golden trace +
settled view-state assertions stay green; projection approval test unchanged.
Exit criteria: editor runs entirely on the shared service with identical observable settled behavior;
service + editor suites green; game unchanged and silent.
Verification:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Configure -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

### Phase 4 — Game wiring over the game's own store (WIRED-BUT-SILENT; assumes plan 13 P1 + this plan's Phase 3)

Scope: drive the shared service from `GameplaySession` **and** restore the game's **own** saved device
route so the game lands on the route its own native config selected. The game composes and reads its
**own** `AudioConfigStore` — it never reads the editor's file. The gate is **wired but silent** until
the game's own store holds a device with a matching calibration; that calibration is **dev-seeded**
here (a scripted store write, exactly the existing test posture), and the **product** calibration
surface arrives with plan 32. This phase depends on **plan 13 P1 + this plan's Phase 3 only** (was
critique F2: the game restores its own route, so there is no dependency on the editor's plan 13 P2
migration).

- **Device-route restore (the game's own route).** Compose one `common::audio::AudioConfigStore{
  gameAudioConfigApplicationName(), Access::ReadWrite }` (plan 13 P1) in rock-hero-game/app/main.cpp,
  and at game Engine startup restore the game's own `activeDeviceRoute().serialized_state` before the
  session starts (message thread), falling back to the JUCE `initialise(1,2)` default when absent
  (today the game always uses that default with no restore). With the game's own route active, the
  calibrate-first gate finds a matching calibration **once the game store holds one**; until then it
  disables silently — never uncalibrated live audio.
- `GameplaySession` gains a 7th constructor dependency
  `common::audio::LiveInputMonitor& live_input_monitor` (stored as `m_live_input_monitor`). It stays a
  thin driver holding one cohesive collaborator, not raw ports.
- **Enable edge (message-thread verified, was critique F5):** in `onRigLoadCompleted`, immediately after
  `m_stage = GameplaySessionStage::Ready` (gameplay_session.cpp:433):
  ```cpp
  const auto status = m_live_input_monitor.refresh(
      common::audio::LiveInputMonitoringContext{
          .session_audio_ready = true, .arrangement_loaded = true});
  // status.reason retained for a future SDL "monitoring off because X" surface (plan 26); non-fatal.
  ```
  This is the exact analogue of the editor's `project_audio_ready = true; select…; applyLiveInputGate()`.
  `onRigLoadCompleted` runs on the JUCE message thread (`loadLiveRig` refuses off-thread at
  engine_live_rig.cpp:884-888; continuations `callAsync` back at :1382-1396; `on_result` at :1236), so
  driving the message-thread-only `ILiveInput` (i_live_input.h:19) from here is contract-correct — **no
  worker-thread marshalling is required**. Add a test asserting `refresh` is invoked on the message
  thread, and re-verify at execution that the game process pumps the JUCE message loop that plan 21's
  `loadLiveRig` call already depends on.
- **Disable edge:** in `close()` (gameplay_session.cpp:344-375), call
  `m_live_input_monitor.disableMonitoring()` alongside the existing teardown.
- **No re-arm on replay:** `restart()`/`play()`-from-`Finished` do no rig work; monitoring stays armed
  across pause/finish/restart.
- Teardown in rock-hero-game/app/main.cpp stays reverse of construction.
- Calibrate-first parity holds by construction: the game drives only `refresh`/`disableMonitoring`,
  never `beginMeasurement`/`commitCalibration`. `NoInputDevice`/`MissingCalibration`/`CalibrationRouteMismatch`
  all disable monitoring, so an uncalibrated or mismatched device yields **silent** monitoring — never
  uncalibrated live audio.

Files/modules: rock-hero-game/core/{include,src}/.../session/gameplay_session.{h,cpp},
rock-hero-game/app/main.cpp, rock-hero-game/core/tests/test_gameplay_session.cpp, game CMake if the test
link changes. Composes plan 13 P1's `AudioConfigStore` + `activeDeviceRoute()` accessor +
`gameAudioConfigApplicationName()`; no new common code.
Public-header impact: game/core session header ctor change only; nothing added to common.
Testing plan (fakes only, no audio hardware): with the game's **own** in-memory `IAudioConfigStore`
seeded with a calibration matching the fake device identity, reaching `Ready` arms `FakeLiveInput` at
the calibrated gain on the message thread; no/mismatched/absent-device calibration each leave monitoring
disabled with the expected `MonitoringDisabledReason` (the **wired-but-silent** default until a
calibration is seeded); a corrupt store logs and is non-fatal; `close()` disables; pause/finish/restart
leave monitoring armed; the app composition restores a stored active device route when present and falls
back to defaults when absent. Construct the real `LiveInputMonitor` over the three fakes (the game
injects the concrete service exactly as it injects the concrete `Engine`).
Exit criteria: game/core tests green; the game drives the shared gate over its **own** store and, with a
**dev-seeded** matching calibration for the restored input route, the player's live guitar routes through
the arrangement's tone rig; without a seeded calibration the gate stays **wired but silent**. The product
calibration surface arrives with plan 32.
Verification:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Configure -Targets all
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

Acceptance: one shared `common::audio::LiveInputMonitor` owns the calibrate-first monitoring gate and
the calibration measurement/commit orchestration; the editor drives it as a thin driver over its **own**
`AudioConfigStore` with identical observable settled behavior (Phase 1 golden `ILiveInput` trace +
settled view-state green through Phases 2-4); the game composes and drives the same service over its
**own** store at `Ready`/`close` on the message thread, wired but silent until a game-side calibration
exists (dev-seeded at Phase 4; the product calibration surface arrives with plan 32); no `common` code
depends on `editor`/`game`; no `std::optional<Error>` is an operation result; the public
`LiveInputMonitorError` is coarse; the typed `MonitoringDisabledReason` is exposed for plan 32's drivers
and plan 26's SDL menu; `IGameSettings` carries no calibration. Each product is the sole writer of its
own audio-config file, so there is no shared file and no cross-process lock — the editor's read of the
game's store (plan 48) is a one-directional, read-only, **fresh one-shot at toggle-on**, not a live
watch. Update docs/roadmap/00-roadmap.md's status line and mirror the open questions into its "Decisions
needed".

## Rollback/abort notes

- **Phase 1** is tests + one common test fake; it never blocks and is the safety net for everything
  after it. If it cannot reproduce a current behavior, that is a finding to resolve **before** moving
  code.
- **Phase 2** is a relocation plus the view-seam extraction; reverting moves the workflow back to
  editor/core and restores the two view-state includes, with the Phase 1 net proving no settled-behavior
  change. The `evaluateMonitoring` addition and the projection module are independently revertible.
- **Phase 3 is the risky one** (port-driving move + store retarget + the interior-repaint coalescing).
  Mitigations, in order: the golden `ILiveInput` trace + settled view-state assertions must stay green;
  the gate, rollback, and typed-commit bodies move with only the two pinned substitutions (drop interior
  `updateView()`; `m_settings` → `m_audio_config_store`), no branch rewritten and the
  `InputRouteUnavailable` internal distinction preserved; the store retarget is mechanical and gated on
  plan 13 P2. Open question 3 is already recommended at B (no second device listener), so there is no
  listener-ordering fallback to invoke. Reverting Phase 3 restores the `Impl` methods; the moved pure
  workflow (Phase 2) can remain.
- **Phase 4** is additive game wiring plus the game's own device-route restore; reverting the wiring
  leaves the game compiling with monitoring never armed (pre-plan state), and reverting the route restore
  falls back to the JUCE default device with no gate effect. No persisted format changes. Keep the
  route-restore step in its own commit so it can be reverted independently of the session wiring.
- **Plan-13 coupling:** do not start Phase 3 until plan 13 P1+P2 land; Phase 4 needs only plan 13 P1 +
  this plan's Phase 3 (the game reads its **own** store). If plan 13 slips, Phases 1-2 (fake +
  characterization + pure move + view-seam extraction) are still safe to land, since they do not consume
  `IAudioConfigStore`.

## Changes from draft (critique resolution)

- **Kept `Context::current_input_device_identity`; added a separate two-bool driver context (critiques
  A1/F3).** The draft's "identity is not a Context field" would have broken every pure workflow method
  (verified reading it at workflow.cpp:112/138/181/228/308) and could re-sample the route mid-operation.
  Resolution: the driver supplies `LiveInputMonitoringContext` (two bools); the service samples identity
  **once per operation** via `workflowContext(...)` and fills the workflow's unchanged internal `Context`.
  This preserves both the "service reads identity" decision and the single-sample semantics.
- **Removed the `Listener` interface / subscriber list; the service is a plain adapter (critiques
  A2/F6).** Choosing open-question-3 option B means every state change is driver-initiated, so no
  observer is needed at all — this satisfies the single-consumer observer-struct rule by eliminating the
  mechanism, and removes the two-listeners-on-one-signal ordering hazard.
- **Made `LiveInputMonitorError` coarse (3 codes) and dropped the mirrored `InputRouteUnavailable`
  (critique A3).** The internal rollback distinction stays in-impl on the raw `LiveInputError.code`;
  the public boundary exposes `InvalidRequest`/`BackendRejected`/`CalibrationStoreUnavailable` with
  detail in `message`, per coding-conventions.md:397-402.
- **Renamed the extracted view seam from a `*View` class to a projection module (critique A4).**
  `input_calibration_text.h` free functions + a `makeInputCalibrationViewState` builder, per
  "Projection Modules" and the `make…` grammar; no headless `*View` class.
- **Specified the device-change thread contract (critiques A5/F5).** Verified both re-gate entry points
  are message-thread (device change = JUCE `ChangeListener` at engine.cpp:67/109; rig-load completion
  marshalled via `callAsync` at engine_live_rig.cpp:884-888/1382-1396/1236), so no worker-thread
  marshalling is needed. Downgraded the "data race" framing to a stated, cited fact plus a verification
  test.
- **Fixed the by-value optional and the ODR redefinition (critiques #6/#7).** `commitCalibration` takes
  `const std::optional<InputDeviceIdentity>&`; `LiveInputMonitoringContext` is defined once in the new
  status header and included.
- **Handled the interior `updateView()` calls explicitly (critique F1).** The commit/measurement move is
  no longer called "verbatim"; interior repaints coalesce into the driver's trailing `updateView()`. I
  **diverge from the critic's exact-paint-count freeze**: pinning paint count fights the clean
  driver-repaints-once design, and the coalescing is provably benign (idempotent view derivation; the
  synchronous message-thread ops render no intermediate frame today). Phase 1 instead pins the
  `ILiveInput` trace and the settled view-state — the invariants that actually govern correctness.
- **Folded device-state restore into Phase 4 as required (critique F2).** The audible milestone no
  longer depends on an "optional" phase; the old Phase 5 is merged into Phase 4, with the calibrate-first
  caveat stated.
- **Scoped "stays green identically" honestly (critique F4).** The durable invariant is the store- and
  error-type-agnostic `ILiveInput` trace + settled view-state; mechanical test retargets at the P2 and
  Phase-3 boundaries (fake store swap, handler error-type change) are explicitly budgeted.
- **One shared `FakeLiveInput`, built first (critique F7).** It lands in Phase 1 under
  `common/audio/tests/include/.../testing/` and is reused by both the editor characterization and the
  service tests, so the golden trace and the service tests inject failures identically.
- **Cross-process parity is moot under per-app stores (critique F8, revised by the per-app
  re-architecture).** Each product is the sole writer of its own audio-config file, so there is no shared
  file, no `InterProcessLock`, and no cross-process refresh question. The editor's read of the **game's**
  store is one-directional, read-only, and a fresh one-shot at toggle-on, owned by plan 48.
- **Avoided the double calibration-store migration (critique F9).** Added a Dependencies coordination
  note: plan 13 P2 retargets device-route only and keeps the `IEditorSettings` calibration methods as
  store-backed delegators; this plan's Phase 3 relocates calibration store access into `LiveInputMonitor`
  and deletes those delegators.
- Corrected the inventory: `calibrationMatches` is a workflow **method** (not a missing free symbol),
  and `applyLiveInputGate()` itself issues **no** `updateView()` (only the commit/measurement methods do).

## Unresolved questions for the user

1. **Open question 3 recommendation flipped to B.** The draft recommended A (autonomous shared device
   listener); this plan recommends B (each product drives `refresh()` in its own handler) to keep the
   service a plain adapter and avoid the listener-ordering hazard, deferring A's free game-unplug-disable
   to a plan-26 fast-follow. If you prefer the maximal "one code path owns the device signal too" shape
   now, choose A and accept the editor observer-callback + listener-ordering care it reintroduces.
2. **Editor calibration-handler return type.** Phase 3 assumes the JUCE callers of
   `IEditorController::onInputCalibration*` consume only `error.message`. If any caller branches on the
   `LiveInputError.code`, the handler must translate `LiveInputMonitorError` back rather than change its
   signature — confirm at execution (a one-file grep of the UI callers).