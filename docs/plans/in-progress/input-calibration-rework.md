# Input Calibration Rework

Status: in progress since 2026-10-01 (user: "Take all six recommendations and write the plan";
then "start executing it").
Written from `docs/tracking/2026-10-01-input-calibration-analysis.md` (what the code does) and
`docs/tracking/2026-10-01-input-level-calibration-research.md` (what the rest of the world does),
after `docs/plans/completed/input-calibration-simplification.md` landed (`8cd9c55d` … `13e74c2d`).
Re-verify every citation against the current code before acting on it.

## Goal

One reference level, stated once as a dBu figure, from which everything else derives: the
known-interface table's gains, the strum fallback's target, and every sentence in the docs. The
primary path stays the interface's documented sensitivity (D8), now a chooser over a code-owned
table instead of a number the user reads off a web page and types. The fallback becomes what every
shipping auto-gain does — listen to the player's hardest playing and set the gain from its peak
— and loses the statistics that made it look precise while it measured the player. The two products
say one thing per live-input status from one function. The game's gate re-runs when it is asked.

Net: the plan deletes more than it adds. The capture loses its RMS-of-peaks accumulator, trim,
spread rule, second target and one error code; the editor loses its own status wording; the user
doc loses a hand-kept table of sixteen gains; a watch item and a plan-26 bullet retire.

## Decisions (ruled 2026-10-01)

- **D1 — the fallback is the hardest-playing peak ceiling.** "Play as hard as you will play";
  `gain = clamp±24(quantize(target_peak − P95(active window peaks)))`, listening a fixed 10 s
  from the first active window, the -40 dBFS floor kept as the listening threshold. Delete the
  RMS-of-peaks accumulator, the P10 trim, the spread rule and `InputInconsistent`, the two-target
  `min()`, `inputCalibrationTargetRmsDb`, and the "moderate" wording. The doc states the bar:
  ±6 dB across passive guitars, active pickups land hot, ±2 dB run to run (INFERRED until the
  hardware checklist below measures it).
- **D2 — a known-interface table in code**, `common/audio` `input/known_interfaces.{h,cpp}`.
  Each row authors its dBu at 0 dBFS, a first-class basis (manufacturer spec / community
  measurement / inferred) and a source; the gain is derived by one function. Phase 1: an
  "Interface" chooser in the editor's calibration popup fills the slider and shows the basis;
  Apply commits through `commitManualCalibration` unchanged. The user doc's table is reduced to
  prose plus the formula — never a second hand-kept copy. Phase 2 (name-matched preselection)
  stays parked behind its trigger. The game's wizard (plan 26 Phase 8) consumes the same table.
- **D3 — one `liveInputStatusText(LiveInputMonitoringStatus)` authority** in `common/audio`,
  feeding the editor's `disabled_message` and the game's `liveInputOffError`. Sentences with a
  period everywhere. Delete `inputCalibrationDisabledMessageFor` and the game's switch.
- **D4 — `refresh({.session_ready = true})` in `play()`/`restart()` before `playRefusal()`.**
  Retire the watch item "The game's live-input status goes stale after Ready" and plan 26 Phase
  8's "Device listener" bullet.
- **D5 — the target wording everywhere** (the header, the user doc, `architecture.md`, the
  developer guide) in D6's terms.
- **D6 — the reference is "+12 dBu reads 0 dBFS".** It replaces the derived +11.21 dBu; every
  row's gain moves -0.8 dB (the Quad Cortex becomes +2.3 dB, exactly the figure Neural DSP's own
  support gives for its plugins). A 1 V peak sine then reads -12.8 dBFS, so that sentence is no
  longer the primary statement: the dBu figure is.

## The one constant and what derives from it

`input_calibration.h` gains two named constants and loses one:

