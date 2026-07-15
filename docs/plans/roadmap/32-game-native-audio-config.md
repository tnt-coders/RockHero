# Plan 32 — Game Native Audio Configuration (headless)

Status: Ready | 2026-07-12 | baseline `refactor @ 75cc26dd`

## Goal

Give the game its own native, headless audio-configuration path so a player can plug in a guitar,
pick a device, and hear their live signal through the authored tone — configured **per-app** and
**calibrate-first**, with no dependency on the editor. A game/core headless model
(`GameAudioConfig` / `PlayerInputConfig`) records which physical routes feed which player slots
(v1: exactly one slot, persisted), while device-state, gain calibration, and latency offsets live
in the game's own instance of the shared `common::audio::AudioConfigStore` (plan 13). Above that,
a game/core headless state machine sequences device selection → gain calibration → (later) latency
capture, driving the already-shared `AudioDeviceSettings` workflow and the plan-14 `LiveInputMonitor`
gate. The state machine is pure and the audio wiring is an adapter, per
docs/design/architectural-principles.md "Separate State From Side Effects" and "Keep Threading at
the Boundary", so the whole path is fake-tested with no audio hardware in CI. Reaching **P2** is the
first product milestone at which the game guitar is audible through the tone.

## Non-goals

- The SDL3/bgfx device-picker and calibration-wizard **UI** — that is
  docs/plans/roadmap/26-game-startup-menus-library.md Phase 8, an SDL-presentation-only layer over the
  headless drivers this plan lands. This plan ships no rendered menus.
- The shared per-app store type, `ActiveDeviceRoute`, `Access::ReadOnly`, and the two audio-config
  identity constants — those are docs/plans/roadmap/13-audio-device-settings-and-calibration.md Phase 1;
  this plan **consumes** them.
- The `LiveInputMonitor` gate itself and its gate logic — that is
  docs/plans/roadmap/14-live-input-monitoring.md (Phase 3 creates the monitor; Phase 4 wires the game's
  own store at Ready); this plan drives the shipped gate, never re-implements it.
- The latency-offset **model** and the play-on-cue **estimator** — those are plan 13 Phase 3 and
  Phase 4; this plan's P3 only *drives* that estimator and persists its result.
- The editor's effective-source facade, "use game settings" toggle, and consolidated Audio Setup
  modal — those are docs/plans/roadmap/48-editor-audio-setup.md; this plan is game-side only.
- Note detection / onset algorithms beyond what plan 13 Phase 4 already owns for calibration
  capture (docs/plans/roadmap/22-note-detection.md owns detection).
- Scoring hit-window widths and the provisional-hit machine
  (docs/plans/roadmap/24-scoring-star-power-failure.md consumes the offset contract).

## Constraints

Applicable subset of the roadmap's non-negotiable constraints (see docs/plans/roadmap/00-roadmap.md):

- (a) **Layering**: `common` never depends on editor or game code; anything both products need is
  extracted to `rock-hero-common` first. `GameAudioConfig` / `PlayerInputConfig` are game-specific
  (the editor has no concept of player slots) and therefore live in `game/core`, not common, per
  the Placement Procedure in docs/design/architectural-principles.md. Tracktion headers stay
  isolated to `rock-hero-common/audio` implementation files.
- (b) **Public-header minimalism**: only headers that must be public are public; ports-and-adapters
  per docs/design/architectural-principles.md ("Ports and Adapters", "Typed Boundary Errors").
- (c) **Naming firewall**: the commercial real-guitar game that inspired this project is never
  named; use RS/neutral phrasing.
- (d) **Typed errors**: fallible boundaries return `std::expected<…, …Error>`, never
  `std::optional<Error>` (docs/design/coding-conventions.md).
- (h) **Builds**: all build/test/lint commands go through `.agents/rockhero-build.ps1` (usage in
  `.agents/README.md`), never raw cmake/ctest/ninja. Intermediate phases run only the checks their
  changes warrant; the final acceptance phase runs the sanctioned bundle as separate invocations.
- (i) **Real guitar input**: calibration flows assume both hands are on the guitar; the gain-capture
  gesture is a strum, never a mid-measurement keypress.

