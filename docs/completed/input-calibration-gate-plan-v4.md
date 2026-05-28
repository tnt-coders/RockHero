# Required Input Calibration Gate Plan V4

Status: completed. This plan replaced the older V3 calibration gate plan. The implemented workflow
persists calibration with the exact input identity, makes calibration setup fallible, keeps
backing-track transport available after dismissal, and visibly disables live guitar audition until
the current input route is calibrated.

## Goal

Replace the current user-adjustable input gain slider with a required input calibration workflow.
Input calibration is per-user application state, not project state and not authored tone-chain
state.

Live processed guitar output must stay unavailable until the currently active input route has a
matching successful calibration. While the calibration window is visible, transport playback is
paused or stopped and play/pause is disabled so the measurement runs in a quiet environment. After
the user dismisses or completes calibration, backing-track transport becomes available even if
calibration is missing; only the live guitar path and signal-chain audition remain gated.

Output gain remains unchanged: it stays in the signal chain, keeps its vertical slider and meter,
and continues to persist with the tone document because it is part of the authored tone balance.

## V4 Review-Finding Resolutions

- Stale gain bypass: persist gain with exact input identity in `InputCalibrationState`.
- Inexact channel identity: identity includes the input device name and physical channel index.
- Fallible live-input setup: measurement start and success return `std::expected`.
- Transport ambiguity: transport is disabled during the calibration window; after dismissal,
  backing-track playback is available even without calibration; live guitar output stays gated.
- Settings bypass: both view state and controller logic block settings while the popup is visible.
- Prompt dismissal: one in-memory dismissal flag suppresses auto-reopening until the input route
  changes, calibration succeeds, or manual calibration is requested.
- Startup restore failure: failed stored calibration reads or failed stored gain application behave
  as missing calibration.

## Design Rules

- Store one optional app-local `InputCalibrationState` for this pass.
- `InputCalibrationState` contains both the calibration gain and the exact input route identity.
- A gain without a matching input route identity is never enough to enable live guitar output.
- Do not add per-device calibration records yet. This is one active calibration record, not a map.
- Do not add instrument or guitar profiles yet.
- Do not continuously detect hardware gain changes during playback yet.
- Clear calibration when the committed active input route identity changes.
- Preserve calibration only when the committed input route identity matches the stored identity.
- Preserve calibration when buffer size, sample rate, output device, or other non-input fields
  change and the input route identity is unchanged.
- Preserve calibration when the audio-device settings window is opened and then closed or applied
  with the exact same input route restored.
- Disable live processed guitar monitoring until calibration succeeds for the current route.
- Keep raw input measurement available for the calibration popup.
- Do not use attenuation as a mute. Use an explicit monitoring gate.
- Do not write input calibration state to project files, `.rock` packages, or tone documents.
- Keep output gain persistence and UI behavior unchanged.
- Opening the calibration window pauses or stops active transport before the prompt becomes visible.
- Transport playback is disabled while the calibration window is visible.
- After the calibration window is dismissed, transport follows normal project/output-route gates
  even if calibration is missing.
- Dismissing the calibration window does not automatically resume transport.
- Audio device settings are unavailable while the calibration window is visible. The user must
  dismiss or complete calibration before changing audio device settings.
- The controller must also reject audio-device settings requests while the calibration window is
  visible, so stale callbacks cannot bypass the view-state gate.
- The auto calibration prompt can be dismissed for the current committed input route. Store this as
  one in-memory `m_input_calibration_prompt_dismissed` flag, and clear it when the committed input
  route changes, calibration succeeds, or manual calibration is requested.
- If stored calibration cannot be read, has invalid identity/gain data, or cannot be applied during
  startup restore, treat it as unset calibration.

## V4 User Experience

The only automatic prompt rule is:

```text
If an active input route exists, there is no matching input calibration state, and the user has not
dismissed the prompt for the current route, show the calibration popup.
```

The app should call that rule after:

- audio-device state is restored on startup;
- an audio-device settings apply succeeds;
- an audio-device settings cancel/native close restores a previous route;
- the editor view attaches after controller construction;
- manual recalibration is requested from the signal-chain panel;
- an external device disconnection or reconnection notification arrives.

When the calibration window opens, the controller pauses or stops any active transport playback.
While the window is visible, transport playback is disabled so the measurement environment stays
quiet. After the user dismisses or completes calibration, transport follows the normal
project/output-route gates even if calibration is missing. The live guitar path remains muted and
the signal-chain audition area is visibly disabled until calibration succeeds.

The signal-chain panel should use the existing left rail but change its meaning:

