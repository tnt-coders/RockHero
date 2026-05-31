# Error Contract Hardening Plan

Status: in-progress planning note. This captures the active project-wide direction for
recoverable error contracts, ignored result handling, and the design-doc updates needed to make
those rules durable.

Approval note: the durable rule change that removes raw `std::string` error channels, including
private helper channels, has been explicitly approved. This is an intentional tightening of the
current `docs/design/coding-conventions.md` allowance for private raw string helpers, not routine
wording cleanup.

## Purpose

Rock Hero already has a good typed-error pattern in many public APIs, but the codebase still has
older failure shapes that can hide meaningful diagnostics:

- public `bool` or `void` APIs for operations that can fail;
- private `std::optional<Error>` command helpers;
- private `std::optional<T>` plus error-code or error-message out-parameters;
- bare `std::expected<T, std::string>` async/helper channels;
- ignored `std::expected` or framework failure results in user-visible workflows.

The goal is to make recoverable failures branchable, testable, and visible until the final
UI/logging boundary. Users should not end up with unexpected behavior because the application
silently discarded an error.

## Design Docs To Update

The implementation should start by updating durable design guidance, not just local code. The
significant `docs/design/` rule change has been approved for this plan.

### `docs/design/coding-conventions.md`

Replace and tighten the current recoverable-error guidance:

- Public project-owned recoverable operations return
  `[[nodiscard]] std::expected<T, DomainError>`.
- Public commands with no success payload return
  `[[nodiscard]] std::expected<void, DomainError>`.
- `bool` is only for predicates or state queries, never for "operation failed."
- `std::optional<T>` is only for true absence, nullable state, cache miss, or lookup miss.
- `[[nodiscard]] std::expected<std::optional<T>, Error>` is the correct shape when absence is
  valid but retrieval or parsing can fail.
- Do not introduce `std::optional<Error>` as an operation result.
- Do not introduce `std::optional<T>` plus `error_code` or `error_message` out-parameters.
- Do not use bare `std::string` as an error channel.
- `std::string` is allowed as the diagnostic `message` field inside a typed error, or as final
  rendered UI text after a typed error has reached the presentation boundary.
- Private helpers should use the owning domain error type. If no owning type exists, add a small
  domain error rather than returning `std::string`.
- Explicit discards of error-returning calls require a destructor-only context or a named
  best-effort helper with a comment or log.

This should remove the existing allowance for private raw string errors. The simpler rule is:
actual error channels use actual error types.

### `docs/design/architectural-principles.md`

Strengthen "Typed Boundary Errors" so it says:

- project-owned boundaries preserve typed, branchable errors;
- adapters translate third-party/framework failures into project-owned domain errors;
- typed errors should be carried through async plumbing until the final UI/logging boundary;
- callers must not parse message text for behavior;
- side-effect failures that can affect user-visible state must be returned or surfaced, not
  silently folded into state.

### `docs/design/architecture.md`

Update the audio/editor boundary descriptions once the APIs change:

- `ISongAudio` reports typed preparation and activation failures.
- `IAudioDeviceConfiguration` reports typed serialized-device restore failures.
- `IEditorSettings` reports typed persistence failures where write failure matters.
- `rock-hero-common/audio` owns audio-backend error domains; `rock-hero-editor/core` owns
  editor workflow and settings error domains.

### `docs/design/documentation-conventions.md`

No new rule is likely required. When the new public error APIs are added, follow the existing
Doxygen requirements for public headers, enum values, parameters, and return documentation.

## Firm Standard

Every recoverable operation has one of these shapes:

- `[[nodiscard]] std::expected<T, DomainError>` for operations with a success payload.
- `[[nodiscard]] std::expected<void, DomainError>` for commands.
- `std::optional<T>` only for absence or state, not failure.
- `[[nodiscard]] std::expected<std::optional<T>, DomainError>` when both absence and failure are
  meaningful.

Every public failure domain owns:

- `<Subject>ErrorCode`;
- `struct [[nodiscard]] <Subject>Error`;
- a stable `code` member;
- a human-readable `message` member.

Do not add:

- `std::expected<T, std::string>` for an error channel;
- `std::optional<Error>` for an operation result;
- `bool` as an operation failure result;
- `void` setters that can fail to persist, restore, route, open, or otherwise affect user-visible
  state.

Acceptable result discards:

- destructor cleanup where no caller-visible channel exists;
- best-effort rollback after a primary error has already been captured;
- deliberate side-effect-only reads that return non-error values and have a local explanatory
  comment.

All other error-returning results must be handled, propagated, reported, or intentionally routed
through a named best-effort helper.

## Current Audit Summary

Good existing patterns:

- Most public `std::expected` APIs are already `[[nodiscard]]`.
- Public error structs such as `ProjectError`, `SongPackageError`, `ArchiveError`,
  `LiveRigError`, `LiveInputError`, `PluginHostError`, `AudioDeviceSettingsError`,
  `AudioNormalizationError`, `InputCalibrationError`, and `SongImportError` already follow the
  stable-code-plus-message pattern.
- Many `std::optional<T>` uses model true absence or state and should remain unchanged.

High-value cleanup targets:

- `rock-hero-common/audio/include/rock_hero/common/audio/i_song_audio.h`
  exposes `prepareSong()` and `setActiveArrangement()` as `bool`.
- `rock-hero-common/audio/include/rock_hero/common/audio/i_audio_device_configuration.h`
  exposes `restoreSerializedDeviceState()` as `bool`.
- `rock-hero-common/audio/include/rock_hero/common/audio/audio_device_settings.h`
  exposes user-initiated `cancel()` as `void` even though route restore can fail.
- `rock-hero-editor/core/include/rock_hero/editor/core/i_editor_settings.h`
  exposes write operations as `void` even though persistence can fail.
- `rock-hero-editor/core/include/rock_hero/editor/core/i_editor_settings.h`
  exposes calibration lookup as `std::optional<T>` even though malformed persisted JSON is
  currently detectable and can be collapsed into absence.
- `rock-hero-common/audio/src/engine.cpp` uses `std::optional<LiveRigError>` and
  `std::optional<LiveInputError>` as command helper results.
- `rock-hero-common/audio/src/audio_normalization.cpp`,
  `rock-hero-common/core/src/rock_song_package.cpp`, and
  `rock-hero-editor/core/src/project.cpp` contain private
  `std::optional<T>` plus `error_message` or `error_code` out-parameter helpers.
- `rock-hero-editor/core/src/editor_controller.cpp` contains private live-rig async plumbing that
  converts typed `LiveRigError` to `std::string` before the final UI boundary.
- `rock-hero-editor/core/src/editor_controller.cpp` and
  `rock-hero-common/audio/src/engine.cpp` ignore live-input monitoring route failures in several
  success and cleanup paths.
- `rock-hero-editor/core/src/audio_device_settings_controller.cpp` discards
  `openControlPanel()` failures.
- `rock-hero-common/audio/src/audio_device_settings.cpp` ignores Cancel route-restore failures
  and control-panel display failures.
- `rock-hero-editor/core/src/editor_settings.cpp` ignores settings save failures.
- `rock-hero-common/audio/src/engine.cpp` ignores JUCE device-manager restore errors after
  parsing serialized device XML.
- `rock-hero-common/audio/include/rock_hero/common/audio/input_calibration.h` contains
  `InputCalibrationCaptureUpdate::error` as optional failure state inside a snapshot/result struct.
  This is not an operation result, but the Stage 7 audit must classify it explicitly.

## Stage 1: Encode The Design Standard

1. Update `docs/design/coding-conventions.md` with the firm standard above.
2. Update `docs/design/architectural-principles.md` to require typed errors across boundaries and
   async plumbing.
3. Update `docs/design/architecture.md` after the affected ports are migrated so the boundary
   descriptions match implemented APIs.
4. Keep `docs/design/documentation-conventions.md` unchanged unless new public API documentation
   reveals a genuine gap.
5. Record in the design-doc wording that the private raw-string helper allowance has been
   deliberately removed.

## Stage 2: Add Missing Error Domains

Add small typed error domains where the current boundary has recoverable failure but no domain
error type.

Recommended new domains:

- `SongAudioError` for song preparation and active-arrangement playback setup.
- `AudioDeviceConfigurationError` for serialized restore and device route diagnostics.
- `EditorSettingsError` for persisted app-local settings writes.

Prefer reusing existing domains when ownership is clear:

- `LiveInputError` for live-input monitoring route operations.
- `LiveRigError` for tone document, plugin-state, and live rig capture/load operations.
- `ProjectError` for project package and editor project workflow failures.
- `AudioDeviceSettingsError` for staged audio-settings dialog operations.

Do not add a global `AnyError`, project-wide error enum, or polymorphic error hierarchy.

## Stage 3: Migrate Public Failure APIs

### Song Audio

Convert:

- `ISongAudio::prepareSong(common::core::Song&)`;
- `ISongAudio::setActiveArrangement(const common::core::Arrangement&)`;
- the matching `Engine` overrides and test fakes.

Target shape:

```cpp
[[nodiscard]] virtual std::expected<void, SongAudioError> prepareSong(
    common::core::Song& song) = 0;

[[nodiscard]] virtual std::expected<void, SongAudioError> setActiveArrangement(
    const common::core::Arrangement& arrangement) = 0;
```

The error codes should preserve at least:

- missing audio asset path;
- unreadable audio file;
- invalid or non-positive audio duration;
- missing backing track;
- backend clip insertion failure.

The editor should surface the message instead of reporting generic load failure text.

Known migration blast radius:

- `rock-hero-common/audio/tests/include/rock_hero/common/audio/testing/configurable_song_audio.h`;
- `rock-hero-common/audio/tests/test_song_audio.cpp`;
- `rock-hero-common/audio/tests/test_engine.cpp`;
- editor-controller tests that reach `ISongAudio` through the configurable test harness.

### Audio Device Configuration

Convert:

- `IAudioDeviceConfiguration::restoreSerializedDeviceState`;
- `Engine::restoreSerializedDeviceState`;
- controller restore handling and tests.

Target shape:

```cpp
[[nodiscard]] virtual std::expected<void, AudioDeviceConfigurationError>
restoreSerializedDeviceState(const std::string& serialized_state) = 0;
```

The error codes should distinguish at least:

- invalid serialized XML;
- device-manager restore/open failure with JUCE's error text preserved.

The implementation must handle `AudioDeviceManager::initialise(...)` failure instead of returning
success after XML parsing.

Open design decision before migration: decide whether `restoreSerializedDeviceState()` remains a
public message-thread-only API. If it does, add a `MessageThreadRequired` code and document the
thread rule. If it does not, move message-thread marshalling to the owning adapter or app layer
before exposing the typed result.

Known migration blast radius:

- `rock-hero-common/audio/tests/include/rock_hero/common/audio/testing/`
  `configurable_audio_device_configuration.h`;
- `rock-hero-editor/ui/tests/test_editor.cpp`;
- editor-controller restore tests and any controller code that clears bad persisted device state.

### Editor Settings

Convert settings reads and writes where persistence or parsing failure matters:

- `setLastOpenProject`;
- `setInterruptedRestoreProject`;
- `setAudioDeviceState`;
- `saveInputCalibration`;
- `removeInputCalibration`.
- `inputCalibrationFor`, unless malformed persisted calibration history is deliberately classified
  as absence and documented as such.

Target shape:

```cpp
[[nodiscard]] virtual std::expected<void, EditorSettingsError> setLastOpenProject(
    std::optional<std::filesystem::path> project_file) = 0;
```

Apply the same shape to the other mutating settings calls. The controller can decide which
failures should be modal, status-only, or log-only, but the persistence layer should not hide
them.

Use `std::expected<std::optional<common::audio::InputCalibrationState>, EditorSettingsError>` for
`inputCalibrationFor()` if malformed settings data should be diagnosable. Keep simple nullable
state reads such as `lastOpenProject()` and `audioDeviceState()` as `std::optional<T>` unless a
specific read failure is introduced.

Known migration blast radius:

- `rock-hero-editor/core/include/rock_hero/editor/core/editor_settings.h`;
- `rock-hero-editor/core/src/editor_settings.cpp`;
- `rock-hero-editor/core/tests/include/rock_hero/editor/core/testing/null_editor_settings.h`;
- `rock-hero-editor/core/tests/test_editor_settings.cpp`;
- editor-controller restore, lifecycle, and input-calibration tests that call settings setters
  directly.

