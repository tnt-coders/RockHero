# Required Input Calibration Plan

Status: V3. Adds `InputDeviceIdentity` in `common/audio` with `IAudioDeviceConfiguration`
support, blocks audio device settings while the calibration window is visible, renames
`input_gain_meter` to `input_meter`, and adds a covered case for external device disconnection
during calibration.

## Goal

Replace the current user-adjustable input gain slider with a required input calibration workflow.
Input calibration is per-user application state, not project or tone-chain state. Live processed
guitar input must stay unavailable until an active input device has a successful calibration.

Output gain remains unchanged: it stays in the signal chain, keeps its vertical slider and meter,
and continues to persist with the tone document because it is part of the authored tone balance.

## Design Rules

- Store one optional app-local calibration gain value for this pass.
- Do not add per-device calibration records yet.
- Do not add instrument or guitar profiles yet.
- Do not continuously detect hardware gain changes during playback yet.
- Clear calibration when the active input route identity changes (device type, device name, or
  selected input channel identities).
- Preserve calibration when the input device identity is unchanged, even if buffer size, sample
  rate, output device, or other non-input fields change.
- Preserve calibration when the audio-device settings window is opened and then closed or applied
  with the same input device identity restored.
- Disable live processed input until calibration succeeds.
- Keep raw input measurement available for the calibration popup.
- Do not use attenuation as a mute. Use an explicit monitoring gate.
- Do not write input calibration gain to project files, `.rock` packages, or tone documents.
- Keep output gain persistence and UI behavior unchanged.
- Audio device settings are unavailable while the calibration window is visible. The user must
  dismiss or complete calibration before changing audio device settings.

## V3 User Experience

The only automatic rule is:

```text
If an active input device exists and input calibration is unset, show the calibration popup.
```

The app should call that rule after:

- audio-device state is restored on startup;
- an audio-device settings apply succeeds;
- an audio-device settings cancel/native close restores a previous route;
- the editor view attaches after controller construction;
- manual recalibration is requested from the signal-chain panel.

The signal-chain panel should use the existing left rail but change its meaning:

```text
+--------------+-------------------------------+--------------+
| Input        | Signal Chain                  | Output       |
|              |                               |              |
| meter        | plugin rows                   | gain slider  |
| meter        |                               | meter        |
| meter        |                               | meter        |
|              |                               | meter        |
| Calibrate    |                               |              |
+--------------+-------------------------------+--------------+
```

The input gain slider is removed. The input rail contains:

- `Input` label;
- existing vertical input meter;
- `Calibrate` button;
- optional compact status text only if it fits cleanly.

The output rail remains:

- `Output` label;
- existing vertical output gain slider;
- existing vertical output meter.

## Calibration Popup Behavior

The calibration popup is required when input calibration is missing. It should allow the user to
leave the popup, but leaving does not enable live input or playback. The app remains usable for
non-audition tasks, but live processed guitar monitoring stays disabled.

While the calibration window is visible, audio device settings changes are unavailable. This
prevents device changes from racing with an active measurement and simplifies the calibration
lifecycle.

The popup should support:

1. `Start` begins a short measurement window.
2. The user plays loud, normal hard strums.
3. The popup samples the raw input meter for the measurement window.
4. If the input clips, the popup tells the user to lower the hardware/interface gain and retry.
5. If there is no usable signal, the popup tells the user to check the input and retry.
6. If the signal is usable, the app computes and stores the calibration gain.
7. The live input port applies the calibration gain and enables live input monitoring.

Before starting a measurement, the controller must disable live processed monitoring and reset the
input gain to `0.0 dB` through the live input port so calibration reads the raw hardware level. If
the user was already calibrated, the previous gain and monitoring state are restored on dismissal;
if calibration succeeds the new gain replaces it and processed monitoring is re-enabled. This makes
recalibration identical to fresh calibration without temporarily monitoring an uncalibrated tone
chain.

V3 still uses a simple peak-based target:

```text
input_calibration_gain_db = target_peak_dbfs - measured_peak_dbfs
```

Use `common::audio::clampGain()` before applying or storing the gain. Start with a conservative
target such as `-12.0 dBFS` for hard-strum peaks. RMS or LUFS-style calibration can replace this
later if real use proves peak-only calibration is too rough.

