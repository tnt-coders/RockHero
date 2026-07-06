#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <juce_data_structures/juce_data_structures.h>
#include <memory>
#include <optional>
#include <rock_hero/common/core/shared/application_identity.h>
#include <rock_hero/common/core/shared/juce_path.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/editor/core/settings/editor_settings.h>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#ifndef TEST_SETTINGS_DIR
#define TEST_SETTINGS_DIR "."
#endif

namespace rock_hero::editor::core
{

namespace
{

constexpr const char* g_input_calibration_gain_db_key{"inputCalibrationGainDb"};
constexpr const char* g_input_calibration_backend_name_key{"inputCalibrationBackendName"};
constexpr const char* g_input_calibration_input_device_name_key{"inputCalibrationInputDeviceName"};
constexpr const char* g_input_calibration_input_channel_index_key{
    "inputCalibrationInputChannelIndex"
};
constexpr const char* g_input_calibration_input_channel_name_key{
    "inputCalibrationInputChannelName"
};
constexpr const char* g_project_cursor_positions_key{"projectCursorPositions"};
constexpr const char* g_project_grid_note_values_key{"projectGridNoteValues"};
constexpr const char* g_retired_grid_spacings_key{"projectGridSpacings"};
constexpr const char* g_project_cursor_positions_tag{"PROJECT_CURSOR_POSITIONS"};
constexpr const char* g_input_calibration_states_key{"inputCalibrationStates"};
constexpr const char* g_input_calibrations_tag{"INPUT_CALIBRATIONS"};

// Owns one build-local settings file so each test starts with clean persisted state.
class ScopedSettingsFile final
{
public:
    // Creates a settings-file path and removes any stale file from a prior test run.
    explicit ScopedSettingsFile(std::string_view file_name)
        : m_path(std::filesystem::path{TEST_SETTINGS_DIR} / file_name)
    {
        removeFile();
    }

    // Removes the settings file so persistence tests cannot leak state into later tests.
    ~ScopedSettingsFile()
    {
        removeFile();
    }

    ScopedSettingsFile(const ScopedSettingsFile&) = delete;
    ScopedSettingsFile& operator=(const ScopedSettingsFile&) = delete;
    ScopedSettingsFile(ScopedSettingsFile&&) = delete;
    ScopedSettingsFile& operator=(ScopedSettingsFile&&) = delete;

    // Returns the test-owned settings-file path.
    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return m_path;
    }

private:
    // Removes the settings file on a best-effort basis.
    void removeFile() const
    {
        std::error_code error;
        std::filesystem::remove(m_path, error);
    }

