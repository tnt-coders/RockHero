#include "editor_settings.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <rock_hero/common/core/application_identity.h>
#include <rock_hero/common/core/json.h>
#include <rock_hero/common/core/juce_path.h>
#include <system_error>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

constexpr const char* g_last_open_project_key{"lastOpenProject"};
constexpr const char* g_interrupted_restore_project_key{"interruptedRestoreProject"};
constexpr const char* g_audio_device_state_key{"audioDeviceState"};
constexpr const char* g_project_cursor_positions_json_key{"projectCursorPositionsJson"};
constexpr const char* g_cursor_project_file_property{"projectFile"};
constexpr const char* g_cursor_position_property{"cursorPosition"};
constexpr const char* g_input_calibration_gain_db_key{"inputCalibrationGainDb"};
constexpr const char* g_input_calibration_backend_name_key{"inputCalibrationBackendName"};
constexpr const char* g_input_calibration_input_device_name_key{"inputCalibrationInputDeviceName"};
constexpr const char* g_input_calibration_input_channel_index_key{
    "inputCalibrationInputChannelIndex"
};
constexpr const char* g_input_calibration_input_channel_name_key{
    "inputCalibrationInputChannelName"
};
constexpr const char* g_input_calibration_states_json_key{"inputCalibrationStatesJson"};
constexpr const char* g_calibration_gain_db_property{"gainDb"};
constexpr const char* g_calibration_backend_name_property{"backendName"};
constexpr const char* g_calibration_input_device_name_property{"inputDeviceName"};
constexpr const char* g_calibration_input_channel_index_property{"inputChannelIndex"};
constexpr const char* g_calibration_input_channel_name_property{"inputChannelName"};

using common::core::Json;

struct ProjectCursorState
{
    std::filesystem::path project_file;
    common::core::TimePosition cursor_position{};
};

struct ProjectCursorHistory
{
    std::vector<ProjectCursorState> states;
    bool malformed_json{false};
};

struct InputCalibrationHistory
{
    std::vector<common::audio::InputCalibrationState> states;
    bool malformed_json{false};
};

// Builds the per-user settings file options used by the editor app.
[[nodiscard]] juce::PropertiesFile::Options editorSettingsOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName = juce::String{common::core::editorApplicationName().data()};
    options.filenameSuffix = ".settings";
    options.folderName = juce::String{common::core::applicationDataFolderName().data()};
    options.osxLibrarySubFolder = "Application Support";
    options.commonToAllUsers = false;
    options.ignoreCaseOfKeyNames = false;
    options.doNotSave = false;
    options.millisecondsBeforeSaving = 0;
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    options.processLock = nullptr;
    return options;
}

// Reads the legacy one-record schema without mutating settings so migration can be explicit.
[[nodiscard]] std::optional<common::audio::InputCalibrationState> readLegacyInputCalibration(
    const juce::PropertiesFile& properties)
{
    if (!properties.containsKey(g_input_calibration_gain_db_key) ||
        !properties.containsKey(g_input_calibration_input_channel_index_key))
    {
        return std::nullopt;
    }

    common::audio::InputCalibrationState state{
        .calibration_gain = common::audio::clampGain(
            common::audio::Gain{properties.getDoubleValue(g_input_calibration_gain_db_key)}),
        .input_device_identity = common::audio::InputDeviceIdentity{
            .backend_name = properties.getValue(g_input_calibration_backend_name_key).toStdString(),
            .input_device_name =
                properties.getValue(g_input_calibration_input_device_name_key).toStdString(),
            .input_channel_index =
                properties.getIntValue(g_input_calibration_input_channel_index_key, -1),
            .input_channel_name =
                properties.getValue(g_input_calibration_input_channel_name_key).toStdString(),
        },
    };
    if (!common::audio::isValidInputDeviceIdentity(state.input_device_identity))
    {
        return std::nullopt;
    }

    return state;
}

// Removes the legacy flat schema after a valid route-history write has replaced it.
void removeLegacyInputCalibration(juce::PropertiesFile& properties)
{
    properties.removeValue(g_input_calibration_gain_db_key);
    properties.removeValue(g_input_calibration_backend_name_key);
    properties.removeValue(g_input_calibration_input_device_name_key);
    properties.removeValue(g_input_calibration_input_channel_index_key);
    properties.removeValue(g_input_calibration_input_channel_name_key);
}

// Converts project paths into stable app-settings keys without requiring callers to pre-normalize
// chooser, restore, and test paths.
[[nodiscard]] std::filesystem::path projectCursorKeyFor(const std::filesystem::path& project_file)
{
    if (project_file.empty())
    {
        return {};
    }

    std::error_code error;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(project_file, error);
    if (!error && !canonical.empty())
    {
        return canonical.lexically_normal();
    }

    return project_file.lexically_normal();
}

