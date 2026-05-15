# Tracktion Guitar Track And Plugin Chain Plan

## Goal

Move normal live guitar playback out of the direct JUCE dry-monitor callback and into a dedicated
Tracktion audio track so guitar input can run through VST plugin FX.

The target user-facing path is:

```text
selected mono ASIO input
  -> Tracktion guitar input device
  -> Guitar AudioTrack
  -> stereo-capable plugin chain
  -> master output
```

The current `juce::AudioIODeviceCallback` dry monitor remains useful as a diagnostic fallback, but
it should not be the normal path once processed monitoring works. Dry and processed monitoring
must not be mixed at the same time.

## Current State

The current engine has one Tracktion audio track for backing audio. Live guitar monitoring is a
separate `LiveGuitarMonitor` callback registered directly with JUCE's `AudioDeviceManager`.

That callback is intentionally simple:

- open the selected ASIO device
- enable one input channel
- copy callback input slot zero to stereo output
- let JUCE mix that buffer with Tracktion playback

This proved the ASIO device path, but it cannot host plugin FX or Tracktion automation. The next
step is to make Tracktion own the live guitar route.

## Target Architecture

`rock-hero-common/audio` remains the only place that knows about Tracktion and JUCE audio-device
APIs. Editor and game code continue to depend on project-owned ports and DTOs.

Engine state should evolve from one ambiguous "current audio track" to explicit tracks:

- backing track: loaded arrangement audio and waveform playback
- guitar track: selected live ASIO input and plugin chain

The guitar track should be present for the life of the edit. Device selection and plugin mutation
only reconfigure that track; they should not rebuild the whole engine unless Tracktion requires it.

## Public API Direction

Keep `IGuitarInput` focused on device selection and monitoring state:

- `availableAsioInputDevices()`
- `selectAsioInput(...)`
- `enableGuitarMonitoring()`
- `disableGuitarMonitoring()`
- `isGuitarMonitoringEnabled()`

Change the implementation of `enableGuitarMonitoring()` from dry callback monitoring to processed
Tracktion monitoring when the guitar track path is ready. Keep the public method name because the
caller intent is still "make the selected guitar input audible."

Add a separate plugin/tone port instead of expanding `IGuitarInput` into a large interface. A
likely shape is:

```cpp
class IToneChain
{
public:
    [[nodiscard]] virtual std::vector<PluginDescriptor> availablePlugins() const = 0;
    [[nodiscard]] virtual std::expected<TonePluginId, ToneChainError>
    addGuitarPlugin(const PluginDescriptorId& plugin_id) = 0;
    [[nodiscard]] virtual std::expected<void, ToneChainError>
    removeGuitarPlugin(TonePluginId plugin_id) = 0;
    [[nodiscard]] virtual std::expected<void, ToneChainError>
    moveGuitarPlugin(TonePluginId plugin_id, std::size_t index) = 0;
    [[nodiscard]] virtual std::vector<TonePluginSlot> guitarPluginChain() const = 0;
};
```

Use standard C++ DTOs in public headers. Keep `juce::PluginDescription`, `tracktion::Plugin`, and
Tracktion value trees private to `engine.cpp` or private implementation headers.

## Phase 1: Create A Durable Two-Track Edit

Replace the single-track assumption in `Engine::Impl`.

Implementation direction:

- keep `Edit::createSingleTrackEdit(...)` if it remains the easiest way to get a valid edit
- treat the initial audio track as the backing track
- append a second `AudioTrack` with `insertNewAudioTrack(TrackInsertPoint::getEndOfTracks(...))`
- store stable helpers or weak references for `backingTrack()` and `guitarTrack()`
- rename `currentAudioTrack()` to `backingTrack()` and update arrangement loading to use it
- ensure `clearActiveArrangement()` only clears backing clips, not the guitar track or FX chain

Open question for implementation:

- Confirm whether the guitar track should get Tracktion default track plugins immediately. If
  Tracktion expects volume/pan and meter plugins on normal audio tracks, keep them at the end of
  the chain and insert user FX before them.

Acceptance criteria:

- Existing backing-track playback still works.
- Loading and clearing arrangements no longer depend on "first audio track" semantics.
- The edit always has a dedicated guitar track available for later phases.

## Phase 2: Bind ASIO Input To The Guitar Track

Move live input assignment into Tracktion.

Implementation direction:

