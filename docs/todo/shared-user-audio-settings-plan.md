# Audio Settings Ownership and Boundary Plan

Status: planned. Consolidates the former `audio-device-configuration-boundary-plan.md` (now removed)
so audio-settings ownership, the source-of-truth boundary, and the device-configuration port live in
one place. Implement after the required input calibration workflow has a stable first pass, or fold
into that work if the game starts using the same audio-device setup soon. UI-view follow-ups from the
completed settings-view extraction are tracked separately in
`audio-device-settings-extraction-followups.md`.

## Goal

Give Rock Hero one coherent ownership model for audio-related settings across the Editor and the
(future) Game: share the per-user audio *hardware* state, keep product workflow state product-local,
and define a single source of truth for audio-device configuration so it cannot drift between stores.

## Source-of-Truth Boundary

Three stores touch "audio settings." Each owns a disjoint slice; nothing is authoritative in two
places.

| Store | Writer | Authoritative for | Shared? |
|---|---|---|---|
| Tracktion `Settings.xml` (`Engine` `PropertyStorage`) | Tracktion, internally | Known-plugins list / scan results and Tracktion-internal bookkeeping | Yes -- created in `rock-hero-common/audio` with the shared `"Rock Hero"` name (`engine.cpp`), so both products hit the same file |
| `UserAudioSettings` (new, `rock-hero-common/audio`) | RockHero controllers via a typed port | The user's audio-device configuration (opaque device-manager state) and per-route input calibration | Yes (editor + game) |
| `EditorSettings` (`rock-hero-editor/core`) | Editor controller | Editor workflow/project state (`lastOpenProject`, interrupted-restore marker, project cursor positions) | No (editor-only) |

Rules:

- **Audio-device configuration is owned by `UserAudioSettings`, not Tracktion's `Settings.xml`.**
  On startup the controller restores the opaque device state from `UserAudioSettings` and applies it
  to the engine's device manager. Tracktion's own device-state persistence in `Settings.xml` is a
  non-authoritative internal cache: the project-owned restore overwrites it, and it is never read as
  the authority. This prevents two restorers racing on startup. The editor already behaves this way
  via `restoreAudioDeviceState()`/`persistAudioDeviceState()`; this plan only moves the backing store
  from `EditorSettings` to the shared `UserAudioSettings`.
- **The plugin scan / known-plugins list stays owned by Tracktion's `Settings.xml`.** RockHero reads
  it through the plugin manager and does not duplicate it into a project-owned store. Because that
  file is already shared, the editor's scan benefits the game for free.
- **Editor workflow/project state stays in `EditorSettings`** and is never shared.
- A value must have exactly one authoritative store. If a future setting could plausibly live in two
  (e.g. buffer size, which Tracktion also tracks), it belongs to `UserAudioSettings`, and Tracktion's
  copy is whatever results from applying the project-owned state -- never an independent source.

### Consequence for shared vs per-product device config

