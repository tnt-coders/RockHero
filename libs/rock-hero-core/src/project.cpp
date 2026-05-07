#include "project.h"

#include "project_io.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <optional>
#include <rock_hero/core/audio_asset.h>
#include <string>
#include <system_error>
#include <utility>

namespace rock_hero::core
{

namespace
{

// Builds a failed project-load result with a single message.
[[nodiscard]] std::expected<Song, std::string> failProjectLoad(std::string message)
{
    return std::unexpected<std::string>{std::move(message)};
}

// Builds a failed project-import result with a single message.
[[nodiscard]] std::expected<Song, std::string> failProjectImport(std::string message)
{
    return std::unexpected<std::string>{std::move(message)};
}

// Builds a failed project-save result with a single message.
[[nodiscard]] std::expected<void, std::string> failProjectSave(std::string message)
{
    return std::unexpected<std::string>{std::move(message)};
}

// Builds a failed project-publish result with a single message.
[[nodiscard]] std::expected<void, std::string> failProjectPublish(std::string message)
{
    return std::unexpected<std::string>{std::move(message)};
}

// Builds a failed project-close result with a single message.
[[nodiscard]] std::expected<void, std::string> failProjectClose(std::string message)
{
    return std::unexpected<std::string>{std::move(message)};
}

// Creates one workspace directory under the platform temp directory for an open project.
[[nodiscard]] std::optional<std::filesystem::path> createWorkspaceDirectory(
    std::string& error_message)
{
    std::error_code error;
    const std::filesystem::path temp_root = std::filesystem::temp_directory_path(error);
    if (error)
    {
        error_message = "Could not locate temporary directory: " + error.message();
        return std::nullopt;
    }

    const std::filesystem::path workspace_root = temp_root / "RockHero" / "project-workspaces";
    std::filesystem::create_directories(workspace_root, error);
    if (error)
    {
        error_message = "Could not create project workspace root: " + error.message();
        return std::nullopt;
    }

    static std::atomic_uint64_t g_workspace_sequence{0};
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::uint64_t sequence = g_workspace_sequence.fetch_add(1, std::memory_order_relaxed);

    for (int attempt = 0; attempt < 100; ++attempt)
    {
        // Intentionally non-const so return-by-value can move the path.
        std::filesystem::path workspace_directory =
            workspace_root /
            (std::to_string(now) + "-" + std::to_string(sequence) + "-" + std::to_string(attempt));

        error.clear();
        if (std::filesystem::create_directory(workspace_directory, error))
        {
            return workspace_directory;
        }

        if (error)
        {
            error_message = "Could not create project workspace directory: " + error.message();
            return std::nullopt;
        }
    }

    error_message = "Could not allocate a unique project workspace directory";
    return std::nullopt;
}

// Resolves imported arrangement audio into the workspace owned by the imported project.
[[nodiscard]] std::expected<Song, std::string> normalizeImportedSong(
    const std::filesystem::path& workspace_directory, Song song)
{
    if (song.chart.arrangements.empty())
    {
        return failProjectImport("Imported project must contain at least one arrangement");
    }

    for (Arrangement& arrangement : song.chart.arrangements)
    {
        if (arrangement.audio_asset.path.empty())
        {
            return failProjectImport("Imported arrangements must reference audio");
        }

        const auto relative_audio_path =
            project_io::relativeWorkspacePath(workspace_directory, arrangement.audio_asset.path);
        if (!relative_audio_path.has_value())
        {
            return failProjectImport(
                "Imported arrangement audio is missing or outside the project workspace");
        }

        arrangement.audio_asset =
            AudioAsset{(workspace_directory / *relative_audio_path).lexically_normal()};
    }

    return std::expected<Song, std::string>{std::in_place, std::move(song)};
}

} // namespace

// Closes the workspace during object teardown; explicit close() is the reporting path.
Project::~Project() noexcept
{
    try
    {
        if (const auto close_result = close(); !close_result.has_value())
        {
            m_path.clear();
            m_workspace_directory.clear();
            m_editor_state = ProjectEditorState{};
        }
    }
    catch (...)
    {
        m_path.clear();
        m_workspace_directory.clear();
        m_editor_state = ProjectEditorState{};
    }
}

// Transfers package state and workspace ownership, clearing the source paths.
Project::Project(Project&& other) noexcept
    : m_path(std::exchange(other.m_path, {}))
    , m_workspace_directory(std::exchange(other.m_workspace_directory, {}))
    , m_editor_state(std::exchange(other.m_editor_state, {}))
{}

// Removes the old workspace before taking ownership from another project.
Project& Project::operator=(Project&& other) noexcept
{
    if (this != &other)
    {
        try
        {
            if (const auto close_result = close(); !close_result.has_value())
            {
                m_path.clear();
                m_workspace_directory.clear();
                m_editor_state = ProjectEditorState{};
            }
        }
        catch (...)
        {
            m_path.clear();
            m_workspace_directory.clear();
            m_editor_state = ProjectEditorState{};
        }

        m_path = std::exchange(other.m_path, {});
        m_workspace_directory = std::exchange(other.m_workspace_directory, {});
        m_editor_state = std::exchange(other.m_editor_state, {});
    }

    return *this;
}

// Returns the file path associated with this open project.
const std::filesystem::path& Project::path() const noexcept
{
    return m_path;
}

// Returns the extracted workspace path used by parsed audio asset references.
const std::filesystem::path& Project::workspaceDirectory() const noexcept
{
    return m_workspace_directory;
}

// Returns editor-only state read from project.json or default state before loading.
const ProjectEditorState& Project::editorState() const noexcept
{
    return m_editor_state;
}

// Opens the package archive, extracts it safely, and reads the song document.
std::expected<Song, std::string> Project::load(const std::filesystem::path& package_path)
{
    std::error_code filesystem_error;
    if (!std::filesystem::is_regular_file(package_path, filesystem_error))
    {
        return failProjectLoad("Project package does not exist: " + package_path.string());
    }

    std::string error_message;
    auto workspace_directory = createWorkspaceDirectory(error_message);
    if (!workspace_directory.has_value())
    {
        return failProjectLoad(error_message);
    }

    Project loaded_project;
    loaded_project.m_path = package_path;
    loaded_project.m_workspace_directory = std::move(*workspace_directory);

    const auto extraction_error =
        project_io::extractPackageToWorkspace(package_path, loaded_project.m_workspace_directory);
    if (extraction_error.has_value())
    {
        return failProjectLoad(*extraction_error);
    }

    auto editor_state = project_io::readProjectDocument(loaded_project.m_workspace_directory);
    if (!editor_state.has_value())
    {
        return failProjectLoad(std::move(editor_state.error()));
    }

    auto loaded_song = project_io::readSong(
        loaded_project.m_workspace_directory / project_io::g_song_directory_name);
    if (!loaded_song.has_value())
    {
        return failProjectLoad(std::move(loaded_song.error()));
    }

    Song song = std::move(*loaded_song);
    loaded_project.m_editor_state = std::move(*editor_state);
    // TODO: Surface a non-fatal load warning if selectedArrangement no longer matches any
    // arrangement ID; EditorController currently falls back to the first arrangement silently.
    if (auto close_result = close(); !close_result.has_value())
    {
        return failProjectLoad(std::move(close_result.error()));
    }

    *this = std::move(loaded_project);

    return song;
}

// Imports a foreign package into a new workspace without assigning a native package path.
std::expected<Song, std::string> Project::import(
    const std::filesystem::path& source_path, IProjectImporter& importer)
{
    std::string error_message;
    auto workspace_directory = createWorkspaceDirectory(error_message);
    if (!workspace_directory.has_value())
    {
        return failProjectImport(error_message);
    }

    Project imported_project;
    imported_project.m_workspace_directory = std::move(*workspace_directory);
    const std::filesystem::path song_directory =
        imported_project.m_workspace_directory / project_io::g_song_directory_name;
    std::error_code create_error;
    std::filesystem::create_directories(song_directory, create_error);
    if (create_error)
    {
        return failProjectImport("Could not create song directory: " + create_error.message());
    }

    auto imported_song = importer.importProject(source_path, song_directory);
    if (!imported_song.has_value())
    {
        return failProjectImport(std::move(imported_song.error()));
    }

    auto normalized_song = normalizeImportedSong(song_directory, std::move(*imported_song));
    if (!normalized_song.has_value())
    {
        return failProjectImport(std::move(normalized_song.error()));
    }

    Song song = std::move(*normalized_song);
    if (auto close_result = close(); !close_result.has_value())
    {
        return failProjectImport(std::move(close_result.error()));
    }

    *this = std::move(imported_project);

    return song;
}

// Writes the current session song to the open project workspace and package.
std::expected<void, std::string> Project::save(const Song& song)
{
    return save(song, m_editor_state);
}

// Writes the current session song and editor state to the open project workspace and package.
std::expected<void, std::string> Project::save(const Song& song, ProjectEditorState editor_state)
{
    if (m_path.empty() || m_workspace_directory.empty())
    {
        return failProjectSave("Cannot save before a project path has been chosen");
    }

    std::error_code error;
    if (!std::filesystem::is_directory(m_workspace_directory, error))
    {
        return failProjectSave("Project workspace does not exist");
    }

    if (const auto write_error =
            project_io::writeProjectFiles(m_workspace_directory, song, editor_state);
        write_error.has_value())
    {
        return failProjectSave(*write_error);
    }

    if (const auto package_error =
            project_io::writeWorkspaceToPackage(m_workspace_directory, m_path);
        package_error.has_value())
    {
        return failProjectSave(*package_error);
    }

    m_editor_state = std::move(editor_state);
    return std::expected<void, std::string>{};
}

// Saves to a chosen package path, creating a workspace first when this project is unopened.
std::expected<void, std::string> Project::saveAs(
    const std::filesystem::path& path, const Song& song)
{
    return saveAs(path, song, m_editor_state);
}

// Saves song data and editor state to a chosen package path.
std::expected<void, std::string> Project::saveAs(
    const std::filesystem::path& path, const Song& song, ProjectEditorState editor_state)
{
    if (path.empty())
    {
        return failProjectSave("Cannot save a project without a package path");
    }

    if (m_workspace_directory.empty())
    {
        std::string error_message;
        auto workspace_directory = createWorkspaceDirectory(error_message);
        if (!workspace_directory.has_value())
        {
            return failProjectSave(error_message);
        }

        Project saved_project;
        saved_project.m_path = path;
        saved_project.m_workspace_directory = std::move(*workspace_directory);

        if (const auto write_error = project_io::writeProjectFiles(
                saved_project.m_workspace_directory, song, editor_state);
            write_error.has_value())
        {
            return failProjectSave(*write_error);
        }

        if (const auto package_error = project_io::writeWorkspaceToPackage(
                saved_project.m_workspace_directory, saved_project.m_path);
            package_error.has_value())
        {
            return failProjectSave(*package_error);
        }

        saved_project.m_editor_state = std::move(editor_state);
        *this = std::move(saved_project);
        return std::expected<void, std::string>{};
    }

    if (const auto write_error =
            project_io::writeProjectFiles(m_workspace_directory, song, editor_state);
        write_error.has_value())
    {
        return failProjectSave(*write_error);
    }

    if (const auto package_error = project_io::writeWorkspaceToPackage(m_workspace_directory, path);
        package_error.has_value())
    {
        return failProjectSave(*package_error);
    }

    m_path = path;
    m_editor_state = std::move(editor_state);
    return std::expected<void, std::string>{};
}

// Publishes runtime song content without project metadata or retargeting future saves.
std::expected<void, std::string> Project::publish(
    const std::filesystem::path& path, const Song& song)
{
    if (path.empty())
    {
        return failProjectPublish("Cannot publish a project without a package path");
    }

    if (m_workspace_directory.empty())
    {
        return failProjectPublish("Cannot publish before a project workspace exists");
    }

    const std::filesystem::path song_directory =
        m_workspace_directory / project_io::g_song_directory_name;
    std::error_code error;
    if (!std::filesystem::is_directory(m_workspace_directory, error))
    {
        return failProjectPublish("Project workspace does not exist");
    }

    if (const auto song_files = project_io::writeSongFiles(song_directory, song);
        !song_files.has_value())
    {
        return failProjectPublish(song_files.error());
    }

    if (const auto package_error = project_io::writeWorkspaceToPackage(song_directory, path);
        package_error.has_value())
    {
        return failProjectPublish(*package_error);
    }

    return std::expected<void, std::string>{};
}

// Reports workspace cleanup failure for callers that explicitly close a project.
std::expected<void, std::string> Project::close()
{
    if (m_workspace_directory.empty())
    {
        m_path.clear();
        m_editor_state = ProjectEditorState{};
        return std::expected<void, std::string>{};
    }

    std::error_code error;
    try
    {
        std::filesystem::remove_all(m_workspace_directory, error);
    }
    catch (const std::exception& exception)
    {
        std::string message = "Could not remove project workspace: ";
        message += exception.what();
        return failProjectClose(std::move(message));
    }
    catch (...)
    {
        return failProjectClose("Could not remove project workspace");
    }

    if (error)
    {
        return failProjectClose("Could not remove project workspace: " + error.message());
    }

    m_path.clear();
    m_workspace_directory.clear();
    m_editor_state = ProjectEditorState{};
    return std::expected<void, std::string>{};
}

} // namespace rock_hero::core