- keep the existing ASIO device enumeration and selection validation
- open the selected ASIO device through JUCE's `AudioDeviceManager` as the code does today
- keep only the selected hardware input channel enabled
- force that hardware input channel to mono in Tracktion's `DeviceManager`
- call `dispatchPendingUpdates()` so Tracktion rebuilds its `WaveInputDevice` list
- find the Tracktion `WaveInputDevice` whose channel index matches the selected input
- assign that input device instance to the guitar track's `itemID`
- set the input device monitor mode to always on for processed monitoring
- keep recording disabled unless Tracktion requires record-arm for live monitoring

Tracktion APIs to verify during implementation:

- `DeviceManager::getWaveInputDevices()`
- `WaveInputDevice::getChannels()`
- `EditInputDevices::getDevicesForTargetTrack(...)`
- `InputDeviceInstance::setTarget(...)`
- `InputDevice::setMonitorMode(InputDevice::MonitorMode::on)`

Important behavior:

- disable `LiveGuitarMonitor` before enabling Tracktion monitoring
- on failure, leave the previous working monitoring state intact where practical
- do all device and graph mutations on the message thread
- never add locks, allocations, or framework calls to the audio callback path

Acceptance criteria:

- Selecting an ASIO device and channel routes that input through the guitar track.
- The guitar remains audible while transport is stopped and while backing playback is running.
- Dry callback monitoring is off whenever processed Tracktion monitoring is on.

## Phase 3: Add A Minimal Internal FX Chain

Before external VST scanning, prove that the guitar track plugin path processes live input.

Implementation direction:

- add a small internal effect or Tracktion built-in plugin to the guitar track
- insert it into `guitarTrack().pluginList`
- place user-audible FX before final volume/pan/meter plugins if those defaults exist
- expose only enough temporary control to prove that changing the chain changes the heard signal

This phase should be short. Its purpose is to validate that live input reaches Tracktion's plugin
graph before adding external plugin scan and UI complexity.

Acceptance criteria:

- A known built-in effect can be inserted into the guitar track.
- The audible guitar signal changes when that effect is enabled or bypassed.
- Backing-track playback remains unaffected.

## Phase 4: Add External VST3 Plugin Discovery

Add a project-owned plugin catalog surface backed by Tracktion's plugin manager.

Implementation direction:

- initialize Tracktion's plugin format manager for VST3 support
- scan user-provided plugin directories from editor workflow or settings
- expose a stable project-owned `PluginDescriptor` list to editor UI/core
- include enough identity to recreate a Tracktion `ExternalPlugin`
- store scan failures as typed errors, not display-only strings
- avoid scanning on the audio thread or during active playback

Tracktion/JUCE APIs to verify during implementation:

- `PluginManager::pluginFormatManager`
- `PluginManager::knownPluginList`
- `PluginManager::createNewPlugin(...)`
- `ExternalPlugin::create(...)`
- `ExternalPlugin::initialiseFully()`
- `PluginList::insertPlugin(...)`

Acceptance criteria:

- The editor can scan a configured VST3 folder.
- The plugin list is visible through project-owned DTOs.
- A selected VST3 plugin can be instantiated and inserted on the guitar track.

## Phase 5: Expose Chain Editing

Add normal plugin-chain operations on the guitar track.

Implementation direction:

- add, remove, reorder, enable, and bypass guitar-track plugins
- stop or pause transport before operations that rebuild Tracktion's processing graph
- pre-initialize external plugins before exposing them as active in the chain
- keep stable project-owned IDs for plugin slots so UI selection does not depend on raw indices
- return typed `ToneChainError` values for load, missing plugin, incompatible plugin, and graph
  mutation failures

Initial UI should stay practical:

- plugin chain list
- add plugin
- remove plugin
- reorder plugin
- bypass plugin
- open plugin editor only after the backend path is stable

Acceptance criteria:

- A VST3 amp sim or effect can be inserted on the guitar track.
- The user hears processed guitar through that plugin.
- Removing or bypassing the plugin changes only the guitar track.
- No dry monitor is mixed with the processed signal.

## Phase 6: Add Parameter Access And Automation

Once plugins can process live input, add parameter read/write and automation.

Implementation direction:

- expose plugin parameters through project-owned DTOs
- set parameter values on the message thread
- serialize selected tone-chain and automation data into `Arrangement::tone_timeline_ref`
- keep `common/core` unaware of plugin formats and Tracktion data
- use Tracktion automation curves for time-based plugin changes
- avoid hard bypass switches; use short gain or mix ramps for audible transitions

Acceptance criteria:

