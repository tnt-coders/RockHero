# Editor Runtime Structure Pressure Findings

Status: in-progress findings document.

## Purpose

Record the runtime design pressure revealed by the editor test-file decomposition.

This is not a separate competing runtime refactor plan. It is a follow-up assessment that should
feed into:

- `docs/in-progress/editor-controller-root-facade-plan-v2.md`;
- `docs/in-progress/editor-core-framework-isolation-plan.md`;
- `docs/in-progress/readability-taxonomy-evaluation-plan.md`.

## Context

The completed test split moved the large editor-controller and editor-view tests into behavior
files and extracted shared test setup into the existing module-owned `*_testing` targets.

The split succeeded as a test-only cleanup, but it also made the remaining runtime pressure easier
to see. The important signal is not that there are many tests. The important signal is that several
narrow behavior files still need the same broad controller construction and shared fakes just to
exercise one responsibility.

## Summary

The main strain is real and is concentrated in `EditorController`.

`EditorController` is doing the right high-level job by keeping workflow policy out of JUCE
components, but it has become the owner of too many editor workflows at once. The test split
reinforces the value of the root-facade plan: keep `EditorController` as the public
`IEditorController` implementation, but move cohesive state machines and decision logic into
smaller headless workflow/state types.

The smaller UI strain is that concrete `EditorView` tests need a harness that includes private
view implementation headers through the UI testing target. That is acceptable for now because the
tests are same-library UI tests, but it should remain a watch item rather than a pattern to expand
casually.

## Findings

### 1. Controller Construction Is Still Too Wide For Narrow Tests

The split created focused controller files for state projection, plugins, transport, project
lifecycle, restore, busy routing, output gain, and input calibration. Those files are easier to
scan, but many of them still need the same broad setup:

- transport and live-input fakes;
- song-audio and audio-device fakes;
- plugin-host and live-rig fakes;
- project operation callbacks;
- settings and task-runner services;
- view-state capture.

That does not mean the tests are wrong. It means the runtime controller still sits at the
intersection of too many policy clusters.

### 2. The Controller Has Several Distinct State Machines

The current controller owns state for:

- project/session load and restore;
- unsaved-change and Save As prompt replay;
- busy operations, stale completion tokens, and live-rig progress;
- plugin catalog/browser state;
- runtime plugin chain projection;
- output gain persistence and dirty tracking;
- input calibration, route identity, rollback, and monitoring;
- audio-device settings-window transitions;
- transport enablement and cursor behavior;
- view-state projection and transient error reporting.

These concerns interact, but they are not one state machine. Keeping all of them as fields on the
root controller makes the class harder to scan and makes future features likely to land in the same
file by default.

### 3. Busy And Async Policy Is Cross-Cutting

The busy tests cover open, import, save, save-as, publish, live-rig loading, stale completions,
paint fencing, close/exit supersession, and audio-device-open scheduling. That breadth is a useful
signal: busy state is not a feature-specific detail. It is an editor operation policy that many
workflows depend on.

This supports the root-facade plan's `BusyOperationState` extraction. The first extraction should
not try to own task submission or JUCE paint fencing. It should own operation identity, tokens,
progress snapshots, stale-token rejection, and finish/supersede decisions.

### 4. Deferred Project Actions Are A Separate Policy Cluster

The project lifecycle and restore tests still need prompt replay state, project operations,
settings persistence, session state, and audio activation setup. The broad setup is partly
expected, but the deferred-action rules are distinct enough to extract before any larger project
lifecycle split.

This supports `DeferredEditorActionState` before any broad `ProjectWorkflow` type. A broad project
workflow would risk becoming a second oversized controller.

### 5. Input Calibration Is The Strongest Workflow Extraction Candidate

The input calibration tests need route identity, previous calibration state, live-input gain,
monitoring flags, prompt visibility, settings-window state, and live-rig readiness. This is the
clearest example of a headless workflow currently living inside the root controller.

