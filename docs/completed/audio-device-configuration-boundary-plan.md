# Audio Device Configuration Boundary Plan

## Purpose

`IAudioDeviceConfiguration` currently exposes `juce::AudioDeviceManager&`. That is pragmatic while
`rock-hero-common/audio` is the JUCE/Tracktion audio adapter, but the API is broad enough that
editor and game code can bypass project-owned audio-device operations.

When the game gets its own audio settings UI, narrow this boundary so both products share the same
headless audio-device choices without coupling product workflow to the full JUCE manager.

## Current Shape

- `common::audio::Engine` implements `IAudioDeviceConfiguration`.
- `common::audio::AudioDeviceSettings` uses the manager to enumerate audio systems, stage device
  routes, inspect channel/sample-rate/buffer capabilities, and apply the selected setup.
- `editor::core::EditorController` still uses the manager directly for serialized state restore and
  persistence.
- `editor::ui` passes the configuration port into the settings window so the shared settings
  backend can be reused.

## Target Shape

Keep JUCE device ownership inside `rock-hero-common/audio` and expose project-owned operations:

- current route status,
- available audio systems and device routes,
- channel, sample-rate, and buffer-size choices for a staged route,
- route apply/cancel/test-output/control-panel commands,
- serialized audio-device state restore and persistence for product settings.

The editor and game should consume the same project-owned snapshots and commands. Their UIs can be
different, but they should not need to know how to drive `juce::AudioDeviceManager` directly.

## Planned Types

### `AudioDeviceRouteSnapshot`

Project-owned description of the current route: backend name, device names, selected channels,
sample rate, buffer size, bit depth, latency text inputs, and open/closed state.

### `AudioDeviceChoiceCatalog`

Project-owned catalog of selectable audio systems, input/output devices, channel choices, sample
rates, and buffer sizes for the currently staged route.

### `AudioDeviceRouteSelection`

Project-owned command value representing the user's selected backend, devices, channels, sample
rate, and buffer size.

### `AudioDeviceConfigurationStore`

Small project-owned serialization boundary that restores and captures opaque device-manager state
without exposing `createStateXml()` or `initialise()` to product controllers.

### `IAudioDeviceConfiguration`

Narrowed port that eventually returns project-owned snapshots/catalogs and accepts project-owned
selection/apply/serialization commands. `juce::AudioDeviceManager&` should become private adapter
surface or be kept only on a concrete adapter type used by app composition and low-level adapter
tests.

## Refactor Steps

1. Add project-owned route snapshot and catalog values beside the existing settings state.
2. Move editor settings restore/persist calls behind `IAudioDeviceConfiguration` methods so
   `EditorController` no longer reaches into `deviceManager()`.
3. Change `AudioDeviceSettings` to depend on narrow enumeration/apply operations rather than the
   full manager where practical.
4. Keep a temporary manager escape hatch only inside adapter tests and app composition if a full
   removal would make the migration too large.
5. Once the game audio settings UI exists, verify both products can use the same common backend
   without product-specific JUCE manager calls.
6. Remove the public manager accessor when no product workflow code depends on it.

## Testing Strategy

- Keep tests at the public `AudioDeviceSettings` and `IAudioDeviceConfiguration` API level.
- Use hand-written fakes for project-owned snapshots and command results.
- Keep JUCE-manager tests focused on the concrete adapter, not editor or game workflow behavior.
- Assert typed error codes for restore/apply failures rather than parsing display text.