```cpp
/*! \brief The calibrated domain's reference: the dBu an interface input reads at 0 dBFS. */
[[nodiscard]] constexpr double inputLevelReferenceDbu() noexcept { return 12.0; }

/*! \brief A 1 V peak sine in dBu (0.707 Vrms against 0.775 Vrms); physics, not policy. */
[[nodiscard]] constexpr double oneVoltPeakSineDbu() noexcept { return -0.79; }

/*! \brief Where the hardest strum, taken as a 1 V peak source, should land after the gain. */
[[nodiscard]] constexpr double inputCalibrationTargetPeakDb() noexcept
{
    return oneVoltPeakSineDbu() - inputLevelReferenceDbu();   // -12.79 dBFS
}
```

`knownInterfaceGain(row) = row.level_at_0dbfs_dbu − inputLevelReferenceDbu()`; the fallback's
target is the derived peak above; the docs quote the reference and derive their examples from it.
Nothing else states 12, 11.21 or -12. `inputCalibrationTargetRmsDb` is deleted.

## Ordered steps

Each step is one commit. Docs and the developer guide ride the step that changes what they
describe (`CLAUDE.md`: a change that touches what the guide names updates the guide).

### 1. `refresh()` at play (D4). Small.

- `rock-hero-game/core/src/session/gameplay_session.cpp` `play()` and `restart()`: call
  `m_live_input_monitor.refresh(common::audio::LiveInputMonitoringContext{.session_ready = true})`
  immediately before `playRefusal()`. `playRefusal()` stays `const` and unchanged; the comment on
  its declaration (`gameplay_session.h`) drops "made at the Ready edge" for "re-run by every
  play". The Ready-edge refresh stays (the guitar must be audible before the song starts).
- Why it is cheap: `Engine::Impl::setMonitoringChannelEnabled` (`engine_live_input.cpp:267-272`)
  returns without a graph rebuild when both flags are unchanged, so a refresh while Active is one
  store read and one plugin parameter write.
- Tests (`test_gameplay_session.cpp`): a session reaching Ready uncalibrated, then a calibration
  saved to the store, then `play()` succeeds — the gate re-ran; and the inverse (calibration
  removed after Ready → `LiveInputOff`).
- Docs: `docs/tracking/watch-items.md` — mark "The game's live-input status goes stale after
  Ready" **Retired 2026-10-01** in place, per the file's convention, with the reason: the
  premise ("a refresh churns the backend") was false, verified at the engine line above.
  `docs/plans/roadmap/26-game-startup-menus-library.md` Phase 8: delete the "Device listener"
  bullet; add one clause to the "Live input" bullet — a live status display may still want a
  device listener, the gate does not. `docs/plans/roadmap/21-game-audio-engine-and-session.md`'s
  D1 line: "the gate re-runs at every play".
- CI: nothing new; no enum or struct changes.

### 2. One status-to-text authority (D3). Small-medium.

- New `rock-hero-common/audio/src/input/live_input_monitoring_status.cpp` (add to the
  `common/audio` CMake source list beside `live_input_monitor_error.cpp`), declaring in
  `include/.../input/live_input_monitoring_status.h`:
  ```cpp
  /*!
  \brief The one sentence per live-input status, for every surface in both products.
  \param status Status the gate reported.
  \return A sentence with a period; empty for Active.
  */
  [[nodiscard]] std::string_view liveInputStatusText(LiveInputMonitoringStatus status) noexcept;
  ```
  Words (period everywhere): Measuring "Input calibration is in progress.";
  CalibrationStoreUnavailable "Input calibration could not be read."; SessionNotReady
  "Live input is not ready.";
  NoInputDevice "No audio input device."; MissingCalibration "Input calibration required.";
  BackendUnavailable "Live input backend unavailable."
