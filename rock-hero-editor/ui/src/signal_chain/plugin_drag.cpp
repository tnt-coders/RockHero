#include "plugin_drag.h"

#include <string>

namespace rock_hero::editor::ui
{

namespace
{

constexpr const char* g_plugin_drag_prefix{"rockhero.signal-chain.plugin:"};
constexpr int g_plugin_drag_prefix_length{static_cast<int>(
    std::char_traits<char>::length(g_plugin_drag_prefix))};

} // namespace

juce::String makePluginDragDescription(const core::PluginViewState& plugin)
{
    juce::String description{g_plugin_drag_prefix};
    description += juce::String{std::to_string(plugin.chain_index)};
    description += ":";
    description += juce::String{plugin.instance_id};
    return description;
}

std::optional<DraggedPlugin> parsePluginDragDescription(const juce::var& description)
{
    if (!description.isString())
    {
        return std::nullopt;
    }

    const juce::String text = description.toString();
    if (!text.startsWith(g_plugin_drag_prefix))
    {
        return std::nullopt;
    }

    const juce::String body = text.substring(g_plugin_drag_prefix_length);
    const int separator = body.indexOfChar(':');
    if (separator <= 0)
    {
        return std::nullopt;
    }

    const juce::String source_index_text = body.substring(0, separator);
    if (!source_index_text.containsOnly("0123456789"))
    {
        return std::nullopt;
    }

    const juce::String instance_id = body.substring(separator + 1);
    if (instance_id.isEmpty())
    {
        return std::nullopt;
    }

    return DraggedPlugin{
        .instance_id = instance_id.toStdString(),
        .source_index = static_cast<std::size_t>(source_index_text.getIntValue()),
    };
}

} // namespace rock_hero::editor::ui
