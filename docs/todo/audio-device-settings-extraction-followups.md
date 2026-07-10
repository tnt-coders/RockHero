# Audio Device Settings Extraction Open Follow-Ups

Status: deferred follow-ups. These are intentionally outside the completed extraction and should be
revisited when the game-side audio settings UI lands or when more settings-view test investment is
prioritized. The ChangeListener re-derivation smoke test formerly tracked here was absorbed into
`docs/roadmap/13-audio-device-settings-and-calibration.md` Phase 6; the editor-UI follow-ups below
remain deferred.

## Context

The shared audio-device settings code now lives in
`rock-hero-common/audio/audio_device_settings.{h,cpp}` with the policy decisions
(`preferredAudioDeviceTypeOrder`, `chooseAudioDeviceSampleRate`) covered by
`test_audio_device_settings.cpp`. The editor's settings UI was renamed to
`AudioDeviceSettingsView` and `AudioDeviceSettingsWindow` and now consumes the shared
policy. The game will reuse the same `IAudioDeviceConfiguration` port and the same shared
policy when it gets its own settings UI.

This document captures concerns raised during code review that were intentionally not addressed in
the extraction PR.

## Open Follow-Ups

### View file size and shape

`audio_device_settings_view.cpp` is around 986 lines. Nothing in the extraction made it
worse; the pure helpers came out, but the view is monolithic enough that a future
split could help. Candidate seams:

- Route management (device type, device names, channel selection).
- Audio format management (sample rate, buffer size).
- Window state management (refresh gating, error label, OK/Cancel/Apply flow).

Out of scope for the extraction. Worth revisiting if the game view ends up sharing more of this
structure than expected, in which case some of the file could move into common as a
presentation-agnostic state machine while the JUCE wiring stays in editor UI.

### Hard window-size caps

`g_max_window_width{1000}` and `g_max_window_height{760}` in
`audio_device_settings_view.cpp` are hard caps. On small displays with separate-device
backends and many visible channel rows the form could clip. The current row counts have
not triggered this, but the constants are worth reconsidering if either the form grows
or the game side targets smaller windows.

### JUCE coupling at the shared API boundary

`audio_device_settings.h` takes `juce::StringArray` for type-name ordering. This keeps
the shared policy header coupled to JUCE. That coupling is already true across the rest
of `rock-hero-common/audio` (Engine, IAudioDeviceConfiguration both expose JUCE types),
so the API is consistent. It does mean a future non-JUCE port cannot selectively replace
just this header. Acceptable today; flagged for visibility.

## Not Tracked Here

Anything covered by the extraction PR itself: the file moves, the rename from "policy" to
"settings," the rename from "dialog" to "window," the consolidation of the two prior test files,
and the behavioral improvement in `chooseAudioDeviceSampleRate` (each priority layer is now tested
for availability before falling through to the next).
Those landed and are not open work.
