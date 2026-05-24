# Live Rig Plus Minus 24 dB Gain Plugin Plan

## Status

Near-term follow-up to the live-rig gain and meter work. This plan narrows the implementation for
supporting input and output gain from `-24.0 dB` to `+24.0 dB` without modifying Tracktion Engine
internals and without stacking multiple hidden Tracktion faders.

## Why This Exists

The current live-rig input and output gain controls use hidden Tracktion `VolumeAndPanPlugin`
instances. That plugin is a DAW fader and maps its maximum fader position to `+6.0 dB`, so asking
it for values above `+6.0 dB` is clamped by Tracktion before audio processing.

Rock Hero's signal-chain input and output controls are not really mixer faders. They are closer to
trim points around the guitar effects chain:

- input gain is before the user's plugin chain;
- output gain is after the user's plugin chain;
- the adjacent meters are post-fader clipping indicators;
- the final mix meter remains global and includes backing audio plus processed guitar.

JUCE provides `juce::dsp::Gain`, which can apply this range, but it is a DSP utility rather than a
loadable Tracktion plugin. The clean design is therefore a small Rock Hero-owned structural
Tracktion plugin that wraps simple gain DSP behind the existing live-rig audio boundary.

## Goal

Replace the hidden `VolumeAndPanPlugin` gain stages with a private Rock Hero structural gain plugin
that supports the project-owned live-rig gain range of `-24.0 dB` to `+24.0 dB`.

Keep the external behavior unchanged except for the symmetric trim range:

- one input gain value;
- one output gain value;
- tone-document persistence of those two values;
- input and output meters immediately after their faders;
- no pan, no mixer, and no master fader.

## Design Direction

- Keep Tracktion and JUCE DSP details private to `rock-hero-common/audio/src`.
- Keep `common::audio::Gain` as the single project-owned gain value and clamp policy.
- Change the shared gain range from Tracktion's fader-shaped behavior to a symmetric
  `-24.0 dB` to `+24.0 dB` guitar trim range rather than special-casing the UI.
- Add a private `LiveRigGainPlugin` Tracktion plugin helper in `rock-hero-common/audio/src`.
- Create the plugin through the adapter's private `EngineBehaviour::createCustomPlugin()` hook
  rather than registering a general built-in plugin type for user browsing.
- Keep the structural plugin hidden from `LiveRigPlugin`, signal-chain rows, and capture sidecars.
- Keep the existing structural order:
  `input gain -> input meter -> user plugins -> output gain -> output meter`.
- Keep gain changes message-thread initiated, with audio-thread processing reading only atomic or
  already-prepared realtime state.
- Do not modify Tracktion Engine source and do not enable optional AirWindows plugins for this.

## Proposed Private Plugin Shape

Add a small Tracktion plugin implementation, likely split out of `engine.cpp` into private adapter
files:

- `rock-hero-common/audio/src/live_rig_gain_plugin.h`
- `rock-hero-common/audio/src/live_rig_gain_plugin.cpp`

The type should be private to the audio adapter target and can be included by adapter tests through
the existing private test include directory.

Expected responsibilities:

1. Inherit from `tracktion::Plugin`.
2. Own a `gainDb` state property so the plugin is inspectable during its lifetime.
3. Expose simple project-facing methods:
   - `setGain(common::audio::Gain gain)`;
   - `common::audio::Gain gain() const`.
4. Store the audio-thread target gain in `std::atomic<float>`.
5. Convert dB to linear gain inside `applyToBuffer()`.
6. Smooth changes with `juce::SmoothedValue<float>` or `juce::dsp::Gain<float>`.
7. Apply the same gain ramp to every audio channel without advancing the ramp separately per
   channel.
8. Avoid locks, heap allocation, file IO, UI calls, and Tracktion graph mutation from
   `applyToBuffer()`.

Prefer a narrow class over a generic reusable plugin framework. The first version only needs to
serve the hidden live-rig input/output trim points.

## Implementation Steps

### Step 1: Raise the Domain Gain Range

Update `rock-hero-common/audio/include/rock_hero/common/audio/gain.h`:

1. Change `maximumGainDb()` to return `24.0`.
2. Change `minimumGainDb()` to return `-24.0`.
3. Keep `defaultGainDb()` at `0.0`.
4. Update gain tests so `-24.0 dB` and `+24.0 dB` are accepted and values outside the range clamp.

This makes the UI, editor controller, tone loading, and audio adapter share one range.

### Step 2: Add the Private Live Rig Gain Plugin

Add `LiveRigGainPlugin` under `rock-hero-common/audio/src` with no public header exposure.

The implementation should:

