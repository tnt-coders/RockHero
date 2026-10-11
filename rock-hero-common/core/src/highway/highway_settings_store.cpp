#include "highway/highway_settings_store.h"

#include <juce_data_structures/juce_data_structures.h>
#include <optional>
#include <string>

namespace rock_hero::common::core
{

namespace
{

// Every key the store owns. The shared "highway." prefix is the whole defense against another
// area's keys in the same file.
constexpr const char* g_theme_key{"highway.theme"};
constexpr const char* g_mirrored_key{"highway.mirrored"};
constexpr const char* g_invert_string_order_key{"highway.invertStringOrder"};

// JUCE stores a bool as "1" or "0"; anything else is unreadable and reads as the default.
[[nodiscard]] bool readFlag(const juce::PropertiesFile& properties, const char* key, bool fallback)
{
    const juce::String text = properties.getValue(key);
    if (text == "1")
    {
        return true;
    }
    if (text == "0")
    {
        return false;
    }
    return fallback;
}

// Reads every setting from one opened copy of the file, each falling back on its own.
[[nodiscard]] HighwayPlayerSettings readFrom(const juce::PropertiesFile& properties)
{
    const HighwayViewPreferences defaults{};
    HighwayPlayerSettings settings{
        .theme_id = std::nullopt,
        .view = HighwayViewPreferences{
            .mirrored = readFlag(properties, g_mirrored_key, defaults.mirrored),
            .invert_string_order =
                readFlag(properties, g_invert_string_order_key, defaults.invert_string_order),
        },
    };
    if (properties.containsKey(g_theme_key))
    {
        settings.theme_id = properties.getValue(g_theme_key).toStdString();
    }
    return settings;
}

// Writes every setting into one opened copy of the file; no pick removes the theme key.
void writeTo(juce::PropertiesFile& properties, const HighwayPlayerSettings& settings)
{
    if (settings.theme_id.has_value())
    {
        properties.setValue(g_theme_key, juce::String::fromUTF8(settings.theme_id->c_str()));
    }
    else
    {
        properties.removeValue(g_theme_key);
    }
    properties.setValue(g_mirrored_key, settings.view.mirrored);
    properties.setValue(g_invert_string_order_key, settings.view.invert_string_order);
}

} // namespace

HighwaySettingsStore::HighwaySettingsStore(SharedSettingsFile& settings_file)
    : m_settings_file(settings_file)
{}

HighwayPlayerSettings HighwaySettingsStore::read() const
{
    const juce::PropertiesFile properties = m_settings_file.open();
    return readFrom(properties);
}

std::expected<HighwayPlayerSettings, HighwaySettingsError> HighwaySettingsStore::update(
    const std::function<void(HighwayPlayerSettings&)>& change)
{
    return m_settings_file
        .whileLocked(
            [&change](juce::PropertiesFile& properties)
                -> std::expected<HighwayPlayerSettings, HighwaySettingsError> {
                HighwayPlayerSettings settings = readFrom(properties);
                change(settings);
                writeTo(properties, settings);
                if (properties.save())
                {
                    return settings;
                }
                return std::unexpected{HighwaySettingsError{
                    HighwaySettingsErrorCode::CouldNotSave, "Could not save the highway settings."
                }};
            })
        .value_or(
            std::unexpected{HighwaySettingsError{
                HighwaySettingsErrorCode::CouldNotSave, "Could not lock the shared settings file."
            }});
}

} // namespace rock_hero::common::core
