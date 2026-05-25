# Shared User Audio Settings Plan

Status: planned. Implement after the required input calibration workflow has a stable first pass,
or fold into that work if the game starts using the same audio-device setup soon.

## Goal

Share per-user audio hardware state between Rock Hero Editor and Rock Hero without sharing
editor-only application state.

The shared settings should contain only audio state that should naturally follow the same user
between both apps:

- selected/restored audio-device manager state;
- required input calibration gain.

Editor-only settings remain editor-only:

- last open editor project;
- interrupted startup restore marker;
- future editor window/workflow preferences.

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

    [[nodiscard]] virtual std::optional<Gain> inputCalibrationGain() const = 0;
    virtual void setInputCalibrationGain(std::optional<Gain> gain) = 0;
};
```

Use `Gain` instead of raw `double` for the public calibration value so shared gain clamping stays
near the audio boundary.

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

    [[nodiscard]] std::optional<Gain> inputCalibrationGain() const override;
    void setInputCalibrationGain(std::optional<Gain> gain) override;

private:
    juce::InterProcessLock m_process_lock;
    juce::PropertiesFile m_properties;
};
```

The explicit-path constructor keeps tests isolated from real user settings.

### Keys And Constants

In `user_audio_settings.cpp`, add:

- `constexpr const char* g_audio_device_state_key{"audioDeviceState"}`
- `constexpr const char* g_input_calibration_gain_db_key{"inputCalibrationGainDb"}`
- `constexpr const char* g_user_audio_settings_application_name{"Rock Hero User Audio"}`
- `constexpr const char* g_user_audio_settings_folder_name{"Rock Hero"}`
- `constexpr const char* g_user_audio_settings_process_lock_name{
  "RockHeroUserAudioSettings"}`

Read behavior:

- missing `audioDeviceState` returns `std::nullopt`;
- empty `audioDeviceState` returns `std::nullopt`;
- missing `inputCalibrationGainDb` returns `std::nullopt`;
- invalid or non-finite calibration gain returns `std::nullopt`;
- finite calibration gain returns `common::audio::clampGain(Gain{value})`.

Write behavior:

- empty `audioDeviceState` removes the key;
- empty calibration gain removes the key;
- present calibration gain is clamped before writing;
- writes call `saveIfNeeded()` immediately, matching current editor settings behavior.

## Editor Integration

Keep `EditorSettings` for editor-only state:

- `lastOpenProject()`;
- `setLastOpenProject(...)`;
- `interruptedRestoreProject()`;
- `setInterruptedRestoreProject(...)`.

Move audio state out of `EditorSettings`:

- remove or deprecate `audioDeviceState()`;
- remove or deprecate `setAudioDeviceState(...)`;
- do not add input calibration methods to `EditorSettings`.

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
- use `inputCalibrationGain()` to apply the same calibrated input gain;
- write back only on successful audio-device apply and calibration success.

Do not make the game depend on `rock-hero-editor/core` to access shared settings.

## Migration

The first implementation should migrate existing editor audio settings once:

1. Open `EditorSettings`.
2. Open `UserAudioSettings`.
3. If shared `audioDeviceState()` is missing and editor `audioDeviceState()` exists, copy the
   editor value into shared settings.
4. Leave the old editor key in place for one release, or clear it immediately only if the
   migration is covered by tests and there is no rollback concern.

Input calibration does not need migration unless a first-pass calibration value already shipped in
`EditorSettings`. If it did, use the same "copy only when shared is missing" policy.

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
   - Shared `inputCalibrationGainDb` updates.
   - Game can use the same input calibration on next startup.

8. Game calibrates input successfully.
   - Shared `inputCalibrationGainDb` updates.
   - Editor can use the same input calibration on next startup.

9. Calibration value is invalid or non-finite in the shared file.
   - Reader treats it as missing.
   - Required calibration workflow handles the missing value.

10. Calibration value is finite but outside the accepted range.
    - Reader clamps through `common::audio::clampGain()`.

11. Editor clears calibration because the committed audio-device state changed.
    - Shared calibration value is cleared.
    - Game also requires calibration next time it uses that shared state.

12. Game clears calibration because the committed audio-device state changed.
    - Shared calibration value is cleared.
    - Editor also requires calibration next time it uses that shared state.

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
- persists calibration gain across reload;
- clamps persisted calibration gain;
- treats missing, empty, invalid, and non-finite calibration values as missing;
- keeps audio-device state and calibration independent.

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
- Do not add per-device calibration records.
- Do not add instrument profiles.
- Do not add cloud sync or cross-user settings.
- Do not solve rich cross-process conflict resolution beyond interprocess file locking.
- Do not move project restore state out of `EditorSettings`.
- Do not move output gain into app settings.
