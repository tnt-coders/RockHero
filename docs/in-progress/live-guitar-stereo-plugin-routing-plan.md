# Live Guitar Stereo Plugin Routing Plan

## Goal

Keep the current live guitar path functional while planning the longer-term route for processed
guitar audio:

- mono ASIO guitar input remains the hardware source
- stereo output remains the monitoring target
- the current dry monitor stays available as the simplest known-good route
- future plugin processing receives a stereo-capable guitar signal
- future left/right effect differences are handled by the plugin routing, not by opening two
  unrelated hardware input channels

The current working behavior is acceptable for now:

```text
selected mono ASIO input -> dry monitor copy -> stereo output L/R
```

That should stay stable until the Tracktion-backed guitar track and plugin chain are ready.

## Current Baseline

The present implementation is an interim live monitor, not the final tone system.

It should do only these things:

- let the user select an ASIO device
- let the user select one mono guitar input channel
- open stereo output
- copy the selected mono input to left and right output
- let backing track playback continue through the same audio device

Do not add input gain, plugin slots, automation, or persistence just to improve this baseline.
Those belong in later slices once the simple route is proven stable.

## Key Routing Decision

The input should remain mono at the hardware boundary.

Stereo behavior should begin after the input enters the guitar processing route:

```text
mono guitar input
  -> mono-to-stereo expansion
  -> stereo-capable plugin chain or rack
  -> stereo output bus
```

This allows simple amp-sim chains, stereo delay/reverb, ping-pong effects, dual-amp tones, and
left/right split processing without pretending the guitar hardware source is stereo.

## Dry Monitor vs Processed Monitor

The current dry monitor should eventually become one of two explicit modes:

- `Dry Monitor`: direct input copied to stereo output for hardware sanity checks.
- `Processed Monitor`: input routed through the Tracktion guitar track and plugin chain.

When processed monitoring exists, avoid mixing both paths by accident. Hearing dry and processed
guitar together would make latency and tone debugging confusing. The UI can expose dry monitoring
later as a troubleshooting option, but processed monitoring should become the normal user path.

## Phase 1: Stabilize The Working Dry Monitor

Keep this phase intentionally small.

- preserve the current `IGuitarInput` device/channel/toggle behavior
- keep the input picker mono
- keep output stereo
- keep dry monitor gain at unity
- keep the callback-slot mapping compatible with JUCE ASIO's compact active-input buffer
- do not persist device settings yet unless repeated manual testing becomes painful

Manual verification remains the important test here because CI should not require ASIO hardware.

## Phase 2: Add A Real Guitar Track In Tracktion

Move the normal monitoring path from the direct JUCE callback into Tracktion.

Expected direction:

- keep the existing backing audio track as-is
- add a dedicated guitar input/effects track to the edit
- bind the selected ASIO input channel to that track's input device
- make the guitar track monitor live input while transport is stopped or playing
- keep recording disabled unless Tracktion requires record-arm for live monitoring
- keep the direct dry monitor disabled when this processed path is active

Before implementing, inspect Tracktion's wave input device and input-device-instance APIs rather
than guessing. The routing should use Tracktion's own graph where practical so plugin hosting and
automation attach naturally later.

## Phase 3: Define The Stereo Plugin Chain Shape

Start with one stereo-capable chain on the guitar track.

First useful behavior:

```text
mono input -> duplicate to L/R -> plugin chain -> stereo output
```

This supports common stereo plugins immediately. A mono amp sim can still output dual-mono, while
stereo delay, reverb, chorus, or widening plugins can create left/right differences after the mono
source.

Do not add a custom routing matrix in this phase. Let Tracktion's track and plugin graph carry the
basic stereo chain first.

## Phase 4: Add Explicit Left/Right Or Dual-Path Routing

Only add this after the single stereo chain works.

Possible routing models:

- `Single Stereo Chain`: default chain where plugins receive and output stereo.
- `Dual Mono Chains`: split the mono guitar into two branches, process each branch differently,
  and pan one left and one right.
- `Parallel Stereo Rack`: use Tracktion rack-style routing for more complex blends and wet/dry
  combinations.

This is where "different effects on the stereo field" should live. It should not change the ASIO
input picker, because the physical guitar input is still one mono source.

## Phase 5: Persist Tone And Device Intent Separately

Persist these as separate concerns:

- Device preference: app-local editor setting, because it depends on the user's hardware.
- Tone chain and automation: song or arrangement data, because it belongs to the authored content.

The durable song model already leaves `tone_timeline_ref` opaque to common/core. Use that boundary
for future Tracktion/JUCE tone data rather than teaching common/core about plugins.

## Phase 6: Add UI Around Real Audio State

Add controls only when backed by working routing:

- input meter for the selected dry input
- processed output meter for the guitar track
- plugin chain slots
- bypass controls
- dry/processed monitoring mode
- optional output pair picker if ASIO outputs `0/1` are not always the user's speakers

Avoid exposing advanced stereo routing before the normal processed-monitoring path is reliable.

## Phase 7: Prepare For Analysis And Gameplay

Keep pitch/scoring input separate from processed tone.

The analysis path should read clean pre-effects guitar input:

```text
mono ASIO input -> clean analysis buffer
mono ASIO input -> stereo plugin/tone route -> output
```

This prevents distortion, delay, and modulation from making pitch detection harder. It also keeps
latency calibration and scoring behavior independent from whatever tone chain the user hears.

## Testing Strategy

Keep hardware-free tests focused on project-owned behavior:

- editor controller tests for device selection and mode toggles
- view tests for picker and monitoring-mode UI intent wiring
- pure tests for future tone-chain DTOs or routing decisions
- adapter tests for "no ASIO device" and typed error behavior

Use manual Windows ASIO verification for the actual hardware route:

1. Select the intended ASIO device.
2. Select one guitar input channel.
3. Confirm dry monitor is audible.
4. Load a backing track and confirm both paths are audible.
5. Later, enable processed monitoring and confirm dry monitor is not duplicated.
6. Insert a stereo effect and confirm left/right output can differ.

## Non-Goals For Now

Do not add these yet:

- input gain control
- plugin scanning UI
- persisted device preferences
- tone package serialization
- left/right split routing
- recording
- pitch detection
- latency calibration

The immediate objective is still to keep live guitar audible. The next major objective is to route
that same mono input through a real Tracktion guitar track without regressing the current working
dry monitor.
