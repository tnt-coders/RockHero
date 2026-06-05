#include "signal_chain_block_placement.h"

#include <utility>

namespace rock_hero::editor::ui
{

// Stores already-validated assignments; factories are the only construction path.
SignalChainBlockPlacement::SignalChainBlockPlacement(
    std::vector<std::size_t> blocks, std::size_t block_count)
    : m_blocks(std::move(blocks))
    , m_block_count(block_count)
{}

// Builds the identity placement and expands capacity so construction preserves the invariant.
SignalChainBlockPlacement SignalChainBlockPlacement::compact(
    std::size_t plugin_count, std::size_t block_count)
{
    const std::size_t effective_block_count =
        block_count < plugin_count ? plugin_count : block_count;
    std::vector<std::size_t> blocks;
    blocks.reserve(plugin_count);
    for (std::size_t index = 0; index < plugin_count; ++index)
    {
        blocks.push_back(index);
    }

    return SignalChainBlockPlacement{std::move(blocks), effective_block_count};
}

// Rejects anything that is not a one-to-one mapping into the visual block range.
std::optional<SignalChainBlockPlacement> SignalChainBlockPlacement::fromIndices(
    std::vector<std::size_t> blocks, std::size_t block_count)
{
    if (blocks.size() > block_count)
    {
        return std::nullopt;
    }

    std::vector<bool> used_blocks(block_count, false);
    for (const std::size_t block : blocks)
    {
        if (block >= block_count || used_blocks[block])
        {
            return std::nullopt;
        }

        used_blocks[block] = true;
    }

    return SignalChainBlockPlacement{std::move(blocks), block_count};
}

std::size_t SignalChainBlockPlacement::pluginCount() const noexcept
{
    return m_blocks.size();
}

std::size_t SignalChainBlockPlacement::blockCount() const noexcept
{
    return m_block_count;
}

const std::vector<std::size_t>& SignalChainBlockPlacement::blocks() const noexcept
{
    return m_blocks;
}

std::optional<std::size_t> SignalChainBlockPlacement::blockForPlugin(
    std::size_t plugin_index) const noexcept
{
    if (plugin_index >= m_blocks.size())
    {
        return std::nullopt;
    }

    return m_blocks[plugin_index];
}

std::optional<std::size_t> SignalChainBlockPlacement::pluginAtBlock(
    std::size_t block_index) const noexcept
{
    for (std::size_t plugin_index = 0; plugin_index < m_blocks.size(); ++plugin_index)
    {
        if (m_blocks[plugin_index] == block_index)
        {
            return plugin_index;
        }
    }

    return std::nullopt;
}

// Counts plugins to the left of a free block to derive its linear insertion point.
std::size_t SignalChainBlockPlacement::insertionIndexForBlock(
    std::size_t block_index) const noexcept
{
    std::size_t chain_index = 0;
    for (const std::size_t block : m_blocks)
    {
        if (block < block_index)
        {
            ++chain_index;
        }
    }

    return chain_index;
}

// Counts plugins left of this plugin's block to produce its final linear chain index.
std::size_t SignalChainBlockPlacement::chainIndexForPlugin(std::size_t plugin_index) const noexcept
{
    const std::size_t plugin_block = m_blocks[plugin_index];
    std::size_t chain_index = 0;
    for (std::size_t other_index = 0; other_index < m_blocks.size(); ++other_index)
    {
        if (other_index != plugin_index && m_blocks[other_index] < plugin_block)
        {
            ++chain_index;
        }
    }

    return chain_index;
}

// Resolves either empty-space placement or occupied-block nudging for one target block.
std::optional<SignalChainBlockPlacement> SignalChainBlockPlacement::withPluginAtBlock(
    std::size_t plugin_index, std::size_t target_block,
    SignalChainBlockPlacement::PushDirection direction) const
{
    if (plugin_index >= m_blocks.size() || target_block >= m_block_count)
    {
        return std::nullopt;
    }

    // An empty or source-owned target simply relocates the dragged plugin onto it.
    const std::optional<std::size_t> occupant = pluginAtBlock(target_block);
    if (!occupant.has_value() || *occupant == plugin_index)
    {
        std::vector<std::size_t> blocks = m_blocks;
        blocks[plugin_index] = target_block;
        return fromIndices(std::move(blocks), m_block_count);
    }

    // An occupied target pushes the contiguous run toward the nearest gap on the entry side.
    std::vector<std::optional<std::size_t>> plugin_at_block(m_block_count);
    for (std::size_t index = 0; index < m_blocks.size(); ++index)
    {
        if (index != plugin_index)
        {
            plugin_at_block[m_blocks[index]] = index;
        }
    }

    std::size_t gap_block = target_block;
    while (plugin_at_block[gap_block].has_value())
    {
        if (direction == SignalChainBlockPlacement::PushDirection::Left)
        {
            if (gap_block == 0)
            {
                return std::nullopt;
            }

            --gap_block;
        }
        else
        {
            if (gap_block + 1 >= m_block_count)
            {
                return std::nullopt;
            }

            ++gap_block;
        }
    }

    std::vector<std::size_t> blocks = m_blocks;
    if (direction == SignalChainBlockPlacement::PushDirection::Left)
    {
        for (std::size_t block = gap_block; block < target_block; ++block)
        {
            const std::optional<std::size_t> shifted_plugin = plugin_at_block[block + 1];
            if (!shifted_plugin.has_value())
            {
                return std::nullopt;
            }

            blocks[*shifted_plugin] = block;
        }
    }
    else
    {
        for (std::size_t block = gap_block; block > target_block; --block)
        {
            const std::optional<std::size_t> shifted_plugin = plugin_at_block[block - 1];
            if (!shifted_plugin.has_value())
            {
                return std::nullopt;
            }

            blocks[*shifted_plugin] = block;
        }
    }

    blocks[plugin_index] = target_block;
    return fromIndices(std::move(blocks), m_block_count);
}

} // namespace rock_hero::editor::ui
