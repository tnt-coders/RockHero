# Input Calibration Simplification

Status: in progress, ruled 2026-10-01 (user); steps 1-6 run now, step 7 is its own plan, step 8
follows once the restructuring lands. Captured from a Fable
review on 2026-10-01 (at commit `15432e1d`). Re-verify every citation against the current code before
acting on it.

## What calibration is for

Calibration measures how loud the player's guitar arrives on one physical input route and stores
one pre-chain gain (`InputCalibrationState{Gain, InputDeviceIdentity}`), so every player on every
interface feeds the tone rack, and later note detection, at the same level: -12 dBFS active RMS,
-6 dBFS peak (`input_calibration.h`, `docs/user/input-calibration.md`). Consistency of level across
users and charts is the point, not loudness alone.

The invariant, enforced today by `LiveInputMonitor::applyGateInternal`: processed live monitoring
is on only while a one-channel input route is current, a stored calibration matches that route,
and the backend accepted the gain and the route. **User ruling, 2026-10-01: the editor never
outputs the live rig without a calibration**, because tones can only be authored properly on a
clean, calibrated input. Every refactor below must keep this gate exactly. `docs/design/architecture.md` states only latency
calibration; this invariant should get a short "Live input" paragraph there.

## Findings, most structural first

1. **Calibration state is a cache rolled back by hand.** The workflow mutates in-memory calibration
   before the backend confirms, then needs a restore plan per failure point (`MeasurementRestore`,
   `restoreMeasurementState`, `RouteState`). A cancel path writes to disk, and one path empties the
   memo while the store still holds the record. Shape: the store is the authority, the monitor keeps
   a memo invalidated only by its own save or an identity change, a commit goes backend first, then
   store, then memo, and any failure re-runs the gate. Deletes `InputCalibrationWorkflow` as a class
   and its restore machinery: roughly 1,000 lines become 350. **Amended 2026-10-01:** the editor and
   the game now share one store file, so a memo invalidated only by its own save goes stale when the
   other product calibrates. Keep no memo: read the store when the gate is evaluated (route changes
   and commits, not per block), which the store's per-operation fresh read already supports.
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
- **Standing cue (BUILT 2026-10-01, UI expert):** the header line is gone. The Input caption
  carries a muted "disabled" line while live input is off, the same word for every cause; the
  Calibrate button's own enablement tells the causes apart, and the reason ("No audio input
  device." / "Input calibration required.") is the tooltip on the Calibrate button and the input
  meter. No accent on Calibrate: the theme's accent means selection and drop targets, and the
  auto-prompt makes it redundant.
- **Prompt words** (UI expert): "Live input stays off until this input is calibrated." The prompt's
  own Calibrate button is the fix, so the sentence carries no fix clause.
- **First run** (UI expert): with no saved route in the shared store, open the audio-device settings
  window at startup over the running OS default. Cancel writes nothing, so it reopens next launch
  while no route is saved. Closing on an uncalibrated input route then fires the editor prompt
  above, so device-then-calibrate needs no first-run wizard.
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
5. Capture, meter read and policy inside the monitor (findings 2, 9, 10). Medium-large. A gate
   run ends a measurement without telling the driver (a device change, a rig-load refresh), so
   the sampling call should report the measurement ended; and the game holds three records of
   "measuring" (its machine phase, its capture, the monitor) that this step should reduce to one.
6. Editor auto-prompt, Input-column cue, shared wording, the architecture paragraph. Medium, after
   the UI expert. The popup seeds its committed gain once from the prompt, so the auto-prompt must
   build a fresh popup for each route (or read the gain from `monitor.calibration()`).
6a. When the saved device is not running (D7, revised): a notice with Audio Settings and Close
    buttons on the mid-session hardware-loss edge (after the pause) and at a startup whose saved
    route did not open; a true first run with no saved route opens the settings window directly.
    Windows open from view state, the one mechanism the auto-prompt also uses. When the device
    drops with the calibration prompt open, the notice wins: the gate run ends the measurement
    and the prompt closes with its route. "Cancel writes nothing" holds since the route is saved
    only by the settings window's apply: JUCE's startup open never wrote a route (it passes
    treatAsChosenDevice=false), but a settings Cancel did, through the editor's old listener
    persist.
7. The game wizard with the hard gate. Large; its own plan. `GainCalibrationProgress` copies
   `InputCalibrationStage` one-to-one: return the monitor's `InputCalibrationProgress` instead, and
   report a measurement a gate run ended as ended rather than as an invalid request. (`--import-editor-audio` is already
   deleted.)
8. A deep analysis, once steps 1-6 land, of whether the calibration algorithm or the whole process
   could be better: what the strum measurement fallback measures and why it does not repeat, how
   documented interface gains reach the user (the window's "?" link to known devices today), and
   whether a known-device table should fill the gain in.

## Decisions for the user (ruled 2026-10-01)

- **D1 — YES:** hard-gate song start in the game on calibration.
- **D2 — YES:** the editor auto-opens the calibration window on an uncalibrated route, with a
  per-route "Later" until the route changes or the app restarts.
- **D3 — YES:** drop `input_channel_name` from the identity and the on-disk format now, pre-release.
- **D4 — after the gain, deferred to plan 22:** note detection taps the calibrated signal, so its
  thresholds hold in one level domain for every player. Nothing in steps 1-6 depends on it.
- **D5 — RESOLVED** by the UI expert (above).
- **D6 — KEEP manual gain as a first-class path, not a demoted "Adjust".** Repeated strum
  measurement did not give consistent results, and many interfaces have a documented gain that
  sets the level exactly (the Neural DSP Quad Cortex: +3.1 dB); typing it dials the route in
  perfectly and repeatably. Step 5's capture move must not weaken it.
- **D7 — YES, widened, then revised (2026-10-01):** when the saved device is not running, the
  editor says so in a notice with two buttons, one opening the audio-device settings window and one
  closing the notice, rather than opening the window itself: "a popup letting the user clearly know
  the device disconnected ... might actually be clearer". It covers the moment the hardware drops
  mid-session (after the pause) and a startup whose saved route did not open (an unavailable
  device, an invalid route). A true first run with no saved route has nothing to report, so it
  opens the settings window directly over the running OS default. Neither path writes anything on
  Cancel or Close.
- **Scope — YES:** steps 1-6 now; step 7 (the game wizard) is its own plan.
- **D8 — the INTERFACE:** players swap guitars without recalibrating, so the interface is the
  closest stable thing to calibrate. The documented per-interface gain is the primary path; the strum
  measurement is the fallback for interfaces with no published figure. See below and step 8.

## D8: the interface, or the guitar? (ruled: the interface)

A documented per-interface gain is a constant of the interface's input sensitivity: it maps a given
instrument voltage to a fixed dBFS. A strum measurement instead folds in the guitar's pickup output
and how hard that strum was played, which is one likely reason it does not repeat. The two also
disagree on what "consistent" means: interface calibration lets a hot pickup drive the rig harder,
as it would a real amp; strum calibration levels every guitar to the same loudness. Decide which
the product wants before step 5 moves the capture into the monitor, because the answer may make the
documented gain primary and the strum measurement a fallback for undocumented interfaces.
