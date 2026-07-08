#include "settings/editor_settings.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <rock_hero/common/core/shared/application_identity.h>
#include <rock_hero/common/core/shared/juce_path.h>
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

// One XML-valued settings property holding a list of keyed records. Project cursors, project
// grid note values, and input calibrations all share this lifecycle: load the whole list (deduping
// by key while loading so corrupt duplicates cannot make lookup ambiguous), replace-by-key on
// save, and write the whole list back under a format-versioned root, distinguishing a missing
// property from unreadable XML so callers surface corruption instead of silently clobbering it.
// The codec supplies everything family-specific — property/tag names, the malformed-history
// error code, record normalization and validity, key equality, and the attribute conversions —
// so a new record family is one codec instead of another copy of these functions.
template <typename Codec> struct KeyedRecordStore
{
    using State = typename Codec::State;

    // Replaces any existing record sharing the new record's key with the newest value, dropping
    // records the codec cannot store.
    static void replace(std::vector<State>& history, State state)
    {
        state = Codec::normalized(std::move(state));
        if (!Codec::isValid(state))
        {
            return;
        }

        std::erase_if(
            history, [&state](const State& existing) { return Codec::sameKey(existing, state); });
        history.push_back(std::move(state));
    }

    // Loads the family's records, or the codec's malformed-history error when the stored value
    // exists but is not valid current-format XML. The message is per-operation so lookups and
    // saves can report what they were attempting.
    [[nodiscard]] static std::expected<std::vector<State>, EditorSettingsError> readOrError(
        const juce::PropertiesFile& properties, std::string malformed_message)
    {
        const std::unique_ptr<juce::XmlElement> xml = properties.getXmlValue(Codec::g_list_key);
        if (xml == nullptr)
        {
            if (properties.containsKey(Codec::g_list_key))
            {
                return malformedHistory(std::move(malformed_message));
            }

            return std::vector<State>{};
        }

        if (!hasCurrentXmlFormat(*xml, Codec::g_list_tag))
        {
            return malformedHistory(std::move(malformed_message));
        }

        std::vector<State> states;
        states.reserve(static_cast<std::size_t>(xml->getNumChildElements()));
        for (const juce::XmlElement* const item :
             xml->getChildWithTagNameIterator(Codec::g_item_tag))
        {
            if (std::optional<State> state = Codec::fromXml(*item); state.has_value())
            {
                replace(states, std::move(*state));
            }
        }

        return states;
    }

    // Writes a complete replacement history as one XML-valued settings property.
    static void write(juce::PropertiesFile& properties, const std::vector<State>& history)
    {
        juce::XmlElement history_xml{Codec::g_list_tag};
        history_xml.setAttribute(g_format_version_property, g_settings_xml_format_version);
        for (const State& state : history)
        {
            if (Codec::isValid(state))
            {
                Codec::toXml(*history_xml.createNewChildElement(Codec::g_item_tag), state);
            }
        }

        properties.setValue(Codec::g_list_key, &history_xml);
    }

private:
    // Builds the family's malformed-history error from the codec's error code.
    [[nodiscard]] static std::unexpected<EditorSettingsError> malformedHistory(std::string message)
    {
        return std::unexpected{
            EditorSettingsError{Codec::g_malformed_history_code, std::move(message)}
        };
    }
};

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

