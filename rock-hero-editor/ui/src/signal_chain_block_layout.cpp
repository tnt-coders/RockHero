#include "signal_chain_block_layout.h"

#include <algorithm>
#include <utility>

namespace rock_hero::editor::ui
{

using core::SignalChainBlockPlacement;

namespace
{

// Builds a placement from the authored block indices the controller attached to each plugin, when
// they form a valid layout. This is how a saved gap arrangement (carried on a project load) is
// adopted as the visual placement instead of being collapsed to a gapless layout.
[[nodiscard]] std::optional<SignalChainBlockPlacement> placementFromPluginBlocks(
    const std::vector<core::PluginViewState>& plugins, std::size_t block_count)
{
    std::vector<std::size_t> block_indices;
    block_indices.reserve(plugins.size());
    for (const core::PluginViewState& plugin : plugins)
    {
        block_indices.push_back(plugin.block_index);
    }

    return SignalChainBlockPlacement::fromIndices(std::move(block_indices), block_count);
}

} // namespace

// Starts empty: no plugins, the minimum number of free blocks, and no preview in flight.
SignalChainBlockLayout::SignalChainBlockLayout(std::size_t minimum_block_count)
    : m_minimum_block_count(minimum_block_count)
    , m_placement(SignalChainBlockPlacement::compact(0, minimum_block_count))
{}

// Adopts the controller-derived placement without applying UI-side reconciliation policy.
void SignalChainBlockLayout::applyPlugins(const std::vector<core::PluginViewState>& plugins)
{
    const std::size_t block_count = blockCountFor(plugins.size());
    std::optional<SignalChainBlockPlacement> placement =
        placementFromPluginBlocks(plugins, block_count);
    if (placement.has_value())
    {
        m_placement = std::move(*placement);
    }
    else
    {
        m_placement = SignalChainBlockPlacement::compact(plugins.size(), block_count);
    }

    m_plugins = plugins;
    m_drag_preview.reset();
}

// Converts an empty fixed block location into the matching linear insertion index.
std::optional<std::size_t> SignalChainBlockLayout::insertionIndexForBlock(
    std::size_t block_index) const
{
    if (block_index >= blockCount() || m_placement.pluginAtBlock(block_index).has_value())
    {
        return std::nullopt;
    }

    const std::size_t chain_index = m_placement.insertionIndexForBlock(block_index);
    return chain_index;
}

// Occupied targets are resolved from the visible preview so reversing direction swaps locally.
// Empty targets still start from the cached placement so displaced plugins return home.
std::optional<SignalChainBlockLayout::DropIntent> SignalChainBlockLayout::dropIntent(
    std::size_t source_index, std::size_t target_block_index) const
{
    const SignalChainBlockPlacement& active_placement = activePlacement();
    const bool use_active_preview = m_drag_preview.has_value() &&
                                    m_drag_preview->source_index == source_index &&
                                    active_placement.pluginAtBlock(target_block_index).has_value();
    const SignalChainBlockPlacement& base_placement =
        use_active_preview ? active_placement : m_placement;
    std::optional<SignalChainBlockPlacement> moved =
        base_placement.withPluginAtBlock(source_index, target_block_index);
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
    const bool layout_changed = applyPlacement(std::move(intent->placement));
    if (destination_index == source_index)
    {
        return DropCompletion{
            .layout_changed = layout_changed,
            .move_destination_index = std::nullopt,
        };
    }

    return DropCompletion{
        .layout_changed = layout_changed,
        .move_destination_index = destination_index,
    };
}

// Removes any drag-hover preview and restores the cached controller placement.
bool SignalChainBlockLayout::clearPreview()
{
    if (!m_drag_preview.has_value())
    {
        return false;
    }

    m_drag_preview.reset();
    return true;
}

// Clears any preview that has not already been converted into cached placement.
bool SignalChainBlockLayout::clearUncommittedPreview()
{
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

const SignalChainBlockPlacement& SignalChainBlockLayout::cachedPlacement() const noexcept
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
    return activePlacement().blockForPlugin(plugin_index);
}

// Chooses the visible fixed-block count without letting loaded plugin count hide overflow.
std::size_t SignalChainBlockLayout::blockCountFor(std::size_t plugin_count) const noexcept
{
    return std::max(m_minimum_block_count, plugin_count);
}

// Applies the current preview when release lacks a fresh target intent.
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

    const bool layout_changed = applyPlacement(preview.placement);
    return DropCompletion{
        .layout_changed = layout_changed,
        .move_destination_index = preview.destination_index,
    };
}

} // namespace rock_hero::editor::ui