Because device config is project-owned (not an artifact of Tracktion's shared `Settings.xml`), the
*granularity* of sharing is a deliberate project decision, not a Tracktion accident:

- Default: editor and game share one device configuration via `UserAudioSettings` (configure once).
- If the game later needs a different route or a lower-latency buffer than the editor, that becomes an
  explicit per-product override on top of the shared store, rather than something forced by sharing
  Tracktion's file. Out of scope until the game needs it; noted so the door stays open.

## Scope of shared values

The shared store contains only audio state that should follow the same user between both apps:

- selected/restored audio-device manager state;
- per-physical-route input calibration history.

Editor-only settings remain editor-only: last open editor project, interrupted startup-restore
marker, project cursor positions, and future editor window/workflow preferences.

## Current Shape

`rock-hero-editor/core` owns `EditorSettings`, which stores app-local settings through
`juce::PropertiesFile`.

Current editor settings include:

- `lastOpenProject`;
- `interruptedRestoreProject`;
- `audioDeviceState`.

The editor settings file is configured with application name `Rock Hero Editor` and folder name
`Rock Hero`, so the data is per-user but still tied to the editor application's settings file.
The game will not naturally read it unless it duplicates that exact file contract, which would
also expose editor-only keys.

## Design Direction

Add a shared user-audio settings boundary in `rock-hero-common/audio`. Both apps already depend on
common audio for hardware and live-rig behavior, and the values being shared are audio-specific.

Do not make all app settings global. Split settings into two stores:

```text
Rock Hero/
  Rock Hero User Audio.settings    shared by editor and game
  Rock Hero Editor.settings        editor-only workflow state
  Rock Hero.settings               future game-only workflow state, if needed
```

The exact file name is controlled by JUCE `PropertiesFile::Options`, not hard-coded path strings.
The shared store should use the same user folder across both apps:

- `folderName = "Rock Hero"`;
- `applicationName = "Rock Hero User Audio"`;
- `filenameSuffix = ".settings"`;
- `commonToAllUsers = false`;
- `storageFormat = juce::PropertiesFile::storeAsXML`.

## Planned Public API

Add:

- `rock-hero-common/audio/include/rock_hero/common/audio/i_user_audio_settings.h`
- `rock-hero-common/audio/include/rock_hero/common/audio/user_audio_settings.h`
- `rock-hero-common/audio/src/user_audio_settings.cpp`

### `IUserAudioSettings`

Project-owned interface for controller and app composition code:

```cpp
class IUserAudioSettings
{
public:
    virtual ~IUserAudioSettings() = default;

    [[nodiscard]] virtual std::optional<std::string> audioDeviceState() const = 0;
    virtual void setAudioDeviceState(std::optional<std::string> xml_state) = 0;

    // Per-physical-route input calibration, relocated from IEditorSettings unchanged.
    [[nodiscard]] virtual std::expected<std::optional<InputCalibrationState>, SettingsError>
    inputCalibrationFor(const InputDeviceIdentity&) const = 0;
    [[nodiscard]] virtual std::expected<void, SettingsError> saveInputCalibration(
        InputCalibrationState) = 0;
    [[nodiscard]] virtual std::expected<void, SettingsError> removeInputCalibration(
        const InputDeviceIdentity&) = 0;
};
```

Calibration is **per physical input route**, not a single gain. The existing per-route calibration
contract and its `inputCalibrationStates` XML schema (now in `EditorSettings`, stored as an
XML-valued property) move into `UserAudioSettings` unchanged. The typed settings-error type moves
with them into `rock-hero-common/audio` (rename `EditorSettingsError`, or introduce a common
`SettingsError`/`UserAudioSettingsError`); `EditorSettings` keeps using the same error type for its
remaining editor-only methods. Keep `Gain` (not raw `double`) at the boundary so clamping stays near
the audio layer.

### `UserAudioSettings`

Concrete JUCE-backed implementation:

```cpp
class UserAudioSettings final : public IUserAudioSettings
{
public:
    UserAudioSettings();
    explicit UserAudioSettings(const std::filesystem::path& settings_file);

    [[nodiscard]] std::optional<std::string> audioDeviceState() const override;
    void setAudioDeviceState(std::optional<std::string> xml_state) override;

    [[nodiscard]] std::expected<std::optional<InputCalibrationState>, SettingsError>
    inputCalibrationFor(const InputDeviceIdentity&) const override;
    [[nodiscard]] std::expected<void, SettingsError> saveInputCalibration(
        InputCalibrationState) override;
    [[nodiscard]] std::expected<void, SettingsError> removeInputCalibration(
        const InputDeviceIdentity&) override;

private:
    juce::InterProcessLock m_process_lock;
    juce::PropertiesFile m_properties;
};
```

The explicit-path constructor keeps tests isolated from real user settings.

### Keys And Constants

In `user_audio_settings.cpp`, add:

- `constexpr const char* g_audio_device_state_key{"audioDeviceState"}`
- `constexpr const char* g_input_calibration_states_key{"inputCalibrationStates"}` (XML-valued)
- `constexpr const char* g_user_audio_settings_application_name{"Rock Hero User Audio"}`
- `constexpr const char* g_user_audio_settings_folder_name{"Rock Hero"}`
- `constexpr const char* g_user_audio_settings_process_lock_name{"RockHeroUserAudioSettings"}`

Reuse the existing calibration storage code verbatim when it moves over: the per-route history is an
XML-valued property (`PropertiesFile::setValue(key, XmlElement*)` / `getXmlValue(key)`) with a
`formatVersion` root and one validated record per route. Carry the same validation, malformed-XML
preservation (report a typed error, never overwrite), duplicate-route collapse, and `clampGain`
behavior already covered by `test_editor_settings.cpp`.

Audio-device-state behavior:

- missing or empty `audioDeviceState` returns `std::nullopt`;
- empty `audioDeviceState` write removes the key;
- writes call `saveIfNeeded()` immediately, matching current editor settings behavior.

## Editor Integration

Keep `EditorSettings` for editor-only state:

- `lastOpenProject()` / `setLastOpenProject(...)`;
- `interruptedRestoreProject()` / `setInterruptedRestoreProject(...)`;
- `projectCursorPositionFor(...)` / `saveProjectCursorPosition(...)`.

Move audio state out of `EditorSettings` into `UserAudioSettings`:

- remove `audioDeviceState()` / `setAudioDeviceState(...)`;
- move the existing per-route calibration methods `inputCalibrationFor(...)`,
  `saveInputCalibration(...)`, and `removeInputCalibration(...)` (and their `inputCalibrationStates`
  XML schema) over unchanged. They currently live in `EditorSettings`; this is a relocation, not a
  rewrite.

Update `EditorController::Services`:

- keep `EditorSettings* settings` for editor restore/workflow state;
- add `common::audio::IUserAudioSettings* user_audio_settings`.

Update `EditorController::Impl`:

- rename settings helpers so ownership is clear:
  - `restoreAudioDeviceState()` reads from `m_user_audio_settings`;
  - `persistAudioDeviceState()` writes to `m_user_audio_settings`;
  - input calibration load/save reads and writes `m_user_audio_settings`;
  - last-open-project and interrupted-restore logic still uses `m_settings`.

Update `rock-hero-editor/app/main.cpp`:

- construct one `common::audio::UserAudioSettings user_audio_settings`;
- pass it into `EditorController::Services`;
- continue constructing `editor::core::EditorSettings editor_settings` for editor-only state.

## Game Integration

When the game gains real audio-device startup and input calibration UI:

- construct `common::audio::UserAudioSettings` in `rock-hero-game/app`;
- restore `audioDeviceState()` into the game's audio device manager;
- use `inputCalibrationFor(route)` to apply the same calibration for the active physical route;
- write back only on successful audio-device apply and calibration success.

Do not make the game depend on `rock-hero-editor/core` to access shared settings.

## Audio-Device Configuration Port

(Consolidated from the former `audio-device-configuration-boundary-plan.md`.)

`IAudioDeviceConfiguration` currently exposes `juce::AudioDeviceManager&`, which lets editor and game
code bypass project-owned device operations. When the game gains its own audio settings UI, narrow
this port so both products drive the same headless device choices without touching the JUCE manager.

Keep JUCE device ownership inside `rock-hero-common/audio` and expose project-owned operations:

- current route status (`AudioDeviceRouteSnapshot`): backend, device names, channels, sample rate,
  buffer size, bit depth, latency text, open/closed state;
- selectable choices (`AudioDeviceChoiceCatalog`): audio systems, devices, channels, sample rates,
  and buffer sizes for the staged route;
- a selection/apply command value (`AudioDeviceRouteSelection`);
- route apply / cancel / test-output / control-panel commands;
- serialized device-state restore and capture via a small `AudioDeviceConfigurationStore`, instead of
  exposing `createStateXml()` / `initialise()` to product controllers.

`AudioDeviceConfigurationStore` is where this port meets the source-of-truth boundary above: it
restores and captures the opaque device-manager state against `UserAudioSettings` (the authority),
not against Tracktion's `Settings.xml`. The `juce::AudioDeviceManager&` accessor becomes private
adapter surface, kept only on the concrete adapter for app composition and low-level adapter tests.

Refactor steps:

1. Add project-owned snapshot/catalog/selection values beside the existing settings state.
2. Route `EditorController`'s restore/persist through `IAudioDeviceConfiguration` /
   `AudioDeviceConfigurationStore` so it no longer reaches into `deviceManager()`.
3. Narrow `AudioDeviceSettings` to the enumeration/apply operations it needs rather than the full
   manager where practical.
4. Keep a temporary manager escape hatch only in adapter tests / app composition if a full removal
   would make the migration too large in one step.
5. Once the game settings UI exists, confirm both products use the common backend with no
   product-specific JUCE manager calls, then remove the public manager accessor.

Test this at the `AudioDeviceSettings` / `IAudioDeviceConfiguration` API level with hand-written
fakes for snapshots and command results; keep JUCE-manager tests on the concrete adapter; assert
typed error codes rather than parsing display text.

## Migration

The first implementation should migrate existing editor audio settings once:

1. Open `EditorSettings`.
2. Open `UserAudioSettings`.
3. If shared `audioDeviceState()` is missing and editor `audioDeviceState()` exists, copy the editor
   value into shared settings.
4. The per-route calibration history (`inputCalibrationStates`) already ships in `EditorSettings`, so
   it must migrate too: if the shared store has no calibration history and the editor store does,
   copy the records over (same "copy only when shared is missing" policy). The XML schema is
   identical, so this is a value copy, not a reformat.
5. Leave the old editor keys in place for one release, or clear them immediately only if the
   migration is covered by tests and there is no rollback concern.

Planned helper:

- `void migrateEditorAudioSettings(EditorSettings& editor_settings,
  common::audio::IUserAudioSettings& user_audio_settings)`

Place this helper in `rock-hero-editor/core` because it knows about the old editor-specific store
and the new common-audio store. Do not put editor migration policy in `rock-hero-common/audio`.

## Concurrency Policy

Both apps may be running under the same user account.

V1 policy:

- use `juce::InterProcessLock` through `PropertiesFile::Options::processLock`;
- write shared settings only on successful audio-device apply and calibration success;
- do not write shared settings during ordinary playback or meter updates;
- use last-writer-wins for concurrent successful applies;
- do not clear shared settings merely because an app starts without an open device.

This is intentionally simple. If cross-process settings conflicts become user-visible later, add a
small "settings changed externally" refresh path rather than overbuilding now.

## Covered Cases

1. Editor starts first with no shared settings and no old editor audio setting.
   - No audio-device state restores.
   - Input calibration is missing.

2. Editor starts with old editor audio-device state and no shared audio-device state.
   - Migration copies the old state to shared settings.
   - Editor restores from shared settings after migration.

3. Shared audio-device state exists.
   - Editor ignores old editor audio-device state for restore.
   - Game restores the same shared audio-device state.

4. Editor changes audio device successfully.
   - Shared `audioDeviceState` updates.
   - Game sees the new device state on next startup.

5. Game changes audio device successfully.
   - Shared `audioDeviceState` updates.
   - Editor sees the new device state on next startup.

6. Editor or game audio-device apply fails.
   - Shared `audioDeviceState` is not overwritten with the failed staged route.

7. Editor calibrates input successfully.
   - The route's record in the shared `inputCalibrationStates` history updates.
   - Game can use the same route's calibration on next startup.

8. Game calibrates input successfully.
   - The route's record in the shared `inputCalibrationStates` history updates.
   - Editor can use the same route's calibration on next startup.

9. A calibration record is malformed or non-finite in the shared file.
   - Malformed history is reported as a typed error and preserved, not overwritten.
   - A single non-finite/invalid record is dropped; the workflow treats that route as uncalibrated.

10. A calibration gain is finite but outside the accepted range.
    - Reader clamps through `common::audio::clampGain()`.

11. Editor removes a route's calibration (e.g. the committed device route changed).
    - That route's record is removed from the shared history; other routes are untouched.
    - Game also requires calibration next time it uses that route.

12. Game removes a route's calibration (e.g. the committed device route changed).
    - That route's record is removed from the shared history; other routes are untouched.
    - Editor also requires calibration next time it uses that route.

13. Editor opens and closes an audio settings window with the exact previous state restored.
    - Shared calibration remains unchanged.
    - Shared audio-device state remains unchanged.

14. Both apps run at the same time and both write settings.
    - The interprocess lock prevents simultaneous file writes.
    - The later successful write wins.

15. Editor project restore state changes.
    - Shared user-audio settings are untouched.

16. Future game-only settings change.
    - Shared user-audio settings are untouched unless the change is audio device or calibration.

## Testing Strategy

### `rock-hero-common/audio/tests/test_user_audio_settings.cpp`

- starts empty with a test-local settings file;
- persists audio-device XML across reload;
- clears audio-device XML;
- persists per-route calibration history across reload;
- replaces a route's calibration and collapses duplicate-route records;
- removes one route's calibration without touching other routes;
- clamps persisted calibration gain;
- drops malformed/non-finite records and preserves a malformed history as a typed error;
- keeps audio-device state and calibration independent.

(These mirror the calibration cases already covered by `test_editor_settings.cpp`, since the schema
and validation move over unchanged.)

### `rock-hero-editor/core/tests/test_editor_settings.cpp`

- editor settings still persist last-open project;
- editor settings still persist interrupted restore project;
- editor settings no longer need new audio-device assertions after migration coverage exists.

### `rock-hero-editor/core/tests/test_editor_audio_settings_migration.cpp`

- copies old editor audio-device state when shared settings are missing;
- does not overwrite existing shared audio-device state;
- copies old editor calibration only if such a key exists by implementation time;
- tolerates missing old settings.

### `rock-hero-editor/core/tests/test_editor_controller.cpp`

- controller restores audio device from `IUserAudioSettings`;
- controller persists audio device to `IUserAudioSettings`;
- controller reads input calibration from `IUserAudioSettings`;
- controller writes input calibration to `IUserAudioSettings`;
- editor-only restore prompts still use `EditorSettings`.

### Future Game Tests

When game audio startup exists:

- game restores audio-device state from `IUserAudioSettings`;
- game applies shared input calibration;
- game writes calibration on successful game-side calibration.

## Acceptance Criteria

- Editor and game use one shared user-audio settings file for audio-device state and input
  calibration.
- Editor-only project restore settings remain in `EditorSettings`.
- Game code does not depend on `rock-hero-editor`.
- Shared settings live under `rock-hero-common/audio`.
- Existing editor audio-device state migrates to the shared store without overwriting an existing
  shared value.
- Input calibration is still not written to project packages or tone documents.
- Output gain remains outside this shared settings store.

## Non-Goals

- Do not share all settings between the editor and game.
- Do not add instrument profiles.
- Do not add cloud sync or cross-user settings.
- Do not solve rich cross-process conflict resolution beyond interprocess file locking.
- Do not move project restore state out of `EditorSettings`.
- Do not move output gain into app settings.