- Editor: delete `inputCalibrationDisabledMessageFor` with its file pair
  (`editor/core/src/input_calibration/input_calibration_text.{h,cpp}`) and its test; an emptied
  file is not kept as a placeholder for step 5, which gives the gain formatter a public home.
  `makeInputCalibrationProjection` (`input_calibration_projection.cpp:34-41`) keeps its four-way
  reduction (it exists because the ordered gate reports `SessionNotReady` before it looks at the
  route or the calibration, so the editor derives those two facts from `route()` and
  `calibration()` itself) and takes only the WORDS from the authority: `NoActiveInputDevice` →
  `liveInputStatusText(NoInputDevice)`, `MissingCalibration` →
  `liveInputStatusText(MissingCalibration)`, `Unavailable` →
  `liveInputStatusText(monitor.status())` (a fault status is exactly
  `BackendUnavailable` or `CalibrationStoreUnavailable`, so the store case gets its own sentence
  instead of being folded into the backend's). `Calibrated` → empty.
- Game: `liveInputOffError` (`gameplay_session.cpp`) becomes `status == Active ? nullopt :
  GameplaySessionError{LiveInputOff, std::string{liveInputStatusText(status)}}`; its switch is
  deleted. `gameplay_session_error.cpp`'s default for `LiveInputOff` becomes "Live input is off."
- Clusters from the status-text report that ride cheaply here: `native_audio_setup_driver.cpp`'s
  guard messages say "input calibration", not "gain calibration" (three strings); the dead
  `EditorSettingsErrorCode::InvalidInputCalibrationHistory` (`editor_settings_error.{h,cpp}` —
  no producer; `rg InvalidInputCalibrationHistory rock-hero-editor` shows only the enum and its
  switch) is deleted. Clusters that do NOT ride: the popup's "Live input stays off until this
  input is calibrated." (a deliberate register for one surface, UI expert); the three
  device-closed wordings (status text, dialog title, game error — three surfaces, each honest);
  the two saved-device-unavailable log lines (two composition roots, logs only); the monitor's
  "No live input route is available to calibrate." versus the native setup's phase guard (two
  different facts).
- Tests: `test_live_input_monitoring_status.cpp` (new, `common/audio/tests` CMake list): every
  enumerator yields a non-empty sentence ending in '.', Active yields empty.
  `test_input_calibration_projection.cpp` and `test_editor_controller_input_calibration.cpp`
  assert the same sentences they do today (unchanged words); `test_gameplay_session.cpp`'s two
  `LiveInputOff` message asserts gain their periods.
- Docs: `docs/developer/audio-device-settings.md` — one sentence under "The shared measurement":
  the status words live in `liveInputStatusText`, never per product.
- CI: `-Wswitch-enum` — the new switch lists all seven enumerators with no `default`; deleting
  the game's switch and the editor's removes two more. `-Wunused-function` on any helper the
  deleted editor function leaves without a caller. `performance-no-automatic-move` does not apply
  to a `string_view` return.

### 3. The reference and the fallback (D1, D5, D6). Medium; the core.

- `input_calibration.h`: add the two constants and the derived target above; delete
  `inputCalibrationTargetRmsDb`, `inputCalibrationReferencePeakPercentile` (becomes
  `inputCalibrationCeilingPercentile() = 0.95`), `inputCalibrationConsistencyLowPercentile`,
  `maximumInputCalibrationActivePeakSpreadDb`. Rename `inputCalibrationMeasurementSampleCount`
  (240) to `inputCalibrationListenSampleCount` = 300 (10 s at 30 Hz, counted from the first
  active window, which itself counts). Keep `inputCalibrationSettleSampleCount` (15),
  `inputCalibrationWaitSampleCount` (300: the player may not have started; `NoUsableSignal` when
  nothing crosses the floor in 10 s), `minimumInputCalibrationSignalDb` (-40, now documented as
  the listening threshold), `minimumInputCalibrationActiveSampleCount` (12), and
  `inputCalibrationGainStepDb` with its step changed from 0.5 to 0.1 so the fallback speaks the
  manual slider's resolution. `InputCalibrationErrorCode` loses `InputInconsistent`.
  `InputCalibrationMeasurement` becomes `{AudioMeterLevel loudest_level; double ceiling_peak_db;
  std::size_t active_sample_count;}` — update every designated initializer of it (sweep by the
  sibling field: `rg '\.loudest_level ='`).