    // Build-local settings path owned by this fixture.
    std::filesystem::path m_path;
};

// Builds explicit settings-file options so tests can seed obsolete/raw properties.
[[nodiscard]] juce::PropertiesFile::Options testSettingsOptions()
{
    juce::PropertiesFile::Options options;
    const std::string_view application_name = common::core::editorApplicationName();
    const std::string_view folder_name = common::core::applicationDataFolderName();
    options.applicationName = juce::String{application_name.data(), application_name.size()};
    options.filenameSuffix = ".settings";
    options.folderName = juce::String{folder_name.data(), folder_name.size()};
    options.osxLibrarySubFolder = "Application Support";
    options.commonToAllUsers = false;
    options.ignoreCaseOfKeyNames = false;
    options.doNotSave = false;
    options.millisecondsBeforeSaving = 0;
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    options.processLock = nullptr;
    return options;
}

// Writes one raw property through JUCE so malformed settings use production storage.
void writeRawSetting(
    const std::filesystem::path& settings_file, const char* key, const juce::var& value)
{
    juce::PropertiesFile properties{
        common::core::juceFileFromPath(settings_file), testSettingsOptions()
    };
    properties.setValue(key, value);
    REQUIRE(properties.save());
}

// Reads whether a raw property remains present after settings operations.
[[nodiscard]] bool rawSettingExists(const std::filesystem::path& settings_file, const char* key)
{
    const juce::PropertiesFile properties{
        common::core::juceFileFromPath(settings_file), testSettingsOptions()
    };
    return properties.containsKey(key);
}

// Reads one XML-valued setting from the same JUCE storage path production uses.
[[nodiscard]] std::unique_ptr<juce::XmlElement> xmlSettingFor(
    const std::filesystem::path& settings_file, const char* key)
{
    const juce::PropertiesFile properties{
        common::core::juceFileFromPath(settings_file), testSettingsOptions()
    };
    return properties.getXmlValue(key);
}

// Builds a stable route identity for settings history tests.
[[nodiscard]] common::audio::InputDeviceIdentity makeIdentity(
    std::string input_device_name = "Interface A", int channel_index = 0,
    std::string backend_name = "ASIO", std::string channel_name = {})
{
    if (channel_name.empty())
    {
        channel_name = "Input " + std::to_string(channel_index + 1);
    }

    return common::audio::InputDeviceIdentity{
        .backend_name = std::move(backend_name),
        .input_device_name = std::move(input_device_name),
        .input_channel_index = channel_index,
        .input_channel_name = std::move(channel_name),
    };
}

// Builds a calibration record for one physical route.
[[nodiscard]] common::audio::InputCalibrationState calibrationFor(
    const common::audio::InputDeviceIdentity& identity, double gain_db)
{
    return common::audio::InputCalibrationState{
        .calibration_gain = common::audio::Gain{gain_db},
        .input_device_identity = identity,
    };
}

// Reads calibration through the typed settings contract and returns the optional payload.
[[nodiscard]] std::optional<common::audio::InputCalibrationState> inputCalibrationFor(
    const EditorSettings& settings, const common::audio::InputDeviceIdentity& identity)
{
    auto result = settings.inputCalibrationFor(identity);
    REQUIRE(result.has_value());
    return std::move(*result);
}

// Reads project cursor state through the typed settings contract and returns the optional payload.
[[nodiscard]] std::optional<common::core::TimePosition> projectCursorPositionFor(
    const EditorSettings& settings, const std::filesystem::path& project_file)
{
    auto result = settings.projectCursorPositionFor(project_file);
    REQUIRE(result.has_value());
    return *result;
}

// Reads project grid spacing through the typed settings contract and returns the optional payload.
[[nodiscard]] std::optional<common::core::Fraction> projectGridNoteValueFor(
    const EditorSettings& settings, const std::filesystem::path& project_file)
{
    auto result = settings.projectGridNoteValueFor(project_file);
    REQUIRE(result.has_value());
    return *result;
}

// Seeds obsolete flat calibration keys so no-migration behavior can be verified.
void writeObsoleteCalibration(
    const std::filesystem::path& settings_file,
    const common::audio::InputCalibrationState& calibration)
{
    juce::PropertiesFile properties{
        common::core::juceFileFromPath(settings_file), testSettingsOptions()
    };
    properties.setValue(g_input_calibration_gain_db_key, calibration.calibration_gain.db);
    properties.setValue(
        g_input_calibration_backend_name_key,
        juce::String::fromUTF8(calibration.input_device_identity.backend_name.c_str()));
    properties.setValue(
        g_input_calibration_input_device_name_key,
        juce::String::fromUTF8(calibration.input_device_identity.input_device_name.c_str()));
    properties.setValue(
        g_input_calibration_input_channel_index_key,
        calibration.input_device_identity.input_channel_index);
    properties.setValue(
        g_input_calibration_input_channel_name_key,
        juce::String::fromUTF8(calibration.input_device_identity.input_channel_name.c_str()));
    REQUIRE(properties.save());
}

} // namespace

// New settings files do not invent a restore target until the app exits with a project open.
TEST_CASE("EditorSettings starts without a last open project", "[core][settings]")
{
    const ScopedSettingsFile settings_file{"starts_empty.settings"};
    const EditorSettings settings{settings_file.path()};

    CHECK_FALSE(settings.lastOpenProject().has_value());
    CHECK_FALSE(settings.interruptedRestoreProject().has_value());
    CHECK_FALSE(settings.audioDeviceState().has_value());
    CHECK_FALSE(inputCalibrationFor(settings, makeIdentity()).has_value());
}

