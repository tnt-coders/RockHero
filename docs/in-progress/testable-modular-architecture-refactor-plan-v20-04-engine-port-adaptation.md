# v20 Stage 04 - Engine Port Adaptation

## Goal

Make `audio::Engine` implement the new transport and edit ports while
keeping legacy methods alive for existing UI code.

## Expected Files

- `libs/rock-hero-audio/include/rock_hero/audio/engine.h`
- `libs/rock-hero-audio/src/engine.cpp`
- possible audio adapter tests under `libs/rock-hero-audio/tests/` if the target is
  created in this stage

## Implementation Steps

1. Make `Engine` inherit `ITransport` and `IEdit`.
2. Implement `state()` from the same internal state used by existing playback UI.
3. Implement `seek(core::TimePosition)` and keep the existing `seek(double)` as a
   temporary compatibility wrapper if needed.
4. Implement `setTrackAudioSource(...)` by adapting the existing single-file load
   path.
5. Ensure a failed edit preserves existing audio content and returns `EditResult`
   with `applied == false`.
6. Introduce or consume the `EditResult` return type so `setTrackAudioSource(...)`
   returns both `applied` and the resulting `TransportState`.
7. Ensure a successful edit updates internal transport state synchronously and
   returns that snapshot through `EditResult`.
8. Keep transport listeners reserved for transport-driven changes. Do not make later
   controller code depend on edit work being reported through transport listeners.
9. Keep old public methods such as `loadFile(...)`, `isPlaying()`, and
   `getTransportPosition()` until UI migration stages no longer need them.
10. Add explicit TODO comments in the engine adapter near the most likely future
   integration points for project-owned edit history / undo-redo bridging. Do not
   implement that behavior in this stage.

## Tests

Add audio adapter tests. Prefer testing the real `Engine` because this stage's main
risk is the concrete adapter producing an inconsistent edit result or accidentally
leaking edit work through transport listeners.

- `Engine` reports transport state through `ITransport`,
- failed replacement load preserves existing content,
- successful edit returns the resulting transport state synchronously,
- edit-caused length, position, or playing changes do not invoke transport
  listeners.

If direct `Engine` construction is too heavy in this stage, extract the edit-result
or transport-publication decision into a small helper that the real `Engine` uses
and test that helper. Do not satisfy the listener rule with an unrelated test-only
fake adapter; that would only test the fake. The minimum acceptable fallback is:

- an automated helper test covering the semantics actually used by `Engine`,
- plus a compile-backed test proving `Engine` is usable through `ITransport&` and
  `IEdit&`.

If even that helper extraction is not practical, document the specific blocker and
keep a failing or skipped test TODO out of the tree.

## Verification

Run or compile the audio target. If the local CMake environment is unreliable, use
focused compile commands for `engine.cpp` and any new test source.

## Exit Criteria

- Later controller code can depend on `ITransport` and `IEdit`.
- Existing UI still works through legacy `Engine` methods.
- No Tracktion headers leak into the new port headers.

## Do Not Do

- Do not migrate UI code in this stage.
- Do not delete `Engine::Listener`.
- Do not implement undo/redo.
- Do not introduce dynamic multi-track playback semantics.