- `input_calibration.cpp`: the accumulator keeps the active peaks; `measurement()` sorts once and
  reads P95 through the existing `percentileIndex`; delete `rmsDbForSortedRange`, the square-sum,
  the spread, and the `InputInconsistent` case of `inputCalibrationError`.
  `calculateInputCalibration` becomes: clipped → `InputClipped`; fewer than 12 active windows →
  `NoUsableSignal`; else
  `clampGain(Gain{quantize(inputCalibrationTargetPeakDb() − ceiling_peak_db)})`.
  The capture's `Measuring` stage now runs `inputCalibrationListenSampleCount` windows; the stage
  names (`Settling`, `WaitingForInput`, `Measuring`) are kept.
- **Progress carries the windows left** (UI ruling: a whole-second countdown while measuring).
  The capture already counts its windows; the popup must not count them a second time, so the
  stage alternative of `InputCalibrationStep` / `InputCalibrationProgress`
  (`input_calibration.h`, `live_input_sample.h`) becomes
  ```cpp
  /*! \brief Where a running measurement is, and how many meter windows its stage has left. */
  struct InputCalibrationStageProgress
  {
      InputCalibrationStage stage;
      std::size_t windows_remaining;
  };
  ```
  in place of the bare enum. `windows_remaining` is meaningful for every stage (settle, wait and
  listen windows left), so the type carries no optional. This is the one piece of common
  machinery the UI rulings add; the alternative — the popup counting its own ticks against
  `inputCalibrationListenSampleCount()` — is the same count stated twice. Sites: the capture's
  `pushSample`, `LiveInputMonitor::sample`, the editor controller's `std::get_if<InputCalibrationStage>`
  (`input_calibration_controller.cpp`), the game driver's `holds_alternative` and its tests, and
  the common tests. The editor derives the countdown as
  `ceil(windows_remaining / inputCalibrationSampleRateHz())` seconds and rewrites the status only
  when the whole second changes.
- Words (UI ruling; `input_calibration_controller.cpp:45-54`): waiting "Play as hard as you play
  in a song, on all strings."; measuring "Keep playing that hard. N s left." (no countdown while
  waiting); the error words in `inputCalibrationError` lose "steadily"/"moderate". **The popup's
  target line is deleted** (`input_calibration_window.cpp:27-32,101`, `inputCalibrationTargetText`
  and `m_target_label`; its RMS constant is gone anyway). The gain slider gets a tooltip in its
  place: "Gain = your interface's dBu at 0 dBFS, minus 12.", the 12 formatted from
  `inputLevelReferenceDbu()` so the sentence can never drift from the constant.
- Tests: `test_input_calibration.cpp` rewritten around the ceiling: a steady level L yields
  `target − L`; P95 ignores a single spike; a decaying pattern's quiet windows do not count; the
  listen count is 300 from the first active window; the wait timeout; the 12-window minimum;
  clipping. `test_live_input_monitor.cpp`, `test_editor_controller_input_calibration.cpp` and
  `test_native_audio_setup.cpp` compute their "longest measurement" from the renamed constants
  (settle + wait + listen). Any test that pinned `InputInconsistent` is deleted, not rewritten.
  Progress: a capture test asserts `windows_remaining` counts down to 0 across each stage; the
  popup controller test asserts "10 s left" at the first measuring sample and that the text
  changes only on whole seconds.
- Docs: `docs/user/input-calibration.md` top: the reference in D6's words, the two paths, the
  automatic method's instruction and its bar (±6 dB passive, actives hot, ±2 dB run to run,
  marked as awaiting measurement), and two sentences on the multimeter method as the way to
  contribute a row (research recommendation 5). `architecture.md:480` "Live input" paragraph:
  add the reference sentence. `docs/developer/audio-device-settings.md` "The shared measurement":
  the ceiling, the listen shape, the one constant. The `InputCalibrationCapture` class doc and
  the measurement struct doc state the method.
