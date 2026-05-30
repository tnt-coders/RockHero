# Input Calibration Workflow and Popup Cleanup Plan

Status: complete. Manual popup smoke testing passed before this note moved to `docs/completed/`.
This plan replaced the earlier narrow type-rename note and was framed around the workflow shape
rather than around isolated names.

## Implementation Outcome

All stages landed in one change set. Two deliberate deviations from the plan as written, both
improvements:

- **Narrow host boundary instead of `IEditorController`.** The popup controller emits intents
  through a small `InputCalibrationController::Host` interface (start/cancel measurement, apply
  automatic/manual gain, dismiss) rather than depending on the full `IEditorController`. The JUCE
  popup `Content` implements both `IInputCalibrationView` and `Host`, adapting host calls to
  `IEditorController`. This keeps the core controller's surface minimal and headlessly testable
  (`RecordingInputCalibrationHost`).
- **`Snapshot` kept, not renamed to `Projection`.** Nesting it as `InputCalibrationWorkflow::Snapshot`
  already disambiguates it from the new top-level `InputCalibrationViewState`, so the rename was
  unnecessary. The names below reflect what shipped.

The restore variant collapse landed as planned: `MeasurementRestorePlan` is a variant, the restore
handler is a `std::visit`, and the old `assert(plan.previous_calibration_state.has_value())` is gone.

Post-review fix applied: `InputCalibrationController::gainText` now rounds to the displayed tenth
before formatting, restoring the old `juce::String{value, 1}` rounding (it had regressed to
truncation, so e.g. 2.06 dB showed as "2.0" instead of "2.1").

Manual smoke testing covered the calibration popup's automatic capture, manual apply, cancel, and
error path. That closes the remaining parity check after moving the live capture state machine out
of JUCE.

## Problem

The current `InputCalibrationWorkflow` is directionally correct: editor-core owns the calibration
policy, while `EditorController` executes live-input and settings side effects through ports. The
smell is that the workflow exposes several top-level `InputCalibration...` types whose names do
not make their lifecycle role obvious:

- `InputCalibrationFacts`
- `InputCalibrationSnapshot`
- `InputCalibrationEffect` / `InputCalibrationEffects`
- `InputCalibrationMeasurement`
- `InputCalibrationCommitPlan`
- `InputCalibrationRestoreKind` / `InputCalibrationRestorePlan`

Read together, these look like several competing calibration "states". They are not. They are
boundary values for different phases:

- context supplied by the controller;
- view projection returned to the controller;
- simple side-effect requests returned after pure state transitions;
- a measurement-session token captured before live-input setup;
- a commit plan prepared before applying a completed gain;
- a measurement-restore decision prepared before cancelling or dismissing a measurement.

The cleanup should make those roles visible in the type names and type ownership.

There is a second, related smell in `InputCalibrationWindow`: the JUCE component owns enough
capture workflow state that it no longer reads like presentation-only code. The popup should follow
the same broad design direction as other editor UI surfaces: core owns state and policy; UI renders
state, samples framework data, and emits intents.

## Ideal Workflow Shape

`InputCalibrationWorkflow` should remain a small headless policy object. It should not own
settings, JUCE UI, Tracktion state, or the live-input port.

The end-to-end flow should read like this:

1. **Controller supplies context.** `EditorController` collects root-owned readiness and routing
   information into `InputCalibrationWorkflow::Context`.
2. **Workflow snapshots editor state.** `snapshot(context)` returns
   `InputCalibrationWorkflow::Snapshot`, which the controller copies into `EditorViewState` and
   `SignalChainViewState`.
3. **Route changes update workflow policy.** Device-setting transitions and audio-device callbacks
   call workflow methods such as `openAudioDeviceSettings()` and
   `syncCommittedInputDeviceIdentity(...)`. Those methods may return
   `InputCalibrationWorkflow::Effects` for simple controller-executed side effects.
4. **Prompt requests stay pure.** `requestPrompt(context)` decides whether calibration UI may open.
   The controller handles transport pause and view updates.
5. **Automatic measurement has a session token.** `prepareMeasurementStart(context)` validates the
   prompt/readiness/current route and returns `InputCalibrationWorkflow::MeasurementSession`.
   The controller arms the live-input route. Only after those side effects succeed does it pass the
   session back to `activateMeasurement(...)`.
6. **Manual or automatic commit has a commit plan.** `prepareCommit(...)` or
   `prepareActiveMeasurementCommit(...)` returns `InputCalibrationWorkflow::CommitPlan`. The
   controller disables calibration monitoring, applies the gain, enables live monitoring, then
   commits or rolls back workflow state based on the result.
7. **Measurement cancel/dismiss has a restore decision.** `prepareMeasurementRestore(context)`
   returns a variant `InputCalibrationWorkflow::MeasurementRestorePlan`. Only the
   `RestorePreviousCalibration` case carries the previous calibration state, so the illegal
   enum-plus-optional state disappears.