## Runtime Gate

There must be two barriers against uncalibrated live input:

1. Controller/UI gate:
   - `play_pause_enabled` is false while calibration is required.
   - signal-chain live input status shows calibration is required.
   - plugin audition through transport cannot start while calibration is required.

2. Audio backend gate:
   - live input monitoring is explicitly disabled through the live input port while calibration is
     required.
   - calibration measurement reads raw input level through the live input port's raw meter, which
     remains active when processed monitoring is disabled.
   - the gate is not represented by setting gain to `-24.0 dB`.

This gives a fail-safe behavior if a UI action gate is missed: the audio adapter still does not
monitor uncalibrated live guitar through the tone chain.

## Planned Public/Core Types

### `rock-hero-common/audio/include/rock_hero/common/audio/input_calibration.h`

Add a small pure calibration helper header in `common/audio` because both editor and game will need
the same policy later.

Planned types and functions:

- `enum class InputCalibrationErrorCode`
  - `NoUsableSignal`
  - `InputClipped`
- `struct InputCalibrationError`
  - `InputCalibrationErrorCode code`
  - `std::string message`
- `struct InputCalibrationMeasurement`
  - `common::audio::AudioMeterLevel loudest_level`
- `struct InputCalibrationResult`
  - `common::audio::Gain calibration_gain`
  - `common::audio::AudioMeterLevel measured_level`
- `class InputCalibrationAccumulator`
  - `void reset()`
  - `void pushSample(AudioMeterLevel level)`
  - `[[nodiscard]] InputCalibrationMeasurement measurement() const`
- `[[nodiscard]] constexpr double inputCalibrationTargetPeakDb() noexcept`
- `[[nodiscard]] constexpr double minimumInputCalibrationSignalDb() noexcept`
- `[[nodiscard]] std::expected<InputCalibrationResult, InputCalibrationError>
  calculateInputCalibration(const InputCalibrationMeasurement& measurement)`

The accumulator only tracks the loudest meter sample and clipping state for this pass. It should
not own timers, UI state, audio devices, or settings.

### `rock-hero-editor/core/include/.../editor_settings.h`

Add app-local persistence:

- `[[nodiscard]] std::optional<double> inputCalibrationGainDb() const`
- `void setInputCalibrationGainDb(std::optional<double> gain_db)`

Implementation variable/key:

- `constexpr const char* g_input_calibration_gain_db_key{"inputCalibrationGainDb"}`

Read behavior:

- missing key returns `std::nullopt`;
- invalid or non-finite values return `std::nullopt`;
- valid values are clamped through `common::audio::clampGain()` before use.

### `rock-hero-common/audio/include/.../input_device_identity.h`

Add a project-owned value struct in `common/audio` that captures the input route fields relevant to
calibration validity. Both editor and game will compare this struct to decide whether a stored
calibration is still valid for the current hardware.

Planned type:

- `struct InputDeviceIdentity`
  - `std::string backend_name` — device type reported by the adapter, such as ASIO
  - `std::string device_name` — human-readable device name
  - `std::string selected_input_channel_name` — name of the selected mono input channel
  - `friend bool operator==(const InputDeviceIdentity&, const InputDeviceIdentity&) = default`

The struct uses channel name rather than a numeric channel ID because the existing audio device
settings model identifies the selected input channel by its name within the device's channel list.
Channel names are stable for the same device across sessions; numeric choice IDs are ephemeral
one-based indices that may shift when available channels change.

A default-constructed value represents no input route. Comparing two default-constructed values is
equal, but the controller should treat a default-constructed identity as no active input rather
than a valid identity to preserve.

### `rock-hero-common/audio/include/.../i_audio_device_configuration.h`

Add a method to query the current input device identity:

- `[[nodiscard]] virtual std::optional<InputDeviceIdentity> currentInputDeviceIdentity() const = 0`

Returns `std::nullopt` when no audio device is open or no input channel is selected. The engine
implementation builds the identity from `juce::AudioIODevice::getTypeName()`,
`juce::AudioIODevice::getName()`, and the name of the first active input channel.

### `rock-hero-common/audio/include/.../i_live_rig.h`

Input gain is an application-level calibration concern, not part of the authored tone. Remove input
gain ownership from the live rig so it is purely about the tone chain.

