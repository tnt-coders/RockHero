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
constexpr const char* g_pinned_palette_key{"highway.palette"};
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

// Reads an optional text setting, which is absent exactly when its key is.
[[nodiscard]] std::optional<std::string> readText(
    const juce::PropertiesFile& properties, const char* key)
{
    if (!properties.containsKey(key))
    {
        return std::nullopt;
    }
    return properties.getValue(key).toStdString();
}

// Stores an optional text setting, removing its key when it is absent.
void writeText(
    juce::PropertiesFile& properties, const char* key, const std::optional<std::string>& text)
{
    if (text.has_value())
    {
        properties.setValue(key, juce::String::fromUTF8(text->c_str()));
    }
    else
    {
        properties.removeValue(key);
    }
}

// Reads every setting from one opened copy of the file, each falling back on its own.
[[nodiscard]] HighwayPlayerSettings readFrom(const juce::PropertiesFile& properties)
{
    const HighwayViewPreferences defaults{};
    return HighwayPlayerSettings{
        .theme_id = readText(properties, g_theme_key),
        .pinned_palette_id = readText(properties, g_pinned_palette_key),
        .view = HighwayViewPreferences{
            .mirrored = readFlag(properties, g_mirrored_key, defaults.mirrored),
            .invert_string_order =
                readFlag(properties, g_invert_string_order_key, defaults.invert_string_order),
        },
    };
}

// Writes every setting into one opened copy of the file.
void writeTo(juce::PropertiesFile& properties, const HighwayPlayerSettings& settings)
{
    writeText(properties, g_theme_key, settings.theme_id);
    writeText(properties, g_pinned_palette_key, settings.pinned_palette_id);
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
