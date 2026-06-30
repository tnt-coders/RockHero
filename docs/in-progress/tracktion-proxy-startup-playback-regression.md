# Tracktion Proxy Startup Playback Regression

## Status

Fix implemented. `Engine::setActiveArrangement(...)` now calls `wave_clip->setUsesProxy(false)`
on the inserted backing clip (`rock-hero-common/audio/src/engine.cpp`). This note is retained as the
rationale of record. The analysis below was verified against the vendored Tracktion source.

## Symptom

Between `419f603016e589be59decf23c7f76786f4ce40a2` and
`10b43a481f175ebe622100e99d95d83faa61dc7e`, opening a project in the new JSON
package format can leave playback temporarily silent if Space is pressed immediately after the
open finishes.

Observed behavior:

- The timeline starts scrolling immediately.
- Backing audio is silent for a short period.
- Audio eventually starts.
- During startup restore, the cursor can briefly freeze and playback can pause. Pressing Space
  again resumes with audio.

The strongest current reproduction clue is that the affected project uses `.ogg` backing audio
instead of `.wav`.

## Rejected Root Cause: Temp Workspace Extraction

The new JSON arrangement document format is not the first code path to extract `.rhp` files into a
temporary project workspace.

The old project-opening code at `419f603016e589be59decf23c7f76786f4ce40a2` already did this:

1. Create a temporary project workspace.
2. Extract the `.rhp` archive into that workspace with `extractArchiveToWorkspace(...)`.
3. Read `project.json`.
4. Read the nested song package directory from the extracted workspace.
5. Keep the workspace alive for the open project because loaded audio paths point into it.

So the fresh temp path is a contributing condition, not the regression by itself. Tracktion still
sees a new absolute file path for packaged backing audio on each open, but that was also true before
the JSON arrangement migration.

## Tracktion Evidence

Rock Hero inserts the selected backing audio in
`rock-hero-common/audio/src/engine.cpp`:

- `Engine::setActiveArrangement(...)` stops and frees Tracktion playback context.
- It inserts a `tracktion::WaveAudioClip` with `track->insertWaveClip(...)`.
- It sets clip gain from normalization metadata.
- It sets `m_loaded_length_seconds`, disables looping, seeks to zero, and rebuilds monitoring.
- It does not override Tracktion's default proxy policy for the inserted clip.

Tracktion defaults newly inserted audio clips to proxy-enabled:

- `tracktion::EngineBehaviour::ClipDefaults::useProxyFile` defaults to `true`.
- `tracktion::AudioClipBase::proxyAllowed` defaults to `true`.
- `tracktion::ClipOwner` applies `getClipDefaults().useProxyFile` when a new clip has no explicit
  `proxyAllowed` property.

Tracktion chooses the clip playback file in `AudioClipBase::getPlaybackFile()`:

- If proxies are enabled and the file is time-stretched, it returns a proxy path.
- If proxies are enabled and `AudioFileInfo::needsCachedProxy` is true, it also returns a proxy
  path.
- `AudioFileInfo::needsCachedProxy` is false for WAV, AIFF, and Tracktion float audio. It is true
  for formats such as `.ogg`.

Tracktion proxy generation is asynchronous:

- `AudioClipBase::beginRenderingNewProxyIfNeeded()` schedules proxy creation.
- `AudioClipBase::timerCallback()` starts a proxy job when needed.
- When the proxy state changes, Tracktion calls `Selectable::changed()` and `edit.restartPlayback()`.

Tracktion playback can be silent while the proxy reader is unavailable. The live graph builds
`WaveNodeRealTime`, not the older `WaveNode` (`tracktion_EditNodeBuilder.cpp` selects
`makeNode<WaveNodeRealTime>`):

- `WaveNodeRealTime` is built from `clip.getPlaybackFile()`.
- If that file is a not-yet-created proxy, `buildAudioReaderGraph()` cannot create a reader and
  returns false, so the node is not ready and emits no audio.
- During realtime playback it does not block waiting for the async proxy render.
- This is the same `WaveNodeRealTime` whose `buildAudioReaderGraph()` streams compressed sources
  directly through `BufferedAudioFileManager` when proxies are off (`tracktion_WaveNode.cpp`). That
  buffered-streaming path is exactly what makes proxy-off `.ogg` playback work, so disabling the
  proxy is a supported first-class path, not a fallback hack.

