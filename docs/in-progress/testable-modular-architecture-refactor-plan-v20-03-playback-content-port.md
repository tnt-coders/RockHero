# v20 Stage 03 - Playback Content Port

## Goal

Separate audio asset application from transport control before adapting the concrete
engine.

## Expected Files

- `libs/rock-hero-audio/include/rock_hero/audio/i_playback_content.h`
- `libs/rock-hero-audio/tests/test_playback_content_port.cpp`
- `libs/rock-hero-audio/CMakeLists.txt`
- possible `libs/rock-hero-audio/tests/CMakeLists.txt`

## Implementation Steps

1. Add `audio::IPlaybackContent`.
2. Add `applyTrackAudio(core::TrackId, const core::AudioAsset&) -> bool`.
3. Document that the first implementation is single-track-applied:
   - only the most recently applied track is required to behave correctly,
   - applying to a second track id is unspecified until stem playback lands,
   - this is tied to the current `TransportState.duration` semantics.
4. Document that `applyTrackAudio(...)` must not invoke transport listeners for any
   transport-state change caused by the apply operation.
5. Document that successful apply updates `transport.state()` synchronously so the
   initiating controller can read the new state immediately after the call.

## Tests

Add an automated contract test with a tiny fake `IPlaybackContent` implementation.
This proves controller tests can use the port without Tracktion.

Cover:

- a fake implementation receives `core::TrackId`,
- a fake implementation receives `core::AudioAsset`,
- return values can represent success and failure,
- the test includes only public core/audio port headers, not concrete `Engine` or
  Tracktion headers.

## Verification

Run the new audio port test if possible. If local CMake remains unreliable, compile
the test source and touched headers through a focused compile path and record the
missing full test run.

## Exit Criteria

- Asset loading is represented by `IPlaybackContent`, not `ITransport`.
- The listener-suppression rule is explicit in the header documentation.
- Existing editor behavior still builds through old `Engine::loadFile(...)`.

## Do Not Do

- Do not add a thumbnail factory.
- Do not implement multi-stem playback.
- Do not remove `Engine::loadFile(...)` yet.
