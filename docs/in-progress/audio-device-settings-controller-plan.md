# Audio Device Settings Controller Plan

## Summary

Extract audio-device settings into the same testable state/controller shape used by the rest of
the editor, without overloading the existing editor project `Session` concept.

The target split is:

- `rock-hero-common/audio`: shared audio-device settings state and service.
- `rock-hero-editor/core`: editor-specific controller and view state.
- `rock-hero-editor/ui`: passive JUCE view and window hosting.

The first implementation should focus on audio-device settings only. Broader editor controller
extractions should be planned separately after this lands, because the controller has several
independent growth areas and extracting all of them at once would create unnecessary risk.

## Classes and Types to Create

### `rock-hero-common/audio`

`AudioDeviceSettingsErrorCode`

- Stable enum for branchable settings failures. Matches the typed-error pattern already used by
  `PluginHostError` / `PluginHostErrorCode` and `LiveRigError` / `LiveRigErrorCode` in the same
  namespace.
- Expected cases: no audio system, no usable device, apply failed, rollback failed, test output
  unavailable, and control panel unavailable.

`AudioDeviceSettingsError`

- Typed error value with `AudioDeviceSettingsErrorCode code` and `std::string message`, mirroring
  `PluginHostError` and `LiveRigError`.
- Used by port APIs so editor and game code do not parse JUCE error strings.

`StereoOutput`

- Project-owned stereo output value with left and right channel indices plus display text.
- Replaces the private `AudioDeviceSettingsView::OutputPair` helper so editor and game settings
  UIs derive the same output routes.
- Lives at common/audio without a settings-specific qualifier because the concept is a general
  stereo-routing value, not a settings-only one.

`AudioDeviceSettingsState`

- Shared snapshot returned by the settings port.
- Contains available audio systems, device names, channel choices, stereo outputs, sample rates,
  buffer sizes, row-shape flags such as separate input/output devices, capability flags for test
  output and control panel, the current staged selection fields directly (audio system name,
  input/output device names, input channel index, stereo output, sample rate, buffer size), and
  the last error message when an operation failed.
- The staged selection fields live directly on this state rather than in a separate
  `AudioDeviceSettingsRoute` type because no API call produces or consumes a route value as a
  unit. The staged fields are read by the view and updated through `IAudioDeviceSettings::select*`
  operations.
- This is not a view state. It describes the shared settings workflow, not one UI layout.

`IAudioDeviceSettings`

- Project-owned port contract consumed by product controllers.
- Exposes operations to begin or refresh a staged edit, select audio system/device/channel/format,
  apply staged settings with rollback, cancel staged settings, test output, and open the active
  backend control panel when available.
- Fallible operations return `std::expected<T, AudioDeviceSettingsError>` to match the typed-error
  convention used by `PluginHostError` and `LiveRigError` at the same boundary.
- Uses only project-owned settings values in its public contract.

`AudioDeviceSettings`

- JUCE-backed common/audio implementation of `IAudioDeviceSettings`.
- Wraps the existing `IAudioDeviceConfiguration` / `juce::AudioDeviceManager` behavior.
- Owns the staging transaction currently embedded in `AudioDeviceSettingsView`.
- Keeps existing device-type ordering, sample-rate defaulting, low-latency sorting, apply rollback,
  and active-device-only enablement semantics.
- Folds in the helpers currently in the free-function `audio_device_settings.{h,cpp}` module
  (`preferredAudioDeviceTypeOrder`, `chooseAudioDeviceSampleRate`, `sampleRatesMatch`) as private
  static helpers. The existing free-function header is removed; its tests move into
  `test_audio_device_settings.cpp` alongside the new port tests.

### `rock-hero-editor/core`

`AudioDeviceSettingsViewState`

- Editor-specific render state for the JUCE settings view.
- Contains the visible row shape, choices, selected IDs, button enabledness, and error text needed
  by the editor window.
- Defines a nested `Choice` value type (stable integer ID plus display label) used directly in the
  state's choice arrays. The view emits IDs back to the controller instead of parsing labels.
- Derived from `common::audio::AudioDeviceSettingsState`.

`IAudioDeviceSettingsView`

- Framework-free view contract used by the settings controller tests.
- Provides `setState(const AudioDeviceSettingsViewState&)` and a one-shot close request method.
- Keeps close-window effects separate from durable render state.

`IAudioDeviceSettingsController`

