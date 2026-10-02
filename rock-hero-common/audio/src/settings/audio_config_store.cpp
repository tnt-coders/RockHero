#include "settings/audio_config_store.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <memory>
#include <ranges>
#include <rock_hero/common/audio/input/input_calibration.h>
#include <rock_hero/common/audio/input/input_device_identity.h>
#include <rock_hero/common/audio/shared/gain.h>
#include <rock_hero/common/core/shared/juce_path.h>
#include <rock_hero/common/core/shared/settings_file_options.h>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace rock_hero::common::audio
{

namespace
{

// The shared audio-config file's name inside the Rock Hero settings folder. The inter-process lock
// that guards the file carries the same name.
constexpr const char* g_audio_config_application_name{"Rock Hero Audio"};

constexpr const char* g_active_device_route_key{"activeDeviceRoute"};

constexpr int g_settings_xml_format_version{1};
constexpr const char* g_format_version_property{"formatVersion"};

// Takes the shared settings-file location policy and names the store's lock as the file's process
// lock, so JUCE's own loads and saves hold it too. JUCE's lock is re-entrant within one process, so
// those nest inside a write that already holds it.
[[nodiscard]] juce::PropertiesFile::Options audioConfigOptions(juce::InterProcessLock& lock)
{
    juce::PropertiesFile::Options options =
        common::core::settingsFileOptions(g_audio_config_application_name);
    options.processLock = &lock;
    return options;
}

// The failure a write reports when the inter-process lock cannot be taken.
[[nodiscard]] std::unexpected<AudioConfigError> couldNotLock()
{
    return std::unexpected{AudioConfigError{
        AudioConfigErrorCode::CouldNotSave, "Could not lock the audio settings file."
    }};
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
        g_identity_backend_name_property, juce::String::fromUTF8(identity.backend_name.c_str()));
    element.setAttribute(
        g_identity_input_device_name_property,
        juce::String::fromUTF8(identity.input_device_name.c_str()));
    element.setAttribute(g_identity_input_channel_index_property, identity.input_channel_index);
}

// Reads the physical-route identity attributes, dropping entries missing any field. The channel
// index must be non-negative to name a real physical channel.
[[nodiscard]] std::optional<InputDeviceIdentity> readIdentity(const juce::XmlElement& element)
{
    const std::optional<std::string> backend_name =
        readStringAttribute(element, g_identity_backend_name_property);
    const std::optional<std::string> input_device_name =
        readStringAttribute(element, g_identity_input_device_name_property);
    const std::optional<int> input_channel_index =
        parseIntAttribute(element, g_identity_input_channel_index_property);
    if (!backend_name.has_value() || !input_device_name.has_value() ||
        !input_channel_index.has_value() || *input_channel_index < 0)
    {
        return std::nullopt;
    }

    return InputDeviceIdentity{
        .backend_name = *backend_name,
        .input_device_name = *input_device_name,
        .input_channel_index = *input_channel_index,
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

    // Every gain takes the calibration normal form on the way into the store, so no persisted or
    // caller value escapes the supported range or the step.
    [[nodiscard]] static State normalized(State state)
    {
        state.calibration_gain = normalizedInputCalibrationGain(state.calibration_gain.db);
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

// Opens the shared file at its standard per-user location.
AudioConfigStore::AudioConfigStore()
    : AudioConfigStore(
          common::core::settingsFileOptions(g_audio_config_application_name).getDefaultFile())
{}

// Opens an explicit store file so lifecycle behavior can be exercised in isolation.
AudioConfigStore::AudioConfigStore(const std::filesystem::path& settings_file)
    : AudioConfigStore(common::core::juceFileFromPath(settings_file))
{}

// Owns the member initialization both public constructors share.
AudioConfigStore::AudioConfigStore(juce::File file)
    : m_lock(g_audio_config_application_name)
    , m_options(audioConfigOptions(m_lock))
    , m_file(std::move(file))
{}

// Reads the stored restore payload; an empty value is no route.
std::optional<std::string> AudioConfigStore::activeDeviceRoute() const
{
    const juce::PropertiesFile properties{m_file, m_options};
    std::string route = properties.getValue(g_active_device_route_key).toStdString();
    if (route.empty())
    {
        return std::nullopt;
    }

    return route;
}

// Stores or clears the restore payload. JUCE owns its format, so it is kept as an opaque string,
// and re-serializes the XML it recognizes: readers parse it, never compare bytes.
std::expected<void, AudioConfigError> AudioConfigStore::setActiveDeviceRoute(
    std::optional<std::string> route)
{
    // Held across the read-modify-write, so the other product cannot write between them.
    const juce::InterProcessLock::ScopedLockType held{m_lock};
    if (!held.isLocked())
    {
        return couldNotLock();
    }
    juce::PropertiesFile properties{m_file, m_options};
    if (!route.has_value() || route->empty())
    {
        properties.removeValue(g_active_device_route_key);
    }
    else
    {
        properties.setValue(g_active_device_route_key, juce::String::fromUTF8(route->c_str()));
    }

    if (properties.save())
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

    const juce::PropertiesFile properties{m_file, m_options};
    auto states = InputCalibrationStore::readOrError(
        properties, "Saved input calibration history is not valid XML.");
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
    if (!isValidInputDeviceIdentity(calibration_state.input_device_identity))
    {
        return std::unexpected{AudioConfigError{
            AudioConfigErrorCode::InvalidSettingValue,
            "Cannot save input calibration for an invalid input route."
        }};
    }

    // Held across the read-modify-write, so the other product cannot write between them.
    const juce::InterProcessLock::ScopedLockType held{m_lock};
    if (!held.isLocked())
    {
        return couldNotLock();
    }
    juce::PropertiesFile properties{m_file, m_options};
    auto states = InputCalibrationStore::readOrError(
        properties, "Cannot save input calibration because saved calibration history is invalid.");
    if (!states.has_value())
    {
        return std::unexpected{std::move(states.error())};
    }

    InputCalibrationStore::replace(*states, std::move(calibration_state));
    InputCalibrationStore::write(properties, *states);
    if (properties.save())
    {
        return {};
    }

    return std::unexpected{
        AudioConfigError{AudioConfigErrorCode::CouldNotSave, "Could not save input calibration."}
    };
}

} // namespace rock_hero::common::audio
