#include "plugin_state_memento.h"

#include "shared/audio_path_util.h"

#include <limits>
#include <memory>
#include <tracktion_engine/tracktion_engine.h>

namespace rock_hero::common::audio
{

// Parses a memento payload into Tracktion's plugin-state tree and rejects non-external states.
[[nodiscard]] std::expected<juce::ValueTree, PluginHostError> pluginStateTreeFromMemento(
    const PluginInstanceState& state)
{
    if (state.opaque_data.empty())
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginStateRestoreFailed,
            "Plugin state is empty",
        }};
    }

    if (state.opaque_data.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginStateRestoreFailed,
            "Plugin state is too large to parse",
        }};
    }

    const std::string xml_text = stringFromBytes(state.opaque_data);
    const juce::String xml_string =
        juce::String::fromUTF8(xml_text.data(), static_cast<int>(xml_text.size()));
    const std::unique_ptr<juce::XmlElement> xml = juce::parseXML(xml_string);
    if (xml == nullptr)
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginStateRestoreFailed,
            "Could not parse plugin state",
        }};
    }

    juce::ValueTree plugin_state = juce::ValueTree::fromXml(*xml);
    if (!plugin_state.isValid() || !plugin_state.hasType(tracktion::IDs::PLUGIN) ||
        plugin_state[tracktion::IDs::type].toString() != tracktion::ExternalPlugin::xmlTypeName)
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginStateRestoreFailed,
            "Plugin state is not an external plugin ValueTree",
        }};
    }

    return plugin_state;
}

// Reads the runtime instance id encoded in a captured Tracktion plugin tree, when one exists.
[[nodiscard]] std::string pluginInstanceIdFromState(const juce::ValueTree& plugin_state)
{
    const tracktion::EditItemID item_id = tracktion::EditItemID::fromID(plugin_state);
    return item_id.isValid() ? item_id.toString().toStdString() : std::string{};
}

} // namespace rock_hero::common::audio