// The settings file preserves the editor project path that should be restored on next launch.
TEST_CASE("EditorSettings persists the last open project", "[core][settings]")
{
    const ScopedSettingsFile settings_file{"persists_project.settings"};
    const std::filesystem::path project_file =
        std::filesystem::path{TEST_SETTINGS_DIR} / "Project With Spaces.rhp";

    {
        EditorSettings settings{settings_file.path()};
        REQUIRE(settings.setLastOpenProject(project_file).has_value());
    }

    const EditorSettings reloaded_settings{settings_file.path()};

    CHECK(reloaded_settings.lastOpenProject() == std::optional{project_file});
}

// Clearing restore state removes the persisted project path from the settings file.
TEST_CASE("EditorSettings clears the last open project", "[core][settings]")
{
    const ScopedSettingsFile settings_file{"clears_project.settings"};
    const std::filesystem::path project_file =
        std::filesystem::path{TEST_SETTINGS_DIR} / "cleared_project.rhp";

    {
        EditorSettings settings{settings_file.path()};
        REQUIRE(settings.setLastOpenProject(project_file).has_value());
        REQUIRE(settings.setLastOpenProject(std::nullopt).has_value());
    }

    const EditorSettings reloaded_settings{settings_file.path()};

    CHECK_FALSE(reloaded_settings.lastOpenProject().has_value());
}

// The settings file preserves the startup-restore interruption marker across launches.
TEST_CASE("EditorSettings persists interrupted restore project", "[core][settings]")
{
    const ScopedSettingsFile settings_file{"persists_interrupted_restore.settings"};
    const std::filesystem::path project_file =
        std::filesystem::path{TEST_SETTINGS_DIR} / "Interrupted Restore.rhp";

    {
        EditorSettings settings{settings_file.path()};
        REQUIRE(settings.setInterruptedRestoreProject(project_file).has_value());
    }

    const EditorSettings reloaded_settings{settings_file.path()};

    CHECK(reloaded_settings.interruptedRestoreProject() == std::optional{project_file});
}

// Clearing the startup-restore interruption marker leaves no stale recovery prompt.
TEST_CASE("EditorSettings clears interrupted restore project", "[core][settings]")
{
    const ScopedSettingsFile settings_file{"clears_interrupted_restore.settings"};
    const std::filesystem::path project_file =
        std::filesystem::path{TEST_SETTINGS_DIR} / "interrupted_restore.rhp";

    {
        EditorSettings settings{settings_file.path()};
        REQUIRE(settings.setInterruptedRestoreProject(project_file).has_value());
        REQUIRE(settings.setInterruptedRestoreProject(std::nullopt).has_value());
    }

    const EditorSettings reloaded_settings{settings_file.path()};

    CHECK_FALSE(reloaded_settings.interruptedRestoreProject().has_value());
}

// The settings file preserves opaque serialized audio-device state across launches.
TEST_CASE("EditorSettings persists the audio device state", "[core][settings]")
{
    const ScopedSettingsFile settings_file{"persists_audio_device.settings"};
    const std::string serialized_state{
        R"(<DEVICESETUP deviceType="ASIO" audioOutputDeviceName="ASIO Interface"/>)"
    };

    {
        EditorSettings settings{settings_file.path()};
        REQUIRE(settings.setAudioDeviceState(serialized_state).has_value());
    }

    const EditorSettings reloaded_settings{settings_file.path()};

    CHECK(reloaded_settings.audioDeviceState() == std::optional{serialized_state});
}

// Clearing audio state removes the persisted serialized state from the settings file.
TEST_CASE("EditorSettings clears the audio device state", "[core][settings]")
{
    const ScopedSettingsFile settings_file{"clears_audio_device.settings"};
    const std::string serialized_state{"<DEVICESETUP deviceType=\"ASIO\"/>"};

    {
        EditorSettings settings{settings_file.path()};
        REQUIRE(settings.setAudioDeviceState(serialized_state).has_value());
        REQUIRE(settings.setAudioDeviceState(std::nullopt).has_value());
    }

    const EditorSettings reloaded_settings{settings_file.path()};

    CHECK_FALSE(reloaded_settings.audioDeviceState().has_value());
}

