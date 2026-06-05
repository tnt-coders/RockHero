#include "signal_chain_workflow.h"

#include <algorithm>
#include <optional>
#include <rock_hero/common/audio/plugin_chain_limits.h>
#include <rock_hero/editor/core/signal_chain_block_placement.h>
#include <utility>

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

// Fixed blocks have a product cap minimum, but the value type still allows recovery from any
// oversized test or future migrated chain.
[[nodiscard]] std::size_t blockCountFor(std::size_t plugin_count) noexcept
{
    return std::max(common::audio::max_signal_chain_plugins, plugin_count);
}

// Extracts the raw opaque block indices carried by a plugin snapshot.
[[nodiscard]] std::vector<std::size_t> blockIndicesFor(const std::vector<PluginViewState>& plugins)
{
    std::vector<std::size_t> block_indices;
    block_indices.reserve(plugins.size());
    for (const PluginViewState& plugin : plugins)
    {
        block_indices.push_back(plugin.block_index);
    }

    return block_indices;
}

// Converts raw block indices into the canonical placement value, compacting malformed data.
[[nodiscard]] SignalChainBlockPlacement normalizedPlacement(
    std::vector<std::size_t> block_indices, std::size_t plugin_count)
{
    const std::size_t block_count = blockCountFor(plugin_count);
    std::optional<SignalChainBlockPlacement> placement =
        SignalChainBlockPlacement::fromIndices(std::move(block_indices), block_count);
    if (placement.has_value())
    {
        return *placement;
    }

    return SignalChainBlockPlacement::compact(plugin_count, block_count);
}

// Builds a placement from snapshot/plugin rows only when those rows already carry a valid layout.
[[nodiscard]] std::optional<SignalChainBlockPlacement> placementFromPluginBlocks(
    const std::vector<PluginViewState>& plugins, std::size_t block_count)
{
    return SignalChainBlockPlacement::fromIndices(blockIndicesFor(plugins), block_count);
}

// Applies a known-valid placement back onto mutable plugin rows for persistence and view state.
void applyPlacement(
    std::vector<PluginViewState>& plugins, const SignalChainBlockPlacement& placement)
{
    const std::vector<std::size_t>& blocks = placement.blocks();
    for (std::size_t index = 0; index < plugins.size(); ++index)
    {
        plugins[index].block_index = blocks[index];
    }
}

// Recognizes a pure deletion snapshot and preserves exact authored blocks for survivors. This
// runs before accepting snapshot block indices because runtime mutation snapshots may carry the
// backend's default zero/compact values rather than the editor-authored placement.
[[nodiscard]] std::optional<SignalChainBlockPlacement> placementAfterRemovingPlugins(
    const std::vector<PluginViewState>& previous_plugins,
    const std::vector<PluginViewState>& next_plugins, std::size_t block_count)
{
    if (next_plugins.size() >= previous_plugins.size())
    {
        return std::nullopt;
    }

    std::vector<std::size_t> blocks;
    blocks.reserve(next_plugins.size());
    std::size_t previous_index = 0;
    for (const PluginViewState& next_plugin : next_plugins)
    {
        while (previous_index < previous_plugins.size() &&
               previous_plugins[previous_index].instance_id != next_plugin.instance_id)
        {
            ++previous_index;
        }

        if (previous_index == previous_plugins.size())
        {
            return std::nullopt;
        }

        blocks.push_back(previous_plugins[previous_index].block_index);
        ++previous_index;
    }

    return SignalChainBlockPlacement::fromIndices(std::move(blocks), block_count);
}

