# IEditorController Overhead Assessment

## Problem

Adding simple editor features (e.g. two gain sliders) touches ~6 files of pure forwarding
plumbing through the `IEditorController` interface, independent of the actual feature logic. This
overhead is disproportionate for lightweight intents and will grow with every new user-facing
control.

## Where the Cost Comes From

Every user intent emitted by any editor widget follows this path:

1. `SignalChainPanel::Listener` (or other widget listener) declares the callback.
2. `EditorView` implements the callback, checks a state guard, and forwards to
   `IEditorController`.
3. `IEditorController` declares the pure virtual method.
4. `EditorController` (header) declares the override.
5. `EditorController` (impl) adds an `Impl` declaration and a one-line forwarding stub from the
   public override to the `Impl` method.
6. `FakeEditorController` in `test_editor_view.cpp` adds a recording stub and counter fields.

Adding two gain intents (`onInputGainChanged`, `onOutputGainChanged`) touched all six of these
sites. The actual gain logic (clamp, call live rig, mark dirty, update view) lives only in step 5.
Steps 1-4 and 6 are mechanical ceremony.

## Why IEditorController Exists

The interface exists so `test_editor_view.cpp` can inject a `FakeEditorController` and verify that
UI interactions emit the correct intents. Without it, UI tests would need a real
`EditorController` with all its audio/transport/session dependencies.

## What the UI Tests Actually Verify

Most `FakeEditorController` methods just record that they were called:

```cpp
void onInputGainChanged(double gain_db) override
{
    last_input_gain_db = gain_db;
    input_gain_change_count += 1;
}
```

The tests then check `controller.input_gain_change_count == 1`. This verifies wiring (did the
slider call the right method?) but not behavior (did the gain actually clamp and persist?).
Behavior is tested separately through `EditorController` with `FakeLiveRig`, `FakeAudio`, etc.

## Cost-Benefit

The wiring tests have value, but their cost scales linearly with every new intent. For 20+
intents on `IEditorController`, that means 20+ fake methods, 20+ forwarding stubs, and 20+
override declarations that must all stay synchronized.

## Options

### Option A: Keep IEditorController, accept the cost

No code changes. The decomposition plan already extracts `ProjectWorkflowController` and
`PluginWorkflowController` with narrower interfaces, which would naturally reduce the surface of
`IEditorController` over time. New features would add intents to the focused sub-controller
interfaces instead.

This is the lowest-risk path but does not address the forwarding overhead for intents that remain
on the root controller.

### Option B: Replace IEditorController with concrete EditorController in view tests

Make `EditorView` take `EditorController&` directly instead of `IEditorController&`. UI tests
would construct a real `EditorController` with fake audio/transport/session dependencies (which
already exist in `test_editor_controller.cpp`).

Removes: `IEditorController`, `FakeEditorController`, all forwarding stubs.
Adds: shared test fakes between controller and UI test targets (the centralized fakes plan in
`docs/todo/` already contemplates this).

Risk: UI tests become slightly heavier because they construct a real controller, but the
controller is already headless and fast to construct with fakes.

### Option C: Replace IEditorController with a narrower callback or intent pattern

Instead of a single interface with N virtual methods, the view could emit intents through a
lightweight mechanism (e.g. `std::function` callbacks, a variant-based intent queue, or
per-widget listener interfaces that the controller implements directly).

This would let new intents be added without touching a central interface, but it changes the
view-controller contract significantly.

### Option D: Keep IEditorController but auto-generate the forwarding

Use a macro or code generation to reduce the per-intent boilerplate. This reduces the maintenance
cost without changing the architecture, but adds complexity of a different kind.

## Recommendation

Option A (accept and wait for decomposition) is the pragmatic near-term choice. The editor
controller decomposition plan already addresses the root cause by splitting the interface. Once
`ProjectWorkflowController` and `PluginWorkflowController` have their own interfaces, new
features add intents to small, focused surfaces rather than the monolithic `IEditorController`.

Option B is worth revisiting if the decomposition does not materially reduce the forwarding
overhead, or if the centralized test fakes plan lands first and makes shared fakes available to
both test targets.
