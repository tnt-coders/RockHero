#include "project.h"

#include "project_io.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <rock_hero/common/audio/audio_normalization.h>
#include <rock_hero/common/core/archive_io.h>
#include <rock_hero/common/core/audio_asset.h>
#include <rock_hero/common/core/audio_normalization.h>
#include <rock_hero/common/core/rock_song_package.h>
#include <rock_hero/common/core/workspace_paths.h>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

using common::core::Arrangement;
using common::core::AudioAsset;
using common::core::Song;

// Creates one workspace directory under the platform temp directory for an open project.
[[nodiscard]] std::expected<std::filesystem::path, ProjectError> createWorkspaceDirectory()
{
    std::error_code error;
    const std::filesystem::path temp_root = std::filesystem::temp_directory_path(error);
    if (error)
    {
        return std::unexpected{ProjectError{
            ProjectErrorCode::WorkspaceCreationFailed,
            "Could not locate temporary directory: " + error.message(),
        }};
    }

    const std::filesystem::path workspace_root = temp_root / "RockHero" / "project-workspaces";
    std::filesystem::create_directories(workspace_root, error);
    if (error)
    {
        return std::unexpected{ProjectError{
            ProjectErrorCode::WorkspaceCreationFailed,
            "Could not create project workspace root: " + error.message(),
        }};
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
            return std::unexpected{ProjectError{
                ProjectErrorCode::WorkspaceCreationFailed,
                "Could not create project workspace directory: " + error.message(),
            }};
        }
    }

    return std::unexpected{ProjectError{
        ProjectErrorCode::WorkspaceCreationFailed,
        "Could not allocate a unique project workspace directory",
    }};
}

// Renamed from normalizeImportedSong because this helper only resolves arrangement audio paths
// into the imported workspace; loudness normalization happens in a separate pass below.
[[nodiscard]] std::expected<Song, ProjectError> resolveImportedSongAudioPaths(
    const std::filesystem::path& workspace_directory, Song song)
{
    if (song.arrangements.empty())
    {
        return std::unexpected{ProjectError{
            ProjectErrorCode::InvalidImportedSong,
            "Imported project must contain at least one arrangement",
        }};
    }

    for (Arrangement& arrangement : song.arrangements)
    {
        if (arrangement.audio_asset.path.empty())
        {
            return std::unexpected{ProjectError{
                ProjectErrorCode::InvalidImportedSong,
                "Imported arrangements must reference audio",
            }};
        }

        const auto relative_audio_path =
            common::core::relativeWorkspacePath(workspace_directory, arrangement.audio_asset.path);
        if (!relative_audio_path.has_value())
        {
            return std::unexpected{ProjectError{
                ProjectErrorCode::InvalidImportedSong,
                "Imported arrangement audio is missing or outside the project workspace",
            }};
        }

        arrangement.audio_asset.path =
            (workspace_directory / *relative_audio_path).lexically_normal();
    }

    return std::expected<Song, ProjectError>{std::in_place, std::move(song)};
}

// Collects unique resolved audio paths referenced by arrangements. Order is preserved so the
// normalized output filenames are deterministic across reruns of the same import.
[[nodiscard]] std::vector<std::filesystem::path> collectUniqueAudioAssets(const Song& song)
{
    std::vector<std::filesystem::path> unique_paths;
    unique_paths.reserve(song.arrangements.size());
    for (const Arrangement& arrangement : song.arrangements)
    {
        const std::filesystem::path& path = arrangement.audio_asset.path;
        if (std::find(unique_paths.begin(), unique_paths.end(), path) == unique_paths.end())
        {
            unique_paths.push_back(path);
        }
    }
    return unique_paths;
}

// Converts a common::audio::AudioNormalizationError into the project workflow's error type while
// preserving the underlying diagnostic message for UI surfaces.
[[nodiscard]] ProjectError projectErrorFromNormalizationError(
    const common::audio::AudioNormalizationError& error)
{
    return ProjectError{
        ProjectErrorCode::AudioNormalizationFailed,
        error.message,
    };
}

