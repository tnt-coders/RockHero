# JUCE Audio Device Open Performance Review

Status: complete - further analysis is deferred until a real problem is noticed

Last reviewed: 2026-05-22

## Scope

This review covers the JUCE audio-device opening path used by Rock Hero's shared audio device
settings code. The goal is to identify clear performance issues in JUCE or in the way Rock Hero
has to drive JUCE, especially on Windows WASAPI/ASIO paths.

No source changes are proposed here as already-decided implementation. These are recommendations
to validate and prioritize after current audio-code edits settle.

## Summary

There are clear performance issues in the JUCE path. The most important ones are not all in the
final `AudioIODevice::open()` call. JUCE does substantial synchronous work earlier, especially while
constructing WASAPI devices just to inspect capabilities. Rock Hero can also end up paying for more
than one device construction/open during a backend switch because of the shape of
`juce::AudioDeviceManager`.

The current Rock Hero pre-close workaround avoids JUCE's obvious 1.5 second sleep in
`setCurrentAudioDeviceType()` when a device is already open, but that does not eliminate the broader
cost of backend switching or WASAPI capability probing.

## Findings

### Fixed Manager Sleep During Backend Switch

`juce::AudioDeviceManager::setCurrentAudioDeviceType()` closes the current device and then sleeps
for 1.5 seconds if a device is open.

Reference:
`external/tracktion_engine/modules/juce/modules/juce_audio_devices/audio_io/juce_AudioDeviceManager.cpp:750`

This is a clear performance trap. Rock Hero currently works around it by closing the device before
calling `setCurrentAudioDeviceType()`. That workaround should remain protected by tests or
instrumentation, because any future direct caller of `setCurrentAudioDeviceType()` while a device is
open will pay the full 1.5 second delay.

Recommendation:
Keep the pre-close behavior or replace it with a better one-shot backend switch. Do not allow UI
code or future settings services to call `setCurrentAudioDeviceType()` directly on an open manager.

### Backend Switch Can Open A Default Route Before The Requested Route

After changing the current backend type, `setCurrentAudioDeviceType()` builds a setup from
`lastDeviceTypeConfigs`, fills in default device names, and immediately calls
`setAudioDeviceSetup()`.

Reference:
`external/tracktion_engine/modules/juce/modules/juce_audio_devices/audio_io/juce_AudioDeviceManager.cpp:767`

Rock Hero then calls `setAudioDeviceSetup()` again with the user's staged setup. If JUCE's default
or last setup differs from the staged route, a backend switch can become:

1. Close old device.
2. Switch type.
3. Open JUCE's default/last route for that type.
4. Close that route.
5. Open the actual staged route.

That means avoiding the 1.5 second sleep still may leave an unnecessary open/probe cycle.

Recommendation:
Investigate a one-shot type-and-setup switch. JUCE's public API does not make this especially clean.
`AudioDeviceManager::initialise(..., preferredSetupOptions)` can set a setup in one call during
initialisation, but it should not be assumed safe as a live backend-switch substitute without
measurement and behavioral tests. If this proves to be a major cost, consider a small local JUCE
patch or wrapper API that sets the current type and requested setup without opening an intermediate
default route.

### Default Device Name Selection Can Create Temporary Devices

`AudioDeviceManager::insertDefaultDeviceNames()` creates temporary devices to compare supported
sample rates between input/output candidates.

Reference:
`external/tracktion_engine/modules/juce/modules/juce_audio_devices/audio_io/juce_AudioDeviceManager.cpp:594`

For WASAPI, `createDevice()` is not cheap. It initializes COM endpoint objects and probes device
capabilities. Therefore "just filling in defaults" can become expensive.

Recommendation:
Make sure Rock Hero enters JUCE manager calls with concrete input/output device names whenever
possible. Avoid paths that hand JUCE empty/default names during apply. Keep route-preview caching in
the shared settings code, and measure whether any remaining refresh path still rebuilds temporary
devices unnecessarily.

