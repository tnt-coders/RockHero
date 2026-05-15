# ASIO Live Guitar Input Plan

## Goal

Add the first real live guitar monitoring path as simply as possible:

- ASIO input enabled on Windows
- user-selected ASIO input device and channel routed to output
- backing track continues playing from the selected arrangement
- live guitar and backing track are audible together
- no recording
- no VST chain
- no tone automation
- no persisted device settings
- no pitch detection

This should validate the core audio-device and routing path without pulling future gameplay,
plugin-hosting, or scoring concerns into the first implementation slice.

## Scope

This belongs in `rock-hero-common/audio` behind the existing Tracktion/JUCE adapter boundary.
Editor core and UI should interact through a project-owned port rather than depending directly on
JUCE, Tracktion, or ASIO APIs.

The first implementation should preserve the existing backing-track behavior and add live input
beside it.

## Step 1: Enable ASIO In The Build

Enable ASIO for the JUCE audio devices wrapper on Windows.

Likely location:

- `cmake/RockHeroExternalModules.cmake`

Expected direction:

```cmake
target_compile_definitions(rock_hero_juce_audio_devices PUBLIC JUCE_ASIO=1)
```

Start with JUCE's bundled ASIO header path unless the build requires an external SDK.

## Step 2: Add A Narrow Live Input Port

Add a public interface in `rock-hero-common/audio`, separate from `IAudio`:

```cpp
struct GuitarInputDevice
{
    std::string name;
    std::vector<std::string> input_channels;
};

struct GuitarInputSelection
{
    std::string device_name;
    int input_channel_index{};
};

class IGuitarInput
{
public:
    virtual ~IGuitarInput() = default;

    [[nodiscard]] virtual std::vector<GuitarInputDevice> availableAsioInputDevices() = 0;

    [[nodiscard]] virtual std::expected<void, AudioDeviceError>
    selectAsioInput(GuitarInputSelection selection) = 0;

    [[nodiscard]] virtual std::expected<void, AudioDeviceError> enableGuitarMonitoring() = 0;

    virtual void disableGuitarMonitoring() = 0;

    [[nodiscard]] virtual bool isGuitarMonitoringEnabled() const noexcept = 0;
};
```

Keep this separate from `IAudio` because `IAudio` currently means song preparation and active
arrangement playback. Guitar input is device/routing behavior, not song persistence or arrangement
activation.

Name the interface after the project capability (`IGuitarInput`) instead of the backend technology.
Keep `Asio` in the device-query and selection names because the first picker is explicitly
ASIO-facing.

## Step 3: Add Typed Audio Device Errors

Add `AudioDeviceErrorCode` and `AudioDeviceError` using the project error policy.

Likely first codes:

- `AsioUnavailable`
- `AsioDeviceNotFound`
- `AsioInputChannelUnavailable`
- `AudioDeviceOpenFailed`
- `LiveInputRoutingFailed`

Keep the enum small and only add codes for branches that exist in the implementation.
Mark the error type `[[nodiscard]]`, do not add error-value equality, and have tests assert on
`.code` rather than comparing whole error objects or diagnostic messages.

## Step 4: Make Engine Implement IGuitarInput

Extend `Engine`:

```cpp
class Engine : public ITransport, public IAudio, public IThumbnailFactory, public IGuitarInput
```

Implementation direction:

- initialize Tracktion/JUCE audio device support with input plus stereo output
- enumerate available `"ASIO"` input devices
- remember the user's selected ASIO device name and input channel in memory
- open the selected ASIO device when monitoring is enabled
- keep output stereo
- return typed errors when device selection or routing fails

The current constructor initializes output only:

```cpp
m_impl->m_engine->getDeviceManager().initialise(0, 2);
```

The first live-input version should move this to input plus output once the selected device type
and channel masks are known.

Do not persist the selected device/channel in this first slice. Persisting through
`EditorSettings` can come after the route is proven.

## Step 5: Add A Guitar Input Track

Keep the existing Tracktion arrangement/backing track intact.

Add a second Tracktion audio track for live guitar monitoring:

- backing track: existing wave clip for selected arrangement audio
- guitar track: live input monitoring

Transport controls should continue to control backing playback. Live input should remain available
while transport is stopped or playing unless the user explicitly disables it.

## Step 6: Route ASIO Input To The Guitar Track

Route the selected ASIO input channel to the live guitar track and enable monitoring.

First slice behavior:

- dry input only
- no plugins
- no recording unless Tracktion requires record-arm for monitoring
- no automation
- no input gain UI beyond a conservative default

Keep all Tracktion and JUCE device APIs private to `engine.cpp`.

## Step 7: Wire Minimal Editor UI

The editor app already owns one `common::audio::Engine`. Pass it through as `IGuitarInput` only
where needed.

Minimal UI:

- add an `ASIO Device` dropdown
- add an `Input Channel` dropdown
- add a `Live Guitar` toggle near transport controls
- changing the device refreshes available input channels
- selecting a device/channel calls `selectAsioInput(...)`
- toggling on calls `enableGuitarMonitoring()`
- toggling off calls `disableGuitarMonitoring()`
- show the typed error message when enabling fails

Because multiple ASIO devices may be connected, do not rely on the backend's default device for the
first real hardware test.

## Step 8: Tests

Add tests that do not require real ASIO hardware in CI.

Suggested coverage:

- compile-time test that `Engine` derives from `IGuitarInput`
- fake `IGuitarInput` tests around device/channel selection and monitor toggle behavior
- engine integration test that safely reports no live input available when no ASIO device exists

Avoid tests that require a physical interface or real ASIO driver to pass.

## Step 9: Manual Verification

On Windows with an ASIO interface:

1. Launch the editor.
2. Load a song.
3. Open the ASIO device dropdown.
4. Select the intended interface.
5. Select the intended guitar input channel.
6. Enable `Live Guitar`.
7. Confirm dry guitar is audible.
8. Press play.
9. Confirm backing track and guitar are audible together.
10. Pause, stop, and seek the backing track.
11. Confirm live guitar routing remains stable.
12. Disable `Live Guitar`.
13. Confirm live input stops.
14. Close the project and app.
15. Confirm device teardown is clean.

## Deferred Work

Do not include these in the first slice:

- persisted device settings
- input gain controls
- advanced device filtering or fallback driver modes
- VST plugin chain
- tone automation
- recording
- pitch detection
- latency calibration
- gameplay scoring input
- lock-free analysis ring buffer

These are natural follow-up features once the basic ASIO live-monitoring path is proven.