### Audio Device Settings

Convert user-initiated cancel because it can fail while restoring the previously-open route:

```cpp
[[nodiscard]] virtual std::expected<void, AudioDeviceSettingsError> cancel() = 0;
```

Destructor-only route restore remains a best-effort cleanup path because there is no caller-visible
channel. It should route failures through a named helper with a comment or log.

The controller should not close the settings dialog after a user Cancel request when restoring the
previous route fails. It should refresh the view state with the typed error message and let the
user choose how to proceed.

Known migration blast radius:

- `rock-hero-common/audio/include/rock_hero/common/audio/audio_device_settings.h`;
- `rock-hero-common/audio/src/audio_device_settings.cpp`;
- `rock-hero-common/audio/tests/test_audio_device_settings.cpp`;
- `rock-hero-editor/core/src/audio_device_settings_controller.cpp`;
- `rock-hero-editor/core/tests/test_audio_device_settings_controller.cpp`.

## Stage 4: Harden Ignored Result Paths

### Live Input And Monitoring Routes

Convert `applyInstrumentMonitoringRoute()` and `rebuildInstrumentMonitoringGraph()` in
`rock-hero-common/audio/src/engine.cpp` to:

```cpp
[[nodiscard]] std::expected<void, LiveInputError> applyInstrumentMonitoringRoute();
[[nodiscard]] std::expected<void, LiveInputError> rebuildInstrumentMonitoringGraph();
```

Then audit every caller.

Decision rules:

| Pattern | Required handling |
|---|---|
| Required route on public success | Propagate `LiveInputError`; do not report success. |
| Rebuild fails after graph mutation | Return failure from the graph mutation operation. |
| Disable on success/direct user action | Propagate or report; do not claim disabled. |
| Disable during rollback after primary error | Preserve primary; route secondary through helper. |
| Destructor/stale/superseded cleanup | Named best-effort helper; no raw discard. |

Do not special-case `InputRouteUnavailable` as success for a disable operation unless the caller
has already established from current state that no live route exists. Otherwise it is still a
failure to prove the safe state.

### EditorController Live Input

Replace the `std::ignore` calls around live-input enable/disable, calibration commit, and route
restore with one of:

- returned `LiveInputError`;
- user-visible `reportError(...)`;
- state update marking live input unavailable;
- named best-effort cleanup helper.

Safety-sensitive operations that disable monitoring should not fail silently if a failure can
leave guitar input audible, armed, or inconsistent with the UI.

Most existing `std::ignore` sites are disable or restore paths. Apply the table above instead of
making site-by-site ad-hoc decisions.

### Audio Device Settings

Handle:

- `openControlPanel()` failure in `AudioDeviceSettingsController`;
- control-panel availability or backend display failure in `AudioDeviceSettings` where the
  framework exposes a failure signal;
- user-initiated Cancel route-restore failure in `AudioDeviceSettings`.

The dialog should keep or display an error when the requested operation failed, rather than closing
or refreshing as if the operation succeeded.

Current JUCE `AudioIODevice::showControlPanel()` usage is a `void` request in this code path. Do
not invent a failure result for it; validate availability before dispatch and preserve typed
backend errors only where the backend API actually reports one.

### Settings Persistence

Stop ignoring `PropertiesFile::save()` and `saveIfNeeded()` results where persistence affects
restore, calibration, or audio-device state.

## Stage 5: Normalize Private Helpers

Replace private error-return helper shapes as touched.

Targets:

- `std::optional<LiveRigError>` helpers in `engine.cpp` become
  `std::expected<void, LiveRigError>`.
- `std::optional<LiveInputError>` helpers in `engine.cpp` become
  `std::expected<void, LiveInputError>`.
- `audio_normalization.cpp` helper failures become `std::expected<T, AudioNormalizationError>` or
  another typed private error where appropriate.
- `rock_song_package.cpp` helper failures become `std::expected<T, SongPackageError>` or a
  private typed helper error translated immediately at the public boundary.