// Analyzes each unique source path and records the resulting normalization metadata so the
// arrangement update pass can attach it without re-running the analyzer.
[[nodiscard]] std::expected<
    std::unordered_map<std::string, common::core::AudioNormalization>, ProjectError>
analyzeImportedAudioAssets(
    const std::vector<std::filesystem::path>& unique_paths,
    const common::core::AudioNormalizationTarget& target,
    const AudioNormalizationAnalyzer& analyze_audio_normalization)
{
    std::unordered_map<std::string, common::core::AudioNormalization> results;
    results.reserve(unique_paths.size());

    for (const std::filesystem::path& source_path : unique_paths)
    {
        auto metadata = analyze_audio_normalization(source_path, target);
        if (!metadata.has_value())
        {
            return std::unexpected{projectErrorFromNormalizationError(metadata.error())};
        }
        results.emplace(source_path.string(), std::move(*metadata));
    }

    return results;
}

// Attaches the analyzed normalization metadata to each arrangement's audio asset. Audio file paths
// are unchanged; gain is applied during playback and waveform drawing.
void attachNormalizationMetadata(
    Song& song, const std::unordered_map<std::string, common::core::AudioNormalization>& results)
{
    for (Arrangement& arrangement : song.arrangements)
    {
        const auto entry = results.find(arrangement.audio_asset.path.string());
        if (entry == results.end())
        {
            continue;
        }
        arrangement.audio_asset.normalization = entry->second;
    }
}

// Validates loaded normalization metadata and repairs any missing or stale records before the
// project is returned to the editor. Existing valid records are copied to sibling arrangements
// that share the same audio file so one backing asset has one authoritative gain.
[[nodiscard]] std::expected<bool, ProjectError> ensureLoadedAudioNormalization(
    Song& song, const common::core::AudioNormalizationTarget& target,
    const AudioNormalizationAnalyzer& analyze_audio_normalization)
{
    bool updated = false;
    const std::vector<std::filesystem::path> unique_paths = collectUniqueAudioAssets(song);
    for (const std::filesystem::path& audio_path : unique_paths)
    {
        std::optional<common::core::AudioNormalization> valid_normalization;
        const common::core::AudioNormalization* stored_normalization = nullptr;
        for (const Arrangement& arrangement : song.arrangements)
        {
            if (arrangement.audio_asset.path != audio_path ||
                !arrangement.audio_asset.normalization.has_value())
            {
                continue;
            }

            stored_normalization = &*arrangement.audio_asset.normalization;
            break;
        }

        if (stored_normalization != nullptr &&
            common::audio::validateAudioNormalization(audio_path, *stored_normalization))
        {
            valid_normalization = *stored_normalization;
        }

        if (!valid_normalization.has_value())
        {
            auto analyzed_normalization = analyze_audio_normalization(audio_path, target);
            if (!analyzed_normalization.has_value())
            {
                return std::unexpected{projectErrorFromNormalizationError(
                    analyzed_normalization.error())};
            }
            valid_normalization = std::move(*analyzed_normalization);
        }

        for (Arrangement& arrangement : song.arrangements)
        {
            if (arrangement.audio_asset.path == audio_path &&
                arrangement.audio_asset.normalization != valid_normalization)
            {
                arrangement.audio_asset.normalization = valid_normalization;
                updated = true;
            }
        }
    }

    return updated;
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
            m_audio_normalization_updated_on_load = false;
        }
    }
    catch (...)
    {
        m_path.clear();
        m_workspace_directory.clear();
        m_editor_state = ProjectEditorState{};
        m_audio_normalization_updated_on_load = false;
    }
}

