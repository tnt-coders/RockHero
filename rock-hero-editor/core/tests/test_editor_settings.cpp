#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <optional>
#include <rock_hero/editor/core/editor_settings.h>
#include <string>
#include <string_view>
#include <system_error>

#ifndef TEST_SETTINGS_DIR
#define TEST_SETTINGS_DIR "."
#endif

namespace rock_hero::editor::core
{

namespace
{

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

} // namespace

// New settings files do not invent a restore target until the app exits with a project open.
TEST_CASE("EditorSettings starts without a last open project", "[core][settings]")
{
    const ScopedSettingsFile settings_file{"starts_empty.settings"};
    const EditorSettings settings{settings_file.path()};

    CHECK_FALSE(settings.lastOpenProject().has_value());
    CHECK_FALSE(settings.audioDeviceState().has_value());
}

// The settings file preserves the editor project path that should be restored on next launch.
TEST_CASE("EditorSettings persists the last open project", "[core][settings]")
{
    const ScopedSettingsFile settings_file{"persists_project.settings"};
    const std::filesystem::path project_file =
        std::filesystem::path{TEST_SETTINGS_DIR} / "Project With Spaces.rhp";

    {
        EditorSettings settings{settings_file.path()};
        settings.setLastOpenProject(project_file);
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
        settings.setLastOpenProject(project_file);
        settings.setLastOpenProject(std::nullopt);
    }

    const EditorSettings reloaded_settings{settings_file.path()};

    CHECK_FALSE(reloaded_settings.lastOpenProject().has_value());
}

// The settings file preserves the serialized audio-device manager state across launches.
TEST_CASE("EditorSettings persists the audio device state", "[core][settings]")
{
    const ScopedSettingsFile settings_file{"persists_audio_device.settings"};
    const std::string xml_state{
        R"(<DEVICESETUP deviceType="ASIO" audioOutputDeviceName="ASIO Interface"/>)"
    };

    {
        EditorSettings settings{settings_file.path()};
        settings.setAudioDeviceState(xml_state);
    }

    const EditorSettings reloaded_settings{settings_file.path()};

    CHECK(reloaded_settings.audioDeviceState() == std::optional{xml_state});
}

// Clearing audio state removes the persisted device-manager XML from the settings file.
TEST_CASE("EditorSettings clears the audio device state", "[core][settings]")
{
    const ScopedSettingsFile settings_file{"clears_audio_device.settings"};
    const std::string xml_state{"<DEVICESETUP deviceType=\"ASIO\"/>"};

    {
        EditorSettings settings{settings_file.path()};
        settings.setAudioDeviceState(xml_state);
        settings.setAudioDeviceState(std::nullopt);
    }

    const EditorSettings reloaded_settings{settings_file.path()};

    CHECK_FALSE(reloaded_settings.audioDeviceState().has_value());
}

} // namespace rock_hero::editor::core
