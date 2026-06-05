/*!
\file signal_chain_block_placement.h
\brief Framework-free assignment of chain plugins to fixed visual blocks.
*/

#pragma once

#include <cstddef>
#include <optional>
#include <vector>

namespace rock_hero::editor::ui
{

/*! \brief Side an occupied block is pushed toward to open a gap for a dropped plugin. */
enum class BlockPushDirection
{
    /*! \brief Push occupied blocks toward lower visual block indices. */
    Left,

    /*! \brief Push occupied blocks toward higher visual block indices. */
    Right,
};

/*!
\brief Valid one-to-one assignment of chain plugins to fixed visual blocks.

Each plugin, identified by its linear chain position, owns exactly one visual block in
[0, blockCount()). Instances are valid by construction: compact() and the transform methods only
ever yield bijections, and fromIndices() rejects malformed input. Callers can therefore manipulate
placement without repeating the range and uniqueness checks the raw index vector would require.
*/
class BlockPlacement final
{
public:
    /*!
    \brief Builds the default placement where plugin i occupies block i.
    \param plugin_count Number of plugins in linear chain order.
    \param block_count Requested fixed visual blocks; expanded when smaller than plugin_count.
    \return Compact placement that fills the leading blocks in order.
    */
    [[nodiscard]] static BlockPlacement compact(std::size_t plugin_count, std::size_t block_count);

    /*!
    \brief Validates untrusted block assignments into a placement.
    \param blocks One visual block per plugin in chain order.
    \param block_count Number of fixed visual blocks.
    \return Placement when blocks is a bijection into [0, block_count), otherwise empty.
    */
    [[nodiscard]] static std::optional<BlockPlacement> fromIndices(
        std::vector<std::size_t> blocks, std::size_t block_count);

    /*! \brief Returns the number of plugins assigned to blocks. */
    [[nodiscard]] std::size_t pluginCount() const noexcept;

    /*! \brief Returns the number of fixed visual blocks the placement spans. */
    [[nodiscard]] std::size_t blockCount() const noexcept;

    /*! \brief Returns the visual block owned by each plugin in chain order. */
    [[nodiscard]] const std::vector<std::size_t>& blocks() const noexcept;

    /*!
    \brief Returns the visual block owned by a plugin.
    \param plugin_index Plugin position in chain order.
    \return Owning block, or empty when the plugin index is out of range.
    */
    [[nodiscard]] std::optional<std::size_t> blockForPlugin(
        std::size_t plugin_index) const noexcept;

    /*!
    \brief Returns the plugin occupying a visual block.
    \param block_index Fixed visual block to inspect.
    \return Plugin index at that block, or empty when the block is free.
    */
    [[nodiscard]] std::optional<std::size_t> pluginAtBlock(std::size_t block_index) const noexcept;

    /*!
    \brief Returns the linear chain index implied by a free block's left-to-right position.
    \param block_index Free visual block to convert.
    \return Number of plugins occupying lower-numbered blocks.
    */
    [[nodiscard]] std::size_t insertionIndexForBlock(std::size_t block_index) const noexcept;

    /*!
    \brief Returns the linear chain index implied by a plugin's block position.
    \param plugin_index Plugin position in chain order; must be less than pluginCount().
    \return Number of other plugins occupying lower-numbered blocks.
    */
    [[nodiscard]] std::size_t chainIndexForPlugin(std::size_t plugin_index) const noexcept;

    /*!
    \brief Moves a plugin onto a target block, pushing occupants aside when needed.
    \param plugin_index Plugin being moved.
    \param target_block Visual block receiving the plugin.
    \param direction Side occupied blocks shift toward to open a gap.
    \return Resulting placement, or empty when that side cannot absorb the move.
    */
    [[nodiscard]] std::optional<BlockPlacement> withPluginAtBlock(
        std::size_t plugin_index, std::size_t target_block, BlockPushDirection direction) const;

    /*!
    \brief Compares placements by plugin-to-block assignment and block count.
    \param other Placement compared against this one.
    \return True when both placements assign the same blocks over the same range.
    */
    [[nodiscard]] bool operator==(const BlockPlacement& other) const = default;

private:
    // Private so every placement originates from a factory that guarantees the bijection invariant.
    BlockPlacement(std::vector<std::size_t> blocks, std::size_t block_count);

    // Visual block owned by each plugin in chain order; entries are distinct and stay below
    // m_block_count.
    std::vector<std::size_t> m_blocks;

    // Number of fixed visual blocks; at least m_blocks.size().
    std::size_t m_block_count{};
};

} // namespace rock_hero::editor::ui
