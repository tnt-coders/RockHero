#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <filesystem>
#include <juce_data_structures/juce_data_structures.h>
#include <memory>
#include <optional>
#include <rock_hero/common/core/shared/application_identity.h>
#include <rock_hero/common/core/shared/juce_path.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/editor/core/audio/editor_audio_config_store.h>
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

constexpr const char* g_project_cursor_positions_key{"projectCursorPositions"};
constexpr const char* g_project_grid_note_values_key{"projectGridNoteValues"};
constexpr const char* g_retired_grid_spacings_key{"projectGridSpacings"};

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
    // Removes the settings file and the sibling audio-config file the owned store opens, on a
    // best-effort basis, so calibration state cannot leak between runs.
    void removeFile() const
    {
        std::error_code error;
        std::filesystem::remove(m_path, error);
        std::filesystem::remove(EditorSettings::audioConfigFileFor(m_path), error);
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

// Finds the first stored settings key beginning with the given flat-key family prefix, so tests
// can corrupt a per-project value without recomputing the production path normalization.
[[nodiscard]] juce::String projectSettingKeyWithPrefix(
    const std::filesystem::path& settings_file, const juce::String& prefix)
{
    juce::PropertiesFile properties{
        common::core::juceFileFromPath(settings_file), testSettingsOptions()
    };
    const juce::StringArray keys = properties.getAllProperties().getAllKeys();
    for (const juce::String& key : keys)
    {
        if (key.startsWith(prefix))
        {
            return key;
        }
    }
    return {};
}

} // namespace

// New settings files do not invent a restore target until the app exits with a project open.
TEST_CASE("EditorSettings starts without a last open project", "[core][settings]")
{
    const ScopedSettingsFile settings_file{"starts_empty.settings"};
    const EditorSettings settings{settings_file.path()};

    CHECK_FALSE(settings.lastOpenProject().has_value());
    CHECK_FALSE(settings.interruptedRestoreProject().has_value());
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
        reloaded_settings.projectCursorPositionFor(project_file) ==
        std::optional{common::core::TimePosition{4.25}});
    CHECK_FALSE(reloaded_settings.projectCursorPositionFor(other_project_file).has_value());
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
        settings.projectCursorPositionFor(project_file) ==
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
        reloaded_settings.projectGridNoteValueFor(project_file) ==
        std::optional{common::core::Fraction{3, 4}});
    CHECK_FALSE(reloaded_settings.projectGridNoteValueFor(other_project_file).has_value());
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

    CHECK(reloaded_settings.projectTimelineZoomFor(project_file) == std::optional{252.8});
    CHECK_FALSE(reloaded_settings.projectTimelineZoomFor(other_project_file).has_value());
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

// The use-game-audio-settings toggle is editor workflow state: absent until first written, then the
// stored choice is authoritative. An absent value resolves to the first-run default (on); a written
// value wins on reload.
TEST_CASE(
    "EditorSettings resolves and persists the use-game-audio-settings toggle", "[core][settings]")
{
    const ScopedSettingsFile settings_file{"persists_use_game_audio_settings.settings"};

    {
        EditorSettings settings{settings_file.path()};
        CHECK_FALSE(settings.useGameAudioSettings().has_value());
        CHECK(useGameAudioSettingsOrDefault(settings));
        REQUIRE(settings.setUseGameAudioSettings(false).has_value());
        CHECK(settings.useGameAudioSettings() == std::optional{false});
        CHECK_FALSE(useGameAudioSettingsOrDefault(settings));
    }

    const EditorSettings reloaded_settings{settings_file.path()};
    CHECK(reloaded_settings.useGameAudioSettings() == std::optional{false});
    CHECK_FALSE(useGameAudioSettingsOrDefault(reloaded_settings));
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
        settings.projectGridNoteValueFor(project_file) ==
        std::optional{common::core::Fraction{1, 4}});
}