// Replaces any existing cursor record for a project path with the newest resume position.
void replaceProjectCursor(
    std::vector<ProjectCursorState>& history, const std::filesystem::path& project_file,
    common::core::TimePosition cursor_position)
{
    const std::filesystem::path key = projectCursorKeyFor(project_file);
    if (key.empty() || !std::isfinite(cursor_position.seconds))
    {
        return;
    }

    std::erase_if(history, [&key](const ProjectCursorState& existing) {
        return projectCursorKeyFor(existing.project_file) == key;
    });
    history.push_back(ProjectCursorState{.project_file = key, .cursor_position = cursor_position});
}

// Converts one JSON object into a validated project cursor record.
[[nodiscard]] std::optional<ProjectCursorState> readProjectCursorStateJson(const juce::var& value)
{
    if (!value.isObject())
    {
        return std::nullopt;
    }

    const auto project_file = Json::tryReadString(value, g_cursor_project_file_property);
    const auto cursor_position = Json::tryReadDouble(value, g_cursor_position_property);
    if (!project_file.has_value() || project_file->empty() || !cursor_position.has_value() ||
        !std::isfinite(*cursor_position))
    {
        return std::nullopt;
    }

    std::filesystem::path key = projectCursorKeyFor(
        common::core::pathFromJuceString(juce::String::fromUTF8(project_file->c_str())));
    if (key.empty())
    {
        return std::nullopt;
    }

    return ProjectCursorState{
        .project_file = std::move(key),
        .cursor_position = common::core::TimePosition{*cursor_position},
    };
}

// Loads app-local project cursor history. Malformed top-level JSON is preserved as a failure so
// callers do not overwrite unknown user state with a partial replacement.
[[nodiscard]] ProjectCursorHistory readProjectCursorHistory(const juce::PropertiesFile& properties)
{
    if (!properties.containsKey(g_project_cursor_positions_json_key))
    {
        return {};
    }

    const juce::String json_text = properties.getValue(g_project_cursor_positions_json_key);
    auto parsed = Json::parseDocument(json_text);
    if (!parsed.has_value() || !parsed->isArray())
    {
        return ProjectCursorHistory{.states = {}, .malformed_json = true};
    }

    ProjectCursorHistory history;
    history.states.reserve(static_cast<std::size_t>(parsed->size()));
    const juce::Array<juce::var>* const array = parsed->getArray();
    for (const juce::var& item : *array)
    {
        if (std::optional<ProjectCursorState> state = readProjectCursorStateJson(item);
            state.has_value())
        {
            replaceProjectCursor(history.states, state->project_file, state->cursor_position);
        }
    }

    return history;
}

// Serializes one project cursor record into app-local settings JSON.
[[nodiscard]] juce::var makeProjectCursorStateJson(const ProjectCursorState& state)
{
    return Json::makeObject({
        {g_cursor_project_file_property,
         Json::makeString(common::core::juceStringFromPath(state.project_file).toStdString())},
        {g_cursor_position_property, juce::var{state.cursor_position.seconds}},
    });
}

// Writes a complete replacement cursor history as one structured settings value.
void writeProjectCursorHistory(
    juce::PropertiesFile& properties, const std::vector<ProjectCursorState>& history)
{
    juce::var history_json = Json::makeArray();
    for (const ProjectCursorState& state : history)
    {
        if (!state.project_file.empty() && std::isfinite(state.cursor_position.seconds))
        {
            history_json.append(makeProjectCursorStateJson(state));
        }
    }

    properties.setValue(g_project_cursor_positions_json_key, juce::JSON::toString(history_json));
}

// Replaces any existing record for a route with the newest one so duplicate history cannot make
// lookup ambiguous.
void replaceRouteCalibration(
    std::vector<common::audio::InputCalibrationState>& history,
    common::audio::InputCalibrationState calibration_state)
{
    if (!common::audio::isValidInputDeviceIdentity(calibration_state.input_device_identity))
    {
        return;
    }

    calibration_state.calibration_gain =
        common::audio::clampGain(calibration_state.calibration_gain);
    std::erase_if(
        history, [&calibration_state](const common::audio::InputCalibrationState& existing) {
            return common::audio::samePhysicalInputRoute(
                existing.input_device_identity, calibration_state.input_device_identity);
        });
    history.push_back(std::move(calibration_state));
}