Design-doc anchors: docs/design/architecture.md "Gameplay Systems" (native onboarding starts at
device setup; calibration built in from day one), "Threading Model" (graph-rebuilding mutations are
message-thread only), and docs/design/architectural-principles.md "Separate State From Side Effects",
"Keep Threading at the Boundary", "Placement Procedure for New Files".

## Current state inventory

Verified paths and behavior on the baseline tree:

- **The game is a skeleton with a hard-coded JUCE default device.** `rock-hero-game/app/main.cpp`
  is a small JUCE `DocumentWindow` shell; nothing game-side consumes audio settings, and the audio
  device is opened via a bare JUCE default (`initialise(1, 2)` posture with no restore path).
  Game `core/`, `audio/`, and `ui/` are placeholder static libraries.
- **No game audio-config model exists.** There is no `GameAudioConfig`, `PlayerInputConfig`, or
  `game/core` `audio/` feature folder yet; the multi-input model is entirely new code here.
- **The shared staged device workflow already shipped.** `IAudioDeviceSettings` /
  `AudioDeviceSettings`
  (rock-hero-common/audio/include/rock_hero/common/audio/device/audio_device_settings.h) provide
  the staged audio-system/device/channel/sample-rate/buffer-size edit with apply/cancel/control-panel
  and typed `AudioDeviceSettingsError`; the ASIO > WASAPI backend ranking is implemented and tested
  in rock-hero-common/audio/src/device/audio_device_settings.cpp (backend-preference block).
- **The device-configuration port exposes restore/capture and the resolved identity.**
  `IAudioDeviceConfiguration`
  (rock-hero-common/audio/include/rock_hero/common/audio/device/i_audio_device_configuration.h)
  exposes `restoreSerializedDeviceState()`, `serializedDeviceState()`, `currentDeviceStatus()`, and
  `currentInputDeviceIdentity()` — the pair the P2 driver captures together into the game store as
  one `ActiveDeviceRoute`. `Engine` implements this port.
- **Route identity and gain calibration value types exist and are reused unchanged.**
  `InputDeviceIdentity` (input/input_device_identity.h) keys a physical route by backend + input
  device + channel index (channel name is metadata); `InputCalibrationState`
  (input/input_calibration_state.h) is a *gain* record (`Gain` + identity, clamped via
  shared/gain.h). Both are consumed by this plan; neither is modified.
- **Per-app app-data folder naming lives in common.**
  rock-hero-common/core/include/rock_hero/common/core/shared/application_identity.h provides
  `applicationDataFolderName()` ("Rock Hero"); the audio-config `applicationName` constants
  (`gameAudioConfigApplicationName()` = "Rock Hero Game Audio") land beside the store in
  `common/audio/settings/` at plan 13 Phase 1.
- **The shared audio-config store and `LiveInputMonitor` are prerequisites, not yet present.**
  `common::audio::AudioConfigStore` / `IAudioConfigStore` / `ActiveDeviceRoute` /
  `Access::ReadOnly` arrive with plan 13 Phase 1; the `LiveInputMonitor` gate
  (`beginMeasurement` / `commitCalibration`, `rawInputMeterLevel()`) arrives with plan 14 Phase 3,
  and its game wiring at Ready with plan 14 Phase 4. This plan does not begin until those land
  (see Dependencies).
- **Game settings persistence.** The game's profile/workflow persistence home
  (`IGameSettings` or a sibling serializer under `game/core`) is where `GameAudioConfig` persists;
  the exact home is a P1 open question (below). It is **not** the shared `IAudioConfigStore`, which
  the editor reads read-only for its toggle.
- Test targets: `rock_hero_common_audio_tests`, `rock_hero_editor_core_tests`, and the game/core
  test target, run via `.agents/rockhero-build.ps1 -RunTouchedTests`.

Verified against code on 2026-07-12, refactor @ 75cc26dd.

## Dependencies

Upstream (must land first):

- **plan 13 Phase 1** — the shared per-app `AudioConfigStore` type, `ActiveDeviceRoute`
  (opaque blob **+** resolved `InputDeviceIdentity`), `Access::ReadOnly`, and the
  `gameAudioConfigApplicationName()` constant. P1, P2, and P4 of this plan all consume it.
