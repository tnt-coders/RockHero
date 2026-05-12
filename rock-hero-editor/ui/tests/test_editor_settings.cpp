#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <optional>
#include <rock_hero/editor/ui/editor_settings.h>
#include <string_view>
#include <system_error>

#ifndef TEST_SETTINGS_DIR
#define TEST_SETTINGS_DIR "."
#endif

namespace rock_hero::editor::ui
{

namespace
{

// Owns one build-local settings file so each test starts with clean persisted state.
class ScopedSettingsFile final
{
public:
    explicit ScopedSettingsFile(std::string_view file_name)
        : m_path(std::filesystem::path{TEST_SETTINGS_DIR} / file_name)
    {
        removeFile();
    }

    ~ScopedSettingsFile()
    {
        removeFile();
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return m_path;
    }

private:
    void removeFile() const
    {
        std::error_code error;
        std::filesystem::remove(m_path, error);
    }

    std::filesystem::path m_path;
};

} // namespace

// New settings files do not invent a restore target until the app exits with a project open.
TEST_CASE("EditorSettings starts without a last open project", "[editor][settings]")
{
    const ScopedSettingsFile settings_file{"starts_empty.settings"};
    const EditorSettings settings{settings_file.path()};

    CHECK_FALSE(settings.lastOpenProject().has_value());
}

// The settings file preserves the native project path that should be restored on next launch.
TEST_CASE("EditorSettings persists the last open project", "[editor][settings]")
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
TEST_CASE("EditorSettings clears the last open project", "[editor][settings]")
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

} // namespace rock_hero::editor::ui
