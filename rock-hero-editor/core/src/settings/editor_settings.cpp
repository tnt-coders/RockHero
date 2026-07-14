#include "settings/editor_settings.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <rock_hero/common/audio/settings/audio_config_error.h>
#include <rock_hero/common/audio/settings/audio_config_identity.h>
#include <rock_hero/common/core/shared/application_identity.h>
#include <rock_hero/common/core/shared/juce_path.h>
#include <string>
#include <string_view>
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
constexpr const char* g_waveform_visible_key{"waveformVisible"};
constexpr const char* g_tab_minimum_displayed_strings_key{"tabMinimumDisplayedStrings"};
constexpr int g_settings_xml_format_version{1};
constexpr const char* g_format_version_property{"formatVersion"};

// Builds the per-user settings file options used by the editor app.
[[nodiscard]] juce::PropertiesFile::Options editorSettingsOptions()
{
    juce::PropertiesFile::Options options;
    const std::string_view application_name = common::core::editorApplicationName();
    const std::string_view folder_name = common::core::applicationDataFolderName();
    options.applicationName = juce::String{application_name.data(), application_name.size()};
    options.filenameSuffix = ".settings";
    options.folderName = juce::String{folder_name.data(), folder_name.size()};
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

// Parses text as a finite double, rejecting empty, malformed, or non-finite input so a corrupt
// entry reads as absent rather than a bogus number. juce::CharacterFunctions rather than
// std::from_chars: Apple's libc++ ships no floating-point from_chars overloads, and the JUCE
// parser is equally locale-independent; the advanced cursor gives the same whole-string
// strictness check.
[[nodiscard]] std::optional<double> parseFiniteDouble(const juce::String& text)
{
    const juce::String trimmed = text.trim();
    if (trimmed.isEmpty())
    {
        return std::nullopt;
    }

    juce::String::CharPointerType cursor = trimmed.getCharPointer();
    const double value = juce::CharacterFunctions::readDoubleValue(cursor);
    if (!cursor.isEmpty() || !std::isfinite(value))
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

    return parseFiniteDouble(element.getStringAttribute(attribute_name));
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
// chooser, restore, and test paths. Shared by every per-project record family.
[[nodiscard]] std::filesystem::path projectSettingsKeyFor(const std::filesystem::path& project_file)
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

// One flat settings property per (family, project): key = "<family>:" + normalized project path,
// value = one string. Each project's value is independent, so a corrupt or missing entry resets
// only that value rather than a shared list. Family names double as the stored key prefix.
constexpr std::string_view g_project_cursor_family{"projectCursor"};
constexpr std::string_view g_project_grid_note_value_family{"projectGridNoteValue"};
constexpr std::string_view g_project_timeline_zoom_family{"projectTimelineZoom"};
constexpr std::string_view g_project_selected_arrangement_family{"projectSelectedArrangement"};

// Builds the flat settings key holding one family's value for one project path. Paths run through
// projectSettingsKeyFor so chooser, restore, and test spellings of the same .rhp resolve to one
// record; an empty project path yields an empty key so pathless writes never share a record.
[[nodiscard]] juce::String projectSettingKey(
    std::string_view family, const std::filesystem::path& project_file)
{
    const std::filesystem::path key = projectSettingsKeyFor(project_file);
    if (key.empty())
    {
        return {};
    }

    return juce::String::fromUTF8(family.data(), static_cast<int>(family.size())) + ":" +
           common::core::juceStringFromPath(key);
}

// Parses a stored "numerator/denominator" token as a grid note value, rejecting malformed text or
// non-positive terms so a corrupt entry reads as absent.
[[nodiscard]] std::optional<common::core::Fraction> parseGridNoteValue(const juce::String& text)
{
    const std::string token = text.toStdString();
    const std::size_t slash = token.find('/');
    if (slash == std::string::npos)
    {
        return std::nullopt;
    }

    const auto parse_positive_int = [](std::string_view part) -> std::optional<int> {
        if (part.empty())
        {
            return std::nullopt;
        }
        int value{};
        const char* const begin = part.data();
        const char* const end = begin + part.size();
        const auto [parsed_to, error] = std::from_chars(begin, end, value);
        if (error != std::errc{} || parsed_to != end || value < 1)
        {
            return std::nullopt;
        }
        return value;
    };

    const std::optional<int> numerator =
        parse_positive_int(std::string_view{token}.substr(0, slash));
    const std::optional<int> denominator =
        parse_positive_int(std::string_view{token}.substr(slash + 1));
    if (!numerator.has_value() || !denominator.has_value())
    {
        return std::nullopt;
    }

    return common::core::Fraction{*numerator, *denominator};
}

// Legacy calibration XML names shared with the pre-migration on-disk schema. The active calibration
// codec now lives in AudioConfigStore; only this read-side view survives so the one-shot migration
// can decode the obsolete history and re-save it through the store. Removed with the calibration
// methods by plan 14 P3.
constexpr const char* g_input_calibration_states_key{"inputCalibrationStates"};
constexpr const char* g_input_calibrations_tag{"INPUT_CALIBRATIONS"};
constexpr const char* g_input_calibration_item_tag{"CALIBRATION"};
constexpr const char* g_gain_db_property{"gainDb"};
constexpr const char* g_backend_name_property{"backendName"};
constexpr const char* g_input_device_name_property{"inputDeviceName"};
constexpr const char* g_input_channel_index_property{"inputChannelIndex"};
constexpr const char* g_input_channel_name_property{"inputChannelName"};

// Decodes one legacy calibration XML item into a record, dropping incomplete entries. Gain is left
// raw; AudioConfigStore::saveInputCalibration clamps it when the record is re-saved.
[[nodiscard]] std::optional<common::audio::InputCalibrationState> legacyCalibrationFromXml(
    const juce::XmlElement& element)
{
    const std::optional<double> gain_db = parseDoubleAttribute(element, g_gain_db_property);
    const std::optional<std::string> backend_name =
        readStringAttribute(element, g_backend_name_property);
    const std::optional<std::string> input_device_name =
        readStringAttribute(element, g_input_device_name_property);
    const std::optional<int> input_channel_index =
        parseIntAttribute(element, g_input_channel_index_property);
    const std::optional<std::string> input_channel_name =
        readStringAttribute(element, g_input_channel_name_property);
    if (!gain_db.has_value() || !backend_name.has_value() || !input_device_name.has_value() ||
        !input_channel_index.has_value() || !input_channel_name.has_value() ||
        *input_channel_index < 0)
    {
        return std::nullopt;
    }

    return common::audio::InputCalibrationState{
        .calibration_gain = common::audio::Gain{*gain_db},
        .input_device_identity = common::audio::InputDeviceIdentity{
            .backend_name = *backend_name,
            .input_device_name = *input_device_name,
            .input_channel_index = *input_channel_index,
            .input_channel_name = *input_channel_name,
        },
    };
}

// Maps a store error code onto the matching settings error code so the delegated calibration
// accessors keep the IEditorSettings error type at their boundary. The two enums are the same three
// members in the same order; the translation carries the diagnostic message through unchanged.
[[nodiscard]] EditorSettingsError toEditorSettingsError(common::audio::AudioConfigError error)
{
    EditorSettingsErrorCode code = EditorSettingsErrorCode::CouldNotSave;
    switch (error.code)
    {
        case common::audio::AudioConfigErrorCode::InvalidSettingValue:
            code = EditorSettingsErrorCode::InvalidSettingValue;
            break;
        case common::audio::AudioConfigErrorCode::InvalidInputCalibrationHistory:
            code = EditorSettingsErrorCode::InvalidInputCalibrationHistory;
            break;
        case common::audio::AudioConfigErrorCode::CouldNotSave:
            code = EditorSettingsErrorCode::CouldNotSave;
            break;
    }

    return EditorSettingsError{code, std::move(error.message)};
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

// Opens the JUCE properties file plus the owned per-app audio-config store. The store uses the
// editor audio-config application name so it partitions from the workflow-state file.
EditorSettings::EditorSettings()
    : m_properties(editorSettingsOptions())
    , m_audio_config_store(
          common::audio::editorAudioConfigApplicationName(),
          common::audio::AudioConfigStore::Access::ReadWrite)
{}

// Opens an explicit settings file so lifecycle behavior can be exercised in isolation. The owned
// store opens at a sibling path so the two files never share a writer.
EditorSettings::EditorSettings(const std::filesystem::path& settings_file)
    : m_properties(common::core::juceFileFromPath(settings_file), editorSettingsOptions())
    , m_audio_config_store(
          audioConfigFileFor(settings_file), common::audio::AudioConfigStore::Access::ReadWrite)
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

// Reads the app-wide waveform visibility preference for the timeline's tablature lane.
std::optional<bool> EditorSettings::waveformVisible() const
{
    if (!m_properties.containsKey(g_waveform_visible_key))
    {
        return std::nullopt;
    }

    return m_properties.getBoolValue(g_waveform_visible_key);
}

// Stores the app-wide waveform visibility preference.
std::expected<void, EditorSettingsError> EditorSettings::setWaveformVisible(bool visible)
{
    m_properties.setValue(g_waveform_visible_key, visible);
    return saveIfNeeded(m_properties, "Could not save waveform visibility setting.");
}

// Reads the app-wide minimum number of tablature string lanes to display.
std::optional<int> EditorSettings::tabMinimumDisplayedStrings() const
{
    if (!m_properties.containsKey(g_tab_minimum_displayed_strings_key))
    {
        return std::nullopt;
    }

    return m_properties.getIntValue(g_tab_minimum_displayed_strings_key);
}

// Stores the app-wide minimum number of tablature string lanes to display.
std::expected<void, EditorSettingsError> EditorSettings::setTabMinimumDisplayedStrings(
    int minimum_strings)
{
    if (minimum_strings < 0)
    {
        return std::unexpected{EditorSettingsError{
            EditorSettingsErrorCode::InvalidSettingValue,
            "Cannot save a negative tablature string display minimum."
        }};
    }

    m_properties.setValue(g_tab_minimum_displayed_strings_key, minimum_strings);
    return saveIfNeeded(m_properties, "Could not save tablature string display setting.");
}

// Reads the app-local resume cursor associated with one project path. A missing or unparseable
// entry reads as absent; the flat key isolates it from every other project's value.
std::optional<common::core::TimePosition> EditorSettings::projectCursorPositionFor(
    const std::filesystem::path& project_file) const
{
    const juce::String key = projectSettingKey(g_project_cursor_family, project_file);
    if (key.isEmpty() || !m_properties.containsKey(key))
    {
        return std::nullopt;
    }

    const std::optional<double> seconds = parseFiniteDouble(m_properties.getValue(key));
    if (!seconds.has_value())
    {
        return std::nullopt;
    }

    return common::core::TimePosition{*seconds};
}

// Stores one project's app-local resume cursor under its own flat key.
std::expected<void, EditorSettingsError> EditorSettings::saveProjectCursorPosition(
    const std::filesystem::path& project_file, common::core::TimePosition cursor_position)
{
    const juce::String key = projectSettingKey(g_project_cursor_family, project_file);
    if (key.isEmpty() || !std::isfinite(cursor_position.seconds))
    {
        return std::unexpected{EditorSettingsError{
            EditorSettingsErrorCode::InvalidSettingValue,
            "Cannot save a project cursor for an invalid project path or position."
        }};
    }

    m_properties.setValue(key, juce::String(cursor_position.seconds));
    return saveNow(m_properties, "Could not save project cursor setting.");
}

// Reads the app-local timeline grid note value associated with one project path.
std::optional<common::core::Fraction> EditorSettings::projectGridNoteValueFor(
    const std::filesystem::path& project_file) const
{
    const juce::String key = projectSettingKey(g_project_grid_note_value_family, project_file);
    if (key.isEmpty() || !m_properties.containsKey(key))
    {
        return std::nullopt;
    }

    return parseGridNoteValue(m_properties.getValue(key));
}

// Stores one project's app-local grid note value under its own flat key.
std::expected<void, EditorSettingsError> EditorSettings::saveProjectGridNoteValue(
    const std::filesystem::path& project_file, common::core::Fraction grid_note_value)
{
    const juce::String key = projectSettingKey(g_project_grid_note_value_family, project_file);
    if (key.isEmpty() || grid_note_value.numerator < 1 || grid_note_value.denominator < 1)
    {
        return std::unexpected{EditorSettingsError{
            EditorSettingsErrorCode::InvalidSettingValue,
            "Cannot save a grid note value for an invalid project path or note value."
        }};
    }

    m_properties.setValue(
        key,
        juce::String(grid_note_value.numerator) + "/" + juce::String(grid_note_value.denominator));
    return saveNow(m_properties, "Could not save project grid note value setting.");
}

// Reads the app-local timeline zoom associated with one project path.
std::optional<double> EditorSettings::projectTimelineZoomFor(
    const std::filesystem::path& project_file) const
{
    const juce::String key = projectSettingKey(g_project_timeline_zoom_family, project_file);
    if (key.isEmpty() || !m_properties.containsKey(key))
    {
        return std::nullopt;
    }

    const std::optional<double> pixels_per_second = parseFiniteDouble(m_properties.getValue(key));
    if (!pixels_per_second.has_value() || *pixels_per_second <= 0.0)
    {
        return std::nullopt;
    }

    return pixels_per_second;
}

// Stores one project's app-local timeline zoom under its own flat key.
std::expected<void, EditorSettingsError> EditorSettings::saveProjectTimelineZoom(
    const std::filesystem::path& project_file, double pixels_per_second)
{
    const juce::String key = projectSettingKey(g_project_timeline_zoom_family, project_file);
    if (key.isEmpty() || !std::isfinite(pixels_per_second) || pixels_per_second <= 0.0)
    {
        return std::unexpected{EditorSettingsError{
            EditorSettingsErrorCode::InvalidSettingValue,
            "Cannot save a timeline zoom for an invalid project path or scale."
        }};
    }

    m_properties.setValue(key, juce::String(pixels_per_second));
    return saveNow(m_properties, "Could not save project timeline zoom setting.");
}

// Reads the app-local displayed-arrangement choice associated with one project path.
std::optional<std::string> EditorSettings::projectSelectedArrangementFor(
    const std::filesystem::path& project_file) const
{
    const juce::String key = projectSettingKey(g_project_selected_arrangement_family, project_file);
    if (key.isEmpty() || !m_properties.containsKey(key))
    {
        return std::nullopt;
    }

    const juce::String value = m_properties.getValue(key);
    if (value.isEmpty())
    {
        return std::nullopt;
    }

    return value.toStdString();
}

// Stores one project's app-local displayed arrangement under its own flat key.
std::expected<void, EditorSettingsError> EditorSettings::saveProjectSelectedArrangement(
    const std::filesystem::path& project_file, std::string arrangement_id)
{
    const juce::String key = projectSettingKey(g_project_selected_arrangement_family, project_file);
    if (key.isEmpty() || arrangement_id.empty())
    {
        return std::unexpected{EditorSettingsError{
            EditorSettingsErrorCode::InvalidSettingValue,
            "Cannot save a selected arrangement for an invalid project path or empty id."
        }};
    }

    m_properties.setValue(key, juce::String::fromUTF8(arrangement_id.c_str()));
    return saveNow(m_properties, "Could not save project selected-arrangement setting.");
}

// Delegates the calibration lookup to the owned audio-config store, translating the store error
// type back onto the settings error type at the boundary. Removed with the store's calibration
// codec by plan 14 P3, which relocates these call sites into LiveInputMonitor.
std::expected<std::optional<common::audio::InputCalibrationState>, EditorSettingsError>
EditorSettings::inputCalibrationFor(const common::audio::InputDeviceIdentity& identity) const
{
    return m_audio_config_store.inputCalibrationFor(identity).transform_error(
        toEditorSettingsError);
}

// Delegates the calibration save to the owned audio-config store.
std::expected<void, EditorSettingsError> EditorSettings::saveInputCalibration(
    common::audio::InputCalibrationState calibration_state)
{
    return m_audio_config_store.saveInputCalibration(std::move(calibration_state))
        .transform_error(toEditorSettingsError);
}

// Delegates the calibration removal to the owned audio-config store.
std::expected<void, EditorSettingsError> EditorSettings::removeInputCalibration(
    const common::audio::InputDeviceIdentity& identity)
{
    return m_audio_config_store.removeInputCalibration(identity).transform_error(
        toEditorSettingsError);
}

// Exposes the owned store so app composition can inject it into the controller's device-route path.
common::audio::IAudioConfigStore& EditorSettings::audioConfigStore() noexcept
{
    return m_audio_config_store;
}

// Reads the obsolete serialized audio-device state so the one-shot migration can move it.
std::optional<std::string> EditorSettings::readLegacyAudioDeviceState() const
{
    const juce::String value = m_properties.getValue(g_audio_device_state_key);
    if (value.isEmpty())
    {
        return std::nullopt;
    }

    return value.toStdString();
}

// Removes the obsolete serialized audio-device state key after a successful migration.
void EditorSettings::clearLegacyAudioDeviceState()
{
    if (m_properties.containsKey(g_audio_device_state_key))
    {
        m_properties.removeValue(g_audio_device_state_key);
        m_properties.saveIfNeeded();
    }
}

// Decodes the obsolete calibration history so the one-shot migration can re-save each record.
std::vector<common::audio::InputCalibrationState> EditorSettings::readLegacyInputCalibrations()
    const
{
    std::vector<common::audio::InputCalibrationState> records;
    const std::unique_ptr<juce::XmlElement> xml =
        m_properties.getXmlValue(g_input_calibration_states_key);
    if (xml == nullptr || !hasCurrentXmlFormat(*xml, g_input_calibrations_tag))
    {
        return records;
    }

    records.reserve(static_cast<std::size_t>(xml->getNumChildElements()));
    for (const juce::XmlElement* const item :
         xml->getChildWithTagNameIterator(g_input_calibration_item_tag))
    {
        if (std::optional<common::audio::InputCalibrationState> state =
                legacyCalibrationFromXml(*item);
            state.has_value())
        {
            records.push_back(std::move(*state));
        }
    }

    return records;
}

// Removes the obsolete calibration history key after a successful migration.
void EditorSettings::clearLegacyInputCalibrations()
{
    if (m_properties.containsKey(g_input_calibration_states_key))
    {
        m_properties.removeValue(g_input_calibration_states_key);
        m_properties.saveIfNeeded();
    }
}

// Derives the sibling audio-config file for an explicit settings path, mirroring the production
// "Rock Hero Editor" -> "Rock Hero Editor Audio" partition so the two files never share a writer.
std::filesystem::path EditorSettings::audioConfigFileFor(const std::filesystem::path& settings_file)
{
    std::filesystem::path file_name = settings_file.stem();
    file_name += " Audio";
    file_name += settings_file.extension();
    return settings_file.parent_path() / file_name;
}

} // namespace rock_hero::editor::core