- **plan 14 Phase 3** — `LiveInputMonitor` created with its gate logic; the game's gain-calibration
  driver (P2) calls `beginMeasurement` / `commitCalibration` and meters via
  `ILiveInput::rawInputMeterLevel()`.
- **plan 14 Phase 4** — the game composes its **own** `AudioConfigStore{ gameAudioConfigApplicationName(),
  ReadWrite }` in `rock-hero-game/app/main.cpp`, restores its own `activeDeviceRoute()`, and wires
  the gate at Ready (wired-but-silent until a game-side calibration exists). P2 promotes that
  dev-seeded calibration to a real product capture; P4 formalizes the startup restore.
- **plan 13 Phase 4** (P3 only) — the play-on-cue latency estimator this plan's P3 drives.

Downstream consumers (recorded in both directions in docs/plans/roadmap/00-roadmap.md):

- **docs/plans/roadmap/26-game-startup-menus-library.md Phase 8** — the SDL device-picker and calibration
  wizard render over this plan's headless drivers (also gated on G20-RENDER and plan 22's tuner);
  this plan keeps the drivers **ungated** by G20-RENDER so the audible milestone is not delayed
  behind SDL.
- **docs/plans/roadmap/48-editor-audio-setup.md** — reads the game's audio-config file read-only through
  the shared store's `activeDeviceRoute().identity`; this plan mirrors the primary player's route
  into that field (P1).
- **docs/plans/roadmap/24-scoring-star-power-failure.md** — consumes the effective-offset contract that
  P3's persisted `LatencyOffsets` feed.

## Decisions already made

- **Per-app, calibrate-first, no editor import.** The game calibrates natively and never imports
  from the editor. Device-state, gain calibration, and latency offsets persist to the game's **own**
  `AudioConfigStore` instance over its own file (`Rock Hero Game Audio.settings`); the editor is a
  read-only consumer, never a source (that direction is plan 48).
- **Multi-input-aware model in game/core, not common.** `GameAudioConfig` /
  `PlayerInputConfig` live in a `game/core` `audio/` feature folder because the editor has no
  concept of player slots — putting slots in the shared schema would give the editor that concept,
  failing the "both products need it" test of the Placement Procedure. The one cross-app feature
  (the editor mirroring the game) needs only the shared store's single primary/active route
  (`ActiveDeviceRoute.identity`), a route-level fact both apps understand, **not** the player→route
  mapping — so the split holds without stranding that feature.
- **v1 persists a real single-entry list, not a hollow symmetry type.** `GameAudioConfig` holds
  `std::vector<PlayerInputConfig>` with exactly one entry (slot 0) at v1, **persisted** through
  game/core and read back at startup. The multi-input schema is therefore genuinely exercised by a
  real consumer, N-input is purely additive, and there is no speculative dead code.
- **Multi-input scope is bounded honestly.** Because the shared store holds one `ActiveDeviceRoute`
  (one JUCE `AudioDeviceManager` = one device) and the shared selection surface exposes a single
  mono channel selection, v1's N-capability is *"N players as N channels on the one active device"*
  — additive with no rework (append `players` entries, more channels on the same device). N players
  on **separate physical interfaces** is genuine future work requiring a multi-device
  `ActiveDeviceRoute` representation and a multi-channel selection surface; this plan says so rather
  than claiming "no rework."
- **Calibration and offsets stay route-keyed in the shared store.** `GameAudioConfig` records only
  *which routes are player slots*; the gain calibration (`InputCalibrationState`) and latency
  offsets (`LatencyOffsets`) remain keyed by `InputDeviceIdentity` / `AudioRouteIdentity` in the
  shared store, reused unchanged. Latency is a per-physical-route property and never lives on
  `PlayerInputConfig`.