// Project cursor history persists app-local resume state without storing it in project packages.
TEST_CASE("EditorSettings persists project cursor position", "[core][settings]")
{
    const ScopedSettingsFile settings_file{"persists_project_cursor.settings"};
    const std::filesystem::path project_file =
        std::filesystem::path{TEST_SETTINGS_DIR} / "Cursor Project.rhp";
    const std::filesystem::path other_project_file =
        std::filesystem::path{TEST_SETTINGS_DIR} / "Other Cursor Project.rhp";

    {
        EditorSettings settings{settings_file.path()};
        REQUIRE(settings.saveProjectCursorPosition(project_file, common::core::TimePosition{4.25})
                    .has_value());
    }

    const EditorSettings reloaded_settings{settings_file.path()};

    CHECK(
        projectCursorPositionFor(reloaded_settings, project_file) ==
        std::optional{common::core::TimePosition{4.25}});
    CHECK_FALSE(projectCursorPositionFor(reloaded_settings, other_project_file).has_value());

    const std::unique_ptr<juce::XmlElement> cursor_xml =
        xmlSettingFor(settings_file.path(), g_project_cursor_positions_key);
    REQUIRE(cursor_xml != nullptr);
    CHECK(cursor_xml->hasTagName(g_project_cursor_positions_tag));
    CHECK(cursor_xml->getNumChildElements() == 1);
}

// Saving the same project cursor again replaces only that project's resume position.
TEST_CASE("EditorSettings overwrites project cursor position", "[core][settings]")
{
    const ScopedSettingsFile settings_file{"overwrites_project_cursor.settings"};
    const std::filesystem::path project_file =
        std::filesystem::path{TEST_SETTINGS_DIR} / "overwritten_cursor.rhp";
    EditorSettings settings{settings_file.path()};

    REQUIRE(settings.saveProjectCursorPosition(project_file, common::core::TimePosition{1.0})
                .has_value());
    REQUIRE(settings.saveProjectCursorPosition(project_file, common::core::TimePosition{7.5})
                .has_value());

    CHECK(
        projectCursorPositionFor(settings, project_file) ==
        std::optional{common::core::TimePosition{7.5}});
}

// The grid note value persists per project path and reloads with exact numerator and denominator.
TEST_CASE("EditorSettings persists the project grid note value", "[core][settings]")
{
    const ScopedSettingsFile settings_file{"persists_project_grid_spacing.settings"};
    const std::filesystem::path project_file =
        std::filesystem::path{TEST_SETTINGS_DIR} / "Grid Project.rhp";
    const std::filesystem::path other_project_file =
        std::filesystem::path{TEST_SETTINGS_DIR} / "Other Grid Project.rhp";

    {
        EditorSettings settings{settings_file.path()};
        REQUIRE(settings.saveProjectGridNoteValue(project_file, common::core::Fraction{3, 4})
                    .has_value());
    }

    const EditorSettings reloaded_settings{settings_file.path()};

    CHECK(
        projectGridNoteValueFor(reloaded_settings, project_file) ==
        std::optional{common::core::Fraction{3, 4}});
    CHECK_FALSE(projectGridNoteValueFor(reloaded_settings, other_project_file).has_value());
}

// Saving again for the same project replaces the record instead of accumulating duplicates.
// Zoom mirrors cursor/grid persistence: app-local per-project resume state, keyed by path.
TEST_CASE("EditorSettings persists the project timeline zoom", "[core][settings]")
{
    const ScopedSettingsFile settings_file{"persists_project_timeline_zoom.settings"};
    const std::filesystem::path project_file =
        std::filesystem::path{TEST_SETTINGS_DIR} / "Zoom Project.rhp";
    const std::filesystem::path other_project_file =
        std::filesystem::path{TEST_SETTINGS_DIR} / "Other Zoom Project.rhp";

    {
        EditorSettings settings{settings_file.path()};
        REQUIRE(settings.saveProjectTimelineZoom(project_file, 252.8).has_value());
        CHECK_FALSE(settings.saveProjectTimelineZoom(project_file, 0.0).has_value());
    }

    const EditorSettings reloaded_settings{settings_file.path()};

    const auto restored = reloaded_settings.projectTimelineZoomFor(project_file);
    REQUIRE(restored.has_value());
    CHECK(*restored == std::optional{252.8});
    const auto missing = reloaded_settings.projectTimelineZoomFor(other_project_file);
    REQUIRE(missing.has_value());
    CHECK_FALSE(missing->has_value());
}