That combination matches the observed failure:

1. Rock Hero reports the project open as complete.
2. User immediately presses Space.
3. Tracktion transport starts and the cursor advances.
4. The backing clip points at a proxy file that is still being generated.
5. Audio is silent until the proxy becomes readable.
6. Tracktion restarts playback when proxy generation completes, causing the visible freeze/pause.

## Why `.ogg` Matters

WAV backing audio usually does not enter the cached-proxy path, so it tends to play directly.

OGG backing audio does enter the cached-proxy path when proxies are enabled. Because packaged audio
is extracted into a fresh temp workspace on every open, the proxy cache is unlikely to already be
ready for that exact file path. Pressing Space immediately after open can therefore race Tracktion's
first-use proxy generation.

## Surgical Fix (implemented)

Rock Hero disables Tracktion proxy generation for the backing audio clip it owns.

In `Engine::setActiveArrangement(...)`, immediately after a successful `insertWaveClip(...)` and its
null check, the engine calls:

```cpp
wave_clip->setUsesProxy(false);
```

The backing track is a single user-selected song file, not an editor clip that needs Tracktion's
offline proxy workflow. Disabling proxies keeps playback on Tracktion's source-reader path.

For compressed formats whose `AudioFileInfo::needsCachedProxy` is true, Tracktion's realtime
non-proxy path uses `WaveNodeRealTime` and `BufferedAudioFileManager` to read from the original
source file instead of waiting on an async proxy file.

### Why this also matches the planned practice-speed feature

Practice-speed playback (slowing down or speeding up the whole backing track to play guitar along
with it) time-stretches this same clip. Proxy-off is not just compatible with that feature, it is
the correct backend for it:

- `WaveNodeRealTime::buildAudioReaderGraph()` already builds a realtime elastique
  `TimeStretchReader` (and `PitchAdjustReader`) over the original source. So whole-clip speed change
  with preserved pitch happens live, on the source file, with no proxy required.
- If the proxy were left on, every speed change would alter `AudioClipBase::getProxyHash()` (it
  mixes in `getSpeedRatio()`, `pitchChange`, length, and loop), invalidating the proxy and
  scheduling a fresh asynchronous offline render that ends in `edit.restartPlayback()`. Dragging a
  speed control would stall and drop audio repeatedly -- the same defect class as this regression,
  amplified.
- At 1x (`timeStretchMode == disabled`) the realtime chain skips elastique entirely and just
  streams the source, so the only added cost is realtime elastique while the user is actually
  slowed or sped up -- negligible for a single backing track.

To preserve pitch when slowed, the practice-speed feature must set a non-`disabled`
`timeStretchMode` (an elastique mode) on the clip and drive a speed ratio (or auto-tempo); this is a
feature concern, not part of this fix. Note that tempo-map grid alignment does not stretch the
source audio; only the explicit practice-speed control engages elastique.

## Impact

User-visible change:

- Pressing Space immediately after opening an `.ogg` project produces audio immediately.
- Tracktion does not restart backing playback later because a generated proxy became available.
- WAV behavior is unchanged.

Code impact:

- One backend-line policy change in `rock-hero-common/audio/src/engine.cpp`.
- No public audio API change.
- No project file format change.
- No editor UI gate or artificial delay.

## Verification

Automated checks (follow-up; not yet added):

- Add an audio integration test that verifies Rock Hero's active backing clip is inserted with
  proxies disabled if that can be observed without exposing Tracktion types publicly.
- Run `rock_hero_common_audio_tests`.
- Run `rock_hero_editor_core_tests` if controller behavior is touched.

Manual acceptance (still required to confirm the fix on a real `.ogg` project):

- Open a project whose packaged backing audio is `.ogg`.
- Press Space immediately after open completes.
- Confirm the cursor starts with audible backing audio.
- Restart the app with that project as the last-open project.
- Press Space immediately after startup restore completes.
- Confirm there is no silent scrolling and no later freeze/pause caused by Tracktion proxy
  completion.

If `.ogg` still reproduces after disabling proxies, the next smallest backend investigation is a
bounded reader warm-up before `setActiveArrangement(...)` reports success. That should only be done
if the proxy policy fix does not close the race.