- The editor can show a selected plugin's automatable parameters.
- Parameter changes affect the live guitar signal.
- A saved tone timeline can be loaded and applied to the guitar track.
- The game can load the same tone timeline read-only.

## Phase 7: Add Amp-Chain Blending

Support gradual transitions between complete tones, such as fading from one amp sim into another.

A plain serial plugin list is not enough for this:

```text
input -> Amp A -> Amp B -> output
```

Bypassing or fading plugins in that layout does not produce two independent amp tones. Amp B is
processing Amp A's output, so the result is not equivalent to crossfading between two rigs.

The preferred single-track solution is a Tracktion rack on the guitar track:

```text
guitar track
  -> rack
       input -> Chain A -> Gain A -> output
       input -> Chain B -> Gain B -> output
```

Then automate a `Blend` macro that drives the two branch gains in opposite directions:

- `Blend = 0.0`: Chain A at unity, Chain B muted
- `Blend = 0.5`: both chains partially audible according to the selected crossfade law
- `Blend = 1.0`: Chain A muted, Chain B at unity

Use equal-power crossfades by default for perceptual smoothness. Keep a linear option only if it
proves useful for authored effects.

Tracktion APIs to verify during implementation:

- `RackType::addPlugin(...)`
- `RackType::addConnection(...)`
- `RackInstance::create(...)`
- rack macro parameters through `MacroParameterList`
- `AutomatableParameter::addModifier(...)`
- `AutomationCurve::addPoint(...)`

Fallback implementation:

- use two guitar `AudioTrack`s monitoring the same ASIO input
- put one amp chain on each track
- automate track output gain or VCA gain in opposite directions

The two-track approach is simpler and close to the REAPER model, but the rack approach keeps the
normal guitar feature as one track with one input assignment and one output route. The public tone
model should describe "chains" and "blend automation" rather than exposing whether the backend uses
one rack or multiple Tracktion tracks.

Acceptance criteria:

- The editor can define two guitar chains and a blend automation curve.
- Playback crossfades between the two complete tones without enabling the dry monitor path.
- The saved tone timeline can reload the chain layout and blend curve.
- The game can play the authored blend read-only.

## Phase 8: Preserve A Clean Analysis Path

Do not make pitch/scoring depend on processed plugin output.

Future gameplay should split the selected mono input into two conceptual paths:

```text
mono ASIO input -> clean analysis buffer
mono ASIO input -> Tracktion guitar track -> plugin FX -> output
```

The analysis path should tap clean pre-effects samples. Distortion, delay, modulation, and
time-based FX should not feed pitch detection or note matching.

## Testing Strategy

Hardware-free tests:

- editor controller tests for plugin-chain intents and state publishing
- DTO and error-domain tests for tone-chain contracts
- adapter tests for no-ASIO and invalid-selection failures
- tests that ensure dry and processed monitoring modes cannot both be active in project state

Manual Windows ASIO verification:

1. Select an ASIO device and one mono guitar input channel.
2. Enable processed monitoring.
3. Confirm dry monitor is disabled.
4. Confirm guitar is audible through the Tracktion guitar track while stopped.
5. Start backing playback and confirm guitar plus backing are audible together.
6. Insert a simple effect and confirm the guitar changes.
7. Insert a VST3 amp/effect plugin and confirm processed monitoring still works.
8. Bypass/remove the plugin and confirm the guitar path updates without app restart.

## Non-Goals For The First Processed Path

Do not include these in the first Tracktion guitar-track slice:

- recording
- persisted hardware-device preference
- plugin editor windows
- advanced stereo split routing
- rack-based parallel routing
- pitch detection
- latency calibration UI
- tone timeline package compatibility guarantees

The first useful milestone is narrower: selected ASIO input reaches a Tracktion guitar track, runs
through one plugin, and is audible with the backing track without the dry callback path mixed in.

## Risks And Watch Points

- Tracktion input devices are rebuilt after audio-device changes, so stored raw pointers must be
  refreshed after `dispatchPendingUpdates()`.
- ASIO callback input slot numbers are compacted by JUCE, while Tracktion `WaveInputDevice`
  channel indices refer to hardware channels. Keep this mapping explicit.
- Some plugins may allocate or initialize lazily. Pre-initialize on the message thread before
  enabling the chain.
- Plugin graph mutations during playback may produce glitches or invalid Tracktion state. Stop or
  pause before mutation until a safer graph-update policy is proven.
- Dry and processed monitoring together will sound like latency/phase problems. Treat that as an
  invalid state except for a deliberate future diagnostic mode.
