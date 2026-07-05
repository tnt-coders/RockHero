#include "settings/editor_settings.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <rock_hero/common/core/infrastructure/application_identity.h>
#include <rock_hero/common/core/infrastructure/juce_path.h>
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
constexpr int g_settings_xml_format_version{1};
constexpr const char* g_format_version_property{"formatVersion"};

struct ProjectCursorState
{
    std::filesystem::path project_file;
    common::core::TimePosition cursor_position{};
};

struct ProjectGridNoteValueState
{
    std::filesystem::path project_file;
    common::core::Fraction grid_note_value{1, 4};
};

struct ProjectTimelineZoomState
{
    std::filesystem::path project_file;
    double pixels_per_second{0.0};
};

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

// Cursor records resume one project's transport position across editor sessions.
struct ProjectCursorCodec
{
    using State = ProjectCursorState;

    static constexpr const char* g_list_key{"projectCursorPositions"};
    static constexpr const char* g_list_tag{"PROJECT_CURSOR_POSITIONS"};
    static constexpr const char* g_item_tag{"POSITION"};
    static constexpr const char* g_project_file_property{"projectFile"};
    static constexpr const char* g_position_property{"cursorPosition"};
    static constexpr EditorSettingsErrorCode g_malformed_history_code{
        EditorSettingsErrorCode::InvalidProjectCursorHistory
    };

    // Records arrive with keys already normalized: fromXml and the public API both run paths
    // through projectSettingsKeyFor before the store sees them.
    [[nodiscard]] static State normalized(State state)
    {
        return state;
    }

    // A storable record has a non-empty path key and a finite resume position.
    [[nodiscard]] static bool isValid(const State& state)
    {
        return !state.project_file.empty() && std::isfinite(state.cursor_position.seconds);
    }

    // Records address the same project when their normalized path keys match.
    [[nodiscard]] static bool sameKey(const State& lhs, const State& rhs)
    {
        return projectSettingsKeyFor(lhs.project_file) == projectSettingsKeyFor(rhs.project_file);
    }

    // Converts one XML item into a validated record, dropping incomplete or malformed entries.
    [[nodiscard]] static std::optional<State> fromXml(const juce::XmlElement& element)
    {
        const std::optional<std::string> project_file =
            readStringAttribute(element, g_project_file_property);
        const std::optional<double> cursor_position =
            parseDoubleAttribute(element, g_position_property);
        if (!project_file.has_value() || project_file->empty() || !cursor_position.has_value() ||
            !std::isfinite(*cursor_position))
        {
            return std::nullopt;
        }

        std::filesystem::path key = projectSettingsKeyFor(
            common::core::pathFromJuceString(juce::String::fromUTF8(project_file->c_str())));
        if (key.empty())
        {
            return std::nullopt;
        }

        return State{
            .project_file = std::move(key),
            .cursor_position = common::core::TimePosition{*cursor_position},
        };
    }

    // Writes one record's attributes onto its XML item.
    static void toXml(juce::XmlElement& element, const State& state)
    {
        element.setAttribute(
            g_project_file_property, common::core::juceStringFromPath(state.project_file));
        element.setAttribute(g_position_property, state.cursor_position.seconds);
    }
};

// Grid note-value records resume one project's timeline grid step across editor sessions. The
// property key is deliberately new: records written by the retired beat-unit grid family live
// under "projectGridSpacings" and are ignored rather than reinterpreted in the wrong unit, so
// affected projects reset to the default grid once.
struct ProjectGridNoteValueCodec
{
    using State = ProjectGridNoteValueState;

    static constexpr const char* g_list_key{"projectGridNoteValues"};
    static constexpr const char* g_list_tag{"PROJECT_GRID_NOTE_VALUES"};
    static constexpr const char* g_item_tag{"NOTE_VALUE"};
    static constexpr const char* g_project_file_property{"projectFile"};
    static constexpr const char* g_numerator_property{"noteValueNumerator"};
    static constexpr const char* g_denominator_property{"noteValueDenominator"};
    static constexpr EditorSettingsErrorCode g_malformed_history_code{
        EditorSettingsErrorCode::InvalidProjectGridNoteValueHistory
    };

    // Records arrive with keys already normalized, matching ProjectCursorCodec.
    [[nodiscard]] static State normalized(State state)
    {
        return state;
    }

    // Structural validity only: semantic note-value bounds stay with the controller so this
    // store never silently rewrites persisted values.
    [[nodiscard]] static bool isValid(const State& state)
    {
        return !state.project_file.empty() && state.grid_note_value.numerator >= 1;
    }

    // Records address the same project when their normalized path keys match.
    [[nodiscard]] static bool sameKey(const State& lhs, const State& rhs)
    {
        return projectSettingsKeyFor(lhs.project_file) == projectSettingsKeyFor(rhs.project_file);
    }

