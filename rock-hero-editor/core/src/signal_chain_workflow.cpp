#include "signal_chain_workflow.h"

#include <algorithm>

namespace rock_hero::editor::core
{

namespace
{

// Converts a backend snapshot entry into the view model rendered by the signal-chain panel.
[[nodiscard]] PluginViewState makePluginViewState(const common::audio::PluginChainEntry& plugin)
{
    return PluginViewState{
        .instance_id = plugin.instance_id,
        .plugin_id = plugin.plugin_id,
        .name = plugin.name,
        .manufacturer = plugin.manufacturer,
        .format_name = plugin.format_name,
        .chain_index = plugin.chain_index,
    };
}

// Converts authoritative backend chain entries into editor view rows.
[[nodiscard]] std::vector<PluginViewState> makePluginViewStates(
    const std::vector<common::audio::PluginChainEntry>& plugins)
{
    std::vector<PluginViewState> states;
    states.reserve(plugins.size());
    for (const common::audio::PluginChainEntry& plugin : plugins)
    {
        states.push_back(makePluginViewState(plugin));
    }
    return states;
}

} // namespace

// Applies the backend order exactly as returned; local reindexing would hide adapter drift.
void SignalChainWorkflow::replaceSnapshot(common::audio::PluginChainSnapshot snapshot)
{
    m_plugins = makePluginViewStates(snapshot.plugins);
}

// Closes the signal-chain editing state when a project or live rig leaves the editor.
void SignalChainWorkflow::clear()
{
    m_plugins.clear();
    clearPendingInsertion();
}

// Stores a slot emitted by an insertion gap while rejecting stale UI slots.
bool SignalChainWorkflow::requestInsertAt(std::size_t chain_index)
{
    if (chain_index > appendIndex())
    {
        return false;
    }

    m_pending_insertion_index = chain_index;
    return true;
}

// Uses the current end of the chain as the next browser insertion target.
void SignalChainWorkflow::requestAppend()
{
    m_pending_insertion_index = appendIndex();
}

// Clears stale browser target state when the browser closes or a mutation succeeds.
void SignalChainWorkflow::clearPendingInsertion() noexcept
{
    m_pending_insertion_index.reset();
}

std::optional<std::size_t> SignalChainWorkflow::insertionIndexForSelection() const noexcept
{
    if (!m_pending_insertion_index.has_value())
    {
        return appendIndex();
    }

    if (*m_pending_insertion_index > appendIndex())
    {
        return std::nullopt;
    }

    return m_pending_insertion_index;
}

bool SignalChainWorkflow::containsInstance(std::string_view instance_id) const noexcept
{
    return chainIndexForInstance(instance_id).has_value();
}

std::optional<std::size_t> SignalChainWorkflow::chainIndexForInstance(
    std::string_view instance_id) const noexcept
{
    const auto plugin = std::ranges::find_if(m_plugins, [instance_id](const PluginViewState& item) {
        return item.instance_id == instance_id;
    });
    if (plugin == m_plugins.end())
    {
        return std::nullopt;
    }

    return plugin->chain_index;
}

bool SignalChainWorkflow::hasPlugins() const noexcept
{
    return !m_plugins.empty();
}

std::size_t SignalChainWorkflow::appendIndex() const noexcept
{
    return m_plugins.size();
}

const std::vector<PluginViewState>& SignalChainWorkflow::plugins() const noexcept
{
    return m_plugins;
}

} // namespace rock_hero::editor::core