// Converts one JSON object into a validated calibration record, dropping incomplete entries.
[[nodiscard]] std::optional<common::audio::InputCalibrationState> readCalibrationStateJson(
    const juce::var& value)
{
    if (!value.isObject())
    {
        return std::nullopt;
    }

    const auto gain_db = Json::tryReadDouble(value, g_calibration_gain_db_property);
    const auto backend_name = Json::tryReadString(value, g_calibration_backend_name_property);
    const auto input_device_name =
        Json::tryReadString(value, g_calibration_input_device_name_property);
    const auto input_channel_index =
        Json::tryReadInt64(value, g_calibration_input_channel_index_property);
    const auto input_channel_name =
        Json::tryReadString(value, g_calibration_input_channel_name_property);
    if (!gain_db.has_value() || !backend_name.has_value() || !input_device_name.has_value() ||
        !input_channel_index.has_value() || !input_channel_name.has_value() ||
        *input_channel_index < 0 || *input_channel_index > std::numeric_limits<int>::max())
    {
        return std::nullopt;
    }

    common::audio::InputCalibrationState state{
        .calibration_gain = common::audio::clampGain(common::audio::Gain{*gain_db}),
        .input_device_identity = common::audio::InputDeviceIdentity{
            .backend_name = *backend_name,
            .input_device_name = *input_device_name,
            .input_channel_index = static_cast<int>(*input_channel_index),
            .input_channel_name = *input_channel_name,
        },
    };
    if (!common::audio::isValidInputDeviceIdentity(state.input_device_identity))
    {
        return std::nullopt;
    }

    return state;
}

// Loads route history. A malformed JSON history is reported separately so lookup can return empty
// and remove can avoid rewriting unknown user state.
[[nodiscard]] InputCalibrationHistory readInputCalibrationHistory(
    const juce::PropertiesFile& properties)
{
    if (!properties.containsKey(g_input_calibration_states_json_key))
    {
        InputCalibrationHistory history;
        if (std::optional<common::audio::InputCalibrationState> legacy =
                readLegacyInputCalibration(properties);
            legacy.has_value())
        {
            history.states.push_back(*legacy);
        }
        return history;
    }

    const juce::String json_text = properties.getValue(g_input_calibration_states_json_key);
    auto parsed = Json::parseDocument(json_text);
    if (!parsed.has_value() || !parsed->isArray())
    {
        return InputCalibrationHistory{.states = {}, .malformed_json = true};
    }

    InputCalibrationHistory history;
    history.states.reserve(static_cast<std::size_t>(parsed->size()));
    const juce::Array<juce::var>* const array = parsed->getArray();
    for (const juce::var& item : *array)
    {
        if (std::optional<common::audio::InputCalibrationState> state =
                readCalibrationStateJson(item);
            state.has_value())
        {
            replaceRouteCalibration(history.states, std::move(*state));
        }
    }

    return history;
}

// Serializes one physical-route calibration record into the app-local settings JSON schema.
[[nodiscard]] juce::var makeCalibrationStateJson(const common::audio::InputCalibrationState& state)
{
    return Json::makeObject({
        {g_calibration_gain_db_property, juce::var{state.calibration_gain.db}},
        {g_calibration_backend_name_property,
         Json::makeString(state.input_device_identity.backend_name)},
        {g_calibration_input_device_name_property,
         Json::makeString(state.input_device_identity.input_device_name)},
        {g_calibration_input_channel_index_property,
         juce::var{state.input_device_identity.input_channel_index}},
        {g_calibration_input_channel_name_property,
         Json::makeString(state.input_device_identity.input_channel_name)},
    });
}

// Writes a complete replacement history as one structured settings value.
void writeInputCalibrationHistory(
    juce::PropertiesFile& properties,
    const std::vector<common::audio::InputCalibrationState>& history)
{
    juce::var history_json = Json::makeArray();
    for (const common::audio::InputCalibrationState& state : history)
    {
        if (common::audio::isValidInputDeviceIdentity(state.input_device_identity))
        {
            history_json.append(makeCalibrationStateJson(state));
        }
    }

    properties.setValue(g_input_calibration_states_json_key, juce::JSON::toString(history_json));
}

// Saves pending changes and translates JUCE persistence failure into the settings domain.
[[nodiscard]] std::expected<void, EditorSettingsError> saveIfNeeded(
    juce::PropertiesFile& properties, std::string message)
{
    if (properties.saveIfNeeded())
    {
        return {};
    }

    return std::unexpected{
        EditorSettingsError{EditorSettingsErrorCode::CouldNotSave, std::move(message)}
    };
}