    // Converts one XML item into a structurally valid record.
    [[nodiscard]] static std::optional<State> fromXml(const juce::XmlElement& element)
    {
        const std::optional<std::string> project_file =
            readStringAttribute(element, g_project_file_property);
        const std::optional<int> numerator = parseIntAttribute(element, g_numerator_property);
        const std::optional<int> denominator = parseIntAttribute(element, g_denominator_property);
        if (!project_file.has_value() || project_file->empty() || !numerator.has_value() ||
            !denominator.has_value() || *numerator < 1 || *denominator < 1)
        {
            return std::nullopt;
        }

        std::filesystem::path key = projectSettingsKeyFor(
            common::core::pathFromJuceString(juce::String::fromUTF8(project_file->c_str())));
        if (key.empty())
        {
            return std::nullopt;
        }

        return State{
            .project_file = std::move(key),
            .grid_note_value = common::core::Fraction{*numerator, *denominator},
        };
    }

    // Writes one record's attributes onto its XML item.
    static void toXml(juce::XmlElement& element, const State& state)
    {
        element.setAttribute(
            g_project_file_property, common::core::juceStringFromPath(state.project_file));
        element.setAttribute(g_numerator_property, state.grid_note_value.numerator);
        element.setAttribute(g_denominator_property, state.grid_note_value.denominator);
    }
};

// Timeline zoom records resume one project's horizontal timeline scale across sessions.
struct ProjectTimelineZoomCodec
{
    using State = ProjectTimelineZoomState;

    static constexpr const char* g_list_key{"projectTimelineZooms"};
    static constexpr const char* g_list_tag{"PROJECT_TIMELINE_ZOOMS"};
    static constexpr const char* g_item_tag{"ZOOM"};
    static constexpr const char* g_project_file_property{"projectFile"};
    static constexpr const char* g_pixels_per_second_property{"pixelsPerSecond"};
    static constexpr EditorSettingsErrorCode g_malformed_history_code{
        EditorSettingsErrorCode::InvalidProjectTimelineZoomHistory
    };

    // Records arrive with keys already normalized, matching ProjectCursorCodec.
    [[nodiscard]] static State normalized(State state)
    {
        return state;
    }

    // Structural validity only: the view clamps restored zoom to its own timeline bounds, so
    // this store never silently rewrites persisted values.
    [[nodiscard]] static bool isValid(const State& state)
    {
        return !state.project_file.empty() && std::isfinite(state.pixels_per_second) &&
               state.pixels_per_second > 0.0;
    }

    // Records address the same project when their normalized path keys match.
    [[nodiscard]] static bool sameKey(const State& lhs, const State& rhs)
    {
        return projectSettingsKeyFor(lhs.project_file) == projectSettingsKeyFor(rhs.project_file);
    }

    // Converts one XML item into a structurally valid record.
    [[nodiscard]] static std::optional<State> fromXml(const juce::XmlElement& element)
    {
        const std::optional<std::string> project_file =
            readStringAttribute(element, g_project_file_property);
        if (!project_file.has_value() || project_file->empty() ||
            !element.hasAttribute(g_pixels_per_second_property))
        {
            return std::nullopt;
        }

        const double pixels_per_second =
            element.getDoubleAttribute(g_pixels_per_second_property, 0.0);
        if (!std::isfinite(pixels_per_second) || pixels_per_second <= 0.0)
        {
            return std::nullopt;
        }

        std::filesystem::path key = projectSettingsKeyFor(
            common::core::pathFromJuceString(juce::String::fromUTF8(project_file->c_str())));
        if (key.empty())
        {
            return std::nullopt;
        }

        return State{
            .project_file = std::move(key),
            .pixels_per_second = pixels_per_second,
        };
    }

    // Writes one record's attributes onto its XML item.
    static void toXml(juce::XmlElement& element, const State& state)
    {
        element.setAttribute(
            g_project_file_property, common::core::juceStringFromPath(state.project_file));
        element.setAttribute(g_pixels_per_second_property, state.pixels_per_second);
    }
};

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

using ProjectCursorStore = KeyedRecordStore<ProjectCursorCodec>;
using ProjectGridNoteValueStore = KeyedRecordStore<ProjectGridNoteValueCodec>;
using ProjectTimelineZoomStore = KeyedRecordStore<ProjectTimelineZoomCodec>;
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

// Reads the app-local resume cursor associated with one project path.
std::expected<std::optional<common::core::TimePosition>, EditorSettingsError> EditorSettings::
    projectCursorPositionFor(const std::filesystem::path& project_file) const
{
    const std::filesystem::path key = projectSettingsKeyFor(project_file);
    if (key.empty())
    {
        return std::nullopt;
    }

    auto states = ProjectCursorStore::readOrError(
        m_properties, "Saved project cursor history is not valid XML.");
    if (!states.has_value())
    {
        return std::unexpected{std::move(states.error())};
    }

    const auto found = std::ranges::find_if(*states, [&key](const ProjectCursorState& state) {
        return projectSettingsKeyFor(state.project_file) == key;
    });
    if (found == states->end())
    {
        return std::nullopt;
    }

    return found->cursor_position;
}

