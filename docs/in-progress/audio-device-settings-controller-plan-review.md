# Audio Device Settings Controller Plan Review

## Summary

The updated direction is a good fit for the project. `IAudioDeviceSettings` and
`AudioDeviceSettings` match the existing common/audio style better than service-suffixed names,
and the split between common/audio behavior, editor/core workflow, and editor/ui rendering is
aligned with the architecture guidance.

## Concerns to Resolve Before Implementation

### Existing typed-error guidance

The plan's "Once This Lands" section says the typed-error convention should be documented in
`docs/design/coding-conventions.md`, but that convention already exists there under
"Recoverable Error Returns." The plan should instead say that `AudioDeviceSettingsErrorCode` and
`AudioDeviceSettingsError` must follow the existing convention.

### `StereoOutput` naming

`StereoOutput` may be too broad for a value that represents two output channel indices plus
display text. Consider `StereoOutputPair` or `AudioOutputPair` so the name makes the channel-pair
meaning explicit.

### External backend refresh lifecycle

The plan should make the refresh path explicit. An open settings controller needs to know when the
audio backend changes externally. Choose one of these designs before implementation:

- `IAudioDeviceSettings` exposes a listener and the controller subscribes to it.
- The window/composition layer observes `IAudioDeviceConfiguration` and forwards refresh intents
  into `IAudioDeviceSettingsController`.

The first option keeps settings refresh behavior closer to the common/audio port and is probably
the cleaner default.

### Test-plan typo

The common/audio test-plan section has a typo: `I- AudioDeviceSettings initializes...` should be
`- AudioDeviceSettings initializes...`.
