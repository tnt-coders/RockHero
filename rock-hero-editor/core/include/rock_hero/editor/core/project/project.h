/*!
\file project.h
\brief Editor project package context and workspace ownership.
*/

#pragma once

#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <rock_hero/common/audio/song/audio_normalization.h>
#include <rock_hero/common/core/song/audio_normalization.h>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/editor/core/project/i_song_importer.h>
#include <rock_hero/editor/core/project/project_error.h>
#include <string>

namespace rock_hero::editor::core
{

/*!
\brief Function that analyzes a source audio file and computes normalization metadata.

Injected into Project::load and Project::import to keep the test surface at the public Project API.
Production composition defaults this alias to common::audio::analyzeAudioForGainNormalization;
tests pass fakes that return a controlled AudioNormalization without touching the loudness
analyzer.
*/
using AudioNormalizationAnalyzer = std::function<
    std::expected<common::core::AudioNormalization, common::audio::AudioNormalizationError>(
        const std::filesystem::path& input, const common::core::AudioNormalizationTarget& target)>;

/*!
\brief Editor project package context.

Project owns the extracted workspace directory because loaded Song audio asset paths can point
into it. Call close() when cleanup failures need to be reported. Destroying the Project removes
that workspace on a best-effort basis. Session owns the editable Song; Project owns only the
project package and workspace context needed to load and save it.
*/
class Project
{
public:
    /*! \brief Creates an unopened project context. */
    Project() = default;

    /*! \brief Closes the project context without allowing cleanup errors to escape. */
    ~Project() noexcept;

    /*! \brief Copying is disabled because workspace ownership is unique. */
    Project(const Project&) = delete;

    /*! \brief Copy assignment is disabled because workspace ownership is unique. */
    Project& operator=(const Project&) = delete;

    /*!
    \brief Transfers project package state and workspace ownership from another project.
    \param other Project to move from.
    */
    Project(Project&& other) noexcept;

    /*!
    \brief Replaces this project with another project's package path and workspace ownership.
    \param other Project to move from.
    \return Reference to this project.
    */
    Project& operator=(Project&& other) noexcept;

    /*!
    \brief Loads a Rock Hero project package into this project context.
    \param path Filesystem path to an .rhp archive.
    \param target Loudness target loaded backing audio is analyzed against when metadata is stale.
    \param analyze_audio_normalization Function used to analyze stale or missing normalization.
    \return Parsed song data, or a typed project failure.
    */
    [[nodiscard]] std::expected<common::core::Song, ProjectError> load(
        const std::filesystem::path& path,
        const common::core::AudioNormalizationTarget& target = {},
        const AudioNormalizationAnalyzer& analyze_audio_normalization =
            common::audio::analyzeAudioForGainNormalization);

    /*!
    \brief Imports a song source into a new unsaved project workspace.

    Analyzes each unique backing audio file to compute LUFS-I gain normalization metadata. The
    audio files are kept as-is; gain is applied during playback and waveform drawing rather than
    by rendering a new WAV.

    \param source_path Song source to import.
    \param importer Importer that understands the source song format.
    \param target Loudness target the imported backing audio is analyzed against.
    \param analyze_audio_normalization Function used to analyze each unique imported audio asset.
    Defaults to common::audio::analyzeAudioForGainNormalization; tests can inject a fake that
    returns controlled AudioNormalization.
    \return Imported song data, or a typed project failure.
    */
    [[nodiscard]] std::expected<common::core::Song, ProjectError> import(
        const std::filesystem::path& source_path, ISongImporter& importer,
        const common::core::AudioNormalizationTarget& target = {},
        const AudioNormalizationAnalyzer& analyze_audio_normalization =
            common::audio::analyzeAudioForGainNormalization);

    /*!
    \brief Saves the supplied song to the currently open project package.
    \param song Song data to persist.
    \return Empty success, or a typed project failure.
    */
    [[nodiscard]] std::expected<void, ProjectError> save(const common::core::Song& song);

    /*!
    \brief Saves the supplied song to a project package path and associates it.
    \param path Destination `.rhp` project package path.
    \param song Song data to persist.
    \return Empty success, or a typed project failure.
    */
    [[nodiscard]] std::expected<void, ProjectError> saveAs(
        const std::filesystem::path& path, const common::core::Song& song);

    /*!
    \brief Publishes the supplied song to a native song package without changing this project path.
    \param path Destination native song package path.
    \param song Song data to publish.
    \return Empty success, or a typed project failure.
    */
    [[nodiscard]] std::expected<void, ProjectError> publish(
        const std::filesystem::path& path, const common::core::Song& song);

    /*!
    \brief Closes the current project context and removes its extracted workspace.
    \return Empty success, or a typed project failure when workspace removal fails.
    */
    [[nodiscard]] std::expected<void, ProjectError> close();

    /*!
    \brief Returns the file path associated with this project.
    \return Project file path, or an empty path before load succeeds.
    */
    [[nodiscard]] const std::filesystem::path& path() const noexcept;

    /*!
    \brief Returns the directory containing extracted files referenced by loaded song data.
    \return Extracted project workspace directory, or an empty path before load succeeds.
    */
    [[nodiscard]] const std::filesystem::path& workspaceDirectory() const noexcept;

    /*!
    \brief Reports whether the most recent load repaired backing-audio normalization metadata.
    \return True when load updated normalization metadata that should be persisted on save.
    */
    [[nodiscard]] bool audioNormalizationUpdatedOnLoad() const noexcept;

private:
    std::filesystem::path m_path;
    std::filesystem::path m_workspace_directory;
    bool m_audio_normalization_updated_on_load{false};
};

} // namespace rock_hero::editor::core
