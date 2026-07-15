\page guide_add_view Adding an Editor UI View

*Applies to: Editor-only.*

Use this checklist for a new JUCE component in `rock-hero-editor/ui`. The governing rule (from
\ref design_architectural_principles, "UI Modules"): a component renders already-derived state
and emits intents. If a component is getting smart, the intelligence moves out — usually into a
view-state derivation or an editor action.

`TransportControls` (`rock-hero-editor/ui/src/transport/transport_controls.h`) is a clean,
small exemplar of every convention below.

# The pattern

1. **Nested `Listener`.** The component declares `class Listener` with pure-virtual intent
   methods (`onXxxPressed()`), takes a `Listener&` in its constructor, and calls it from gesture
   handlers. No callbacks reach into controllers or ports directly.
2. **`setState(const core::XyzViewState&)`.** The component renders a plain struct pushed by its
   owner. View-state structs live in editor *core* (so tests can derive and assert them), are
   aggregated into `EditorViewState`, and are produced in `deriveViewState()`
   (`rock-hero-editor/core/src/controller/editor_controller.cpp`).
3. **Theme.** All colors come from `editorTheme()`
   (`rock-hero-editor/ui/src/shared/editor_theme.h`) by semantic role. Never hard-code a color;
   if no role fits, the theme gains a role.

# Part A — The compiler walks you through these {#add_view_part_a}

Once `EditorView` (`rock-hero-editor/ui/src/main_window/editor_view.h`) inherits your component's
`Listener`, its pure virtuals force the forwarding overrides. That is the only compiler help you
get on this recipe.

# Part B — Silent steps: nothing fails if you forget {#add_view_part_b}

1. **All of `EditorView`'s wiring** is silent except the listener overrides: constructing the
   member, `addAndMakeVisible`, laying it out in `resized()`, and pushing its slice of state in
   `setState()`. A component you forgot to lay out simply isn't there.
2. **View-state derivation.** The struct exists, the component renders it — but if
   `deriveViewState()` never fills it, the component draws defaults forever.
3. **Intent forwarding.** Each listener override should forward to the controller (usually
   becoming an editor action — see \ref guide_add_action). An empty override compiles happily.
4. **CMake + placement.** New files in a feature folder under `ui/src/`, listed in
   `rock-hero-editor/ui/CMakeLists.txt` (see \ref guide_add_file).
5. **Async safety.** If the component posts any deferred work, guard it with
   `juce::Component::SafePointer` — see \ref guide_invariants.
6. **Tests.** Add `test_<view>.cpp` to `rock-hero-editor/ui/tests/CMakeLists.txt`, using
   `editor_view_test_harness.h` (it supplies port fakes) and `component_test_helpers.h`. Cover
   the synchronous wiring: state in → visible change; gesture → listener call. Behavior beyond
   wiring belongs in editor-core tests, not component tests.