Planned removals:

- remove `inputGain() const` — moves to `ILiveInput`
- remove `setInputGain(Gain gain)` — moves to `ILiveInput`
- remove `input_gain` from `LiveRigSnapshot` — tone captures no longer include input gain
- remove `input_gain` from `LiveRigLoadResult` — tone loads no longer return input gain

Keep:

- `outputGain() const`
- `setOutputGain(Gain gain)`
- `output_gain` in `LiveRigSnapshot` and `LiveRigLoadResult`

### `rock-hero-common/audio/include/.../i_live_input.h`

Add a small interface in `common/audio` for application-level input stage control. Both editor and
game will need this for calibration and monitoring gating. The engine implements it alongside its
other ports; it is not part of the tone rig.

Planned types and methods:

- `[[nodiscard]] virtual Gain inputGain() const = 0`
- `[[nodiscard]] virtual std::expected<void, LiveInputError>
  setInputGain(Gain gain) = 0`
- `[[nodiscard]] virtual AudioMeterLevel rawInputMeterLevel() const = 0`
- `[[nodiscard]] virtual bool liveInputMonitoringEnabled() const = 0`
- `[[nodiscard]] virtual std::expected<void, LiveInputError>
  setLiveInputMonitoringEnabled(bool enabled) = 0`

The backend must keep raw input metering available when monitoring is disabled and when no project
or tone document is loaded. The structural input gain plugin in the engine stays where it is in the
Tracktion chain; only the ownership boundary changes.

### `rock-hero-common/audio/include/.../live_input_error.h`

Add a typed error domain for `ILiveInput` operations:

- `enum class LiveInputErrorCode`
  - `MessageThreadRequired`
  - `InputRouteMissing`
  - `CouldNotSetInputGain`
  - `CouldNotSetMonitoring`
- `struct LiveInputError`
  - `LiveInputErrorCode code`
  - `std::string message`

Use this error domain for live-input calibration and monitoring operations so failures do not reuse
the tone-rig error vocabulary.

### `rock-hero-editor/core/include/.../signal_chain_view_state.h`

Replace input gain state with calibration state while leaving output gain fields intact.

Planned additions:

- `enum class InputCalibrationStatus`
  - `NoActiveInputDevice`
  - `Required`
  - `Calibrated`
- `InputCalibrationStatus input_calibration_status{InputCalibrationStatus::NoActiveInputDevice}`
- `bool input_calibration_button_enabled{false}`
- `bool output_gain_controls_enabled{false}`

Planned removals or renames:

- remove `input_gain_db`;
- replace `gain_controls_enabled` with `output_gain_controls_enabled`;
- keep `output_gain_db`.

### `rock-hero-editor/core/include/.../editor_view_state.h`

Add prompt state and audio-settings gate:

- `struct InputCalibrationPrompt`
  - `bool required{true}`
- `std::optional<InputCalibrationPrompt> input_calibration_prompt`

The prompt is state, not a one-shot effect, because the controller must be able to keep it visible
until the user succeeds, closes it, or changes audio settings.

Disable audio device settings while the calibration window is visible:

- `audio_device_settings_enabled` is false when `input_calibration_prompt` is set.

This replaces the need for runtime device-change handling during measurement.

### `rock-hero-editor/core/include/.../i_editor_controller.h`

Replace the input-gain slider intent with calibration intents:

- remove `virtual void onInputGainChanged(double gain_db) = 0`
- add `virtual void onInputCalibrationRequested() = 0`
- add `virtual void onInputCalibrationMeasurementStarted() = 0`
- add `virtual void onInputCalibrationSucceeded(double gain_db) = 0`
- add `virtual void onInputCalibrationDismissed() = 0`

Keep:

- `virtual void onOutputGainChanged(double gain_db) = 0`

Add audio-device edit lifecycle hooks so calibration can distinguish temporary settings-window
device closure from a committed route change:

- `virtual void onAudioDeviceSettingsOpened() = 0`
- `virtual void onAudioDeviceSettingsClosed() = 0`

`EditorView::showAudioDeviceSettingsWindow()` calls `onAudioDeviceSettingsOpened()` before showing
the settings window and calls `onAudioDeviceSettingsClosed()` from the settings window close path.

