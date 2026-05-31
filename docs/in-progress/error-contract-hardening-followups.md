# Error Contract Hardening — Follow-Up Gaps

Status: in-progress review note. This captures gaps found while reviewing the staged
implementation of `error-contract-hardening-plan.md`. The production migration landed well; this
list is the remaining work needed before the change meets that plan's acceptance criteria.

## Summary

The production code is faithful to the plan and, in the ignored-path work, tighter than it asked
for. The shortfalls are concentrated in the test stage and two specific Stage 4 items.

## Gap 1 (highest priority): Stage 6 test coverage does not exercise the new contract

The test changes are almost entirely mechanical `CHECK(x)` to `CHECK(x.has_value())` rewrites that
preserve the previous coverage. They do not assert the new typed failure behavior.

Evidence:

- `SongAudioErrorCode`, `AudioDeviceConfigurationErrorCode`, and `EditorSettingsErrorCode` never
  appear in a `.error().code == ...` assertion. They appear only inside the test fakes.
- The new failure-injection knobs are defined but never set by any test body:
  - `next_prepare_error`
  - `next_set_active_arrangement_error`
  - `next_clear_active_arrangement_error`
  - `next_restore_serialized_device_state_error`
  - `next_cancel_error`
- `MonitoringRouteFailed` has zero test references.

Required coverage from the plan that is still missing:

- `ISongAudio` preparation failure reports a typed code and useful path/message context (assert the
  specific `SongAudioErrorCode` and that the message carries the offending path).
- `ISongAudio` active-arrangement failure reports backend setup failure rather than a bare empty
  result (assert the code, not just `!has_value()`).
- Serialized device restore distinguishes `InvalidSerializedState` (unparseable XML) from
  `RestoreFailed` (backend rejected the state).
- Audio-device `cancel()` restore failure keeps the dialog open / surfaces the error instead of
  closing as if it succeeded. The fake already supports `next_cancel_error`; drive it.
- Live-input monitoring rebuild failure cannot report success for a user-visible operation (e.g.
  `setActiveArrangement` returning `MonitoringRouteFailed`).
- Best-effort cleanup failures are routed through the approved helper (assert the helper logs and
  the primary result is still returned).
- `EditorSettings` typed failure codes, including the corrupt-history path: `inputCalibrationFor`
  on malformed JSON returns `InvalidInputCalibrationHistory` rather than empty absence.

Highest-value tests to add first:

1. Serialized-restore test asserting `InvalidSerializedState` vs `RestoreFailed`.
2. `prepareSong` failure test asserting the specific code plus path in the message.
3. Controller test driving `next_cancel_error` to confirm the dialog stays open on restore failure.

## Gap 2: `showControlPanel()` bool return is still ignored

`rock-hero-common/audio/src/audio_device_settings.cpp` `openControlPanel()` calls
`m_staged_device->showControlPanel()` (around line 559), ignores the returned bool, and
unconditionally calls `refreshState({})` and returns success. Stage 4 explicitly listed
"`showControlPanel()` false return in `AudioDeviceSettings`" as a case to handle. Either translate
a false return into a typed failure, or leave a comment explaining why the bool is not meaningful
for the supported backends (e.g. ASIO).

## Gap 3: two discards do not meet the discard rule this change introduced

The updated convention requires explicit discards to be destructor-only cleanup, best-effort
rollback after a primary error, or a named best-effort helper with a comment or log.

- `rock-hero-editor/core/src/audio_device_settings_controller.cpp` `onControlPanelRequested()`:
  `[[maybe_unused]] const auto opened = m_settings.openControlPanel();` relies on the
  `refreshState` + `updateView()` side channel to surface the error, but has no comment saying so.
  Add a comment, or route through a named helper.
- The destructor `cancel()` discard in the same file is acceptable as destructor context, but is
  also uncommented.

## Smaller items to confirm (not necessarily defects)

- Cross-domain code mirroring is slightly heavier than the convention advises ("add mirrored codes
  only after callers demonstrably need to branch"). `MonitoringRouteFailed` was added to both
  `PluginHostError` and `LiveRigError`, and the `*FromLiveInputError` mappers preserve
  `MessageThreadRequired` / `TrackMissing`, yet no caller branches on these codes yet. The three
  mappers are also inconsistent: two preserve fine-grained codes while
  `songAudioErrorFromLiveInputError` collapses to a single code. Confirm this is intentional.
- Behavior change in the `MeasurementRestore` branches of `editor_controller.cpp`: reordering to
  disable-monitoring-then-clear-state means a failed disable now leaves the
  measurement/calibration state uncleared and returns the error. This is the safer behavior and
  likely intended, but it is an unpinned behavior change — add a test if it matters.
- Settings persistence failures are uniformly log-only via `recordSettingsResultBestEffort`. The
  plan permits this, but it means a failed `saveInputCalibration` is invisible to the user. The
  acceptance criterion "user-visible settings failures produce typed diagnostics" is currently met
  only at log level. Confirm that is the intended UX rather than a status- or modal-level surface.
- `loadSessionSong` maps "editor session rejected the song" to `BackendClipInsertionFailed`, which
  is semantically loose (it is not a clip-insertion failure). It is an assert-shouldn't-happen
  path with no better existing code; leave as-is or add a dedicated code if it can occur in
  practice.

## Acceptance criteria still open

From the original plan, these remain unmet until the above is addressed:

- "tests cover the new failure paths" — not yet; see Gap 1.
- "ignored error results are either gone or routed through named best-effort cleanup" — mostly
  done; `showControlPanel()` (Gap 2) and the uncommented discard (Gap 3) are the exceptions.