// Forces a settings-file write when callers need to know whether a staged update reached disk.
[[nodiscard]] std::expected<void, EditorSettingsError> saveNow(
    juce::PropertiesFile& properties, std::string message)
{
    if (properties.save())
    {
        return {};
    }

    return std::unexpected{
        EditorSettingsError{EditorSettingsErrorCode::CouldNotSave, std::move(message)}
    };
}

} // namespace

// Opens the JUCE properties file that backs app-local editor settings.
EditorSettings::EditorSettings()
    : m_properties(editorSettingsOptions())
{}

// Opens an explicit settings file so lifecycle behavior can be exercised in isolation.
EditorSettings::EditorSettings(const std::filesystem::path& settings_file)
    : m_properties(common::core::juceFileFromPath(settings_file), editorSettingsOptions())
{}

// Reads the last editor project path stored by a previous allowed editor exit.
std::optional<std::filesystem::path> EditorSettings::lastOpenProject() const
{
    const juce::String value = m_properties.getValue(g_last_open_project_key);
    if (value.isEmpty())
    {
        return std::nullopt;
    }

    return common::core::pathFromJuceString(value);
}

// Stores or clears the editor project path to restore on the next editor launch.
std::expected<void, EditorSettingsError> EditorSettings::setLastOpenProject(
    std::optional<std::filesystem::path> project_file)
{
    if (project_file.has_value() && !project_file->empty())
    {
        m_properties.setValue(
            g_last_open_project_key, common::core::juceStringFromPath(*project_file));
    }
    else
    {
        m_properties.removeValue(g_last_open_project_key);
    }

    return saveIfNeeded(m_properties, "Could not save last open project setting.");
}

// Reads the project path whose previous startup restore was interrupted before completion.
std::optional<std::filesystem::path> EditorSettings::interruptedRestoreProject() const
{
    const juce::String value = m_properties.getValue(g_interrupted_restore_project_key);
    if (value.isEmpty())
    {
        return std::nullopt;
    }

    return common::core::pathFromJuceString(value);
}

// Stores or clears the startup-restore interruption marker used to avoid retry loops.
std::expected<void, EditorSettingsError> EditorSettings::setInterruptedRestoreProject(
    std::optional<std::filesystem::path> project_file)
{
    if (project_file.has_value() && !project_file->empty())
    {
        m_properties.setValue(
            g_interrupted_restore_project_key, common::core::juceStringFromPath(*project_file));
    }
    else
    {
        m_properties.removeValue(g_interrupted_restore_project_key);
    }

    return saveIfNeeded(m_properties, "Could not save interrupted project restore setting.");
}

// Reads the opaque serialized audio-device state stored by a previous editor session.
std::optional<std::string> EditorSettings::audioDeviceState() const
{
    const juce::String value = m_properties.getValue(g_audio_device_state_key);
    if (value.isEmpty())
    {
        return std::nullopt;
    }

    return value.toStdString();
}

// Stores or clears the opaque serialized audio-device state.
std::expected<void, EditorSettingsError> EditorSettings::setAudioDeviceState(
    std::optional<std::string> serialized_state)
{
    if (serialized_state.has_value() && !serialized_state->empty())
    {
        m_properties.setValue(g_audio_device_state_key, juce::String{serialized_state->c_str()});
    }
    else
    {
        m_properties.removeValue(g_audio_device_state_key);
    }

    return saveIfNeeded(m_properties, "Could not save audio device state setting.");
}

// Reads the app-local resume cursor associated with one project path.
std::expected<std::optional<common::core::TimePosition>, EditorSettingsError> EditorSettings::
    projectCursorPositionFor(const std::filesystem::path& project_file) const
{
    const std::filesystem::path key = projectCursorKeyFor(project_file);
    if (key.empty())
    {
        return std::nullopt;
    }

    const ProjectCursorHistory history = readProjectCursorHistory(m_properties);
    if (history.malformed_json)
    {
        return std::unexpected{EditorSettingsError{
            EditorSettingsErrorCode::InvalidProjectCursorHistory,
            "Saved project cursor history is not valid JSON."
        }};
    }

    const auto found =
        std::ranges::find_if(history.states, [&key](const ProjectCursorState& state) {
            return projectCursorKeyFor(state.project_file) == key;
        });
    if (found == history.states.end())
    {
        return std::nullopt;
    }

    return found->cursor_position;
}

