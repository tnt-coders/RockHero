# Project Load Indeterminate Bar Freeze Investigation

## Status

Completed on 2026-06-22.

The original symptom was a startup project restore where the indeterminate busy bar briefly stopped
animating while opening the default project. The confirmed cause was not project package IO, song
audio activation, or live-rig restore. It was repeated no-op live-input gate disables on the JUCE
message thread.

## Root Cause

Startup restore calls `EditorController::Impl::applyLiveInputGate()` before submitting the project
open worker. That method disables calibration monitoring and live-input monitoring as a safety gate.
For the reproduced project, both monitoring modes were already off.

Before the fix, each no-op disable still reached
`Engine::Impl::setMonitoringChannelEnabled(...)` and rebuilt the Tracktion instrument monitoring
graph. Each rebuild could allocate a Tracktion playback context and cost roughly 360 ms. Together,
the two no-op disables kept the message thread busy for about 729 ms before the project-open worker
was even submitted, so the indeterminate progress bar could not animate smoothly.

## Fix

`Engine::Impl::setMonitoringChannelEnabled(...)` now compares the requested monitoring flags with
the current flags before rebuilding Tracktion routing.

- If a no-input-device request forces monitoring off and monitoring was already off, the method
  skips the graph rebuild and still reports `InputRouteUnavailable`.
- If the requested monitoring flags match the current flags, the method returns success immediately.
- Real enable/disable transitions still update state and rebuild routing as before.

## Validation

Temporary timing logs identified the slow path:

- Before the fix, `applyLiveInputGate()` took about 729 ms during startup project restore.
- Each no-op monitoring disable took about 360 ms.
- `prepareSong(...)`, `setActiveArrangement(...)`, and `loadSessionSong(...)` were all around
  0-13 ms and were not the visible freeze source.

After the fix, the same default project restore showed:

- calibration monitoring disable: 0 ms
- live-input monitoring disable: 0 ms
- `applyLiveInputGate()`: 0 ms

The startup MIDI device scan still occurs later, but it did not reproduce the loading-bar freeze
once the no-op monitoring rebuilds were removed.