- CI: `-Wswitch-enum` — removing `InputInconsistent` deletes a case, never adds one, but
  `rg "InputCalibrationErrorCode::"` across tests for pinned enumerators; `-Wfloat-equal` — the
  capture compares peaks with `<`/`>=` only, and tests use `WithinULP`/`Approx` as today;
  `-Wmissing-designated-field-initializers` at every `InputCalibrationMeasurement{` and every
  `InputCalibrationStageProgress{` (no DMIs; list both fields; sweep tests by `.stage =`);
  `-Wunused-function` for `rmsDbForSortedRange` if a stub survives, and for the window's
  `inputCalibrationTargetText` if its label goes but the function stays; `-Wsign-conversion` in
  the seconds derivation (`std::size_t` windows over an `int` rate — convert once, explicitly).

### 4. The known-interface table (D2 data, D6 numbers). Medium.

- New `rock-hero-common/audio/include/rock_hero/common/audio/input/known_interfaces.h` and
  `src/input/known_interfaces.cpp` (CMake source list):
  ```cpp
  /*! \brief How a row's level was established, so a surface can say how far to trust it. */
  enum class KnownInterfaceBasis : std::uint8_t
  {
      ManufacturerSpec,
      CommunityMeasurement,
      Inferred,
  };

  /*! \brief One interface whose instrument-input sensitivity at minimum gain is known. */
  struct KnownInterface
  {
      std::string_view model;             // "Neural DSP Quad Cortex"
      std::string_view unity_input;       // "instrument input, 1 MOhm, 0.0 dB input level"
      double level_at_0dbfs_dbu;          // authored: +14.3
      KnownInterfaceBasis basis;
      std::string_view source;            // the URL the user doc cites today
  };

  [[nodiscard]] std::span<const KnownInterface> knownInterfaces() noexcept;   // sorted by model
  [[nodiscard]] Gain knownInterfaceGain(const KnownInterface& interface) noexcept; // level − ref
  ```
  Rows: the sixteen in `docs/user/input-calibration.md:38-53` minus one (below), each with the
  basis the doc states today and its source link. **`unity_input` phrasing rule (UI ruling, a
  correctness point):** a figure is true only at the setting it was measured at, so the popup
  shows it — "Set the interface to <unity_input>, then click Apply." Every `unity_input` is
  therefore a lower-case phrase, no trailing period, that completes "Set the interface to ___":
  "instrument input, 1 MOhm, 0.0 dB input level", "combo TRS guitar input, minimum gain",
  "instrument input, pad off, minimum gain". Derived gains after D6: Scarlett 3rd Gen +0.5,
  4th Gen 0.0, MOTU M +4.0, Quad Cortex +2.3, Quad Cortex mini +2.5, Nano Cortex -2.0, UA Volt
  +0.5, Arturia MiniFuse -0.5, Audient iD4 0.0, SSL 2 +3.0, PreSonus 24c +7.0, Behringer UMC22
  -10.0.