```text
+--------------+-------------------------------+--------------+
| Input        | Signal Chain                  | Output       |
|              | disabled message or plugins   |              |
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

When live input cannot be auditioned, the middle signal-chain/plugin area is visibly disabled and
shows a concise message such as:

- `Select an input device to audition tones.`
- `Calibrate input to audition tones.`
- `Input route changed. Recalibrate to audition tones.`
- `Live input unavailable.`

The output rail remains:

- `Output` label;
- existing vertical output gain slider;
- existing vertical output meter.

The output rail is not part of the live-input calibration gate. Output gain is authored tone state,
so its slider and meter remain visually and behaviorally unchanged when the live rig exists.

## Calibration Popup Behavior

The calibration popup is required when an active input route exists and calibration is missing or
does not match that route. It should allow the user to leave the popup, but leaving does not enable
live processed guitar monitoring. The app remains usable for non-live-guitar work, including
backing-track playback when normal transport gates pass.

Opening the calibration window pauses or stops any active transport playback first. While the
window is visible, transport playback and audio device settings changes are both unavailable. This
keeps the measurement environment quiet and prevents device changes from racing with an active
measurement. External device removal can still happen and is handled through the normal
device-change listener path.

The popup should support:

1. `Start` asks the controller to prepare raw measurement.
2. If setup fails, the popup displays the live-input error and does not start measurement.
3. If setup succeeds, the user plays loud, normal hard strums.
4. The popup samples the raw input meter for the measurement window.
5. If the input clips, the popup tells the user to lower the hardware/interface gain and retry.
6. If there is no usable signal, the popup tells the user to check the input and retry.
7. If the signal is usable, the popup asks the controller to apply and store the calibration.
8. If applying live-input gain or monitoring fails, the popup displays the error and calibration is
   not stored.
9. If applying succeeds, the controller stores the calibration state and enables monitoring.

Before starting a measurement, the controller must:

1. capture the current exact `InputDeviceIdentity`;
2. store the current matching `InputCalibrationState`, if one exists;
3. disable live processed monitoring through `ILiveInput`;
4. reset input gain to `0.0 dB` through `ILiveInput`;
5. store the measurement session only after both live-input operations succeed.

If the user was already calibrated and dismisses manual recalibration after measurement started,
the controller restores the previous `InputCalibrationState`. It applies that state's gain first,
then enables live monitoring only if the identity still matches and the gain apply succeeds. If
gain restore or monitoring restore fails, the controller clears the previous calibration state,
leaves monitoring disabled, and reports the live-input error.

If calibration succeeds, the new gain and current input identity replace the old state and
processed monitoring is enabled. This makes recalibration identical to fresh calibration without
temporarily monitoring an uncalibrated tone chain.

V4 still uses a simple peak-based target:

```text
input_calibration_gain_db = target_peak_dbfs - measured_peak_dbfs
```

Use `common::audio::clampGain()` before applying or storing the gain. Start with a conservative
target such as `-12.0 dBFS` for hard-strum peaks. RMS or LUFS-style calibration can replace this
later if real use proves peak-only calibration is too rough.

## Runtime Gate

There must be two barriers against uncalibrated live processed guitar output:

1. Controller/UI gate:
   - `play_pause_enabled` is false while the calibration window is visible;
   - after the calibration window is dismissed, `play_pause_enabled` follows the normal
     transport/project/output-route gates even if calibration is missing;
   - signal-chain live input status shows the blocking reason;
   - plugin-chain audition and live guitar output controls are disabled while calibration is
     missing, mismatched, or unavailable;
   - the signal-chain/plugin area shows a visible disabled message while live guitar output is
     blocked.

2. Audio backend gate:
   - live input monitoring is explicitly disabled through the live input port while calibration is
     missing, mismatched, or unavailable;
   - calibration measurement reads raw input level through the live input port's raw meter, which
     remains active when processed monitoring is disabled;
   - the gate is not represented by setting gain to `-24.0 dB`;
   - transport start, live rig load, and tone document load must not implicitly enable live input
     monitoring.

The only paths that may call `ILiveInput::setLiveInputMonitoringEnabled(true)` are successful input
calibration, startup restore of a matching `InputCalibrationState`, and manual recalibration
dismissal that restores a still-matching previous calibration. Each path must first apply the
relevant calibration gain successfully.

## Planned Public/Core Types

### `rock-hero-common/audio/include/.../input_device_identity.h`

Add a project-owned value struct in `common/audio` that captures the input route fields relevant to
calibration validity. Both editor and game will compare this struct to decide whether a stored
calibration is still valid for the current hardware.

Planned type:

- `struct InputDeviceIdentity`
  - `std::string backend_name`
  - `std::string input_device_name`
  - `int input_channel_index{-1}`
  - `std::string input_channel_name`
  - `friend bool operator==(const InputDeviceIdentity&, const InputDeviceIdentity&) = default`

Identity rules:

- `backend_name` is the JUCE device type, such as `ASIO` or `Windows Audio`.
- `input_device_name` comes from `juce::AudioDeviceManager::AudioDeviceSetup::inputDeviceName`,
  not from `juce::AudioIODevice::getName()`. This matters for separate input/output backends.
- `input_channel_index` is the zero-based physical input channel bit selected for the route.
- `input_channel_name` is stored for diagnostics and display, but it is not the sole identity.
- The active input channel mask must contain exactly one bit. Otherwise there is no valid identity.
- A default-constructed value represents no input route and must not be persisted as calibrated.

Planned helper:

- `[[nodiscard]] bool isValidInputDeviceIdentity(const InputDeviceIdentity& identity)`

The helper returns true only when `backend_name` and `input_device_name` are non-empty and
`input_channel_index >= 0`.

### `rock-hero-common/audio/include/.../input_calibration_state.h`

Add a shared value type for the persisted calibration record:

- `struct InputCalibrationState`
  - `common::audio::Gain calibration_gain`
  - `common::audio::InputDeviceIdentity input_device_identity`
  - `friend bool operator==(const InputCalibrationState&, const InputCalibrationState&) = default`

Planned helper:

- `[[nodiscard]] bool inputCalibrationMatchesIdentity(
  const InputCalibrationState& state,
  const InputDeviceIdentity& identity)`

This helper returns true only when the state contains a valid identity and that identity exactly
matches the current input route identity. This is still not a per-device calibration record; it is
the single active app-local calibration plus the route it belongs to.

### `rock-hero-common/audio/include/.../input_calibration.h`

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

### `rock-hero-common/audio/include/.../i_audio_device_configuration.h`

Add a method to query the current exact input route identity:

- `[[nodiscard]] virtual std::optional<InputDeviceIdentity> currentInputDeviceIdentity() const = 0`

Returns `std::nullopt` when:

- no audio device is open;
- the current device is not open;
- no input device is selected;
- no input channel is selected;
- more than one input channel is selected;
- the active input route cannot be mapped to one physical channel.

The engine implementation builds the identity from:

- `juce::AudioDeviceManager::getCurrentAudioDeviceType()`;
- `juce::AudioDeviceManager::getAudioDeviceSetup().inputDeviceName`;
- `juce::AudioIODevice::getActiveInputChannels()`;
- `juce::AudioIODevice::getInputChannelNames()`.

The engine must reject multi-input masks instead of choosing the first active bit.

### `rock-hero-editor/core/include/.../editor_settings.h`

Add app-local persistence for the full calibration state:

- `[[nodiscard]] std::optional<common::audio::InputCalibrationState>
  inputCalibrationState() const`
- `void setInputCalibrationState(
  std::optional<common::audio::InputCalibrationState> calibration_state)`

Implementation keys:

- `constexpr const char* g_input_calibration_gain_db_key{"inputCalibrationGainDb"}`
- `constexpr const char* g_input_calibration_backend_name_key{"inputCalibrationBackendName"}`
- `constexpr const char* g_input_calibration_input_device_name_key{
  "inputCalibrationInputDeviceName"}`
- `constexpr const char* g_input_calibration_input_channel_index_key{
  "inputCalibrationInputChannelIndex"}`
- `constexpr const char* g_input_calibration_input_channel_name_key{
  "inputCalibrationInputChannelName"}`

Read behavior:

- missing state returns `std::nullopt`;
- missing identity fields return `std::nullopt`;
- a gain-only stored value returns `std::nullopt` and may be cleared;
- invalid or non-finite gain values return `std::nullopt`;
- out-of-range gain values are clamped through `common::audio::clampGain()`;
- invalid input identity returns `std::nullopt`.

The earlier gain-only API names are not implemented. If a gain-only key appears from local testing,
it must not enable live input because it has no stored input identity.

### `rock-hero-common/audio/include/.../i_live_rig.h`

Input gain is an application-level calibration concern, not part of the authored tone. Remove input
gain ownership from the live rig so it is purely about the tone chain.

Planned removals:

- remove `inputGain() const` - moves to `ILiveInput`
- remove `setInputGain(Gain gain)` - moves to `ILiveInput`
- remove `input_gain` from `LiveRigSnapshot` - tone captures no longer include input gain
- remove `input_gain` from `LiveRigLoadResult` - tone loads no longer return input gain

Keep:

- `outputGain() const`
- `setOutputGain(Gain gain)`
- `output_gain` in `LiveRigSnapshot` and `LiveRigLoadResult`

### `rock-hero-common/audio/include/.../i_live_input.h`

Add a small interface in `common/audio` for application-level input stage control. Both editor and
game will need this for calibration and monitoring gating. The engine implements it alongside its
other ports; it is not part of the tone rig.

Planned methods:

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

Replace input gain state with calibration and live-input availability state while leaving output
gain fields intact.

Planned additions:

- `enum class InputCalibrationStatus`
  - `NoActiveInputDevice`
  - `Required`
  - `RouteMismatch`
  - `Calibrated`
  - `LiveInputUnavailable`
- `InputCalibrationStatus input_calibration_status{InputCalibrationStatus::NoActiveInputDevice}`
- `bool input_calibration_button_enabled{false}`
- `bool plugin_chain_controls_enabled{false}`
- `std::string signal_chain_disabled_message`
- `bool output_gain_controls_enabled{false}`

Planned removals or renames:

- remove `input_gain_db`;
- replace `gain_controls_enabled` with `output_gain_controls_enabled`;
- keep `output_gain_db`.

The plugin-chain controls are disabled when live guitar audition is unavailable. The output gain
controls remain governed by the existing live-rig/arrangement availability rules.

### `rock-hero-editor/core/include/.../editor_view_state.h`

Add prompt state and audio-settings gate:

- `struct InputCalibrationPrompt`
  - `bool required{true}`
  - `std::string message`
- `std::optional<InputCalibrationPrompt> input_calibration_prompt`
- `bool audio_device_settings_enabled{true}`

The prompt is state, not a one-shot effect, because the controller must be able to keep it visible
until the user succeeds, closes it, or changes audio settings.

Disable audio device settings while the calibration window is visible:

- `audio_device_settings_enabled` is false when `input_calibration_prompt` is set.

The controller also guards audio-device settings intents directly.

### `rock-hero-editor/core/include/.../i_editor_controller.h`

Replace the input-gain slider intent with calibration intents:

- remove `virtual void onInputGainChanged(double gain_db) = 0`
- add `virtual void onInputCalibrationRequested() = 0`
- add `[[nodiscard]] virtual std::expected<void, common::audio::LiveInputError>
  onInputCalibrationMeasurementStarted() = 0`
- add `[[nodiscard]] virtual std::expected<void, common::audio::LiveInputError>
  onInputCalibrationSucceeded(double gain_db) = 0`
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

- `std::optional<common::audio::InputCalibrationState> m_input_calibration_state`
- `std::optional<common::audio::InputDeviceIdentity> m_committed_input_device_identity`
- `bool m_input_calibration_prompt_visible{false}`
- `bool m_input_calibration_prompt_dismissed{false}`
- `bool m_audio_device_settings_window_open{false}`
- `std::optional<InputCalibrationMeasurementSession> m_input_calibration_measurement`

Add one private controller-only session struct:

- `struct InputCalibrationMeasurementSession`
  - `common::audio::InputDeviceIdentity input_device_identity`
  - `std::optional<common::audio::InputCalibrationState> previous_calibration_state`

The controller queries `IAudioDeviceConfiguration::currentInputDeviceIdentity()` to get the
current identity. `m_committed_input_device_identity` is the last non-temporary route the
controller accepted as real. It exists only to distinguish a real route change from the temporary
device closure caused by opening the audio settings window.

Do not add separate variables for previous input gain, previous monitoring state, or measurement
active state. `m_input_calibration_measurement.has_value()` is the active-measurement flag, and
`previous_calibration_state` is the only value manual recalibration dismissal can restore.

Add helper methods:

- `[[nodiscard]] std::optional<common::audio::InputDeviceIdentity>
  currentInputDeviceIdentity() const`
- `[[nodiscard]] bool activeInputDeviceAvailable() const`
- `[[nodiscard]] bool inputCalibrationMatchesCurrentInput() const`
- `[[nodiscard]] bool inputCalibrationPromptRequired() const`
- `[[nodiscard]] bool liveInputAuditionAllowed() const`
- `[[nodiscard]] std::string signalChainDisabledMessage() const`
- `void loadInputCalibrationFromSettings()`
- `void persistInputCalibration()`
- `void clearInputCalibration()`
- `[[nodiscard]] std::expected<void, common::audio::LiveInputError> applyLiveInputGate()`
- `void ensureInputCalibrationPrompt()`
- `void closeInputCalibrationPrompt()`
- `void syncCommittedInputDeviceIdentity()`
- `[[nodiscard]] std::expected<void, common::audio::LiveInputError>
  restoreCalibrationMeasurementState()` - on failure, clears calibration state and requires
  recalibration rather than leaving monitoring at the wrong gain

`inputCalibrationPromptRequired()` returns false after the user dismisses the prompt for the
current route. This suppression is intentionally a single in-memory boolean, not a per-device
record. The boolean is cleared whenever the committed input route changes, calibration succeeds, or
manual calibration is requested.

`syncCommittedInputDeviceIdentity()` is the only place that decides whether the input route changed.
It compares `currentInputDeviceIdentity()` with `m_committed_input_device_identity`. If they differ,
it updates the committed identity, clears prompt dismissal, clears calibration, disables live input
monitoring, and lets the normal prompt rule run. While `m_audio_device_settings_window_open` is
true, a missing current identity is treated as temporary and does not update the committed identity.

`applyLiveInputGate()` stays simple:

1. If the current input identity is missing, disable monitoring and stop.
2. If calibration is missing for the current input route, disable monitoring.
3. If calibration exists but does not match the current input route, clear it and disable
   monitoring.
4. If calibration matches, apply its gain.
5. If gain apply succeeds, enable monitoring.
6. If either restore step fails, clear calibration and leave monitoring disabled.

`ensureInputCalibrationPrompt()` returns without opening anything while
`m_audio_device_settings_window_open` is true. Otherwise it pauses or stops active transport before
setting `m_input_calibration_prompt_visible` to true.

Update existing methods:

- `EditorController::Impl::Impl()`
  - accept `ILiveInput*` as a new optional port;
  - load calibration state from settings;
  - restore audio device state;
  - initialize `m_committed_input_device_identity` from `currentInputDeviceIdentity()` after
    restore;
  - apply the live-input gate after device restore;
  - treat stored calibration read or restore failures as missing calibration.
- `EditorController::Impl::attachView()`
  - call `ensureInputCalibrationPrompt()` after the first state push if calibration is required.
- `EditorController::Impl::onAudioDeviceSettingsOpened()`
  - return without changing state when the calibration prompt is visible;
  - set `m_audio_device_settings_window_open` to true.
- `EditorController::Impl::onAudioDeviceSettingsClosed()`
  - set `m_audio_device_settings_window_open` to false;
  - call `syncCommittedInputDeviceIdentity()`;
  - apply the live-input gate;
  - call `ensureInputCalibrationPrompt()`.
- `EditorController::Impl::onAudioDeviceConfigurationChanged()`
  - persist audio-device state;
  - call `syncCommittedInputDeviceIdentity()`;
  - apply the live-input gate;
  - call `ensureInputCalibrationPrompt()`, which does not open a prompt while the audio settings
    window is still open.
- `EditorController::Impl::onAudioDeviceChangeRequested()`
  - return immediately without running `change_audio_device` when the calibration prompt is
    visible;
  - keep the existing busy overlay behavior otherwise;
  - run the supplied device operation;
  - call `syncCommittedInputDeviceIdentity()` after the operation finishes.
- `EditorController::Impl::deriveViewState()`
  - disable `play_pause_enabled` while `m_input_calibration_prompt_visible`;
  - after dismissal, leave `play_pause_enabled` governed by existing project/transport/output-route
    gates;
  - derive `SignalChainViewState::input_calibration_status`;
  - set `plugin_chain_controls_enabled` from `liveInputAuditionAllowed()`;
  - set `signal_chain_disabled_message` when live input audition is blocked;
  - set `audio_device_settings_enabled` to false when `m_input_calibration_prompt_visible`;
  - keep output gain view state unchanged.
- `EditorController::Impl::onOutputGainChanged()`
  - unchanged except for renamed output-only enabled state.
- `EditorController::Impl::onInputCalibrationRequested()`
  - clear `m_input_calibration_prompt_dismissed`;
  - pause or stop active transport before the prompt becomes visible;
  - show the calibration prompt when an active input route exists;
  - if no active input exists, keep the prompt closed and rely on audio settings.
- `EditorController::Impl::onInputCalibrationMeasurementStarted()`
  - fail if `m_live_input` is null;
  - fail if `currentInputDeviceIdentity()` is `std::nullopt`;
  - build `InputCalibrationMeasurementSession` with the current identity and the current matching
    calibration state, if one exists;
  - call `m_live_input->setLiveInputMonitoringEnabled(false)`;
  - call `m_live_input->setInputGain(common::audio::Gain{0.0})`;
  - store the measurement session only after both calls succeed.
- `EditorController::Impl::onInputCalibrationSucceeded(double gain_db)`
  - fail if `m_input_calibration_measurement` is unset;
  - fail if current input identity is missing or differs from the measurement session identity;
  - clamp the gain;
  - call `m_live_input->setInputGain(...)`;
  - call `m_live_input->setLiveInputMonitoringEnabled(true)`;
  - store `InputCalibrationState{gain, current_identity}` only after both calls succeed;
  - persist the state in `EditorSettings`;
  - clear `m_input_calibration_measurement`;
  - clear `m_input_calibration_prompt_dismissed`;
  - close the prompt and update the view.
- `EditorController::Impl::onInputCalibrationDismissed()`
  - set `m_input_calibration_prompt_dismissed` when required calibration is dismissed without
    success;
  - if `m_input_calibration_measurement` has a `previous_calibration_state` that still matches the
    current identity, restore that calibration gain and then enable monitoring;
  - if gain or monitoring restore fails, clear the previous calibration state and require
    recalibration;
  - clear `m_input_calibration_measurement`;
  - close the prompt but leave live input monitoring disabled when calibration is missing.

## Planned UI Changes

### `SignalChainPanel`

Change listener API:

- remove `SignalChainPanel::Listener::onInputGainChanged(double gain_db)`
- add `SignalChainPanel::Listener::onInputCalibrationPressed()`

Change members:

- remove `juce::Slider m_input_gain_slider`
- keep `AudioLevelMeter m_input_meter`
- add `juce::TextButton m_input_calibrate_button`
- add `juce::Label m_signal_chain_disabled_label`
- keep `juce::Slider m_output_gain_slider`
- keep `AudioLevelMeter m_output_meter`

Change component IDs:

- remove `input_gain_slider`
- rename `input_gain_meter` to `input_meter`
- add `input_calibrate_button`
- add `signal_chain_disabled_message`
- keep `output_gain_slider`
- keep `output_gain_meter`

Layout:

- left rail width can remain close to the current gain-control width;
- place the input meter vertically in the rail;
- place `Calibrate` under the meter;
- render the disabled message over or in place of the plugin-chain area when plugin-chain controls
  are disabled;
- visually dim plugin rows and plugin controls while disabled;
- do not change the output rail layout.

### `InputCalibrationWindow`

Add a focused modal UI class under `rock-hero-editor/ui/src`:

- `input_calibration_window.h`
- `input_calibration_window.cpp`

Planned class:

- `class InputCalibrationWindow final`

Responsibilities:

- call `IEditorController::onInputCalibrationMeasurementStarted()` when measurement starts;
- show the returned `LiveInputError::message` and do not start sampling if setup fails;
- sample `common::audio::ILiveInput::rawInputMeterLevel()`;
- run a short measurement timer;
- display retry/failure states for calibration math errors;
- call `IEditorController::onInputCalibrationSucceeded(gain.db)` when calculation succeeds;
- show the returned `LiveInputError::message` and keep the prompt open if applying fails;
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
   - Live guitar monitoring remains disabled or unavailable.
   - Audio settings button remains unavailable as it does today.
   - Backing playback follows existing audio-route availability and will likely be unavailable.

2. Audio-device backend exists but no active input route is open.
   - No calibration prompt yet.
   - Live guitar monitoring remains disabled.
   - Backing playback can be available if output route and project state allow it.
   - Signal-chain status is `NoActiveInputDevice`.
   - Signal-chain/plugin area is disabled with a select-input-device message.

3. Audio input route restores successfully and calibration state is unset.
   - Calibration prompt opens after restore/view attachment.
   - If transport is active, opening the prompt pauses or stops it.
   - Transport is disabled while the calibration window is visible.
   - Live guitar monitoring is disabled.
   - Signal-chain/plugin area is disabled with a calibrate-input message.
   - If the user dismisses the prompt, transport becomes available but signal chain stays disabled.

4. Audio input route restores successfully and calibration state matches it.
   - Calibration prompt does not open.
   - Stored gain is clamped and applied through the live input port.
   - Live input monitoring is enabled only if applying gain succeeds.
   - If applying the stored gain or enabling monitoring fails, treat calibration as unset: clear
     the stored calibration state, keep live input monitoring disabled, and open the calibration
     prompt if an active input route exists.
   - When applying succeeds, signal-chain/plugin area is enabled.
   - Backing playback availability follows the normal project/transport gates after any prompt is
     dismissed or completed.

5. Audio input route restores successfully and stored calibration identity differs.
   - Stored calibration state is cleared.
   - Calibration prompt opens after restore/view attachment.
   - If transport is active, opening the prompt pauses or stops it.
   - Transport is disabled while the calibration window is visible.
   - Live guitar monitoring remains disabled.
   - Signal-chain/plugin area is disabled with a route-changed message.
   - If the user dismisses the prompt, transport becomes available but signal chain stays disabled.

6. Stored calibration value is missing, invalid, non-finite, or out of range.
   - Missing, unreadable, invalid, or non-finite values are treated as unset.
   - Out-of-range values are clamped before use.
   - If active input exists and the state is unset, calibration is required.

7. Stored calibration has gain but no stored input identity.
   - Treat it as unset.
   - Do not apply the gain.
   - Do not enable live input monitoring.
   - Clear the incomplete settings state.

8. Stored calibration has invalid input identity.
   - Treat it as unset.
   - Do not apply the gain.
   - Do not enable live input monitoring.
   - Clear the invalid settings state.

### Audio Settings

9. User opens audio settings and cancels with the previous input identity restored.
   - Calibration is preserved.
   - Prompt does not reopen unless calibration was already missing.

10. User opens audio settings and applies the same input identity.
    - Calibration is preserved.
    - Prompt does not reopen unless calibration was already missing.

11. User applies a different input device.
    - Calibration is cleared.
    - `m_input_calibration_prompt_dismissed` is cleared.
    - Live input monitoring is disabled.
    - Calibration prompt opens after apply succeeds.
    - Signal-chain/plugin area is disabled with a route-changed message.

12. User applies a different selected input channel.
    - Calibration is cleared even if the active input channel count is unchanged.
    - `m_input_calibration_prompt_dismissed` is cleared.
    - Live input monitoring is disabled.
    - Calibration prompt opens after apply succeeds.

13. User changes sample rate, buffer size, output device, or other non-input fields.
    - Calibration is preserved because the input identity is unchanged.
    - Peak dBFS measurement is independent of sample rate and buffer size.

14. Audio settings apply fails.
    - Do not save a new calibration.
    - Do not enable live input because of a failed apply.
    - Preserve previous calibration only if the previous input identity is restored.

15. Audio settings window closes natively and restores the previous route.
    - Treat it like cancel.
    - Preserve calibration when the committed input identity is unchanged.

16. Audio settings window temporarily closes the audio device while editing.
    - Do not clear calibration solely because of that temporary closure.
    - Only the committed final input identity decides whether calibration is cleared.

17. Audio device settings button is pressed while calibration window is visible.
    - View action is unavailable.
    - Controller rejects audio-device settings intents without opening settings or running the
      callback.
    - Calibration window must be dismissed first.

18. External device disconnection while calibration window is visible.
    - `onAudioDeviceConfigurationChanged()` fires through the normal listener path.
    - Controller clears calibration, prompt dismissal, and measurement state without restoring
      a previous calibration.
    - Live input monitoring remains disabled.
    - Calibration window closes because the controller removes the prompt from the view state.
    - If the device reconnects, the calibration prompt reopens through the normal rule.

19. External device disconnection while calibrated.
    - Calibration state is cleared because the committed input identity became unavailable.
    - `m_input_calibration_prompt_dismissed` is cleared.
    - Live input monitoring is disabled.
    - Signal-chain/plugin area is disabled.
    - If the same device later reconnects, calibration is required again.

### Calibration Popup

20. Required popup is already visible and state is re-derived.
    - Do not open another popup.

21. User dismisses a required popup.
    - Popup closes.
    - `m_input_calibration_prompt_dismissed` is set.
    - Calibration remains unset.
    - Transport becomes available if normal project/output-route gates pass.
    - Live input monitoring remains disabled.
    - Signal-chain/plugin area remains disabled with a calibration message.
    - Signal-chain panel still exposes `Calibrate`.
    - Audio device settings become available again.
    - The popup does not automatically reopen until the input route changes or the user clicks
      `Calibrate`.

22. User starts calibration and live-input setup fails.
    - Measurement does not start.
    - No calibration is stored.
    - Live input monitoring remains disabled.
    - Popup displays the `LiveInputError` message.

23. User starts calibration with no usable input signal.
    - No calibration is stored.
    - Live input monitoring remains disabled.
    - User can retry.

24. User starts calibration and raw input clips.
    - No calibration is stored.
    - Live input monitoring remains disabled.
    - User is told to lower hardware/interface gain and retry.

25. User starts calibration and measurement succeeds, but applying gain fails.
    - No calibration is stored.
    - Live input monitoring remains disabled.
    - Popup displays the live-input error.

26. User starts calibration and measurement succeeds, but enabling monitoring fails.
    - No calibration is stored.
    - Live input monitoring remains disabled.
    - Popup displays the live-input error.

27. User starts calibration and the input identity changes before success is committed.
    - No calibration is stored.
    - Live input monitoring remains disabled.
    - Popup displays a route-changed error.
    - The normal prompt rule can reopen after the committed route stabilizes.

28. User starts calibration and measurement commits successfully.
    - Gain is computed, clamped, and applied through the live input port.
    - `InputCalibrationState{gain, current_identity}` is stored in `EditorSettings`.
    - Live input monitoring is enabled.
    - Prompt closes.
    - Signal-chain/plugin area becomes enabled.
    - Transport becomes available if normal project/output-route gates pass.

29. User clicks `Calibrate` while already calibrated.
    - Popup opens for manual recalibration.
    - Transport and audio device settings become unavailable.
    - Live processed monitoring is disabled when measurement starts.
    - Input gain is reset to `0.0 dB` when measurement starts so the raw input meter reads raw
      hardware level.
    - If the user dismisses before measurement starts, the previous calibration is unchanged.
    - If the user dismisses after measurement starts, the previous calibration state is restored by
      applying its gain and enabling live input monitoring.
    - If restoring gain or enabling monitoring fails, the previous calibration state is cleared and
      calibration is required again. The controller reports the live-input error.
    - If calibration succeeds, the new gain and identity replace the old state.

30. User clicks `Calibrate` with no active input route.
    - Do not show the calibration popup.
    - Keep live input disabled.
    - The audio-device button remains the path to choose a device.

### Transport, Signal Chain, And Live Rig

31. Play/pause is pressed while calibration window is visible.
    - Action is unavailable.

32. Play/pause is pressed while calibration is required but the window is dismissed.
    - Backing-track transport can start if normal project/output-route gates pass.
    - Live processed guitar monitoring remains disabled.
    - Signal-chain/plugin area remains disabled with a calibration message.

33. A project is opened while calibration is required.
    - Project can load.
    - Live rig can restore plugins and output gain.
    - If the calibration window is visible, transport remains disabled.
    - If the calibration window was dismissed, backing-track transport can run if normal gates
      pass.
    - Live input monitoring remains disabled until calibration succeeds.

34. Tone document load restores `outputGainDb`.
    - Output gain slider and meter behave as they do now.
    - Output gain remains part of project dirty state.
    - Output gain restore does not enable live input.

35. Tone document load contains old `inputGainDb`.
    - The field is silently ignored by the engine.
    - `LiveRigLoadResult` does not carry `input_gain`.
    - It does not mark the user calibrated.
    - It does not overwrite app-local calibration.
    - It does not enable live input monitoring.

36. Saving or publishing a project after calibration.
    - Does not write input calibration state to project or song package data.
    - Does still write output gain to the tone document.

37. Output gain changes while calibrated.
    - Behavior remains unchanged.
    - The project becomes dirty when output gain changes.

38. Output gain changes while calibration is required.
    - Allow the authored output value to change only if the live rig exists and the arrangement is
      loaded.
    - Do not enable live input monitoring.

39. Plugin add, remove, or open while calibration is required.
    - Plugin-chain controls are disabled with the signal-chain disabled message.
    - Live guitar audition remains unavailable.
    - Backing-track transport follows normal gates after calibration window is dismissed.

40. User changes audio device while transport is playing.
    - The existing UI already pauses before opening audio settings.
    - After apply, calibration is cleared if the input identity changed.
    - Transport can restart if normal output/project gates pass and the calibration window is not
      visible.
    - Live guitar monitoring cannot restart until calibration succeeds.

41. Live input backend is missing in a test or reduced composition.
    - Calibration state still derives from app settings and audio-device state.
    - No live input can be enabled because no live input port exists.
    - Backing playback must not become disabled solely because the live input port is missing.
    - Signal-chain/plugin area shows `Live input unavailable.`

## Testing Strategy

### `rock-hero-common/audio/tests/test_input_device_identity.cpp`

- default identity is invalid;
- non-empty backend/device with non-negative input channel index is valid;
- channel index participates in equality;
- channel name alone does not define identity.

### `rock-hero-common/audio/tests/test_input_calibration_state.cpp`

- calibration state matches only the same exact input identity;
- changed backend fails to match;
- changed input device fails to match;
- changed input channel index fails to match;
- changed channel display name with the same index fails to match because V4 includes the display
  name in exact equality for conservative safety.

### `rock-hero-common/audio/tests/test_input_calibration.cpp`

- accumulator starts silent;
- accumulator keeps the loudest peak;
- clipping sample causes `InputClipped`;
- silent/too-low sample causes `NoUsableSignal`;
- valid peak returns a clamped gain toward `inputCalibrationTargetPeakDb()`.

### `rock-hero-editor/core/tests/test_editor_settings.cpp`

- new settings start without input calibration state;
- full calibration state persists across reload;
- clearing calibration removes gain and identity keys;
- gain-only stored values are treated as missing;
- missing identity fields are treated as missing;
- invalid stored identity is treated as missing;
- invalid stored gain values are treated as missing;
- out-of-range stored gain values clamp on read.

### `rock-hero-editor/core/tests/test_editor_controller.cpp`

- active input plus missing calibration opens prompt;
- active input plus missing calibration disables live input and transport while prompt is visible;
- dismissing required calibration enables transport but keeps live input disabled;
- dismissing required calibration suppresses automatic prompt reopen for the same input route;
- active input plus matching stored calibration applies gain and enables monitoring;
- active input plus mismatched stored calibration clears state and disables monitoring;
- no active input disables live input and signal-chain audition without opening the prompt;
- calibration success stores gain plus identity and enables monitoring;
- calibration success fails without storage when input identity changes mid-measurement;
- calibration dismissal keeps live input disabled when required;
- manual recalibration dismissal before measurement start leaves previous calibration unchanged;
- manual recalibration dismissal after measurement start restores previous calibration only after
  applying its gain and enabling monitoring;
- changed input device identity clears calibration;
- changed input device identity clears prompt dismissal;
- different selected input channel clears calibration even when input channel count is unchanged;
- same input identity with changed buffer size or sample rate preserves calibration;
- same input identity with changed output device preserves calibration;
- failed audio-device apply does not create calibration;
- manual recalibration measurement disables processed live input;
- manual recalibration dismissal with gain restore failure clears calibration and requires
  recalibration;
- audio device settings are unavailable while calibration prompt is visible;
- controller rejects audio-device settings intents while calibration prompt is visible;
- external device disconnection during calibration clears state without restoring;
- live rig load result does not carry input gain and does not mark calibrated;
- transport is disabled while calibration window is visible;
- transport is available under normal gates after calibration window is dismissed;
- output gain restore and dirty behavior remains unchanged.

### `rock-hero-editor/ui/tests/test_editor_view.cpp`

- input gain slider no longer exists;
- input meter still exists and remains vertical;
- input meter component ID is `input_meter`;
- `Calibrate` button exists in the input rail;
- disabled signal-chain message renders when state provides one;
- plugin-chain controls are disabled when state disables them;
- output gain slider and output meter still exist;
- clicking `Calibrate` emits `onInputCalibrationRequested()`;
- required prompt state opens the calibration window;
- calibration window setup failure displays the returned live-input error;
- meter sampling uses `ILiveInput::rawInputMeterLevel()`.

### `rock-hero-common/audio/tests/test_engine.cpp`

- `IAudioDeviceConfiguration::currentInputDeviceIdentity()` returns the current exact identity;
- `currentInputDeviceIdentity()` returns `std::nullopt` when no input is open;
- `currentInputDeviceIdentity()` returns `std::nullopt` for multi-input masks;
- identity uses `AudioDeviceSetup::inputDeviceName` for split input/output backends;
- identity includes the selected physical input channel index;
- `ILiveInput::setLiveInputMonitoringEnabled(false)` mutes live processed output without breaking
  raw input metering;
- `ILiveInput::setInputGain()` applies the calibration gain stage;
- `ILiveInput::rawInputMeterLevel()` reports input level without requiring live rig monitoring;
- transport start does not implicitly enable live input monitoring;
- live rig load does not implicitly enable live input monitoring;
- `ILiveRig::captureActiveRig()` writes `outputGainDb` but not `inputGainDb`;
- `ILiveRig::loadLiveRig()` result does not carry `input_gain`;
- loading old `inputGainDb` from a tone document is silently ignored.

## Acceptance Criteria

- No input gain slider remains in the signal-chain panel.
- The input rail shows a vertical meter and a calibration button.
- Input meter component ID is `input_meter`.
- Output gain slider and output meter are visually and behaviorally unchanged.
- Input calibration state is stored only in app-local settings.
- Input calibration state includes gain and exact input route identity.
- A stored gain without a matching stored input identity never enables live guitar output.
- A stored calibration whose identity differs from the current input route never enables live
  guitar output.
- New tone documents do not write `inputGainDb`.
- Output gain continues to persist in tone documents.
- Opening or applying settings that preserve the exact input identity preserves calibration.
- Changing buffer size, sample rate, or output device does not clear calibration.
- Changing the input backend, input device name, or selected input channel identity clears
  calibration.
- Active input with missing or mismatched calibration shows a required calibration prompt.
- Audio device settings are unavailable while the calibration window is visible.
- Controller-level audio-device settings intents are rejected while the prompt is visible.
- Transport playback is disabled while the calibration window is visible.
- After the calibration window is dismissed, backing-track transport follows normal gates even if
  calibration is missing.
- Live processed guitar monitoring cannot start while calibration is missing or mismatched.
- Signal-chain/plugin audition controls are visibly disabled with a message while live guitar
  output is blocked.
- Dismissing calibration does not enable live input.
- Successful calibration applies gain, saves gain plus identity, enables live input monitoring,
  and enables signal-chain audition.

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