// Transfers project package state and workspace ownership, clearing the source paths.
Project::Project(Project&& other) noexcept
    : m_path(std::exchange(other.m_path, {}))
    , m_workspace_directory(std::exchange(other.m_workspace_directory, {}))
    , m_editor_state(std::exchange(other.m_editor_state, {}))
    , m_audio_normalization_updated_on_load(
          std::exchange(other.m_audio_normalization_updated_on_load, false))
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
                m_audio_normalization_updated_on_load = false;
            }
        }
        catch (...)
        {
            m_path.clear();
            m_workspace_directory.clear();
            m_editor_state = ProjectEditorState{};
            m_audio_normalization_updated_on_load = false;
        }

        m_path = std::exchange(other.m_path, {});
        m_workspace_directory = std::exchange(other.m_workspace_directory, {});
        m_editor_state = std::exchange(other.m_editor_state, {});
        m_audio_normalization_updated_on_load =
            std::exchange(other.m_audio_normalization_updated_on_load, false);
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

// Reports whether load repaired normalization metadata that has not yet been saved.
bool Project::audioNormalizationUpdatedOnLoad() const noexcept
{
    return m_audio_normalization_updated_on_load;
}

// Opens the project package archive, extracts it safely, and reads the song document.
std::expected<Song, ProjectError> Project::load(
    const std::filesystem::path& package_path, const common::core::AudioNormalizationTarget& target,
    const AudioNormalizationAnalyzer& analyze_audio_normalization)
{
    std::error_code filesystem_error;
    if (!std::filesystem::is_regular_file(package_path, filesystem_error))
    {
        return std::unexpected{ProjectError{
            ProjectErrorCode::MissingProjectPackage,
            "Project package does not exist: " + package_path.string(),
        }};
    }

    auto workspace_directory = createWorkspaceDirectory();
    if (!workspace_directory.has_value())
    {
        return std::unexpected{std::move(workspace_directory.error())};
    }

    Project loaded_project;
    loaded_project.m_path = package_path;
    loaded_project.m_workspace_directory = std::move(*workspace_directory);

    const auto extraction_error =
        common::core::extractArchiveToWorkspace(package_path, loaded_project.m_workspace_directory);
    if (!extraction_error.has_value())
    {
        return std::unexpected{ProjectError{
            ProjectErrorCode::CouldNotExtractPackage,
            "Could not extract project package: " + extraction_error.error().message,
        }};
    }

    auto editor_state = project_io::readProjectDocument(loaded_project.m_workspace_directory);
    if (!editor_state.has_value())
    {
        return std::unexpected{std::move(editor_state.error())};
    }

    auto loaded_song = common::core::readRockSongPackageDirectory(
        loaded_project.m_workspace_directory / project_io::g_song_directory_name);
    if (!loaded_song.has_value())
    {
        return std::unexpected{ProjectError{
            ProjectErrorCode::InvalidSongPackage,
            std::move(loaded_song.error().message),
        }};
    }

    Song song = std::move(*loaded_song);
    auto normalization_result =
        ensureLoadedAudioNormalization(song, target, analyze_audio_normalization);
    if (!normalization_result.has_value())
    {
        return std::unexpected{std::move(normalization_result.error())};
    }

    loaded_project.m_editor_state = std::move(*editor_state);
    loaded_project.m_audio_normalization_updated_on_load = *normalization_result;
    // TODO: Surface a non-fatal load warning if selectedArrangement no longer matches any
    // arrangement ID; EditorController currently falls back to the first arrangement silently.
    if (auto close_result = close(); !close_result.has_value())
    {
        return std::unexpected{std::move(close_result.error())};
    }

    *this = std::move(loaded_project);

    return song;
}

