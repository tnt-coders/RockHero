# Error Contract Hardening — Follow-Up Review

Status: completed review note. The gaps found while reviewing
`error-contract-hardening-plan.md` have been addressed, and the plan's acceptance criteria have
been verified.

## Summary

The production code is faithful to the plan and, in the ignored-path work, tighter than it asked
for. The follow-up work closed the remaining test coverage and Stage 4 gaps.

## Completion Verification

- Focused targets `rock_hero_common_audio_tests` and `rock_hero_editor_core_tests` build.
- `rock_hero_common_audio_tests.exe`: 99 test cases and 444 assertions passed.
- `rock_hero_editor_core_tests.exe`: 217 test cases and 1685 assertions passed.
- Stage 7 audit grep found no raw-string `std::expected` error channels and no explicit discard
  hits.
- Remaining `std::optional<...Error>` hits are test failure-injection knobs or
  `InputCalibrationCaptureUpdate::error`, which is phase-associated snapshot state.
- Remaining `error_message` and `error_code` hits are UI state, error constructors,
  `std::error_code` locals, or test fixture fields rather than private error-channel out
  parameters.

## Resolved Gap 1: Stage 6 test coverage exercises the new contract

The original staged test changes were almost entirely mechanical `CHECK(x)` to
`CHECK(x.has_value())` rewrites that preserved the previous coverage. They did not assert the new
typed failure behavior.

Original evidence:

- `SongAudioErrorCode`, `AudioDeviceConfigurationErrorCode`, and `EditorSettingsErrorCode` never
  appear in a `.error().code == ...` assertion. They appear only inside the test fakes.
- The new failure-injection knobs are defined but never set by any test body:
  - `next_prepare_error`
  - `next_set_active_arrangement_error`
  - `next_clear_active_arrangement_error`
  - `next_restore_serialized_device_state_error`
  - `next_cancel_error`
- `MonitoringRouteFailed` has zero test references.

Required coverage added or verified:

- `ISongAudio` preparation failure reports a typed code and useful path/message context (assert the
  specific `SongAudioErrorCode` and that the message carries the offending path).
- `ISongAudio` active-arrangement failure reports backend setup failure rather than a bare empty
  result (assert the code, not just `!has_value()`).
- Serialized device restore distinguishes `InvalidSerializedState` (unparseable XML) from
  `RestoreFailed` (backend rejected the state).
- Audio-device `cancel()` restore failure keeps the dialog open / surfaces the error instead of
  closing as if it succeeded; the fake supports `next_cancel_error` and tests drive it.
- Live-input monitoring rebuild failure cannot report success for a user-visible operation (e.g.
  `setActiveArrangement` returning `MonitoringRouteFailed`).
- Best-effort cleanup failures are routed through the approved helper (assert the helper logs and
  the primary result is still returned).
- `EditorSettings` typed failure codes, including the corrupt-history path: `inputCalibrationFor`
  on malformed JSON returns `InvalidInputCalibrationHistory` rather than empty absence.

Highest-value tests added first:

1. Serialized-restore test asserting `InvalidSerializedState` vs `RestoreFailed`.
2. `prepareSong` failure test asserting the specific code plus path in the message.
3. Controller test driving `next_cancel_error` to confirm the dialog stays open on restore failure.

## Resolved Gap 2: `showControlPanel()` bool return is handled

`AudioDeviceSettings::openControlPanel()` now translates a false `showControlPanel()` return into
`AudioDeviceSettingsErrorCode::ControlPanelUnavailable`, refreshes state with the diagnostic, and
returns the typed failure.

## Resolved Gap 3: explicit discards meet the discard rule

The controller now comments the destructor-only `cancel()` cleanup path and the control-panel
side-channel handling that relies on `refreshState()` plus `updateView()` to surface failure.


## Smaller Items Reviewed

These were reviewed as non-blocking implementation choices for this migration. The historical
bullets below explain the tradeoffs that were accepted.

- Cross-domain code mirroring is slightly heavier than the convention advises ("add mirrored codes
  only after callers demonstrably need to branch"). `MonitoringRouteFailed` was added to both
  `PluginHostError` and `LiveRigError`, and the `*FromLiveInputError` mappers preserve
  `MessageThreadRequired` / `TrackMissing`, yet no caller branches on these codes yet. The three
  mappers are also inconsistent: two preserve fine-grained codes while
  `songAudioErrorFromLiveInputError` collapses to a single code. This remains accepted as the
  implemented boundary shape for this migration.
- Behavior change in the `MeasurementRestore` branches of `editor_controller.cpp`: reordering to
  disable-monitoring-then-clear-state means a failed disable now leaves the
  measurement/calibration state uncleared and returns the error. This is the safer behavior and
  remains accepted for this migration.
- Settings persistence failures are uniformly log-only via `recordSettingsResultBestEffort`. The
  plan permits this, and stronger user-visible surfacing can be handled as a separate UX decision.
- `loadSessionSong` maps "editor session rejected the song" to `BackendClipInsertionFailed`, which
  is semantically loose (it is not a clip-insertion failure). It is an assert-shouldn't-happen
  path with no better existing code; it remains accepted as-is for this migration.

## Acceptance Criteria Resolution

The original open acceptance criteria are now satisfied:

- Tests cover the typed failure paths listed in Gap 1.
- Ignored error results are gone, routed through typed results, or documented as accepted
  best-effort cleanup paths.
