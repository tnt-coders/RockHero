#include "signal_chain_block_layout.h"

#include <algorithm>
#include <numeric>
#include <utility>

namespace rock_hero::editor::ui
{

namespace
{

// Compares stable plugin IDs so label or manufacturer updates do not collapse visual gaps.
[[nodiscard]] bool hasSamePluginOrder(
    const std::vector<core::PluginViewState>& previous_plugins,
    const std::vector<core::PluginViewState>& next_plugins)
{
    if (previous_plugins.size() != next_plugins.size())
    {
        return false;
    }

    for (std::size_t index = 0; index < previous_plugins.size(); ++index)
    {
        if (previous_plugins[index].instance_id != next_plugins[index].instance_id)
        {
            return false;
        }
    }

    return true;
}

// Preserves gaps only when the same plugin IDs still occupy the same linear positions. When the
// order is unchanged the previous placement is already valid for the new state, because block
// count depends solely on plugin count.
[[nodiscard]] SignalChainBlockPlacement reconciledPlacement(
    const std::vector<core::PluginViewState>& previous_plugins,
    const SignalChainBlockPlacement& previous_placement,
    const std::vector<core::PluginViewState>& next_plugins, std::size_t block_count)
{
    if (hasSamePluginOrder(previous_plugins, next_plugins))
    {
        return previous_placement;
    }

    return SignalChainBlockPlacement::compact(next_plugins.size(), block_count);
}

// Applies a just-requested insert to the selected empty block after controller state refreshes.
[[nodiscard]] std::optional<SignalChainBlockPlacement> placementAfterInsert(
    const std::vector<core::PluginViewState>& previous_plugins,
    const SignalChainBlockPlacement& previous_placement,
    const std::vector<core::PluginViewState>& next_plugins, std::size_t insert_chain_index,
    std::size_t insert_block_index, std::size_t block_count)
{
    const std::size_t previous_plugin_count = previous_plugins.size();
    const std::size_t next_plugin_count = next_plugins.size();
    if (next_plugin_count != previous_plugin_count + 1 ||
        insert_chain_index > previous_plugin_count || insert_block_index >= block_count ||
        previous_placement.pluginAtBlock(insert_block_index).has_value())
    {
        return std::nullopt;
    }

    std::vector<std::size_t> blocks;
    blocks.reserve(next_plugin_count);
    for (std::size_t next_index = 0; next_index < next_plugin_count; ++next_index)
    {
        if (next_index == insert_chain_index)
        {
            blocks.push_back(insert_block_index);
            continue;
        }

        const std::size_t previous_index =
            next_index < insert_chain_index ? next_index : next_index - 1;
        if (next_plugins[next_index].instance_id != previous_plugins[previous_index].instance_id)
        {
            return std::nullopt;
        }

        blocks.push_back(previous_placement.blocks()[previous_index]);
    }

    return SignalChainBlockPlacement::fromIndices(std::move(blocks), block_count);
}

// Keeps the committed preview placement only when the backend confirms the same reordered chain.
[[nodiscard]] std::optional<SignalChainBlockPlacement> placementAfterCommittedPreview(
    const SignalChainBlockPlacement& preview_placement,
    const std::vector<core::PluginViewState>& previous_plugins,
    const std::vector<core::PluginViewState>& next_plugins, std::size_t block_count)
{
    const std::size_t plugin_count = previous_plugins.size();
    if (plugin_count != next_plugins.size() || preview_placement.pluginCount() != plugin_count ||
        preview_placement.blockCount() != block_count)
    {
        return std::nullopt;
    }

    // Order previous plugins by their preview block so the result tracks the dragged reordering.
    std::vector<std::size_t> preview_order(plugin_count);
    std::iota(preview_order.begin(), preview_order.end(), std::size_t{0});
    const std::vector<std::size_t>& preview_blocks = preview_placement.blocks();
    std::ranges::sort(preview_order, [&preview_blocks](std::size_t lhs, std::size_t rhs) {
        if (preview_blocks[lhs] != preview_blocks[rhs])
        {
            return preview_blocks[lhs] < preview_blocks[rhs];
        }

        return lhs < rhs;
    });

    std::vector<std::size_t> blocks(plugin_count);
    for (std::size_t next_index = 0; next_index < plugin_count; ++next_index)
    {
        const std::size_t previous_index = preview_order[next_index];
        if (next_plugins[next_index].instance_id != previous_plugins[previous_index].instance_id)
        {
            return std::nullopt;
        }

        blocks[next_index] = preview_blocks[previous_index];
    }

    return SignalChainBlockPlacement::fromIndices(std::move(blocks), block_count);
}

} // namespace

// Starts empty: no plugins, the minimum number of free blocks, no insertion or preview in flight.
SignalChainBlockLayout::SignalChainBlockLayout(std::size_t minimum_block_count)
    : m_minimum_block_count(minimum_block_count)
    , m_placement(SignalChainBlockPlacement::compact(0, minimum_block_count))
{}

// Reconciles controller state with any pending insert or committed preview held by the view.
void SignalChainBlockLayout::applyPlugins(const std::vector<core::PluginViewState>& plugins)
{
    const std::size_t block_count = blockCountFor(plugins.size());
    SignalChainBlockPlacement next_placement =
        reconciledPlacement(m_plugins, m_placement, plugins, block_count);
    bool keep_committed_preview = false;

    if (m_pending_insert.has_value())
    {
        std::optional<SignalChainBlockPlacement> inserted = placementAfterInsert(
            m_plugins,
            m_placement,
            plugins,
            m_pending_insert->chain_index,
            m_pending_insert->block_index,
            block_count);
        if (inserted.has_value())
        {
            next_placement = std::move(*inserted);
            m_pending_insert.reset();
        }
        else if (!hasSamePluginOrder(m_plugins, plugins))
        {
            m_pending_insert.reset();
        }
    }

    if (m_drag_preview.has_value() && m_drag_preview->committed)
    {
        std::optional<SignalChainBlockPlacement> committed = placementAfterCommittedPreview(
            m_drag_preview->placement, m_plugins, plugins, block_count);
        if (committed.has_value())
        {
            next_placement = std::move(*committed);
        }
        else if (hasSamePluginOrder(m_plugins, plugins))
        {
            // Same order at the same counts keeps the preview valid until the move lands.
            next_placement = m_drag_preview->placement;
            keep_committed_preview = true;
        }
    }

    m_plugins = plugins;
    m_placement = std::move(next_placement);
    if (!keep_committed_preview)
    {
        m_drag_preview.reset();
    }
}

// Converts an empty fixed block location into the matching linear insertion index.
std::optional<std::size_t> SignalChainBlockLayout::beginInsertAtBlock(std::size_t block_index)
{
    if (block_index >= blockCount() || m_placement.pluginAtBlock(block_index).has_value())
    {
        return std::nullopt;
    }

    const std::size_t chain_index = m_placement.insertionIndexForBlock(block_index);
    m_pending_insert = PendingInsert{
        .chain_index = chain_index,
        .block_index = block_index,
    };
    return chain_index;
}

// Resolves a target block against the current authoritative placement.
std::optional<SignalChainBlockLayout::DropIntent> SignalChainBlockLayout::dropIntent(
    std::size_t source_index, std::size_t target_block_index) const
{
    std::optional<SignalChainBlockPlacement> moved =
        m_placement.withPluginAtBlock(source_index, target_block_index);
    if (!moved.has_value())
    {
        return std::nullopt;
    }

    return DropIntent{
        .destination_index = moved->chainIndexForPlugin(source_index),
        .placement = std::move(*moved),
    };
}

// Checks whether the target block can produce a concrete move or no-op placement.
bool SignalChainBlockLayout::canReceiveDrop(
    std::size_t source_index, std::size_t target_block_index) const
{
    return dropIntent(source_index, target_block_index).has_value();
}

// Stores a drag-hover preview and reports whether the visible placement changed.
bool SignalChainBlockLayout::previewMove(std::size_t source_index, DropIntent intent)
{
    if (source_index >= m_placement.pluginCount() ||
        intent.destination_index >= m_placement.pluginCount() ||
        intent.placement.pluginCount() != m_placement.pluginCount() ||
        intent.placement.blockCount() != blockCount())
    {
        return clearPreview();
    }

    DragPreview next_preview{
        .source_index = source_index,
        .destination_index = intent.destination_index,
        .placement = std::move(intent.placement),
        .committed = false,
    };
    if (m_drag_preview.has_value() && m_drag_preview->source_index == next_preview.source_index &&
        m_drag_preview->destination_index == next_preview.destination_index &&
        m_drag_preview->placement == next_preview.placement)
    {
        return false;
    }

    m_drag_preview = std::move(next_preview);
    return true;
}

// Converts a target drop or last valid preview into the action the view must perform.
SignalChainBlockLayout::DropCompletion SignalChainBlockLayout::completeDrop(
    std::size_t source_index, std::optional<DropIntent> intent)
{
    if (!intent.has_value())
    {
        std::optional<DropCompletion> preview_completion = completeCurrentPreviewDrop(source_index);
        if (preview_completion.has_value())
        {
            return *preview_completion;
        }

        return DropCompletion{
            .layout_changed = clearPreview(),
            .move_destination_index = std::nullopt,
        };
    }

    const std::size_t destination_index = intent->destination_index;
    SignalChainBlockPlacement placement = std::move(intent->placement);
    bool layout_changed = previewMove(
        source_index, DropIntent{.destination_index = destination_index, .placement = placement});
    if (destination_index == source_index)
    {
        layout_changed = applyPlacement(std::move(placement)) || layout_changed;
        return DropCompletion{
            .layout_changed = layout_changed,
            .move_destination_index = std::nullopt,
        };
    }

    commitPreview();
    return DropCompletion{
        .layout_changed = layout_changed,
        .move_destination_index = destination_index,
    };
}

// Removes any drag-hover preview and restores authoritative chain layout.
bool SignalChainBlockLayout::clearPreview()
{
    if (!m_drag_preview.has_value())
    {
        return false;
    }

    m_drag_preview.reset();
    return true;
}

// Leaves committed previews alone so source mouse-up does not snap back a valid drop.
bool SignalChainBlockLayout::clearUncommittedPreview()
{
    if (m_drag_preview.has_value() && m_drag_preview->committed)
    {
        return false;
    }

    return clearPreview();
}

// Applies a fixed-slot placement that leaves the plugin chain's linear order unchanged.
bool SignalChainBlockLayout::applyPlacement(SignalChainBlockPlacement placement)
{
    if (placement.pluginCount() != m_placement.pluginCount() ||
        placement.blockCount() != blockCount())
    {
        return clearPreview();
    }

    const bool layout_changed = m_drag_preview.has_value() || m_placement != placement;
    m_placement = std::move(placement);
    m_drag_preview.reset();
    return layout_changed;
}

const SignalChainBlockPlacement& SignalChainBlockLayout::committedPlacement() const noexcept
{
    return m_placement;
}

// Exposes the placement the view should currently render, including transient drag previews.
const SignalChainBlockPlacement& SignalChainBlockLayout::activePlacement() const noexcept
{
    return m_drag_preview.has_value() ? m_drag_preview->placement : m_placement;
}

std::size_t SignalChainBlockLayout::blockCount() const noexcept
{
    return m_placement.blockCount();
}

std::optional<std::size_t> SignalChainBlockLayout::blockForPlugin(
    std::size_t plugin_index) const noexcept
{
    return m_placement.blockForPlugin(plugin_index);
}

// Chooses the visible fixed-block count without letting loaded plugin count hide overflow.
std::size_t SignalChainBlockLayout::blockCountFor(std::size_t plugin_count) const noexcept
{
    return std::max(m_minimum_block_count, plugin_count);
}

// Commits the current preview so release and same-order refreshes keep the fixed-slot layout.
void SignalChainBlockLayout::commitPreview()
{
    if (m_drag_preview.has_value())
    {
        m_placement = m_drag_preview->placement;
        m_drag_preview->committed = true;
    }
}

// Applies the current preview when release lands on a side that cannot produce a new preview.
std::optional<SignalChainBlockLayout::DropCompletion> SignalChainBlockLayout::
    completeCurrentPreviewDrop(std::size_t source_index)
{
    if (!m_drag_preview.has_value() || m_drag_preview->source_index != source_index)
    {
        return std::nullopt;
    }

    const DragPreview preview = *m_drag_preview;
    if (preview.destination_index == source_index)
    {
        return DropCompletion{
            .layout_changed = applyPlacement(preview.placement),
            .move_destination_index = std::nullopt,
        };
    }

    commitPreview();
    return DropCompletion{
        .layout_changed = false,
        .move_destination_index = preview.destination_index,
    };
}

} // namespace rock_hero::editor::ui