This keeps the ports-and-adapters split intact: workflow state transitions stay directly testable,
and the controller remains the executor for real settings and live-input side effects.

The popup itself should have its own view contract. The workflow snapshot should not be called a
`ViewState` once the popup gets a real public `InputCalibrationViewState`.

## Type Ownership Decision

Nest workflow-only boundary types under `InputCalibrationWorkflow`.

This is the main readability improvement. These values are not reusable audio-domain model types;
they are the private vocabulary of one workflow. Nesting lets the code use short role names without
polluting `rock_hero::editor::core` with several similar top-level names.

Recommended shape:

```cpp
class InputCalibrationWorkflow final
{
public:
    struct Context;
    enum class Effect;
    using Effects = std::vector<Effect>;
    struct Snapshot;
    struct MeasurementSession;
    struct CommitPlan;

    struct MeasurementRestore
    {
        struct NoRestore {};
        struct DisableLiveInput {};
        struct ClearCalibration {};
        struct ClearCalibrationAndClosePrompt {};
        struct RestorePreviousCalibration {
            common::audio::InputCalibrationState previous_calibration_state;
        };
    };
    using MeasurementRestorePlan = std::variant<
        MeasurementRestore::NoRestore, MeasurementRestore::DisableLiveInput,
        MeasurementRestore::ClearCalibration, MeasurementRestore::ClearCalibrationAndClosePrompt,
        MeasurementRestore::RestorePreviousCalibration>;
};
```

`InputCalibrationStatus`, `InputCalibrationPrompt`,
`InputCalibrationViewState`, `IInputCalibrationView`, and
`common::audio::InputCalibrationState` should not be nested or renamed here. They are not workflow
implementation types:

- `InputCalibrationStatus` and `InputCalibrationPrompt` are view-state vocabulary.
- `InputCalibrationViewState` and `IInputCalibrationView` are the popup's public UI contract.
- `common::audio::InputCalibrationState` is the stored audio-domain value.

## Proposed Final Names

| Current | Proposed final | Rationale |
|---|---|---|
| `InputCalibrationFacts` | `InputCalibrationWorkflow::Context` | Controller-supplied environment, not only boolean conditions. |
| `InputCalibrationSnapshot` | `InputCalibrationWorkflow::Snapshot` | Editor-state snapshot, not the popup view contract. |
| `InputCalibrationEffect` | `InputCalibrationWorkflow::Effect` | Workflow-local simple side-effect request. |
| `InputCalibrationEffects` | `InputCalibrationWorkflow::Effects` | Workflow-local effect collection. |
| `InputCalibrationMeasurement` | `InputCalibrationWorkflow::MeasurementSession` | Editor workflow session/rollback token, not raw audio measurement data. |
| `InputCalibrationCommitPlan` | `InputCalibrationWorkflow::CommitPlan` | Prepared transactional commit recipe. |
| `InputCalibrationRestoreKind` | removed | Fold into a variant. |
| `InputCalibrationRestorePlan` | `InputCalibrationWorkflow::MeasurementRestorePlan` | Measurement-specific restore decision. |
| restore holder | `InputCalibrationWorkflow::MeasurementRestore` | Namespace for restore variant alternatives. |
| restore no-op case | `MeasurementRestore::NoRestore` | More explicit than `Nothing`. |
| restore previous case | `MeasurementRestore::RestorePreviousCalibration` | Names the data-bearing case precisely. |

The current `Effect::PersistCalibration` name is acceptable for this no-behavior-change cleanup
only if the upcoming per-device persistence work revisits it. With route history, an effect named
`PersistCalibration` is likely too broad because persistence will distinguish saving the active
route's calibration from preserving or removing other route records.

## Popup View Contract Direction

`InputCalibrationWindow` should become a JUCE shell around a presentation component, not the owner
of calibration capture policy.

Add a headless editor-core controller named `InputCalibrationController`. Do not call it
`InputCalibrationWindowController`; the controller owns the calibration popup workflow, not a JUCE
window. The window is only one presentation host for that workflow.

Target split:

- `InputCalibrationWorkflow`: route readiness, prompt visibility, active route calibration,
  measurement session token, commit plans, and restore plans.
- `InputCalibrationController`: popup-local interaction state such as idle/measuring/error/complete
  phase, manual gain preview, status text, button enabled state, capture progress, and calls into
  `IEditorController` for workflow intents.
- `InputCalibrationViewState`: render state for the calibration popup controls.
- `IInputCalibrationView`: framework-free view contract driven by `InputCalibrationController`.
- `InputCalibrationWindow` / popup content: JUCE rendering, layout, help button, timer sampling,
  and forwarding user gestures or sampled meter levels to `InputCalibrationController`.

The UI timer may stay in the JUCE layer because timer cadence is framework-owned. The timer should
sample `ILiveInput::rawInputMeterLevel()` and pass that plain value to
`InputCalibrationController` rather than having the JUCE component own the capture state machine.