### WASAPI `createDevice()` Does Heavy Work Before `open()`

`WASAPIAudioIODeviceType::createDevice()` constructs a `WASAPIAudioIODevice` and immediately calls
`initialise()`.

References:
`external/tracktion_engine/modules/juce/modules/juce_audio_devices/native/juce_WASAPI_windows.cpp:1839`
`external/tracktion_engine/modules/juce/modules/juce_audio_devices/native/juce_WASAPI_windows.cpp:1226`

Inside initialization, JUCE re-creates a Windows audio endpoint enumerator, enumerates all active
endpoints, and searches for the cached input/output endpoint IDs.

Reference:
`external/tracktion_engine/modules/juce/modules/juce_audio_devices/native/juce_WASAPI_windows.cpp:1696`

This looks unnecessarily broad. JUCE already has the endpoint IDs from the scan. For a selected
route, using `IMMDeviceEnumerator::GetDevice(id)` should be cheaper than enumerating every active
endpoint again.

Recommendation:
As a JUCE patch experiment, replace the all-endpoint enumeration in `WASAPIAudioIODevice::createDevices()`
with direct `GetDevice()` calls for the selected input/output IDs. This should be one of the lower
risk framework-level performance experiments because it preserves the same endpoint identity model.

### WASAPI Capability Probing Is Very Expensive

`WASAPIDeviceBase` probes capabilities during construction. It queries buffer sizes, all supported
sample rates, and maximum channel count before the device is actually opened.

References:
`external/tracktion_engine/modules/juce/modules/juce_audio_devices/native/juce_WASAPI_windows.cpp:428`
`external/tracktion_engine/modules/juce/modules/juce_audio_devices/native/juce_WASAPI_windows.cpp:696`
`external/tracktion_engine/modules/juce/modules/juce_audio_devices/native/juce_WASAPI_windows.cpp:819`

The worst-looking part is `queryMaxNumChannels()`. It queries down from
`AudioChannelSet::maxChannelsOfNamedLayout`, which is 64, and for each channel count it tries every
known sample rate and several sample formats through `IAudioClient::IsFormatSupported()`.

References:
`external/tracktion_engine/modules/juce/modules/juce_audio_basics/buffers/juce_AudioChannelSet.h:558`
`external/tracktion_engine/modules/juce/modules/juce_audio_devices/audio_io/juce_SampleRateHelpers.cpp:38`

For shared WASAPI, sample-rate conversion can make the supported-rate list large. In the common
two-channel case, the max-channel search can therefore mean thousands of `IsFormatSupported()` calls
per endpoint during device construction. If both input and output are selected, this can happen
twice. This is a very plausible explanation for roughly one second of visible delay.

Recommendation:
Measure this directly, then try one or more local JUCE experiments:

- Cache WASAPI endpoint capabilities by endpoint ID and WASAPI mode.
- Avoid exhaustive 64-channel probing for Rock Hero, which currently needs a small number of input
  and output channels.
- In shared mode, avoid using every sample-rate-converted rate as input to the max-channel search.
- Defer expensive capability discovery until the UI actually needs that capability, or until apply.

The safest application-level version is capability caching. The fastest framework-level version is
likely limiting or removing the exhaustive max-channel search for this app's needs.

### WASAPI `open()` Has Small Fixed Waits, But They Are Not The Main Issue

The WASAPI open path starts a high-priority thread and sleeps for 5 ms. Client shutdown also has a
5 ms sleep for old Windows session callback behavior.

Reference:
`external/tracktion_engine/modules/juce/modules/juce_audio_devices/native/juce_WASAPI_windows.cpp:1366`

These waits are real but small. They do not explain a one-second delay by themselves. The larger
cost is more likely endpoint construction, probing, duplicate route creation, or a slow driver/OS
call under the probe loops.

Recommendation:
Do not focus first on the 5 ms sleeps. Time the constructor/probe path before trying to tune
`open()`.

### ASIO Has Conservative Driver Workarounds