1. Use a private type string such as `rockHeroLiveRigGain`.
2. Construct a valid Tracktion plugin `ValueTree` with a `gainDb` property.
3. Apply `common::audio::clampGain()` in `setGain()`.
4. Write the clamped dB value to both plugin state and an atomic target.
5. Read the atomic target in `applyToBuffer()`.
6. Initialize smoothing in `initialise()` using Tracktion's sample rate.
7. Make bypass/disabled behavior straightforward: disabled means no gain change.
8. Return the input channel count from `getNumOutputChannelsGivenInputs()`.

The first implementation should not expose an automatable Tracktion parameter. Rock Hero's current
input/output gain controls are explicit editor intents, not timeline automation lanes.

Wire it through `RockHeroEngineBehaviour::createCustomPlugin()` so Tracktion's plugin cache can
own and re-resolve the structural plugin from its ValueTree, while still keeping it out of the
general built-in plugin list.

### Step 3: Replace Structural Gain Helpers in `Engine`

Replace the current structural gain helpers that search for and create `VolumeAndPanPlugin`
instances:

1. `findStructuralGainPlugin()` should find `LiveRigGainPlugin`.
2. `createVolumeAndPanPlugin()` should become a helper that creates `LiveRigGainPlugin`.
3. `readGainFromPlugin()` should call `LiveRigGainPlugin::gain()`.
4. `applyGainToPlugin()` should call `LiveRigGainPlugin::setGain()`.
5. Preserve structural plugin IDs, ordering, hiding, and graph rebuild behavior.
6. Keep existing meter plugin placement unchanged.

Do not change the public `ILiveRig` shape for this range increase.

### Step 4: Keep Persistence Compatible

Tone documents already store `inputGainDb` and `outputGainDb`. Keep that format.

1. Existing tone documents with missing gain fields still default to `0.0 dB`.
2. Existing tone documents with values inside `-24.0 dB` to `+24.0 dB` should now load as authored.
3. Values outside `-24.0 dB` to `+24.0 dB` should clamp to the nearest bound.
4. Capture should write the value returned by the private gain plugin, not Tracktion fader state.

No package migration should be needed because the tone-document fields are already project-owned
dB values.

### Step 5: Update UI Expectations

The signal-chain sliders already read `minimumGainDb()` and `maximumGainDb()`. After Step 1, they
should naturally present `+24.0 dB` and `-24.0 dB`.

Update focused UI tests where needed:

1. Slider maximum is `maximumGainDb()`.
2. Double-click reset remains `0.0 dB`.
3. Slider labels remain `Input` and `Output`.
4. Input and output meters remain post-fader.

Do not add new UI controls for this change.

## Testing Strategy

Prefer small tests at the boundary where behavior changes.

1. `test_gain.cpp`
   - `-24.0 dB` and `+24.0 dB` pass through.
   - values outside the range clamp to the shared bounds.
2. Private gain plugin test
   - `setGain(Gain{24.0})` reads back `24.0`.
   - out-of-range values clamp to `minimumGainDb()` or `maximumGainDb()`.
   - processing a deterministic buffer at `+24.0 dB` multiplies samples by approximately
     `juce::Decibels::decibelsToGain(24.0f)` after smoothing is settled or disabled for the test.
3. `test_engine.cpp`
   - `ILiveRig::setInputGain(Gain{24.0})` and `setOutputGain(Gain{-24.0})` read back as authored.
   - capture and load round-trip the symmetric range.
   - structural plugins remain hidden from the external plugin chain.
   - structural ordering remains input gain, input meter, user plugins, output gain, output meter.
4. Editor controller tests
   - `+24.0 dB` is accepted and sent through the live-rig boundary.
   - out-of-range values clamp through the shared gain policy.
5. UI tests
   - signal-chain sliders use the shared maximum and still reset to `0.0 dB`.

Run the focused audio and UI test targets first. Run the broader debug test preset if the focused
targets pass.

## Acceptance Criteria

- Input and output sliders can be set from `-24.0 dB` to `+24.0 dB`.
- `+24.0 dB` produces an audible gain close to 24 dB, not Tracktion's old `+6.0 dB` cap.
- Values outside `-24.0 dB` to `+24.0 dB` clamp consistently across UI, controller,
  persistence, and adapter code.
- Double-click reset still returns both sliders to `0.0 dB`.
- Input and output meters remain post-fader.
- The final mix meter remains in the global transport/status area.
- No Tracktion Engine source files are modified.
- No hidden stacked `VolumeAndPanPlugin` workaround is introduced.
- No user-visible plugin row appears for the structural gain points.

## Non-Goals

- No pan control.
- No master output fader.
- No project mixer.
- No backing-track gain UI.
- No gain automation lanes.
- No generic Tracktion built-in plugin browser.
- No optional AirWindows dependency.
- No persistence format change beyond accepting the wider dB range.
- No broad extraction of all live-rig backend code as part of this small range change.
