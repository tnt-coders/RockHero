/*!
\file project_error.h
\brief Typed errors reported by editor project workflow operations.
*/

#pragma once

#include <string>

namespace rock_hero::editor::core
{

/*! \brief Stable editor project failure reasons. */
enum class ProjectErrorCode
{
    /*! \brief The requested editor project package was missing. */
    MissingProjectPackage,

    /*! \brief A temporary editor project workspace could not be created. */
    WorkspaceCreationFailed,

    /*! \brief An editor project package archive could not be extracted. */
    CouldNotExtractPackage,

    /*! \brief The required editor project document was missing. */
    MissingProjectDocument,

    /*! \brief The editor project document was malformed or unsupported. */
    InvalidProjectDocument,

    /*! \brief The nested native song package content was missing or invalid. */
    InvalidSongPackage,

    /*! \brief A source song could not be imported into the editor workspace. */
    SongImportFailed,

    /*! \brief Imported song data did not satisfy editor project requirements. */
    InvalidImportedSong,

    /*! \brief A save operation required a project package path that was not available. */
    SavePathRequired,

    /*! \brief A save or publish operation required a workspace that was not available. */
    MissingWorkspace,

    /*! \brief Editor project files could not be written into the workspace. */
    CouldNotWriteProjectFiles,

    /*! \brief The editor project package archive could not be written. */
    CouldNotWritePackage,

    /*! \brief A publish operation required a native song package path that was not available. */
    PublishPathRequired,

    /*! \brief Native song package content could not be published. */
    CouldNotPublishSong,

    /*! \brief The editor project workspace could not be removed. */
    CouldNotCloseWorkspace,

    /*! \brief Loudness normalization of backing audio failed. */
    AudioNormalizationFailed,
};

/*! \brief Editor project error with a stable code and diagnostic message. */
struct [[nodiscard]] ProjectError
{
    /*! \brief Stable reason for the project workflow failure. */
    ProjectErrorCode code{};

    /*! \brief Human-readable diagnostic text for UI display or logs. */
    std::string message;

    /*!
    \brief Creates a project error with the default message for the code.
    \param error_code Stable project failure code.
    */
    explicit ProjectError(ProjectErrorCode error_code);

    /*!
    \brief Creates a project error with explicit diagnostic text.
    \param error_code Stable project failure code.
    \param message_text Human-readable diagnostic text.
    */
    ProjectError(ProjectErrorCode error_code, std::string message_text);
};

} // namespace rock_hero::editor::core