// Waveform visibility and the tablature lane minimum are app-wide single values (no project
// key): absent until first written, then replaced on each save.
TEST_CASE("EditorSettings persists tab display preferences", "[core][settings]")
{
    const ScopedSettingsFile settings_file{"persists_tab_display_preferences.settings"};

    {
        EditorSettings settings{settings_file.path()};
        CHECK_FALSE(settings.waveformVisible().has_value());
        CHECK_FALSE(settings.tabMinimumDisplayedStrings().has_value());
        REQUIRE(settings.setWaveformVisible(false).has_value());
        REQUIRE(settings.setTabMinimumDisplayedStrings(10).has_value());
        REQUIRE(settings.setTabMinimumDisplayedStrings(8).has_value());
        CHECK_FALSE(settings.setTabMinimumDisplayedStrings(-1).has_value());
    }

    const EditorSettings reloaded_settings{settings_file.path()};
    CHECK(reloaded_settings.waveformVisible() == std::optional{false});
    CHECK(reloaded_settings.tabMinimumDisplayedStrings() == std::optional{8});
}

TEST_CASE("EditorSettings overwrites the project grid note value", "[core][settings]")
{
    const ScopedSettingsFile settings_file{"overwrites_project_grid_spacing.settings"};
    const std::filesystem::path project_file =
        std::filesystem::path{TEST_SETTINGS_DIR} / "overwritten_grid.rhp";
    EditorSettings settings{settings_file.path()};

    REQUIRE(
        settings.saveProjectGridNoteValue(project_file, common::core::Fraction{1, 2}).has_value());
    REQUIRE(
        settings.saveProjectGridNoteValue(project_file, common::core::Fraction{1, 4}).has_value());

    CHECK(
        projectGridNoteValueFor(settings, project_file) ==
        std::optional{common::core::Fraction{1, 4}});
}

// Malformed grid note-value XML is reported and preserved instead of overwritten by a save
// attempt.
TEST_CASE("EditorSettings preserves malformed grid note-value history", "[core][settings]")
{
    const ScopedSettingsFile settings_file{"malformed_project_grid_spacing.settings"};
    const std::filesystem::path project_file =
        std::filesystem::path{TEST_SETTINGS_DIR} / "malformed_grid.rhp";
    writeRawSetting(settings_file.path(), g_project_grid_note_values_key, juce::String{"not-xml"});

    EditorSettings settings{settings_file.path()};
    const auto loaded = settings.projectGridNoteValueFor(project_file);
    CHECK_FALSE(loaded.has_value());
    CHECK(loaded.error().code == EditorSettingsErrorCode::InvalidProjectGridNoteValueHistory);

    const auto saved =
        settings.saveProjectGridNoteValue(project_file, common::core::Fraction{1, 2});
    CHECK_FALSE(saved.has_value());
    CHECK(saved.error().code == EditorSettingsErrorCode::InvalidProjectGridNoteValueHistory);
    CHECK(rawSettingExists(settings_file.path(), g_project_grid_note_values_key));
}

// Records written by the retired beat-unit grid family live under a different property key, so
// they are ignored rather than reinterpreted in the wrong unit; affected projects simply have no
// stored note value.
TEST_CASE("EditorSettings ignores retired beat-unit grid records", "[core][settings]")
{
    const ScopedSettingsFile settings_file{"retired_grid_spacing_records.settings"};
    const std::filesystem::path project_file =
        std::filesystem::path{TEST_SETTINGS_DIR} / "retired_grid.rhp";
    writeRawSetting(
        settings_file.path(),
        g_retired_grid_spacings_key,
        juce::String{"<PROJECT_GRID_SPACINGS formatVersion=\"1\"/>"});

    const EditorSettings settings{settings_file.path()};
    const auto loaded = settings.projectGridNoteValueFor(project_file);
    REQUIRE(loaded.has_value());
    CHECK_FALSE(loaded->has_value());
}