## Planned Controller State And Helpers

In `EditorController::Impl`, add:

- `std::optional<double> m_input_calibration_gain_db`
- `bool m_input_calibration_prompt_visible{false}`
- `std::optional<common::audio::InputDeviceIdentity>
  m_input_device_identity_before_settings_window`
- `std::optional<common::audio::Gain> m_input_gain_before_calibration_measurement`
- `bool m_live_input_monitoring_before_calibration_measurement{false}`
- `bool m_input_calibration_measurement_active{false}`
- `bool m_audio_device_settings_window_open{false}`

The controller queries `IAudioDeviceConfiguration::currentInputDeviceIdentity()` to get the
current identity and compares it against the stored identity to decide whether calibration should
be cleared.

Add helper methods:

- `[[nodiscard]] bool activeInputDeviceAvailable() const`
- `[[nodiscard]] bool inputCalibrationAvailable() const`
- `[[nodiscard]] bool inputCalibrationRequired() const`
- `void loadInputCalibrationFromSettings()`
- `void persistInputCalibration()`
- `void clearInputCalibration()`
- `void applyInputCalibrationGate()`
- `void ensureInputCalibrationPrompt()`
- `void closeInputCalibrationPrompt()`
- `[[nodiscard]] bool inputDeviceIdentityChanged() const`
- `void handleCommittedAudioDeviceStateChanged()`

Update existing methods:

- `EditorController::Impl::Impl()`
  - accept `ILiveInput*` as a new optional port;
  - load calibration from settings;
  - restore audio device state;
  - apply the calibration gate after device restore.
- `EditorController::Impl::attachView()`
  - call `ensureInputCalibrationPrompt()` after the first state push if calibration is required.
- `EditorController::Impl::onAudioDeviceConfigurationChanged()`
  - persist audio-device state;
  - avoid clearing calibration for temporary settings-window closure;
  - compare current input device identity against stored identity to decide whether to clear
    calibration;
  - apply the calibration gate;
  - call `ensureInputCalibrationPrompt()`.
- `EditorController::Impl::onAudioDeviceChangeRequested()`
  - keep the busy overlay behavior;
  - after the supplied device operation finishes, compare the committed input device identity with
    `m_input_device_identity_before_settings_window`;
  - clear calibration only when the input device identity differs.
- `EditorController::Impl::deriveViewState()`
  - disable play/pause when `inputCalibrationRequired()`;
  - derive `SignalChainViewState::input_calibration_status`;
  - set `audio_device_settings_enabled` to false when `m_input_calibration_prompt_visible`;
  - keep output gain view state unchanged.
- `EditorController::Impl::onOutputGainChanged()`
  - unchanged except for renamed output-only enabled state.
- `EditorController::Impl::onInputCalibrationRequested()`
  - show the calibration prompt when an active input device exists;
  - if no active input exists, keep the prompt closed and rely on audio settings.
- `EditorController::Impl::onInputCalibrationMeasurementStarted()`
  - store the current input gain and monitoring state;
  - call `m_live_input->setLiveInputMonitoringEnabled(false)`;
  - call `m_live_input->setInputGain(common::audio::Gain{0.0})`;
  - mark measurement active so dismissal can restore correctly.
- `EditorController::Impl::onInputCalibrationSucceeded(double gain_db)`
  - clamp and store the calibration gain;
  - persist it in `EditorSettings`;
  - call `m_live_input->setInputGain(...)`;
  - call `m_live_input->setLiveInputMonitoringEnabled(true)`;
  - close the prompt and update the view.
- `EditorController::Impl::onInputCalibrationDismissed()`
  - restore the previous gain and monitoring state when a manual recalibration measurement was
    dismissed;
  - close the prompt but leave live input monitoring disabled while calibration is required.

## Planned UI Changes

### `SignalChainPanel`

Change listener API:

- remove `SignalChainPanel::Listener::onInputGainChanged(double gain_db)`
- add `SignalChainPanel::Listener::onInputCalibrationPressed()`

Change members:

- remove `juce::Slider m_input_gain_slider`
- keep `AudioLevelMeter m_input_meter`
- add `juce::TextButton m_input_calibrate_button`
- keep `juce::Slider m_output_gain_slider`
- keep `AudioLevelMeter m_output_meter`

