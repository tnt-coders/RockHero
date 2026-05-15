# Tracktion Live Instrument Monitoring Reference

This is the compact reference for the first Tracktion-backed live instrument monitoring slice. It
supersedes the earlier draft, review, and `for-reference-only` planning files that led to this
implementation.

## Goal

Route the app-selected mono input through Tracktion Engine on a dedicated live instrument track.
This is the minimal foundation for plugin FX and automation; it does not add plugin scanning,
plugin-chain editing, automation lanes, pitch detection, recording, or game integration yet.

Target signal path:

```text
app-local audio device settings
  -> selected mono input
  -> Tracktion WaveInputDevice
  -> Live Instrument AudioTrack
  -> future Tracktion plugin chain
  -> stereo mix output
```

The user-facing contract is intentionally simple:

- Audio Device Settings selects one audio system, one mono input, and one stereo output pair.
- Hardware routing is app-local settings, not song or project data.
- The backing track does not expose or need an input selector.
- The live instrument input is bound automatically after accepted device changes.
- Canceling the audio settings dialog leaves the live device route unchanged.
- Live monitoring should be audible while stopped and while backing audio is playing.

## Implementation Shape

The implementation lives in `rock-hero-common/audio` because this is Tracktion/JUCE adapter
behavior shared by the editor and future game audio path.

Key files:

- `rock-hero-common/audio/src/engine.cpp`
- `rock-hero-common/audio/src/tracktion_live_wave_device_mapping.h`
- `rock-hero-common/audio/src/tracktion_live_wave_device_mapping.cpp`
- `rock-hero-common/audio/tests/test_tracktion_live_wave_device_mapping.cpp`

`Engine` now builds a Tracktion edit with explicit track roles:

- `Backing`: owns arrangement audio clips.
- `Live Instrument`: owns the selected live input and future plugin chain.

The edit keeps stable `tracktion::EditItemID` values for both tracks. Arrangement loading and
clearing operate only on the backing track; live input assignment and future plugins stay on the
live instrument track.

`RockHeroEngineBehaviour` is private to `engine.cpp` and supplies Tracktion with generated wave
device descriptions for the current JUCE device masks. It disables Tracktion's automatic device
manager initialization so `Engine` can create the edit first, initialize the JUCE device manager,
then bind the live route.

## Route Binding

`applyLiveInstrumentMonitoringRoute()` is the single engine-local binding path. It should remain
message-thread only because it mutates Tracktion device and edit state.

The binding sequence is:

1. Read the current `juce::AudioIODevice` from Tracktion's device manager.
2. Build compact live wave-device descriptions from the active JUCE input/output masks.
3. Dispatch pending Tracktion device-list updates.
4. Force or allocate the playback context with `ensureContextAllocated(true)`.
5. Clear existing live track input assignments with `EditInputDevices::clearAllInputs(...)`.
6. Find the generated `WaveInputDevice` by its stable generated name.
7. Force mono treatment with `WaveInputDevice::setStereoPair(false)`.
8. Get the current `InputDeviceInstance`.
9. Assign it to the live track with `setTarget(..., moveToTrack=true, ..., 0)`.
10. Keep recording disabled with `setRecordingEnabled(..., false)`.
11. Enable Tracktion monitoring and force a final graph rebuild.

This makes rebinding idempotent. Extra JUCE async change messages may cause another bind, but the
clear-then-assign sequence still leaves one live target.

Do not store raw `WaveInputDevice*` or `InputDeviceInstance*` across device changes. Re-resolve
them from the current device state each time the route is applied.

## Wave-Device Mapping

`tracktion_live_wave_device_mapping` exists because JUCE device masks and Tracktion callback
channels are not the same thing.

For ASIO and other multi-channel devices, the selected physical channel can be non-zero. If the
user selects physical input `3`, the callback buffer exposed to Tracktion for the selected route
can still be compact channel `0`. The helper converts from physical selected channels to compact
Tracktion channels.

The route contract is deliberately narrow:

- exactly one input channel bit must be enabled
- exactly two output channel bits must be enabled
- the output bits must be one even-left adjacent stereo pair such as `0/1`, `2/3`, or `6/7`
- physical input maps to compact Tracktion input channel `0`
- physical output pair maps to compact Tracktion output channels `0` and `1`
- generated wave-device names include the hardware device name and selected physical channels

Generated names are part of Tracktion's wave-device identity, so they must stay stable and
hardware-qualified.

## Transport And Context Lifetime

Live monitoring requires a Tracktion playback context even while the backing transport is stopped.
The edit uses `playInStopEnabled = true` so the graph can process monitored input at rest.

The stop paths are intentionally split:

- Pause/stop for playback reset keeps devices available for live monitoring.
- Graph mutation and shutdown release the playback context and clear devices.
- Public stopped position reads use the transport head, not audible time, because the live graph
  can remain allocated while backing playback is stopped.

The cost of this design is that the live graph can process continuously while stopped. That is
acceptable for the first pass, but it becomes important once plugin FX are inserted.

## Audio Settings Boundary

The audio settings UI remains the only place to select hardware routing. It stages route changes
locally and applies them to the live `juce::AudioDeviceManager` only on OK.

Settings persistence remains app-local through `EditorSettings::audioDeviceState()`. Do not store
ASIO driver names, input channels, output channels, sample rate, or buffer size in project/song
files.

There should be no track-level input picker in the editor for this first product shape. The app has
one backing output mix and one live mono input route.

## Testing

Hardware-free coverage belongs around the compact route mapping helper. The current mapping tests
cover:

- first physical input maps to compact mono input
- non-first physical input maps to compact mono input
- non-first physical output pair maps to compact stereo output
- generated names include hardware and physical channel identity
- missing input is rejected
- multiple inputs are rejected
- missing output is rejected
- non-adjacent output is rejected
- offset adjacent output such as `1/2` is rejected
- multiple output pairs are rejected

Manual Windows ASIO verification should cover:

- selecting a valid mono input and stereo output pair
- selecting non-first input and output channels
- confirming live input is audible while stopped
- confirming live input remains audible while backing playback runs
- confirming Cancel leaves the prior live route unchanged
- confirming accepted device changes rebind the route without needing a restart
- confirming shutdown does not leave the device in a noisy or stuck state

## Deferred Work

The next audio milestones can build on the live instrument track:

- Add VST3 discovery and plugin insertion on the live instrument track.
- Add plugin-chain editing UI without exposing DAW-style track routing.
- Persist tone/plugin intent in project or song data, separate from hardware device routing.
- Add Tracktion automation for plugin bypass, wet/dry mix, and parameters.
- Support amp-chain blending on the live track if Tracktion's plugin/rack model fits cleanly.
- Use a second track or more explicit parallel routing only if the single-track approach becomes
  brittle or unclear.
- Add a clean pre-effects analysis tap for pitch detection and scoring.
- Add performance monitoring once plugins process continuously while stopped.

## Watch Points

- `ensureContextAllocated(true)` is load-bearing after live input target changes.
- Device change callbacks may arrive more than once; binding must stay idempotent.
- Generated wave-device names should not become generic names that collide across devices.
- `playInStopEnabled` means future plugins may consume CPU even when backing playback is stopped.
- A fresh install may have no valid mono-input/stereo-output route until the user opens Audio
  Device Settings.
- Do not reintroduce a direct JUCE dry-monitor path alongside Tracktion monitoring.
