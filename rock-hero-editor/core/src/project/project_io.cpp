#include "project_io.h"

#include <algorithm>
#include <expected>
#include <filesystem>
#include <fstream>
#include <juce_core/juce_core.h>
#include <optional>
#include <rock_hero/common/core/package/rock_song_package.h>
#include <rock_hero/common/core/shared/json.h>
#include <rock_hero/common/core/shared/juce_path.h>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace rock_hero::editor::core::project_io
{

namespace
{
constexpr std::string_view g_project_document_name{"project.json"};

using common::core::Json;

// Reads an optional string property while rejecting non-string values.
[[nodiscard]] std::expected<std::optional<std::string>, ProjectError> readOptionalString(
    const juce::var& object, const char* property_name)
{
    const juce::var& value = Json::value(object, property_name);
    if (value.isVoid() || value.isUndefined())
    {
        return std::optional<std::string>{};
    }

    if (!value.isString())
    {
        return std::unexpected{ProjectError{
            ProjectErrorCode::InvalidProjectDocument,
            std::string{property_name} + " must be a string when present"
        }};
    }

    return std::optional<std::string>{value.toString().toStdString()};
}

// Chooses a selected arrangement ID to persist in project.json.
[[nodiscard]] std::optional<std::string> selectedArrangementForSave(
    const ProjectEditorState& editor_state, const std::vector<std::string>& arrangement_ids)
{
    if (arrangement_ids.empty())
    {
        return std::nullopt;
    }

    if (editor_state.selected_arrangement.has_value() &&
        std::ranges::find(arrangement_ids, *editor_state.selected_arrangement) !=
            arrangement_ids.end())
    {
        return editor_state.selected_arrangement;
    }

    return arrangement_ids.front();
}

} // namespace

// Reads project.json editor state from the extracted editor project root.
std::expected<ProjectEditorState, ProjectError> readProjectDocument(
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

    ProjectEditorState editor_state;
    const juce::var& editor_state_json = Json::value(project_document, "editorState");
    if (editor_state_json.isVoid() || editor_state_json.isUndefined())
    {
        return editor_state;
    }

    if (!editor_state_json.isObject())
    {
        return std::unexpected{ProjectError{
            ProjectErrorCode::InvalidProjectDocument, "project.json editorState must be an object"
        }};
    }

    auto selected_arrangement = readOptionalString(editor_state_json, "selectedArrangement");
    if (!selected_arrangement.has_value())
    {
        return std::unexpected{std::move(selected_arrangement.error())};
    }

    std::optional<std::string> selected_arrangement_value = std::move(*selected_arrangement);
    if (selected_arrangement_value.has_value() && !selected_arrangement_value->empty())
    {
        editor_state.selected_arrangement = std::move(*selected_arrangement_value);
    }

    return editor_state;
}

// Writes project.json into the extracted project workspace root.
std::expected<void, ProjectError> writeProjectDocument(
    const std::filesystem::path& workspace_directory, const ProjectEditorState& editor_state,
    const std::vector<std::string>& arrangement_ids)
{
    juce::var editor_state_json = Json::makeObject({});

    const auto selected_arrangement = selectedArrangementForSave(editor_state, arrangement_ids);
    if (selected_arrangement.has_value())
    {
        editor_state_json.getDynamicObject()->setProperty(
            Json::identifier("selectedArrangement"), Json::makeString(*selected_arrangement));
    }

    const juce::var project_document = Json::makeObject({
        {"formatVersion", juce::var{1}},
        {"editorState", editor_state_json},
    });

    std::ofstream project_document_file{workspace_directory / g_project_document_name};
    if (!project_document_file.is_open())
    {
        return std::unexpected{ProjectError{
            ProjectErrorCode::CouldNotWriteProjectFiles, "Could not write project.json"
        }};
    }

    project_document_file << juce::JSON::toString(project_document).toStdString() << '\n';
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
    const std::filesystem::path& workspace_directory, const common::core::Song& song,
    const ProjectEditorState& editor_state)
{
    const std::filesystem::path song_directory = workspace_directory / g_song_directory_name;
    const auto song_files = common::core::writeRockSongPackageDirectory(song_directory, song);
    if (!song_files.has_value())
    {
        return std::unexpected{ProjectError{
            ProjectErrorCode::CouldNotWriteProjectFiles,
            song_files.error().message,
        }};
    }

    if (auto write_error = writeProjectDocument(workspace_directory, editor_state, *song_files);
        !write_error.has_value())
    {
        return std::unexpected{std::move(write_error.error())};
    }

    return std::expected<void, ProjectError>{};
}

} // namespace rock_hero::editor::core::project_io
