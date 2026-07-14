#include "settings/audio_config_store.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <memory>
#include <ranges>
#include <rock_hero/common/audio/shared/gain.h>
#include <rock_hero/common/core/shared/application_identity.h>
#include <rock_hero/common/core/shared/juce_path.h>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace rock_hero::common::audio
{

namespace
{

constexpr const char* g_active_device_route_key{"activeDeviceRoute"};
constexpr const char* g_active_device_route_tag{"ACTIVE_DEVICE_ROUTE"};
constexpr const char* g_serialized_state_property{"serializedState"};
constexpr const char* g_identity_tag{"IDENTITY"};

constexpr const char* g_backend_name_property{"backendName"};
constexpr const char* g_input_device_name_property{"inputDeviceName"};
constexpr const char* g_input_channel_index_property{"inputChannelIndex"};
constexpr const char* g_input_channel_name_property{"inputChannelName"};

constexpr int g_settings_xml_format_version{1};
constexpr const char* g_format_version_property{"formatVersion"};

// Builds the JUCE properties-file options shared by both store constructors. applicationName is
// empty for the explicit-path constructor because the file is supplied directly and the name only
// matters when JUCE derives the default per-user path. processLock is always null because each app
// owns exactly one writer for its own audio-config file.
[[nodiscard]] juce::PropertiesFile::Options audioConfigOptions(
    const juce::String& application_name, AudioConfigStore::Access access)
{
    juce::PropertiesFile::Options options;
    const std::string_view folder_name = common::core::applicationDataFolderName();
    options.applicationName = application_name;
    options.filenameSuffix = ".settings";
    options.folderName = juce::String{folder_name.data(), folder_name.size()};
    options.osxLibrarySubFolder = "Application Support";
    options.commonToAllUsers = false;
    options.ignoreCaseOfKeyNames = false;
    options.doNotSave = access == AudioConfigStore::Access::ReadOnly;
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

// Writes the physical-route identity attributes shared by calibration records and the active route.
void writeIdentityAttributes(juce::XmlElement& element, const InputDeviceIdentity& identity)
{
    element.setAttribute(
        g_backend_name_property, juce::String::fromUTF8(identity.backend_name.c_str()));
    element.setAttribute(
        g_input_device_name_property, juce::String::fromUTF8(identity.input_device_name.c_str()));
    element.setAttribute(g_input_channel_index_property, identity.input_channel_index);
    element.setAttribute(
        g_input_channel_name_property, juce::String::fromUTF8(identity.input_channel_name.c_str()));
}

// Reads the physical-route identity attributes, dropping entries missing any field. The channel
// index must be non-negative to name a real physical channel.
[[nodiscard]] std::optional<InputDeviceIdentity> readIdentity(const juce::XmlElement& element)
{
    const std::optional<std::string> backend_name =
        readStringAttribute(element, g_backend_name_property);
    const std::optional<std::string> input_device_name =
        readStringAttribute(element, g_input_device_name_property);
    const std::optional<int> input_channel_index =
        parseIntAttribute(element, g_input_channel_index_property);
    const std::optional<std::string> input_channel_name =
        readStringAttribute(element, g_input_channel_name_property);
    if (!backend_name.has_value() || !input_device_name.has_value() ||
        !input_channel_index.has_value() || !input_channel_name.has_value() ||
        *input_channel_index < 0)
    {
        return std::nullopt;
    }

    return InputDeviceIdentity{
        .backend_name = *backend_name,
        .input_device_name = *input_device_name,
        .input_channel_index = *input_channel_index,
        .input_channel_name = *input_channel_name,
    };
}

// One XML-valued settings property holding a list of keyed records. Loads the whole list (deduping
// by key while loading so corrupt duplicates cannot make lookup ambiguous), replaces-by-key on
// save, and writes the whole list back under a format-versioned root, distinguishing a missing
// property from unreadable XML so callers surface corruption instead of silently clobbering it.
// The codec supplies everything family-specific — property/tag names, the malformed-history
// error code, record normalization and validity, key equality, and the attribute conversions.
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
    [[nodiscard]] static std::expected<std::vector<State>, AudioConfigError> readOrError(
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
    [[nodiscard]] static std::unexpected<AudioConfigError> malformedHistory(std::string message)
    {
        return std::unexpected{
            AudioConfigError{Codec::g_malformed_history_code, std::move(message)}
        };
    }
};

// Calibration records remember per-physical-route input gain across device changes.
struct InputCalibrationCodec
{
    using State = InputCalibrationState;

    static constexpr const char* g_list_key{"inputCalibrationStates"};
    static constexpr const char* g_list_tag{"INPUT_CALIBRATIONS"};
    static constexpr const char* g_item_tag{"CALIBRATION"};
    static constexpr const char* g_gain_db_property{"gainDb"};
    static constexpr AudioConfigErrorCode g_malformed_history_code{
        AudioConfigErrorCode::InvalidInputCalibrationHistory
    };

    // Every gain is clamped on the way into the store, so out-of-range persisted or caller
    // values cannot escape the supported calibration range.
    [[nodiscard]] static State normalized(State state)
    {
        state.calibration_gain = clampGain(state.calibration_gain);
        return state;
    }

    // A storable record names a complete physical input route.
    [[nodiscard]] static bool isValid(const State& state)
    {
        return isValidInputDeviceIdentity(state.input_device_identity);
    }

    // Records address the same route when their identities match physically, so renamed channels
    // replace their old records instead of accumulating.
    [[nodiscard]] static bool sameKey(const State& lhs, const State& rhs)
    {
        return samePhysicalInputRoute(lhs.input_device_identity, rhs.input_device_identity);
    }

    // Converts one XML item into a validated record, dropping incomplete entries. The gain is
    // stored raw here; normalized() clamps it when the record enters the store.
    [[nodiscard]] static std::optional<State> fromXml(const juce::XmlElement& element)
    {
        const std::optional<double> gain_db = parseDoubleAttribute(element, g_gain_db_property);
        std::optional<InputDeviceIdentity> identity = readIdentity(element);
        if (!gain_db.has_value() || !identity.has_value())
        {
            return std::nullopt;
        }

        return State{
            .calibration_gain = Gain{*gain_db},
            .input_device_identity = *std::move(identity),
        };
    }

    // Writes one record's attributes onto its XML item.
    static void toXml(juce::XmlElement& element, const State& state)
    {
        element.setAttribute(g_gain_db_property, state.calibration_gain.db);
        writeIdentityAttributes(element, state.input_device_identity);
    }
};

using InputCalibrationStore = KeyedRecordStore<InputCalibrationCodec>;

} // namespace

// Opens the store at the standard per-user location for an application name.
AudioConfigStore::AudioConfigStore(std::string_view application_name, Access access)
    : m_read_only(access == Access::ReadOnly)
    , m_properties(audioConfigOptions(
          juce::String{application_name.data(), application_name.size()}, access))
{}

// Opens an explicit store file so lifecycle behavior can be exercised in isolation.
AudioConfigStore::AudioConfigStore(const std::filesystem::path& settings_file, Access access)
    : m_read_only(access == Access::ReadOnly)
    , m_properties(common::core::juceFileFromPath(settings_file), audioConfigOptions({}, access))
{}

// Reads the paired device blob and resolved identity, treating unreadable state as absence.
std::optional<ActiveDeviceRoute> AudioConfigStore::activeDeviceRoute() const
{
    const std::unique_ptr<juce::XmlElement> xml =
        m_properties.getXmlValue(g_active_device_route_key);
    if (xml == nullptr || !hasCurrentXmlFormat(*xml, g_active_device_route_tag))
    {
        return std::nullopt;
    }

    const std::optional<std::string> serialized_state =
        readStringAttribute(*xml, g_serialized_state_property);
    if (!serialized_state.has_value() || serialized_state->empty())
    {
        return std::nullopt;
    }

    ActiveDeviceRoute route;
    route.serialized_state = *serialized_state;
    if (const juce::XmlElement* const identity_xml = xml->getChildByName(g_identity_tag))
    {
        route.identity = readIdentity(*identity_xml);
    }

    return route;
}

// Stores or clears the paired device blob and resolved identity as one XML-valued property.
std::expected<void, AudioConfigError> AudioConfigStore::setActiveDeviceRoute(
    std::optional<ActiveDeviceRoute> route)
{
    if (m_read_only)
    {
        return std::unexpected{
            AudioConfigError{AudioConfigErrorCode::CouldNotSave, "store opened read-only"}
        };
    }

    if (!route.has_value() || route->serialized_state.empty())
    {
        m_properties.removeValue(g_active_device_route_key);
    }
    else
    {
        juce::XmlElement route_xml{g_active_device_route_tag};
        route_xml.setAttribute(g_format_version_property, g_settings_xml_format_version);
        route_xml.setAttribute(
            g_serialized_state_property, juce::String::fromUTF8(route->serialized_state.c_str()));
        if (route->identity.has_value() && isValidInputDeviceIdentity(*route->identity))
        {
            writeIdentityAttributes(
                *route_xml.createNewChildElement(g_identity_tag), *route->identity);
        }

        m_properties.setValue(g_active_device_route_key, &route_xml);
    }

    if (m_properties.save())
    {
        return {};
    }

    return std::unexpected{
        AudioConfigError{AudioConfigErrorCode::CouldNotSave, "Could not save active device route."}
    };
}

// Reads the calibration history without mutating state or compacting invalid persisted records.
std::expected<std::optional<InputCalibrationState>, AudioConfigError> AudioConfigStore::
    inputCalibrationFor(const InputDeviceIdentity& identity) const
{
    if (!isValidInputDeviceIdentity(identity))
    {
        return std::nullopt;
    }

    auto states = InputCalibrationStore::readOrError(
        m_properties, "Saved input calibration history is not valid XML.");
    if (!states.has_value())
    {
        return std::unexpected{std::move(states.error())};
    }

    const auto found =
        std::ranges::find_if(*states, [&identity](const InputCalibrationState& state) {
            return inputCalibrationMatchesPhysicalRoute(state, identity);
        });
    if (found == states->end())
    {
        return std::nullopt;
    }

    InputCalibrationState calibration = *found;
    calibration.input_device_identity = identity;
    return calibration;
}

// Saves one physical-route calibration in the XML-valued route history.
std::expected<void, AudioConfigError> AudioConfigStore::saveInputCalibration(
    InputCalibrationState calibration_state)
{
    if (m_read_only)
    {
        return std::unexpected{
            AudioConfigError{AudioConfigErrorCode::CouldNotSave, "store opened read-only"}
        };
    }

    if (!isValidInputDeviceIdentity(calibration_state.input_device_identity))
    {
        return std::unexpected{AudioConfigError{
            AudioConfigErrorCode::InvalidSettingValue,
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
    if (m_properties.save())
    {
        return {};
    }

    return std::unexpected{
        AudioConfigError{AudioConfigErrorCode::CouldNotSave, "Could not save input calibration."}
    };
}

// Removes one physical-route calibration without touching unrelated saved routes.
std::expected<void, AudioConfigError> AudioConfigStore::removeInputCalibration(
    const InputDeviceIdentity& identity)
{
    if (m_read_only)
    {
        return std::unexpected{
            AudioConfigError{AudioConfigErrorCode::CouldNotSave, "store opened read-only"}
        };
    }

    if (!isValidInputDeviceIdentity(identity))
    {
        return std::unexpected{AudioConfigError{
            AudioConfigErrorCode::InvalidSettingValue,
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
    std::erase_if(*states, [&identity](const InputCalibrationState& state) {
        return inputCalibrationMatchesPhysicalRoute(state, identity);
    });
    if (states->size() != original_size)
    {
        InputCalibrationStore::write(m_properties, *states);
        if (!m_properties.save())
        {
            return std::unexpected{AudioConfigError{
                AudioConfigErrorCode::CouldNotSave, "Could not save input calibration removal."
            }};
        }
    }

    return {};
}

} // namespace rock_hero::common::audio