// Stores one project's app-local resume cursor without treating it as project package data.
std::expected<void, EditorSettingsError> EditorSettings::saveProjectCursorPosition(
    const std::filesystem::path& project_file, common::core::TimePosition cursor_position)
{
    const std::filesystem::path key = projectCursorKeyFor(project_file);
    if (key.empty() || !std::isfinite(cursor_position.seconds))
    {
        return std::unexpected{EditorSettingsError{
            EditorSettingsErrorCode::InvalidSettingValue,
            "Cannot save a project cursor for an invalid project path or position."
        }};
    }

    ProjectCursorHistory history = readProjectCursorHistory(m_properties);
    if (history.malformed_json)
    {
        return std::unexpected{EditorSettingsError{
            EditorSettingsErrorCode::InvalidProjectCursorHistory,
            "Cannot save project cursor because saved cursor history is invalid."
        }};
    }

    replaceProjectCursor(history.states, key, cursor_position);
    writeProjectCursorHistory(m_properties, history.states);
    return saveNow(m_properties, "Could not save project cursor setting.");
}

// Reads the calibration history without mutating settings or compacting invalid persisted records.
std::expected<std::optional<common::audio::InputCalibrationState>, EditorSettingsError>
EditorSettings::inputCalibrationFor(const common::audio::InputDeviceIdentity& identity) const
{
    if (!common::audio::isValidInputDeviceIdentity(identity))
    {
        return std::nullopt;
    }

    const InputCalibrationHistory history = readInputCalibrationHistory(m_properties);
    if (history.malformed_json)
    {
        return std::unexpected{EditorSettingsError{
            EditorSettingsErrorCode::InvalidInputCalibrationHistory,
            "Saved input calibration history is not valid JSON."
        }};
    }

    const auto found = std::ranges::find_if(
        history.states, [&identity](const common::audio::InputCalibrationState& state) {
            return common::audio::inputCalibrationMatchesPhysicalRoute(state, identity);
        });
    if (found == history.states.end())
    {
        return std::nullopt;
    }

    common::audio::InputCalibrationState calibration = *found;
    calibration.input_device_identity = identity;
    return calibration;
}

// Saves one physical-route calibration and migrates any legacy record into the JSON history.
std::expected<void, EditorSettingsError> EditorSettings::saveInputCalibration(
    common::audio::InputCalibrationState calibration_state)
{
    if (!common::audio::isValidInputDeviceIdentity(calibration_state.input_device_identity))
    {
        return std::unexpected{EditorSettingsError{
            EditorSettingsErrorCode::InvalidSettingValue,
            "Cannot save input calibration for an invalid input route."
        }};
    }

    InputCalibrationHistory history = readInputCalibrationHistory(m_properties);
    replaceRouteCalibration(history.states, std::move(calibration_state));
    writeInputCalibrationHistory(m_properties, history.states);
    auto saved = saveNow(m_properties, "Could not save input calibration setting.");
    if (!saved.has_value())
    {
        return std::unexpected{std::move(saved.error())};
    }

    // Only drop the legacy flat schema once the JSON history is the trusted source. If the prior
    // history was malformed it overwrote any parsed routes, so leave legacy keys as a fallback.
    if (!history.malformed_json)
    {
        removeLegacyInputCalibration(m_properties);
        return saveIfNeeded(m_properties, "Could not remove legacy input calibration setting.");
    }

    return {};
}

// Removes one physical-route calibration without touching unrelated saved routes.
std::expected<void, EditorSettingsError> EditorSettings::removeInputCalibration(
    const common::audio::InputDeviceIdentity& identity)
{
    if (!common::audio::isValidInputDeviceIdentity(identity))
    {
        return std::unexpected{EditorSettingsError{
            EditorSettingsErrorCode::InvalidSettingValue,
            "Cannot remove input calibration for an invalid input route."
        }};
    }

    InputCalibrationHistory history = readInputCalibrationHistory(m_properties);
    if (history.malformed_json)
    {
        return std::unexpected{EditorSettingsError{
            EditorSettingsErrorCode::InvalidInputCalibrationHistory,
            "Cannot remove input calibration because saved calibration history is invalid."
        }};
    }

    const std::size_t original_size = history.states.size();
    std::erase_if(history.states, [&identity](const common::audio::InputCalibrationState& state) {
        return common::audio::inputCalibrationMatchesPhysicalRoute(state, identity);
    });
    if (history.states.size() != original_size)
    {
        writeInputCalibrationHistory(m_properties, history.states);
        auto saved = saveNow(m_properties, "Could not save input calibration removal.");
        if (!saved.has_value())
        {
            return std::unexpected{std::move(saved.error())};
        }

        removeLegacyInputCalibration(m_properties);
        return saveIfNeeded(m_properties, "Could not remove legacy input calibration setting.");
    }

    return {};
}

} // namespace rock_hero::editor::core
