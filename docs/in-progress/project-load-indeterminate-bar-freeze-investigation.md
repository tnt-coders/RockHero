# Project Load Indeterminate Bar Freeze Investigation

## Status

This is a static-code diagnosis for an intermittent UI freeze that is currently hard to reproduce.
The observed symptom is: while opening a project, the animated indeterminate busy bar momentarily
stops animating, then resumes and the project load continues.

This note is intentionally not a fix plan yet. It records the most likely code locations to review
when the problem can be reproduced again.

## Current Hypothesis

The freeze is most likely message-thread work running while the indeterminate `OpeningProject`
busy overlay is already visible. The progress animation is owned by JUCE's message thread, so any
synchronous Tracktion or audio setup on that thread will make the animated bar appear to pause.

The strongest suspected path is:

1. `EditorController::Impl::completeOpenProject()`
   - `rock-hero-editor/core/src/editor_controller.cpp`
   - Calls `loadSessionSong(...)` after the project worker has completed.
2. `EditorController::Impl::loadSessionSong(...)`
   - Calls `m_song_audio.prepareSong(song)`.
   - Calls `m_song_audio.setActiveArrangement(...)`.
3. `Engine::prepareSong(...)`
   - `rock-hero-common/audio/src/engine.cpp`
   - Reads each arrangement's backing audio duration through Tracktion audio-file handling.
4. `Engine::setActiveArrangement(...)`
   - Stops and releases Tracktion playback context.
   - Inserts the backing wave clip into the Tracktion edit.
   - Rebuilds the monitoring graph.

That entire handoff runs on the JUCE message thread after the worker-side package IO has returned.
If Tracktion audio-file probing, clip insertion, playback-context teardown, or monitoring-route
rebuild takes long enough, the indeterminate bar cannot tick until control returns to the message
loop.

## Less Likely For This Specific Symptom

Live-rig plugin restore can also block the message thread, but that usually happens after the busy
state has switched to determinate `LoadingLiveRig` progress. The relevant path is:

1. `EditorController::Impl::runLiveRigLoadStage(...)`
2. `EditorController::Impl::restoreLiveRig(...)`
3. `Engine::loadLiveRig(...)`
4. `Engine::Impl::executePluginStep()`

`executePluginStep()` can block during plugin lookup, plugin-state file reads, Tracktion plugin
insertion, and plugin load-error checks. However, the symptom described here is specifically the
animated indeterminate bar, so the pre-live-rig audio activation path above is the better first
target.

## What Would Confirm This

Add temporary timing logs around these message-thread calls:

- `completeOpenProject()` before and after `loadSessionSong(...)`.
- `loadSessionSong()` around `m_song_audio.prepareSong(song)`.
- `loadSessionSong()` around `m_song_audio.setActiveArrangement(...)`.
- `Engine::prepareSong(...)` around each `readAudioDuration(...)`.
- `Engine::setActiveArrangement(...)` around:
  - `stopTransportAndReleaseContext()`
  - `insertWaveClip(...)`
  - `rebuildInstrumentMonitoringGraph()`

If the freeze lines up with one of those durations, the issue is confirmed as message-thread
blocking inside project audio activation.

## What Not To Chase First

Do not start by changing `BusyOverlay` or the progress bar rendering. The overlay already paints
through the normal JUCE message loop. An animated JUCE progress bar freezing briefly is expected
when the message thread is occupied.

Do not start by changing Tracktion source. The suspected blocking calls are adapter-side usage of
Tracktion, not necessarily Tracktion bugs.

## Later Fix Directions

Possible fixes should be evaluated only after timing data identifies the actual slow call:

- split `loadSessionSong()` into smaller message-thread stages with paint-fenced yields;
- change `OpeningProject` to message-only during known blocking message-thread phases;
- move any safe, non-Tracktion file probing off the message thread;
- add permanent slow-operation timing logs for project load phases.