- Framework-free intent contract implemented by the concrete settings controller.
- Receives editor settings intents such as selected audio system ID, selected device ID, selected
  input channel ID, selected stereo output ID, selected sample-rate ID, selected buffer-size ID,
  OK, Cancel, Test Output, Control Panel, and external backend refresh.

`AudioDeviceSettingsController`

- Editor-core workflow controller for one audio-device settings window.
- Maps `AudioDeviceSettingsState` into `AudioDeviceSettingsViewState`.
- Translates view-selected integer IDs into common/audio port operations.
- Applies OK/Cancel semantics, surfaces port apply errors through view state, and requests window
  close only after apply or cancel succeeds.
- Does not own JUCE widgets, `DialogWindow`, `AudioDeviceManager`, or native window placement.

### `rock-hero-editor/ui`

`AudioDeviceSettingsView`

- Refactor the existing private JUCE component into a passive renderer.
- It should accept `AudioDeviceSettingsViewState`, populate controls from that state, and emit
  selected IDs through `IAudioDeviceSettingsController`.
- Remove direct `juce::AudioDeviceManager` ownership, staged setup state, preview-device creation,
  defaulting policy, apply rollback, and device-manager listener behavior.

`AudioDeviceSettingsWindow`

- Continue to own window positioning, resize limits, and JUCE dialog behavior.
- Compose an `AudioDeviceSettingsController` with `AudioDeviceSettingsView` around an
  `IAudioDeviceSettings` supplied by editor composition.
- Close the modal window when `IAudioDeviceSettingsView` receives the controller's close request.

## Implementation Plan

1. Add common/audio settings values (`StereoOutput`, `AudioDeviceSettingsState`),
   `IAudioDeviceSettings`, and `AudioDeviceSettings`. Fold the existing free-function
   `audio_device_settings.{h,cpp}` helpers into `AudioDeviceSettings` and migrate their tests.
2. Move the current staged-device logic out of `AudioDeviceSettingsView` into
   `AudioDeviceSettings` without changing behavior.
3. Add editor-core settings controller/view contracts and `AudioDeviceSettingsViewState`.
4. Refactor `AudioDeviceSettingsView` so it renders only controller-derived view state and emits
   controller intents.
5. Refactor `AudioDeviceSettingsWindow` and `EditorView` composition so `EditorView` no longer
   receives or stores a raw `juce::AudioDeviceManager*`.
6. Keep existing editor-wide audio-device status text and persisted device-state restore/save in
   `EditorController` for the first pass. They are editor-wide app state, not window-local
   settings state.
7. After the new shape lands, replace the current JUCE-device-heavy settings view smoke tests with
   focused editor-core controller tests plus a small UI wiring test.

## Editor Structure Assessment

Not every view should have its own controller. Controllers should map to workflows or feature
state machines, not to every leaf component. Every independently rendered feature view should have
explicit view state, but small visual controls can remain controller-free when they only render
already-derived state and emit local intents.

Current editor surfaces:

- `EditorView` has `EditorViewState` and is driven by `EditorController`. Keep it as the
  editor shell, but do not move every child workflow into this type.
- `ArrangementView` has `ArrangementViewState` and emits normalized clicks. It does not need a
  dedicated controller until arrangement editing grows beyond click-to-seek.
- `TransportControls` has `TransportControlsViewState` in UI and emits play/stop intents. It does
  not need a controller. Consider moving the view state to editor/core later for naming
  consistency.
- `SignalChainPanel` has `SignalChainViewState`, but workflow lives inside `EditorController`.
  This is a candidate for a future `SignalChainController` in editor/core.
- `PluginBrowserWindow` has `PluginBrowserViewState`, but workflow lives inside
  `EditorController`. Treat it as part of the signal-chain workflow first, not a separate
  controller unless it grows.
- `BusyOverlay` has `BusyViewState`, but busy lifecycle lives inside `EditorController`. Extract a
  busy-state helper if needed, not a `BusyOverlayController`.
- `AudioDeviceSettingsView` has no view state/controller and owns backend logic directly. This is
  the highest-priority extraction covered by this plan.
- `MenuBarButton` is a leaf visual component with text set by `EditorView`. It does not need a
  controller or view state.
- `AudioDeviceSettingsWindow` is a window host around the settings view. Keep it UI-only; it
  should compose controller/view but not own settings policy.
- `Editor` is the composition wrapper for controller and view. It does not need view state or a
  controller.
- `MainWindow` is the top-level JUCE application window. It does not need view state or a
  controller.