Change component IDs:

- remove `input_gain_slider`
- rename `input_gain_meter` to `input_meter`
- add `input_calibrate_button`
- keep `output_gain_slider`
- keep `output_gain_meter`

Layout:

- left rail width can remain close to the current gain-control width;
- place the input meter vertically in the rail;
- place `Calibrate` under the meter;
- do not change the output rail layout.

### `InputCalibrationWindow`

Add a focused modal UI class under `rock-hero-editor/ui/src`:

- `input_calibration_window.h`
- `input_calibration_window.cpp`

Planned class:

- `class InputCalibrationWindow final`

Responsibilities:

- call `IEditorController::onInputCalibrationMeasurementStarted()` when measurement starts so the
  controller can disable processed monitoring and reset input gain to `0.0 dB`;
- sample `common::audio::ILiveInput::rawInputMeterLevel()`;
- run a short measurement timer;
- display retry/failure states;
- call `IEditorController::onInputCalibrationSucceeded(gain.db)` when calculation succeeds;
- call `IEditorController::onInputCalibrationDismissed()` when closed without success.

The class may use `common::audio::InputCalibrationAccumulator`. It receives `ILiveInput` only for
raw meter reads; it should not persist settings, set gain, or toggle monitoring directly.

## Tone Document And Live Rig Struct Changes

Input gain is no longer part of the authored tone. The structural input gain plugin in the engine
stays in the Tracktion chain, but the live rig interface and tone serialization stop owning it.

### Serialization

New tone documents should stop writing `inputGainDb`.

Keep:

- `outputGainDb`

For backward compatibility:

- reading old `inputGainDb` should silently ignore the value;
- missing or old input gain fields must not create a calibrated app state.

This prevents a project from importing another user's input calibration.

### Internal structs in `engine.cpp`

- `ToneDocument`: remove `input_gain` field.
- `LiveRigLoadOperation`: remove `input_gain` field.
- `captureActiveRig()`: stop reading the structural input gain plugin into the snapshot.
- `loadLiveRig()`: stop applying a tone document's input gain to the structural plugin. The
  structural input gain plugin is managed exclusively through `ILiveInput` at the application
  level.

### Public live rig types in `i_live_rig.h`

- `LiveRigSnapshot`: remove `input_gain` field.
- `LiveRigLoadResult`: remove `input_gain` field.

Output gain remains in both structs and in tone documents because it is part of the authored tone
balance.

## Covered Cases

### Startup And Restore

1. No audio-device backend is available.
   - No calibration prompt.
   - Live rig monitoring remains disabled or unavailable.
   - Audio settings button remains unavailable as it does today.

2. Audio-device backend exists but no active input device is open.
   - No calibration prompt yet.
   - Play/pause remains unavailable if there is no usable audio route.
   - Signal-chain input status is `NoActiveInputDevice`.

3. Audio device restores successfully and calibration is unset.
   - Calibration prompt opens after restore/view attachment.
   - Play/pause is disabled.
   - Live input monitoring is disabled.

4. Audio device restores successfully and calibration is set.
   - Calibration prompt does not open.
   - Stored gain is clamped and applied through the live input port.
   - Live input monitoring is enabled.
   - Play/pause availability follows the normal project/transport gates.

5. Stored calibration value is missing, invalid, non-finite, or out of range.
   - Missing, invalid, or non-finite values are treated as unset.
   - Out-of-range values are clamped before use.
   - If active input exists and the value is unset, calibration is required.

### Audio Settings

6. User opens audio settings and cancels with the previous input device identity restored.
   - Calibration is preserved.
   - Prompt does not reopen unless calibration was already missing.

7. User opens audio settings and applies the same input device identity.
   - Calibration is preserved.
   - Prompt does not reopen unless calibration was already missing.

8. User applies a different input device.
   - Calibration is cleared.
   - Live input monitoring is disabled.
   - Calibration prompt opens after apply succeeds.

9. User applies a different selected input channel.
   - Calibration is cleared.
   - Live input monitoring is disabled.
   - Calibration prompt opens after apply succeeds.

10. User changes sample rate, buffer size, output device, or other non-input device fields.
    - Calibration is preserved because the input device identity is unchanged.
    - Peak dBFS measurement is independent of sample rate and buffer size.