// Old list-format records written before per-project settings flattened to flat keys are orphaned,
// not migrated: the flat getters never read them, so affected projects simply reset once.
TEST_CASE("EditorSettings ignores legacy list-format project records", "[core][settings]")
{
    const ScopedSettingsFile settings_file{"legacy_list_project_records.settings"};
    const std::filesystem::path project_file =
        std::filesystem::path{TEST_SETTINGS_DIR} / "legacy_list.rhp";
    writeRawSetting(
        settings_file.path(),
        g_project_cursor_positions_key,
        juce::String{"<PROJECT_CURSOR_POSITIONS formatVersion=\"1\"/>"});
    writeRawSetting(
        settings_file.path(),
        g_project_grid_note_values_key,
        juce::String{"<PROJECT_GRID_NOTE_VALUES formatVersion=\"1\"/>"});
    writeRawSetting(
        settings_file.path(),
        g_retired_grid_spacings_key,
        juce::String{"<PROJECT_GRID_SPACINGS formatVersion=\"1\"/>"});

    const EditorSettings settings{settings_file.path()};
    CHECK_FALSE(settings.projectCursorPositionFor(project_file).has_value());
    CHECK_FALSE(settings.projectGridNoteValueFor(project_file).has_value());
}

// A corrupt flat value reads as absent rather than a bogus number; the flat key isolates it so no
// other project's independently-keyed value is disturbed.
TEST_CASE("EditorSettings reads a corrupt project value as absent", "[core][settings]")
{
    const ScopedSettingsFile settings_file{"corrupt_project_value.settings"};
    const std::filesystem::path project_file =
        std::filesystem::path{TEST_SETTINGS_DIR} / "corrupt_cursor.rhp";

    {
        EditorSettings settings{settings_file.path()};
        REQUIRE(settings.saveProjectCursorPosition(project_file, common::core::TimePosition{2.0})
                    .has_value());
    }

    const juce::String key =
        projectSettingKeyWithPrefix(settings_file.path(), juce::String{"projectCursor:"});
    REQUIRE(key.isNotEmpty());
    writeRawSetting(settings_file.path(), key.toRawUTF8(), juce::String{"not-a-number"});

    const EditorSettings reloaded_settings{settings_file.path()};
    CHECK_FALSE(reloaded_settings.projectCursorPositionFor(project_file).has_value());
}

// Chooser, restore, and test spellings of the same project file normalize to one stored record.
TEST_CASE("EditorSettings normalizes project paths to one record", "[core][settings]")
{
    const ScopedSettingsFile settings_file{"normalizes_project_path.settings"};
    const std::filesystem::path canonical_path =
        std::filesystem::path{TEST_SETTINGS_DIR} / "Normalized Project.rhp";
    const std::filesystem::path spelled_path =
        std::filesystem::path{TEST_SETTINGS_DIR} / "." / "Normalized Project.rhp";

    EditorSettings settings{settings_file.path()};
    REQUIRE(settings.saveProjectSelectedArrangement(canonical_path, "lead-uuid").has_value());

    CHECK(
        settings.projectSelectedArrangementFor(spelled_path) ==
        std::optional<std::string>{"lead-uuid"});
}

// Paths with spaces and non-ASCII characters survive the flat-key round-trip: JUCE escapes them in
// the settings XML, so unicode project locations restore correctly.
TEST_CASE("EditorSettings round-trips a unicode project path", "[core][settings]")
{
    const ScopedSettingsFile settings_file{"unicode_project_path.settings"};
    const std::filesystem::path project_file =
        std::filesystem::path{TEST_SETTINGS_DIR} /
        std::filesystem::path{std::u8string{u8"Prôjéct — spaces.rhp"}};

    {
        EditorSettings settings{settings_file.path()};
        REQUIRE(settings.saveProjectSelectedArrangement(project_file, "arr-unicode").has_value());
    }

    const EditorSettings reloaded_settings{settings_file.path()};
    CHECK(
        reloaded_settings.projectSelectedArrangementFor(project_file) ==
        std::optional<std::string>{"arr-unicode"});
}

} // namespace rock_hero::editor::core