- **Disputed rows.** Behringer UMC202HD/204HD is NOT shipped: the spec sheet's -3 dBu and the
  community's +16.8 dBu are 20 dB apart, a row that may be 20 dB wrong is worse than none, and
  the fallback serves those players until a meter reading lands (hardware checklist). MOTU M
  (+16.0, the user guide) ships as `ManufacturerSpec` with GENOME's +17.5 noted in the source
  comment; Audient iD4 (+12.0, Audient's own sheet) ships as `ManufacturerSpec`; the Quad Cortex
  family (+14.3 / +14.5 / +10.0) ships as `Inferred`, as the doc says today, with GENOME's +14.8
  in the comment. The popup (step 5) shows the basis, so an inferred row says so.
- The user doc: delete the table and the "1V peak sine reads" column arithmetic; keep the
  reference, the formula `Rock Hero gain = level at 0 dBFS (dBu) − 12 dBu`, the unity-input
  assumptions (minimum gain, Hi-Z input, pads and boosts off), the sources list (moved into the
  rows' `source` fields, so the doc can link to the sources without restating any number), and a
  sentence saying the Interface list in the calibration window carries the figure and its basis.
- Tests: `test_known_interfaces.cpp` (new): rows sorted and unique by model; every row has a
  model, a unity-input phrase that starts lower-case and ends without a period, and a source;
  every dBu within [-10, +30]; every derived gain inside the ±24 clamp; the Quad Cortex derives
  +2.3 (pins the D6 reference through the one function).
- Docs: `docs/developer/audio-device-settings.md` — a short "Known interfaces" paragraph: data
  in code, gain derived, basis first-class, the doc never copies a number; the silent step
  "adding a row" (model, unity input, dBu, basis, source; nothing else to update).
- CI: every row is a designated initializer of a struct with no DMIs — list all five fields in
  every row (`-Wmissing-designated-field-initializers`); `-Wfloat-equal` — no row compares;
  `modernize-use-designated-initializers` on the rows; `performance-enum-size` on the basis enum
  (`std::uint8_t` above). A `constexpr std::array` of rows with `std::string_view` members is
  the shape; `std::span` over it is the interface.

### 5. The Interface chooser in the editor popup (D2 Phase 1). Medium.

The popup's look and words follow the UI expert's rulings of 2026-10-01 (the player calibrates
once and reads carefully; every sentence says what to do next). Each ruling below is adopted as
stated unless marked otherwise.

- **Layout** (top to bottom): the Interface row (label, combo, the "?" at its end); the Gain row
  (label, the slider — the one gain control for every path — its signed text box, Apply); the
  status box, two lines, full width; the input meter; then "Measure by playing" bottom-left and
  "Later" bottom-right. Both labels share the existing 60 px column.
- **View state** (`input_calibration_view_state.h`): add `std::optional<std::size_t>
  selected_interface` (an index into `knownInterfaces()`; the view and the controller read the
  same span, and the index is the contract) and extend the hand-written `operator==`, as its
  comment requires. The choices are static data the view reads from `knownInterfaces()`, not
  view state.
- **Chooser**: a `juce::ComboBox`, label "Interface:", `setTextWhenNothingSelected("Choose your
  interface")`, items from `knownInterfaces()` in table order (sorted by model; no manufacturer
  groups below about 30 rows). **No "Not listed" item**: a placeholder is true in every cleared
  state, an item would be a claim. A slider drag after choosing clears the selection and returns
  the status to the ready text. A measurement clears the selection when it starts, disables the
  combo along with the slider and Apply while it runs, and leaves the selection cleared on
  success and failure.
- **Controller** (`input_calibration_controller.{h,cpp}`): new intent
  `onInterfaceSelected(std::size_t index)` — sets the displayed gain to `knownInterfaceGain(row).db`
  and the status to `<basis sentence> Set the interface to <unity_input>, then click Apply.`
  with the basis from one file-local function over `KnownInterfaceBasis`: "Manufacturer's
  figure." / "Community-measured figure." / "Estimated figure." The model and the gain are left
  out: the combo and the slider already show them. The `IInputCalibrationView` contract is
  unchanged; Apply is unchanged and commits the displayed gain through the host.
- **Words**: the Calibrate button becomes "Measure by playing" (no ellipsis), so a player with no
  listed interface finds the second path unaided. Idle, uncalibrated: "Live input stays off until
  you calibrate. Choose your interface, or click Measure by playing." (the earlier one-button
  sentence carried no fix clause because one button was the fix; with two paths the sentence
  names both). Idle, calibrated: "Calibrated. Choose an interface or change the gain to
  recalibrate." Success has one shape, replacing the two sentences at
  `input_calibration_controller.cpp:81,87`: "Saved: +2.3 dB for the Neural DSP Quad Cortex." /
  "Saved: +2.3 dB." / "Saved: +4.1 dB, measured from your playing." The docs-missing text becomes
  "The calibration guide is not installed."
- **One gain formatter**: gains are signed everywhere. The controller's file-local `gainText`
  (`:65`) moves to a PUBLIC editor-core header (`editor/core/include/.../input_calibration/`,
  since the slider in `editor/ui` calls it and cannot include core's `src/`) as
  `signedGainText(double) -> std::string`
  ("+2.3", "-0.5", "0.0"), and the slider's text box prints through the same function via
  `textFromValueFunction` — the status line and the slider can never disagree on a sign or a
  decimal.
- **Theme**: the status label's own colours (`input_calibration_window.cpp:139-142`: an RGB
  background, `whitesmoke`, a black outline) bypass `EditorTheme`, the one colour seam. Use
  `panel_background` and `primary_text`, no outline; the basis sentence is normal text — no badge,
  icon, tooltip, muted grey or red.
- **Tests**: controller — selecting a row shows its gain and the basis-plus-unity sentence; Apply
  commits that gain and reports the one success shape with the model; a slider drag clears the
  selection and restores the ready text; starting a measurement clears it; `signedGainText` on
  +2.3 / -0.5 / 0.0. UI wiring — the combo emits the intent with the right index; the combo is
  disabled while measuring. Test names ≤ 78 characters.
- **Sighting** (1:1 screenshot, before the step closes): the longest model and the longest
  `unity_input` fit the two-line box — if not, the basis sentence shrinks to one word; "Estimated
  figure." does not read as an error; a player without a listed interface finds "Measure by
  playing" unaided. **Open for the user at the sighting:** whether clearing the selection on a
  nudge feels punishing; the alternative is a "(modified)" suffix. Clearing is the simpler model
  (the placeholder is always true) and ships first.
- Docs: `docs/user/input-calibration.md` "Recommended method": choose the interface, set it as
  the sentence says, Apply; `docs/developer/audio-device-settings.md` names the chooser as the
  popup's primary path and the status-sentence rule. Plan 26 Phase 8's wizard bullet: "renders
  `knownInterfaces()` with the same basis-plus-unity sentence; the game has no manual path
  otherwise".
- CI: `-Wshadow` in the combo's `onChange` lambda; `bugprone-unchecked-optional-access` on
  `selected_interface` (bind once, guard with `if`); `-Wsign-conversion` between the combo's
  `int` ids and `std::size_t` (convert explicitly once at the boundary); `-Wswitch-enum` on the
  basis-sentence switch (three enumerators, no `default`); `-Wunused-function` for the two
  deleted success-text helpers.

## Hardware checklist (for the user; nothing in the steps waits on it except the Behringer row)

- [ ] Behringer UMC202HD/204HD instrument input at minimum gain, pad off: one multimeter reading
      settles -3 dBu versus +16.8 dBu and restores the row.
- [ ] MOTU M-series: +16.0 (user guide) or +17.5 (GENOME); 1.5 dB.
- [ ] Quad Cortex: +14.3 (inferred from NDSP support's -15.1 dBFS at 1 Vp) or +14.8 (GENOME).
- [ ] What `AudioDeviceStatus::device_name` and the ASIO channel names read for a Scarlett and a
      Quad Cortex under ASIO and under WASAPI — the Phase 2 trigger.
- [ ] Run-to-run spread of "play hard, P95 peak": five runs each on a single coil, a humbucker
      and an active guitar through one interface, replacing the INFERRED ±2 dB in the user doc.
- [ ] Whether Windows shared-mode volume or "microphone boost" shifts the raw level of a
      class-driver device (the fixed-gain USB cables) under WASAPI; if it does, such a device can
      have no table row and the fallback is its only path — a sentence in the user doc.

## Out of scope

- **Loopback through the interface's own output** — rejected for gain (it measures output level
  minus input sensitivity and isolates nothing without a published output spec) and reserved for
  latency calibration. Plan 13 Phase 3 is where latency lives; this plan does not edit plan 13.
- **Automatic device reopen** — `docs/plans/todo/safe-device-auto-reopen.md` records what a
  reopen owes the device-lost notice; unchanged by this plan.
- **Phase 2 name-matched preselection** — parked behind the sighted device-name trigger above;
  authoring name patterns from guesses would be speculative.
- **A known-voltage phone-adapter source** (research method 4) — a documented advanced path at
  most, after a sighting proves a player would use it; no code.
- **Per-guitar leveling on top of the interface calibration** (the modelers' second knob) — the
  rack's own input trim serves it; not a calibration concern.