// Stores one project's app-local resume cursor without treating it as project package data.
std::expected<void, EditorSettingsError> EditorSettings::saveProjectCursorPosition(
    const std::filesystem::path& project_file, common::core::TimePosition cursor_position)
{
    const std::filesystem::path key = projectSettingsKeyFor(project_file);
    if (key.empty() || !std::isfinite(cursor_position.seconds))
    {
        return std::unexpected{EditorSettingsError{
            EditorSettingsErrorCode::InvalidSettingValue,
            "Cannot save a project cursor for an invalid project path or position."
        }};
    }

    auto states = ProjectCursorStore::readOrError(
        m_properties, "Cannot save project cursor because saved cursor history is invalid.");
    if (!states.has_value())
    {
        return std::unexpected{std::move(states.error())};
    }

    ProjectCursorStore::replace(
        *states, ProjectCursorState{.project_file = key, .cursor_position = cursor_position});
    ProjectCursorStore::write(m_properties, *states);
    return saveNow(m_properties, "Could not save project cursor setting.");
}

// Reads the app-local timeline grid note value associated with one project path.
std::expected<std::optional<common::core::Fraction>, EditorSettingsError> EditorSettings::
    projectGridNoteValueFor(const std::filesystem::path& project_file) const
{
    const std::filesystem::path key = projectSettingsKeyFor(project_file);
    if (key.empty())
    {
        return std::nullopt;
    }

    auto states = ProjectGridNoteValueStore::readOrError(
        m_properties, "Saved project grid note-value history is not valid XML.");
    if (!states.has_value())
    {
        return std::unexpected{std::move(states.error())};
    }

    const auto found =
        std::ranges::find_if(*states, [&key](const ProjectGridNoteValueState& state) {
            return projectSettingsKeyFor(state.project_file) == key;
        });
    if (found == states->end())
    {
        return std::nullopt;
    }

    return found->grid_note_value;
}

// Stores one project's app-local grid note value without treating it as project package data.
std::expected<void, EditorSettingsError> EditorSettings::saveProjectGridNoteValue(
    const std::filesystem::path& project_file, common::core::Fraction grid_note_value)
{
    const std::filesystem::path key = projectSettingsKeyFor(project_file);
    if (key.empty() || grid_note_value.numerator < 1)
    {
        return std::unexpected{EditorSettingsError{
            EditorSettingsErrorCode::InvalidSettingValue,
            "Cannot save a grid note value for an invalid project path or note value."
        }};
    }

    auto states = ProjectGridNoteValueStore::readOrError(
        m_properties,
        "Cannot save grid note value because saved grid note-value history is invalid.");
    if (!states.has_value())
    {
        return std::unexpected{std::move(states.error())};
    }

    ProjectGridNoteValueStore::replace(
        *states,
        ProjectGridNoteValueState{.project_file = key, .grid_note_value = grid_note_value});
    ProjectGridNoteValueStore::write(m_properties, *states);
    return saveNow(m_properties, "Could not save project grid note value setting.");
}

// Reads the app-local timeline zoom associated with one project path.
std::expected<std::optional<double>, EditorSettingsError> EditorSettings::projectTimelineZoomFor(
    const std::filesystem::path& project_file) const
{
    const std::filesystem::path key = projectSettingsKeyFor(project_file);
    if (key.empty())
    {
        return std::nullopt;
    }

    auto states = ProjectTimelineZoomStore::readOrError(
        m_properties, "Saved project timeline zoom history is not valid XML.");
    if (!states.has_value())
    {
        return std::unexpected{std::move(states.error())};
    }

    const auto found = std::ranges::find_if(*states, [&key](const ProjectTimelineZoomState& state) {
        return projectSettingsKeyFor(state.project_file) == key;
    });
    if (found == states->end())
    {
        return std::nullopt;
    }

    return found->pixels_per_second;
}

// Stores one project's app-local timeline zoom without treating it as project package data.
std::expected<void, EditorSettingsError> EditorSettings::saveProjectTimelineZoom(
    const std::filesystem::path& project_file, double pixels_per_second)
{
    const std::filesystem::path key = projectSettingsKeyFor(project_file);
    if (key.empty() || !std::isfinite(pixels_per_second) || pixels_per_second <= 0.0)
    {
        return std::unexpected{EditorSettingsError{
            EditorSettingsErrorCode::InvalidSettingValue,
            "Cannot save a timeline zoom for an invalid project path or scale."
        }};
    }

    auto states = ProjectTimelineZoomStore::readOrError(
        m_properties, "Cannot save timeline zoom because saved zoom history is invalid.");
    if (!states.has_value())
    {
        return std::unexpected{std::move(states.error())};
    }

    ProjectTimelineZoomStore::replace(
        *states,
        ProjectTimelineZoomState{.project_file = key, .pixels_per_second = pixels_per_second});
    ProjectTimelineZoomStore::write(m_properties, *states);
    return saveNow(m_properties, "Could not save project timeline zoom setting.");
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
