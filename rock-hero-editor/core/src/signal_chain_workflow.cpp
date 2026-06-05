#include "signal_chain_workflow.h"

#include <algorithm>
#include <rock_hero/common/audio/plugin_chain_limits.h>

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
        .block_index = plugin.block_index,
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

// The audio layer carries block placement opaquely, so the editor owns its validity. A snapshot
// (from a loaded tone document or a runtime mutation) may carry a placement that is not a valid
// layout - duplicate or out-of-range blocks, or simply the runtime default. Keep it when it is a
// valid one-to-one layout within range; otherwise fall back to a gapless layout. Placement is
// presentation metadata, so an invalid set never fails anything - it just drops the gaps.
void compactInvalidBlockPlacement(std::vector<PluginViewState>& plugins)
{
    const std::size_t plugin_count = plugins.size();
    const std::size_t block_count = std::max(common::audio::max_signal_chain_plugins, plugin_count);

    bool valid = true;
    std::vector<bool> used_blocks(block_count, false);
    for (const PluginViewState& plugin : plugins)
    {
        if (plugin.block_index >= block_count || used_blocks[plugin.block_index])
        {
            valid = false;
            break;
        }

        used_blocks[plugin.block_index] = true;
    }

    if (valid)
    {
        return;
    }

    for (std::size_t index = 0; index < plugin_count; ++index)
    {
        plugins[index].block_index = index;
    }
}

} // namespace

// Applies the backend order exactly as returned; local reindexing would hide adapter drift.
void SignalChainWorkflow::replaceSnapshot(common::audio::PluginChainSnapshot snapshot)
{
    m_plugins = makePluginViewStates(snapshot.plugins);
    compactInvalidBlockPlacement(m_plugins);
    if (!hasInsertCapacity() ||
        (m_pending_insertion_index.has_value() && *m_pending_insertion_index > appendIndex()))
    {
        clearPendingInsertion();
    }
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
    if (!hasInsertCapacity() || chain_index > appendIndex())
    {
        return false;
    }

    m_pending_insertion_index = chain_index;
    return true;
}

// Uses the current end of the chain as the next browser insertion target.
void SignalChainWorkflow::requestAppend()
{
    if (!hasInsertCapacity())
    {
        clearPendingInsertion();
        return;
    }

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
        if (!hasInsertCapacity())
        {
            return std::nullopt;
        }

        return appendIndex();
    }

    if (!hasInsertCapacity() || *m_pending_insertion_index > appendIndex())
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

bool SignalChainWorkflow::hasInsertCapacity() const noexcept
{
    return m_plugins.size() < common::audio::max_signal_chain_plugins;
}

std::size_t SignalChainWorkflow::appendIndex() const noexcept
{
    return m_plugins.size();
}

const std::vector<PluginViewState>& SignalChainWorkflow::plugins() const noexcept
{
    return m_plugins;
}

// Stores the editor-authored visual placement reported by the view. The view owns the placement
// algebra and gaps; the workflow holds the committed result so it persists on capture. The vector
// is aligned to the current chain order; a size mismatch is ignored as a stale report from before
// the latest snapshot. A valid one-to-one layout is the core invariant, so an invalid report
// (duplicate or out-of-range blocks, e.g. from a future undo/redo path) is compacted rather than
// stored as-is.
void SignalChainWorkflow::setBlockPlacement(const std::vector<std::size_t>& block_indices)
{
    if (block_indices.size() != m_plugins.size())
    {
        return;
    }

    for (std::size_t index = 0; index < m_plugins.size(); ++index)
    {
        m_plugins[index].block_index = block_indices[index];
    }

    compactInvalidBlockPlacement(m_plugins);
}

std::vector<std::size_t> SignalChainWorkflow::blockIndices() const
{
    std::vector<std::size_t> block_indices;
    block_indices.reserve(m_plugins.size());
    for (const PluginViewState& plugin : m_plugins)
    {
        block_indices.push_back(plugin.block_index);
    }

    return block_indices;
}

} // namespace rock_hero::editor::core