// Malformed cursor XML is reported and preserved instead of overwritten by a save attempt.
TEST_CASE("EditorSettings preserves malformed cursor history", "[core][settings]")
{
    const ScopedSettingsFile settings_file{"malformed_project_cursor.settings"};
    const std::filesystem::path project_file =
        std::filesystem::path{TEST_SETTINGS_DIR} / "malformed_cursor.rhp";
    writeRawSetting(settings_file.path(), g_project_cursor_positions_key, juce::String{"not-xml"});

    EditorSettings settings{settings_file.path()};
    const auto loaded = settings.projectCursorPositionFor(project_file);
    CHECK_FALSE(loaded.has_value());
    CHECK(loaded.error().code == EditorSettingsErrorCode::InvalidProjectCursorHistory);

    const auto saved =
        settings.saveProjectCursorPosition(project_file, common::core::TimePosition{2.0});
    CHECK_FALSE(saved.has_value());
    CHECK(saved.error().code == EditorSettingsErrorCode::InvalidProjectCursorHistory);
    CHECK(rawSettingExists(settings_file.path(), g_project_cursor_positions_key));
}

// Calibration history persists one physical input route and ignores unrelated routes.
TEST_CASE("EditorSettings persists physical input calibration", "[core][settings]")
{
    const ScopedSettingsFile settings_file{"persists_input_calibration.settings"};
    const common::audio::InputDeviceIdentity identity = makeIdentity();
    const common::audio::InputDeviceIdentity other_identity = makeIdentity("Interface B");

    {
        EditorSettings settings{settings_file.path()};
        REQUIRE(settings.saveInputCalibration(calibrationFor(identity, 6.5)).has_value());
    }

    const EditorSettings reloaded_settings{settings_file.path()};

    const auto stored_calibration = inputCalibrationFor(reloaded_settings, identity);
    REQUIRE(stored_calibration.has_value());
    if (stored_calibration.has_value())
    {
        CHECK(stored_calibration->calibration_gain.db == 6.5);
        CHECK(stored_calibration->input_device_identity == identity);
    }
    CHECK_FALSE(inputCalibrationFor(reloaded_settings, other_identity).has_value());

    const std::unique_ptr<juce::XmlElement> calibration_xml =
        xmlSettingFor(settings_file.path(), g_input_calibration_states_key);
    REQUIRE(calibration_xml != nullptr);
    CHECK(calibration_xml->hasTagName(g_input_calibrations_tag));
    CHECK(calibration_xml->getNumChildElements() == 1);
}

// Saving the same physical route again replaces only that route's gain.
TEST_CASE("EditorSettings overwrites physical input calibration", "[core][settings]")
{
    const ScopedSettingsFile settings_file{"overwrites_input_calibration.settings"};
    const common::audio::InputDeviceIdentity identity = makeIdentity();
    EditorSettings settings{settings_file.path()};

    REQUIRE(settings.saveInputCalibration(calibrationFor(identity, 2.0)).has_value());
    REQUIRE(settings.saveInputCalibration(calibrationFor(identity, 8.0)).has_value());

    const auto stored_calibration = inputCalibrationFor(settings, identity);
    REQUIRE(stored_calibration.has_value());
    if (stored_calibration.has_value())
    {
        CHECK(stored_calibration->calibration_gain.db == 8.0);
    }
}