Guideline going forward:

- Add a `ViewState` when a component renders app/domain state derived outside the component.
- Add a controller when a feature owns workflow state, backend operations, async operations, or
  nontrivial intent policy.
- Do not add controllers for leaf controls that only paint state and emit local button/click
  intents.
- Prefer one controller per coherent workflow. For example, signal-chain and plugin-browser
  behavior can initially share `SignalChainController`.

## Likely Follow-Up Extractions

`SignalChainController`

- Owns `SignalChainViewState`, `PluginBrowserViewState`, plugin catalog scan state, add/remove/open
  plugin intents, and plugin-chain error handling.
- Reduces `EditorController` size without splitting browser behavior away from the chain it edits.

`ProjectWorkflowController`

- Owns open/import/save/save-as/publish/close/exit, unsaved-change prompts, Save As prompt state,
  project IO worker state, and project dirty-state transitions.
- Should be considered only after audio settings and signal-chain extraction clarify the desired
  controller composition pattern.

`BusyOperationState`

- Helper value/object, not a view controller.
- Owns busy operation identity, token invalidation, live-rig progress, and derived `BusyViewState`.
- Can be extracted independently if busy policy continues to obscure feature code.

`TransportController`

- Not needed now. Transport behavior is small and tightly coupled to editor-wide loaded-session
  state. Revisit only if transport gains richer loop, metronome, count-in, or scrub policy.

## Test Plan

Common/audio tests:

I- `AudioDeviceSettings` initializes from an active backend route and derives available settings
  choices.
- Audio system, device name, channel, stereo output, sample-rate, and buffer-size selections
  update staged state without applying to the active route.
- OK applies the staged route and returns `std::expected<void, AudioDeviceSettingsError>` on
  failure with the matching error code.
- Apply failure restores the previous backend type/setup and leaves staged state visible.
- Cancel abandons staged state without mutating the backend route.
- External refresh preserves user-staged choices when still valid and falls back when invalid.
- Existing free-function policy tests (`preferredAudioDeviceTypeOrder`,
  `chooseAudioDeviceSampleRate`, `sampleRatesMatch`) move alongside the port tests in the same
  file.

Editor/core tests:

- Controller maps port state into `AudioDeviceSettingsViewState` with stable choice IDs.
- Selection intents call the expected port operation and push refreshed view state.
- OK closes the view only after successful apply.
- Failed OK keeps the view open and renders the error text from the port.
- Cancel closes without applying.
- Test Output and Control Panel intents are gated by state capability flags.

Editor/ui tests:

- `AudioDeviceSettingsView::setState` populates controls, visibility, selected IDs, enabledness,
  and error text.
- Combo changes emit selected IDs to `IAudioDeviceSettingsController`.
- OK, Cancel, Test Output, and Control Panel buttons emit controller intents.
- Window test coverage remains limited to composition/close behavior; backend policy is covered in
  common/audio and editor/core tests.

## Assumptions

- `Session` remains reserved for loaded editor/project song state; audio settings will not use that
  name.
- `AudioDeviceSettings` is the shared common/audio use-case boundary, while
  `AudioDeviceSettingsController` is editor-specific.
- The game will eventually reuse common/audio settings state and the `IAudioDeviceSettings` port,
  but it will have its own game-specific view state and controller if its UI flow differs from
  the editor.
- Existing audio-device status text in the editor menu bar remains driven by `EditorViewState`.
- The first extraction should preserve current user-visible behavior unless a test exposes a
  current bug.

## Once This Lands

Two pieces of project-wide guidance referenced by this plan are currently undocumented in
`docs/design/`. After the extraction proves out, both should migrate to the design docs so future
contributors find the rules there rather than buried in an in-progress plan:

- The [Editor Structure Assessment](#editor-structure-assessment) section above (which views
  need controllers, which need view state, and when to defer) belongs in
  `docs/design/architectural-principles.md`.
- The typed-error convention shared by `PluginHostError`, `LiveRigError`, `ProjectError`,
  `ArchiveError`, `SongImportError`, `SongPackageError`, and the new `AudioDeviceSettingsError`
  — `<Thing>ErrorCode` enum plus `<Thing>Error` struct holding `code` and `message`, returned
  through `std::expected<T, <Thing>Error>` at port boundaries — should be documented in
  `docs/design/coding-conventions.md` so it is followed by future ports rather than rediscovered
  by reading neighboring headers.
