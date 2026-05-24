#include "project_error.h"

#include <string>
#include <utility>

namespace rock_hero::editor::core
{

namespace
{

// Provides fallback text for call sites that have no richer runtime context.
[[nodiscard]] std::string defaultProjectErrorMessage(ProjectErrorCode code)
{
    switch (code)
    {
        case ProjectErrorCode::MissingProjectPackage:
        {
            return "Project package does not exist.";
        }
        case ProjectErrorCode::WorkspaceCreationFailed:
        {
            return "Could not create project workspace.";
        }
        case ProjectErrorCode::CouldNotExtractPackage:
        {
            return "Could not extract project package.";
        }
        case ProjectErrorCode::MissingProjectDocument:
        {
            return "Project directory does not contain project.json.";
        }
        case ProjectErrorCode::InvalidProjectDocument:
        {
            return "Invalid project.json.";
        }
        case ProjectErrorCode::InvalidSongPackage:
        {
            return "Invalid project song package.";
        }
        case ProjectErrorCode::SongImportFailed:
        {
            return "Could not import song source.";
        }
        case ProjectErrorCode::InvalidImportedSong:
        {
            return "Imported song is not valid for an editor project.";
        }
        case ProjectErrorCode::SavePathRequired:
        {
            return "Cannot save before a project package path has been chosen.";
        }
        case ProjectErrorCode::MissingWorkspace:
        {
            return "Project workspace does not exist.";
        }
        case ProjectErrorCode::CouldNotWriteProjectFiles:
        {
            return "Could not write project files.";
        }
        case ProjectErrorCode::CouldNotWritePackage:
        {
            return "Could not write project package.";
        }
        case ProjectErrorCode::PublishPathRequired:
        {
            return "Cannot publish without a native song package path.";
        }
        case ProjectErrorCode::CouldNotPublishSong:
        {
            return "Could not publish native song package.";
        }
        case ProjectErrorCode::CouldNotCloseWorkspace:
        {
            return "Could not remove project workspace.";
        }
        case ProjectErrorCode::AudioNormalizationFailed:
        {
            return "Could not normalize backing audio.";
        }
    }

    return "Project operation failed.";
}

} // namespace

ProjectError::ProjectError(ProjectErrorCode error_code)
    : ProjectError(error_code, defaultProjectErrorMessage(error_code))
{}

ProjectError::ProjectError(ProjectErrorCode error_code, std::string message_text)
    : code(error_code)
    , message(std::move(message_text))
{}

} // namespace rock_hero::editor::core
