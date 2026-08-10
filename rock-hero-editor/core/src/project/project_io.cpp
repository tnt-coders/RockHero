#include "project_io.h"

#include <expected>
#include <filesystem>
#include <fstream>
#include <juce_core/juce_core.h>
#include <rock_hero/common/core/package/rock_song_package.h>
#include <rock_hero/common/core/shared/json.h>
#include <rock_hero/common/core/shared/juce_path.h>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace rock_hero::editor::core::project_io
{

namespace
{
constexpr std::string_view g_project_document_name{"project.json"};

using common::core::Json;

} // namespace

// Validates the project.json manifest at the extracted editor project root.
std::expected<void, ProjectError> readProjectDocument(
    const std::filesystem::path& workspace_directory)
{
    const std::filesystem::path project_document_path =
        workspace_directory / g_project_document_name;
    std::error_code error;
    if (!std::filesystem::is_regular_file(project_document_path, error))
    {
        return std::unexpected{ProjectError{ProjectErrorCode::MissingProjectDocument}};
    }

    juce::FileInputStream project_document_file{common::core::juceFileFromPath(
        project_document_path)};
    if (project_document_file.failedToOpen())
    {
        return std::unexpected{ProjectError{
            ProjectErrorCode::InvalidProjectDocument,
            "Could not open project.json: " +
                project_document_file.getStatus().getErrorMessage().toStdString(),
        }};
    }

    auto parsed_document = Json::parseDocument(project_document_file.readEntireStreamAsString());
    if (!parsed_document.has_value())
    {
        return std::unexpected{ProjectError{
            ProjectErrorCode::InvalidProjectDocument,
            "Could not parse project.json: " + parsed_document.error().message
        }};
    }

    const juce::var project_document = std::move(*parsed_document);
    if (!project_document.isObject() ||
        Json::readOptionalInt(project_document, "formatVersion", 0) != 1)
    {
        return std::unexpected{ProjectError{
            ProjectErrorCode::InvalidProjectDocument, "Unsupported project.json formatVersion"
        }};
    }

    return std::expected<void, ProjectError>{};
}

// Writes project.json into the extracted project workspace root.
std::expected<void, ProjectError> writeProjectDocument(
    const std::filesystem::path& workspace_directory)
{
    const juce::var project_document = Json::makeObject({
        {"formatVersion", juce::var{1}},
    });

    // Binary, so the document is the bytes written on every platform rather than gaining CRLF here
    // while song.json beside it gets LF.
    std::ofstream project_document_file{
        workspace_directory / g_project_document_name, std::ios::binary
    };
    if (!project_document_file.is_open())
    {
        return std::unexpected{ProjectError{
            ProjectErrorCode::CouldNotWriteProjectFiles, "Could not write project.json"
        }};
    }

    project_document_file << juce::JSON::toString(project_document).toStdString() << '\n';
    // Closed BEFORE the check, because the destructor's implicit flush would otherwise run after it
    // — and this document is small enough to sit entirely in the buffer, so a failing flush would
    // be reported as success. The package writer beside it already closes for exactly this reason. A
    // false success here is not a lost save but an unloadable one: the archive packages an empty
    // project.json, the dirty flag clears, and reopening reports a malformed project.
    project_document_file.close();
    if (!project_document_file.good())
    {
        return std::unexpected{ProjectError{
            ProjectErrorCode::CouldNotWriteProjectFiles, "Could not write project.json"
        }};
    }

    return std::expected<void, ProjectError>{};
}

// Writes project.json and song/song.json into the extracted workspace.
std::expected<void, ProjectError> writeProjectFiles(
    const std::filesystem::path& workspace_directory, const common::core::Song& song)
{
    const std::filesystem::path song_directory = workspace_directory / g_song_directory_name;
    if (const auto song_files = common::core::writeRockSongPackageDirectory(song_directory, song);
        !song_files.has_value())
    {
        return std::unexpected{ProjectError{
            ProjectErrorCode::CouldNotWriteProjectFiles,
            song_files.error().message,
        }};
    }

    if (auto write_error = writeProjectDocument(workspace_directory); !write_error.has_value())
    {
        return std::unexpected{std::move(write_error.error())};
    }

    return std::expected<void, ProjectError>{};
}

} // namespace rock_hero::editor::core::project_io
