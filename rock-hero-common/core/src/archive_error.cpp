#include "archive_error.h"

#include <string>
#include <utility>

namespace rock_hero::common::core
{

namespace
{

// Provides fallback text for call sites that have no richer runtime context.
[[nodiscard]] std::string defaultArchiveErrorMessage(ArchiveErrorCode code)
{
    switch (code)
    {
        case ArchiveErrorCode::OpenFailed:
        {
            return "Could not open archive.";
        }
        case ArchiveErrorCode::UnsafeEntry:
        {
            return "Archive contains an unsafe entry.";
        }
        case ArchiveErrorCode::EntryOpenFailed:
        {
            return "Could not open archive entry for extraction.";
        }
        case ArchiveErrorCode::OutputDirectoryCreationFailed:
        {
            return "Could not create archive output directory.";
        }
        case ArchiveErrorCode::OutputWriteFailed:
        {
            return "Could not write archive entry.";
        }
        case ArchiveErrorCode::EntryReadFailed:
        {
            return "Could not read archive entry.";
        }
        case ArchiveErrorCode::ArchiveDirectoryCreationFailed:
        {
            return "Could not create archive directory.";
        }
        case ArchiveErrorCode::OpenForWritingFailed:
        {
            return "Could not open archive for writing.";
        }
        case ArchiveErrorCode::WorkspaceEnumerationFailed:
        {
            return "Could not enumerate archive workspace.";
        }
        case ArchiveErrorCode::WorkspaceFileInspectionFailed:
        {
            return "Could not inspect archive workspace file.";
        }
        case ArchiveErrorCode::UnsafeWorkspacePath:
        {
            return "Archive workspace contains an unsafe file path.";
        }
        case ArchiveErrorCode::WorkspaceFileReadFailed:
        {
            return "Could not read archive workspace file.";
        }
        case ArchiveErrorCode::EntryAddFailed:
        {
            return "Could not add archive entry.";
        }
        case ArchiveErrorCode::WriteFailed:
        {
            return "Could not write archive.";
        }
    }

    return "Archive operation failed.";
}

} // namespace

ArchiveError::ArchiveError(ArchiveErrorCode error_code)
    : ArchiveError(error_code, defaultArchiveErrorMessage(error_code))
{}

ArchiveError::ArchiveError(ArchiveErrorCode error_code, std::string message_text)
    : code(error_code)
    , message(std::move(message_text))
{}

} // namespace rock_hero::common::core