The ASIO backend has several deliberate sleeps and handshakes. During device setup, JUCE creates
dummy buffers, starts the driver, sleeps for 80 ms, and stops the driver because some drivers expect
that sequence. During `open()`, JUCE waits for the first callback in 10 ms increments, up to 300
iterations.

References:
`external/tracktion_engine/modules/juce/modules/juce_audio_devices/native/juce_ASIO_windows.cpp:1205`
`external/tracktion_engine/modules/juce/modules/juce_audio_devices/native/juce_ASIO_windows.cpp:1279`
`external/tracktion_engine/modules/juce/modules/juce_audio_devices/native/juce_ASIO_windows.cpp:405`
`external/tracktion_engine/modules/juce/modules/juce_audio_devices/native/juce_ASIO_windows.cpp:588`

This is not necessarily a bug. ASIO drivers are historically inconsistent, and JUCE is being
defensive. However, it means ASIO timing can be heavily driver-dependent.

Recommendation:
Instrument ASIO separately from WASAPI. Do not remove the ASIO sleeps without testing real drivers.
If the app sees ASIO delay, first identify whether it is the constructor/open handshake or the
first-callback wait.

### DirectSound Is Not Worth Optimizing For Low Latency

DirectSound has high default buffering and rescans devices during open.

References:
`external/tracktion_engine/modules/juce/modules/juce_audio_devices/native/juce_DirectSound_windows.cpp:836`
`external/tracktion_engine/modules/juce/modules/juce_audio_devices/native/juce_DirectSound_windows.cpp:1127`

Recommendation:
Keep DirectSound sorted below ASIO/WASAPI, but do not spend much optimization effort there unless a
specific user need appears.

## Recommended Next Steps

1. Add temporary timing instrumentation before changing behavior.

   Measure the elapsed time for:

   - settings window construction/begin
   - `scanForDevices()`
   - route-preview `createDevice()`
   - `setCurrentAudioDeviceType()`
   - `setAudioDeviceSetup()`
   - WASAPI `createDevice()`
   - WASAPI `initialise()`
   - WASAPI `createDevices()`
   - `WASAPIDeviceBase` construction
   - `querySupportedSampleRates()`
   - `queryMaxNumChannels()`
   - WASAPI `open()`

   This should confirm whether the one-second delay is mostly duplicate opens, WASAPI capability
   probing, actual open/start, or async refresh work after apply.

2. Verify whether backend switching is still double-opening.

   Compare same-backend apply versus backend-switch apply. Log the route opened by
   `setCurrentAudioDeviceType()` before Rock Hero's staged setup is applied. If the intermediate
   route is common, prioritize a one-shot switch.

3. Try the low-risk WASAPI endpoint lookup patch.

   Replace all-endpoint enumeration with direct endpoint lookup by cached ID. This is likely less
   invasive than changing capability semantics and should be easy to benchmark.

4. Try capability caching or narrower capability probing.

   If `queryMaxNumChannels()` dominates, cache capabilities by endpoint ID and mode, or cap probing
   to Rock Hero's practical channel needs. For this app, exhaustive 64-channel probing is probably
   not buying meaningful user value.

5. Keep application-level safeguards.

   Continue avoiding JUCE's 1.5 second manager sleep. Continue keeping staged routes concrete and
   caching route previews. Avoid rebuilding preview devices for sample-rate or buffer-size-only
   changes.

## Risks

JUCE's Windows audio code contains workarounds for real driver and OS behavior. ASIO changes are
especially risky without hardware coverage. WASAPI changes are more promising, but exclusive mode
still requires accurate format validation. Any framework patch should be guarded by timing data and
tested across shared, low-latency shared, and exclusive WASAPI modes.

## Provisional Priority

Highest value:
Measure and reduce WASAPI construction/capability probing.

Second:
Eliminate intermediate default-route opens during backend switches.

Third:
Patch WASAPI endpoint lookup to avoid enumerating all endpoints during selected-route construction.

Lowest:
Tune small WASAPI sleeps or DirectSound behavior.