`InputCalibrationController` may own `common::audio::InputCalibrationCapture`; that type is
headless audio-domain capture policy and already directly testable without JUCE.

## Effect vs Plan Rule

Keep both concepts, but document the distinction:

- **Effect**: a simple side-effect request returned after the workflow has already updated its own
  state. The controller executes it best-effort against settings or live-input ports.
- **Plan**: a prepared transactional recipe returned before the controller mutates live-input ports.
  The controller must report success/failure back into the workflow so the workflow can commit,
  restore, or preserve calibration state correctly.

Do not collapse these into one generic output type in this pass. That would hide the important
difference between "state already changed; please mirror it outside" and "side effects must succeed
before workflow state can commit".

## Keep Separate

Do not merge `MeasurementSession` and `CommitPlan`.

They share route identity and previous calibration fields, but they exist at different lifecycle
points:

- `MeasurementSession` is captured before raw measurement setup and becomes the active rollback
  token only after live-input setup succeeds.
- `CommitPlan` is prepared after a gain is known and before that gain is applied to the live route.

Adding a shared base value would create another type without removing meaningful complexity.

## Cleanup Stages

### 1. Nest and rename workflow boundary types

Move workflow-local boundary types inside `InputCalibrationWorkflow` and apply the names in the
table above.

This should be a pure compile-time rename/re-home:

- no behavior change;
- no side-effect ordering change;
- no settings format change;
- no controller policy change.

Exit criteria:

- old top-level workflow type names are gone;
- `InputCalibrationWorkflow::Context`, `Snapshot`, `Effect`, `Effects`,
  `MeasurementSession`, and `CommitPlan` are used consistently;
- tests still cover the same behavior.

### 2. Replace restore enum-plus-optional with a variant

Replace `InputCalibrationRestoreKind` and the optional-bearing `InputCalibrationRestorePlan` with
`InputCalibrationWorkflow::MeasurementRestorePlan`.

Exit criteria:

- `InputCalibrationRestoreKind` is gone;
- the restore plan cannot represent `RestorePreviousCalibration` without previous state;
- the controller no longer needs `assert(plan.previous_calibration_state.has_value())`;
- restore behavior is unchanged.

### 3. Re-read controller restore and commit coupling

After the type cleanup, review whether the controller still owns too much calibration policy around
commit/restore failure handling.

Do not add a new executor class by default. Only move policy back into the workflow if it removes
real branching from `EditorController` without making the workflow depend on live-input ports.

Candidate follow-up only if the code still reads poorly:

- a workflow method that records successful measurement restore by plan case;
- a workflow method that records restore failure by plan case and `LiveInputErrorCode`;
- tighter comments around the remaining controller-owned side-effect ordering.

Exit criteria:

- either no follow-up is needed, or the specific policy moved back into the workflow is documented
  and covered by tests;
- no new class exists unless it removes real complexity.

### 4. Extract popup state into InputCalibrationController

Introduce the popup View/ViewState contract after the workflow boundary names are clear.

Expected implementation shape:

- add `InputCalibrationViewState`;
- add `IInputCalibrationView`;
- add `InputCalibrationController`;
- have the controller attach to the view and push initial state seeded from
  `InputCalibrationPrompt`;
- move capture phase, status text selection, manual gain preview/apply, button enabled states, and
  completion/error transitions out of `InputCalibrationWindow::Content`;
- keep JUCE layout and raw meter sampling in the UI layer.

Exit criteria:

- `InputCalibrationWindow::Content` renders state and forwards intents instead of owning the
  calibration capture state machine;
- popup behavior is covered in editor-core controller tests where possible;
- UI code retains only framework-specific behavior that is not valuable to test headlessly.

### 5. Align with per-device persistence planning

Before implementing route-history persistence, revisit `Effect::PersistCalibration` and any route
change effect names. Route history should not keep a name that implies clearing or rewriting one
global calibration record when the durable behavior is exact-route lookup/upsert/remove.

## Verification

Run the focused editor core and UI targets after implementation:

```powershell
cmd.exe /d /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && ninja -C build/debug rock_hero_editor_core_tests rock_hero_editor_ui_tests'
& 'build/debug/rock-hero-editor/core/tests/rock_hero_editor_core_tests.exe'
& 'build/debug/rock-hero-editor/ui/tests/rock_hero_editor_ui_tests.exe'
git diff --check
```

## Non-Goals

- Do not move calibration workflow out of editor core.
- Do not make the workflow own settings, JUCE UI, Tracktion, or live-input ports.
- Do not rename `common::audio::InputCalibrationState`.
- Do not add a helper/executor class unless the post-cleanup review proves it removes real
  complexity.
- Do not name the popup controller after the JUCE window. Use `InputCalibrationController`.
- Do not change settings persistence behavior in this cleanup; per-device persistence remains its
  own plan.
- Do not update durable `docs/design/` documents from this note.
