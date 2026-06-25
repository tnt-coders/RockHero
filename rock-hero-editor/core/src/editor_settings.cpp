#include "editor_settings.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <rock_hero/common/core/application_identity.h>
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
constexpr int g_settings_xml_format_version{1};
constexpr const char* g_format_version_property{"formatVersion"};
constexpr const char* g_project_cursor_positions_key{"projectCursorPositions"};
constexpr const char* g_project_cursor_positions_tag{"PROJECT_CURSOR_POSITIONS"};
constexpr const char* g_project_cursor_position_tag{"POSITION"};
constexpr const char* g_cursor_project_file_property{"projectFile"};
constexpr const char* g_cursor_position_property{"cursorPosition"};
constexpr const char* g_input_calibration_states_key{"inputCalibrationStates"};
constexpr const char* g_input_calibrations_tag{"INPUT_CALIBRATIONS"};
constexpr const char* g_input_calibration_tag{"CALIBRATION"};
constexpr const char* g_calibration_gain_db_property{"gainDb"};
constexpr const char* g_calibration_backend_name_property{"backendName"};
constexpr const char* g_calibration_input_device_name_property{"inputDeviceName"};
constexpr const char* g_calibration_input_channel_index_property{"inputChannelIndex"};
constexpr const char* g_calibration_input_channel_name_property{"inputChannelName"};

struct ProjectCursorState
{
    std::filesystem::path project_file;
    common::core::TimePosition cursor_position{};
};

struct ProjectCursorHistory
{
    std::vector<ProjectCursorState> states;
    bool malformed_xml{false};
};

