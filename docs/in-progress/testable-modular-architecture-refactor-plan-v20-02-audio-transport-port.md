# v20 Stage 02 - Audio Transport Port

## Goal

Introduce the Tracktion-free transport boundary before changing `audio::Engine`
behavior. This gives later controller code a small project-owned interface to fake.

## Expected Files

- `libs/rock-hero-audio/include/rock_hero/audio/transport_state.h`
- `libs/rock-hero-audio/include/rock_hero/audio/i_transport.h`
- `libs/rock-hero-audio/tests/test_transport.cpp`
- `libs/rock-hero-audio/CMakeLists.txt`
- possible `libs/rock-hero-audio/tests/CMakeLists.txt`

## Implementation Steps

1. Add `audio::TransportState` with `playing`, `position`, and `duration`.
2. Use `core::TimePosition` and `core::TimeDuration`; do not use raw doubles in the
   transport snapshot.
3. Add `audio::ITransport` with:
   - nested `Listener`,
   - `play()`,
   - `pause()`,
   - `stop()`,
   - `seek(core::TimePosition)`,
   - `state()`,
   - `addListener(Listener& listener)`,
   - `removeListener(Listener& listener)`.

   Use `Listener&`, not `Listener*`, to match the v19 spec. References make the
   non-null, non-owning relationship explicit at the call site. The legacy
   `Engine::Listener` pointer-based registration may remain on `Engine` itself
   during migration but must not propagate into the new port.
4. Listener should expose one state-oriented callback:
   `onTransportStateChanged(const TransportState&)`.
5. Document that this public contract is message-thread owned for now.

## Tests

Add an automated contract test with a tiny fake `ITransport` implementation. This
does not test `Engine`; it verifies the new port is usable by future headless tests.

Cover:

- `TransportState` can be constructed with `core::TimePosition` and `core::TimeDuration`,
- a fake transport can store and return state through `ITransport::state()`,
- a listener can be registered by reference, notified, and removed by reference,
- `seek(core::TimePosition)` accepts the semantic wrapper type,
- the test file includes only the public port headers, not Tracktion headers.

## Verification

Run the new audio port test if the test target exists. If the test target does not
exist yet, add it in this stage. If local CMake remains unreliable, compile the test
source and any touched audio source from `compile_commands.json` and record the
missing full test run.

## Exit Criteria

- The new headers do not include Tracktion headers.
- `TransportState` contains no asset identity.
- `ITransport` contains no load/apply audio method.
- Existing editor behavior still builds through the old `Engine` API.

## Do Not Do

- Do not make `Engine` implement the port yet unless it is mechanically necessary.
- Do not delete `Engine::Listener`.
- Do not add `IPlaybackContent` in this stage.
