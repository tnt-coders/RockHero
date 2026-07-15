# Audio Device Settings Controller Extraction

## Summary

The audio-device settings workflow has been extracted from a JUCE-heavy settings view into the
project's normal testable layering:

- `rock-hero-common/audio` owns shared audio-device settings state, typed errors, the settings
  port, and the JUCE-backed implementation.
- `rock-hero-editor/core` owns the editor-specific controller and view state.
- `rock-hero-editor/ui` owns passive JUCE controls and window hosting.

This replaced the original `AudioDeviceSettingsView` design where the view directly owned staged
device setup, preview device creation, rollback behavior, and device-manager listener handling.

## Completed Shape

### Common Audio

`AudioDeviceSettingsErrorCode` and `AudioDeviceSettingsError`

- Provide the typed boundary error contract for recoverable settings failures.
- Follow the existing `std::expected<T, DomainError>` convention from
  `docs/design/coding-conventions.md`.
- Cover missing audio systems, missing devices, apply failure, rollback failure, unavailable test
  output, and unavailable control panel cases.

`StereoOutputPair`

- Represents a selectable stereo output route with left channel, right channel, and display label.
- Lives in common/audio so editor and game settings flows can share route derivation.

`AudioDeviceSettingsState`

- Describes the shared staged settings workflow, not any one UI layout.
- Carries available audio systems, devices, channels, output pairs, sample rates, buffer sizes,
  selected IDs, capability flags, and current error text.

`IAudioDeviceSettings`

- Exposes the project-owned audio-device settings port.
- Supports begin/cancel/apply, staged selection updates, test output, control-panel access, and a
  nested `Listener` for backend refreshes.
- Keeps public contracts project-owned instead of exposing JUCE setup objects to product code.

`AudioDeviceSettings`

- Implements the settings port around `IAudioDeviceConfiguration` and `juce::AudioDeviceManager`.
- Owns staging, preview-device capability reads, apply rollback, and backend refresh forwarding.
- Preserves the active route until OK applies staged settings.

### Editor Core

`AudioDeviceSettingsViewState`

- Converts shared settings state into editor-specific render data: choices, selected IDs, enabled
  flags, row shape, and error text.

`IAudioDeviceSettingsView`

- Provides a framework-free view contract for controller tests.
- Separates durable state rendering from one-shot close and applying presentation effects.

`IAudioDeviceSettingsController`

- Receives editor settings intents as selected integer IDs and button actions.
- Avoids parsing UI labels or exposing JUCE component behavior to tests.

`AudioDeviceSettingsController`

- Maps common/audio settings state into editor view state.
- Translates editor intents into settings port operations.
- Subscribes to `IAudioDeviceSettings::Listener` and refreshes the view on backend changes.
- Closes the view only after OK succeeds or Cancel completes.

### Editor UI

`AudioDeviceSettingsView`

- Renders `AudioDeviceSettingsViewState`.
- Emits selected IDs and button intents to `IAudioDeviceSettingsController`.
- No longer owns backend policy, staged setup, preview devices, or rollback logic.

`AudioDeviceSettingsWindow`

- Hosts the view and controller inside a JUCE dialog.
- Owns window positioning, resize limits, modal lifetime, and apply hide/show behavior.
- Composes a common/audio settings instance with the editor controller and view.

## Resolved Design Questions

- Audio settings do not use the name `Session`; that remains reserved for loaded editor/project
  song state.
- The shared settings backend lives in common/audio because both editor and game need the same
  device choices and staging behavior.
- The editor-specific controller lives in editor/core because it owns editor workflow policy and
  remains testable without JUCE widgets.
- External backend refresh uses a listener on `IAudioDeviceSettings`, keeping refresh semantics
  inside the shared audio boundary rather than composition glue.
- The game can reuse `IAudioDeviceSettings` and `AudioDeviceSettingsState` later while providing
  its own view state and controller if its UI flow differs.

## Editor Structure Guidance Captured

The extraction clarified these project conventions:

- Add a view state when a component renders app or domain state derived outside the component.
- Add a controller when a feature owns workflow state, backend operations, async operations, or
  nontrivial intent policy.
- Do not add controllers for leaf controls that only paint state and emit local button or click
  intents.
- Prefer one controller per coherent workflow.

This guidance was useful for the extraction but has not been promoted to `docs/design/` yet. If it
becomes a durable project rule, update `docs/design/architectural-principles.md` after confirming
that broader design change.

## Completed Tests

Common/audio tests cover:

- Initializing from an active backend route.
- Staging system, device, channel, output pair, sample-rate, and buffer-size choices without
  mutating the active route.
- Applying staged routes.
- Preserving and reporting typed apply failures.
- Rolling back failed applies.
- Cancelling staged changes.
- Defaulting staged sample rates and buffer sizes.
- Forwarding backend refresh notifications.

Editor/core tests cover:

- Mapping shared settings state into view state.
- Emitting port operations from selected IDs and button intents.
- Closing only after successful OK or Cancel.
- Keeping the view open on failed OK.
- Capability gating for Test Output and Control Panel.

Editor/ui tests cover:

- Rendering settings state into JUCE controls.
- Emitting selected IDs and button intents.
- Applying presentation and window lifetime behavior where needed.