- `project.cpp::createWorkspaceDirectory()` becomes
  `std::expected<std::filesystem::path, ProjectError>`.
- `editor_controller.cpp` live-rig async plumbing carries `LiveRigError` or `ProjectError` until
  the final UI reporting boundary instead of converting to `std::string`.

Keep ordinary lookup and state optionals unchanged.

`InputCalibrationCaptureUpdate::error` may remain `std::optional<InputCalibrationError>` if it is
documented as phase-associated snapshot state rather than an operation failure result. The
operation boundary remains `calculateInputCalibration()` returning
`std::expected<InputCalibrationResult, InputCalibrationError>`. If this distinction becomes
unclear in tests or callers, replace the update payload with a named variant/result type.

## Stage 6: Tests

Add or update tests close to each changed module.

Required coverage:

- `ISongAudio` preparation failure reports a typed code and useful path/message context.
- `ISongAudio` active-arrangement failure reports backend setup failure rather than `false`.
- Serialized device restore distinguishes invalid XML from JUCE restore/open failure.
- Settings write failures propagate through `IEditorSettings` where practical.
- Audio-device control-panel and cancel failures update view state or report an error.
- Live-input monitoring rebuild failure cannot report success for user-visible operations.
- Best-effort cleanup failures are explicitly routed through the approved helper.

Existing tests that use `std::optional<Error>` as fake "next failure" knobs can keep that shape.
Those optionals model test configuration, not production error contracts.

Existing test migration must be part of the work, not a follow-up. In particular:

- replace `bool` assertions for `prepareSong()` and `setActiveArrangement()` with `has_value()`
  and code/message assertions where failure is expected;
- update configurable song-audio and audio-device-configuration fakes to return typed expected
  values;
- update `NullEditorSettings` and local fake settings implementations to return
  `EditorSettingsError` expected values for mutating calls;
- update audio-device-settings controller fakes so Cancel can fail and the controller tests cover
  the failure path.

## Stage 7: Review Gate And Follow-Up Audits

Do not add pre-commit or custom clang-tidy enforcement as part of this plan unless the manual gate
proves too fragile. The first durability mechanism is a required review gate: the implementation
is not accepted until the audit commands below have been run, every remaining hit is classified,
and intentional exceptions are documented in the PR or final implementation notes.

After the migrations, run:

- `rg "std::expected<[^>]+, std::string" rock-hero-common rock-hero-editor rock-hero-game`
- `rg "std::optional<[^>]*(Error|error)" rock-hero-common rock-hero-editor rock-hero-game`
- `rg "std::ignore|static_cast<void>|\\(void\\)" rock-hero-common rock-hero-editor rock-hero-game`
- `rg "error_message|error_code" rock-hero-common rock-hero-editor rock-hero-game`

For each remaining hit, classify it as one of:

- true absence, cache state, lookup miss, or test configuration;
- phase-associated result state such as `InputCalibrationCaptureUpdate::error`;
- destructor-only or best-effort cleanup routed through the approved helper;
- a follow-up defect that must be fixed before this plan is accepted.

If future changes repeatedly erode the rule, add the smallest practical automated guard first:
a pre-commit grep script with a narrow allowlist. A custom clang-tidy matcher is a later option
only if grep becomes too noisy or too easy to bypass.

## Non-Goals

- Do not ban `std::optional<T>` for true absence or state.
- Do not add a single global error type.
- Do not mirror every low-level framework failure code through high-level APIs unless callers need
  to branch on it.
- Do not turn UI display strings into program behavior.
- Do not refactor unrelated architecture while touching error contracts.
- Do not update stale `docs/todo/` plans as part of this work unless the specific plan is being
  implemented.

## Acceptance Criteria

The work is complete when:

- durable design docs clearly encode the standard;
- public recoverable operations no longer use `bool`, `void`, `std::optional<Error>`, or raw
  string error channels;
- important private helpers use typed `std::expected` contracts;
- ignored error results are either gone or routed through named best-effort cleanup;
- user-visible restore, routing, settings, and audio-device failures produce typed diagnostics;
- tests cover the new failure paths;
- the Stage 7 review gate has been run, remaining hits are classified, and any accepted
  exceptions are documented.