// Recognizes a single-plugin insertion and places the new plugin at the editor-chosen block while
// surviving plugins keep their authored blocks. Mirrors placementAfterRemovingPlugins because the
// backend insertion snapshot carries default blocks, not the editor placement.
[[nodiscard]] std::optional<SignalChainBlockPlacement> placementAfterInserting(
    const std::vector<PluginViewState>& previous_plugins,
    const std::vector<PluginViewState>& next_plugins, std::size_t insert_block,
    std::size_t block_count)
{
    if (next_plugins.size() != previous_plugins.size() + 1)
    {
        return std::nullopt;
    }

    std::vector<std::size_t> blocks;
    blocks.reserve(next_plugins.size());
    std::size_t previous_index = 0;
    bool inserted = false;
    for (const PluginViewState& next_plugin : next_plugins)
    {
        if (previous_index < previous_plugins.size() &&
            previous_plugins[previous_index].instance_id == next_plugin.instance_id)
        {
            blocks.push_back(previous_plugins[previous_index].block_index);
            ++previous_index;
            continue;
        }

        if (inserted)
        {
            return std::nullopt;
        }

        blocks.push_back(insert_block);
        inserted = true;
    }

    if (!inserted || previous_index != previous_plugins.size())
    {
        return std::nullopt;
    }

    return SignalChainBlockPlacement::fromIndices(std::move(blocks), block_count);
}

// Chooses the canonical placement for a fresh backend snapshot. Valid snapshot placement wins for
// project loads, captures, and future undo restores; pure insertions and removals keep survivor
// blocks when the backend has no placement model of its own.
[[nodiscard]] SignalChainBlockPlacement placementForSnapshot(
    const std::vector<PluginViewState>& previous_plugins,
    const std::vector<PluginViewState>& next_plugins,
    std::optional<std::size_t> pending_insert_block)
{
    const std::size_t block_count = blockCountFor(next_plugins.size());
    if (pending_insert_block.has_value())
    {
        if (std::optional<SignalChainBlockPlacement> inserted = placementAfterInserting(
                previous_plugins, next_plugins, *pending_insert_block, block_count);
            inserted.has_value())
        {
            return *inserted;
        }
    }

    if (std::optional<SignalChainBlockPlacement> removed =
            placementAfterRemovingPlugins(previous_plugins, next_plugins, block_count);
        removed.has_value())
    {
        return *removed;
    }

    if (std::optional<SignalChainBlockPlacement> adopted =
            placementFromPluginBlocks(next_plugins, block_count);
        adopted.has_value())
    {
        return *adopted;
    }

    if (hasSamePluginOrder(previous_plugins, next_plugins))
    {
        if (std::optional<SignalChainBlockPlacement> preserved =
                placementFromPluginBlocks(previous_plugins, block_count);
            preserved.has_value())
        {
            return *preserved;
        }
    }

    return SignalChainBlockPlacement::compact(next_plugins.size(), block_count);
}

} // namespace

// Applies the backend order exactly as returned; local reindexing would hide adapter drift.
void SignalChainWorkflow::replaceSnapshot(common::audio::PluginChainSnapshot snapshot)
{
    std::vector<PluginViewState> next_plugins = makePluginViewStates(snapshot.plugins);
    const SignalChainBlockPlacement placement =
        placementForSnapshot(m_plugins, next_plugins, m_pending_insert_block);
    applyPlacement(next_plugins, placement);
    m_plugins = std::move(next_plugins);
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
    // An append has no editor-chosen block; the snapshot uses its default placement.
    m_pending_insert_block.reset();
}

// Records the block a pending specific-slot insert should occupy; used by the next snapshot.
void SignalChainWorkflow::setPendingInsertBlock(std::optional<std::size_t> block_index) noexcept
{
    m_pending_insert_block = block_index;
}

// Clears stale browser target state when the browser closes or a mutation succeeds.
void SignalChainWorkflow::clearPendingInsertion() noexcept
{
    m_pending_insertion_index.reset();
    m_pending_insert_block.reset();
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
bool SignalChainWorkflow::setBlockPlacement(const std::vector<std::size_t>& block_indices)
{
    if (block_indices.size() != m_plugins.size())
    {
        return false;
    }

    const SignalChainBlockPlacement placement =
        normalizedPlacement(block_indices, m_plugins.size());
    if (placement.blocks() == blockIndices())
    {
        return false;
    }

    applyPlacement(m_plugins, placement);
    return true;
}

std::vector<std::size_t> SignalChainWorkflow::blockIndices() const
{
    return blockIndicesFor(m_plugins);
}

} // namespace rock_hero::editor::core