// Parses a stored value as a finite double, rejecting empty, malformed, or non-finite text so a
// corrupt entry reads as absent rather than a bogus number.
[[nodiscard]] std::optional<double> parseFiniteDouble(const juce::String& text)
{
    const std::string value_text = text.toStdString();
    if (value_text.empty())
    {
        return std::nullopt;
    }

    double value{};
    const char* const begin = value_text.data();
    const char* const end = begin + value_text.size();
    const auto [parsed_to, error] = std::from_chars(begin, end, value);
    if (error != std::errc{} || parsed_to != end || !std::isfinite(value))
    {
        return std::nullopt;
    }

    return value;
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

// Calibration records remember per-physical-route input gain across device changes.
struct InputCalibrationCodec
{
    using State = common::audio::InputCalibrationState;

    static constexpr const char* g_list_key{"inputCalibrationStates"};
    static constexpr const char* g_list_tag{"INPUT_CALIBRATIONS"};
    static constexpr const char* g_item_tag{"CALIBRATION"};
    static constexpr const char* g_gain_db_property{"gainDb"};
    static constexpr const char* g_backend_name_property{"backendName"};
    static constexpr const char* g_input_device_name_property{"inputDeviceName"};
    static constexpr const char* g_input_channel_index_property{"inputChannelIndex"};
    static constexpr const char* g_input_channel_name_property{"inputChannelName"};
    static constexpr EditorSettingsErrorCode g_malformed_history_code{
        EditorSettingsErrorCode::InvalidInputCalibrationHistory
    };

    // Every gain is clamped on the way into the store, so out-of-range persisted or caller
    // values cannot escape the supported calibration range.
    [[nodiscard]] static State normalized(State state)
    {
        state.calibration_gain = common::audio::clampGain(state.calibration_gain);
        return state;
    }

    // A storable record names a complete physical input route.
    [[nodiscard]] static bool isValid(const State& state)
    {
        return common::audio::isValidInputDeviceIdentity(state.input_device_identity);
    }

    // Records address the same route when their identities match physically, so renamed
    // channels replace their old records instead of accumulating.
    [[nodiscard]] static bool sameKey(const State& lhs, const State& rhs)
    {
        return common::audio::samePhysicalInputRoute(
            lhs.input_device_identity, rhs.input_device_identity);
    }

    // Converts one XML item into a validated record, dropping incomplete entries. The gain is
    // stored raw here; normalized() clamps it when the record enters the store.
    [[nodiscard]] static std::optional<State> fromXml(const juce::XmlElement& element)
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

        return State{
            .calibration_gain = common::audio::Gain{*gain_db},
            .input_device_identity = common::audio::InputDeviceIdentity{
                .backend_name = *backend_name,
                .input_device_name = *input_device_name,
                .input_channel_index = *input_channel_index,
                .input_channel_name = *input_channel_name,
            },
        };
    }

    // Writes one record's attributes onto its XML item.
    static void toXml(juce::XmlElement& element, const State& state)
    {
        element.setAttribute(g_gain_db_property, state.calibration_gain.db);
        element.setAttribute(
            g_backend_name_property,
            juce::String::fromUTF8(state.input_device_identity.backend_name.c_str()));
        element.setAttribute(
            g_input_device_name_property,
            juce::String::fromUTF8(state.input_device_identity.input_device_name.c_str()));
        element.setAttribute(
            g_input_channel_index_property, state.input_device_identity.input_channel_index);
        element.setAttribute(
            g_input_channel_name_property,
            juce::String::fromUTF8(state.input_device_identity.input_channel_name.c_str()));
    }
};

using InputCalibrationStore = KeyedRecordStore<InputCalibrationCodec>;

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

    return *pixels_per_second;
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

// Reads the calibration history without mutating settings or compacting invalid persisted records.
std::expected<std::optional<common::audio::InputCalibrationState>, EditorSettingsError>
EditorSettings::inputCalibrationFor(const common::audio::InputDeviceIdentity& identity) const
{
    if (!common::audio::isValidInputDeviceIdentity(identity))
    {
        return std::nullopt;
    }

    auto states = InputCalibrationStore::readOrError(
        m_properties, "Saved input calibration history is not valid XML.");
    if (!states.has_value())
    {
        return std::unexpected{std::move(states.error())};
    }

    const auto found = std::ranges::find_if(
        *states, [&identity](const common::audio::InputCalibrationState& state) {
            return common::audio::inputCalibrationMatchesPhysicalRoute(state, identity);
        });
    if (found == states->end())
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

    auto states = InputCalibrationStore::readOrError(
        m_properties,
        "Cannot save input calibration because saved calibration history is invalid.");
    if (!states.has_value())
    {
        return std::unexpected{std::move(states.error())};
    }

    InputCalibrationStore::replace(*states, std::move(calibration_state));
    InputCalibrationStore::write(m_properties, *states);
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

    auto states = InputCalibrationStore::readOrError(
        m_properties,
        "Cannot remove input calibration because saved calibration history is invalid.");
    if (!states.has_value())
    {
        return std::unexpected{std::move(states.error())};
    }

    const std::size_t original_size = states->size();
    std::erase_if(*states, [&identity](const common::audio::InputCalibrationState& state) {
        return common::audio::inputCalibrationMatchesPhysicalRoute(state, identity);
    });
    if (states->size() != original_size)
    {
        InputCalibrationStore::write(m_properties, *states);
        auto saved = saveNow(m_properties, "Could not save input calibration removal.");
        if (!saved.has_value())
        {
            return std::unexpected{std::move(saved.error())};
        }
    }

    return {};
}

} // namespace rock_hero::editor::core
