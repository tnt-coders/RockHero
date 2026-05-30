# Per-Device Input Calibration Persistence Plan

Status: in-progress planning note. This is an app-local calibration persistence improvement that
can be implemented before Stage 5 or handled independently after the disconnect/reconnect fix is
committed.

## Scope

Remember input calibration gains for previously used input routes so users can switch devices and
switch back without recalibrating.

This remains editor-only app-local state:

- not stored in `.rhp` projects;
- not stored in `.rock` packages;
- not owned by JUCE UI components;
- not owned by Tracktion or the shared audio engine.

The implementation should stay in `rock-hero-editor/core` and the existing app-local settings
boundary. The audio layer should continue to report only the current exact input route identity.

## Current State

`common::audio::InputCalibrationState` already stores the right atomic record:

- calibration gain;
- exact `InputDeviceIdentity`;
- equality against the exact input route.

The limitation is persistence and active-route policy:

- `EditorSettings` stores one flat calibration record using the `inputCalibration*` keys.
- `IEditorSettings` exposes one optional `inputCalibrationState()`.
- `InputCalibrationWorkflow` owns one active calibration state.
- `EditorController` loads one stored calibration at startup.
- A concrete route mismatch clears the active calibration and persists that clear.

That is correct for "do not apply the wrong calibration to the current input", but it is too
destructive for switching between known devices. Route A -> route B -> route A should restore route
A's prior calibration when it is still an exact identity match.

## Desired Behavior

- If the current exact input route has a saved calibration, apply it automatically.
- If the current exact input route has no saved calibration, show `MissingCalibration`.
- Switching from route A to route B must not delete route A's saved calibration.
- Switching back from route B to route A should restore route A's saved gain.
- Successful automatic or manual calibration should upsert the record for the current exact route.
- Recalibrating the same exact route should replace only that route's saved gain.
- A temporary unavailable route (`std::nullopt`) should preserve the full calibration history.
- A different backend, device, channel index, or channel name must not reuse another route's gain.

Recommended default: treat "same device, different input channel" as a different exact route. The
old channel's calibration remains saved but is not applied to the new channel. This avoids applying
a gain measured on one preamp/channel to another channel that may have different analog gain.

Before implementation, confirm whether changing channels on the same backend/device should:

1. keep both exact-route records, which is safest for interfaces with multiple calibrated inputs;
2. delete the previous same-device record, which enforces one calibration per hardware device.

## Non-Goals

- Do not add UI for listing or deleting old calibration records.
- Do not introduce fuzzy matching by partial device name.
- Do not infer hardware identity beyond the current `InputDeviceIdentity` fields.
- Do not change Tracktion/JUCE device management.
- Do not promote app-local calibration into project or song package data.
- Do not add a broad settings abstraction beyond the existing `IEditorSettings` boundary.

## Proposed Design

Keep `InputCalibrationWorkflow` focused on the active route. Add persistence history behavior at
the settings/controller boundary.

### Settings Contract

Replace or extend the single-record API with exact-route operations. The final names should be
confirmed before implementation because several reasonable names exist.

Candidate API shape:

```cpp
[[nodiscard]] std::optional<common::audio::InputCalibrationState> inputCalibrationFor(
    const common::audio::InputDeviceIdentity& identity) const;

void saveInputCalibration(common::audio::InputCalibrationState calibration_state);

void removeInputCalibration(const common::audio::InputDeviceIdentity& identity);
```

An all-record API is acceptable for implementation and tests if it keeps `EditorSettings` simpler,
but controller code should not need to scan or mutate raw serialized settings.

Potential naming decision:

- `inputCalibrationFor` / `saveInputCalibration` / `removeInputCalibration`;
- `inputCalibrationStateFor` / `setInputCalibrationState`;
- `InputCalibrationHistory` helper type if the matching/upsert/remove policy becomes more than a
  few small functions.

Do not add a new helper class unless it removes real complexity. If the policy stays as exact
lookup plus upsert, free functions or `EditorSettings` private helpers are enough.

### Persistence Format

Persist the history as one structured setting value rather than inventing a family of dynamic
property keys.

Recommended shape:

- new property key such as `inputCalibrationStatesJson`;
- value is a JSON array of calibration records;
- each record stores gain, backend name, input device name, input channel index, and input channel
  name;
- loading drops invalid or incomplete records;
- duplicate exact identities collapse to the last valid record.

Migration:

- read the existing single-record keys as a legacy fallback;
- if a valid legacy record exists and the new history key is missing, seed the history with it;
- after successfully writing the new history, remove the old flat keys.