11. Audio settings apply fails.
    - Do not save a new calibration.
    - Do not enable live input because of a failed apply.
    - Preserve the previous calibration only if the previous input device identity is restored.

12. Audio settings window closes natively and restores the previous route.
    - Treat it like cancel.
    - Preserve calibration when the committed input device identity matches the pre-window
      identity.

13. Audio settings window temporarily closes the audio device while editing.
    - Do not clear calibration solely because of that temporary closure.
    - Only the committed final input device identity decides whether calibration is cleared.

### Calibration Popup

14. Required popup is already visible and state is re-derived.
    - Do not open another popup.

15. User dismisses a required popup.
    - Popup closes.
    - Calibration remains unset.
    - Play/pause remains disabled.
    - Live input monitoring remains disabled.
    - Signal-chain panel still exposes `Calibrate`.
    - Audio device settings become available again.

16. User starts calibration with no usable input signal.
    - No calibration is stored.
    - Live input monitoring remains disabled.
    - User can retry.

17. User starts calibration and raw input clips.
    - No calibration is stored.
    - Live input monitoring remains disabled.
    - User is told to lower hardware/interface gain and retry.

18. User starts calibration and measurement succeeds.
    - Gain is computed, clamped, stored in `EditorSettings`, and applied through the live input
      port.
    - Live input monitoring is enabled.
    - Prompt closes.
    - Play/pause can become enabled if all normal transport gates pass.

19. User clicks `Calibrate` while already calibrated.
    - Popup opens for manual recalibration.
    - Audio device settings become unavailable.
    - Live processed monitoring is disabled when measurement starts.
    - Input gain is reset to `0.0 dB` when measurement starts so the raw input meter reads raw
      hardware level.
    - If the user dismisses without success, the previous calibration gain is restored and live
      input monitoring returns to its previous state.
    - If calibration succeeds, the new gain replaces the old one.

20. User clicks `Calibrate` with no active input device.
    - Do not show the calibration popup.
    - Keep live input disabled.
    - The audio-device button remains the path to choose a device.

21. Audio device settings button pressed while calibration window is visible.
    - Action is unavailable. The calibration window must be dismissed first.

22. External device disconnection while calibration window is visible.
    - `onAudioDeviceConfigurationChanged()` fires through the normal listener path.
    - Controller clears calibration and measurement state without restoring previous gain.
    - Calibration window closes because the controller removes the prompt from the view state.
    - If the device reconnects, the calibration prompt reopens through the normal rule.

### Transport And Live Rig

23. Play/pause is pressed while calibration is required.
    - Action is unavailable and does not start transport.
    - Audio backend monitoring gate remains disabled.

24. A project is opened while calibration is required.
    - Project can load.
    - Live rig can restore plugins and output gain.
    - Play/pause remains disabled until calibration succeeds.
    - Live input monitoring remains disabled.

25. Tone document load restores `outputGainDb`.
    - Output gain slider and meter behave as they do now.
    - Output gain remains part of project dirty state.

26. Tone document load contains old `inputGainDb`.
    - The field is silently ignored by the engine.
    - `LiveRigLoadResult` does not carry `input_gain`.
    - It does not mark the user calibrated.
    - It does not overwrite app-local calibration.
    - It does not enable live input monitoring.

27. Saving or publishing a project after calibration.
    - Does not write input calibration gain to project or song package data.
    - Does still write output gain to the tone document.

28. Output gain changes while calibrated.
    - Behavior remains unchanged.
    - The project becomes dirty when output gain changes.

29. Output gain changes while calibration is required.
    - Allow the authored output value to change only if the live rig exists and the arrangement is
      loaded.
    - Do not enable play/pause or live input monitoring.

30. Plugin add, remove, or open while calibration is required.
    - Plugin editing may remain available.
    - Plugin audition through live input remains unavailable.
    - Play/pause remains disabled.

31. User changes audio device while transport is playing.
    - The existing UI already pauses before opening audio settings.
    - After apply, calibration is cleared if the input device identity changed.
    - Transport cannot restart until calibration succeeds.

32. Live rig backend is missing in a test or reduced composition.
    - Calibration state still derives from app settings and audio-device state.
    - No live input can be enabled because no live rig exists.
    - Play/pause must not become enabled solely because the live rig is missing.

