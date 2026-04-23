# v20 Stage 04 - Engine Port Adaptation

## Goal

Make `audio::Engine` implement the new transport and playback-content ports while
keeping legacy methods alive for existing UI code.

## Expected Files

- `libs/rock-hero-audio/include/rock_hero/audio/engine.h`
- `libs/rock-hero-audio/src/engine.cpp`
- possible audio adapter tests under `libs/rock-hero-audio/tests/` if the target is
  created in this stage

## Implementation Steps

1. Make `Engine` inherit `ITransport` and `IPlaybackContent`.
2. Implement `state()` from the same internal state used by existing playback UI.
3. Implement `seek(core::TimePosition)` and keep the existing `seek(double)` as a
   temporary compatibility wrapper if needed.
4. Implement `applyTrackAudio(...)` by adapting the existing single-file load path.
5. Ensure a failed apply preserves existing audio content and returns `false`.
6. Ensure a successful apply updates internal transport state synchronously.
7. Suppress all transport-listener callbacks caused by work inside
   `applyTrackAudio(...)`.
8. Keep old public methods such as `loadFile(...)`, `isPlaying()`, and
   `getTransportPosition()` until UI migration stages no longer need them.

## Tests

Add audio adapter tests. Prefer testing the real `Engine` because this stage's main
risk is the concrete adapter accidentally firing transport listeners from
`applyTrackAudio(...)`.

- `Engine` reports transport state through `ITransport`,
- failed replacement load preserves existing content,
- successful apply updates `state()` synchronously,
- apply-caused length, position, or playing changes do not invoke transport
  listeners.

If direct `Engine` construction is too heavy in this stage, extract the
listener-suppression decision into a small helper that the real `Engine` uses and
test that helper. Do not satisfy the listener-suppression requirement with an
unrelated test-only fake adapter; that would only test the fake. The minimum
acceptable fallback is:

- an automated helper test covering suppression semantics actually used by `Engine`,
- plus a compile-backed test proving `Engine` is usable through `ITransport&` and
  `IPlaybackContent&`.

If even that helper extraction is not practical, document the specific blocker and
keep a failing or skipped test TODO out of the tree.

## Verification

Run or compile the audio target. If the local CMake environment is unreliable, use
focused compile commands for `engine.cpp` and any new test source.

## Exit Criteria

- Later controller code can depend on `ITransport` and `IPlaybackContent`.
- Existing UI still works through legacy `Engine` methods.
- No Tracktion headers leak into the new port headers.

## Do Not Do

- Do not migrate UI code in this stage.
- Do not delete `Engine::Listener`.
- Do not introduce dynamic multi-track playback semantics.