This keeps existing user calibration data through the schema change and lets future settings
changes reason about a single durable history property.

### Workflow Policy

The workflow should still own only one active calibration state. It should not know how settings
history is serialized.

Route-change policy should become:

- `std::nullopt` route: close prompt/measurement, disable monitoring, preserve active calibration
  and history.
- same concrete identity: keep active state.
- different concrete identity: close prompt/measurement and replace active state with the matching
  persisted record for that exact route, or clear active state if none exists.

The route-change transition should not request "clear all persisted calibration". If an effect is
needed, make it specific enough that the controller can distinguish:

- upsert current route calibration;
- remove invalid current route calibration;
- disable live monitoring;
- disable calibration monitoring.

Avoid keeping a misleading `PersistCalibration` effect if it still means "write the one active
record or clear the only stored record".

### Controller Policy

`EditorController` should be the integration point:

- ask `IEditorSettings` for the saved calibration matching the current route;
- load or replace the workflow's active calibration for that route;
- apply the live-input gate after the active calibration has been selected;
- save the active route calibration after successful automatic or manual calibration;
- remove or compact invalid records only when settings load detects invalid persisted data.

The controller remains the root facade that executes side effects against settings and live-input
ports. The workflow remains headless and directly testable.

## Implementation Stages

### Stage 1: Settings History Storage

- Add exact-route calibration lookup/upsert/remove behavior to `IEditorSettings`.
- Implement `EditorSettings` history serialization and legacy migration.
- Update `NullEditorSettings` and test harness fakes.
- Add settings tests for empty history, upsert, overwrite, lookup miss, invalid-record cleanup,
  duplicate collapse, and legacy single-record migration.

### Stage 2: Active Calibration Selection

- Adjust `InputCalibrationWorkflow` so concrete route changes clear or replace only active state.
- Keep route-unavailable behavior from the disconnect/reconnect fix.
- Add workflow tests for route A -> route B with no saved B, route A -> route B with saved B, and
  route B -> route A restoring the matching active calibration.

### Stage 3: Controller Integration

- Update startup loading to select calibration for the current route rather than loading one global
  record.
- Update audio-device change handling to select a matching saved record for the new concrete route.
- Update calibration commit paths to upsert the current route's saved record.
- Preserve the existing live-input gate behavior: no matching active calibration means monitoring
  stays disabled.

### Stage 4: Regression Coverage

Add controller tests for:

- startup with route A selected and saved route A calibration applies;
- startup with route B selected and only route A saved reports missing calibration;
- route A calibrated, switch to route B, switch back to route A restores route A;
- route A calibrated, route B has its own saved gain, switch to route B applies route B;
- same device with different input channel does not reuse the previous channel's gain;
- unavailable route preserves the full history and active saved records.

Keep existing tests that verify a real mismatched route is not treated as calibrated. They should
change from "global settings calibration is deleted" to "current route is missing calibration and
previous route remains saved" if the exact-route history policy is adopted.

### Stage 5: Verification

- Build `rock_hero_editor_core_tests`.
- Run `rock_hero_editor_core_tests.exe`.
- Run `git diff --check`.
- If settings serialization changes are substantial, inspect the generated settings XML in a temp
  test file through assertions rather than manual file inspection.

## Impact On Editor Runtime Extraction

This does not block the editor runtime extraction plan, but it should be sequenced deliberately.

It touches the same files and concepts as the current input-calibration work:

- `InputCalibrationWorkflow`;
- `EditorController` calibration loading, route sync, and commit paths;
- `EditorSettings` and `IEditorSettings`;
- editor-controller input-calibration tests.

If this is implemented before Stage 5, finish and commit the disconnect/reconnect fix first so the
history work starts from a clean calibration baseline. Then implement this as its own focused
calibration-persistence change.

If Stage 5 proceeds first, keep this plan as follow-up work. Stage 5 should avoid broadening the
calibration workflow into a persistent history owner; this plan still belongs at the settings and
controller integration boundary.

## Risks And Decisions

- `InputDeviceIdentity` is only as stable as backend/device/channel names. If a driver renames a
  channel after reconnect, exact-route history will intentionally require recalibration.
- Two physical devices with identical backend/device/channel strings cannot be distinguished by the
  current identity model.
- Keeping old route records can grow settings over time. This is acceptable for now; add a cap or
  UI cleanup only after it becomes a real problem.
- The migration must not erase a valid legacy single-record calibration if JSON parsing fails.
- The final API/type names should be confirmed before implementation if a new helper type is added.

