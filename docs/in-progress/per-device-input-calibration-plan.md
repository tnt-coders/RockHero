# Per-Device Input Calibration Persistence Plan

Status: implementation-ready plan, updated against the current input-calibration workflow,
controller, and settings code. The completed input-calibration workflow/popup cleanup is now the
baseline for this work.

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

The limitation is now persistence and active-route selection policy:

- `EditorSettings` stores one flat calibration record using the `inputCalibration*` keys.
- `IEditorSettings` exposes one optional `inputCalibrationState()`.
- `InputCalibrationWorkflow` owns one active calibration state.
- `EditorController` loads one stored calibration at startup through
  `loadInputCalibrationFromSettings()`.
- `EditorController::persistInputCalibration()` writes the workflow's one active calibration back
  through `setInputCalibrationState(...)`.
- `InputCalibrationWorkflow::syncCommittedInputDeviceIdentity(...)` clears active calibration on a
  concrete route change and returns `Effect::PersistCalibration`.
- `EditorController::applyLiveInputGate()` still clears and persists when the active calibration no
  longer matches the current route.

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

Decision: treat "same device, different input channel" as a different exact route. The old
channel's calibration remains saved but is not applied to the new channel. This avoids applying a
gain measured on one preamp/channel to another channel that may have different analog gain.

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

Replace the single-record `IEditorSettings` calibration API with exact-route operations. Keep the
legacy flat setting keys private to `EditorSettings` for migration only; controller code should no
longer read or write a global `inputCalibrationState()`.

Use this API shape unless the implementation proves a specific name is misleading:

```cpp
[[nodiscard]] std::optional<common::audio::InputCalibrationState> inputCalibrationFor(
    const common::audio::InputDeviceIdentity& identity) const;

void saveInputCalibration(common::audio::InputCalibrationState calibration_state);

void removeInputCalibration(const common::audio::InputDeviceIdentity& identity);
```

`saveInputCalibration` should validate the identity and ignore invalid records. It should clamp the
stored gain the same way `setInputCalibrationState(...)` does today.

Do not add a new public helper class by default. If JSON parsing/upsert/remove becomes hard to
read, add private file-local helpers in `editor_settings.cpp` first. Add an `InputCalibrationHistory`
type only if it removes real complexity from both serialization and tests.

### Persistence Format

Persist the history as one structured setting value rather than inventing a family of dynamic
property keys.

Use this shape:

- new property key `inputCalibrationStatesJson`;
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

If the JSON property exists but is malformed, treat the history as empty and leave the legacy flat
keys untouched. Do not erase a valid legacy calibration unless a valid history has been written.

### Workflow Policy

The workflow should still own only one active calibration state. It should not know how settings
history is serialized.

Route-change policy should become:

- `std::nullopt` route: close prompt/measurement, disable monitoring, preserve active calibration
  and history.
- same concrete identity: keep active state.
- different concrete identity: close prompt/measurement and replace active state with the matching
  persisted record for that exact route, or clear active state if none exists.

The route-change transition should not request persistence. Changing the active route is a
selection operation, not a durable history mutation. Controller commit/restore paths are
responsible for saving or removing exact-route records.

Current code watch point: `InputCalibrationWorkflow::Effect::PersistCalibration` is still coupled to
the one-record settings model. Remove it if route changes no longer need persistence; otherwise
rename/split it so no call site can accidentally clear the full saved history.

Preferred implementation shape:

- controller queries `IEditorSettings::inputCalibrationFor(current_identity)` for concrete routes;
- controller passes that optional matching record into the workflow when syncing a new concrete
  route;
- workflow validates that any supplied record exactly matches the route before making it active;
- workflow returns only live-input monitoring effects for route loss/change.

### Controller Policy

`EditorController` should be the integration point:

- ask `IEditorSettings` for the saved calibration matching the current route;
- load or replace the workflow's active calibration for that route;
- apply the live-input gate after the active calibration has been selected;
- save the active route calibration after successful automatic or manual calibration;
- remove exact-route records only when the workflow determines the active route's saved calibration
  is invalid or unusable, not merely because the user switched to another route;
- compact invalid records inside `EditorSettings` during history parsing/writing.

The controller remains the root facade that executes side effects against settings and live-input
ports. The workflow remains headless and directly testable.

Current code watch points:

- replace `loadInputCalibrationFromSettings()` so it does not load an arbitrary global calibration;
- replace `persistInputCalibration()` with exact-route save/remove helpers;
- update `syncCommittedInputDeviceIdentity()` to select matching history for the current route;
- remove the mismatch clear/persist branch from `applyLiveInputGate()`. A mismatch should disable
  monitoring and select the route's saved calibration if one exists; it should not delete history.

## Implementation Stages

### Stage 1: Settings History Storage

- Add exact-route calibration lookup/upsert/remove behavior to `IEditorSettings`.
- Implement `EditorSettings` history serialization and legacy migration.
- Update `NullEditorSettings` and test harness fakes.
- Add settings tests for empty history, upsert, overwrite, lookup miss, invalid-record cleanup,
  duplicate collapse, malformed JSON fallback, and legacy single-record migration.
- Remove public use of `inputCalibrationState()` / `setInputCalibrationState(...)` after the new
  API exists. Any legacy flat-key parsing should stay private to `EditorSettings`.

### Stage 2: Active Calibration Selection

- Adjust `InputCalibrationWorkflow` so concrete route changes clear or replace only active state
  from a controller-supplied exact-route calibration.
- Keep route-unavailable behavior from the disconnect/reconnect fix.
- Add workflow tests for route A -> route B with no saved B, route A -> route B with saved B, and
  route B -> route A restoring the matching active calibration.
- Remove or narrow `Effect::PersistCalibration` so route changes do not request full settings
  persistence.

### Stage 3: Controller Integration

- Update startup/initial sync to select calibration for the current route rather than loading one
  global record.
- Update audio-device change handling to select a matching saved record for each new concrete
  route.
- Update calibration commit paths to upsert the current exact route's saved record.
- Update measurement cancel/restore failure paths so they preserve or restore only the active exact
  route record.
- Remove route-mismatch history deletion from `applyLiveInputGate()`.
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
- mismatched active calibration disables monitoring without deleting the previous route's saved
  record.

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

This no longer blocks the editor runtime extraction plan. Stages 0-5 and the action-policy
follow-up have landed; this is now a focused calibration-persistence follow-up that can proceed
independently.

It touches the same files and concepts as the current input-calibration work:

- `InputCalibrationWorkflow`;
- `EditorController` calibration loading, route sync, and commit paths;
- `EditorSettings` and `IEditorSettings`;
- editor-controller input-calibration tests.

Do not broaden `InputCalibrationWorkflow` into a persistent history owner while implementing this.
The workflow owns the active route's calibration state; `EditorSettings` owns durable exact-route
history; `EditorController` coordinates the two.

## Risks And Decisions

- `InputDeviceIdentity` is only as stable as backend/device/channel names. If a driver renames a
  channel after reconnect, exact-route history will intentionally require recalibration.
- Two physical devices with identical backend/device/channel strings cannot be distinguished by the
  current identity model.
- Keeping old route records can grow settings over time. This is acceptable for now; add a cap or
  UI cleanup only after it becomes a real problem.
- The migration must not erase a valid legacy single-record calibration if JSON parsing fails.
- A new helper type should be added only if private `EditorSettings` helpers are not enough to keep
  JSON history parsing and upsert/remove behavior readable.