struct InputCalibrationHistory
{
    std::vector<common::audio::InputCalibrationState> states;
    bool malformed_xml{false};
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

// Reads an XML attribute as an integer without accepting JUCE's permissive partial parsing.
[[nodiscard]] std::optional<int> parseIntAttribute(
    const juce::XmlElement& element, const char* attribute_name)
{
    if (!element.hasAttribute(attribute_name))
    {
        return std::nullopt;
    }

    const std::string text = element.getStringAttribute(attribute_name).toStdString();
    if (text.empty())
    {
        return std::nullopt;
    }

    int value{};
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const auto [parsed_to, error] = std::from_chars(begin, end, value);
    if (error != std::errc{} || parsed_to != end)
    {
        return std::nullopt;
    }

    return value;
}

// Reads an XML attribute as a finite double without accepting malformed numeric text.
[[nodiscard]] std::optional<double> parseDoubleAttribute(
    const juce::XmlElement& element, const char* attribute_name)
{
    if (!element.hasAttribute(attribute_name))
    {
        return std::nullopt;
    }

    const std::string text = element.getStringAttribute(attribute_name).toStdString();
    if (text.empty())
    {
        return std::nullopt;
    }

    double value{};
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const auto [parsed_to, error] = std::from_chars(begin, end, value);
    if (error != std::errc{} || parsed_to != end || !std::isfinite(value))
    {
        return std::nullopt;
    }

    return value;
}

// Reads a required XML string attribute while allowing the caller to validate emptiness.
[[nodiscard]] std::optional<std::string> readStringAttribute(
    const juce::XmlElement& element, const char* attribute_name)
{
    if (!element.hasAttribute(attribute_name))
    {
        return std::nullopt;
    }

    return element.getStringAttribute(attribute_name).toStdString();
}

// Validates the root element shared by app-local XML history values.
[[nodiscard]] bool hasCurrentXmlFormat(const juce::XmlElement& xml, const char* root_tag)
{
    const std::optional<int> format_version = parseIntAttribute(xml, g_format_version_property);
    return xml.hasTagName(root_tag) && format_version.has_value() &&
           *format_version == g_settings_xml_format_version;
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

// Converts one XML element into a validated project cursor record.
[[nodiscard]] std::optional<ProjectCursorState> readProjectCursorStateXml(
    const juce::XmlElement& element)
{
    if (!element.hasTagName(g_project_cursor_position_tag))
    {
        return std::nullopt;
    }

    const std::optional<std::string> project_file =
        readStringAttribute(element, g_cursor_project_file_property);
    const std::optional<double> cursor_position =
        parseDoubleAttribute(element, g_cursor_position_property);
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

// Loads app-local project cursor history from its XML-valued settings property.
[[nodiscard]] ProjectCursorHistory readProjectCursorHistory(const juce::PropertiesFile& properties)
{
    std::unique_ptr<juce::XmlElement> xml = properties.getXmlValue(g_project_cursor_positions_key);
    if (xml == nullptr)
    {
        return properties.containsKey(g_project_cursor_positions_key)
                   ? ProjectCursorHistory{.states = {}, .malformed_xml = true}
                   : ProjectCursorHistory{};
    }

    if (!hasCurrentXmlFormat(*xml, g_project_cursor_positions_tag))
    {
        return ProjectCursorHistory{.states = {}, .malformed_xml = true};
    }

    ProjectCursorHistory history;
    history.states.reserve(static_cast<std::size_t>(xml->getNumChildElements()));
    for (const juce::XmlElement* const item :
         xml->getChildWithTagNameIterator(g_project_cursor_position_tag))
    {
        if (std::optional<ProjectCursorState> state = readProjectCursorStateXml(*item);
            state.has_value())
        {
            replaceProjectCursor(history.states, state->project_file, state->cursor_position);
        }
    }

    return history;
}

// Writes a complete replacement cursor history as one XML-valued settings property.
void writeProjectCursorHistory(
    juce::PropertiesFile& properties, const std::vector<ProjectCursorState>& history)
{
    juce::XmlElement history_xml{g_project_cursor_positions_tag};
    history_xml.setAttribute(g_format_version_property, g_settings_xml_format_version);
    for (const ProjectCursorState& state : history)
    {
        if (!state.project_file.empty() && std::isfinite(state.cursor_position.seconds))
        {
            juce::XmlElement* const item =
                history_xml.createNewChildElement(g_project_cursor_position_tag);
            item->setAttribute(
                g_cursor_project_file_property,
                common::core::juceStringFromPath(state.project_file));
            item->setAttribute(g_cursor_position_property, state.cursor_position.seconds);
        }
    }

    properties.setValue(g_project_cursor_positions_key, &history_xml);
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

// Converts one XML element into a validated calibration record, dropping incomplete entries.
[[nodiscard]] std::optional<common::audio::InputCalibrationState> readCalibrationStateXml(
    const juce::XmlElement& element)
{
    if (!element.hasTagName(g_input_calibration_tag))
    {
        return std::nullopt;
    }

    const std::optional<double> gain_db =
        parseDoubleAttribute(element, g_calibration_gain_db_property);
    const std::optional<std::string> backend_name =
        readStringAttribute(element, g_calibration_backend_name_property);
    const std::optional<std::string> input_device_name =
        readStringAttribute(element, g_calibration_input_device_name_property);
    const std::optional<int> input_channel_index =
        parseIntAttribute(element, g_calibration_input_channel_index_property);
    const std::optional<std::string> input_channel_name =
        readStringAttribute(element, g_calibration_input_channel_name_property);
    if (!gain_db.has_value() || !backend_name.has_value() || !input_device_name.has_value() ||
        !input_channel_index.has_value() || !input_channel_name.has_value() ||
        *input_channel_index < 0)
    {
        return std::nullopt;
    }

    common::audio::InputCalibrationState state{
        .calibration_gain = common::audio::clampGain(common::audio::Gain{*gain_db}),
        .input_device_identity = common::audio::InputDeviceIdentity{
            .backend_name = *backend_name,
            .input_device_name = *input_device_name,
            .input_channel_index = *input_channel_index,
            .input_channel_name = *input_channel_name,
        },
    };
    if (!common::audio::isValidInputDeviceIdentity(state.input_device_identity))
    {
        return std::nullopt;
    }

    return state;
}

// Loads route history from its XML-valued settings property.
[[nodiscard]] InputCalibrationHistory readInputCalibrationHistory(
    const juce::PropertiesFile& properties)
{
    std::unique_ptr<juce::XmlElement> xml = properties.getXmlValue(g_input_calibration_states_key);
    if (xml == nullptr)
    {
        return properties.containsKey(g_input_calibration_states_key)
                   ? InputCalibrationHistory{.states = {}, .malformed_xml = true}
                   : InputCalibrationHistory{};
    }

    if (!hasCurrentXmlFormat(*xml, g_input_calibrations_tag))
    {
        return InputCalibrationHistory{.states = {}, .malformed_xml = true};
    }

    InputCalibrationHistory history;
    history.states.reserve(static_cast<std::size_t>(xml->getNumChildElements()));
    for (const juce::XmlElement* const item :
         xml->getChildWithTagNameIterator(g_input_calibration_tag))
    {
        if (std::optional<common::audio::InputCalibrationState> state =
                readCalibrationStateXml(*item);
            state.has_value())
        {
            replaceRouteCalibration(history.states, std::move(*state));
        }
    }

    return history;
}

// Writes a complete replacement history as one XML-valued settings property.
void writeInputCalibrationHistory(
    juce::PropertiesFile& properties,
    const std::vector<common::audio::InputCalibrationState>& history)
{
    juce::XmlElement history_xml{g_input_calibrations_tag};
    history_xml.setAttribute(g_format_version_property, g_settings_xml_format_version);
    for (const common::audio::InputCalibrationState& state : history)
    {
        if (common::audio::isValidInputDeviceIdentity(state.input_device_identity))
        {
            juce::XmlElement* const item =
                history_xml.createNewChildElement(g_input_calibration_tag);
            item->setAttribute(g_calibration_gain_db_property, state.calibration_gain.db);
            item->setAttribute(
                g_calibration_backend_name_property,
                juce::String::fromUTF8(state.input_device_identity.backend_name.c_str()));
            item->setAttribute(
                g_calibration_input_device_name_property,
                juce::String::fromUTF8(state.input_device_identity.input_device_name.c_str()));
            item->setAttribute(
                g_calibration_input_channel_index_property,
                state.input_device_identity.input_channel_index);
            item->setAttribute(
                g_calibration_input_channel_name_property,
                juce::String::fromUTF8(state.input_device_identity.input_channel_name.c_str()));
        }
    }

    properties.setValue(g_input_calibration_states_key, &history_xml);
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
    if (history.malformed_xml)
    {
        return std::unexpected{EditorSettingsError{
            EditorSettingsErrorCode::InvalidProjectCursorHistory,
            "Saved project cursor history is not valid XML."
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
    if (history.malformed_xml)
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
    if (history.malformed_xml)
    {
        return std::unexpected{EditorSettingsError{
            EditorSettingsErrorCode::InvalidInputCalibrationHistory,
            "Saved input calibration history is not valid XML."
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

// Saves one physical-route calibration in the XML-valued route history.
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
    if (history.malformed_xml)
    {
        return std::unexpected{EditorSettingsError{
            EditorSettingsErrorCode::InvalidInputCalibrationHistory,
            "Cannot save input calibration because saved calibration history is invalid."
        }};
    }

    replaceRouteCalibration(history.states, std::move(calibration_state));
    writeInputCalibrationHistory(m_properties, history.states);
    return saveNow(m_properties, "Could not save input calibration setting.");
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
    if (history.malformed_xml)
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
    }

    return {};
}

} // namespace rock_hero::editor::core
