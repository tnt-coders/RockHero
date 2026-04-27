# v20 Stage 11 - Editor Composition Wrapper

## Goal

Add `ui::Editor` as the fully wired editor feature so app code cannot create
half-wired controller/view objects.

## Expected Files

- `libs/rock-hero-ui/include/rock_hero/ui/editor.h`
- `libs/rock-hero-ui/src/editor.cpp`
- `libs/rock-hero-ui/CMakeLists.txt`
- `libs/rock-hero-ui/tests/test_editor.cpp`
- `libs/rock-hero-ui/tests/CMakeLists.txt`

## Implementation Steps

1. Add `ui::Editor`.
2. Store `EditorController` before `EditorView` so construction order is correct.
3. Constructor parameters:
   - `core::Session&`,
   - `audio::ITransport&`,
   - `audio::IEdit&`,
   - `const ThumbnailCreator&`.
4. Construct `m_controller(session, transport, edit)`.
5. Construct `m_view(m_controller, transport, create_thumbnail)`.
6. Call `m_controller.attachView(m_view)` after view construction finishes.
7. Expose only `juce::Component& component() noexcept`.
8. Do not retain the `ThumbnailCreator` after construction.
9. Pass the transport to `EditorView` as a const reference so the view can read
   live cursor position but cannot issue playback-control commands.

## Tests

Add a lightweight construction test with fakes if the JUCE test harness can construct
`EditorView`.

- `Editor` constructs with fakes,
- `component()` returns the view component,
- initial state is pushed during construction.
- the fake transport is supplied to `EditorView` construction as the cursor
  position source.

If constructing the full JUCE view is not practical, add a smaller automated test for
the composition invariant through a test-only lightweight view/controller pair or
defer the construction test with a documented blocker. Compile-only verification is
the fallback, not the target.

## Verification

Compile `rock-hero-ui`. If possible, compile the editor app after temporarily wiring
or including `ui::Editor`.

## Exit Criteria

- `ui::Editor` is the only object the app needs for the editor UI feature.
- Controller and view lifetimes are bound together.
- The controller receives mutable `ITransport` and `IEdit`; the view receives the
  same transport as a const live-position source for cursor rendering.
- There is no public `setController`, `attachThumbnail`, or half-wired construction
  sequence exposed to the app.

## Do Not Do

- Do not remove legacy app content yet.
- Do not split CMake targets.
- Do not move controller classes into a separate namespace.
