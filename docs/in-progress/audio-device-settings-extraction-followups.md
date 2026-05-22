# Audio Device Settings Extraction — Open Follow-Ups

## Context

The shared audio-device settings code now lives in
`rock-hero-common/audio/audio_device_settings.{h,cpp}` with the policy decisions
(`preferredAudioDeviceTypeOrder`, `chooseAudioDeviceSampleRate`) covered by
`test_audio_device_settings.cpp`. The editor's settings UI was renamed to
`AudioDeviceSettingsView` and `AudioDeviceSettingsWindow` and now consumes the shared
policy. The game will reuse the same `IAudioDeviceConfiguration` port and the same shared
policy when it gets its own settings UI.

This document captures concerns raised during code review that were intentionally not
addressed in the extraction PR. They are tracked here so they can be picked up when the
game-side UI lands or when test investment is prioritized.

## Open Follow-Ups

### View-level smoke tests

`AudioDeviceSettingsView` (and its sibling that will live in the game UI) currently has
no direct tests. The shared policy is well covered, but the view-level behavior is not:

- OK / Cancel preview semantics: pressing Cancel must leave the active route untouched,
  pressing OK must apply the staged route.
- Apply rollback: when `juce::AudioDeviceManager::setAudioDeviceSetup` returns a
  non-empty error string, the view restores both the prior device type and prior setup
  and surfaces the error in the error label without closing the window.
- `ChangeListener` re-derivation: when the device manager broadcasts a change while the
  window is open, the view re-reads its staged state and the combos reflect the new
  reality without losing user-visible staging the user has already touched (or, if that
  is the design choice, the staging is intentionally discarded with a documented reason).

A small smoke test for each of these in editor UI tests would pay off twice once the
game-side view exists, because both views will share the same interaction patterns even
if the controls differ.

### Sample-rate tolerance duplication

`g_sample_rate_match_tolerance{0.001}` lives in
`rock-hero-common/audio/src/audio_device_settings.cpp`, while the editor view's
`selectedSampleRateId` still hardcodes `0.001` directly for ComboBox ID lookup. Two
copies of the same studio-engineering constant. Options:

- Leave it: the value is conventional and unlikely to change.
- Export a constant from the common header so view code can reference it.

Leaving it is the lower-friction choice and is the current state. Revisit only if the
tolerance ever needs to change.

### View file size and shape

`audio_device_settings_view.cpp` is around 986 lines. Nothing in the extraction made it
worse — the pure helpers came out — but the view is monolithic enough that a future
split could help. Candidate seams:

- Route management (device type, device names, channel selection).
- Audio format management (sample rate, buffer size).
- Window state management (refresh gating, error label, OK/Cancel/Apply flow).

Out of scope for the extraction. Worth revisiting if the game view ends up sharing more
of this structure than expected, in which case some of the file could move into common
as a presentation-agnostic state machine while the JUCE wiring stays in editor UI.

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

Anything covered by the extraction PR itself — the file moves, the rename from "policy"
to "settings," the rename from "dialog" to "window," the consolidation of the two prior
test files, and the behavioral improvement in `chooseAudioDeviceSampleRate` (each
priority layer is now tested for availability before falling through to the next).
Those landed and are not open work.