## Testing Strategy

### `rock-hero-common/audio/tests/test_input_calibration.cpp`

- accumulator starts silent;
- accumulator keeps the loudest peak;
- clipping sample causes `InputClipped`;
- silent/too-low sample causes `NoUsableSignal`;
- valid peak returns a clamped gain toward `inputCalibrationTargetPeakDb()`.

### `rock-hero-editor/core/tests/test_editor_settings.cpp`

- new settings start without input calibration;
- calibration gain persists across reload;
- clearing calibration removes it;
- invalid stored values are treated as missing;
- out-of-range stored values clamp on read.

### `rock-hero-editor/core/tests/test_editor_controller.cpp`

- active input plus missing calibration opens prompt;
- active input plus missing calibration disables play/pause;
- active input plus stored calibration applies gain via live input port and enables monitoring;
- calibration success stores gain and enables monitoring;
- calibration dismissal keeps play/pause disabled;
- manual recalibration dismissal preserves the previous calibration;
- changed input device identity clears calibration;
- different selected input channel clears calibration even when input channel count is unchanged;
- same input device identity with changed buffer size or sample rate preserves calibration;
- same input device identity with changed output device preserves calibration;
- failed audio-device apply does not create calibration;
- manual recalibration measurement disables processed live input and restores it on dismissal;
- audio device settings are unavailable while calibration prompt is visible;
- external device disconnection during calibration clears state without restoring;
- live rig load result does not carry input gain and does not mark calibrated;
- output gain restore and dirty behavior remains unchanged.

### `rock-hero-editor/ui/tests/test_editor_view.cpp`

- input gain slider no longer exists;
- input meter still exists and remains vertical;
- input meter component ID is `input_meter`;
- `Calibrate` button exists in the input rail;
- output gain slider and output meter still exist;
- clicking `Calibrate` emits `onInputCalibrationRequested()`;
- required prompt state opens the calibration window;
- meter sampling uses `ILiveInput::rawInputMeterLevel()`.

### `rock-hero-common/audio/tests/test_engine.cpp`

- `ILiveInput::setLiveInputMonitoringEnabled(false)` mutes live processed output without breaking
  raw input metering;
- `ILiveInput::setInputGain()` applies the calibration gain stage;
- `ILiveInput::rawInputMeterLevel()` reports input level without requiring live rig monitoring;
- `ILiveInput` live input monitoring defaults disabled when requested by controller;
- `ILiveRig::captureActiveRig()` writes `outputGainDb` but not `inputGainDb`;
- `ILiveRig::loadLiveRig()` result does not carry `input_gain`;
- loading old `inputGainDb` from a tone document is silently ignored;
- `IAudioDeviceConfiguration::currentInputDeviceIdentity()` returns the current identity;
- `currentInputDeviceIdentity()` returns `std::nullopt` when no input is open.

## Acceptance Criteria

- No input gain slider remains in the signal-chain panel.
- The input rail shows a vertical meter and a calibration button.
- Input meter component ID is `input_meter`.
- Output gain slider and output meter are visually and behaviorally unchanged.
- Input calibration gain is stored only in editor app-local settings.
- New tone documents do not write `inputGainDb`.
- Output gain continues to persist in tone documents.
- Opening or applying settings that preserve the input device identity preserves calibration.
- Changing buffer size, sample rate, or output device does not clear calibration.
- Changing the input device type, device name, or selected input channel identity clears
  calibration.
- Active input with missing calibration shows a required calibration prompt.
- Audio device settings are unavailable while the calibration window is visible.
- Transport playback and live processed guitar monitoring cannot start while calibration is
  required.
- Dismissing calibration does not enable live input.
- Successful calibration applies gain, saves the value, enables live input monitoring, and restores
  normal play/pause availability.

## Non-Goals

- No per-device calibration records.
- No guitar/instrument profiles.
- No runtime hardware-gain drift detection.
- No RMS/LUFS calibration pass in this pass.
- No master output volume work.
- No output gain UI redesign.
- No broad `EditorController` decomposition as part of this feature. The calibration helpers are
  grouped as a logical unit inside `EditorController::Impl` for later extraction into
  `InputCalibrationController` per the editor-controller-decomposition plan.