// Imports a song source into a new workspace without assigning a project package path.
std::expected<Song, ProjectError> Project::import(
    const std::filesystem::path& source_path, ISongImporter& importer,
    const common::core::AudioNormalizationTarget& target,
    const AudioNormalizationAnalyzer& analyze_audio_normalization)
{
    auto workspace_directory = createWorkspaceDirectory();
    if (!workspace_directory.has_value())
    {
        return std::unexpected{std::move(workspace_directory.error())};
    }

    Project imported_project;
    imported_project.m_workspace_directory = std::move(*workspace_directory);
    const std::filesystem::path song_directory =
        imported_project.m_workspace_directory / project_io::g_song_directory_name;
    std::error_code create_error;
    std::filesystem::create_directories(song_directory, create_error);
    if (create_error)
    {
        return std::unexpected{ProjectError{
            ProjectErrorCode::WorkspaceCreationFailed,
            "Could not create song directory: " + create_error.message(),
        }};
    }

    auto imported_song = importer.importSong(source_path, song_directory);
    if (!imported_song.has_value())
    {
        return std::unexpected{ProjectError{
            ProjectErrorCode::SongImportFailed,
            std::move(imported_song.error().message),
        }};
    }

    auto resolved_song = resolveImportedSongAudioPaths(song_directory, std::move(*imported_song));
    if (!resolved_song.has_value())
    {
        return std::unexpected{std::move(resolved_song.error())};
    }

    Song song = std::move(*resolved_song);
    const std::vector<std::filesystem::path> unique_source_paths = collectUniqueAudioAssets(song);
    auto analysis_results =
        analyzeImportedAudioAssets(unique_source_paths, target, analyze_audio_normalization);
    if (!analysis_results.has_value())
    {
        return std::unexpected{std::move(analysis_results.error())};
    }

    attachNormalizationMetadata(song, *analysis_results);

    if (auto close_result = close(); !close_result.has_value())
    {
        return std::unexpected{std::move(close_result.error())};
    }

    *this = std::move(imported_project);

    return song;
}

// Writes the current session song to the open project workspace and package.
std::expected<void, ProjectError> Project::save(const Song& song)
{
    return save(song, m_editor_state);
}

// Writes the current session song and editor state to the open project workspace and package.
std::expected<void, ProjectError> Project::save(const Song& song, ProjectEditorState editor_state)
{
    if (m_path.empty() || m_workspace_directory.empty())
    {
        return std::unexpected{ProjectError{
            ProjectErrorCode::SavePathRequired,
            "Cannot save before a project package path has been chosen",
        }};
    }

    std::error_code error;
    if (!std::filesystem::is_directory(m_workspace_directory, error))
    {
        return std::unexpected{ProjectError{
            ProjectErrorCode::MissingWorkspace,
            "Project workspace does not exist",
        }};
    }

    if (auto write_error = project_io::writeProjectFiles(m_workspace_directory, song, editor_state);
        !write_error.has_value())
    {
        return std::unexpected{std::move(write_error.error())};
    }

    if (const auto package_error =
            common::core::writeWorkspaceToArchive(m_workspace_directory, m_path);
        !package_error.has_value())
    {
        return std::unexpected{ProjectError{
            ProjectErrorCode::CouldNotWritePackage,
            "Could not write project package: " + package_error.error().message,
        }};
    }

    m_editor_state = std::move(editor_state);
    m_audio_normalization_updated_on_load = false;
    return std::expected<void, ProjectError>{};
}

// Saves to a chosen project package path, creating a workspace first when unopened.
std::expected<void, ProjectError> Project::saveAs(
    const std::filesystem::path& path, const Song& song)
{
    return saveAs(path, song, m_editor_state);
}