// Channel display-name drift must not make the same physical route lose calibration.
TEST_CASE("EditorSettings restores renamed physical channel", "[core][settings]")
{
    const ScopedSettingsFile settings_file{"renamed_channel_input_calibration.settings"};
    const common::audio::InputDeviceIdentity saved_identity =
        makeIdentity("Interface A", 0, "ASIO", "Input 1");
    const common::audio::InputDeviceIdentity current_identity =
        makeIdentity("Interface A", 0, "ASIO", "Mic/Inst 1");
    EditorSettings settings{settings_file.path()};

    REQUIRE(settings.saveInputCalibration(calibrationFor(saved_identity, 4.0)).has_value());

    const auto restored_calibration = inputCalibrationFor(settings, current_identity);
    REQUIRE(restored_calibration.has_value());
    if (restored_calibration.has_value())
    {
        CHECK(restored_calibration->calibration_gain.db == 4.0);
        CHECK(restored_calibration->input_device_identity == current_identity);
    }

    REQUIRE(settings.saveInputCalibration(calibrationFor(current_identity, 7.0)).has_value());
    const auto overwritten_calibration = inputCalibrationFor(settings, saved_identity);
    REQUIRE(overwritten_calibration.has_value());
    if (overwritten_calibration.has_value())
    {
        CHECK(overwritten_calibration->calibration_gain.db == 7.0);
        CHECK(overwritten_calibration->input_device_identity == saved_identity);
    }

    REQUIRE(settings.removeInputCalibration(saved_identity).has_value());
    CHECK_FALSE(inputCalibrationFor(settings, current_identity).has_value());
}

// Different physical input channels on the same device keep independent calibration records.
TEST_CASE("EditorSettings keeps input channels separate", "[core][settings]")
{
    const ScopedSettingsFile settings_file{"input_calibration_channel_identity.settings"};
    const common::audio::InputDeviceIdentity channel_one = makeIdentity("Interface A", 0);
    const common::audio::InputDeviceIdentity channel_three = makeIdentity("Interface A", 2);
    EditorSettings settings{settings_file.path()};

    REQUIRE(settings.saveInputCalibration(calibrationFor(channel_one, 3.0)).has_value());
    REQUIRE(settings.saveInputCalibration(calibrationFor(channel_three, 9.0)).has_value());

    const auto channel_one_calibration = inputCalibrationFor(settings, channel_one);
    const auto channel_three_calibration = inputCalibrationFor(settings, channel_three);
    REQUIRE(channel_one_calibration.has_value());
    REQUIRE(channel_three_calibration.has_value());
    if (channel_one_calibration.has_value() && channel_three_calibration.has_value())
    {
        CHECK(channel_one_calibration->calibration_gain.db == 3.0);
        CHECK(channel_three_calibration->calibration_gain.db == 9.0);
    }
}

// Removing one physical route leaves other saved route calibrations intact.
TEST_CASE("EditorSettings removes one physical calibration", "[core][settings]")
{
    const ScopedSettingsFile settings_file{"removes_input_calibration.settings"};
    const common::audio::InputDeviceIdentity first_identity = makeIdentity("Interface A");
    const common::audio::InputDeviceIdentity second_identity = makeIdentity("Interface B");
    EditorSettings settings{settings_file.path()};
    REQUIRE(settings.saveInputCalibration(calibrationFor(first_identity, 3.0)).has_value());
    REQUIRE(settings.saveInputCalibration(calibrationFor(second_identity, 6.0)).has_value());

    REQUIRE(settings.removeInputCalibration(first_identity).has_value());

    CHECK_FALSE(inputCalibrationFor(settings, first_identity).has_value());
    const auto preserved_calibration = inputCalibrationFor(settings, second_identity);
    REQUIRE(preserved_calibration.has_value());
    if (preserved_calibration.has_value())
    {
        CHECK(preserved_calibration->calibration_gain.db == 6.0);
    }
}

