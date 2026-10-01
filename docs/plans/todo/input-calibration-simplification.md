# Input Calibration Simplification

Status: deferred; captured from a Fable review on 2026-10-01 (at commit `15432e1d`). Re-verify every
citation against the current code before acting on it. The user's decisions D1-D6 below come first.

## What calibration is for

Calibration measures how loud the player's guitar arrives on one physical input route and stores
one pre-chain gain (`InputCalibrationState{Gain, InputDeviceIdentity}`), so every player on every
interface feeds the tone rack, and later note detection, at the same level: -12 dBFS active RMS,
-6 dBFS peak (`input_calibration.h`, `docs/user/input-calibration.md`). Consistency of level across
users and charts is the point, not loudness alone.

The invariant, enforced today by `LiveInputMonitor::applyGateInternal`: processed live monitoring
is on only while a one-channel input route is current, a stored calibration matches that route,
and the backend accepted the gain and the route. `docs/design/architecture.md` states only latency
calibration; this invariant should get a short "Live input" paragraph there.

## Findings, most structural first

1. **Calibration state is a cache rolled back by hand.** The workflow mutates in-memory calibration
   before the backend confirms, then needs a restore plan per failure point (`MeasurementRestore`,
   `restoreMeasurementState`, `RouteState`). A cancel path writes to disk, and one path empties the
   memo while the store still holds the record. Shape: the store is the authority, the monitor keeps
   a memo invalidated only by its own save or an identity change, a commit goes backend first, then
   store, then memo, and any failure re-runs the gate. Deletes `InputCalibrationWorkflow` as a class
   and its restore machinery: roughly 1,000 lines become 350.
2. **The measurement session is written twice**, once in the editor
   (`input_calibration_controller.cpp`) and once in the game (`native_audio_setup_driver.cpp`), with
   duplicate `CaptureSettings` and the capture policy living only in editor UI. Shape: the monitor
   owns the capture and the meter read (`beginMeasurement`, `sampleMeasurement() -> Progress`
   variant, `cancelMeasurement`); each product only drives a timer and renders.
3. **Editor UI state lives in the shared service, and the game sets it falsely.** The prompt-visible
   and settings-open flags sit in the monitor; the game calls `requestPrompt` only to pass a
   precondition and never closes it. Shape: those flags are editor-controller state.
4. **The ordered gate is stated three times** (`evaluateMonitoring`, used only by tests;
   `applyGateInternal`; `statusReasonFor`), and `contextReadyForCalibration` twice.
5. **`live_input_ready` restates `identity.has_value()`**; the game hard-codes it `true`.
6. **"Backend rejected" is stored twice** (`m_backend_available` and the status reason).
7. **A write-only identity field.** `input_channel_name` is persisted but never displayed, and five
   sites exist only to refresh it. Drop it pre-release; the identity becomes exactly its key.
8. **Dead surface:** `removeInputCalibration`, `InputCalibrationResult::measured_*`,
   `InputCalibrationPrompt::message`, and `InputCalibrationStatus` in the signal-chain view state.
9. **`InputCalibrationCaptureUpdate`** allows illegal states (phase plus two optionals); use a sum
   type.
10. **`rawInputMeterLevel() const` mutates** (attaches the reader) and must be primed by callers.

## Getting players to calibrate

Calibration is not a refused verb, so the refusal flash does not apply. The right tools are the
calibration window that already exists, plus a standing cue. The UI expert should confirm the
placement, emphasis and words.

- **Editor:** open the calibration window when the current route becomes an input route with no
  matching calibration (startup restore, a settings Apply, a device change). "Later" suppresses it
  for that route until the route changes or the app restarts. Today it opens only from the
  Calibrate button.
- **Standing cue:** on the Input column, not the header. The Calibrate button carries the accent
  while uncalibrated; the header line shrinks to the consequence.
- **Proposed words** (short, same in both products): standing "Guitar muted - calibrate input";
  prompt "Calibrate so your guitar plays at the same level as everyone else's."
- **Game:** a hard gate. The first-run wizard (plan 26 P8 over plan 32 P2) is required before the
  first song, and song start stays unavailable while the route has no matching calibration. The
  game has no calibration UI today, so with the silent device a song would play with the guitar
  inaudible and nothing saying why.

## Ordered changes

1. Dead surface and duplicates (findings 4, 7, 8, a user-doc fix: it says "press Start" where the
   button says "Calibrate"). Small.
2. Delete `live_input_ready` (finding 5). Small.
3. Move the prompt and settings-open flags to the editor controller (finding 3). Medium.
4. Store-derived calibration, deleting the rollback machinery (findings 1, 6). Large; the core.
5. Capture, meter read and policy inside the monitor (findings 2, 9, 10). Medium-large.
6. Editor auto-prompt, Input-column cue, shared wording, the architecture paragraph. Medium, after
   the UI expert.
7. The game wizard with the hard gate, deleting `--import-editor-audio`. Large; its own plan.

## Decisions for the user

- **D1:** hard-gate song start in the game on calibration (recommended: yes).
- **D2:** the editor auto-opens the calibration window on an uncalibrated route, with a per-route
  "Later" (recommended: yes).
- **D3:** drop `input_channel_name` from the identity and the on-disk format now, pre-release
  (recommended: yes).
- **D4:** whether note detection taps the input before or after the calibration gain (plan 22);
  this decides how much scoring depends on calibration.
- **D5:** cue placement and wording (UI expert).
- **D6:** keep the manual-gain path (recommended: yes; the user doc relies on it).
