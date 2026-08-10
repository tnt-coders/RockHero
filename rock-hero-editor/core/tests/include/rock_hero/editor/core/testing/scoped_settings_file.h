/*!
\file scoped_settings_file.h
\brief RAII fixture owning one build-local settings file for persistence tests.
*/

#pragma once

#include <filesystem>
#include <rock_hero/editor/core/settings/editor_settings.h>
#include <string_view>
#include <system_error>

// Test targets point this at their own build directory; the fallback keeps the fixture usable from
// a target that has no settings directory of its own.
#ifndef TEST_SETTINGS_DIR
#define TEST_SETTINGS_DIR "."
#endif

namespace rock_hero::editor::core::testing
{

/*!
\brief Owns one build-local settings file so each test starts and ends with clean persisted state.

Removal happens on construction as well as destruction, so a file left behind by an aborted run
cannot seed the next one. An EditorSettings store opens an audio-config file beside its settings
file, so that sibling is removed too — otherwise calibration state outlives the test that wrote it.
*/
class ScopedSettingsFile final
{
public:
    /*!
    \brief Creates the settings-file path and removes any stale file from a prior test run.
    \param file_name File name inside the build-local test settings directory.
    */
    explicit ScopedSettingsFile(std::string_view file_name)
        : m_path(std::filesystem::path{TEST_SETTINGS_DIR} / file_name)
    {
        removeFiles();
    }

    /*! \brief Removes the owned files so persisted state cannot leak into later tests. */
    ~ScopedSettingsFile()
    {
        removeFiles();
    }

    ScopedSettingsFile(const ScopedSettingsFile&) = delete;
    ScopedSettingsFile& operator=(const ScopedSettingsFile&) = delete;
    ScopedSettingsFile(ScopedSettingsFile&&) = delete;
    ScopedSettingsFile& operator=(ScopedSettingsFile&&) = delete;

    /*!
    \brief Returns the test-owned settings-file path.
    \return Path to the settings file inside the build-local test settings directory.
    */
    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return m_path;
    }

private:
    // Removes the settings file and its audio-config sibling on a best-effort basis.
    void removeFiles() const
    {
        std::error_code error;
        std::filesystem::remove(m_path, error);
        std::filesystem::remove(EditorSettings::audioConfigFileFor(m_path), error);
    }

    // Build-local settings path owned by this fixture.
    std::filesystem::path m_path;
};

} // namespace rock_hero::editor::core::testing