The future `InputCalibrationWorkflow` should own calibration policy and return requested effects.
The root controller should still execute effects against `ILiveInput`, `IEditorSettings`, and the
view.

### 6. Plugin Catalog State Can Be Extracted, But Signal-Chain Mutation Should Wait

The plugin tests cover catalog scanning, browser state, add/remove/open, live-rig loading, capture
before save, stale plugin IDs, and dirty tracking.

Catalog/browser state is small and likely extractable now. Full signal-chain workflow extraction
should wait until the audio boundary exposes a stronger authoritative chain model than append/remove
against the current linear list. Extracting too early would harden today's temporary signal-chain
shape.

### 7. Project Lifecycle Should Not Be Extracted As One Large Type First

The project lifecycle tests show real weight, but the root-facade plan is correct to avoid a
first-pass `ProjectLifecycleController` or broad `ProjectWorkflow`.

The safer path is:

1. extract busy operation state;
2. extract deferred action state;
3. extract plugin catalog state if it deletes meaningful controller ownership;
4. extract input calibration workflow;
5. reassess project lifecycle after those fields and branches are gone.

At that point, any remaining project extraction can be a small decision slice rather than a second
root controller.

### 8. Concrete EditorView Tests Are Acceptable But Should Stay Narrow

The UI test split produced a shared `EditorView` harness in `rock_hero::editor::ui_testing`. That
harness includes private `EditorView` implementation headers through the UI test target's private
source include path.

That is acceptable because these are same-library tests of concrete JUCE presentation. It would
become a runtime design warning if non-UI tests started needing concrete `EditorView`, or if the
view harness began recreating controller workflow setup. For now, the UI side does not justify a
runtime refactor by itself.

## Recommended Direction

Use the existing root-facade plan as the runtime cleanup path. Do not create a new standalone
controller decomposition effort from this document.

Recommended order:

1. Implement `editor-core-framework-isolation-plan.md` Part 1 first.
2. Continue with `editor-controller-root-facade-plan-v2.md` Phases 2 through 5.
3. Use the Phase 6 checkpoint to decide whether project lifecycle and signal-chain workflow
   extraction are still needed.
4. Fold any naming or folder placement decisions into the readability taxonomy plan once the
   extracted runtime types exist.

This order keeps the work incremental and prevents speculative type creation.

## Naming Guidance For Follow-Up Types

Keep the naming rules from the root-facade plan:

- use `State` for focused state machines or snapshot owners;
- use `Workflow` for headless policy that accepts inputs and returns decisions/effects;
- reserve `Controller` for public view-facing intent surfaces;
- avoid `Manager`, `Handler`, `Helper`, and broad `Service` names.

Likely names from the existing plan still fit the pressure revealed by the split:

- `BusyOperationState`;
- `DeferredEditorActionState`;
- `PluginCatalogWorkflow`;
- `InputCalibrationWorkflow`.

Avoid adding `ProjectWorkflow` or `SignalChainWorkflow` until the active root-facade phases prove
that a remaining cohesive responsibility actually needs those names.

## Acceptance Criteria For Addressing This Pressure

The pressure is addressed when:

- controller tests for extracted policy no longer need the full controller harness;
- each extracted type deletes direct fields and policy branches from `EditorController`;
- extracted workflow/state tests construct only the dependencies relevant to that responsibility;
- `EditorController` still handles intent routing, side-effect execution, async liveness, and
  view-state publication;
- no extracted type calls `IEditorView`, owns JUCE components, or reaches into Tracktion/JUCE
  adapters directly;
- new runtime type names follow the taxonomy consistently.

## Non-Goals

- Do not refactor runtime code as part of the completed test-file split.
- Do not create a workflow type merely because a test file exists with a matching name.
- Do not move concrete JUCE view behavior into editor core.
- Do not extract signal-chain mutation before the audio boundary is ready for authoritative chain
  operations.
- Do not add durable `docs/design/` rules until the extracted shape has proven itself in code.
