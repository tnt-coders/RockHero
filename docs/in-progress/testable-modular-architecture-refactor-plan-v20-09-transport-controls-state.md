# v20 Stage 09 - Transport Controls State

## Goal

Make `TransportControls` a simple state-rendering and intent-emitting widget with no
editor workflow policy.

## Expected Files

- `libs/rock-hero-ui/include/rock_hero/ui/transport_controls_state.h`
- `libs/rock-hero-ui/include/rock_hero/ui/transport_controls.h`
- `libs/rock-hero-ui/src/transport_controls.cpp`
- possible `libs/rock-hero-ui/tests/test_transport_controls.cpp`

## Implementation Steps

1. Add `TransportControlsState` with the enabledness and playing values the widget
   needs.
   Define it in its own public header, `rock_hero/ui/transport_controls_state.h`,
   rather than nesting it into `transport_controls.h`.
2. Replace public `std::function` callback members with a nested
   `TransportControls::Listener` interface.
3. Require `Listener&` in the constructor.
4. Add `setState(const TransportControlsState&)`.
5. Keep click handling local: button clicks call listener methods; they do not decide
   whether play, pause, or stop is semantically valid beyond button enabledness.
6. Keep `TransportControls` concrete inside `EditorView`; do not add
   `ITransportControls`.

## Tests

Add narrow widget tests if the JUCE test harness can construct the component.

- `setState(...)` updates enabledness,
- `setState(...)` switches play/pause icon state,
- play/pause click calls the listener,
- stop click calls the listener.

If JUCE component construction is not practical yet, extract the pure state-to-button
model decision into a small helper and test that helper in this stage. Compile-only
verification is acceptable only if neither path is practical; document why.

## Verification

Compile `transport_controls.cpp` and any current call sites. Run widget tests if
available.

## Exit Criteria

- No public callback members remain on `TransportControls`.
- `TransportControls` does not know about `audio::Engine`, `ITransport`, or
  `EditorController`.
- The old UI can still be adapted or temporarily shimmed until `EditorView` lands.

## Do Not Do

- Do not add `ITransportControls`.
- Do not move editor play/pause policy into the widget.
- Do not extract `EditorView` in this stage.
