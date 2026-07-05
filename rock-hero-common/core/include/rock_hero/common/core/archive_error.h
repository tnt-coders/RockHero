/*!
\file archive_error.h
\brief Typed errors reported by shared ZIP archive helpers.
*/

#pragma once

#include <cstdint>
#include <string>

namespace rock_hero::common::core
{

/*! \brief Stable archive helper failure reasons. */
enum class ArchiveErrorCode : std::uint8_t
{
    /*! \brief An input archive could not be opened for reading. */
    OpenFailed,

    /*! \brief An archive entry was unsafe, duplicated, or otherwise invalid. */
    UnsafeEntry,

    /*! \brief An archive entry stream could not be opened for extraction. */
    EntryOpenFailed,

    /*! \brief A directory needed for extracted output could not be created. */
    OutputDirectoryCreationFailed,

    /*! \brief Extracted entry bytes could not be written to disk. */
    OutputWriteFailed,

    /*! \brief Archive entry bytes could not be read. */
    EntryReadFailed,

    /*! \brief A destination archive parent directory could not be created. */
    ArchiveDirectoryCreationFailed,

    /*! \brief A destination archive could not be opened for writing. */
    OpenForWritingFailed,

    /*! \brief The workspace directory could not be enumerated for archiving. */
    WorkspaceEnumerationFailed,

    /*! \brief A workspace entry could not be inspected while archiving. */
    WorkspaceFileInspectionFailed,

    /*! \brief A workspace file path could not be represented safely in the archive. */
    UnsafeWorkspacePath,

    /*! \brief A workspace file could not be opened as an archive source. */
    WorkspaceFileReadFailed,

    /*! \brief A workspace file could not be added as an archive entry. */
    EntryAddFailed,

    /*! \brief The destination archive could not be finalized. */
    WriteFailed,
};

/*!
\brief Archive helper error with a stable code and diagnostic message.

Archive helpers report safe ZIP operations without package-format knowledge. Public package APIs
that call archive helpers should translate archive failures into the package API's error domain.
*/
struct [[nodiscard]] ArchiveError
{
    /*! \brief Stable reason for the archive failure. */
    ArchiveErrorCode code{};

    /*! \brief Human-readable diagnostic text for UI display or logs. */
    std::string message;

    /*!
    \brief Creates an archive error with the default message for the code.
    \param error_code Stable archive failure code.
    */
    explicit ArchiveError(ArchiveErrorCode error_code);

    /*!
    \brief Creates an archive error with explicit diagnostic text.
    \param error_code Stable archive failure code.
    \param message_text Human-readable diagnostic text.
    */
    ArchiveError(ArchiveErrorCode error_code, std::string message_text);
};

} // namespace rock_hero::common::core