- **Latency is off the audible path.** A calibrated *gain* route is fully audible with no latency
  compensation (plan 14's own non-goal). Gain calibration (P2) reaches "audible"; latency capture
  (P3) is a later, independent phase depending only on plan 13 Phase 4.
- **Separate state from side effects.** The native-setup driver is a pure state machine; device
  open/restore, metering, and store writes are adapter side effects on the message thread, with
  sample capture on the audio side via the existing lock-free tap.

## Open questions for the user

1. **Game audio-config persistence home.** P1 persists `GameAudioConfig` through `game/core` —
   either by **extending `IGameSettings`** or via a **dedicated game audio-config serializer**
   sibling to it. Both keep it out of the shared `IAudioConfigStore` (which the editor reads).
   Recommendation: decide during **P1 execution** once the shape of the game's other persisted
   settings is concrete; flagged here so the choice is deliberate and not silently defaulted.
2. **Primary-route mirror timing.** The primary player's route is mirrored into the shared store's
   `activeDeviceRoute().identity` for the editor toggle. Recommendation: mirror it as part of the
   same successful-apply write that captures `serializedDeviceState()` in P2, so the mirror is never
   out of sync with the device blob; confirm you want the mirror written eagerly on every apply
   rather than lazily.

## Phased implementation

### Phase 1 — Game audio-config model (game/core)

Scope: introduce the multi-input-aware headless model in a new `game/core` `audio/` feature folder
and persist it through game/core. No audio hardware, no state machine yet — pure value types plus
their serialization, fake-tested.

- **`PlayerInputConfig`** (`rock-hero-game/core/include/rock_hero/game/core/audio/game_audio_config.h`):
  `{ int player_slot; common::audio::InputDeviceIdentity route; }` with defaulted `operator==`.
  `player_slot` is 0-based and always 0 at v1; `route` is the physical route feeding that player,
  reusing `InputDeviceIdentity` unchanged.
- **`GameAudioConfig`**: `{ std::vector<PlayerInputConfig> players; }` with defaulted `operator==`;
  v1 holds exactly one entry (slot 0). Persisting the single-entry list is what makes the
  multi-input schema real rather than a hollow symmetry type.
- **Persistence through game/core** (open question 1): either extend `IGameSettings` with
  `gameAudioConfig()` / `setGameAudioConfig()` returning `std::expected<…, …Error>`, or add a
  dedicated game audio-config serializer sibling. **Not** the shared `IAudioConfigStore` — that is
  the editor-readable route/calibration store; this is the game-private player→route mapping.
- **Primary-route mirror:** define the rule that the primary player's `route` is written into the
  shared store's `activeDeviceRoute().identity` (the field the editor toggle reads). At P1 this is
  the contract and the pure mapping function; the actual store write happens on apply in P2.
- Reuse `InputCalibrationState` / `InputDeviceIdentity` unchanged; add no multiplayer types to the
  shared store.

Files/modules: new `rock-hero-game/core/include/rock_hero/game/core/audio/game_audio_config.h`,
new `rock-hero-game/core/src/audio/` serialization TU (or `IGameSettings` extension TUs), game/core
CMake source-list additions, a game/core in-memory settings fake extension for the new accessors.

Public-header impact: new public `game_audio_config.h`; the persistence accessor(s) on the chosen
game/core settings port. No common public headers change.

Testing plan: new `rock-hero-game/core/tests/test_game_audio_config.cpp` — round-trip persist/reload
of a single-slot config; empty/default start; a two-entry config round-trips (proving the schema is
N-ready even though v1 writes one); the primary-route mapping function selects slot 0's route.

Exit criteria: model + persistence build and pass their tests; nothing drives audio yet; no editor
or common code touched.

Verification (graph changed → configure; code + tests changed → build + tests):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Configure -Targets rock_hero_game_core_tests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

### Phase 2 — Native setup drivers ★ (the audible milestone)

Scope: a game/core headless state machine that sequences **device selection → gain calibration**,
driving the shared workflows and writing the game's own store and `GameAudioConfig`. This is the
phase at which **the game guitar is audible, per-app, calibrate-first**.

- **State machine** (pure, in `game/core` `audio/`): `idle → selectingDevice → calibratingGain →
  ready` (with a `failed` terminal carrying a typed reason). Per "Separate State From Side Effects,"
  the machine is pure; a thin adapter performs the audio side effects on the message thread.
- **Device selection** drives the shared staged `AudioDeviceSettings` workflow (stage system /
  device / channel / sample rate / buffer size, then apply). On successful apply, capture
  `serializedDeviceState()` **and** the resolved `currentInputDeviceIdentity()` **together** and
  write them as one `ActiveDeviceRoute` into the game's `AudioConfigStore`. The same apply mirrors
  the primary player's route into `activeDeviceRoute().identity` (open question 2).
- **Gain calibration** drives the plan-14 `LiveInputMonitor`: `beginMeasurement` →
  meter via `ILiveInput::rawInputMeterLevel()` while the player strums → `commitCalibration`, which
  persists the `InputCalibrationState` for the active route through the shared store's
  `saveInputCalibration`. The plan-14 gate is unchanged; once a matching calibration exists for the
  active route, plan 14 Phase 4's wired-but-silent gate arms and the live guitar is audible through
  the tone.
- **Write the slot-0 `PlayerInputConfig`** into `GameAudioConfig` (via P1's persistence) for the
  selected route, completing the model side of the flow.
- Threading per "Keep Threading at the Boundary": device apply and gate arm on the message thread;
  meter sampling on the audio side via the existing lock-free tap. No audio hardware in CI — the
  machine is fake-tested against a scripted device workflow, a `FakeLiveInput`, and the in-memory
  stores.

Files/modules: new `rock-hero-game/core/include/rock_hero/game/core/audio/native_audio_setup.h`
(port + result types) and `rock-hero-game/core/src/audio/native_audio_setup.cpp` (pure machine) +
adapter TU wiring the shared workflow / monitor; game/core CMake additions.

Public-header impact: one new setup-driver port header + its result value types; the adapter and any
onset/metering glue stay `src/`-private.

Testing plan: new `rock-hero-game/core/tests/test_native_audio_setup.cpp` — full happy path
(select → apply captures blob+identity → calibrate → ready writes calibration + slot-0 config +
mirrored identity); device-apply failure surfaces a typed `failed` reason and writes nothing;
calibration-abort leaves the applied device but no calibration; re-running overwrites cleanly. Fed
by scripted fakes only.

Exit criteria: a scripted native-setup session drives a fake device + `FakeLiveInput` to a `ready`
state that has written a game `ActiveDeviceRoute` (blob + identity), a matching route calibration,
the mirrored primary-route identity, and a slot-0 `PlayerInputConfig` — proving the plan-14 gate
would arm and the live guitar would be audible. All game/core and common-audio tests pass.

Verification:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

### Phase 3 — Latency capture (off the audible path)

Scope: drive plan 13 Phase 4's play-on-cue estimator to measure and persist `LatencyOffsets`, and
mark latency stale on route change. This phase is **not** a prerequisite for audibility — a
calibrated gain route is fully audible with no latency compensation (plan 14's non-goal) — so it
depends only on plan 13 Phase 4, never pulling the latency subsystem in front of the P2 milestone.

- Extend the native-setup state machine with an optional `measuringLatency` step after `ready`
  (or a standalone driver) that runs plan 13 Phase 4's play-on-cue capture: schedule cues, collect
  strum responses, feed the shared estimator, and on success persist `LatencyOffsets` (with its
  measurement snapshot) to the game's `AudioConfigStore` for the active `AudioRouteIdentity`.
- **Staleness:** a route change (device / sample rate / buffer size differing from the measurement
  snapshot) auto-marks the latency measurement stale by plan 13 Phase 3's snapshot rule; consumers
  fall back to reported latencies and the product prompts re-measurement. Latency staleness never
  affects gain audibility.
- Pure estimation math is plan 13's; this phase only sequences the capture and persistence, fake-
  tested with scripted cue/response feeds.

Files/modules: extension to `game/core` `audio/` setup driver + a latency-capture adapter TU;
game/core CMake additions.

Public-header impact: at most a latency-step addition to the setup-driver port; no new common
headers (the estimator and `LatencyOffsets` are plan 13's).

Testing plan: extend `test_native_audio_setup.cpp` — scripted capture yields a stored,
snapshot-stamped `LatencyOffsets`; a simulated route change marks the stored measurement stale;
too-few-samples surfaces a typed failure without corrupting the audible gain state.

Exit criteria: a scripted latency capture persists a staleness-stamped offset for the game's active
route; changing the route marks it stale; gain audibility from P2 is unaffected; tests pass.

Verification:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Configure -Targets rock_hero_game_core_tests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

### Phase 4 — Startup restore

Scope: restore the game's persisted audio configuration at launch, replacing today's JUCE default,
and re-select the player-slot config; a changed route auto-marks latency stale.

- In `rock-hero-game/app/main.cpp` (building on plan 14 Phase 4's own-store composition), restore
  the game's `activeDeviceRoute().serialized_state` to the game engine via
  `IAudioDeviceConfiguration::restoreSerializedDeviceState` before the session starts (message
  thread), falling back to the JUCE default (`initialise(1, 2)`) when the store is empty.
- Re-select the `GameAudioConfig` player-slot config from game/core persistence (P1) so the slot-0
  route is bound at startup; the plan-14 gate arms if a matching calibration exists.
- A restored route that differs from a stored latency measurement's snapshot auto-marks that
  measurement stale (plan 13 Phase 3's rule), leaving gain audibility intact.

Files/modules: `rock-hero-game/app/main.cpp`, game/core startup-restore glue (a small headless
restore helper in `game/core` `audio/` so `main.cpp` stays thin), game/core CMake additions.

Public-header impact: possibly one headless restore-helper port header in `game/core`; no common
public headers change.

Testing plan: game/core test that a populated game config restores the active route + slot-0 player
and, when a calibration exists, reports a gate-armable state; an empty config falls back to default
with no crash; a route mismatch against a stored latency snapshot reports stale.

Exit criteria: launching the game restores the persisted device + player config (or cleanly falls
back to default when absent); latency staleness is reported correctly on route change; tests pass.

Verification:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

## N-input extension (post-v1, no v1 shape change)

Recorded so the multi-input claim is honest and additive:

- **N channels on the one active device** (the additive path): persist multiple `players` entries,
  each a `PlayerInputConfig` naming a different channel index on the same active
  `ActiveDeviceRoute`. The route-keyed shared store, the shared gate, and the setup machine need no
  structural change — appending entries and selecting additional channels is sufficient.
- **N players on separate physical interfaces** (genuine future rework, explicitly out of v1 scope):
  requires a **multi-device** `ActiveDeviceRoute` representation (more than one JUCE
  `AudioDeviceManager` / device blob) and a **multi-channel selection surface** across devices. This
  is real work, not a comment, and is not falsely promised as "no rework."

## Final acceptance phase

Per constraint (h), run the sanctioned bundle as separate invocations from the repo root, plus
formatting:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets clang-tidy
pre-commit run --all-files
```

Acceptance: the game configures its audio natively and headlessly — device selection → gain
calibration reaches an audible, calibrated, per-app state at P2 with no editor dependency;
`GameAudioConfig` / `PlayerInputConfig` persist a real single-slot config through game/core and
mirror the primary route into the shared store's `activeDeviceRoute().identity`; latency capture
(P3) persists staleness-stamped offsets off the audible path; startup restore (P4) rebinds the
device + player config or falls back to default; the multi-input schema is exercised and its N-input
boundary is stated honestly; all game/core and common-audio tests pass; the SDL wizard UI remains
plan 26 Phase 8's job over these headless drivers.

## Rollback/abort notes

- **P1 is additive** (new game/core types + persistence): revert = delete the new files and the
  settings-accessor addition; nothing else depends on it yet.
- **P2 is the audible milestone and the riskiest phase** (drives real shared workflows). It is
  game/core + composition only: reverting leaves plan 14 Phase 4's wired-but-silent gate intact
  (dev-seeded calibration), with no editor or common-store coupling introduced. The device-apply
  and calibration writes are last-writer-wins into the game's own file; reverting the driver does
  not corrupt an already-written store.
- **P3 is off the audible path**: reverting removes latency capture only; gain audibility from P2 is
  unaffected. The estimator is plan 13's, seam-independent — only the game-side driver is dropped.
- **P4 reverts to the JUCE default device** with no game-store coupling: reverting restores the
  `initialise(1, 2)` startup posture; the persisted config simply goes unread until re-landed.
- Each phase is fake-tested and independently revertible; none blocks the editor track (plan 48) or
  the SDL layer (plan 26 Phase 8), which degrade to "no game config yet" rather than breaking.