// Duplicate XML records collapse to the last valid record for a physical route.
TEST_CASE("EditorSettings collapses duplicate calibration history", "[core][settings]")
{
    const ScopedSettingsFile settings_file{"duplicates_input_calibration.settings"};
    const common::audio::InputDeviceIdentity identity = makeIdentity();
    writeRawSetting(
        settings_file.path(),
        g_input_calibration_states_key,
        juce::String{
            R"(<INPUT_CALIBRATIONS formatVersion="1">)"
            R"(<CALIBRATION gainDb="2.0" backendName="ASIO" inputDeviceName="Interface A" )"
            R"(inputChannelIndex="0" inputChannelName="Input 1"/>)"
            R"(<CALIBRATION gainDb="7.0" backendName="ASIO" inputDeviceName="Interface A" )"
            R"(inputChannelIndex="0" inputChannelName="Mic/Inst 1"/>)"
            R"(</INPUT_CALIBRATIONS>)"
        });

    const EditorSettings settings{settings_file.path()};

    const auto stored_calibration = inputCalibrationFor(settings, identity);
    REQUIRE(stored_calibration.has_value());
    if (stored_calibration.has_value())
    {
        CHECK(stored_calibration->calibration_gain.db == 7.0);
        CHECK(stored_calibration->input_device_identity == identity);
    }
}

// Malformed calibration XML blocks lookup and removal without overwriting unknown state.
TEST_CASE("EditorSettings preserves malformed calibration history", "[core][settings]")
{
    const ScopedSettingsFile settings_file{"malformed_input_calibration.settings"};
    const common::audio::InputDeviceIdentity identity = makeIdentity();
    writeRawSetting(settings_file.path(), g_input_calibration_states_key, juce::String{"[not-xml"});

    EditorSettings settings{settings_file.path()};

    const auto loaded = settings.inputCalibrationFor(identity);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().code == EditorSettingsErrorCode::InvalidInputCalibrationHistory);
    CHECK_FALSE(loaded.error().message.empty());
    const auto removed = settings.removeInputCalibration(identity);
    REQUIRE_FALSE(removed.has_value());
    CHECK(removed.error().code == EditorSettingsErrorCode::InvalidInputCalibrationHistory);
    CHECK(rawSettingExists(settings_file.path(), g_input_calibration_states_key));
}

// Obsolete flat calibration keys are ignored rather than migrated into the current history.
TEST_CASE("EditorSettings ignores obsolete flat calibration", "[core][settings]")
{
    const ScopedSettingsFile settings_file{"ignores_obsolete_input_calibration.settings"};
    const common::audio::InputDeviceIdentity obsolete_identity = makeIdentity("Interface A");
    const common::audio::InputDeviceIdentity new_identity = makeIdentity("Interface B");
    writeObsoleteCalibration(settings_file.path(), calibrationFor(obsolete_identity, 4.0));

    {
        EditorSettings settings{settings_file.path()};
        CHECK_FALSE(inputCalibrationFor(settings, obsolete_identity).has_value());
        REQUIRE(settings.saveInputCalibration(calibrationFor(new_identity, 7.0)).has_value());
    }

    const EditorSettings reloaded_settings{settings_file.path()};

    const auto new_calibration = inputCalibrationFor(reloaded_settings, new_identity);
    REQUIRE(new_calibration.has_value());
    if (new_calibration.has_value())
    {
        CHECK(new_calibration->calibration_gain.db == 7.0);
    }
    CHECK_FALSE(inputCalibrationFor(reloaded_settings, obsolete_identity).has_value());
}

// Saving refuses to overwrite malformed current-format calibration history.
TEST_CASE("EditorSettings blocks saving over malformed calibration", "[core][settings]")
{
    const ScopedSettingsFile settings_file{"malformed_save_input_calibration.settings"};
    const common::audio::InputDeviceIdentity new_identity = makeIdentity("Interface B");
    writeRawSetting(settings_file.path(), g_input_calibration_states_key, juce::String{"[not-xml"});

    EditorSettings settings{settings_file.path()};
    const auto saved = settings.saveInputCalibration(calibrationFor(new_identity, 7.0));
    REQUIRE_FALSE(saved.has_value());
    CHECK(saved.error().code == EditorSettingsErrorCode::InvalidInputCalibrationHistory);
    CHECK(rawSettingExists(settings_file.path(), g_input_calibration_states_key));

    const EditorSettings reloaded_settings{settings_file.path()};
    const auto loaded = reloaded_settings.inputCalibrationFor(new_identity);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().code == EditorSettingsErrorCode::InvalidInputCalibrationHistory);
}

} // namespace rock_hero::editor::core