// Saves song data and editor state to a chosen project package path.
std::expected<void, ProjectError> Project::saveAs(
    const std::filesystem::path& path, const Song& song, ProjectEditorState editor_state)
{
    if (path.empty())
    {
        return std::unexpected{ProjectError{
            ProjectErrorCode::SavePathRequired,
            "Cannot save a project without a project package path",
        }};
    }

    if (m_workspace_directory.empty())
    {
        auto workspace_directory = createWorkspaceDirectory();
        if (!workspace_directory.has_value())
        {
            return std::unexpected{std::move(workspace_directory.error())};
        }

        Project saved_project;
        saved_project.m_path = path;
        saved_project.m_workspace_directory = std::move(*workspace_directory);

        if (auto write_error = project_io::writeProjectFiles(
                saved_project.m_workspace_directory, song, editor_state);
            !write_error.has_value())
        {
            return std::unexpected{std::move(write_error.error())};
        }

        if (const auto package_error = common::core::writeWorkspaceToArchive(
                saved_project.m_workspace_directory, saved_project.m_path);
            !package_error.has_value())
        {
            return std::unexpected{ProjectError{
                ProjectErrorCode::CouldNotWritePackage,
                "Could not write project package: " + package_error.error().message,
            }};
        }

        saved_project.m_editor_state = std::move(editor_state);
        saved_project.m_audio_normalization_updated_on_load = false;
        *this = std::move(saved_project);
        return std::expected<void, ProjectError>{};
    }

    if (auto write_error = project_io::writeProjectFiles(m_workspace_directory, song, editor_state);
        !write_error.has_value())
    {
        return std::unexpected{std::move(write_error.error())};
    }

    if (const auto package_error =
            common::core::writeWorkspaceToArchive(m_workspace_directory, path);
        !package_error.has_value())
    {
        return std::unexpected{ProjectError{
            ProjectErrorCode::CouldNotWritePackage,
            "Could not write project package: " + package_error.error().message,
        }};
    }

    m_path = path;
    m_editor_state = std::move(editor_state);
    m_audio_normalization_updated_on_load = false;
    return std::expected<void, ProjectError>{};
}

// Publishes native song content without project metadata or retargeting future saves.
std::expected<void, ProjectError> Project::publish(
    const std::filesystem::path& path, const Song& song)
{
    if (path.empty())
    {
        return std::unexpected{ProjectError{
            ProjectErrorCode::PublishPathRequired,
            "Cannot publish a project without a native song package path",
        }};
    }

    if (m_workspace_directory.empty())
    {
        return std::unexpected{ProjectError{
            ProjectErrorCode::MissingWorkspace,
            "Cannot publish before a project workspace exists",
        }};
    }

    const std::filesystem::path song_directory =
        m_workspace_directory / project_io::g_song_directory_name;
    std::error_code error;
    if (!std::filesystem::is_directory(m_workspace_directory, error))
    {
        return std::unexpected{ProjectError{
            ProjectErrorCode::MissingWorkspace,
            "Project workspace does not exist",
        }};
    }

    if (const auto publish_result = common::core::writeRockSongPackage(path, song_directory, song);
        !publish_result.has_value())
    {
        return std::unexpected{ProjectError{
            ProjectErrorCode::CouldNotPublishSong,
            publish_result.error().message,
        }};
    }

    return std::expected<void, ProjectError>{};
}

// Reports workspace cleanup failure for callers that explicitly close a project.
std::expected<void, ProjectError> Project::close()
{
    if (m_workspace_directory.empty())
    {
        m_path.clear();
        m_editor_state = ProjectEditorState{};
        m_audio_normalization_updated_on_load = false;
        return std::expected<void, ProjectError>{};
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
        return std::unexpected{ProjectError{
            ProjectErrorCode::CouldNotCloseWorkspace,
            std::move(message),
        }};
    }
    catch (...)
    {
        return std::unexpected{ProjectError{
            ProjectErrorCode::CouldNotCloseWorkspace,
            "Could not remove project workspace",
        }};
    }

    if (error)
    {
        return std::unexpected{ProjectError{
            ProjectErrorCode::CouldNotCloseWorkspace,
            "Could not remove project workspace: " + error.message(),
        }};
    }

    m_path.clear();
    m_workspace_directory.clear();
    m_editor_state = ProjectEditorState{};
    m_audio_normalization_updated_on_load = false;
    return std::expected<void, ProjectError>{};
}

} // namespace rock_hero::editor::core
